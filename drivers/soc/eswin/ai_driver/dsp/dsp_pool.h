// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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

#ifndef __DSP_POOL_H_
#define __DSP_POOL_H_
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/types.h>

struct dsp_pool {
	struct list_head page_list;
	spinlock_t lock;
	size_t size;
	struct device *dev;
	size_t allocation;
	size_t boundary;
};

struct dsp_pool_page {
	struct list_head page_list;
	void *vaddr;
	dma_addr_t dma;
	unsigned int in_use;
	unsigned int offset;
};

void dsp_pool_free(struct dsp_pool *pool, void *vaddr, dma_addr_t dma);
void *dsp_pool_alloc(struct dsp_pool *pool, gfp_t mem_flags,
		     dma_addr_t *handle);

struct dsp_pool *dsp_pool_create(struct device *dev, size_t size,
				 size_t pool_size, size_t align, size_t boundary);
void dsp_pool_destroy(struct dsp_pool *pool);
#endif
