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

#ifndef __DSP_PLATFORM_INTF_H_
#define __DSP_PLATFORM_INTF_H_

#include <linux/platform_device.h>
#include "dsp_main.h"
struct es_dsp_hw;
int es_dsp_send_irq(struct es_dsp_hw *, dsp_request_t *);
int es_dsp_reboot_core(struct es_dsp_hw *);
int es_dsp_enable(struct es_dsp_hw *);
void es_dsp_disable(struct es_dsp_hw *);
int es_dsp_set_rate(struct es_dsp_hw *, unsigned long rate);
void es_dsp_reset(struct es_dsp_hw *);
void es_dsp_halt(struct es_dsp_hw *);
void es_dsp_release(struct es_dsp_hw *);
int es_dsp_sync(struct es_dsp *dsp);
int es_dsp_load_op(struct es_dsp_hw *, void *op_ptr);

int es_dsp_platform_init(void);
int es_dsp_platform_uninit(void);
int es_dsp_hw_init(struct es_dsp *dsp);
void es_dsp_hw_uninit(struct es_dsp *dsp);
void reset_uart_mutex(struct es_dsp_hw *hw);

int es_dsp_pm_get_sync(struct es_dsp *dsp);
void es_dsp_pm_put_sync(struct es_dsp *dsp);

int dsp_request_firmware(struct es_dsp *dsp);
void dsp_release_firmware(struct es_dsp *dsp);

long es_dsp_hw_ioctl(struct file *flip, unsigned int cmd, unsigned long arg);
void dsp_send_invalid_code_seg(struct es_dsp_hw *hw, struct dsp_op_desc *op);
int dsp_load_op_file(struct es_dsp *dsp, struct dsp_op_desc *op);
void dsp_free_flat_mem(struct es_dsp *dsp, u32 size, void *cpu,
		       dma_addr_t dma_addr);
void *dsp_alloc_flat_mem(struct es_dsp *dsp, u32 dma_len, dma_addr_t *dma_addr);
int es_dsp_clk_enable(struct es_dsp *);
int es_dsp_clk_disable(struct es_dsp *dsp);
int es_dsp_core_clk_enable(struct es_dsp *dsp);
int es_dsp_core_clk_disable(struct es_dsp *dsp);

int dsp_get_resource(struct platform_device *pdev, struct es_dsp *dsp);
int dsp_put_resource(struct es_dsp *dsp);

int es_dsp_get_subsys(struct platform_device *pdev, struct es_dsp *dsp);
void dsp_free_hw(struct es_dsp *dsp);
int dsp_alloc_hw(struct platform_device *pdev, struct es_dsp *dsp);
int dsp_enable_irq(struct es_dsp *dsp);
void dsp_disable_irq(struct es_dsp *dsp);
void dsp_disable_mbox_clock(struct es_dsp *dsp);
int dsp_enable_mbox_clock(struct es_dsp *dsp);
int wait_for_current_tsk_done(struct es_dsp *dsp);

#endif
