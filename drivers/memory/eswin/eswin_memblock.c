/*
 * eswin_rsvmem initializes the reserved memory in dst that has compatible = "eswin-reserve-memory" and
 * no-map property. Each of these memory region will be treated as one memory block and managed by eswin
 * buddy system. Users can allocate/frree pages from/to these memory blocks via es_alloc_pages/es_free_pages.
 *
 * Copyright 2023, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "eswin_rsvmem: " fmt

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/kmemleak.h>
#include "eswin_memblock.h"


struct mem_block eswin_rsvmem_blocks[MAX_ESWIN_RSVMEM_AREAS] = {0};
unsigned eswin_rsvmem_block_count = 0;

phys_addr_t eswin_rsvmem_get_base(const struct mem_block *memblock)
{
	return page_to_phys(memblock->kPageStart);
}

unsigned long eswin_rsvmem_get_size(const struct mem_block *memblock)
{
	return memblock->page_num << PAGE_SHIFT;
}
EXPORT_SYMBOL(eswin_rsvmem_get_size);

const char *eswin_rsvmem_get_name(const struct mem_block *memblock)
{
	return memblock->name;
}
EXPORT_SYMBOL(eswin_rsvmem_get_name);

struct mem_block *eswin_rsvmem_get_memblock(const char *memBlkName)
{
	struct mem_block *memblock = NULL;
	int i;

	for (i = 0; i < eswin_rsvmem_block_count; i++) {
		if ((!strcmp(memBlkName, eswin_rsvmem_blocks[i].name)) &&
		    (NULL != eswin_rsvmem_blocks[i].esPagesStart)){
			memblock = &eswin_rsvmem_blocks[i];
			break;
		}
	}

	return memblock;
}
EXPORT_SYMBOL(eswin_rsvmem_get_memblock);

/**
 * eswin_rsvmem_init_reserved_mem() - create eswin reserve memory from reserved memory
 * @base: Base address of the reserved area
 * @size: Size of the reserved area (in bytes),
 * @name: The name of the area.
 *
 * This function creates eswin reserve memory block and manage it with eswin buddy allocator.
 */
static int __init eswin_rsvmem_init_reserved_mem(phys_addr_t base, phys_addr_t size, const char *name)
{
	struct mem_block *memblock;
	phys_addr_t alignment;
	char *temp;
	int bname_len;

	/* Sanity checks */
	if (eswin_rsvmem_block_count == ARRAY_SIZE(eswin_rsvmem_blocks)) {
		pr_err("Not enough slots for eswin-reserve-memory!\n");
		return -ENOSPC;
	}

	if (!size) {
		pr_err("Invalid size 0x%llx\n", size);
		return -EINVAL;
	}

	/* ensure minimal alignment */
	alignment = PAGE_SIZE <<
			max_t(unsigned long, MAX_ORDER - 1, pageblock_order);

	if (ALIGN(base, alignment) != base || ALIGN(size, alignment) != size) {
		pr_err("Alignment Err! base:0x%llx, size:0x%llx, alignment:0x%llx\n", base, size, alignment);
		return -EINVAL;
	}

	if (!name) {
		pr_err("rsvmem block name is NULL!\n");
		return -EINVAL;
	}

	memblock = &eswin_rsvmem_blocks[eswin_rsvmem_block_count];
	memblock->page_num = size >> PAGE_SHIFT;

	/* Set esPagesStart = NULL here, it will be allocated later by fs_initcall*/
	memblock->esPagesStart = NULL;
	memblock->kPageStart = phys_to_page(base);

	snprintf(memblock->name, BLOCK_MAX_NAME, name);
	temp = strchr(memblock->name, '@');
	if (temp) {
		bname_len = strnlen(memblock->name, BLOCK_MAX_NAME) - strnlen(temp,
								BLOCK_MAX_NAME);
		*(memblock->name + bname_len) = '\0';
	}

	eswin_rsvmem_block_count++;

	return 0;
}

int eswin_rsvmem_for_each_block(int (*it)(struct mem_block *rsvmem_block, void *data), void *data)
{
	int i;

	for (i = 0; i < eswin_rsvmem_block_count; i++) {
		int ret = it(&eswin_rsvmem_blocks[i], data);

		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(eswin_rsvmem_for_each_block);

#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

#undef pr_fmt
#define pr_fmt(fmt) fmt

static int __init rmem_eswin_setup(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;
	int err;

	if (of_get_flat_dt_prop(node, "reusable", NULL) ||
	    !of_get_flat_dt_prop(node, "no-map", NULL))
		return -EINVAL;


	err = eswin_rsvmem_init_reserved_mem(rmem->base, rmem->size, rmem->name);
	if (err) {
		pr_err("Reserved memory: unable to setup eswin reserve region, err=%d\n", err);
		return err;
	}

	pr_info("Reserved memory: created %s eswin reserve memory at %pa, size %ld MiB\n",
		rmem->name, &rmem->base, (unsigned long)rmem->size / SZ_1M);

	return 0;
}
RESERVEDMEM_OF_DECLARE(eswin_rsvmem, "eswin-reserve-memory", rmem_eswin_setup);
#endif