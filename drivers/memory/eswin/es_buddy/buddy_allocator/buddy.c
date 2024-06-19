// SPDX-License-Identifier: GPL-2.0-only
/*
 * Internal APIs for ESWIN buddy allocator
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#include "../../buddy.h"

#define SIZE_4GB    0x100000000
static int is_4G_boundary_page(struct mem_block *memblock, unsigned long page_idx)
{
	struct page *kpage;

    kpage = memblock->kPageStart + page_idx;
    if (!((page_to_phys(kpage) + BUDDY_PAGE_SIZE) % SIZE_4GB))
        return 1;

    return 0;
}

void buddy_system_init(struct mem_block *memblock,
                       struct esPage_s *start_page,
                       unsigned long start_addr,
                       unsigned long page_num)
{
    unsigned long i;
    struct esPage_s *page = NULL;
    struct es_free_area_s *area = NULL;
    // init memory zone
    struct mem_zone *zone = &memblock->zone;
    zone->page_num = page_num;
    zone->page_size = BUDDY_PAGE_SIZE;
    zone->first_page = start_page;
    zone->start_addr = start_addr;
    zone->end_addr = start_addr + page_num * BUDDY_PAGE_SIZE;
    // TODO: init zone->lock
    #ifdef RUN_IN_KERNEL
    buddy_spin_lock_init(&zone->lock);
    #endif
    // init each area
    for (i = 0; i < BUDDY_MAX_ORDER; i++)
    {
        area = zone->free_area + i;
        INIT_LIST_HEAD(&area->free_list);
        area->nr_free = 0;
    }
    memset(start_page, 0, page_num * sizeof(struct esPage_s));
    // init and free each page
    for (i = 0; i < page_num; i++)
    {
        page = zone->first_page + i;
        INIT_LIST_HEAD(&page->lru);
        /* Reserve 4kB at (4GB-4k) alignment address boundary. This is a workaround for g2d.
           The g2d hardware has problem with accessing the 4GB alignment address boundray,
           such as the address at 4GB, 8GB, 12GB and 16GB.
         */
        if (is_4G_boundary_page(memblock, i)) {
            memblock->page_num--;
            continue;
        }

        buddy_free_pages(zone, page);
    }
}

static void prepare_compound_pages(struct esPage_s *page, unsigned long order)
{
    unsigned long i;
    unsigned long nr_pages = (1UL<<order);

    esSet_compound_order(page, order);
    __esSetPageHead(page);
    for(i = 1; i < nr_pages; i++)
    {
        struct esPage_s *p = page + i;
        __SetPageTail(p);
        p->first_page = page;
    }
}

static void expand(struct mem_zone *zone, struct esPage_s *page,
                   unsigned long low_order, unsigned long high_order,
                   struct es_free_area_s *area)
{
    unsigned long size = (1U << high_order);
    while (high_order > low_order)
    {
        area--;
        high_order--;
        size >>= 1;
        list_add(&page[size].lru, &area->free_list);
        area->nr_free++;
        // set page order
        set_page_order_buddy(&page[size], high_order);
    }
}

static struct esPage_s *__alloc_page(unsigned long order,
                                 struct mem_zone *zone)
{
    struct esPage_s *page = NULL;
    struct es_free_area_s *area = NULL;
    unsigned long current_order = 0;

    for (current_order = order;
         current_order < BUDDY_MAX_ORDER; current_order++)
    {
        area = zone->free_area + current_order;
        if (list_empty(&area->free_list)) {
            continue;
        }
        // remove closest size page
        page = list_entry(area->free_list.next, struct esPage_s, lru);
        list_del(&page->lru);
        rmv_page_order_buddy(page);
        area->nr_free--;
        // expand to lower order
        expand(zone, page, order, current_order, area);
        // compound page
        if (order > 0)
            prepare_compound_pages(page, order);
        else // single page
            page->order = 0;
        return page;
    }
    return NULL;
}

struct esPage_s *buddy_get_pages(struct mem_zone *zone,
                             unsigned long order)
{
    struct esPage_s *page = NULL;

    if (order >= BUDDY_MAX_ORDER)
    {
        BUDDY_BUG(__FILE__, __LINE__);
        return NULL;
    }
    //TODO: lock zone->lock
    buddy_spin_lock(&zone->lock);
    page = __alloc_page(order, zone);
    //TODO: unlock zone->lock
    buddy_spin_unlock(&zone->lock);
    return page;
}

static int destroy_compound_pages(struct esPage_s *page, unsigned long order)
{
    int bad = 0;
    unsigned long i;
    unsigned long nr_pages = (1UL<<order);

    __esClearPageHead(page);
    for(i = 1; i < nr_pages; i++)
    {
        struct esPage_s *p = page + i;
        if( !esPageTail(p) || p->first_page != page )
        {
            bad++;
            BUDDY_BUG(__FILE__, __LINE__);
        }
        __ClearPageTail(p);
    }
    return bad;
}

#define PageCompound(page) \
        (page->flags & ((1UL<<enPG_head)|(1UL<<enPG_tail)))

#define page_is_buddy(page,order) \
        (esPageBuddy(page) && (page->order == order))

void buddy_free_pages(struct mem_zone *zone,
                      struct esPage_s *page)
{
    unsigned long order = esCompound_order(page);
    unsigned long buddy_idx = 0, combinded_idx = 0;
    unsigned long page_idx = page - zone->first_page;
    //TODO: lock zone->lock
    buddy_spin_lock(&zone->lock);
    if (PageCompound(page))
        if (destroy_compound_pages(page, order))
            BUDDY_BUG(__FILE__, __LINE__);

    while (order < BUDDY_MAX_ORDER-1)
    {
        struct esPage_s *buddy;
        // find and delete buddy to combine
        buddy_idx = __find_buddy_index(page_idx, order);
        buddy = page + (buddy_idx - page_idx);
        if (!page_is_buddy(buddy, order))
            break;
        list_del(&buddy->lru);
        zone->free_area[order].nr_free--;
        // remove buddy's flag and order
        rmv_page_order_buddy(buddy);
        // update page and page_idx after combined
        combinded_idx = __find_combined_index(page_idx, order);
        page = page + (combinded_idx - page_idx);
        page_idx = combinded_idx;
        order++;
    }
    set_page_order_buddy(page, order);
    list_add(&page->lru, &zone->free_area[order].free_list);
    zone->free_area[order].nr_free++;
    //TODO: unlock zone->lock
     buddy_spin_unlock(&zone->lock);
}

unsigned long buddy_num_free_page(struct mem_zone *zone)
{
    unsigned long i, ret;
    for (i = 0, ret = 0; i < BUDDY_MAX_ORDER; i++)
    {
        ret += zone->free_area[i].nr_free * (1UL<<i);
    }
    return ret;
}
