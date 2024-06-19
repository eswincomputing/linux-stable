/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#ifndef _ESWIN_HEAPS_H
#define _ESWIN_HEAPS_H

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
#define dma_buf_map		iosys_map
#define dma_buf_map_set_vaddr	iosys_map_set_vaddr
#define dma_buf_map_clear	iosys_map_clear
#else
#define vm_flags_clear(vma, flags)	(vma->vm_flags &= ~flags)
#endif

struct eswin_heap;

/**
 * struct eswin_heap_ops - ops to operate on a given heap
 * @allocate:		allocate dmabuf and return struct dma_buf ptr
 *
 * allocate returns dmabuf on success, ERR_PTR(-errno) on error.
 */
struct eswin_heap_ops {
	struct dma_buf *(*allocate)(struct eswin_heap *heap,
				    unsigned long len,
				    unsigned long fd_flags,
				    unsigned long heap_flags);
};

/**
 * struct eswin_heap_export_info - information needed to export a new dmabuf heap
 * @name:	used for debugging/device-node name
 * @ops:	ops struct for this heap
 * @priv:	heap exporter private data
 *
 * Information needed to export a new dmabuf heap.
 */
struct eswin_heap_export_info {
	const char *name;
	const struct eswin_heap_ops *ops;
	void *priv;
};

/**
 * eswin_heap_get_drvdata() - get per-heap driver data
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-heap data for the heap.
 */
void *eswin_heap_get_drvdata(struct eswin_heap *heap);

/**
 * eswin_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *eswin_heap_get_name(struct eswin_heap *heap);

/**
 * eswin_heap_add - adds a heap to dmabuf heaps
 * @exp_info:		information needed to register this heap
 */
struct eswin_heap *eswin_heap_add(const struct eswin_heap_export_info *exp_info);

/**
 * eswin_heap_delete - delete a heap from dmabuf heaps
 * @heap:		heap needed to delete
 */
int eswin_heap_delete(struct eswin_heap *heap);

/**
 * eswin_heap_delete_by_name - find and delete a heap from dmabuf heaps by name
 * @name:		heap name needed to delete
 */
int eswin_heap_delete_by_name(const char *name);

int eswin_heap_kalloc(char *name, size_t len, unsigned int fd_flags, unsigned int heap_flags);

int eswin_heap_init(void);
void eswin_heap_uninit(void);
#define dma_heap_kalloc(name, len, fd_flags, heap_flags)  eswin_heap_kalloc(name, len, fd_flags, heap_flags)
#endif /* _ESWIN_HEAPS_H */
