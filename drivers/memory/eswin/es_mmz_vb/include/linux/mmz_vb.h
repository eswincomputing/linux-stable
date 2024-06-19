// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#ifndef __MMZ_VB_H__
#define __MMZ_VB_H__

#include <linux/types.h>
#include "../../../eswin_memblock.h"
#include "../../../es_buddy/es_buddy.h"


/**
 *  select whether the block will be memset to 0 while exporting it as dmabuf.
 *  1: memset
 *  0: do NOT memset
*/
// #define MMZ_VB_DMABUF_MEMSET	1

#define MMZ_VB_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE)

#define VB_K_POOL_MAX_ID	INT_MAX

/* one block info organized in kernel */
typedef struct esVB_K_BLOCK_INFO_S {
	struct page *cma_pages;
	struct sg_table sg_table; // for buddy allocator
	struct esVB_K_POOL_INFO_S *pool;
	int nr;
}VB_K_BLOCK_INFO_S;

/* one pool info organized in kernel */
typedef struct esVB_K_POOL_INFO_S {
	s32 poolId;
	struct esVB_POOL_CONFIG_S poolCfg;
	unsigned long *bitmap; // used for block get/release managment
	struct esVB_K_BLOCK_INFO_S *blocks; // point to the block array
	struct esVB_K_MMZ_S *partitions; // poiont to the partitions
	struct hlist_node node;
	spinlock_t lock;
	enum esVB_UID_E enVbUid;
	unsigned long flag;
}VB_K_POOL_INFO_S;

/* MMZs info in kernel */
typedef struct esVB_K_MMZ_S {
	u32 partCnt;
	// pool is found via idr api
	struct idr pool_idr;
	struct rw_semaphore idr_lock;
	struct mem_block *mem_blocks[ES_VB_MAX_MMZs];
}VB_K_MMZ_S;

#endif
