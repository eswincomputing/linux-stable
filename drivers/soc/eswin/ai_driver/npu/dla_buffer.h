// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef DLA_BUFFER_H
#define DLA_BUFFER_H
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include "internal_interface.h"
#include "dsp_dma_buf.h"

struct user_model;

struct dla_buffer_object {
	int32_t fd;
	void *vaddr;
	dma_addr_t dma_addr;
	uint32_t size;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
};

struct npu_dma_buf_ex {
	struct khandle handle;
	ES_DEV_BUF_S buf_info;
	struct dla_buffer_object obj;
};

struct dla_buffer_object *dla_alloc_dmabuf(size_t size,
					   enum es_malloc_policy policy);
int dla_attach_dmabuf(struct dla_buffer_object *dobj,
		      struct device *attach_dev);
int dla_detach_dmabuf(struct dla_buffer_object *dobj);
void *dla_dmabuf_vmap(struct dla_buffer_object *dobj);
void dla_dmabuf_vunmap(struct dla_buffer_object *dobj);
void dla_release_bobj(struct dla_buffer_object *dobj);
struct dla_buffer_object *dla_import_fd_to_device(int fd,
						  struct device *attach_dev);
int dla_import_dmabuf_from_model(struct user_model *model);
int dla_detach_dmabuf_from_model(struct user_model *model);

int dla_dmabuf_vmap_for_model(struct user_model *model);
void dla_dmabuf_vunmap_for_model(struct user_model *model);

int dla_mapdma_buf_to_dev(int fd, struct dla_buffer_object *obj,
			  struct device *attach_dev);
void dla_unmapdma_buf_to_dev(struct dla_buffer_object *obj);
#endif
