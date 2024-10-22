// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#include <linux/dma-buf.h>
#include "dsp_dma_buf.h"
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
