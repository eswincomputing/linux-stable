// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file of ESWIN buddy allocator
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#ifndef __ESWIN_BUDDY_H__
#define __ESWIN_BUDDY_H__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
#include <linux/mm_types.h>
#include <asm/atomic.h>
#endif
#include <linux/numa.h>
#include "../buddy.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
static inline void folio_set_order(struct folio *folio, unsigned int order)
{
	if (WARN_ON_ONCE(!order || !folio_test_large(folio)))
		return;

	folio->_flags_1 = (folio->_flags_1 & ~0xffUL) | order;
#ifdef CONFIG_64BIT
	folio->_folio_nr_pages = 1U << order;
#endif
}

static inline void prep_compound_head(struct page *page, unsigned int order)
{
	struct folio *folio = (struct folio *)page;

	folio_set_order(folio, order);
	atomic_set(&folio->_entire_mapcount, -1);
	atomic_set(&folio->_nr_pages_mapped, 0);
	atomic_set(&folio->_pincount, 0);
}
#define set_compound_order(kpage, order) prep_compound_head(kpage, order)
#endif

extern struct page *es_alloc_pages(struct mem_block *memblock, unsigned long order);
extern void es_free_pages(struct mem_block *memblock, struct page *kpage);
extern unsigned long es_num_free_pages(struct mem_block *memblock);

#endif
