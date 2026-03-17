// SPDX-License-Identifier: GPL-2.0
/*
 * DVP2AXI VB2 Manager
 *
 * Copyright (C) 2026 Beijing ESWIN Computing Technology Co., Ltd.
 *
 * VB2 Manager implementation for DVP2AXI driver to improve
 * multi-channel streaming stability and reduce memory allocation
 * overhead during start/stop cycles.
 */

#ifndef _DVP2AXI_VB2_H
#define _DVP2AXI_VB2_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/dma-direction.h>
#include <media/videobuf2-core.h>

/* Memory pool configuration */
#define DVP2AXI_BLOCK_ALIGN		4096
#define DVP2AXI_DEFAULT_BLOCK_SIZE (20 * 1024 * 1024) /* 20MB */
#define DVP2AXI_DEFAULT_BLOCK_NUM	20
/* Memory block states */
enum dvp2axi_block_state {
	DVP2AXI_BLOCK_FREE = 0,
	DVP2AXI_BLOCK_ALLOCATED,
	DVP2AXI_BLOCK_MAPPED,
};

struct dvp2axi_mem_block {
	struct list_head list;
	struct dvp2axi_mem_pool *pool;
	void *vaddr;
	dma_addr_t dma_addr;
	size_t size;
	bool is_dynamic;
};

struct dvp2axi_mem_pool {
	struct device *dev;
	const char *name;
	struct kobject kobj;

	struct list_head free_list;
	struct list_head used_list;
	spinlock_t lock;

	size_t buf_size;
	unsigned int num_total;
	unsigned int num_free;
	unsigned int peak_used;
	unsigned int auto_grow;
	unsigned int max_total;

	atomic_t alloc_count;
	atomic_t free_count;
	atomic_t dynamic_count;
};

/**
 * struct dvp2axi_vb2_buffer - Custom vb2 buffer with memory pool support
 * @vb: Standard vb2_buffer
 * @block: Pointer to memory block
 * @pool: Pointer to memory pool
 */
struct dvp2axi_vb2_buffer {
	struct vb2_buffer vb;
	struct dvp2axi_mem_block *block;
	struct dvp2axi_mem_pool *pool;
};

struct dvp2axi_mem_pool *dvp2axi_mem_pool_create(struct device *dev,
						  const char *name,
						  size_t buf_size,
						  unsigned int num_bufs);
void dvp2axi_mem_pool_destroy(struct dvp2axi_mem_pool *pool);

struct dvp2axi_mem_block *dvp2axi_mem_pool_alloc(struct dvp2axi_mem_pool *pool,
						  size_t size);
void dvp2axi_mem_pool_free(struct dvp2axi_mem_block *block);

unsigned int dvp2axi_mem_pool_grow(struct dvp2axi_mem_pool *pool,
				   unsigned int count);
unsigned int dvp2axi_mem_pool_shrink(struct dvp2axi_mem_pool *pool,
				     unsigned int count);
int dvp2axi_mem_pool_resize(struct dvp2axi_mem_pool *pool, size_t new_size);

int dvp2axi_mem_pool_sysfs_init(struct dvp2axi_mem_pool *pool,
				struct kobject *parent_kobj);
void dvp2axi_mem_pool_sysfs_cleanup(struct dvp2axi_mem_pool *pool);

void dvp2axi_mem_pool_dump_stats(struct dvp2axi_mem_pool *pool);
size_t dvp2axi_mem_pool_get_free(struct dvp2axi_mem_pool *pool);
size_t dvp2axi_mem_pool_get_used(struct dvp2axi_mem_pool *pool);

extern const struct vb2_mem_ops dvp2axi_vb2_mem_ops;

#endif