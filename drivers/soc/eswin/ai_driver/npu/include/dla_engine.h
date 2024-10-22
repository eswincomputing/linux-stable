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

#ifndef DLA_ENGINE_H
#define DLA_ENGINE_H

#include <linux/types.h>
#include <linux/time.h>
#include <dla_interface.h>
#include <dla_sched.h>

/**
 * @ingroup Processors
 * @name Number of groups
 * @brief Each processor has 2 groups of registers
 * @{
 */
#define DLA_NUM_GROUPS 2

struct win_executor;
struct dla_processor_group {
	uint8_t id;
	uint8_t rdma_id;
	uint8_t active;
	uint8_t events;
	uint8_t is_rdma_needed;
	uint8_t pending;
	int32_t lut_index;
	uint8_t programming;
	uint64_t start_time;
	struct list_head edma_programed_jobs;
	struct win_executor *executor;

	struct dla_common_op_desc *op_desc;
	struct dla_common_op_desc *consumers[HW_OP_NUM];
	struct dla_common_op_desc *fused_parent;
	union dla_operation_container *operation_desc;
	union dla_surface_container *surface_desc;
};

struct dla_processor {
	const char *name;
	uint8_t op_type;
	uint8_t consumer_ptr;
	uint8_t group_status;
	uint8_t rdma_status;
	uint8_t last_group;
	uint8_t ping_pong;

	struct dla_common_op_desc *tail_op;
	struct dla_processor_group groups[DLA_NUM_GROUPS];

	int32_t (*enable)(struct dla_processor_group *group);
	void (*set_producer)(int32_t group_id, int32_t rdma_id);
	void (*dump_config)(struct dla_processor_group *group);
	int (*rdma_check)(struct dla_processor_group *group,
			  union dla_operation_container *op,
			  union dla_surface_container *surface);
	void (*get_stat_data)(struct dla_processor *processor,
			      struct dla_processor_group *group);
	void (*dump_stat)(struct dla_processor *processor);
};

struct op_stats_t {
	uint32_t op_start;
	uint32_t op_end_comp;
	uint32_t pg_start;
	uint32_t pg_end;
	uint32_t op_end_intr;
	uint8_t op_type;
};

#define OP_TYPE_MAX 8
#define OP_INDEX_MAX 512
struct dla_engine {
	struct dla_task task;
	struct dla_network_desc network;
	struct dla_processor processors[HW_OP_NUM];
	uint16_t num_proc_hwl;
	int32_t status;
	uint32_t stat_enable;
	void *driver_context;
	/*
	 * op_index used to index element.
	 */
	struct op_stats_t op_stats[OP_INDEX_MAX];
	uint32_t npu_saved_time;
	uint32_t dla_saved_time;
};
#endif
