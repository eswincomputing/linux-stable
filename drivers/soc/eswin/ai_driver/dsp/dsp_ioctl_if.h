// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#ifndef ESWIN_COMMON_DSP_IOCTL_IF_H_
#define ESWIN_COMMON_DSP_IOCTL_IF_H_

#include "es_type.h"
#include "es_dsp_types.h"

#define ES_DSP_IOCTL_MAGIC 'e'
#define DSP_IOCTL_ALLOC _IO(ES_DSP_IOCTL_MAGIC, 1)
#define DSP_IOCTL_FREE _IO(ES_DSP_IOCTL_MAGIC, 2)
#define DSP_IOCTL_QUEUE _IO(ES_DSP_IOCTL_MAGIC, 3)
#define DSP_IOCTL_QUEUE_NS _IO(ES_DSP_IOCTL_MAGIC, 4)
#define DSP_IOCTL_IMPORT _IO(ES_DSP_IOCTL_MAGIC, 5)
#define DSP_IOCTL_ALLOC_COHERENT _IO(ES_DSP_IOCTL_MAGIC, 6)
#define DSP_IOCTL_FREE_COHERENT _IO(ES_DSP_IOCTL_MAGIC, 7)
#define DSP_IOCTL_REG_TASK _IO(ES_DSP_IOCTL_MAGIC, 8)
#define DSP_IOCTL_TEST_INFO _IO(ES_DSP_IOCTL_MAGIC, 9)
#define DSP_IOCTL_DMA_TEST _IO(ES_DSP_IOCTL_MAGIC, 10)
#define DSP_IOCTL_LOAD_OP _IO(ES_DSP_IOCTL_MAGIC, 11)
#define DSP_IOCTL_UNLOAD_OP _IO(ES_DSP_IOCTL_MAGIC, 12)
#define DSP_IOCTL_SUBMIT_TSK _IO(ES_DSP_IOCTL_MAGIC, 13)
#define DSP_IOCTL_WAIT_IRQ _IO(ES_DSP_IOCTL_MAGIC, 14)
#define DSP_IOCTL_SEND_ACK _IO(ES_DSP_IOCTL_MAGIC, 15)
#define DSP_IOCTL_QUERY_TASK _IO(ES_DSP_IOCTL_MAGIC, 16)
#define DSP_IOCTL_SUBMIT_TSK_ASYNC _IO(ES_DSP_IOCTL_MAGIC, 17)
#define DSP_IOCTL_GET_CMA_INFO _IO(ES_DSP_IOCTL_MAGIC, 18)
#define DSP_IOCTL_PROCESS_REPORT _IO(ES_DSP_IOCTL_MAGIC, 19)
#define DSP_IOCTL_PREPARE_DMA _IO(ES_DSP_IOCTL_MAGIC, 20)
#define DSP_IOCTL_UNPREPARE_DMA _IO(ES_DSP_IOCTL_MAGIC, 21)
#define DSP_IOCTL_ENABLE_PERF _IO(ES_DSP_IOCTL_MAGIC, 22)
#define DSP_IOCTL_GET_PERF_DATA _IO(ES_DSP_IOCTL_MAGIC, 23)
#define DSP_IOCTL_GET_FW_PERF_DATA _IO(ES_DSP_IOCTL_MAGIC, 24)
#define DSP_IOCTL_SUBMIT_TSKS_ASYNC _IO(ES_DSP_IOCTL_MAGIC, 25)

typedef struct dsp_ioctl_pre_dma_t {
    ES_DEV_BUF_S desc;
} __attribute__((packed)) dsp_ioctl_pre_dma_s;

typedef struct dsp_ioctl_load_t {
    ES_S32 dspId;
    ES_U64 op_name;
    ES_U64 op_lib_dir;
    ES_DSP_HANDLE op_handle;
} __attribute__((packed)) dsp_ioctl_load_s;

typedef struct dsp_ioctl_unload_t {
    ES_S32 dspId;
    ES_DSP_HANDLE op_handle;
} __attribute__((packed)) dsp_ioctl_unload_s;

typedef struct dsp_ioctl_task_t {
    ES_U32 task_num;
    ES_DSP_TASK_S task;
    ES_S32 result;
} __attribute__((packed)) dsp_ioctl_task_s;

typedef struct dsp_ioctl_query_t {
    ES_S32 dspId;
    ES_S32 finish;
    ES_BOOL block;
    ES_DSP_HANDLE task_handle;
} __attribute__((packed)) dsp_ioctl_query_s;

typedef struct dsp_task_status {
    ES_U64 callback;
    ES_U64 cbArg;
    ES_S32 finish;
} __attribute__((packed)) dsp_task_status_s;

struct dsp_cma_info {
    ES_U64 base;
    ES_U32 size;
} __attribute__((packed));

typedef struct dsp_ioctl_async_process_t {
    ES_S32 timeout;
    ES_U32 task_num;
    ES_U32 return_num;
    ES_U64 task;
} __attribute__((packed)) dsp_ioctl_async_process_s;
#endif  // ESWIN_COMMON_DSP_IOCTL_IF_H_
