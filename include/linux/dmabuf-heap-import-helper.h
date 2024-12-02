// SPDX-License-Identifier: GPL-2.0
/*
 * Header File of ESWIN DMABUF heap helper APIs
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
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#ifndef _DMABUF_HEAP_IMPORT_H_
#define _DMABUF_HEAP_IMPORT_H_

#include <linux/rbtree.h>
#include <linux/dma-buf.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/eswin_rsvmem_common.h>
#include <linux/es_iommu_rsv.h>

#define SYSTEM_DEV_NODE "system"
#define CMA_DEV_NODE_RES "reserved"
#define CMA_DEV_NODE_DFT "linux,cma"
#define SYSTEM_COHERENT_DEV_NODE "system_coherent"

struct dmaheap_file_private {
	/* private: */
	struct rb_root dmabufs;
	struct rb_root handles;
};

struct heap_root {
	struct dmaheap_file_private fp;
	struct device *dev;
	struct list_head header;
	struct mutex lock;
};

struct heap_mem {
	/* refcount is also protected by lock in the struct heap_root */
	struct kref refcount;

	int dbuf_fd;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *import_attach;

	struct heap_root *root;
	struct list_head list;
	struct rb_node *rb;

	struct sg_table *sgt;
	void *vaddr;

	enum dma_data_direction dir;
	dma_addr_t iova;
	size_t size;
};

struct eswin_split_buffer {
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table; // for Re-formatted splitted sgt
	struct sg_table orig_sg_table; // for originally splitted sgt
	struct page **pages;
	int vmap_cnt;
	void *vaddr;
	unsigned long fd_flags; // for vmap to determin the cache or non-cached mapping

	char name[64]; // export name
	int dbuf_fd; // parent dmabuf fd
	struct dma_buf *dmabuf; // parent dmabuf
	struct esw_slice_buffer {
		__u64 offset;
		size_t len;
	} slice;
};

static int inline common_dmabuf_heap_begin_cpu_access(struct heap_mem *heap_obj)
{
	return dma_buf_begin_cpu_access(heap_obj->dbuf, heap_obj->dir);
}

static int inline common_dmabuf_heap_end_cpu_access(struct heap_mem *heap_obj)
{
	return dma_buf_end_cpu_access(heap_obj->dbuf, heap_obj->dir);
}

static inline size_t common_dmabuf_heap_get_size(struct heap_mem *heap_obj)
{
	WARN_ON(!heap_obj);
	return (heap_obj != NULL) ? heap_obj->dbuf->size : 0;
}

static inline void common_dmabuf_heap_set_dir(struct heap_mem *heap_obj, enum dma_data_direction dir)
{
	WARN_ON(!heap_obj);
	if (heap_obj)
		heap_obj->dir = dir;
}

void common_dmabuf_heap_import_init(struct heap_root *root, struct device *dev);
void common_dmabuf_heap_import_uninit(struct heap_root *root);

struct heap_mem *common_dmabuf_lookup_heapobj_by_fd(struct heap_root *root, int fd);
struct heap_mem *common_dmabuf_lookup_heapobj_by_dma_buf_st(struct heap_root *root, struct dma_buf *dma_buf);

struct heap_mem *common_dmabuf_heap_import_from_user(struct heap_root *root, int fd);
struct heap_mem *common_dmabuf_heap_import_from_user_with_dma_buf_st(struct heap_root *root, struct dma_buf *dma_buf);
void common_dmabuf_heap_release(struct heap_mem *heap_obj);

void *common_dmabuf_heap_map_vaddr(struct heap_mem *heap_obj);
void common_dmabuf_heap_umap_vaddr(struct heap_mem *heap_obj);

struct heap_mem *common_dmabuf_heap_import_from_kernel(struct heap_root *root, char *name, size_t len, unsigned int fd_flags);

int esw_common_dmabuf_split_export(int dbuf_fd, unsigned int offset, size_t len, int fd_flags, char *name);

struct heap_mem *common_dmabuf_heap_rsv_iova_map(struct heap_root *root, int fd, dma_addr_t iova, size_t size);
void common_dmabuf_heap_rsv_iova_unmap(struct heap_mem *heap_obj);
void common_dmabuf_heap_rsv_iova_uninit(struct heap_root *root);

#endif
