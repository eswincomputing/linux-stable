// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN hdcp driver
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

#ifndef DW_HDMI_HDCP_H
#define DW_HDMI_HDCP_H

#include <linux/miscdevice.h>
#include "dsp_dma_buf.h"

#define DW_HDCP_DRIVER_NAME "dw-hdmi-hdcp"
#define HDCP_PRIVATE_KEY_SIZE 280
#define HDCP_KEY_SHA_SIZE 20

struct hdcp_keys {
	u8 KSV[8];
	u8 devicekey[HDCP_PRIVATE_KEY_SIZE];
	u8 sha1[HDCP_KEY_SHA_SIZE];
};

typedef struct {
	int allocated, initialized;
	int code_loaded;

	int code_is_phys_mem;
	dma_addr_t code_base;
	uint32_t code_size;
	uint8_t *code;
	int data_is_phys_mem;
	dma_addr_t data_base;
	uint32_t data_size;
	uint8_t *data;

	struct resource *hpi_resource;
	uint8_t __iomem *hpi;
	struct device *dev;
} hl_device;

struct dw_hdcp2 {
	int numa_id;
	int enable;
	void (*start)(struct dw_hdcp2* hdcp2);
	void (*stop)(struct dw_hdcp2* hdcp2);

	struct device *dev;
	int wait_hdcp2_reset;
	int hot_plug;
	struct miscdevice mdev;
	int auth_sucess;
	hl_device *hld;
	struct miscdevice mics_hld;
	struct clk *aclk;
	struct clk *iesmclk;

	struct dev_buffer_desc code_buffer;
	struct dev_buffer_desc data_buffer;
	struct dma_buf_attachment *code_attach;
	struct dma_buf_attachment *data_attach;
};

struct dw_hdcp {
	bool enable;
	int retry_times;
	int remaining_times;
	char *seeds;
	int invalidkey;
	char *invalidkeys;
	int hdcp2_enable;
	int status;
	u32 reg_io_width;
	int numa_id;

	struct dw_hdcp2 *hdcp2;
	struct miscdevice mdev;
	struct hdcp_keys *keys;
	struct device *dev;
	struct dw_hdmi *hdmi;
	void __iomem *regs;

	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
	int (*hdcp_start)(struct dw_hdcp *hdcp);
	int (*hdcp_stop)(struct dw_hdcp *hdcp);
	void (*hdcp_isr)(struct dw_hdcp *hdcp, int hdcp_int);
};

extern u8 tv_hdmi_hdcp2_support(struct dw_hdmi *hdmi);
extern void dw_hdmi_hdcp2_init(struct dw_hdcp2 *hdcp2);
extern void dw_hdmi_hdcp2_remove(struct dw_hdcp2 *hdcp2);
extern void dw_hdmi_hdcp2_start(int enable, int numa_id);
#endif
