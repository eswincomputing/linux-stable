// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN drm driver
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
 * Authors: Eswin Driver team
 */

#ifndef __ES_GEM_H__
#define __ES_GEM_H__

#include <linux/dma-buf.h>

#include <drm/drm_gem.h>

#include "es_drv.h"

#ifdef CONFIG_ESWIN_MMU
typedef struct _iova_info {
	u32 iova;
	u32 nr_pages;
} iova_info_t;
#endif

/*
 *
 * @base: drm gem object.
 * @size: size requested from user
 * @cookie: cookie returned by dma_alloc_attrs
 *  - not kernel virtual address with DMA_ATTR_NO_KERNEL_MAPPING
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *  - this address could be physical address without IOMMU and
 *  device address with IOMMU.
 * @dma_attrs: attribute for DMA API
 * @get_pages: flag for manually applying for non-contiguous memory.
 * @pages: Array of backing pages.
 * @sgt: Imported sg_table.
 *
 */
struct es_gem_object {
	struct drm_gem_object base;
	size_t size;
	void *cookie;
	dma_addr_t dma_addr;
	u32 iova;
	unsigned long dma_attrs;
	bool get_pages;
	struct page **pages;
	struct sg_table *sgt;
#ifdef CONFIG_ESWIN_MMU
	iova_info_t *iova_list;
	u32 nr_iova;
#endif
};

static inline struct es_gem_object *to_es_gem_object(struct drm_gem_object *obj)
{
	return container_of(obj, struct es_gem_object, base);
}

struct es_gem_object *es_gem_create_object(struct drm_device *dev, size_t size);

int es_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);

int es_gem_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
		       struct drm_mode_create_dumb *args);

int es_gem_mmap(struct file *filp, struct vm_area_struct *vma);

struct sg_table *es_gem_prime_get_sg_table(struct drm_gem_object *obj);

struct drm_gem_object *es_gem_prime_import(struct drm_device *dev,
					   struct dma_buf *dma_buf);
struct drm_gem_object *
es_gem_prime_import_sg_table(struct drm_device *dev,
			     struct dma_buf_attachment *attach,
			     struct sg_table *sgt);

#endif /* __ES_GEM_H__ */
