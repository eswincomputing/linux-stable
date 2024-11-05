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

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include "dsp_pool.h"

static void dsp_pool_init_page(struct dsp_pool *pool,
			       struct dsp_pool_page *page)
{
	unsigned int offset = 0;
	unsigned int next_b = pool->boundary;

	do {
		unsigned int next = offset + pool->size;
		if (unlikely((next + pool->size) >= next_b)) {
			next = next_b;
			next_b += pool->boundary;
		}
		*(int *)(page->vaddr + offset) = next;
		offset = next;
	} while (offset < pool->allocation);
}

static struct dsp_pool_page *dsp_pool_alloc_page(struct dsp_pool *pool,
						 gfp_t mem_flags)
{
	struct dsp_pool_page *page;
	page = kmalloc(sizeof(*page), mem_flags);
	if (!page)
		return NULL;

	page->vaddr = dma_alloc_coherent(pool->dev, pool->allocation,
					 &page->dma, mem_flags);
	if (page->vaddr) {
		dsp_pool_init_page(pool, page);
		page->in_use = 0;
		page->offset = 0;
	} else {
		kfree(page);
		page = NULL;
	}
	return page;
}

struct dsp_pool *dsp_pool_create(struct device *dev, size_t size,
				 size_t pool_size, size_t align, size_t boundary)
{
	size_t allocation;
	struct dsp_pool *retval;
	struct dsp_pool_page *page;

	if (align == 0) {
		align = 1;
	} else if (align & (align - 1)) {
		return NULL;
	}

	if (size == 0) {
		return NULL;
	} else if (size < 4) {
		size = 4;
	}

	size = ALIGN(size, align);
	allocation = ALIGN(pool_size, PAGE_SIZE);
	if (!boundary) {
		boundary = allocation;
	} else if (boundary < size || (boundary & (boundary - 1)))
		return NULL;

	retval = kmalloc(sizeof(*retval), GFP_KERNEL);
	if (!retval)
		return retval;

	retval->dev = dev;
	INIT_LIST_HEAD(&retval->page_list);
	spin_lock_init(&retval->lock);
	retval->size = size;
	retval->allocation = allocation;
	retval->boundary = boundary;
	page = dsp_pool_alloc_page(retval, GFP_KERNEL & (~__GFP_ZERO));
	if (page) {
		list_add(&page->page_list, &retval->page_list);
	}
	return retval;
}

void *dsp_pool_alloc(struct dsp_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{
	unsigned long flags;
	struct dsp_pool_page *page;
	void *retval;
	size_t offset;

	spin_lock_irqsave(&pool->lock, flags);
	list_for_each_entry(page, &pool->page_list, page_list) {
		if (page->offset < pool->allocation)
			goto ready;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	page = dsp_pool_alloc_page(pool, mem_flags & (~__GFP_ZERO));
	if (!page)
		return NULL;
	spin_lock_irqsave(&pool->lock, flags);
	list_add(&page->page_list, &pool->page_list);
ready:
	page->in_use++;
	offset = page->offset;
	page->offset = *(int *)(page->vaddr + offset);
	retval = offset + page->vaddr;
	*handle = offset + page->dma;
	spin_unlock_irqrestore(&pool->lock, flags);
	return retval;
}

static struct dsp_pool_page *dsp_pool_find_page(struct dsp_pool *pool,
						dma_addr_t dma)
{
	struct dsp_pool_page *page;
	list_for_each_entry(page, &pool->page_list, page_list) {
		if (dma < page->dma)
			continue;
		if ((dma - page->dma) < pool->allocation)
			return page;
	}
	return NULL;
}

void dsp_pool_free(struct dsp_pool *pool, void *vaddr, dma_addr_t dma)
{
	struct dsp_pool_page *page;
	unsigned long flags;
	unsigned int offset;

	spin_lock_irqsave(&pool->lock, flags);

	page = dsp_pool_find_page(pool, dma);
	if (!page) {
		spin_unlock_irqrestore(&pool->lock, flags);
		return;
	}

	offset = vaddr - page->vaddr;
	page->in_use--;
	*(int *)vaddr = page->offset;
	page->offset = offset;
	spin_unlock_irqrestore(&pool->lock, flags);
}

static void dsp_pool_free_page(struct dsp_pool *pool,
			       struct dsp_pool_page *page)
{
	dma_addr_t dma = page->dma;
	dma_free_coherent(pool->dev, pool->allocation, page->vaddr, dma);
}

void dsp_pool_destroy(struct dsp_pool *pool)
{
	struct dsp_pool_page *page, *tmp;
	if (!pool) {
		return;
	}

	list_for_each_entry_safe(page, tmp, &pool->page_list, page_list) {
		if (!page->in_use) {
			dsp_pool_free_page(pool, page);
		}
		list_del(&page->page_list);
		kfree(page);
	}
	kfree(pool);
}
