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
