/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPI_ESWIN_H__
#define __SPI_ESWIN_H__

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/irqreturn.h>
#include <linux/io.h>
#include <linux/scatterlist.h>
#include <linux/spi/spi-mem.h>
#include <linux/bitfield.h>

struct eswin_spi;

struct eswin_spi {
	struct spi_controller	*host;

	u32			caps;		/* DW SPI capabilities */

	void __iomem		*regs;
	void __iomem		*flash_base;
	unsigned long		flash_paddr;
	u32			fifo_len;	/* depth of the FIFO buffer */
	u32			max_mem_freq;	/* max mem-ops bus freq */
	u32			max_freq;	/* max bus freq supported */

	u16			bus_num;
	u16			num_cs;		/* supported slave numbers */

	/* Current message transfer state info */
	const void			*tx;
	unsigned int		tx_len;
	void				*rx;
	unsigned int		rx_len;
	u16 opcode;
	u64 addr;
	u32 cmd_type;

	/* Custom memory operations */
	struct spi_controller_mem_ops mem_ops;

	/* DMA info */
	int			dma_mapped;
	struct dma_chan		*txchan;
	u32			txburst;
	struct dma_chan		*rxchan;
	u32			rxburst;
	unsigned long		dma_chan_busy;
	struct completion	tx_dma_completion;
	struct completion	rx_dma_completion;

	/* DMA buffer*/
	u32					*tx_dma_buf;
	dma_addr_t				tx_dma_phys;
	u32					*rx_dma_buf;
	dma_addr_t				rx_dma_phys;


};
#define ES_SPI_WAIT_RETRIES			5

#define ES_SPI_FIFO_OFFSET 0xA800000

/* Register offsets */
#define ES_SPI_CSR_00			0x00	/*WRITE_STATUS_REG_TIME*/
#define ES_SPI_CSR_01			0x04	/*SPI_BUS_MODE*/
#define ES_SPI_CSR_02			0x08	/*ERASE_COUNTER_TAP*/
#define ES_SPI_CSR_03			0x0c	/*DMA_EN_HCLK_STATUS*/
#define ES_SPI_CSR_04			0x10	/*FAST_READ_CONTROL*/
#define ES_SPI_CSR_05			0x14	/*SPI_FLASH_WR_NUM*/
#define ES_SPI_CSR_06			0x18	/*SPI_FLASH_COMMAND*/
#define ES_SPI_CSR_07			0x1c	/*INTERRUPT_CONTROL*/
#define ES_SPI_CSR_08			0x20	/*DMA_REQUEST_TAP*/
#define ES_SPI_CSR_09			0x24	/*SPI_FLASH_WR_ADDRESS*/
#define ES_SPI_CSR_10			0x28	/*PAGE_PROGRAM_TIME*/
#define ES_SPI_CSR_11			0x2c	/*SECTOR_ERASE_TIME*/
#define ES_SPI_CSR_12			0x30	/*SMALL_BLOCK_ERASE_TIME*/
#define ES_SPI_CSR_13			0x34	/*LARGE_BLOCK_ERASE_TIME*/
#define ES_SPI_CSR_14			0x38	/*CHIP_ERASE_TIME*/
#define ES_SPI_CSR_15			0x3c	/*CHIP_DESELECT_TIME*/
#define ES_SPI_CSR_16			0x40	/*POWER_DOWN_TIME*/

/* Flash opcodes. */
#define SPINOR_OP_RDSR3		0x15	/* Read status register 3 */
#define SPINOR_OP_WRSR3		0x11	/* Write status register 3 */
#define SPINOR_BLOCK_LOCK		0x36	/* Individual Block/Sector Lock */
#define SPINOR_BLOCK_UNLOCK		0x39	/* Individual Block/Sector UnLock */
#define SPINOR_GLOBAL_BLOCK_LOCK		0x7E	/* global Block/Sector UnLock */
#define SPINOR_GLOBAL_BLOCK_UNLOCK		0x98	/* global Block/Sector UnLock */

#define ES_SYSCSR_SPIMODECFG			0x340

#define ES_CONCSR_SPI_INTSEL			0x3c0

#define SPI_COMMAND_VALID				0x01
#define SPI_COMMAND_MOVE_VALUE			0x00
#define SPI_COMMAND_MOVE_DMA			0x01
#define SPI_COMMAND_CODE_FIELD_POSITION 0X06
#define SPI_COMMAND_MOVE_FIELD_POSITION 0X05
#define SPI_COMMAND_TYPE_FIELD_POSITION 0X01

/* Bit fields in CTRLR0 */
/*
 * Only present when SSI_MAX_XFER_SIZE=16. This is the default, and the only
 * option before version 3.23a.
 */
#define SPI_INTSEL_MASK			GENMASK(11, 10)
#define INT_ROUTED_U84			0x0
#define INT_ROUTED_LPCPU		0x1
#define INT_ROUTED_SCPU			0x3u

#define RX_TIMEOUT			5000		/* timeout in ms */

#define SPI_COMMAND_INIT_VALUE       0XFFFFC000
#define FLASH_PAGE_SIZE      0x100

typedef enum {
	SPI_FLASH_WR_BYTE  = 1,
	SPI_FLASH_WR_2BYTE = 2,
	SPI_FLASH_WR_WORD  = 4,
} SPI_FLASH_WR_NUM_T;

typedef enum {
	SPI_FAST_READ_DEFAULT = 0,
	SPI_FAST_READ_ENABLE  = 3 /*WHEN SPI QUAD0 OR DUAL MODE*/
} SPI_FAST_READ_CTL_T;

typedef enum { STANDARD_SPI = 0, DUAL_SPI, QUAD_SPI } SPI_BUS_MODE_T;

typedef enum {
	SPIC_CMD_TYPE_SPI_PROGRAM = 0,
	SPIC_CMD_TYPE_WRITE_STATUS_REGISTER,
	SPIC_CMD_TYPE_READ_STATUS_REGISTER,
	SPIC_CMD_TYPE_SECTOR_ERASE,
	SPIC_CMD_TYPE_BLOCK_ERASE_TYPE1,
	SPIC_CMD_TYPE_BLOCK_ERASE_TYPE2,
	SPIC_CMD_TYPE_CHIP_ERASE,
	SPIC_CMD_TYPE_POWER_DOWN,
	SPIC_CMD_TYPE_RELEASE_POWER_DOWM,
	SPIC_CMD_TYPE_ENTER_OR_EXIT_32BIT_MODE,
	SPIC_CMD_TYPE_READ_SECURITY_REG,
	SPIC_CMD_TYPE_ERASE_SECURITY_REG,
	SPIC_CMD_TYPE_WRITE_SECURITY_REG,
	SPIC_CMD_TYPE_READ_DATA,
	SPIC_CMD_TYPE_READ_MANUFACTURED_ID,
	SPIC_CMD_TYPE_READ_JEDEC_ID
} SPI_FLASH_COMMAND_TYPE_T;

#define SPIC_CMD_CODE_POWER_DOWN              0xb9
#define SPIC_CMD_CODE_RELEASE_POWER_DOWN       0xab
#define SPIC_CMD_CODE_ENABLE_RESET            0x66
#define SPIC_CMD_CODE_RESET                   0x99

/* Flash opcodes. */
#define SPINOR_OP_RDSR3		0x15	/* Read status register 3 */
#define SPINOR_OP_WRSR3		0x11	/* Write status register 3 */
#define SPINOR_GLOBAL_BLOCK_LOCK		0x7E	/* global Block/Sector UnLock */
#define SPINOR_GLOBAL_BLOCK_UNLOCK		0x98	/* global Block/Sector UnLock */

static inline u32 bootspi_readl(struct eswin_spi *esws, u32 offset)
{
	return readl(esws->regs + offset);
}

static inline void bootspi_writel(struct eswin_spi *esws, u32 offset, u32 val)
{
	writel(val, esws->regs + offset);
}

static inline u32 bootspi_data_readl(struct eswin_spi *esws, u32 offset)
{
	return readl(esws->flash_base + offset);
}

static inline void bootspi_data_writel(struct eswin_spi *esws, u32 offset, u32 val)
{
	writel(val, esws->flash_base + offset);
}

#endif /* __SPI_ESWIN_H__ */
