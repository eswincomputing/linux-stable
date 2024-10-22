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
#include "post_drp.h"
#include "dla_driver.h"
#include "internal_interface.h"
#include "dla_buffer.h"

#define MAX_SPLIT_NUM 64
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a[0])))
#endif

static const uint8_t map_ram[] = {
	FIELD_ENUM(PDP_RDMA_D_SRC_RAM_CFG_0, SRC_RAM_TYPE, MC),
	FIELD_ENUM(PDP_RDMA_D_SRC_RAM_CFG_0, SRC_RAM_TYPE, CV),
};

static const uint8_t map_pool[] = {
	FIELD_ENUM(PDP_D_OPERATION_MODE_CFG_0, POOLING_METHOD,
		   POOLING_METHOD_AVERAGE),
	FIELD_ENUM(PDP_D_OPERATION_MODE_CFG_0, POOLING_METHOD,
		   POOLING_METHOD_MAX),
	FIELD_ENUM(PDP_D_OPERATION_MODE_CFG_0, POOLING_METHOD,
		   POOLING_METHOD_MIN),
};

static const uint8_t map_precision[] = {
	FIELD_ENUM(PDP_D_DATA_FORMAT_0, INPUT_DATA, INT8),
	FIELD_ENUM(PDP_D_DATA_FORMAT_0, INPUT_DATA, INT16),
	FIELD_ENUM(PDP_D_DATA_FORMAT_0, INPUT_DATA, FP16),
};

static const uint8_t map_pool_kernel[] = {
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_1),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_2),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_3),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_4),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_5),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_6),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_7),
	FIELD_ENUM(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH, KERNEL_WIDTH_8),
};

/* The reciprocal of kernel width: 1/1, 1/2, 1/3, ... */
static const uint32_t recip_kernel_size[2][8] = {
	/*
	 * INT8/16
	 * 1      1/2     1/3     1/4     1/5     1/6     1/7     1/8
	 */
	{ 0x10000, 0x8000, 0x5555, 0x4000, 0x3333, 0x2aaa, 0x2492, 0x2000 },
	{ 0x7c00, 0x7800, 0x7555, 0x7400, 0x7266, 0x7155, 0x7092, 0x7000 },
};

static uint32_t get_fly_mode(uint8_t type)
{
	uint32_t val;

	val = type == DLA_MEM_HW ? FIELD_ENUM(PDP_D_OPERATION_MODE_CFG_0,
					      FLYING_MODE, ON_FLYING) :
				   FIELD_ENUM(PDP_D_OPERATION_MODE_CFG_0,
					      FLYING_MODE, OFF_FLYING);

	return val;
}

int dla_pdp_rdma_check(struct dla_processor_group *group,
		       union dla_operation_container *op,
		       union dla_surface_container *surface)
{
	struct dla_pdp_surface_desc *pdp_surface;
	int ret;

	if (group) {
		pdp_surface = &group->surface_desc->pdp_surface;
		group->is_rdma_needed = 0;
	} else {
		pdp_surface = &surface->pdp_surface;
	}

	ret = pdp_surface->src_data.type != DLA_MEM_HW;
	if (group) {
		group->is_rdma_needed = ret;
	}
	return ret;
}
static int validate_strides(uint8_t stride_x, uint8_t stride_y)
{
	int32_t ret = 0;

	if (stride_x < 1 || stride_y < 1 || stride_x > 8 || stride_y > 8) {
		dla_error("Invalid Stride (x[%d], y[%d])\n", stride_x,
			  stride_y);
		ret = ERR(INVALID_INPUT);
	}

	return ret;
}

static int vaildate_pdp_configs(struct dla_pdp_op_desc *pdp_op,
				struct dla_pdp_surface_desc *pdp_surface)
{
	int32_t ret = 0;

	if (pdp_surface->dst_data.type == DLA_MEM_HW) {
		dla_error(
			"Destination buffer for PDP has to be either MC or CV");
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	ret = validate_data_cube(&pdp_surface->src_data, &pdp_surface->dst_data,
				 DLA_MEM_HW);
	if (ret)
		goto exit;

	ret = validate_precision(pdp_op->precision, ARRAY_SIZE(map_precision));
	if (ret)
		goto exit;

	ret = validate_strides(pdp_op->stride_x, pdp_op->stride_y);
	if (ret)
		goto exit;

	if (pdp_op->split_num > MAX_SPLIT_NUM) {
		dla_error("Invalid split_num: %u\n", pdp_op->split_num);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (pdp_op->pool_mode >= ARRAY_SIZE(map_pool)) {
		dla_error("Invalid pool_mode: %u\n", pdp_op->pool_mode);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (!(pdp_op->pool_height <= 8 && pdp_op->pool_width <= 8)) {
		dla_error(
			"pdp param check error: pool_height <= 8 && pool_width <= 8\n");
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (!(pdp_op->stride_y <= 16 && pdp_op->stride_x <= 16)) {
		dla_error(
			"pdp param check error: stride_y <= 16 && stride_x <= 16\n");
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (!(pdp_surface->dst_data.height > 0 &&
	      pdp_surface->dst_data.width > 0)) {
		dla_error(
			"pdp param check error: dst_data.height > 0 && dst_data.width > 0\n");
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (!(pdp_surface->dst_data.height <= 8192 &&
	      pdp_surface->dst_data.width <= 8192)) {
		dla_error(
			"pdp param check error: dst_data.height <= 8192 && dst_data.width <= 8192\n");
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

exit:
	return ret;
}

int pdp_tensor_unfold(struct win_executor *executor, int op_idx,
		      union dla_operation_container *operation_desc,
		      union dla_surface_container *surface_desc, void *tensor,
		      int idx)
{
	struct dla_pdp_surface_desc *pdp_surface = &surface_desc->pdp_surface;
	pdp_tensor_t *pdp_tensor = (pdp_tensor_t *)tensor;
	int ret;
	pdp_dev_t *pdp =
		(pdp_dev_t *)executor->prog_data_buf_bobj[IDX_PDP];
	pdp_dev_t *pdp_data = (pdp_dev_t *)&pdp[idx];

	pdp_data->npu_info.current_op_idx = op_idx;
	ret = read_input_address(executor, &pdp_surface->dst_data,
				 &pdp_tensor[idx].output_address,
				 &pdp_tensor[idx].output_is_io_tensor);
	if (ret) {
		dla_error("pdp read output address error\n");
		return -1;
	}

	if (pdp_surface->src_data.type == DLA_MEM_HW) {
		return 0;
	}
	ret = read_input_address(executor, &pdp_surface->src_data,
				 &pdp_tensor[idx].input_address,
				 &pdp_tensor[idx].input_is_io_tensor);
	if (ret) {
		dla_error("pdp read input address error\n");
		return -1;
	}

	return 0;
}

static void pdp_post_drp_set_dst_addr(pdp_program_t *data, u64 addr)
{
	data->pdp_drp_reg[R_PDP_POST_BASE_ADDR_H] =
		(u32)(addr >> NUM_BIT_IN_DWORD);
	data->pdp_drp_reg[R_PDP_POST_BASE_ADDR_L] = (u32)addr;
	npu_set_u64_bit(R_PDP_POST_BASE_ADDR_H, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_POST_BASE_ADDR_L, &data->u84_drp_bitmap);
}

static void pdp_post_drp_handle(pdp_program_t *data,
				struct post_drp_op_desc *desc)
{
	data->pdp_drp_reg[R_PDP_DRP_G_STRIDE_SRAM] = desc->g_stride_lsram - 1;
	npu_set_u64_bit(R_PDP_DRP_G_STRIDE_SRAM, &data->u84_drp_bitmap);

	data->pdp_drp_reg[R_PDP_DRP_N_STRIDE_SRAM] = desc->n_stride_lsram - 1;
	data->pdp_drp_reg[R_PDP_DRP_H_STRIDE_SRAM] = desc->h_stride - 1;
	data->pdp_drp_reg[R_PDP_DRP_C_STRIDE_SRAM] = desc->c_stride - 1;
	data->pdp_drp_reg[R_PDP_DRP_W_EXT_STRIDE] = desc->w_stride - 1;
	data->pdp_drp_reg[R_PDP_DRP_LAYER_PARA_L] = (desc->n - 1) << 16 |
						    (desc->e - 1);
	data->pdp_drp_reg[R_PDP_DRP_LAYER_PARA_H] = (desc->m - 1) << 16 |
						    (desc->f - 1);
	data->pdp_drp_reg[R_PDP_DRP_OMAP_PARA_L] = desc->c0 - 1;
	data->pdp_drp_reg[R_PDP_DRP_CTRL] = (desc->surface_double << 1) |
					    desc->type_16;
	data->pdp_drp_reg[R_PDP_DRP_SPLIT] = desc->split_num - 1;

	data->pdp_drp_reg[R_PDP_DRP_PARTIAL_WIDTH] = ((desc->f_lst - 1) << 20) |
						     ((desc->f_mid - 1) << 10) |
						     (desc->f_fst - 1);
	data->pdp_drp_reg[R_PDP_DRP_SRAM_LOOP_PARA_H] =
		((desc->e4_all - 1) << 16) | (desc->m3_all - 1);
	data->pdp_drp_reg[R_PDP_DRP_SRAM_LOOP_PARA_L] =
		((desc->n3_all - 1) << 16) | (desc->g3_all - 1);
	npu_set_u64_bit(R_PDP_DRP_N_STRIDE_SRAM, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_H_STRIDE_SRAM, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_C_STRIDE_SRAM, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_W_EXT_STRIDE, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_LAYER_PARA_L, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_LAYER_PARA_H, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_OMAP_PARA_L, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_CTRL, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_SPLIT, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_PARTIAL_WIDTH, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_SRAM_LOOP_PARA_H, &data->u84_drp_bitmap);
	npu_set_u64_bit(R_PDP_DRP_SRAM_LOOP_PARA_L, &data->u84_drp_bitmap);
}

static int processor_pdp_program(struct win_executor *executor, int rdma,
				 int tensor_idx,
				 union dla_operation_container *operation_desc,
				 union dla_surface_container *surface_desc)
{
	int32_t ret = 0;
	uint32_t reg, high, low;
	uint64_t input_address = 0;
	uint64_t output_address = 0;
	struct dla_pdp_op_desc *pdp_op;
	struct dla_pdp_surface_desc *pdp_surface;
	pdp_tensor_t *tensor = NULL;
	pdp_tensor_t *pdp_tensor = NULL;
	pdp_program_t *data = NULL;
	pdp_dev_t *pdp =
		(pdp_dev_t *)executor->prog_data_buf_bobj[IDX_PDP];
	pdp_dev_t *pdp_data = (pdp_dev_t *)&pdp[tensor_idx];

	tensor = (pdp_tensor_t *)executor->tensor_set[IDX_PDP];
	pdp_tensor = &tensor[tensor_idx];

	data = &pdp_data->prog_data;
	pdp_data->prog_data.input_tensor_idx = invalid_tensor_idx;
	pdp_data->prog_data.output_tensor_idx = invalid_tensor_idx;

	pdp_op = &operation_desc->pdp_op;
	pdp_surface = &surface_desc->pdp_surface;

	if (pdp_surface->src_data.type != DLA_MEM_HW) {
		if (pdp_tensor->input_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(pdp_tensor->input_address == -1ull)) {
				dla_error("pdp read input address error\n");
				goto exit;
			} else {
				input_address = pdp_tensor->input_address;
			}
		} else {
			input_address = pdp_surface->src_data.offset;
			pdp_data->prog_data.input_tensor_idx =
				pdp_tensor->input_is_io_tensor;
		}
	}

	ret = vaildate_pdp_configs(pdp_op, pdp_surface);
	data->is_rdma = (pdp_surface->src_data.type != DLA_MEM_HW);

	if (ret) {
		dla_error("pdp params check error\n");
		goto exit;
	}

	pdp_post_drp_handle(data, &pdp_op->post_drp_op);

	if (pdp_tensor->output_is_io_tensor == invalid_tensor_idx) {
		if (pdp_surface->dst_data.address != -1) {
			if (unlikely(pdp_tensor->output_address == -1ull)) {
				dla_error("pdp output_address error\n");
				goto exit;
			} else {
				output_address = pdp_tensor->output_address;
			}
		}
	} else {
		output_address = pdp_surface->dst_data.offset;
		pdp_data->prog_data.output_tensor_idx =
			pdp_tensor->output_is_io_tensor;
	}

	pdp_post_drp_set_dst_addr(data, output_address);

	if (pdp_surface->src_data.type != DLA_MEM_HW) {
		data->pdp_rdma_reg[R_DATA_CUBE_IN_WIDTH] =
			pdp_surface->src_data.width - 1;
		data->pdp_rdma_reg[R_DATA_CUBE_IN_HEIGHT] =
			pdp_surface->src_data.height - 1;
		data->pdp_rdma_reg[R_DATA_CUBE_IN_CHANNEL] =
			pdp_surface->src_data.channel - 1;
		npu_set_u64_bit(R_DATA_CUBE_IN_WIDTH, &data->u84_rdma_bitmap);
		npu_set_u64_bit(R_DATA_CUBE_IN_HEIGHT, &data->u84_rdma_bitmap);
		npu_set_u64_bit(R_DATA_CUBE_IN_CHANNEL, &data->u84_rdma_bitmap);

		high = HIGH32BITS(input_address);
		low = LOW32BITS(input_address);
		data->pdp_rdma_reg[R_SRC_BASE_ADDR_HIGH] = high;
		data->pdp_rdma_reg[R_SRC_BASE_ADDR_LOW] = low;
		npu_set_u64_bit(R_SRC_BASE_ADDR_HIGH, &data->u84_rdma_bitmap);
		npu_set_u64_bit(R_SRC_BASE_ADDR_LOW, &data->u84_rdma_bitmap);

		data->pdp_rdma_reg[R_SRC_LINE_STRIDE] =
			pdp_surface->src_data.line_stride;
		data->pdp_rdma_reg[R_SRC_SURFACE_STRIDE] =
			pdp_surface->src_data.surf_stride;
		npu_set_u64_bit(R_SRC_LINE_STRIDE, &data->u84_rdma_bitmap);
		npu_set_u64_bit(R_SRC_SURFACE_STRIDE, &data->u84_rdma_bitmap);

		reg = (map_precision[pdp_op->precision]
		       << SHIFT(PDP_RDMA_D_DATA_FORMAT_0, INPUT_DATA));
		data->pdp_rdma_reg[R_DATA_FORMAT] = reg;
		npu_set_u64_bit(R_DATA_FORMAT, &data->u84_rdma_bitmap);

		reg = map_ram[pdp_surface->src_data.type]
		      << SHIFT(PDP_RDMA_D_SRC_RAM_CFG_0, SRC_RAM_TYPE);
		data->pdp_rdma_reg[R_SRC_RAM_CFG] = reg;
		npu_set_u64_bit(R_SRC_RAM_CFG, &data->u84_rdma_bitmap);

		reg = ((pdp_op->split_num - 1)
		       << SHIFT(PDP_RDMA_D_OPERATION_MODE_CFG_0, SPLIT_NUM));
		data->pdp_rdma_reg[R_OPERATION_MODE_CFG] = reg;
		npu_set_u64_bit(R_OPERATION_MODE_CFG, &data->u84_rdma_bitmap);

		reg = (map_pool_kernel[pdp_op->pool_width - 1] << SHIFT(
			       PDP_RDMA_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH)) |
		      ((pdp_op->stride_x - 1)
		       << SHIFT(PDP_RDMA_D_POOLING_KERNEL_CFG_0,
				KERNEL_STRIDE_WIDTH));
		data->pdp_rdma_reg[R_POOLING_KERNEL_CFG] = reg;
		npu_set_u64_bit(R_POOLING_KERNEL_CFG, &data->u84_rdma_bitmap);

		reg = (pdp_op->pad_left
		       << SHIFT(PDP_RDMA_D_POOLING_PADDING_CFG_0, PAD_WIDTH));
		data->pdp_rdma_reg[R_POOLING_PADDING_CFG] = reg;
		npu_set_u64_bit(R_POOLING_PADDING_CFG, &data->u84_rdma_bitmap);
		reg = ((pdp_op->partial_in_width_first == 0 ?
				0 :
				pdp_op->partial_in_width_first - 1)
		       << SHIFT(PDP_RDMA_D_PARTIAL_WIDTH_IN_0,
				PARTIAL_WIDTH_IN_FIRST)) |
		      ((pdp_op->partial_in_width_mid == 0 ?
				0 :
				pdp_op->partial_in_width_mid - 1)
		       << SHIFT(PDP_RDMA_D_PARTIAL_WIDTH_IN_0,
				PARTIAL_WIDTH_IN_MID)) |
		      ((pdp_op->partial_in_width_last == 0 ?
				0 :
				pdp_op->partial_in_width_last - 1)
		       << SHIFT(PDP_RDMA_D_PARTIAL_WIDTH_IN_0,
				PARTIAL_WIDTH_IN_LAST));
		data->pdp_rdma_reg[R_PARTIAL_WIDTH_IN] = reg;
		npu_set_u64_bit(R_PARTIAL_WIDTH_IN, &data->u84_rdma_bitmap);

	} else {
		if (pdp_op->split_num != 1) {
			dla_error("pdp split num invalid, %d\n",
				  pdp_op->split_num);
			ret = ERR(INVALID_INPUT);
			goto exit;
		}
	}

	reg = ((pdp_surface->src_data.width - 1)
	       << SHIFT(PDP_D_DATA_CUBE_IN_WIDTH_0, CUBE_IN_WIDTH));
	data->reg[D_DATA_CUBE_IN_WIDTH] = reg;
	npu_set_u64_bit(D_DATA_CUBE_IN_WIDTH, &data->u84_bitmap);

	reg = ((pdp_surface->src_data.height - 1)
	       << SHIFT(PDP_D_DATA_CUBE_IN_HEIGHT_0, CUBE_IN_HEIGHT));
	data->reg[D_DATA_CUBE_IN_HEIGHT] = reg;
	npu_set_u64_bit(D_DATA_CUBE_IN_HEIGHT, &data->u84_bitmap);

	reg = ((pdp_surface->src_data.channel - 1)
	       << SHIFT(PDP_D_DATA_CUBE_IN_CHANNEL_0, CUBE_IN_CHANNEL));
	data->reg[D_DATA_CUBE_IN_CHANNEL] = reg;
	npu_set_u64_bit(D_DATA_CUBE_IN_CHANNEL, &data->u84_bitmap);

	reg = ((pdp_surface->dst_data.width - 1)
	       << SHIFT(PDP_D_DATA_CUBE_OUT_WIDTH_0, CUBE_OUT_WIDTH));
	data->reg[D_DATA_CUBE_OUT_WIDTH] = reg;
	npu_set_u64_bit(D_DATA_CUBE_OUT_WIDTH, &data->u84_bitmap);

	reg = ((pdp_surface->dst_data.height - 1)
	       << SHIFT(PDP_D_DATA_CUBE_OUT_HEIGHT_0, CUBE_OUT_HEIGHT));
	data->reg[D_DATA_CUBE_OUT_HEIGHT] = reg;
	npu_set_u64_bit(D_DATA_CUBE_OUT_HEIGHT, &data->u84_bitmap);

	reg = ((pdp_surface->dst_data.channel - 1)
	       << SHIFT(PDP_D_DATA_CUBE_OUT_CHANNEL_0, CUBE_OUT_CHANNEL));
	data->reg[D_DATA_CUBE_OUT_CHANNEL] = reg;
	npu_set_u64_bit(D_DATA_CUBE_OUT_CHANNEL, &data->u84_bitmap);

	reg = (map_pool[pdp_op->pool_mode]
	       << SHIFT(PDP_D_OPERATION_MODE_CFG_0, POOLING_METHOD)) |
	      (get_fly_mode(pdp_surface->src_data.type)
	       << SHIFT(PDP_D_OPERATION_MODE_CFG_0, FLYING_MODE)) |
	      ((pdp_op->split_num - 1)
	       << SHIFT(PDP_D_OPERATION_MODE_CFG_0, SPLIT_NUM));
	data->reg[D_OPERATION_MODE_CFG] = reg;
	npu_set_u64_bit(D_OPERATION_MODE_CFG, &data->u84_bitmap);

	reg = ((pdp_op->partial_in_width_first == 0 ?
			0 :
			pdp_op->partial_in_width_first - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_IN_0, PARTIAL_WIDTH_IN_FIRST)) |
	      ((pdp_op->partial_in_width_mid == 0 ?
			0 :
			pdp_op->partial_in_width_mid - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_IN_0, PARTIAL_WIDTH_IN_MID)) |
	      ((pdp_op->partial_in_width_last == 0 ?
			0 :
			pdp_op->partial_in_width_last - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_IN_0, PARTIAL_WIDTH_IN_LAST));
	data->reg[D_PARTIAL_WIDTH_IN] = reg;
	npu_set_u64_bit(D_PARTIAL_WIDTH_IN, &data->u84_bitmap);

	reg = ((pdp_op->partial_width_first == 0 ?
			0 :
			pdp_op->partial_width_first - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_OUT_0, PARTIAL_WIDTH_OUT_FIRST)) |
	      ((pdp_op->partial_width_mid == 0 ? 0 :
						 pdp_op->partial_width_mid - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_OUT_0, PARTIAL_WIDTH_OUT_MID)) |
	      ((pdp_op->partial_width_last == 0 ?
			0 :
			pdp_op->partial_width_last - 1)
	       << SHIFT(PDP_D_PARTIAL_WIDTH_OUT_0, PARTIAL_WIDTH_OUT_LAST));
	data->reg[D_PARTIAL_WIDTH_OUT] = reg;
	npu_set_u64_bit(D_PARTIAL_WIDTH_OUT, &data->u84_bitmap);

	reg = (map_pool_kernel[pdp_op->pool_width - 1]
	       << SHIFT(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_WIDTH)) |
	      (map_pool_kernel[pdp_op->pool_height - 1]
	       << SHIFT(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_HEIGHT)) |
	      ((pdp_op->stride_x - 1)
	       << SHIFT(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_STRIDE_WIDTH)) |
	      ((pdp_op->stride_y - 1)
	       << SHIFT(PDP_D_POOLING_KERNEL_CFG_0, KERNEL_STRIDE_HEIGHT));
	data->reg[D_POOLING_KERNEL_CFG] = reg;
	npu_set_u64_bit(D_POOLING_KERNEL_CFG, &data->u84_bitmap);

	data->reg[D_RECIP_KERNEL_WIDTH] =
		(recip_kernel_size[pdp_op->precision == PRECISION_FP16]
				  [pdp_op->pool_width - 1]);

	npu_set_u64_bit(D_RECIP_KERNEL_WIDTH, &data->u84_bitmap);

	data->reg[D_RECIP_KERNEL_HEIGHT] =
		(recip_kernel_size[pdp_op->precision == PRECISION_FP16]
				  [pdp_op->pool_height - 1]);
	npu_set_u64_bit(D_RECIP_KERNEL_HEIGHT, &data->u84_bitmap);

	reg = (pdp_op->pad_left
	       << SHIFT(PDP_D_POOLING_PADDING_CFG_0, PAD_LEFT)) |
	      (pdp_op->pad_right
	       << SHIFT(PDP_D_POOLING_PADDING_CFG_0, PAD_RIGHT)) |
	      (pdp_op->pad_top << SHIFT(PDP_D_POOLING_PADDING_CFG_0, PAD_TOP)) |
	      (pdp_op->pad_bottom
	       << SHIFT(PDP_D_POOLING_PADDING_CFG_0, PAD_BOTTOM));
	if (pdp_op->precision == PRECISION_FP16) {
		int32_t i;

		for (i = 0; i < PDP_PAD_VAL_NUM; i++) {
			if (pdp_op->padding_value[i] != 0) {
				dla_error("pdp padding value is invalid\n");
				ret = ERR(INVALID_INPUT);
				goto exit;
			}
		}
	}

	data->reg[D_POOLING_PADDING_CFG] = reg;
	npu_set_u64_bit(D_POOLING_PADDING_CFG, &data->u84_bitmap);

	data->reg[D_POOLING_PADDING_VALUE_1_CFG] = pdp_op->padding_value[0];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_1_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_2_CFG] = pdp_op->padding_value[1];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_2_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_3_CFG] = pdp_op->padding_value[2];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_3_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_4_CFG] = pdp_op->padding_value[3];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_4_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_5_CFG] = pdp_op->padding_value[4];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_5_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_6_CFG] = pdp_op->padding_value[5];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_6_CFG, &data->u84_bitmap);
	data->reg[D_POOLING_PADDING_VALUE_7_CFG] = pdp_op->padding_value[6];
	npu_set_u64_bit(D_POOLING_PADDING_VALUE_7_CFG, &data->u84_bitmap);

	if (pdp_surface->src_data.type != DLA_MEM_HW) {
		data->reg[D_SRC_LINE_STRIDE] =
			pdp_surface->src_data.line_stride;
		npu_set_u64_bit(D_SRC_LINE_STRIDE, &data->u84_bitmap);
		data->reg[D_SRC_SURFACE_STRIDE] =
			pdp_surface->src_data.surf_stride;
		npu_set_u64_bit(D_SRC_SURFACE_STRIDE, &data->u84_bitmap);
	}
	data->reg[D_PDP_DST_LINE_STRIDE] = pdp_surface->dst_data.line_stride;
	npu_set_u64_bit(D_PDP_DST_LINE_STRIDE, &data->u84_bitmap);

	data->reg[D_PDP_DST_SURFACE_STRIDE] = pdp_surface->dst_data.surf_stride;
	npu_set_u64_bit(D_PDP_DST_SURFACE_STRIDE, &data->u84_bitmap);

	reg = (map_precision[pdp_op->precision]
	       << SHIFT(PDP_D_DATA_FORMAT_0, INPUT_DATA));
	data->reg[D_PDP_DATA_FORMAT] = reg;
	npu_set_u64_bit(D_PDP_DATA_FORMAT, &data->u84_bitmap);

	data->pdp_drp_reg[D_PDP_POST_DRP_ENABLE] = 1;
	npu_set_u64_bit(D_PDP_POST_DRP_ENABLE, &data->u84_drp_bitmap);
exit:
	return ret;
}

int dla_pdp_prepare_prog_data(struct win_executor *executor, int rdma,
			      int tensor_idx, u16 op_idx,
			      union dla_operation_container *operation_desc,
			      union dla_surface_container *surface_desc)

{
	int32_t ret;
	dla_debug("%s %d\n", __func__, __LINE__);

	ret = processor_pdp_program(executor, rdma, tensor_idx, operation_desc,
				    surface_desc);
	if (ret)
		goto exit;

exit:
	return ret;
}
