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

#include <linux/slab.h>
#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "internal_interface.h"
#include "dla_buffer.h"
#include "dla_driver.h"

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
	struct event_surface_desc *surface = &surface_desc->event_surface;
	event_sink_dev_t *event_sink = NULL;
	event_sink_dev_t *event_sink_data = NULL;
	struct nvdla_task *task = executor->mem_handles;
	struct nvdla_device *nvdla_dev = executor->driver_context;
	int index = surface->data[0].address;
	int ret = 0;
	u64 src_base_addr;
	event_sink_tensor_t *sink_tensor = tensor;

	event_sink =
		(event_sink_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SINK];
	event_sink_data = (event_sink_dev_t *)&event_sink[idx];
	event_sink_data->npu_info.current_op_idx = op_idx;

	dla_detail("op_idx:%d idx:%d\n", op_idx, idx);

	if (index == -1 || operation_desc->event_op.submodel_type != P2P) {
		return 0;
	}

	dla_debug("sink event addr index:%d\n", index);

	ret = read_input_address(executor, &surface->data[0],
							(void *)&src_base_addr,
							&sink_tensor[idx].input_is_io_tensor);
	if (ret != 0) {
		dla_error("%s %d bad memory type %d\n", __func__, __LINE__,
			surface->data[0].type);
		return -1;
	}

	if (sink_tensor[idx].input_is_io_tensor != invalid_tensor_idx) {
		dla_debug("sink event tensor is io tensor\n");
		return 0;
	}

	ret = dla_detach_dmabuf(&task->bobjs[index]);
	if (ret < 0) {
		dla_error("err:dla_detach_dmabuf failed!\n");
		return ret;
	}

	mutex_lock(&nvdla_dev->mapping_mutex);
	ret = dma_set_mask_and_coherent(&nvdla_dev->pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		mutex_unlock(&nvdla_dev->mapping_mutex);
		dev_warn(&nvdla_dev->pdev->dev, "Unable to set coherent mask 32bit\n");
		return ret;
	}

	ret = dla_attach_dmabuf(&task->bobjs[index], &nvdla_dev->pdev->dev);
	if (ret < 0) {
		dla_error("err:dla_detach_dmabuf failed!\n");
		mutex_unlock(&nvdla_dev->mapping_mutex);
		return ret;
	}

	dla_debug("sink event iova addr:0x%llx\n", task->bobjs[index].dma_addr);

	event_sink_data->npu_info.peer_address = task->bobjs[index].dma_addr;
	event_sink_data->npu_info.peer_link = operation_desc->event_op.p2p_src << 8 | operation_desc->event_op.p2p_dst;

	dla_debug("sink event peer addr:0x%x, link:0x%x\n", event_sink_data->npu_info.peer_address,
			  event_sink_data->npu_info.peer_link);

	ret = dma_set_mask_and_coherent(&nvdla_dev->pdev->dev, DMA_BIT_MASK(41));
	mutex_unlock(&nvdla_dev->mapping_mutex);
	if (ret) {
		dev_warn(&nvdla_dev->pdev->dev, "Unable to set coherent mask 41bit\n");
		return ret;
	}

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
	event_source_dev_t *event_source = NULL;
	event_source_dev_t *event_source_data = NULL;

	event_source =
		(event_source_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SOURCE];
	event_source_data = (event_source_dev_t *)&event_source[idx];
	event_source_data->npu_info.current_op_idx = op_idx;

	event_source_data->npu_info.peer_link = operation_desc->event_op.p2p_src << 8 | operation_desc->event_op.p2p_dst;

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


