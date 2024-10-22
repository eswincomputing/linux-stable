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

#include <linux/slab.h>
#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "internal_interface.h"

int dla_event_sink_rdma_check(struct dla_processor_group *group,
			 union dla_operation_container *op,
			 union dla_surface_container *surface)
{
	return 0;
}

int event_sink_tensor_unfold(struct win_executor *executor, int op_idx,
			union dla_operation_container *operation_desc,
			union dla_surface_container *surface_desc, void *tensor,
			int idx)
{
	event_sink_dev_t *event_sink = NULL;
	event_sink_dev_t *event_sink_data = NULL;

	event_sink =
		(event_sink_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SINK];
	event_sink_data = (event_sink_dev_t *)&event_sink[idx];
	event_sink_data->npu_info.current_op_idx = op_idx;

	dla_detail("op_idx:%d idx:%d\n", op_idx, idx);

	return 0;
}

int event_sink_prepare_io_tensor(struct win_executor *executor, int seq,
			    struct host_frame_desc *f,
			    union dla_surface_container *surface_desc)
{
	return 0;
}

void dla_event_sink_dump_config(struct dla_processor_group *group)
{
}

int dla_event_sink_prepare_prog_data(struct win_executor *executor, int rdma,
				int tensor_idx, u16 op_idx,
				union dla_operation_container *operation_desc,
				union dla_surface_container *surface_desc)
{
	return 0;
}

int dla_event_source_rdma_check(struct dla_processor_group *group,
			 union dla_operation_container *op,
			 union dla_surface_container *surface)
{
	return 0;
}

int event_source_tensor_unfold(struct win_executor *executor, int op_idx,
			union dla_operation_container *operation_desc,
			union dla_surface_container *surface_desc, void *tensor,
			int idx)
{
	event_sink_dev_t *event_sink = NULL;
	event_sink_dev_t *event_sink_data = NULL;

	event_sink =
		(event_sink_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SOURCE];
	event_sink_data = (event_sink_dev_t *)&event_sink[idx];
	event_sink_data->npu_info.current_op_idx = op_idx;

	dla_detail("op_idx:%d idx:%d\n", op_idx, idx);

	return 0;
}

int event_source_prepare_io_tensor(struct win_executor *executor, int seq,
			    struct host_frame_desc *f,
			    union dla_surface_container *surface_desc)
{
	return 0;
}

void dla_event_source_dump_config(struct dla_processor_group *group)
{
}

int dla_event_source_prepare_prog_data(struct win_executor *executor, int rdma,
				int tensor_idx, u16 op_idx,
				union dla_operation_container *operation_desc,
				union dla_surface_container *surface_desc)
{
	return 0;
}


