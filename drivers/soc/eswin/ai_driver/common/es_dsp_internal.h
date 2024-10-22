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

#ifndef __ESWIN_DSP_INTERNAL_H__
#define __ESWIN_DSP_INTERNAL_H__
#include "es_type.h"
#include "es_dsp_types.h"
#include "es_dsp_op_types.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#ifdef __KERNEL__
#include "eswin-khandle.h"
#include <linux/dma-buf.h>
struct dsp_file;

struct dsp_dma_buf {
    int fd;
    u32 dma_addr;
    struct dma_buf_attachment *attach;
    struct dma_buf *dmabuf;
    struct dsp_file *dsp_file;
};

struct dsp_dma_buf_ex {
    struct khandle handle;
    u32 count;
    u32 offset;
    struct dsp_dma_buf buf;
};
#endif

typedef enum {
    DSP_CMD_LEGACY,
    DSP_CMD_FLAT1,
    DSP_CMD_READY,
    DSP_CMD_INVALID_ICACHE,
} es_dsp_cmd;

#if (defined(DSP_ENV_SIM) && DSP_ENV_SIM) || (defined(NPU_DEV_SIM) && NPU_DEV_SIM)
#define KERNAL_NAME_MAXLEN 128
struct operator_funcs {
    /**
     *  In simulation environment, DSP simulator accepts DSP operator name other
     *  than the real DSP function pointers.
     */
    char op_name[KERNAL_NAME_MAXLEN];
};
#else
struct operator_funcs {
    ES_U32 dsp_prepare_fn;
    ES_U32 dsp_eval_fn;
};
#endif


typedef struct es_dsp_buffer_t {
    ES_U32 addr;
    ES_U32 size;
} __attribute__((packed)) es_dsp_buffer;

struct es_dsp_flat1_desc {
    struct operator_funcs funcs;

    /**
     * Specifies total number of buffers attached to this descriptor. Note the
     *  buffer space holds parameter buffers, input buffers and output buffers
     *  in sequence.
     */
    ES_U32 num_buffer;
    /**
     * Specifies the offset of input (In number of elements) buffer to the start
     * of buffer variable.
     */
    ES_U32 input_index;
    /**
     * Specifies the offset of output (In number of elements) buffer to the start
     * of buffer variable.
     */
    ES_U32 output_index;
    /**
     * Specifies the actual buffer storage.
     *
     */
    es_dsp_buffer buffers[0];
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct {
    /**
     * Accessing the IOVA corresponding to external storage
     * space in DSP.
     */
    ES_U32 iova_ptr : 32;
    /**
     * Command for U84 to access DSP. The meanings of different values are as follows:
     * 0: Legacy Mode, using XRP data structure.
     * 1: Use FLAT1 data structure to pass operator execution commands.
     * 2: Update the IOVA address of Scratch Pad.
     */
    ES_U32 command : 3;
    /**
     * 1: indicates that the FLAT1 data structure has been updated,
     * and the DSP needs to synchronize the cache. 0 indicates
     * that it is not necessary to synchronize the cache.
     */
    ES_U32 sync_cache : 1;
    /**
     * 0: means that prepare needs to be executed first and wait here;
     * 1: means that eval is allowed to be executed. If prepare has not been
     * executed at this point, both prepare and eval should be executed in sequence.
     */
    ES_U32 allow_eval : 1;
    /**
     * 0: means that the DSP driver is responsible for notifying the DSP
     * when to perform the eval action;
     * 1: means that the auxiliary CPU (E31) is responsible for notifying
     * the DSP when to perform the eval action. The DSP needs to determine
     * whether to interact with the DSP driver or the auxiliary CPU based on this flag
     */
    ES_U32 poll_mode : 1;
    /**
     * invalid icache size, unit is 4Byte, and max size is 128KB
     */
    ES_U32 size : 15;
    /**
     * reserved bit
     */
    ES_U32 reserved : 11;
} es_dsp_h2d_msg;
struct dsp_hw_flat_test {
    struct operator_funcs funcs;
    ES_U32 num_buffer;
    ES_U32 input_index;
    ES_U32 output_index;
    ES_U32 flat_iova;
    es_dsp_buffer buffers[0];
};

typedef struct {
    /**
     * DSP running result return value
     */
    ES_U32 return_value : 32;
    /**
     * DSP running result return status. The meanings of different values are:
     * 0: Service completed normally 1: Service generated an exception,
     * the specific reason can be seen in the return_value field
     */
    ES_U32 status : 15;
    /**
     * DSP core index
     */
    ES_U32 core_id : 4;
    /**
     * Reserved bits
     */
    ES_U32 reserved : 13;
} es_dsp_d2h_msg;

typedef struct {
    /**
     *  @brief Specifies the transaction layer protocol.
     */
    ES_U8 type;
    /**
     *  @brief Specifies the parameter passed to transaction layer handler.
     */
    ES_U8 param;
    /**
     *  @brief Specifies the long parameter passed to transaction layer handler.
     */
    ES_U16 lparam;
    /**
     * Reserved
     */
    ES_U32 reserved;
} e31_msg_payload_t;

typedef struct DSP_TASK_DESC_S {
    ES_S32 dspFd;
    ES_DSP_TASK_S opTask;
} __attribute__((packed)) ES_DSP_TASK_DESC_S;

/**
 * @brief Synchronously submit operator tasks to DSP devices, and only return after all tasks are completed.
 *
 * @param[in] tasks: pointer to tasks.
 * @param[in] numTasks: number of tasks.
 * @return Returns the execution status code: ES_DSP_SUCCESS for success, others for failure.
 */
ES_S32 ES_DSP_LL_SubmitTasks(ES_DSP_TASK_DESC_S *tasks, ES_U32 numTasks);

/**
 * @brief Asynchronously submit operator tasks to DSP devices, and only return after all tasks are completed.
 *
 * @param[in] tasks: pointer to tasks.
 * @param[in] numTasks: number of tasks.
 * @return Returns the execution status code: ES_DSP_SUCCESS for success, others for failure.
 */
ES_S32 ES_DSP_LL_SubmitAsyncTasks(ES_DSP_TASK_DESC_S *tasks, ES_U32 numTasks);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
