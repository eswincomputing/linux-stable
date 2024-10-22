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
