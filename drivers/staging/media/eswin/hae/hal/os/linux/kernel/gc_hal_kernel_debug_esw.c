// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN HAE Debug Proc File 
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Zhilin Lei <leizhilin@eswincomputing.com>
 */

#include <linux/es_proc.h>
#include <linux/printk.h>

#include "gc_hal_kernel_linux.h"

#define PROC_ENTRY_HAE_STATE "stat"

static struct es_proc_dir_entry *es_proc_entry_hae = NULL;
static struct es_proc_dir_entry *es_proc_entry_stat = NULL;
static gckGALDEVICE gal_device = NULL;

static void gc_hal_kernel_esw_print_usage(es_proc_entry_t *s, gckDEVICE device)
{
    gctUINT32 i;

    for (i = gcvCORE_2D; i <= gcvCORE_2D1; i++) {
        if (!device->kernels[i]) {
            continue;
        }

        if (!device->kernels[i]->hardware) {
            continue;
        }

        gckHARDWARE Hardware = device->kernels[i]->hardware;
        es_seq_printf(s, "dev_id      : %d\n", device->id);
        es_seq_printf(s, "dev_core    : %d\n", i);
        es_seq_printf(s, "pooling_ms  : %d\n", HARDWARE_USAGE_MEASURE_TIME_MS);
        es_seq_printf(s, "curr_load   : %u%%\n", Hardware->load);
        es_seq_printf(s, "\n");
    }

    return;
}

static int gc_hal_kernel_dbg_esw_proc(es_proc_entry_t *s)
{
    gctUINT32 i;
    gckDEVICE device = gcvNULL;

    if (!gal_device) {
        return -1;
    }

    for (i = 0; i < gcdDEVICE_COUNT; i++) {
        device = gal_device->devices[i];
        if (!device) {
            continue;
        }

        gc_hal_kernel_esw_print_usage(s, device);
    }

    return 0;
}

int gc_hal_kernel_dbg_esw_create_procfs(gckGALDEVICE g_dev)
{
    if (!g_dev) {
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    gal_device = g_dev;

    es_proc_entry_hae = es_proc_mkdir(PROC_ENTRY_HAE, 0555, NULL);
    if (NULL == es_proc_entry_hae) {
        pr_err("[%s, %d] create proc enty: %s failed!\n", __func__, __LINE__, PROC_ENTRY_HAE);
        return gcvSTATUS_INVALID_ADDRESS;
    }

    es_proc_entry_t *es_proc_entry_stat = es_create_proc_entry(PROC_ENTRY_HAE_STATE, 0444, es_proc_entry_hae);
    if (NULL == es_proc_entry_stat) {
        pr_err("[%s, %d] create proc enty: %s failed!\n", __func__, __LINE__, PROC_ENTRY_HAE_STATE);
        goto err_stat;
    }

    es_proc_entry_stat->read = gc_hal_kernel_dbg_esw_proc;
    es_proc_entry_stat->write = NULL;
    es_proc_entry_stat->open = NULL;

    return gcvSTATUS_OK;

err_stat:
    es_remove_proc_entry(PROC_ENTRY_HAE, NULL);
    return gcvSTATUS_INVALID_ADDRESS;
}

void gc_hal_kernel_dbg_esw_remove_procfs(void)
{
    if (es_proc_entry_hae) {
        es_remove_proc_entry(PROC_ENTRY_HAE_STATE, es_proc_entry_hae);
        es_proc_entry_stat = NULL;
    }

    if (es_proc_entry_hae) {
        es_remove_proc_entry(PROC_ENTRY_HAE, NULL);
        es_proc_entry_hae = NULL;
    }

    gal_device = NULL;

    return;
}