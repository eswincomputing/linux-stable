// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __DSP_DMA_BUF_H_
#define __DSP_DMA_BUF_H_
#include <linux/dma-buf.h>

struct dev_buffer_desc {
	struct dma_buf *buf;
	u32 offset;
	u32 len;
};

enum es_malloc_policy {
	ES_MEM_ALLOC_RSV,
	ES_MEM_ALLOC_NORMAL,
	ES_MEM_ALLOC_NORMAL_COHERENT,
	ES_MEM_ALLOC_CMA,
	ES_MEM_ALLOC_CMA_COHERENT,
	ES_MEM_ALLOC_CMA_LLC,
	ES_MEM_ALLOC_SPRAM_DIE0,
	ES_MEM_ALLOC_SPRAM_DIE1,
};

int dev_mem_alloc(size_t size, enum es_malloc_policy policy,
		   struct dma_buf **buf);
int dev_mem_free(struct dma_buf *buf);

int dev_mem_alloc_pool(size_t size, enum es_malloc_policy policy, char *mmb,
		       char *zone, struct dma_buf **buf);

dma_addr_t dev_mem_attach(struct dma_buf *buf, struct device *dev,
			  enum dma_data_direction direc,
			  struct dma_buf_attachment **attachment);
int dev_mem_detach(struct dma_buf_attachment *attach,
		   enum dma_data_direction direction);
void *dev_mem_vmap(struct dev_buffer_desc *dev_buffer);
void dev_mem_vunmap(struct dev_buffer_desc *dev_buf, void *vaddr);

#endif
