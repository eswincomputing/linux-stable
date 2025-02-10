// SPDX-License-Identifier: GPL-2.0
/*
 * Performance test APIs for ESWIN memory
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 *
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#ifndef __MEM_PERF_API_H__
#define __MEM_PERF_API_H__

#define IN_KERNEL   1

#if IN_KERNEL
#include <linux/list.h>
#define alloc_mem_perf_info(size)   kmalloc(size, GFP_KERNEL)
#define free_mem_perf_info(info)    kfree(info)
#define PRINT_INFO(fmt, ...)     pr_info(fmt, ##__VA_ARGS__)
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "list.h"
#define alloc_mem_perf_info(size)   malloc(size)
#define free_mem_perf_info(info)    free(info)
#define PRINT_INFO(fmt, ...)     printf("%s" fmt, "[ES_DMA_INF]", ##__VA_ARGS__)
#endif

typedef unsigned long long uint64; 

struct mem_perf_info {
    struct list_head node;
    char    func_name[64];
    uint64     cycles_start;
    uint64     cycles_end;
    uint64     cycles_elapased;
};

#if defined(CONFIG_RISCV)
static inline int metal_timer_get_cyclecount(unsigned long long *mcc)
{
    unsigned long cycles;
    asm volatile ("rdtime %0" : "=r" (cycles));
    *mcc = cycles;

    return 0;
}
#else
static inline int metal_timer_get_cyclecount(unsigned long long *mcc)
{
	return 0;
}
#endif

//struct mem_perf_info *lookup_mem_api_from_list()
static inline struct mem_perf_info *memperf_record_cycle_start(const char *func_name, struct list_head *mem_perf_list_head)
{
    struct mem_perf_info *m_perf_i;

    m_perf_i = alloc_mem_perf_info(sizeof(*m_perf_i));
    if (!m_perf_i) {
        PRINT_INFO("mem perf info alloc failed!\n");
        return NULL;
    }

    sprintf(m_perf_i->func_name, "%s", func_name);
    list_add_tail(&m_perf_i->node, mem_perf_list_head);
    metal_timer_get_cyclecount(&m_perf_i->cycles_start);
    m_perf_i->cycles_end = m_perf_i->cycles_start;

    return m_perf_i;
}

static inline int memperf_record_cycle_end(struct mem_perf_info *m_perf_i)
{
    if (NULL == m_perf_i)
        return -1;

    metal_timer_get_cyclecount(&m_perf_i->cycles_end);

    return 0;
}

#if defined(CONFIG_RISCV)
// #define RTCCLK 32768
#define RTCCLK 1000000
static inline int memperf_print_records(struct list_head *mem_perf_list_head)
{
    struct mem_perf_info *m_perf_i;
    uint64 total_cycles = 0;

    list_for_each_entry(m_perf_i, mem_perf_list_head, node) {
        m_perf_i->cycles_elapased = m_perf_i->cycles_end - m_perf_i->cycles_start;
        total_cycles += m_perf_i->cycles_elapased;
    }
    PRINT_INFO("Total cycles:%lld, %lld us\n", total_cycles, total_cycles*1000*1000/RTCCLK);
    list_for_each_entry(m_perf_i, mem_perf_list_head, node) {
        PRINT_INFO("cycle_elapsed:%lld---%%\%lld.%2lld, %s\n",
            m_perf_i->cycles_elapased, (100*m_perf_i->cycles_elapased)/total_cycles,
            (10000*m_perf_i->cycles_elapased)%total_cycles, m_perf_i->func_name);
    }

    return 0;
}
#else
static inline int memperf_print_records(struct list_head *mem_perf_list_head)
{
	return 0;
}
#endif

static inline int memperf_free_records_list(struct list_head *mem_perf_list_head)
{
    struct mem_perf_info *m_perf_i, *tmp;

	list_for_each_entry_safe(m_perf_i, tmp, mem_perf_list_head, node) {
		list_del(&m_perf_i->node);
		free_mem_perf_info(m_perf_i);
	}

    return 0;
}

#endif
