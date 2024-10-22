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

#ifndef DLA_DRIVER_H
#define DLA_DRIVER_H

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/mailbox_controller.h>

#include "dla_interface.h"
#include "hetero_common.h"

struct nvdla_device {
	int numa_id;
	int32_t npu_irq;
	int32_t dla_irq;
	void __iomem *base;
	void __iomem *e31_mmio_base;
	void __iomem *emission_base;
	void __iomem *program_base;
	void __iomem *uart_mutex_base;
	char *e31_fw_virt_base;
	dma_addr_t e31_nim_iova;
	uint32_t e31_fw_size;
	struct mutex task_mutex;
	void *engine_context;
	void *win_engine;
	struct mbox_chan *mbx_chan;
	struct platform_device *mbox_pdev;

	void *executor;
	struct platform_device *pdev;
	bool irq_happened;
	wait_queue_head_t event_wq;
	spinlock_t nvdla_lock;
	struct dla_buffer_object *spram_bobj;
	uint64_t spram_base_addr;
	uint32_t spram_size;
	void *edma;
	bool perf_stat;

	struct reset_control *rstc_e31_core;
	struct clk *e31_core_clk;

	struct clk *mbox_pclk_device;
	struct clk *mbox_pclk;
	int32_t mbox_irq;
	struct reset_control *mbox_rst_device;
	struct reset_control *mbox_rst;
	void __iomem *mbox_rx_base;
	void __iomem *mbox_tx_base;
	u32 mbox_tx_lock_bit;
	u32 mbox_tx_irq_bit;
	spinlock_t mbox_lock;

	uint16_t *pause_op_list;
};

void dla_reg_write(struct nvdla_device *dev, uint32_t addr, uint32_t value);

uint32_t dla_reg_read(struct nvdla_device *dev, uint32_t addr);

#endif
