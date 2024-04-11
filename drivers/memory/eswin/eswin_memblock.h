/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ESWIN_RSVMEM_H__
#define __ESWIN_RSVMEM_H__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/numa.h>
#include "buddy.h"

// #define QEMU_DEBUG 1

#define MAX_ESWIN_RSVMEM_AREAS	(64)

struct mem_block *eswin_rsvmem_get_memblock(const char *memBlkName);
int eswin_rsvmem_for_each_block(int (*it)(struct mem_block *rsvmem_block, void *data), void *data);
const char *eswin_rsvmem_get_name(const struct mem_block *memblock);

#endif
