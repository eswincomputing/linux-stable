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

#ifndef _DSP_IOCTL_DEFS_H
#define _DSP_IOCTL_DEFS_H

#include "eswin-khandle.h"
#include "dsp_ioctl_if.h"
#include "dsp_hw_if.h"
struct es_dsp;

#define OPERATOR_DIR_MAXLEN 512

struct dsp_ioctl_alloc {
	u32 size;
	u32 align;
	u64 addr;
	u64 dev_addr;
	u64 dbuf_fd;
	u64 dbuf_phys;
};

struct dsp_ioctl_task {
	es_dsp_h2d_msg msg;
	u32 flat_size;
} __attribute__((packed));

struct dsp_file {
	struct es_dsp *dsp;
	/* async_ll_complete and async_ll_lock can only for low level interface*/
	struct list_head async_ll_complete;
	spinlock_t async_ll_lock;
	wait_queue_head_t async_ll_wq;
	struct dsp_ioctl_alloc *ioctl_alloc;
	struct mutex xrray_lock;
	struct xarray buf_xrray;
	struct khandle h;
};

enum {
	DSP_FLAG_READ = 0x1,
	DSP_FLAG_WRITE = 0x2,
	DSP_FLAG_READ_WRITE = 0x3,
};

extern const struct file_operations dsp_fops;
#endif
