// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __HETERO_PERF_H__
#define __HETERO_PERF_H__

#include "hetero_env.h"
#include "hetero_types.h"

#define NPU_PERF_STATS_DEBUG 0
#define NPU_PERF_STATS_RELEASE 1
#define NPU_PERF_STATS_ALL 2
#define NPU_PERF_STATS_HW 3

#define OPERATOR_NAME_MAXLEN 128

typedef struct _npu_model_perf {
    u32 APIStartCycle;
    u32 APIEndCycle;
} npu_model_perf_t;

typedef struct _npu_umd_perf {
    u32 Die;
    u32 OpIndex;
    u32 OpType;
    u32 OpStartCycle;
    u32 OpEndCycle;
} npu_umd_perf_t;

typedef struct _npu_kmd_perf {
    u32 Die;
    u32 OpIndex;
    u32 OpType;
    u32 OpStartCycle;
    u32 OpEndCycle;
} npu_kmd_perf_t;

typedef struct _npu_e31_perf {
    u32 Die;
    u32 OpIndex;
    u32 OpType;
    u32 OpStartCycle;
    u32 OpCdmaStartCycle;
    u32 OpCdmaEndCycle;
    u32 OpTransferStartCycle;
    u32 OpTransferEndCycle;
    u32 OpProgramStartCycle;
    u32 OpProgramEndCycle;
    u32 OpEvalStartCycle;
    u32 OpEvalEndCycle;
    u32 OpEndCycle;
    u32 ConvPecStartCycle;
    u64 ConvMacPerfCnt;

    // event task timestamp
    u32 EvtRefStartCycle;
    u32 EvtRefEndCycle;
    // u32 EvtTransDoneStartCycle; // OpTransferEndCycle
    u32 EvtTransDoneEndCycle;
    // u32 EvtProgDonStartCycle; // OpProgramEndCycle
    u32 EvtProgDoneEndCycle;
    // u32 EvtCdmaDoneStartCycle; // OpCdmaEndCycle
    u32 EvtCdmaDoneEndCycle;
    u32 EvtEvalDoneStartCycle;
    u32 EvtEvalDoneEndCycle;
} npu_e31_perf_t;

typedef struct _dsp_kmd_perf {
    u32 Die;
    u32 CoreId;
    u32 OpIndex;
    u32 OpType;
    char OpName[OPERATOR_NAME_MAXLEN];
    u32 OpStartCycle;
    u32 OpSendTaskCycle;
    u32 OpEndCycle;
} dsp_kmd_perf_t;

typedef struct _dsp_fw_perf {
    u32 Die;
    u32 CoreId;
    u32 OpIndex;
    u32 OpType;
    char OpName[OPERATOR_NAME_MAXLEN];
    u32 OpStartCycle;
    u32 OpPrepareStartCycle;
    u32 OpPrepareEndCycle;
    u32 OpEvalStartCycle;
    u32 OpEvalEndCycle;
    u32 OpNotifyStartCycle;
    u32 OpEndCycle;
} dsp_fw_perf_t;

#endif
