/*
 * ESWIN buddy allocator.
 * eswin_rsvmem initializes the reserved memory in dst that has compatible = "eswin-reserve-memory" and
 * no-map property. Each of these memory region will be treated as one memory block and managed by eswin
 * buddy system. Users can allocate/frree pages from/to these memory blocks via es_alloc_pages/es_free_pages.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#define pr_fmt(fmt) "eswin_buddy: " fmt

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/kmemleak.h>

#include "../eswin_memblock.h"
#include "es_buddy.h"


static void es_buddy_system_init(struct mem_block *memblock,
                       unsigned long start_addr,
                       unsigned long page_num)
{
	struct esPage_s *start_page = memblock->esPagesStart;

	es_spin_lock_init(&memblock->esLock);
	buddy_system_init(memblock, start_page, start_addr, page_num);
}

struct page *es_alloc_pages(struct mem_block *memblock,
					unsigned long order)
{
	struct esPage_s *page;
	struct page *kpage;
	struct mem_zone *zone = &memblock->zone;
	unsigned long page_idx;

	es_spin_lock(&memblock->esLock);
	page = buddy_get_pages(zone, order);
	if (NULL == page) {
		es_spin_unlock(&memblock->esLock);
		return NULL;
	}

	page_idx = page - zone->first_page;
	kpage = memblock->kPageStart + page_idx;

	if (order > 0) {
		__SetPageHead(kpage);
		set_compound_order(kpage, order);
	}
	es_spin_unlock(&memblock->esLock);

	buddy_print("%s:input order=%ld, esCompound_order(page)=%ld, kCompound_order(kpage)=%d, page_size(kpage)=0x%lx, phys_addr=0x%llx\n",
			__func__, order, esCompound_order(page), compound_order(kpage), page_size(kpage), page_to_phys(kpage));
	return kpage;
}
EXPORT_SYMBOL(es_alloc_pages);

void es_free_pages(struct mem_block *memblock,
					struct page *kpage)
{
	struct mem_zone *zone = &memblock->zone;
	unsigned long page_idx = kpage - memblock->kPageStart;
	struct esPage_s *page = zone->first_page + page_idx;
	unsigned long order = esCompound_order(page);

	buddy_print("%s:esCompound_order(page)=%ld, kCompound_order(kpage)=%d, page_idx=0x%lx, page_size(kpage)=0x%lx, phys_addr=0x%llx\n",
			__func__, esCompound_order(page), compound_order(kpage), page_idx, page_size(kpage), page_to_phys(kpage));
	es_spin_lock(&memblock->esLock);
	buddy_free_pages(zone, page);

	if (order > 0) {
		ClearPageHead(kpage);
	}
	es_spin_unlock(&memblock->esLock);

}
EXPORT_SYMBOL(es_free_pages);

unsigned long es_num_free_pages(struct mem_block *memblock)
{
	struct mem_zone *zone = &memblock->zone;

	return buddy_num_free_page(zone);
}
EXPORT_SYMBOL(es_num_free_pages);

void *es_page_to_virt(struct mem_zone *zone,
                   struct esPage_s *page)
{
    unsigned long page_idx = 0;
    unsigned long address = 0;

    page_idx = page - zone->first_page;
    address = zone->start_addr + page_idx * BUDDY_PAGE_SIZE;

    return (void *)address;
}

struct esPage_s *es_virt_to_page(struct mem_zone *zone, void *ptr)
{
    unsigned long page_idx = 0;
    struct esPage_s *page = NULL;
    unsigned long address = (unsigned long)ptr;

    if((address<zone->start_addr)||(address>zone->end_addr))
    {
        buddy_print("start_addr=0x%lx, end_addr=0x%lx, address=0x%lx\n",
                zone->start_addr, zone->end_addr, address);
        BUDDY_BUG(__FILE__, __LINE__);
        return NULL;
    }
    page_idx = (address - zone->start_addr)>>BUDDY_PAGE_SHIFT;

    page = zone->first_page + page_idx;
    return page;
}

static int do_rsvmem_buddy_init(struct mem_block *memblock, void *data)
{
	int pages_size;

	pr_debug("eswin buddy init for %s\n", memblock->name);
	/* alloc esPage_s for all the pages to manage the pages*/
	pages_size = memblock->page_num * sizeof(struct esPage_s);
	memblock->esPagesStart = (struct esPage_s*)vmalloc(pages_size);
	if (!memblock->esPagesStart) {
		pr_err("%s:%d, failed to buddy init for %s\n",
			__func__, __LINE__, memblock->name);
		return -ENOMEM;
	}
	es_buddy_system_init(memblock, 0, memblock->page_num);

	return 0;
}
static int eswin_rsvmem_buddy_init(void)
{
	int ret = 0;

	ret = eswin_rsvmem_for_each_block(do_rsvmem_buddy_init, NULL);

	return ret;
}

static int do_rsvmem_buddy_uninit(struct mem_block *memblock, void *data)
{
	unsigned long numFreePages = 0;

	if (NULL == memblock->esPagesStart)
		return 0;

	numFreePages = es_num_free_pages(memblock);
	pr_debug("%s: free mem=0x%lx\n",
		memblock->name, numFreePages<<PAGE_SHIFT);
	if (numFreePages == memblock->zone.page_num) {
		vfree((void*)memblock->esPagesStart);
	}
	else {
		pr_err("%s: %ld outof %ld pages still in use, skip destroy memblock!\n",
			memblock->name, numFreePages, memblock->zone.page_num);

		return -EBUSY;
	}

	return 0;
}

static void __exit eswin_rsvmem_buddy_uninit(void)
{
	eswin_rsvmem_for_each_block(do_rsvmem_buddy_uninit, NULL);
}

subsys_initcall(eswin_rsvmem_buddy_init);
module_exit(eswin_rsvmem_buddy_uninit);
MODULE_LICENSE("GPL v2");