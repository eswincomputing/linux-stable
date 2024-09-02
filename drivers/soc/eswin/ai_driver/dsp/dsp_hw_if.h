// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef ESWIN_COMMON_DSP_HW_IF_H_
#define ESWIN_COMMON_DSP_HW_IF_H_

#include "es_dsp_types.h"
#include "es_nn_common.h"
#include "es_dsp_internal.h"

#define MAX_DSP_TASKS 4096
#define DSP_2M_SHIFT 20
#define DSP_2M_SIZE 0x200000

#if defined DSP_ENV_SIM && DSP_ENV_SIM
#define DSP_OP_LIB_DIR "./operators"
#else
#define DSP_OP_LIB_DIR "/lib/firmware/dsp_kernels"
#endif

/*
 * bit7-0: dsp fw state.
 * 0: dsp fw not inited.
 * 1: dsp is alive
 * 2: dsp is died.
 * bit15-8: npu task state.
 * 0: no npu task process.
 * 1: npu task is processing.
 * bit23-16: dsp task state.
 * 0: no dsp task process.
 * 1: dsp task is processing.
 * bit31-24: task func state:
 * 0: no func exec
 * 1: prepare func is executing.
 * 2: prepare func is done.
 * 3: eval func is executing.
 * 4: eval func is done
 */
struct dsp_fw_state_t {
    ES_U32 exccause;
    ES_U32 ps;
    ES_U32 pc;
    union {
        struct {
            ES_U32 fw_state : 8;
            ES_U32 npu_task_state : 8;
            ES_U32 dsp_task_state : 8;
            ES_U32 func_state : 8;
        };
        ES_U32 val;
    };
};

typedef struct es_dsp_buffer_group_t {
    es_dsp_buffer* buffers;
    ES_U32 bufferCnt;
} es_dsp_buffer_group;

typedef ES_S32 (*PREPARE_FUNC)(es_dsp_buffer_group* params);
typedef ES_S32 (*EVAL_FUNC)(es_dsp_buffer_group* params, es_dsp_buffer_group* inputs, es_dsp_buffer_group* outputs);

typedef struct operator_func_t {
    PREPARE_FUNC prepare_func;
    EVAL_FUNC eval_func;
} operator_func;

#endif  // ESWIN_COMMON_DSP_HW_IF_H_
