// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __NVDLA_PROC_H_
#define __NVDLA_PROC_H_
struct win_executor;
struct win_engine;
int npu_create_procfs(void);
void npu_remove_procfs(void);
void refresh_op_statistic(struct win_executor *executor,
			  struct win_engine *engine, u32 tiktok);
#endif
