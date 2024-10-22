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
#include "dla_log.h"
#include "internal_interface.h"
#include "dla_buffer.h"

static uint8_t map_rubik_mode[] = {
	FIELD_ENUM(RBK_D_MISC_CFG_0, RUBIK_MODE, CONTRACT),
	FIELD_ENUM(RBK_D_MISC_CFG_0, RUBIK_MODE, SPLIT),
	FIELD_ENUM(RBK_D_MISC_CFG_0, RUBIK_MODE, MERGE),
};

static uint8_t map_ram_type[] = {
	FIELD_ENUM(RBK_D_DAIN_RAM_TYPE_0, DATAIN_RAM_TYPE, MCIF),
	FIELD_ENUM(RBK_D_DAIN_RAM_TYPE_0, DATAIN_RAM_TYPE, CVIF),
};

static uint8_t map_precision[] = {
	FIELD_ENUM(RBK_D_MISC_CFG_0, IN_PRECISION, INT8),
	FIELD_ENUM(RBK_D_MISC_CFG_0, IN_PRECISION, INT16),
	FIELD_ENUM(RBK_D_MISC_CFG_0, IN_PRECISION, FP16),
};

static uint8_t map_bpe[] = {
	BPE_PRECISION_INT8,
	BPE_PRECISION_INT16,
	BPE_PRECISION_FP16,
};

int dla_rubik_rdma_check(struct dla_processor_group *group,
			 union dla_operation_container *op,
			 union dla_surface_container *surface)
{
	if (group) {
		group->is_rdma_needed = 0;
	}
	return 0;
}

int rubik_tensor_unfold(struct win_executor *executor, int op_idx,
			union dla_operation_container *operation_desc,
			union dla_surface_container *surface_desc, void *tensor,
			int idx)
{
	struct dla_rubik_surface_desc *rubik_surface =
		&surface_desc->rubik_surface;
	rubik_tensor_t *rubik_tensor = (rubik_tensor_t *)tensor;
	int ret;
	rubik_dev_t *rubik_data = NULL;
	rubik_dev_t *rubik =
		(rubik_dev_t *)executor->prog_data_buf_bobj[IDX_RUBIK];

	rubik_data = (rubik_dev_t *)&rubik[idx];

	dla_debug("%s, %d, idx=%d, op_idx=%d.\n", __func__, __LINE__, idx,
		  op_idx);

	/* get the addresses from task descriptor */
	ret = read_input_address(executor, &rubik_surface->src_data,
				 &rubik_tensor[idx].input_address,
				 &rubik_tensor[idx].input_is_io_tensor);
	if (ret)
		return -1;

	rubik_data->npu_info.current_op_idx = op_idx;
	dla_get_dma_cube_address(executor->driver_context,
				 executor->mem_handles,
				 rubik_surface->dst_data.address,
				 rubik_surface->dst_data.offset,
				 (void *)&rubik_tensor[idx].output_address,
				 &rubik_tensor[idx].output_is_io_tensor);
	return 0;
}

static int32_t
processor_rubik_program(struct win_executor *executor, int rdma, int tensor_idx,
			union dla_operation_container *operation_desc,
			union dla_surface_container *surface_desc)
{
	int32_t ret = 0;
	uint32_t reg, high, low;
	uint64_t input_address = 0;
	uint64_t output_address = 0;
	struct dla_rubik_op_desc *rubik_op;
	struct dla_rubik_surface_desc *rubik_surface;
	rubik_tensor_t *tensor = executor->tensor_set[IDX_RUBIK];
	rubik_tensor_t *rubik_tensor = &tensor[tensor_idx];
	rubik_dev_t *rubik =
		(rubik_dev_t *)executor->prog_data_buf_bobj[IDX_RUBIK];
	rubik_dev_t *rubik_data = (rubik_dev_t *)&rubik[tensor_idx];
	rubik_program_t *data = &rubik_data->prog_data;

	rubik_op = &operation_desc->rubik_op;
	rubik_surface = &surface_desc->rubik_surface;
	data->input_tensor_idx = invalid_tensor_idx;
	data->output_tensor_idx = invalid_tensor_idx;
	/* Argument check */
	if (rubik_surface->src_data.type == DLA_MEM_HW ||
	    rubik_surface->dst_data.type == DLA_MEM_HW) {
		ret = ERR(INVALID_INPUT);
		goto exit;
	}
	if (rubik_tensor->input_is_io_tensor == invalid_tensor_idx) {
		if (unlikely(rubik_tensor->input_address == -1ull)) {
			dla_error("rubik input_address fail\n");
			goto exit;
		} else {
			input_address = rubik_tensor->input_address;
		}
	} else {
		input_address = rubik_surface->src_data.offset;
		data->input_tensor_idx = rubik_tensor->input_is_io_tensor;
	}

	if (rubik_tensor->output_is_io_tensor == invalid_tensor_idx) {
		if (unlikely(rubik_tensor->output_address == -1ull)) {
			dla_error("rubik ouput_address fail\n");
			goto exit;
		} else {
			output_address = rubik_tensor->output_address;
		}
	} else {
		output_address = rubik_surface->dst_data.offset;
		data->output_tensor_idx = rubik_tensor->output_is_io_tensor;
	}
	/* config rubik */
	reg = (((uint32_t)map_rubik_mode[rubik_op->mode])
	       << SHIFT(RBK_D_MISC_CFG_0, RUBIK_MODE)) |
	      (((uint32_t)map_precision[rubik_op->precision])
	       << SHIFT(RBK_D_MISC_CFG_0, IN_PRECISION));
	data->reg[D_MISC_CFG] = reg;
	npu_set_u64_bit(D_MISC_CFG, &data->u84_bitmap);

	reg = (((uint32_t)map_ram_type[rubik_surface->src_data.type])
	       << SHIFT(RBK_D_DAIN_RAM_TYPE_0, DATAIN_RAM_TYPE));
	data->reg[D_DAIN_RAM_TYPE] = reg;
	npu_set_u64_bit(D_DAIN_RAM_TYPE, &data->u84_bitmap);

	reg = ((rubik_surface->src_data.width - 1)
	       << SHIFT(RBK_D_DATAIN_SIZE_0_0, DATAIN_WIDTH)) |
	      ((rubik_surface->src_data.height - 1)
	       << SHIFT(RBK_D_DATAIN_SIZE_0_0, DATAIN_HEIGHT));
	data->reg[D_DATAIN_SIZE_0] = reg;
	npu_set_u64_bit(D_DATAIN_SIZE_0, &data->u84_bitmap);

	reg = ((rubik_surface->src_data.channel - 1)
	       << SHIFT(RBK_D_DATAIN_SIZE_1_0, DATAIN_CHANNEL));
	data->reg[D_DATAIN_SIZE_1] = reg;
	npu_set_u64_bit(D_DATAIN_SIZE_1, &data->u84_bitmap);

	high = HIGH32BITS(input_address);
	low = LOW32BITS(input_address);
	data->reg[D_DAIN_ADDR_LOW] = low;
	data->reg[D_DAIN_ADDR_HIGH] = high;
	npu_set_u64_bit(D_DAIN_ADDR_LOW, &data->u84_bitmap);
	npu_set_u64_bit(D_DAIN_ADDR_HIGH, &data->u84_bitmap);

	if (rubik_op->mode == RUBIK_MODE_MERGE) {
		if (rubik_surface->src_data.plane_stride == 0 ||
		    (rubik_surface->src_data.plane_stride & 0x1f) != 0) {
			ret = ERR(INVALID_INPUT);
			goto exit;
		}
		data->reg[D_DAIN_PLANAR_STRIDE] =
			rubik_surface->src_data.plane_stride;
		npu_set_u64_bit(D_DAIN_PLANAR_STRIDE, &data->u84_bitmap);
	} else {
		data->reg[D_DAIN_SURF_STRIDE] =
			rubik_surface->src_data.surf_stride,
		npu_set_u64_bit(D_DAIN_SURF_STRIDE, &data->u84_bitmap);
	}
	data->reg[D_DAIN_LINE_STRIDE] = rubik_surface->src_data.line_stride;
	npu_set_u64_bit(D_DAIN_LINE_STRIDE, &data->u84_bitmap);

	reg = (((uint32_t)map_ram_type[rubik_surface->dst_data.type])
	       << SHIFT(RBK_D_DAOUT_RAM_TYPE_0, DATAOUT_RAM_TYPE));
	data->reg[D_DAOUT_RAM_TYPE] = reg;
	npu_set_u64_bit(D_DAOUT_RAM_TYPE, &data->u84_bitmap);

	reg = ((rubik_surface->dst_data.channel - 1)
	       << SHIFT(RBK_D_DATAOUT_SIZE_1_0, DATAOUT_CHANNEL));
	data->reg[D_DATAOUT_SIZE_1] = reg;
	npu_set_u64_bit(D_DATAOUT_SIZE_1, &data->u84_bitmap);

	high = HIGH32BITS(output_address);
	low = LOW32BITS(output_address);

	data->reg[D_DAOUT_ADDR_LOW] = low;
	data->reg[D_DAOUT_ADDR_HIGH] = high;
	npu_set_u64_bit(D_DAOUT_ADDR_LOW, &data->u84_bitmap);
	npu_set_u64_bit(D_DAOUT_ADDR_HIGH, &data->u84_bitmap);

	data->reg[D_DAOUT_LINE_STRIDE] = rubik_surface->dst_data.line_stride;
	npu_set_u64_bit(D_DAOUT_LINE_STRIDE, &data->u84_bitmap);

	if (rubik_op->mode != RUBIK_MODE_SPLIT) {
		data->reg[D_DAOUT_SURF_STRIDE] =
			rubik_surface->dst_data.surf_stride;
		npu_set_u64_bit(D_DAOUT_SURF_STRIDE, &data->u84_bitmap);

		if (rubik_op->mode == RUBIK_MODE_CONTRACT) {
			reg = ((rubik_surface->dst_data.channel *
					map_bpe[rubik_op->precision] +
				31) >>
			       5) *
			      rubik_surface->src_data.surf_stride;
			data->reg[D_CONTRACT_STRIDE_0] = reg;
			npu_set_u64_bit(D_CONTRACT_STRIDE_0, &data->u84_bitmap);

			reg = rubik_op->stride_y *
			      rubik_surface->dst_data.line_stride;
			data->reg[D_CONTRACT_STRIDE_1] = reg;
			npu_set_u64_bit(D_CONTRACT_STRIDE_1, &data->u84_bitmap);

			reg = (((uint32_t)(rubik_op->stride_x - 1))
			       << SHIFT(RBK_D_DECONV_STRIDE_0,
					DECONV_X_STRIDE)) |
			      (((uint32_t)(rubik_op->stride_y - 1)) << SHIFT(
				       RBK_D_DECONV_STRIDE_0, DECONV_Y_STRIDE));
			data->reg[D_DECONV_STRIDE] = reg;
			npu_set_u64_bit(D_DECONV_STRIDE, &data->u84_bitmap);
		}
	} else {
		data->reg[D_DAOUT_PLANAR_STRIDE] =
			rubik_surface->dst_data.plane_stride;
		npu_set_u64_bit(D_DAOUT_PLANAR_STRIDE, &data->u84_bitmap);
	}

exit:
	return ret;
}

void dla_rubik_dump_config(struct dla_processor_group *group)
{
	struct dla_rubik_op_desc *rubik_op;
	struct dla_rubik_surface_desc *rubik_surface;

	rubik_op = &group->operation_desc->rubik_op;
	rubik_surface = &group->surface_desc->rubik_surface;

	dla_debug("op_desc mode: %d\n", rubik_op->mode);
	dla_debug("op_desc precision: %d\n", rubik_op->precision);
	dla_debug("op_desc stride_x: %d\n", rubik_op->stride_x);
	dla_debug("op_desc stride_y: %d\n", rubik_op->stride_y);

	dla_debug("surface src type:%d\n", rubik_surface->src_data.type);
	dla_debug("surface src address:%d\n", rubik_surface->src_data.address);
	dla_debug("surface src offset:%d\n", rubik_surface->src_data.offset);
	dla_debug("surface src size:%d\n", rubik_surface->src_data.size);
	dla_debug("surface src batch:%d\n", rubik_surface->src_data.batch);
	dla_debug("surface src width:%d\n", rubik_surface->src_data.width);
	dla_debug("surface src height:%d\n", rubik_surface->src_data.height);
	dla_debug("surface src channel:%d\n", rubik_surface->src_data.channel);
	dla_debug("surface src line_stride:%d\n",
		  rubik_surface->src_data.line_stride);
	dla_debug("surface src surf_stride:%d\n",
		  rubik_surface->src_data.surf_stride);
	dla_debug("surface src plane_stride:%d\n",
		  rubik_surface->src_data.plane_stride);

	dla_debug("surface dst type:%d\n", rubik_surface->dst_data.type);
	dla_debug("surface dst address:%d\n", rubik_surface->dst_data.address);
	dla_debug("surface dst offset:%d\n", rubik_surface->dst_data.offset);
	dla_debug("surface dst size:%d\n", rubik_surface->dst_data.size);
	dla_debug("surface dst batch:%d\n", rubik_surface->dst_data.batch);
	dla_debug("surface dst width:%d\n", rubik_surface->dst_data.width);
	dla_debug("surface dst height:%d\n", rubik_surface->dst_data.height);
	dla_debug("surface dst channel:%d\n", rubik_surface->dst_data.channel);
	dla_debug("surface dst line_stride:%d\n",
		  rubik_surface->dst_data.line_stride);
	dla_debug("surface dst surf_stride:%d\n",
		  rubik_surface->dst_data.surf_stride);
	dla_debug("surface dst plane_stride:%d\n",
		  rubik_surface->dst_data.plane_stride);
}

int dla_rubik_prepare_prog_data(struct win_executor *executor, int rdma,
				int tensor_idx, u16 op_idx,
				union dla_operation_container *operation_desc,
				union dla_surface_container *surface_desc)
{
	int32_t ret = 0;
	ret = processor_rubik_program(executor, rdma, tensor_idx,
				      operation_desc, surface_desc);
	if (ret)
		goto exit;

exit:
	return ret;
}
