// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN audio sof driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: DengLei <denglei@eswincomputing.com>
 */

#include <linux/module.h>
#include <sound/sof/xtensa.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/firmware.h>
#include "../ops.h"

#include "es-common.h"

#define DSP_SUBSYS_HILOAD_CLK     1040000000   //1G
#define DSP_SUBSYS_LOWLOAD_CLK    5200000
struct xtensa_exception_cause {
	u32 id;
	const char *msg;
	const char *description;
};

/*
 * From 4.4.1.5 table 4-64 Exception Causes of Xtensa
 * Instruction Set Architecture (ISA) Reference Manual
 */
static const struct xtensa_exception_cause xtensa_exception_causes[] = {
	{0, "IllegalInstructionCause", "Illegal instruction"},
	{1, "SyscallCause", "SYSCALL instruction"},
	{2, "InstructionFetchErrorCause",
	"Processor internal physical address or data error during instruction fetch"},
	{3, "LoadStoreErrorCause",
	"Processor internal physical address or data error during load or store"},
	{4, "Level1InterruptCause",
	"Level-1 interrupt as indicated by set level-1 bits in the INTERRUPT register"},
	{5, "AllocaCause",
	"MOVSP instruction, if caller’s registers are not in the register file"},
	{6, "IntegerDivideByZeroCause",
	"QUOS, QUOU, REMS, or REMU divisor operand is zero"},
	{8, "PrivilegedCause",
	"Attempt to execute a privileged operation when CRING ? 0"},
	{9, "LoadStoreAlignmentCause", "Load or store to an unaligned address"},
	{12, "InstrPIFDataErrorCause",
	"PIF data error during instruction fetch"},
	{13, "LoadStorePIFDataErrorCause",
	"Synchronous PIF data error during LoadStore access"},
	{14, "InstrPIFAddrErrorCause",
	"PIF address error during instruction fetch"},
	{15, "LoadStorePIFAddrErrorCause",
	"Synchronous PIF address error during LoadStore access"},
	{16, "InstTLBMissCause", "Error during Instruction TLB refill"},
	{17, "InstTLBMultiHitCause",
	"Multiple instruction TLB entries matched"},
	{18, "InstFetchPrivilegeCause",
	"An instruction fetch referenced a virtual address at a ring level less than CRING"},
	{20, "InstFetchProhibitedCause",
	"An instruction fetch referenced a page mapped with an attribute that does not permit instruction fetch"},
	{24, "LoadStoreTLBMissCause",
	"Error during TLB refill for a load or store"},
	{25, "LoadStoreTLBMultiHitCause",
	"Multiple TLB entries matched for a load or store"},
	{26, "LoadStorePrivilegeCause",
	"A load or store referenced a virtual address at a ring level less than CRING"},
	{28, "LoadProhibitedCause",
	"A load referenced a page mapped with an attribute that does not permit loads"},
	{32, "Coprocessor0Disabled",
	"Coprocessor 0 instruction when cp0 disabled"},
	{33, "Coprocessor1Disabled",
	"Coprocessor 1 instruction when cp1 disabled"},
	{34, "Coprocessor2Disabled",
	"Coprocessor 2 instruction when cp2 disabled"},
	{35, "Coprocessor3Disabled",
	"Coprocessor 3 instruction when cp3 disabled"},
	{36, "Coprocessor4Disabled",
	"Coprocessor 4 instruction when cp4 disabled"},
	{37, "Coprocessor5Disabled",
	"Coprocessor 5 instruction when cp5 disabled"},
	{38, "Coprocessor6Disabled",
	"Coprocessor 6 instruction when cp6 disabled"},
	{39, "Coprocessor7Disabled",
	"Coprocessor 7 instruction when cp7 disabled"},
};

/* only need xtensa atm */
static void xtensa_dsp_oops(struct snd_sof_dev *sdev, const char *level, void *oops)
{
	struct sof_ipc_dsp_oops_xtensa *xoops = oops;
	int i;

	dev_printk(level, sdev->dev, "error: DSP Firmware Oops\n");
	for (i = 0; i < ARRAY_SIZE(xtensa_exception_causes); i++) {
		if (xtensa_exception_causes[i].id == xoops->exccause) {
			dev_printk(level, sdev->dev,
				   "error: Exception Cause: %s, %s\n",
				   xtensa_exception_causes[i].msg,
				   xtensa_exception_causes[i].description);
		}
	}
	dev_printk(level, sdev->dev,
		   "EXCCAUSE 0x%8.8x EXCVADDR 0x%8.8x PS       0x%8.8x SAR     0x%8.8x\n",
		   xoops->exccause, xoops->excvaddr, xoops->ps, xoops->sar);
	dev_printk(level, sdev->dev,
		   "EPC1     0x%8.8x EPC2     0x%8.8x EPC3     0x%8.8x EPC4    0x%8.8x",
		   xoops->epc1, xoops->epc2, xoops->epc3, xoops->epc4);
	dev_printk(level, sdev->dev,
		   "EPC5     0x%8.8x EPC6     0x%8.8x EPC7     0x%8.8x DEPC    0x%8.8x",
		   xoops->epc5, xoops->epc6, xoops->epc7, xoops->depc);
	dev_printk(level, sdev->dev,
		   "EPS2     0x%8.8x EPS3     0x%8.8x EPS4     0x%8.8x EPS5    0x%8.8x",
		   xoops->eps2, xoops->eps3, xoops->eps4, xoops->eps5);
	dev_printk(level, sdev->dev,
		   "EPS6     0x%8.8x EPS7     0x%8.8x INTENABL 0x%8.8x INTERRU 0x%8.8x",
		   xoops->eps6, xoops->eps7, xoops->intenable, xoops->interrupt);
}

static void xtensa_stack(struct snd_sof_dev *sdev, const char *level, void *oops,
			 u32 *stack, u32 stack_words)
{
	struct sof_ipc_dsp_oops_xtensa *xoops = oops;
	u32 stack_ptr = xoops->plat_hdr.stackptr;
	/* 4 * 8chars + 3 ws + 1 terminating NUL */
	unsigned char buf[4 * 8 + 3 + 1];
	int i;

	dev_printk(level, sdev->dev, "stack dump from 0x%8.8x\n", stack_ptr);

	/*
	 * example output:
	 * 0x0049fbb0: 8000f2d0 0049fc00 6f6c6c61 00632e63
	 */
	for (i = 0; i < stack_words; i += 4) {
		hex_dump_to_buffer(stack + i, 16, 16, 4,
				   buf, sizeof(buf), false);
		dev_printk(level, sdev->dev, "0x%08x: %s\n", stack_ptr + i * 4, buf);
	}
}

const struct dsp_arch_ops es_sof_xtensa_arch_ops = {
	.dsp_oops = xtensa_dsp_oops,
	.dsp_stack = xtensa_stack,
};

/**
 * eswin_get_registers() - This function is called in case of DSP oops
 * in order to gather information about the registers, filename and
 * linenumber and stack.
 * @sdev: SOF device
 * @xoops: Stores information about registers.
 * @panic_info: Stores information about filename and line number.
 * @stack: Stores the stack dump.
 * @stack_words: Size of the stack dump.
 */
static void eswin_dsp_get_registers(struct snd_sof_dev *sdev,
			struct sof_ipc_dsp_oops_xtensa *xoops,
			struct sof_ipc_panic_info *panic_info,
			u32 *stack, size_t stack_words)
{
	u32 offset = sdev->dsp_oops_offset;

	/* first read registers */
	sof_mailbox_read(sdev, offset, xoops, sizeof(*xoops));

	/* then get panic info */
	if (xoops->arch_hdr.totalsize > EXCEPT_MAX_HDR_SIZE) {
		dev_err(sdev->dev, "invalid header size 0x%x. FW oops is bogus\n",
			xoops->arch_hdr.totalsize);
		return;
	}
	offset += xoops->arch_hdr.totalsize;
	sof_mailbox_read(sdev, offset, panic_info, sizeof(*panic_info));

	/* then get the stack */
	offset += sizeof(*panic_info);
	sof_mailbox_read(sdev, offset, stack, stack_words * sizeof(u32));
}

/**
 * es_dsp_dump() - This function is called when a panic message is
 * received from the firmware.
 * @sdev: SOF device
 * @flags: parameter not used but required by ops prototype
 */
void es_dsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc_dsp_oops_xtensa xoops;
	struct sof_ipc_panic_info panic_info;
	u32 stack[ESWIN_DSP_STACK_DUMP_SIZE];
	u32 status;

	/* Get information about the panic status from the debug box area.
	 * Compute the trace point based on the status.
	 */
	sof_mailbox_read(sdev, sdev->debug_box.offset + 0x4, &status, 4);

	/* Get information about the registers, the filename and line
	 * number and the stack.
	 */
	eswin_dsp_get_registers(sdev, &xoops, &panic_info, stack,
			   ESWIN_DSP_STACK_DUMP_SIZE);

	/* Print the information to the console */
	sof_print_oops_and_stack(sdev, KERN_ERR, status, status, &xoops,
				 &panic_info, stack, ESWIN_DSP_STACK_DUMP_SIZE);
}

int es_dsp_ring_doorbell(struct es_dsp_ipc *ipc, void *msg)
{
	int ret;
	struct es_dsp_chan *dsp_chan;

	dsp_chan = &ipc->chan;
	ret = mbox_send_message(dsp_chan->ch, msg);
	if (ret < 0)
		return ret;

	return 0;
}

static void es_dsp_handle_rx(struct mbox_client *c, void *msg)
{
	struct es_dsp_chan *chan = container_of(c, struct es_dsp_chan, cl);
	u32 dsp_msg = *(u32 *)msg;

	if (dsp_msg == IPC_MSG_RP_FLAG) {
		chan->ipc->ops->handle_reply(chan->ipc);
	} else {
		chan->ipc->ops->handle_request(chan->ipc);
	}
}

static void es_dsp_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	if (mssg)
		kfree(mssg);
}

struct mbox_chan *es_dsp_request_channel(struct es_dsp_ipc *dsp_ipc)
{
	struct es_dsp_chan *dsp_chan;

	dsp_chan = &dsp_ipc->chan;
	dsp_chan->ch = mbox_request_channel_byname(&dsp_chan->cl, dsp_chan->name);
	return dsp_chan->ch;
}

void es_dsp_free_channel(struct es_dsp_ipc *dsp_ipc)
{
	struct es_dsp_chan *dsp_chan;

	dsp_chan = &dsp_ipc->chan;
	mbox_free_channel(dsp_chan->ch);
}

static int es_dsp_setup_channels(struct es_dsp_ipc *dsp_ipc)
{
	struct device *dev = dsp_ipc->dev;
	struct es_dsp_chan *dsp_chan;
	struct mbox_client *cl;
	char *chan_name;
	int ret;

	chan_name = "dsp-mbox";

	dsp_chan = &dsp_ipc->chan;
	dsp_chan->name = chan_name;
	cl = &dsp_chan->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = false;
	cl->rx_callback = es_dsp_handle_rx;
	cl->tx_done = es_dsp_tx_done;

	dsp_chan->ipc = dsp_ipc;
	dsp_chan->ch = mbox_request_channel_byname(cl, chan_name);
	if (IS_ERR(dsp_chan->ch)) {
		ret = PTR_ERR(dsp_chan->ch);
		dev_err(dev, "Failed to request mbox chan %s ret %d\n", chan_name, ret);
		return ret;
	}

	dev_info(dev, "request mbox chan %s\n", chan_name);

	return 0;
}

struct es_dsp_ipc *es_ipc_init(struct snd_sof_dev *sdev)
{
	struct es_dsp_ipc *dsp_ipc;
	int ret;

	dsp_ipc = devm_kzalloc(sdev->dev, sizeof(*dsp_ipc), GFP_KERNEL);
	if (!dsp_ipc)
		return NULL;

	dsp_ipc->dev = sdev->dev;

	ret = es_dsp_setup_channels(dsp_ipc);
	if (ret < 0)
		return NULL;

	dev_info(sdev->dev, "ESW DSP IPC initialized\n");

	return dsp_ipc;
}

int es_get_hw_res(struct snd_sof_dev *sdev)
{
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;
	struct device_node *nd;
	struct resource res;
	int ret;

	ret = of_address_to_resource(sdev->dev->of_node, 0, &res);
	if (ret) {
		dev_err(sdev->dev, "failed to get dsp iram address\n");
		return ret;
	}
	of_node_put(sdev->dev->of_node);
	priv->dsp_iram_phyaddr = res.start;
	priv->dsp_iram_size = resource_size(&res);


	ret = of_address_to_resource(sdev->dev->of_node, 1, &res);
	if (ret) {
		dev_err(sdev->dev, "failed to get dsp dram address\n");
		return ret;
	}
	of_node_put(sdev->dev->of_node);
	priv->dsp_dram_phyaddr = res.start;
	priv->dsp_dram_size = resource_size(&res);

	ret = device_property_read_u32(sdev->dev, "process-id", &(priv->process_id));
	if (0 != ret) {
		dev_err(sdev->dev, "Failed to get process id\n");
		return ret;
	}

	ret = device_property_read_u32(sdev->dev, "mailbox-dsp-to-u84-addr", &priv->mbox_rx_phyaddr);
	if (0 != ret) {
		dev_err(sdev->dev, "failed to get mailbox rx addr\n");
		return ret;
	}

	ret = device_property_read_u32(sdev->dev, "mailbox-u84-to-dsp-addr", &priv->mbox_tx_phyaddr);
	if (0 != ret) {
		dev_err(sdev->dev, "failed to get mailbox tx addr\n");
		return ret;
	}

	/* get aclk */
	priv->aclk = devm_clk_get(sdev->dev, "aclk");
	if (IS_ERR(priv->aclk)) {
		ret = PTR_ERR(priv->aclk);
		dev_err(sdev->dev, "failed to get aclk: %d\n", ret);
		return ret;
	}

	nd = of_parse_phandle(sdev->dev->of_node, "dsp-uart", 0);
	if (!nd) {
		dev_err(sdev->dev, "failed to get uart node\n");
		return -ENODEV;
	}
	of_node_put(sdev->dev->of_node);

	ret = of_address_to_resource(nd, 0, &res);
	if (ret) {
		dev_err(sdev->dev, "failed to get dsp uart address\n");
		return ret;
	}
	of_node_put(nd);
	priv->uart_phyaddr = res.start;
	priv->uart_reg_size =  resource_size(&res);

	ret = device_property_read_u32(sdev->dev, "device-uart-mutex", &priv->uart_mutex_phyaddr);
	if (0 != ret) {
		dev_err(sdev->dev, "Failed to get uart mutex reg base\n");
		return ret;
	}

	nd = of_parse_phandle(sdev->dev->of_node, "ringbuffer-region", 0);
	if (!nd) {
		dev_err(sdev->dev, "failed to get ringbuffer region node\n");
		return -ENODEV;
	}
	of_node_put(sdev->dev->of_node);

	ret = of_address_to_resource(nd, 0, &res);
	if (ret) {
		dev_err(sdev->dev, "failed to get ring buffer address\n");
		return ret;
	}
	of_node_put(nd);

	priv->ringbuffer_phyaddr = res.start;
	priv->ringbuffer_size = resource_size(&res);

	return ret;
}

int es_dsp_get_subsys(struct snd_sof_dev *sdev)
{
	struct device *parent;
	struct es_dsp_subsys *subsys;
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	parent = priv->dev->parent;
	subsys = dev_get_drvdata(parent);
	if (IS_ERR_OR_NULL(subsys)) {
		return -EPROBE_DEFER;
	}
	if (!try_module_get(subsys->pdev->dev.driver->owner)) {
		dev_err(sdev->dev, "error try get dsp subsys module.\n");
		return -EIO;
	}

	get_device(&subsys->pdev->dev);
	priv->dsp_subsys = subsys;
	return 0;
}

void es_dsp_put_subsys(struct snd_sof_dev *sdev)
{
	struct es_dsp_subsys *subsys;
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	subsys = priv->dsp_subsys;
	put_device(&subsys->pdev->dev);
	module_put(subsys->pdev->dev.driver->owner);
	return;
}

static int es_dsp_set_rate(struct es_sof_priv *priv, unsigned long rate)
{
	int ret;

	rate = clk_round_rate(priv->aclk, rate);
	if (rate > 0) {
		ret = clk_set_rate(priv->aclk, rate);
		if (ret) {
			dev_err(priv->dev, "failed to set aclk: %d\n", ret);
			return ret;
		}
	}
	dev_info(priv->dev, "set device rate to %ldHZ\n", rate);
	return 0;
}

int es_dsp_hw_init(struct snd_sof_dev *sdev)
{
	int ret;
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	ret = iommu_map_rsv_iova_with_phys(sdev->dev,
					(dma_addr_t)DSP_UART_IOVA,
					priv->uart_reg_size,
					priv->uart_phyaddr, IOMMU_MMIO);
	if (ret != 0) {
		dev_err(sdev->dev, "uart iommu map error\n");
		return ret;
	}
	priv->uart_dev_addr = DSP_UART_IOVA;

	ret = iommu_map_rsv_iova_with_phys(
					sdev->dev, (dma_addr_t)DSP_UART_MUTEX_IOVA,
					DSP_UART_MUTEX_IOVA_SIZE, priv->uart_mutex_phyaddr,
					IOMMU_MMIO);
	if (ret != 0) {
		dev_err(sdev->dev, "uart mutex iommu map error\n");
		ret = -ENOMEM;
		goto err_map_uart;
	}
	priv->uart_mutex_dev_addr = DSP_UART_MUTEX_IOVA;

	ret = iommu_map_rsv_iova_with_phys(sdev->dev,
					(dma_addr_t)DSP_MBOX_TX_IOVA,
					DSP_MBOX_IOVA_SIZE,
					priv->mbox_tx_phyaddr, IOMMU_MMIO);
	if (ret != 0) {
		dev_err(sdev->dev, "mailbox tx iommu map error\n");
		ret = -ENOMEM;
		goto err_map_uart_mutex; 
	}
	priv->mbox_tx_dev_addr = DSP_MBOX_TX_IOVA;

	ret = iommu_map_rsv_iova_with_phys(sdev->dev,
					(dma_addr_t)DSP_MBOX_RX_IOVA,
					DSP_MBOX_IOVA_SIZE,
					priv->mbox_rx_phyaddr, IOMMU_MMIO);
	if (ret != 0) {
		dev_err(sdev->dev, "mailbox rx iommu map error\n");
		ret = -ENOMEM;
		goto err_map_mbox_tx;
	}
	priv->mbox_rx_dev_addr = DSP_MBOX_RX_IOVA;

	priv->firmware_addr =
		iommu_map_rsv_iova(sdev->dev, (dma_addr_t)DSP_FIRMWARE_IOVA,
						   DSP_FIRMWARE_IOVA_SIZE, GFP_KERNEL, 0);
	if (IS_ERR_OR_NULL(priv->firmware_addr)) {
		dev_err(sdev->dev, "failed to alloc firmware memory\n");
		ret = -ENOMEM;
		goto err_map_mbox_rx;
	}
	priv->firmware_dev_addr = DSP_FIRMWARE_IOVA;
	sdev->bar[SOF_FW_BLK_TYPE_SRAM] = priv->firmware_addr;

	ret = iommu_map_rsv_iova_with_phys(sdev->dev, (dma_addr_t)DSP_RING_BUFFER_IOVA,
					priv->ringbuffer_size,  priv->ringbuffer_phyaddr, IOMMU_MMIO);
	if (ret != 0) {
		dev_err(sdev->dev, "failed to map ringbuffer memory\n");
		ret = -ENOMEM;
		goto err_map_fw;
	}
	priv->ringbuffer_dev_addr = DSP_RING_BUFFER_IOVA;

	if (request_mem_region(priv->dsp_iram_phyaddr, priv->dsp_iram_size,
						   "DSP_IRAM_BASE") == NULL) {
		dev_err(sdev->dev, "request dsp iram mem region error\n");
		ret = -EBUSY;
		goto err_map_rb;
	}
	sdev->bar[SOF_FW_BLK_TYPE_IRAM] = devm_ioremap(sdev->dev, priv->dsp_iram_phyaddr,
													  priv->dsp_iram_size);
	if (IS_ERR_OR_NULL(sdev->bar[SOF_FW_BLK_TYPE_IRAM])) {
		dev_err(sdev->dev, "failed to remap dsp iram\n");
		ret = -ENOMEM;
		goto err_map_iram_region;
	}
	sdev->mmio_bar = SOF_FW_BLK_TYPE_IRAM;

	if (request_mem_region(priv->dsp_dram_phyaddr, priv->dsp_dram_size,
						   "DSP_DRAM_BASE") == NULL) {
		dev_err(sdev->dev, "request dsp dram mem region error\n");
		ret = -EBUSY;
		goto err_map_iram_region;
	}
	sdev->bar[SOF_FW_BLK_TYPE_DRAM] = devm_ioremap(sdev->dev, priv->dsp_dram_phyaddr,
									 				  priv->dsp_dram_size);
	if (IS_ERR_OR_NULL(sdev->bar[SOF_FW_BLK_TYPE_DRAM])) {
		dev_err(sdev->dev, "failed to remap dsp dram\n");
		ret = -ENOMEM;
		goto err;
	}

	es_dsp_set_rate(priv, DSP_SUBSYS_HILOAD_CLK);

	return 0;
err:
	release_mem_region(priv->dsp_dram_phyaddr,  priv->dsp_dram_size);
err_map_iram_region:
	release_mem_region(priv->dsp_iram_phyaddr,  priv->dsp_iram_size);
err_map_rb:
	iommu_unmap_rsv_iova(sdev->dev, NULL,
						 priv->ringbuffer_dev_addr, priv->ringbuffer_size);
err_map_fw:
	iommu_unmap_rsv_iova(sdev->dev, priv->firmware_addr,
						 priv->firmware_dev_addr, DSP_FIRMWARE_IOVA_SIZE);
	priv->firmware_addr = NULL;
err_map_mbox_rx:
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->mbox_rx_dev_addr,
						 DSP_MBOX_IOVA_SIZE);
err_map_mbox_tx:
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->mbox_tx_dev_addr,
						 DSP_MBOX_IOVA_SIZE);
err_map_uart_mutex:
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->uart_mutex_dev_addr,
						 DSP_UART_MUTEX_IOVA_SIZE);
err_map_uart:
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->uart_dev_addr,
						 priv->uart_reg_size);
	return ret;
}