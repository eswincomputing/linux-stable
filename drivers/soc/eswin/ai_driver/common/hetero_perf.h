// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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

#ifndef __HETERO_PERF_H__
#define __HETERO_PERF_H__

#include "hetero_env.h"
#include "hetero_types.h"

#define OPERATOR_NAME_MAXLEN 128

extern u32 get_perf_timer_cnt(u32 numa_id);
typedef struct _npu_model_perf {
    //APIStartCycle -> TaskSubmitCycle: User-mode task scheduling time consumption
    u32 APIStartCycle;
    //TaskSubmitCycle -> TaskDoneCycle: Time consumption for task running
    u32 TaskSubmitCycle;
    //TaskDoneCycle -> APIEndCycle: Time consumption for completing scheduling
    u32 TaskDoneCycle;
    u32 APIEndCycle;
} npu_model_perf_t;

typedef struct _npu_drv_perf {
    //FrameCreateCycle -> FrameSendCycle: Frame scheduling time consumption
	u32 FrameCreateCycle;
    //FrameSendCycle -> FrameDoneCycle: Time consumption for frame inference
    u32 FrameSendCycle;
    //FrameDoneCycle -> FrameReleaseCycle: Time consumption for completing queue scheduling
    u32 FrameDoneCycle;
	u32 FrameReleaseCycle;
    //FrameSinkCycle -> FrameEventCycle: Time consumption between user mode and kernel mode
    u32 FrameSinkCycle;
    u32 FrameEventCycle;
}npu_drv_perf_t;

typedef struct _npu_umd_perf {
    u32 Die;
    u32 OpIndex;
    u32 OpType;
    u32 OpStartCycle;
    u32 OpEndCycle;
} npu_umd_perf_t;

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
