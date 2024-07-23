// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/dma-buf.h>
#include <linux/dsp_dma_buf.h>
#include <linux/device.h>
#include "dsp_main.h"

int dsp_attach_sram_dmabuf(struct device *dev, struct dma_buf *dmabuf)
{
	int ret;
	struct attach;
	struct es_dsp *dsp;
	dma_addr_t dma_addr = 0;

	if (dev == NULL) {
		dsp_err("%s, %d, device is null, err.\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (dmabuf == NULL) {
		dsp_err("%s, %d, param dmabuf is null, err.\n", __func__,
			__LINE__);
		return -EINVAL;
	}
	get_dma_buf(dmabuf);
	dsp = dev_get_drvdata(dev);

	dma_addr = dev_mem_attach(dmabuf, dev, DMA_BIDIRECTIONAL,
				  &dsp->sram_attach);
	if (dma_addr == 0) {
		dsp_err("err:dev_mem_attach failed!\n");
		dsp->sram_dma_addr = 0;
		dsp->sram_phy_addr = 0;
		return -ENOMEM;
	}
	dsp->sram_phy_addr = sg_phys(dsp->sram_attach->sgt->sgl);
	dsp->sram_dma_addr = dma_addr;
	return 0;
}
EXPORT_SYMBOL(dsp_attach_sram_dmabuf);

int dsp_detach_sram_dmabuf(struct device *dev)
{
	struct es_dsp *dsp;
	struct dma_buf *dmabuf;

	if (dev == NULL) {
		return -EINVAL;
	}

	dsp = dev_get_drvdata(dev);
	if (dsp == NULL) {
		dsp_err("dsp device is null.\n");
		return -EINVAL;
	}

	if (!dsp->sram_attach) {
		return -EINVAL;
	}
	dmabuf = dsp->sram_attach->dmabuf;
	if (dmabuf == NULL) {
		return -EINVAL;
	}
	dev_mem_detach(dsp->sram_attach, DMA_BIDIRECTIONAL);
	dma_buf_put(dmabuf);
	dsp->sram_attach = NULL;
	dsp->sram_phy_addr = 0;
	dsp->sram_dma_addr = 0;
	return 0;
}
EXPORT_SYMBOL(dsp_detach_sram_dmabuf);

u32 dsp_get_sram_iova_by_addr(struct device *dev, u64 phy_addr)
{
	struct es_dsp *dsp;

	if (dev == NULL) {
		return 0;
	}

	dsp = dev_get_drvdata(dev);
	if (dsp == NULL) {
		dsp_err("dsp device is null.\n");
		return 0;
	}
	if (dsp->sram_phy_addr == 0) {
		dsp_err("dsp sram addr is not map, error.\n");
		return 0;
	}

	return (phy_addr - dsp->sram_phy_addr + dsp->sram_dma_addr);
}
EXPORT_SYMBOL(dsp_get_sram_iova_by_addr);
