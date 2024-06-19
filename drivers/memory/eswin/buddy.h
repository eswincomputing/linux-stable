// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file of ESWIN internal buddy allocator
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#ifndef __BUDDY_H__
#define __BUDDY_H__

#define RUN_IN_KERNEL 1

#ifdef RUN_IN_KERNEL
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/spinlock.h>

// #define buddy_print(fmt...) printk(fmt)
#define buddy_print(fmt...)
#define BUDDY_BUG_ON(condition) WARN_ON(condition)

#define es_spin_lock_init(esLock)	spin_lock_init(esLock)
#define es_spin_lock(esLock)		spin_lock(esLock)
#define es_spin_unlock(esLock)		spin_unlock(esLock)
#define buddy_spin_lock_init(lock)
#define buddy_spin_lock(lock)
#define buddy_spin_unlock(lock)
#else
#include "list.h"
#include <stdio.h>  //printf
#include <string.h> //memset
#include <assert.h> //assert

#define buddy_print(fmt...) printf(fmt)
#define BUDDY_BUG_ON(condition) assert(condition)

#define buddy_spin_lock_init(lock)	do {}while(0)
#define buddy_spin_lock(lock)		do {}while(0)
#define buddy_spin_unlock(lock)		do {}while(0)

#define es_spin_lock_init(esLock)
#define es_spin_lock(esLock)
#define es_spin_unlock(esLock)
#endif

enum esPageflags_e{
	enPG_head,
	enPG_tail,
	enPG_buddy,
};

#define BUDDY_PAGE_SHIFT    PAGE_SHIFT//(12UL)
#define BUDDY_PAGE_SIZE     (1UL << BUDDY_PAGE_SHIFT)
#define BUDDY_MAX_ORDER     MAX_ORDER // (10UL)//(9UL)

struct esPage_s
{
	// spin_lock        lock;
	struct list_head    lru;
	unsigned long       flags;
	union {
		unsigned long   order;
		struct esPage_s     *first_page;
	};
};

struct es_free_area_s
{
	struct list_head    free_list;
	unsigned long       nr_free;
};

struct mem_zone
{
#ifdef RUN_IN_KERNEL
	spinlock_t          lock;
#endif
	unsigned long       page_num;
	unsigned long       page_size;
	struct esPage_s     *first_page;
	unsigned long       start_addr;
	unsigned long       end_addr;
	struct es_free_area_s    free_area[BUDDY_MAX_ORDER];
};

#ifdef RUN_IN_KERNEL
#define BLOCK_MAX_NAME 64
struct mem_block {
#ifdef RUN_IN_KERNEL
	spinlock_t		esLock;
#endif
	struct mem_zone		zone;
	unsigned long		page_num;
	struct esPage_s		*esPagesStart;
	struct page		*kPageStart;
	char name[BLOCK_MAX_NAME];
};
#else
struct mem_block {
	struct mem_zone     zone;
	struct esPage_s        *pages;
};
#endif

void         buddy_system_init(struct mem_block *memblock,
							   struct esPage_s *start_page,
							   unsigned long start_addr,
							   unsigned long page_num);
struct esPage_s *buddy_get_pages(struct mem_zone *zone,
							 unsigned long order);
void         buddy_free_pages(struct mem_zone *zone,
							  struct esPage_s *page);
unsigned long buddy_num_free_page(struct mem_zone *zone);


static inline void __esSetPageHead(struct esPage_s *page)
{
	page->flags |= (1UL<<enPG_head);
}

static inline void __SetPageTail(struct esPage_s *page)
{
	page->flags |= (1UL<<enPG_tail);
}

static inline void __esSetPageBuddy(struct esPage_s *page)
{
	page->flags |= (1UL<<enPG_buddy);
}
/**/
static inline void __esClearPageHead(struct esPage_s *page)
{
	page->flags &= ~(1UL<<enPG_head);
}

static inline void __ClearPageTail(struct esPage_s *page)
{
	page->flags &= ~(1UL<<enPG_tail);
}

static inline void __esClearPageBuddy(struct esPage_s *page)
{
	page->flags &= ~(1UL<<enPG_buddy);
}
/**/
static inline int esPageHead(struct esPage_s *page)
{
	return (page->flags & (1UL<<enPG_head));
}

static inline int esPageTail(struct esPage_s *page)
{
	return (page->flags & (1UL<<enPG_tail));
}

static inline int esPageBuddy(struct esPage_s *page)
{
	return (page->flags & (1UL<<enPG_buddy));
}


static inline void set_page_order_buddy(struct esPage_s *page, unsigned long order)
{
	page->order = order;
	__esSetPageBuddy(page);
}

static inline void rmv_page_order_buddy(struct esPage_s *page)
{
	page->order = 0;
	__esClearPageBuddy(page);
}


static inline unsigned long
__find_buddy_index(unsigned long page_idx, unsigned int order)
{
	return (page_idx ^ (1 << order));
}

static inline unsigned long
__find_combined_index(unsigned long page_idx, unsigned int order)
{
	return (page_idx & ~(1 << order));
}


static inline unsigned long esCompound_order(struct esPage_s *page)
{
	if (!esPageHead(page))
		return 0;

	return page->order;
}

static inline void esSet_compound_order(struct esPage_s *page, unsigned long order)
{
	page->order = order;
}

static inline void BUDDY_BUG(const char *f, int line)
{
	buddy_print("BUDDY_BUG in %s, %d.\n", f, line);
	BUDDY_BUG_ON(1);
}

// print buddy system status
void dump_print(struct mem_zone *zone);
void dump_print_dot(struct mem_zone *zone);

#endif
