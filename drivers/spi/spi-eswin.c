// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN BootSpi Driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Xuxiang <xuxiang@eswincomputing.com>
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/pm_runtime.h>
#include "../mtd/mtdcore.h"
#include "../mtd/spi-nor/core.h"
#include "spi-eswin.h"

#define DRIVER_NAME "eswin_spi_mmio"

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_data/dma-dw.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "spi-eswin.h"

struct eswin_spi_mmio {
	struct eswin_spi  esws;
	struct clk     *cfg_clk;
	struct clk     *clk;
	void           *priv;
	struct reset_control *rstc;
	bool wp_status;
};

static DEFINE_MUTEX(wp_lock);

#define ES_SPI_RX_BUSY		0
#define ES_SPI_RX_BURST_LEVEL	16
#define ES_SPI_TX_BUSY		1
#define ES_SPI_TX_BURST_LEVEL	16

static int esw_spi_tx_dma_wait(struct eswin_spi  *esws, unsigned int len, u32 speed)
{
	unsigned long long ms = wait_for_completion_timeout(&esws->tx_dma_completion,
					 msecs_to_jiffies(1000));
	if (!ms) {
		dev_err(&esws->host->dev,"TX DMA transaction timed out\n");
		dump_stack();
		dmaengine_terminate_sync(esws->txchan);
		return -ETIMEDOUT;
	}
	return 0;
}

static int esw_spi_rx_dma_wait(struct eswin_spi  *esws, unsigned int len, u32 speed)
{
	unsigned long long ms = wait_for_completion_timeout(&esws->rx_dma_completion,
					 msecs_to_jiffies(1000));
	if (!ms) {
		dev_err(&esws->host->dev,"RX DMA transaction timed out\n");
		dump_stack();
		dmaengine_terminate_sync(esws->rxchan);
		return -ETIMEDOUT;
	}
	return 0;
}

static void esw_spi_dma_tx_done(void *arg)
{
	struct eswin_spi  *esws = arg;
	complete(&esws->tx_dma_completion);
}

static int esw_spi_dma_config_tx(struct eswin_spi  *esws)
{
	struct dma_slave_config txconf;

	memset(&txconf, 0, sizeof(txconf));
	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = esws->flash_paddr;
	txconf.src_maxburst = 64;
	txconf.dst_maxburst = 64;
	txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.device_fc = false;

	return dmaengine_slave_config(esws->txchan, &txconf);
}

int esw_spi_dma_submit_tx(struct eswin_spi  *esws, size_t size)
{
	struct dma_async_tx_descriptor *txdesc;
	dma_cookie_t cookie;
	struct scatterlist tx_tmp;
	int ret;

	size = (size + SPI_FLASH_WR_WORD -1) / SPI_FLASH_WR_WORD * SPI_FLASH_WR_WORD;

	sg_init_table(&tx_tmp, 1);
	sg_dma_address(&tx_tmp) = esws->tx_dma_phys;
	sg_dma_len(&tx_tmp) = size;

	txdesc = dmaengine_prep_slave_sg(esws->txchan, &tx_tmp, 1,
					 DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		return -ENOMEM;

	txdesc->callback = esw_spi_dma_tx_done;
	txdesc->callback_param = esws;

	cookie = dmaengine_submit(txdesc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(esws->txchan);
		return ret;
	}

	return 0;
}

static void esw_spi_dma_rx_done(void *arg)
{
	struct eswin_spi  *esws = arg;
	complete(&esws->rx_dma_completion);
}

static int esw_spi_dma_config_rx(struct eswin_spi  *esws)
{
	struct dma_slave_config rxconf;
	memset(&rxconf, 0, sizeof(rxconf));
	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = esws->flash_paddr;
	rxconf.src_maxburst = 64;
	rxconf.dst_maxburst = 64;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.device_fc = false;

	return dmaengine_slave_config(esws->rxchan, &rxconf);
}

static int esw_spi_dma_submit_rx(struct eswin_spi  *esws, size_t size)
{
	struct scatterlist rx_tmp;
	struct dma_async_tx_descriptor *rxdesc;
	dma_cookie_t cookie;
	int ret;

	size = (size + SPI_FLASH_WR_WORD -1) / SPI_FLASH_WR_WORD  * SPI_FLASH_WR_WORD;

	sg_init_table(&rx_tmp, 1);
	sg_dma_address(&rx_tmp) = esws->rx_dma_phys;
	sg_dma_len(&rx_tmp) = size;

	rxdesc = dmaengine_prep_slave_sg(esws->rxchan, &rx_tmp, 1,
					 DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		return -ENOMEM;

	rxdesc->callback = esw_spi_dma_rx_done;
	rxdesc->callback_param = esws;

	cookie = dmaengine_submit(rxdesc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(esws->rxchan);
		return ret;
	}

	return 0;
}


static int esw_spi_dma_init_generic(struct device *dev, struct eswin_spi  *esws)
{
	int ret;
	esws->rxchan = dma_request_chan(dev, "rx");
	if (IS_ERR(esws->rxchan)) {
		ret = PTR_ERR(esws->rxchan);
		esws->rxchan = NULL;
		ret = -EPROBE_DEFER;
		goto err_exit;
	}

	esws->txchan = dma_request_chan(dev, "tx");
	if (IS_ERR(esws->txchan)) {
		ret = PTR_ERR(esws->txchan);
		esws->txchan = NULL;
		ret = -EPROBE_DEFER;
		goto free_rxchan;
	}

	esws->rx_dma_buf = dma_alloc_coherent(esws->rxchan->device->dev, esws->fifo_len, &esws->rx_dma_phys, GFP_KERNEL);
	if (!esws->rx_dma_buf) {
		ret = -ENOMEM;
		goto free_txchan;
	}

	esws->tx_dma_buf = dma_alloc_coherent(esws->txchan->device->dev, esws->fifo_len, &esws->tx_dma_phys, GFP_KERNEL);
	if (!esws->rx_dma_buf) {
		ret = -ENOMEM;
		goto free_rxbuf;
	}

	esws->dma_mapped = 1;
	init_completion(&esws->tx_dma_completion);
	init_completion(&esws->rx_dma_completion);

	pr_err("spi-dma: %s init success\n", __func__);

	return 0;

free_rxbuf:
	dma_free_coherent(esws->rxchan->device->dev, esws->fifo_len,
				  esws->rx_dma_buf, esws->rx_dma_phys);
	esws->rx_dma_buf = NULL;
free_txchan:
	dma_release_channel(esws->txchan);
	esws->txchan = NULL;
free_rxchan:
	dma_release_channel(esws->rxchan);
	esws->rxchan = NULL;
err_exit:
	return ret;
}

static void esw_spi_dma_exit(struct eswin_spi  *esws)
{

	if(esws->rx_dma_buf) {
		dma_free_coherent(esws->rxchan->device->dev, esws->fifo_len,
					esws->rx_dma_buf, esws->rx_dma_phys);
		esws->rx_dma_buf = NULL;
	}

	if(esws->tx_dma_buf) {
		dma_free_coherent(esws->txchan->device->dev, esws->fifo_len,
					esws->tx_dma_buf, esws->tx_dma_phys);
		esws->tx_dma_buf = NULL;
	}

	if (esws->txchan) {
		dmaengine_terminate_sync(esws->txchan);
		dma_release_channel(esws->txchan);
	}

	if (esws->rxchan) {
		dmaengine_terminate_sync(esws->rxchan);
		dma_release_channel(esws->rxchan);
	}

}

static ssize_t wp_show(struct device *dev,
                       struct device_attribute *attr, char *buf)
{
	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", eswmmio->wp_status ? "enabled" : "disabled");
}

static int spi_nor_set_write_protect(struct eswin_spi  *esws, struct spi_nor *nor, bool enable)
{
    int ret = 0;
    u8 request_register_data, register_data;
	u16 lock_reg;

	dev_info(&esws->host->dev,"Bootspi %s flash write protect!\n", enable?"enable":"disable");

	//Update status register1
    struct spi_mem_op srr_op = SPI_MEM_OP(
        SPI_MEM_OP_CMD(SPINOR_OP_RDSR, 1),              // Read Status Register
        SPI_MEM_OP_NO_ADDR,
        SPI_MEM_OP_NO_DUMMY,
        SPI_MEM_OP_DATA_IN(1, &register_data, 1)
    );
	ret = esws->mem_ops.exec_op(nor->spimem, &srr_op);
    if (ret) {
        dev_err(&esws->host->dev,"Read Status Register failed\n");
		goto out;
	}

	request_register_data = register_data;
	/*
			SRP SEC TB BP2 BP1 BP0 WEL BUSY
	*/
	request_register_data |= (1 << 5);  //TB 1, bottom
	request_register_data &= ~(1 << 6);  // SEC 0, 64K
	if (request_register_data != register_data) {
		struct spi_mem_op srw_op = SPI_MEM_OP(
        	SPI_MEM_OP_CMD(SPINOR_OP_WRSR, 1),              // write Status Register
        	SPI_MEM_OP_NO_ADDR,
        	SPI_MEM_OP_NO_DUMMY,
        	SPI_MEM_OP_DATA_OUT(1, &request_register_data, 1)
    	);
		ret = esws->mem_ops.exec_op(nor->spimem, &srw_op);
   		if (ret) {
        	dev_err(&esws->host->dev,"Write Status Register failed\n");
			goto out;
		}
	}
	//Update status register3
    struct spi_mem_op sr3r_op = SPI_MEM_OP(
        SPI_MEM_OP_CMD(SPINOR_OP_RDSR3, 1),              // Read Status Register3
        SPI_MEM_OP_NO_ADDR,
        SPI_MEM_OP_NO_DUMMY,
        SPI_MEM_OP_DATA_IN(1, &register_data, 1)
    );
	ret = esws->mem_ops.exec_op(nor->spimem, &sr3r_op);
    if (ret) {
        dev_err(&esws->host->dev,"Read Status Register failed\n");
		goto out;
	}

	request_register_data = register_data;
	/*
			SRP SEC TB BP2 BP1 BP0 WEL BUSY
	*/
	request_register_data |= (1 << 2);   //WPS 1, individual block
	if (request_register_data != register_data) {
		struct spi_mem_op sr3w_op = SPI_MEM_OP(
        	SPI_MEM_OP_CMD(SPINOR_OP_WRSR3, 1),              // write Status Register3
        	SPI_MEM_OP_NO_ADDR,
        	SPI_MEM_OP_NO_DUMMY,
        	SPI_MEM_OP_DATA_OUT(1, &request_register_data, 1)
    	);
		ret = esws->mem_ops.exec_op(nor->spimem, &sr3w_op);
   		if (ret) {
        	dev_err(&esws->host->dev,"Write Status Register failed\n");
			goto out;
		}
	}

	//Update global lock/unlock register
	if(enable)
		lock_reg = SPINOR_GLOBAL_BLOCK_LOCK;
	else
		lock_reg = SPINOR_GLOBAL_BLOCK_UNLOCK;

    struct spi_mem_op lock_op = SPI_MEM_OP(
        SPI_MEM_OP_CMD(lock_reg, 1),              // Write Lock/Unlock Register
        SPI_MEM_OP_NO_ADDR,
        SPI_MEM_OP_NO_DUMMY,
        SPI_MEM_OP_NO_DATA
    );

    ret = esws->mem_ops.exec_op(nor->spimem, &lock_op);
    if (ret)
        dev_err(&esws->host->dev,"Write Lock/Unlock Register failed\n");
out:
    return ret;
}

struct spi_nor *find_nor_via_mtd(struct eswin_spi  *esws)
{
    struct mtd_info *mtd;
    struct spi_nor *nor = NULL;

    mtd_for_each_device(mtd) {
		nor = mtd_to_spi_nor(mtd);
		if(nor) {
            struct spi_device *spi = nor->spimem ? nor->spimem->spi : NULL;
			if(spi->controller == esws->host)
                break;

            nor = NULL;
		}
    }

    return nor;
}

static ssize_t wp_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{

	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);
	struct eswin_spi  *esws = &eswmmio->esws;

	int enable;
	int ret;

	struct spi_nor *nor = find_nor_via_mtd(esws);

    if (kstrtoint(buf, 0, &enable))
        return -EINVAL;

    mutex_lock(&wp_lock);

    if (enable != eswmmio->wp_status) {
        ret = spi_nor_set_write_protect(esws, nor, enable);
        if (ret) {
            dev_err(&esws->host->dev,"Failed to set WP to %d\n", enable);
            mutex_unlock(&wp_lock);
            return ret;
        }
        eswmmio->wp_status = enable;
    }

    mutex_unlock(&wp_lock);
    return count;
}

static DEVICE_ATTR_RW(wp);


/**
 *  @brief write data from dest address to flash
 */
static void eswin_bootspi_send_data(struct eswin_spi *esws, const u32 *dest, u32 size)
{
	u32 offset = 0;
    u8  *buff = NULL;
	u32 data = 0;
	int i;

	while (size >= SPI_FLASH_WR_WORD) {
		bootspi_data_writel(esws, offset, *dest++);
		offset = offset + SPI_FLASH_WR_WORD;
		size = size - SPI_FLASH_WR_WORD;
	}
	if (size != 0) {
        buff = (u8 *)dest;
		for (i = 0; i < size; i++) {
            data |= (*buff) << (8 * i);
            buff++;
		}
		bootspi_data_writel(esws, offset, data);
	}
}

/**
 *  @brief Read data from flash to dest address
 */
static void eswin_bootspi_recv_data(struct eswin_spi *esws, u32 *dest, u32 size)
{
	u32 offset = 0;
	u8 *buff = NULL;
	u32 data = 0xFFFFFFFF;
	int i;

	while (size >= SPI_FLASH_WR_WORD) {
        *dest++ = bootspi_data_readl(esws, offset);
        offset = offset + SPI_FLASH_WR_WORD;
        size   = size - SPI_FLASH_WR_WORD;
	}
	if (size != 0) {
		buff = (u8 *)dest;
		data = bootspi_data_readl(esws, offset);
		for (i = 0; i < size; i++) {
			*buff = (u8)(data >> (8 * i));
			buff++;
		}
	}
}

/**
 * @brief spi read and write cfg
 */
static void spi_read_write_cfg(struct eswin_spi *esws, u32 byte, u32 addr)
{
	bootspi_writel(esws, ES_SPI_CSR_09, addr);
	bootspi_writel(esws, ES_SPI_CSR_05, byte);
	bootspi_writel(esws, ES_SPI_CSR_04, SPI_FAST_READ_DEFAULT);
	bootspi_writel(esws, ES_SPI_CSR_01, STANDARD_SPI);
}

static int spi_flash_idle(struct eswin_spi *esws)
{
	uint32_t register_data = 0xff;
	int ret;
	struct spi_nor *nor = find_nor_via_mtd(esws);

	//Update status register1
    struct spi_mem_op srr_op = SPI_MEM_OP(
        SPI_MEM_OP_CMD(SPINOR_OP_RDSR, 1),              // Read Status Register
        SPI_MEM_OP_NO_ADDR,
        SPI_MEM_OP_NO_DUMMY,
        SPI_MEM_OP_DATA_IN(1, &register_data, 1)
    );

	// check flash status register' busy bit to make sure operation is finish.
	do {
		ret = esws->mem_ops.exec_op(nor->spimem, &srr_op);
		if (ret) {
     		dev_err(&esws->host->dev,"Read Status Register failed\n");
			return -1;
		}
	} while (register_data & 0x1);

	return 0;
}

static int spi_wait_over(struct eswin_spi *esws)
{
	u32 val;
	if (readl_poll_timeout(esws->regs + ES_SPI_CSR_06, val,
		(!(val & 0x1)), 10, RX_TIMEOUT * 1000)) {
			dev_err(&esws->host->dev,"spi_wait_over : timeout in waiting contronller busy status\n");
			return -ETIMEDOUT;
	}

	return 0;
}

/**
 * @brief spi send command
 */
static void spi_command_cfg(struct eswin_spi *esws, u32 code, u32 type, u32 dma)
{
	u32 command;

	bootspi_writel(esws, ES_SPI_CSR_08, 0x5);

	if(dma == SPI_COMMAND_MOVE_DMA)
		bootspi_writel(esws, ES_SPI_CSR_07, 0x0);
	else
		bootspi_writel(esws, ES_SPI_CSR_07, 0x4);

	command = ((code << SPI_COMMAND_CODE_FIELD_POSITION) |
		(dma << SPI_COMMAND_MOVE_FIELD_POSITION) |
		(type << SPI_COMMAND_TYPE_FIELD_POSITION) | SPI_COMMAND_VALID);

	bootspi_writel(esws, ES_SPI_CSR_06, command);
}

int wait_spi_irq(struct eswin_spi *esws)
{
	while(!(bootspi_readl(esws, ES_SPI_CSR_07) & 0x1));
	bootspi_writel(esws, ES_SPI_CSR_07, 0x2);
	return 0;
}

static void bootspi_tx_dma(struct eswin_spi *esws)
{
	int ret;
	u32 write_size = 0;
	u32 offset = esws->addr;
	u8 *wr_dest = (u8 *)(esws->tx);
	int size = esws->tx_len;

	while (size > 0) {
		write_size = size;
		if (write_size > FLASH_PAGE_SIZE) {
			write_size = FLASH_PAGE_SIZE;
		}
		memcpy(esws->tx_dma_buf, wr_dest, write_size);

		spi_read_write_cfg(esws, write_size, offset);
		spi_command_cfg(esws, esws->opcode, esws->cmd_type, SPI_COMMAND_MOVE_DMA);
		esw_spi_dma_config_tx(esws);
		esw_spi_dma_submit_tx(esws, write_size);
		dma_async_issue_pending(esws->txchan);

		ret = esw_spi_tx_dma_wait(esws, write_size, esws->max_freq);
		if (ret)
			break;
		reinit_completion(&esws->tx_dma_completion);
		spi_wait_over(esws);
		spi_flash_idle(esws);

		wr_dest += write_size;
		offset += write_size;
		size -= write_size;
	}
}

static void bootspi_tx_cpu(struct eswin_spi *esws)
{
	u32 write_size = 0;
	u32 cmd_type = esws->cmd_type;
	u32 offset = esws->addr;
	u32 cmd_code = esws->opcode;
	int size = esws->tx_len;
	const u32 *wr_dest = esws->tx;

	while (size > 0) {
		write_size = size;
		if (write_size > FLASH_PAGE_SIZE) {
			write_size = FLASH_PAGE_SIZE;
		}

		spi_read_write_cfg(esws, write_size, offset);
		eswin_bootspi_send_data(esws, wr_dest, write_size);
		spi_command_cfg(esws, cmd_code, cmd_type, SPI_COMMAND_MOVE_VALUE);
		spi_wait_over(esws);
		wait_spi_irq(esws);
		wr_dest += write_size / SPI_FLASH_WR_WORD;
		offset += write_size;
		size = size - write_size;
	}
}
/**
 * @brief  spi write flash
 * @param [in]  offset: address of flash to be write
 * @param [in]  wr_dest: Address of data to be sent
 * @param [in]  size: size of flash to be write
 */
static void bootspi_writer(struct eswin_spi *esws)
{
	if (esws->tx_len == 0)
	{
		spi_read_write_cfg(esws, 0, (uintptr_t)esws->addr);
		spi_command_cfg(esws, esws->opcode, esws->cmd_type, SPI_COMMAND_MOVE_VALUE);
		spi_wait_over(esws);
		wait_spi_irq(esws);
	} else {
		if(esws->dma_mapped)
			bootspi_tx_dma(esws);
		else
			bootspi_tx_cpu(esws);
	}
}

static void bootspi_rx_dma(struct eswin_spi *esws)
{
	int read_size = 0;
	int ret;
	u32 offset = esws->addr;
	u8 *rd_dest = (u8 *)(esws->rx);
	int size = esws->rx_len;
	while (size > 0) {
		read_size = size;
		if (read_size > FLASH_PAGE_SIZE) {
			read_size = FLASH_PAGE_SIZE;
		} else if (read_size < SPI_FLASH_WR_WORD) {
			read_size = SPI_FLASH_WR_WORD;
		}
		else {
			read_size = (read_size + SPI_FLASH_WR_WORD -1) / SPI_FLASH_WR_WORD  * SPI_FLASH_WR_WORD;
		}

		spi_read_write_cfg(esws, read_size, offset);
		spi_command_cfg(esws, esws->opcode, esws->cmd_type, SPI_COMMAND_MOVE_DMA);

		esw_spi_dma_config_rx(esws);
		esw_spi_dma_submit_rx(esws, read_size);
		dma_async_issue_pending(esws->rxchan);

		spi_wait_over(esws);
		ret = esw_spi_rx_dma_wait(esws, read_size, esws->max_freq);
		if (ret)
			break;
		reinit_completion(&esws->rx_dma_completion);
		memcpy(rd_dest, esws->rx_dma_buf, read_size);

		rd_dest += read_size;
		offset += read_size;
		size = size - read_size;
	}
}

static void bootspi_rx_cpu(struct eswin_spi *esws)
{
	int read_size = 0;
	u32 offset = esws->addr;
	u32 cmd_code = esws->opcode;
	u32 cmd_type = esws->cmd_type;
	u32 *mem_dest = esws->rx;
	int size = esws->rx_len;

	while (size > 0) {
		read_size = size;
		if (read_size > FLASH_PAGE_SIZE) {
			read_size = FLASH_PAGE_SIZE;
		}

		spi_read_write_cfg(esws, read_size, offset);
		spi_command_cfg(esws, cmd_code, cmd_type, SPI_COMMAND_MOVE_VALUE);
		spi_wait_over(esws);
		wait_spi_irq(esws);

		eswin_bootspi_recv_data(esws, mem_dest, read_size);
		mem_dest += read_size / SPI_FLASH_WR_WORD;
		offset += read_size;
		size = size - read_size;
	}
}
static void bootspi_reader(struct eswin_spi *esws)
{
	if(esws->dma_mapped == 1)
		bootspi_rx_dma(esws);
	else
		bootspi_rx_cpu(esws);
}


/* The size of ctrl1 limits data transfers to 64K */
static int eswin_spi_adjust_op_size(struct spi_mem *mem,
					struct spi_mem_op *op)
{
	op->data.nbytes = min(op->data.nbytes, (unsigned int)SZ_64K);

	return 0;
}

/*
 * The controller only supports Standard SPI mode, Duall mode and
 * Quad mode. Double sanitize the ops here to avoid OOB access.
 */
static bool eswin_spi_supports_op(struct spi_mem *mem,
				      const struct spi_mem_op *op)
{
	return spi_mem_default_supports_op(mem, op);
}


static int eswin_spi_exec_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	bool read = op->data.dir == SPI_MEM_DATA_IN;
	int ret = 0;
	struct eswin_spi *esws = spi_controller_get_devdata(mem->spi->controller);

	struct device *dev = &esws->host->dev;

	esws->addr = op->addr.val;
	esws->opcode = op->cmd.opcode;

	if ( esws->opcode == SPINOR_OP_WREN
		|| esws->opcode == SPINOR_OP_WRDI)
		return 0;

	switch(esws->opcode) {
		case SPINOR_OP_RDID:
		case SPINOR_OP_RDSFDP:
			esws->cmd_type = SPIC_CMD_TYPE_READ_JEDEC_ID;
			break;
		case SPINOR_OP_BE_4K:
		case SPINOR_OP_BE_4K_PMC:
			esws->opcode = SPINOR_OP_BE_4K;
			esws->cmd_type = SPIC_CMD_TYPE_SECTOR_ERASE;
			break;
		case SPINOR_OP_BE_32K:
			esws->cmd_type = SPIC_CMD_TYPE_BLOCK_ERASE_TYPE1;
			break;
		case SPINOR_OP_SE:
			esws->cmd_type = SPIC_CMD_TYPE_BLOCK_ERASE_TYPE2;
			break;
		case SPINOR_OP_CHIP_ERASE:
		case SPINOR_GLOBAL_BLOCK_LOCK:
		case SPINOR_GLOBAL_BLOCK_UNLOCK:
			esws->cmd_type = SPIC_CMD_TYPE_CHIP_ERASE;
			break;
		case SPINOR_OP_PP:
		case SPINOR_OP_PP_1_1_4:
		case SPINOR_OP_PP_1_4_4:
		case SPINOR_OP_PP_1_1_8:
		case SPINOR_OP_PP_1_8_8:
			esws->opcode = SPINOR_OP_PP;
			esws->cmd_type = SPIC_CMD_TYPE_SPI_PROGRAM;
			break;
		case SPINOR_OP_READ:
		case SPINOR_OP_READ_FAST:
		case SPINOR_OP_READ_1_1_2:
		case SPINOR_OP_READ_1_2_2:
		case SPINOR_OP_READ_1_1_4:
		case SPINOR_OP_READ_1_4_4:
		case SPINOR_OP_READ_1_1_8:
		case SPINOR_OP_READ_1_8_8:
			esws->opcode = SPINOR_OP_READ;
			esws->cmd_type = SPIC_CMD_TYPE_READ_DATA;
			break;
		case SPINOR_OP_RDSR:
		case SPINOR_OP_RDSR2:
		case SPINOR_OP_RDSR3:
			esws->cmd_type = SPIC_CMD_TYPE_READ_STATUS_REGISTER;
			break;
		case SPINOR_OP_WRSR:
		case SPINOR_OP_WRSR2:
		case SPINOR_OP_WRSR3:
			esws->cmd_type = SPIC_CMD_TYPE_WRITE_STATUS_REGISTER;
			break;
		case SPIC_CMD_CODE_POWER_DOWN:
			esws->cmd_type = SPIC_CMD_TYPE_POWER_DOWN;
			break;
		case SPIC_CMD_CODE_RELEASE_POWER_DOWN:
			esws->cmd_type = SPIC_CMD_TYPE_RELEASE_POWER_DOWM;
			break;
		case SPIC_CMD_CODE_ENABLE_RESET:
		case SPIC_CMD_CODE_RESET:
			esws->cmd_type = SPIC_CMD_TYPE_SPI_PROGRAM;
			break;
		default:
			dev_warn(dev, "[%s %d]: unsupport opcode = 0x%x, return sucess directly!\n",
				__func__,__LINE__, esws->opcode);
			return 0;
	}

	dev_dbg(dev, "[%s %d]: data direction=%d, opcode = 0x%x, cmd_type 0x%x\n",
		__func__,__LINE__, op->data.dir, esws->opcode, esws->cmd_type);

	if (read) {
		esws->rx = op->data.buf.in;
		esws->rx_len = op->data.nbytes;
		bootspi_reader(esws);
	} else {
		esws->tx = op->data.buf.out;
		esws->tx_len = op->data.nbytes;
		/* Fill up the write fifo before starting the transfer */
		bootspi_writer(esws);
		if (spi_wait_over(esws) < 0) {
			dev_err(dev, "spi_wait_over ETIMEDOUT\n");
			ret = -ETIMEDOUT;
		}
	}

	dev_dbg(dev, "%u bytes xfered\n", op->data.nbytes);
	return ret;
}


/*
 * Initialize the default memory operations if a glue layer hasn't specified
 * custom ones. Direct mapping operations will be preserved anyway since DW SPI
 * controller doesn't have an embedded dirmap interface. Note the memory
 * operations implemented in this driver is the best choice only for the DW APB
 * SSI controller with standard native CS functionality. If a hardware vendor
 * has fixed the automatic CS assertion/de-assertion peculiarity, then it will
 * be safer to use the normal SPI-messages-based transfers implementation.
 */
static void esw_spi_init_mem_ops(struct eswin_spi *esws)
{
	if (!esws->mem_ops.exec_op) {
		esws->mem_ops.adjust_op_size = eswin_spi_adjust_op_size;
		esws->mem_ops.supports_op = eswin_spi_supports_op;
		esws->mem_ops.exec_op = eswin_spi_exec_op;
		if (!esws->max_mem_freq)
			esws->max_mem_freq = esws->max_freq;
	}
}

int eswin_spi_add_host(struct device *dev, struct eswin_spi *esws)
{
	struct spi_controller *host;
	int ret;

	if (!esws)
		return -EINVAL;

	host = spi_alloc_host(dev, 0);
	if (!host)
		return -ENOMEM;

	device_set_node(&host->dev, dev_fwnode(dev));

	esws->host = host;
	esws->fifo_len = 256;

	spi_controller_set_devdata(host, esws);

	esw_spi_init_mem_ops(esws);

	host->use_gpio_descriptors = false;
	host->mode_bits =  SPI_MODE_0;
	host->bits_per_word_mask =  SPI_BPW_MASK(32) | SPI_BPW_MASK(16) |
				     SPI_BPW_MASK(8);
	host->bus_num = esws->bus_num;
	host->num_chipselect = esws->num_cs;
	host->dev.of_node = dev->of_node;
	host->mem_ops = &esws->mem_ops;
	host->max_speed_hz = esws->max_freq;
	host->flags = SPI_MASTER_HALF_DUPLEX;
	host->auto_runtime_pm = true;

	ret = esw_spi_dma_init_generic(dev, esws);
	if (ret) {
		dev_err_probe(dev, ret, "spi dma request faild\n");
		goto err_free_host;
	}

	ret = spi_register_controller(host);
	if (ret) {
		dev_err_probe(dev, ret, "problem registering spi host\n");
		goto err_free_host;
	}

	return 0;

err_free_host:
	spi_controller_put(host);
	return ret;
}


static int eswin_spi_mmio_probe(struct platform_device *pdev)
{
	int (*init_func)(struct platform_device *pdev,
			 struct eswin_spi_mmio *eswmmio);
	struct eswin_spi_mmio *eswmmio;
	struct resource *mem;
	struct eswin_spi *esws;
	int ret;

	eswmmio = devm_kzalloc(&pdev->dev, sizeof(struct eswin_spi_mmio),
			GFP_KERNEL);
	if (!eswmmio)
		return -ENOMEM;

	esws = &eswmmio->esws;

	/* Get basic io resource and map it */
	esws->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(esws->regs))
		return PTR_ERR(esws->regs);

	esws->flash_base = devm_platform_get_and_ioremap_resource(pdev, 1, &mem);
	if (IS_ERR(esws->flash_base))
		return PTR_ERR(esws->flash_base);
	esws->flash_paddr = mem->start;

	eswmmio->clk = devm_clk_get_optional(&pdev->dev, "clk");
	if (IS_ERR(eswmmio->clk))
		return PTR_ERR(eswmmio->clk);
	ret = clk_prepare_enable(eswmmio->clk);
	if (ret)
		return ret;

	/* Optional clock needed to access the registers */
	eswmmio->cfg_clk = devm_clk_get_optional(&pdev->dev, "cfg_clk");
	if (IS_ERR(eswmmio->cfg_clk)) {
		ret = PTR_ERR(eswmmio->cfg_clk);
		goto out_clk;
	}
	ret = clk_prepare_enable(eswmmio->cfg_clk);
	if (ret)
		goto out_clk;

	/* find an optional reset controller */
	eswmmio->rstc = devm_reset_control_get_optional_exclusive(&pdev->dev, "rst");
	if (IS_ERR(eswmmio->rstc)) {
		ret = PTR_ERR(eswmmio->rstc);
		goto out_clk;
	}

	ret = device_property_read_u32(&pdev->dev, "spi-max-frequency",
					&esws->max_freq);
	if (ret) {
		esws->max_freq = 40000000;
	}

	/* rate max 40M */
	if (esws->max_freq  > 40000000)
		esws->max_freq = 40000000;

	reset_control_deassert(eswmmio->rstc);
	esws->bus_num = pdev->id;
	ret = clk_set_rate(eswmmio->clk, esws->max_freq);
	if (ret) {
		dev_err(&pdev->dev, "could not enable clock: %d\n", ret);
		goto out_clk;
	}

	esws->max_freq = clk_get_rate(eswmmio->clk);
	esws->num_cs = 1;

	init_func = device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func(pdev, eswmmio);
		if (ret)
			goto out;
	}

	pm_runtime_enable(&pdev->dev);

	ret = eswin_spi_add_host(&pdev->dev, esws);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, eswmmio);

	eswmmio->wp_status = 1;
	// Register the new region_wp_enable attribute
	ret = device_create_file(&pdev->dev, &dev_attr_wp);
	if (ret)
		goto out;

	return 0;

out:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(eswmmio->cfg_clk);
out_clk:
	clk_disable_unprepare(eswmmio->clk);
	reset_control_assert(eswmmio->rstc);
	devm_kfree(&pdev->dev, eswmmio);
	return ret;
}

static void eswin_spi_mmio_remove(struct platform_device *pdev)
{
	struct eswin_spi_mmio *eswmmio = platform_get_drvdata(pdev);
	struct eswin_spi *esws = &eswmmio->esws;

	device_remove_file(&pdev->dev, &dev_attr_wp);
	spi_unregister_controller(esws->host);
	esw_spi_dma_exit(esws);

	pm_runtime_disable(&pdev->dev);
	reset_control_assert(eswmmio->rstc);
	clk_disable_unprepare(eswmmio->cfg_clk);
	clk_disable_unprepare(eswmmio->clk);
}

static int __maybe_unused eswin_spi_runtime_resume(struct device *dev)
{
	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(eswmmio->cfg_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(eswmmio->clk);
	if (ret) {
		clk_disable_unprepare(eswmmio->cfg_clk);
		return ret;
	}
	return 0;
}

static int __maybe_unused eswin_spi_runtime_suspend(struct device *dev)
{
	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);

	clk_disable_unprepare(eswmmio->clk);
	clk_disable_unprepare(eswmmio->cfg_clk);
	return 0;
}

static int __maybe_unused eswin_spi_suspend(struct device *dev)
{

	int ret;
	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);
	struct spi_controller *host = eswmmio->esws.host;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = spi_controller_suspend(host);
	if (ret)
		return ret;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_force_suspend(dev);

	clk_disable_unprepare(eswmmio->clk);
	clk_disable_unprepare(eswmmio->cfg_clk);
	return 0;
}

static int __maybe_unused eswin_spi_resume(struct device *dev)
{
	int ret;
	struct eswin_spi_mmio *eswmmio = dev_get_drvdata(dev);
	struct spi_controller *host = eswmmio->esws.host;

	ret = clk_prepare_enable(eswmmio->cfg_clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable cfg_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(eswmmio->clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable clk (%d)\n", ret);
		clk_disable_unprepare(eswmmio->cfg_clk);
		return ret;
	}

	ret = spi_controller_resume(host);
	if (ret) {
		clk_disable_unprepare(eswmmio->cfg_clk);
		clk_disable_unprepare(eswmmio->clk);
		return ret;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;
}

static const struct dev_pm_ops eswin_bootspi_pm = {
	SET_RUNTIME_PM_OPS(eswin_spi_runtime_suspend,
				eswin_spi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(eswin_spi_suspend, eswin_spi_resume)
};


static const struct of_device_id eswin_spi_mmio_of_match[] = {
	{ .compatible = "eswin,bootspi",},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, eswin_spi_mmio_of_match);


static struct platform_driver eswin_spi_mmio_driver = {
	.probe		= eswin_spi_mmio_probe,
	.remove_new	= eswin_spi_mmio_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = eswin_spi_mmio_of_match,
		.acpi_match_table = ACPI_PTR(eswin_spi_mmio_acpi_match),
	},
};
static int __init eswin_spi_init(void)
{
	return platform_driver_register(&eswin_spi_mmio_driver);
}
late_initcall(eswin_spi_init);

static void __exit eswin_spi_exit(void)
{
	platform_driver_unregister(&eswin_spi_mmio_driver);
}
module_exit(eswin_spi_exit);

MODULE_LICENSE("GPL v2");