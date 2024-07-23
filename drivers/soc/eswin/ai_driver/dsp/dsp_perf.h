// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __ESWIN_DSP_PERF_H__
#define __ESWIN_DSP_PERF_H__

#include "es_dsp_types.h"
#include "es_dsp_op_types.h"

#define OP_NAME_MAXLEN 128

// dsp driver perf info
typedef struct _dsp_kmd_perf {
    ES_U32 Die;
    ES_U32 CoreId;
    ES_U32 OpIndex;
    ES_U32 OpType;
    ES_CHAR OpName[OP_NAME_MAXLEN];
    ES_U32 OpStartCycle;
    ES_U32 OpSendTaskCycle;
    ES_U32 OpEndCycle;
} dsp_kmd_perf_t;

typedef struct _dsp_fw_perf {
    ES_U32 Die;
    ES_U32 CoreId;
    ES_U32 OpIndex;
    ES_U32 OpType;
    char OpName[OPERATOR_NAME_MAXLEN];
    ES_U32 OpStartCycle;
    ES_U32 OpPrepareStartCycle;
    ES_U32 OpPrepareEndCycle;
    ES_U32 OpEvalStartCycle;
    ES_U32 OpEvalEndCycle;
    ES_U32 OpNotifyStartCycle;
    ES_U32 OpEndCycle;
} dsp_fw_perf_t;

// dsp hardware perf info
typedef struct {
    volatile ES_U32 core_id;
    volatile ES_U32 op_index;
    volatile ES_U32 op_type;
    volatile ES_U32 flat1_start_time;
    volatile ES_U32 prepare_start_time;
    volatile ES_U32 prepare_end_time;
    volatile ES_U32 eval_start_time;
    volatile ES_U32 eval_end_time;
    volatile ES_U32 notify_start_time;
    volatile ES_U32 flat1_end_time;
    volatile ES_U32 task_cnt;
    volatile ES_U32 send_prepare_to_npu;
    volatile ES_U32 send_eval_to_npu;
    volatile ES_U32 invalid_cmd_cnt;
} es_dsp_perf_info;

#endif
