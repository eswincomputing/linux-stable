// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __DSP_PLATFORM_INTF_H_
#define __DSP_PLATFORM_INTF_H_

#include <linux/platform_device.h>
#include "dsp_main.h"
struct es_dsp_hw;
void es_dsp_send_irq(struct es_dsp_hw *, dsp_request_t *);
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

#endif
