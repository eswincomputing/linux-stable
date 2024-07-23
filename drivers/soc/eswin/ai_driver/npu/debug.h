// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __DEBUG_H__
#define __DEBUG_H__
#include <dla_engine.h>
#include "internal_interface.h"

#define OP_STAT_START 0x1
#define OP_STAT_END 0x2

#define PG_STAT_START 0x4
#define PG_STAT_END 0x8

#define STAT_IN_INTR 0x10
#define OP_STAT_NPU 0x20
#define OP_STAT_DLA 0x40

void dump_dtim_to_file(struct win_engine *engine, u32 tiktok);

void dump_data(const void *buf, const u32 len);
int dump2file(const char *fname, const void *data, size_t len);
void dla_dump_src_data(struct win_executor *executor, struct host_frame_desc *f,
		       int op_index);
void dla_dump_dst_data(struct win_executor *executor, struct host_frame_desc *f,
		       int op_index);

void dla_op_stats(struct dla_engine *engine, struct dla_common_op_desc *op,
		  uint8_t flag);
void dla_dump_op_stats(struct dla_engine *engine);
void dump_data_to_file(struct work_struct *work);

#endif
