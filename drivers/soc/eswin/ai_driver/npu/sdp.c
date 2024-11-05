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
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "dla_log.h"
#include "post_drp.h"
#include "dla_driver.h"
#include "internal_interface.h"
#include "sdp_regs.h"
#include "dla_buffer.h"

static const uint8_t map_ena[] = {
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DISABLE, YES),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DISABLE, NO),
};

static const uint8_t map_prelu[] = {
	FIELD_ENUM(SDP_D_DP_BS_CFG_0, BS_MUL_PRELU, NO),
	FIELD_ENUM(SDP_D_DP_BS_CFG_0, BS_MUL_PRELU, YES),
};

static const uint8_t map_bypass[] = {
	FIELD_ENUM(SDP_D_DP_BS_CFG_0, BS_BYPASS, YES),
	FIELD_ENUM(SDP_D_DP_BS_CFG_0, BS_BYPASS, NO),
};

static const uint8_t map_alu_op[] = {
	FIELD_ENUM(SDP_D_DP_EW_CFG_0, EW_ALU_ALGO, MAX),
	FIELD_ENUM(SDP_D_DP_EW_CFG_0, EW_ALU_ALGO, MIN),
	FIELD_ENUM(SDP_D_DP_EW_CFG_0, EW_ALU_ALGO, SUM),
	FIELD_ENUM(SDP_D_DP_EW_CFG_0, EW_ALU_ALGO, EQL),
};

static const uint8_t map_alu_src[] = {
	FIELD_ENUM(SDP_D_DP_BS_ALU_CFG_0, BS_ALU_SRC, MEM),
	FIELD_ENUM(SDP_D_DP_BS_ALU_CFG_0, BS_ALU_SRC, REG),
};

static const uint8_t map_fly[] = {
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, FLYING_MODE, OFF),
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, FLYING_MODE, ON),
};

static const uint8_t map_dst[] = {
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, OUTPUT_DST, MEM),
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, OUTPUT_DST, PDP),
};

static const uint8_t map_wg[] = {
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, WINOGRAD, OFF),
	FIELD_ENUM(SDP_D_FEATURE_MODE_CFG_0, WINOGRAD, ON),
};

static const uint8_t map_precision[] = {
	FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT8),
	FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT16),
	FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, FP16),
};

static const uint32_t map_proc_precision[3][3] = {
	{
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT8),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT8),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, FP16),
	},
	{
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT8),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT16),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, FP16),
	},
	{
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT8),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, INT16),
		FIELD_ENUM(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION, FP16),
	},
};

static const uint8_t map_op_type[] = {
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_USE, MUL),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_USE, MUL),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_USE, ALU),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_USE, BOTH),
};

static const uint8_t map_element_size[] = {
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_SIZE, ONE_BYTE),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_SIZE, TWO_BYTE),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_SIZE, TWO_BYTE),
};

static const uint8_t map_op_mode[] = {
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_MODE, PER_ELEMENT),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_MODE, PER_KERNEL),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_MODE, PER_ELEMENT),
};

static const uint8_t map_ram_type[] = {
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_RAM_TYPE, MC),
	FIELD_ENUM(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_RAM_TYPE, CV),
};

static const uint8_t map_perf_dma[] = {
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_DMA_EN, NO),
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_DMA_EN, YES),
};

static const uint8_t map_perf_lut[] = {
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_LUT_EN, NO),
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_LUT_EN, YES),
};

static const uint8_t map_perf_sat[] = {
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_SAT_EN, NO),
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_SAT_EN, YES),
};

static const uint8_t map_perf_nan_inf[] = {
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_NAN_INF_COUNT_EN, NO),
	FIELD_ENUM(SDP_D_PERF_ENABLE_0, PERF_NAN_INF_COUNT_EN, YES),
};

int dla_sdp_rdma_check(struct dla_processor_group *group,
		       union dla_operation_container *op,
		       union dla_surface_container *surface)
{
	uint8_t x1_rdma_ena;
	uint8_t x2_rdma_ena;
	uint8_t y_rdma_ena;
	uint8_t fly;
	int ret;
	struct dla_sdp_op_desc *sdp_op;
	struct dla_sdp_surface_desc *sdp_surface;

	if (group) {
		sdp_op = &group->operation_desc->sdp_op;
		sdp_surface = &group->surface_desc->sdp_surface;
	} else {
		sdp_op = &op->sdp_op;
		sdp_surface = &surface->sdp_surface;
	}

	x1_rdma_ena = sdp_op->x1_op.enable;
	x2_rdma_ena = sdp_op->x2_op.enable;
	y_rdma_ena = sdp_op->y_op.enable;

	x1_rdma_ena &= (sdp_op->x1_op.mode != SDP_OP_PER_LAYER);
	x2_rdma_ena &= (sdp_op->x2_op.mode != SDP_OP_PER_LAYER);
	y_rdma_ena &= (sdp_op->y_op.mode != SDP_OP_PER_LAYER);

	fly = sdp_surface->src_data.type == DLA_MEM_HW;
	ret = (!fly) || (x1_rdma_ena || x2_rdma_ena || y_rdma_ena);

	if (group) {
		group->is_rdma_needed = ret;
	}
	return ret;
}

int sdp_tensor_unfold(struct win_executor *executor, int op_idx,
		      union dla_operation_container *operation_desc,
		      union dla_surface_container *surface_desc, void *tensor,
		      int idx)
{
	struct dla_sdp_op_desc *sdp_op = &operation_desc->sdp_op;
	struct dla_sdp_surface_desc *sdp_surface = &surface_desc->sdp_surface;
	struct dla_sdp_op *x1_op;
	struct dla_sdp_op *x2_op;
	struct dla_sdp_op *y_op;
	uint8_t x1_rdma_ena;
	uint8_t x2_rdma_ena;
	uint8_t y_rdma_ena;
	uint8_t out_dma_ena;
	uint8_t fly;
	sdp_tensor_t *sdp_tensor = (sdp_tensor_t *)tensor;
	int ret;
	void *vaddr = executor->prog_data_buf_bobj[IDX_SDP];
	sdp_dev_t *sdp = &(((sdp_dev_t *)vaddr)[idx]);

	fly = sdp_surface->src_data.type == DLA_MEM_HW;
	out_dma_ena = sdp_surface->dst_data.type != DLA_MEM_HW;
	x1_op = &sdp_op->x1_op;
	x2_op = &sdp_op->x2_op;
	y_op = &sdp_op->y_op;
	x1_rdma_ena = x1_op->enable && x1_op->type != SDP_OP_NONE;
	x2_rdma_ena = x2_op->enable && x2_op->type != SDP_OP_NONE;
	y_rdma_ena = y_op->enable && y_op->type != SDP_OP_NONE;

	sdp->npu_info.current_op_idx = op_idx;
	/* load address */
	if (!fly) {
		ret = read_input_address(executor, &sdp_surface->src_data,
					 &sdp_tensor[idx].no_fly_src_addr,
					 &sdp_tensor[idx].src_is_io_tensor);
		if (ret < 0) {
			dla_error("%s %d sdp src addr read fail\n", __func__,
				  __LINE__);
			goto exit;
		}
		CHECK_ALIGN(sdp_tensor[idx].no_fly_src_addr, atom_size);
	}
	if (out_dma_ena) {
		ret = read_input_address(executor, &sdp_surface->dst_data,
					 &sdp_tensor[idx].out_dma_dst_addr,
					 &sdp_tensor[idx].dst_is_io_tensor);
		if (ret < 0) {
			dla_error("%s %d sdp dst addr read fail", __func__,
				  __LINE__);
			goto exit;
		}
		CHECK_ALIGN(sdp_tensor[idx].out_dma_dst_addr, atom_size);
	}

	x1_rdma_ena &= (x1_op->mode != SDP_OP_PER_LAYER);
	x2_rdma_ena &= (x2_op->mode != SDP_OP_PER_LAYER);
	y_rdma_ena &= (y_op->mode != SDP_OP_PER_LAYER);

	if (x1_rdma_ena) {
		ret = read_input_address(executor, &sdp_surface->x1_data,
					 &sdp_tensor[idx].x1_addr,
					 &sdp_tensor[idx].x1_is_io_tensor);
		if (ret) {
			dla_error("%s %d sdp x1_addr read fail\n", __func__,
				  __LINE__);
			goto exit;
		}
		CHECK_ALIGN(sdp_tensor->x1_addr, atom_size);
	}
	if (x2_rdma_ena) {
		ret = read_input_address(executor, &sdp_surface->x2_data,
					 &sdp_tensor[idx].x2_addr,
					 &sdp_tensor[idx].x2_is_io_tensor);
		if (ret) {
			dla_error("%s %d sdp x2_addr read fail\n", __func__,
				  __LINE__);
			goto exit;
		}
		CHECK_ALIGN(sdp_tensor[idx].x2_addr, atom_size);
	}
	if (y_rdma_ena) {
		ret = read_input_address(executor, &sdp_surface->y_data,
					 &sdp_tensor[idx].y_addr,
					 &sdp_tensor[idx].y_is_io_tensor);
		if (ret) {
			dla_error("%s %d sdp y_addr read fail\n", __func__,
				  __LINE__);
			goto exit;
		}
		CHECK_ALIGN(sdp_tensor[idx].y_addr, atom_size);
	}
exit:
	return ret;
}

static int sdp_post_drp_handle(sdp_program_t *data,
			       struct post_drp_op_desc *desc)
{
	data->sdp_drp_reg[DRP_D_REG_G_STRIDE_SRAM] = desc->g_stride_lsram - 1;
	npu_set_u64_bit(DRP_D_REG_G_STRIDE_SRAM, &data->u84_drp_bitmap);
	data->sdp_drp_reg[DRP_D_REG_N_STRIDE_SRAM] = desc->n_stride_lsram - 1;
	npu_set_u64_bit(DRP_D_REG_N_STRIDE_SRAM, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_H_STRIDE_SRAM] = desc->h_stride - 1;
	npu_set_u64_bit(DRP_D_REG_H_STRIDE_SRAM, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_C_STRIDE_SRAM] = desc->c_stride - 1;
	npu_set_u64_bit(DRP_D_REG_C_STRIDE_SRAM, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_W_EXT_STRIDE] = desc->w_stride - 1;
	npu_set_u64_bit(DRP_D_REG_W_EXT_STRIDE, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_LAYER_PARA_L] = (desc->n - 1) << 16 |
						    (desc->e - 1);
	npu_set_u64_bit(DRP_D_REG_LAYER_PARA_L, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_LAYER_PARA_H] = (desc->m - 1) << 16 |
						    (desc->f - 1);
	npu_set_u64_bit(DRP_D_REG_LAYER_PARA_H, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_OMAP_PARA_L] = desc->c0 - 1;
	npu_set_u64_bit(DRP_D_REG_OMAP_PARA_L, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_CTRL] = (desc->surface_double << 1) |
					    desc->type_16;
	npu_set_u64_bit(DRP_D_REG_CTRL, &data->u84_drp_bitmap);
	data->sdp_drp_reg[DRP_D_REG_SPLIT] = desc->split_num - 1;
	npu_set_u64_bit(DRP_D_REG_SPLIT, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_PARTIAL_WIDTH] = ((desc->f_lst - 1) << 20) |
						     ((desc->f_mid - 1) << 10) |
						     (desc->f_fst - 1);
	npu_set_u64_bit(DRP_D_REG_PARTIAL_WIDTH, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_SRAM_LOOP_PARA_H] =
		((desc->e4_all - 1) << 16) | (desc->m3_all - 1);
	npu_set_u64_bit(DRP_D_REG_SRAM_LOOP_PARA_H, &data->u84_drp_bitmap);

	data->sdp_drp_reg[DRP_D_REG_SRAM_LOOP_PARA_L] =
		((desc->n3_all - 1) << 16) | (desc->g3_all - 1);
	npu_set_u64_bit(DRP_D_REG_SRAM_LOOP_PARA_L, &data->u84_drp_bitmap);
	return 0;
}

static void sdp_post_drp_set_dst_addr(sdp_program_t *data, u64 addr)
{
	data->sdp_drp_reg[DRP_D_REG_BASE_ADDR_H] = (u32)(addr >> 32);
	data->sdp_drp_reg[DRP_D_REG_BASE_ADDR_L] = (u32)addr;

	npu_set_u64_bit(DRP_D_REG_BASE_ADDR_H, &data->u84_drp_bitmap);
	npu_set_u64_bit(DRP_D_REG_BASE_ADDR_L, &data->u84_drp_bitmap);
}

static int32_t
processor_sdp_program(struct win_executor *executor, int rdma, int idx,
		      union dla_operation_container *operation_desc,
		      union dla_surface_container *surface_desc)
{
	int32_t ret = 0;
	uint32_t not_used;
	uint64_t src_addr = -1, x1_addr = -1, x2_addr = -1;
	uint64_t y_addr = -1, dst_addr = -1;
	uint32_t reg, high, low;
	uint8_t fly;
	struct dla_sdp_op *x1_op;
	struct dla_sdp_op *x2_op;
	struct dla_sdp_op *y_op;
	uint8_t x1_rdma_ena;
	uint8_t x2_rdma_ena;
	uint8_t y_rdma_ena;
	uint8_t out_dma_ena;
	struct dla_sdp_op_desc *sdp_op;
	struct dla_sdp_surface_desc *sdp_surface;
	sdp_tensor_t *tensor = executor->tensor_set[IDX_SDP];
	sdp_tensor_t *sdp_tensor = NULL;
	void *vaddr = executor->prog_data_buf_bobj[IDX_SDP];
	sdp_dev_t *sdp = &(((sdp_dev_t *)vaddr)[idx]);
	sdp_program_t *sdp_prog = &sdp->prog_data;
	sdp_tensor = &tensor[idx];

	sdp_op = &operation_desc->sdp_op;
	sdp_surface = &surface_desc->sdp_surface;

	sdp_prog->input_tensor_idx = invalid_tensor_idx;
	sdp_prog->output_tensor_idx = invalid_tensor_idx;
	sdp_prog->x1_tensor_idx = invalid_tensor_idx;
	sdp_prog->x2_tensor_idx = invalid_tensor_idx;
	sdp_prog->y_tensor_idx = invalid_tensor_idx;
	dla_debug(
		"sdp_tensor_set no_fly_src_addr 0x%llx out_dma_dst_addr 0x%llx x1_addr 0x%llx x2_addr 0x%llx y_addr 0x%llx\n",
		sdp_tensor->no_fly_src_addr, sdp_tensor->out_dma_dst_addr,
		sdp_tensor->x1_addr, sdp_tensor->x2_addr, sdp_tensor->y_addr);

	fly = sdp_surface->src_data.type == DLA_MEM_HW;
	out_dma_ena = sdp_surface->dst_data.type != DLA_MEM_HW;
	x1_op = &sdp_op->x1_op;
	x2_op = &sdp_op->x2_op;
	y_op = &sdp_op->y_op;

	x1_rdma_ena = x1_op->enable && x1_op->type != SDP_OP_NONE;
	x2_rdma_ena = x2_op->enable && x2_op->type != SDP_OP_NONE;
	y_rdma_ena = y_op->enable && y_op->type != SDP_OP_NONE;

	x1_rdma_ena &= (x1_op->mode != SDP_OP_PER_LAYER);
	x2_rdma_ena &= (x2_op->mode != SDP_OP_PER_LAYER);
	y_rdma_ena &= (y_op->mode != SDP_OP_PER_LAYER);

	sdp_prog->is_rdma = (!fly) || (x1_rdma_ena || x2_rdma_ena || y_rdma_ena);
#ifdef CONV_DUMP
	dla_debug(
		"--fly: %d, x1_rdma_ena: %d, x2_rdma_ena: %d, y_rdma_ena: %x\n",
		fly, x1_rdma_ena, x2_rdma_ena, y_rdma_ena);

	dla_debug("--src_precision: %d\n", sdp_op->src_precision);
	dla_debug("--dst_precision: %d\n", sdp_op->dst_precision);
	dla_debug("--lut_index: %d\n", sdp_op->lut_index);
	dla_debug("--out_cvt.scale: %d\n", sdp_op->out_cvt.scale);
	dla_debug("--out_cvt.truncate: %d\n", sdp_op->out_cvt.truncate);
	dla_debug("--out_cvt.enable: %d\n", sdp_op->out_cvt.enable);
	dla_debug("--sdp_op->out_cvt.offset: %d\n", sdp_op->out_cvt.offset);
	dla_debug("--conv_mode: %d\n", sdp_op->conv_mode);
	dla_debug("--batch_num: %d\n", sdp_op->batch_num);
	dla_debug("--batch_stride: %d\n", sdp_op->batch_stride);

	dla_debug("--src_data.type: %d\n", sdp_surface->src_data.type);
	dla_debug("--src_data.address: %d\n", sdp_surface->src_data.address);
	dla_debug("--src_data.offset: %d\n", sdp_surface->src_data.offset);
	dla_debug("--src_data.size: %d\n", sdp_surface->src_data.size);
	dla_debug("--src_data.batch: %d\n", sdp_surface->src_data.batch);
	dla_debug("--src_data.width: %d\n", sdp_surface->src_data.width);
	dla_debug("--src_data.height: %d\n", sdp_surface->src_data.height);
	dla_debug("--src_data.channel: %d\n", sdp_surface->src_data.channel);
	dla_debug("--src_data.line_stride: %d\n",
		  sdp_surface->src_data.line_stride);
	dla_debug("--src_data.sur_stride: %d\n",
		  sdp_surface->src_data.surf_stride);
	dla_debug("--src_data.plan_stride: %d\n",
		  sdp_surface->src_data.plane_stride);

	dla_debug("--x1_data.type: %d\n", sdp_surface->x1_data.type);
	dla_debug("--x1_data.address: %d\n", sdp_surface->x1_data.address);
	dla_debug("--x1_data.offset: %d\n", sdp_surface->x1_data.offset);
	dla_debug("--x1_data.size: %d\n", sdp_surface->x1_data.size);
	dla_debug("--x1_data.batch: %d\n", sdp_surface->x1_data.batch);
	dla_debug("--x1_data.width: %d\n", sdp_surface->x1_data.width);
	dla_debug("--x1_data.height: %d\n", sdp_surface->x1_data.height);
	dla_debug("--x1_data.channel: %d\n", sdp_surface->x1_data.channel);
	dla_debug("--x1_data.line_stride: %d\n",
		  sdp_surface->x1_data.line_stride);
	dla_debug("--x1_data.sur_stride: %d\n",
		  sdp_surface->x1_data.surf_stride);
	dla_debug("--x1_data.plan_stride: %d\n",
		  sdp_surface->x1_data.plane_stride);

	dla_debug("--x2_data.type: %d\n", sdp_surface->x2_data.type);
	dla_debug("--x2_data.address: %d\n", sdp_surface->x2_data.address);
	dla_debug("--x2_data.offset: %d\n", sdp_surface->x2_data.offset);
	dla_debug("--x2_data.size: %d\n", sdp_surface->x2_data.size);
	dla_debug("--x2_data.batch: %d\n", sdp_surface->x2_data.batch);
	dla_debug("--x2_data.width: %d\n", sdp_surface->x2_data.width);
	dla_debug("--x2_data.height: %d\n", sdp_surface->x2_data.height);
	dla_debug("--x2_data.channel: %d\n", sdp_surface->x2_data.channel);
	dla_debug("--x2_data.line_stride: %d\n",
		  sdp_surface->x2_data.line_stride);
	dla_debug("--x2_data.sur_stride: %d\n",
		  sdp_surface->x2_data.surf_stride);
	dla_debug("--x2_data.plan_stride: %d\n",
		  sdp_surface->x2_data.plane_stride);

	dla_debug("--y_data.type: %d\n", sdp_surface->y_data.type);
	dla_debug("--y_data.address: %d\n", sdp_surface->y_data.address);
	dla_debug("--y_data.offset: %d\n", sdp_surface->y_data.offset);
	dla_debug("--y_data.size: %d\n", sdp_surface->y_data.size);
	dla_debug("--y_data.batch: %d\n", sdp_surface->y_data.batch);
	dla_debug("--y_data.width: %d\n", sdp_surface->y_data.width);
	dla_debug("--y_data.height: %d\n", sdp_surface->y_data.height);
	dla_debug("--y_data.channel: %d\n", sdp_surface->y_data.channel);
	dla_debug("--y_data.line_stride: %d\n",
		  sdp_surface->y_data.line_stride);
	dla_debug("--y_data.sur_stride: %d\n", sdp_surface->y_data.surf_stride);
	dla_debug("--y_data.plan_stride: %d\n",
		  sdp_surface->y_data.plane_stride);

	dla_debug("--dst_data.type: %d\n", sdp_surface->dst_data.type);
	dla_debug("--dst_data.address: %d\n", sdp_surface->dst_data.address);
	dla_debug("--dst_data.offset: %d\n", sdp_surface->dst_data.offset);
	dla_debug("--dst_data.size: %d\n", sdp_surface->dst_data.size);
	dla_debug("--dst_data.batch: %d\n", sdp_surface->dst_data.batch);
	dla_debug("--dst_data.width: %d\n", sdp_surface->dst_data.width);
	dla_debug("--dst_data.height: %d\n", sdp_surface->dst_data.height);
	dla_debug("--dst_data.channel: %d\n", sdp_surface->dst_data.channel);
	dla_debug("--dst_data.line_stride: %d\n",
		  sdp_surface->dst_data.line_stride);
	dla_debug("--dst_data.sur_stride: %d\n",
		  sdp_surface->dst_data.surf_stride);
	dla_debug("--dst_data.plan_stride: %d\n",
		  sdp_surface->dst_data.plane_stride);
#endif
	if (sdp_surface->dst_data.type != DLA_MEM_HW) {
		sdp_post_drp_handle(sdp_prog, &sdp_op->post_drp_op);
	}

	sdp_tensor->fly = fly;
	sdp_tensor->out_dma_ena = out_dma_ena;
	if (!fly) {
		if (sdp_tensor->src_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(sdp_tensor->no_fly_src_addr == -1ull)) {
				dla_error("%s %d no_fly_src_addr -1\n",
					  __func__, __LINE__);
				goto exit;
			} else {
				src_addr = sdp_tensor->no_fly_src_addr;
			}
		} else {
			//store offset to register when io_tensor_idx is effective.
			src_addr = sdp_surface->src_data.offset;
			sdp_prog->input_tensor_idx =
				sdp_tensor->src_is_io_tensor;
		}
	}

	if (out_dma_ena) {
		if (sdp_tensor->dst_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(sdp_tensor->out_dma_dst_addr == -1ull)) {
				dla_error(
					"%s %d out_dma_dst_addr -1, try read again\n",
					__func__, __LINE__);
				goto exit;
			} else {
				dst_addr = sdp_tensor->out_dma_dst_addr;
			}
			if (sdp_surface->dst_data.type != DLA_MEM_HW) {
				sdp_post_drp_set_dst_addr(sdp_prog, dst_addr);
			}

		} else {
			dst_addr = sdp_surface->dst_data.offset;
			sdp_prog->output_tensor_idx =
				sdp_tensor->dst_is_io_tensor;
		}
	}

	x1_rdma_ena &= (x1_op->mode != SDP_OP_PER_LAYER);
	x2_rdma_ena &= (x2_op->mode != SDP_OP_PER_LAYER);
	y_rdma_ena &= (y_op->mode != SDP_OP_PER_LAYER);

	sdp_tensor->x1_rdma_ena = x1_rdma_ena;
	sdp_tensor->x2_rdma_ena = x2_rdma_ena;
	sdp_tensor->y_rdma_ena = y_rdma_ena;

	if (x1_rdma_ena) {
		if (sdp_tensor->x1_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(sdp_tensor->x1_addr == -1ull)) {
				dla_error("%s %d x1_addr -1\n", __func__,
					  __LINE__);
				goto exit;
			} else {
				x1_addr = sdp_tensor->x1_addr;
			}
		} else {
			x1_addr = sdp_surface->x1_data.offset;
			sdp_prog->x1_tensor_idx = sdp_tensor->x1_is_io_tensor;
		}
	}

	if (x2_rdma_ena) {
		if (sdp_tensor->x2_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(sdp_tensor->x2_addr == -1ull)) {
				dla_error("%s %d x2_addr -1\n", __func__,
					  __LINE__);
				goto exit;
			} else {
				x2_addr = sdp_tensor->x2_addr;
			}
		} else {
			x2_addr = sdp_surface->x2_data.offset;
			sdp_prog->x2_tensor_idx = sdp_tensor->x2_is_io_tensor;
		}
	}

	if (y_rdma_ena) {
		if (sdp_tensor->y_is_io_tensor == invalid_tensor_idx) {
			if (unlikely(sdp_tensor->y_addr == -1ull)) {
				dla_error("%s %d y_addr -1\n", __func__,
					  __LINE__);
				goto exit;
			} else {
				y_addr = sdp_tensor->y_addr;
			}
		} else {
			y_addr = sdp_surface->y_data.offset;
			sdp_prog->y_tensor_idx = sdp_tensor->y_is_io_tensor;
		}
	}

	reg = (map_fly[0] << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, FLYING_MODE));
	sdp_prog->sdp_rdma_reg[SDP_RDMA_D_FEATURE_MODE_CFG] = reg;
	npu_set_u64_bit(SDP_RDMA_D_FEATURE_MODE_CFG,
			&sdp_prog->u84_rdma_bitmap);

	reg = (map_ena[1] << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DISABLE));
	sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BRDMA_CFG] = reg;
	npu_set_u64_bit(SDP_RDMA_D_BRDMA_CFG, &sdp_prog->u84_rdma_bitmap);

	reg = (map_ena[1] << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_DISABLE));
	sdp_prog->sdp_rdma_reg[SDP_RDMA_D_NRDMA_CFG] = reg;
	npu_set_u64_bit(SDP_RDMA_D_NRDMA_CFG, &sdp_prog->u84_rdma_bitmap);

	reg = (map_ena[1] << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_DISABLE));
	sdp_prog->sdp_rdma_reg[SDP_RDMA_D_ERDMA_CFG] = reg;
	npu_set_u64_bit(SDP_RDMA_D_ERDMA_CFG, &sdp_prog->u84_rdma_bitmap);

	reg = (map_fly[fly]
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, FLYING_MODE)) |
	      (map_wg[sdp_op->conv_mode == CONV_MODE_WINOGRAD]
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, WINOGRAD)) |
	      (map_precision[sdp_op->src_precision]
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, IN_PRECISION)) |
	      (map_precision[sdp_op->dst_precision]
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, OUT_PRECISION)) |
	      (map_proc_precision[sdp_op->dst_precision][sdp_op->src_precision]
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, PROC_PRECISION)) |
	      ((sdp_op->batch_num - 1)
	       << SHIFT(SDP_RDMA_D_FEATURE_MODE_CFG_0, BATCH_NUMBER));

	sdp_prog->sdp_rdma_reg[SDP_RDMA_D_FEATURE_MODE_CFG] = reg;
	npu_set_u64_bit(SDP_RDMA_D_FEATURE_MODE_CFG,
			&sdp_prog->u84_rdma_bitmap);
	if (rdma) {
		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_DATA_CUBE_WIDTH] =
			sdp_surface->src_data.width - 1;
		npu_set_u64_bit(SDP_RDMA_D_DATA_CUBE_WIDTH,
				&sdp_prog->u84_rdma_bitmap);
		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_DATA_CUBE_HEIGHT] =
			sdp_surface->src_data.height - 1;
		npu_set_u64_bit(SDP_RDMA_D_DATA_CUBE_HEIGHT,
				&sdp_prog->u84_rdma_bitmap);

		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_DATA_CUBE_CHANNEL] =
			sdp_surface->src_data.channel - 1;
		npu_set_u64_bit(SDP_RDMA_D_DATA_CUBE_CHANNEL,
				&sdp_prog->u84_rdma_bitmap);

		/* config SDP source info */
		if (!fly) {
			high = HIGH32BITS(src_addr);
			low = LOW32BITS(src_addr);
			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_SRC_BASE_ADDR_LOW] =
				low;
			npu_set_u64_bit(SDP_RDMA_D_SRC_BASE_ADDR_LOW,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_SRC_BASE_ADDR_HIGH] =
				high;
			npu_set_u64_bit(SDP_RDMA_D_SRC_BASE_ADDR_HIGH,
					&sdp_prog->u84_rdma_bitmap);
			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_SRC_LINE_STRIDE] =
				sdp_surface->src_data.line_stride;
			npu_set_u64_bit(SDP_RDMA_D_SRC_LINE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_SRC_SURFACE_STRIDE] =
				sdp_surface->src_data.surf_stride;
			npu_set_u64_bit(SDP_RDMA_D_SRC_SURFACE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);
			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_SRC_DMA_CFG] =
				map_ram_type[sdp_surface->src_data.type];

			npu_set_u64_bit(SDP_RDMA_D_SRC_DMA_CFG,
					&sdp_prog->u84_rdma_bitmap);
		}

		/* config x1 source info */
		reg = (map_ena[x1_rdma_ena]
		       << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DISABLE)) |
		      (map_op_type[x1_op->type]
		       << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_USE)) |
		      (map_element_size[x1_op->precision]
		       << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_SIZE)) |
		      (map_op_mode[x1_op->mode]
		       << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_DATA_MODE)) |
		      (map_ram_type[sdp_surface->x1_data.type]
		       << SHIFT(SDP_RDMA_D_BRDMA_CFG_0, BRDMA_RAM_TYPE));
		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BRDMA_CFG] = reg;
		npu_set_u64_bit(SDP_RDMA_D_BRDMA_CFG,
				&sdp_prog->u84_rdma_bitmap);
		if (x1_rdma_ena) {
			high = HIGH32BITS(x1_addr);
			low = LOW32BITS(x1_addr);
			dla_debug("sdp set x1 addr: 0x%x, 0x%x\n", high, low);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BS_BASE_ADDR_LOW] =
				low;

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BS_BASE_ADDR_HIGH] =
				high;

			npu_set_u64_bit(SDP_RDMA_D_BS_BASE_ADDR_LOW,
					&sdp_prog->u84_rdma_bitmap);
			npu_set_u64_bit(SDP_RDMA_D_BS_BASE_ADDR_HIGH,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BS_LINE_STRIDE] =
				sdp_surface->x1_data.line_stride;

			npu_set_u64_bit(SDP_RDMA_D_BS_LINE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BS_SURFACE_STRIDE] =
				sdp_surface->x1_data.surf_stride;
			npu_set_u64_bit(SDP_RDMA_D_BS_SURFACE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);
		}

		/* config x2 source info */
		reg = (map_ena[x2_rdma_ena]
		       << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_DISABLE)) |
		      (map_op_type[x2_op->type]
		       << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_DATA_USE)) |
		      (map_element_size[x2_op->precision]
		       << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_DATA_SIZE)) |
		      (map_op_mode[x2_op->mode]
		       << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_DATA_MODE)) |
		      (map_ram_type[sdp_surface->x2_data.type]
		       << SHIFT(SDP_RDMA_D_NRDMA_CFG_0, NRDMA_RAM_TYPE));

		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_NRDMA_CFG] = reg;
		npu_set_u64_bit(SDP_RDMA_D_NRDMA_CFG,
				&sdp_prog->u84_rdma_bitmap);
		if (x2_rdma_ena) {
			high = HIGH32BITS(x2_addr);
			low = LOW32BITS(x2_addr);
			dla_debug("sdp set x2 addr: 0x%x, 0x%x\n", high, low);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BN_BASE_ADDR_LOW] =
				low;

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BN_BASE_ADDR_HIGH] =
				high;

			npu_set_u64_bit(SDP_RDMA_D_BN_BASE_ADDR_LOW,
					&sdp_prog->u84_rdma_bitmap);
			npu_set_u64_bit(SDP_RDMA_D_BN_BASE_ADDR_HIGH,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BN_LINE_STRIDE] =
				sdp_surface->x2_data.line_stride;
			npu_set_u64_bit(SDP_RDMA_D_BN_LINE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_BN_SURFACE_STRIDE] =
				sdp_surface->x2_data.surf_stride;

			npu_set_u64_bit(SDP_RDMA_D_BN_SURFACE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);
		}

		/* config y source info */
		reg = (map_ena[y_rdma_ena]
		       << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_DISABLE)) |
		      (map_op_type[y_op->type]
		       << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_DATA_USE)) |
		      (map_element_size[y_op->precision]
		       << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_DATA_SIZE)) |
		      (map_op_mode[y_op->mode]
		       << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_DATA_MODE)) |
		      (map_ram_type[sdp_surface->y_data.type]
		       << SHIFT(SDP_RDMA_D_ERDMA_CFG_0, ERDMA_RAM_TYPE));

		sdp_prog->sdp_rdma_reg[SDP_RDMA_D_ERDMA_CFG] = reg;
		npu_set_u64_bit(SDP_RDMA_D_ERDMA_CFG,
				&sdp_prog->u84_rdma_bitmap);

		if (y_rdma_ena) {
			high = HIGH32BITS(y_addr);
			low = LOW32BITS(y_addr);
			dla_debug("sdp set y addr: 0x%x, 0x%x\n", high, low);
			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_EW_BASE_ADDR_LOW] =
				low;
			npu_set_u64_bit(SDP_RDMA_D_EW_BASE_ADDR_LOW,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_EW_BASE_ADDR_HIGH] =
				high;

			npu_set_u64_bit(SDP_RDMA_D_EW_BASE_ADDR_HIGH,
					&sdp_prog->u84_rdma_bitmap);
			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_EW_LINE_STRIDE] =
				sdp_surface->y_data.line_stride;
			npu_set_u64_bit(SDP_RDMA_D_EW_LINE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);

			sdp_prog->sdp_rdma_reg[SDP_RDMA_D_EW_SURFACE_STRIDE] =
				sdp_surface->y_data.surf_stride;
			npu_set_u64_bit(SDP_RDMA_D_EW_SURFACE_STRIDE,
					&sdp_prog->u84_rdma_bitmap);
		}
	}

	if (sdp_op->lut_index >= 0) {
		ret = dla_get_dma_cube_address(
			executor->driver_context, executor->mem_handles,
			executor->network->lut_data_index,
			sdp_op->lut_index * sizeof(lut_dev_t),
			&sdp->npu_info.lut_address, &not_used);
		if (ret) {
			dla_error("Failed to read lut_address, ret=%d\n", ret);
			return -1;
		}

		if (sdp_op->lut_index == 0) {
			executor->lut_base_iova = sdp->npu_info.lut_address;
		}

		BUG_ON(sdp->npu_info.lut_address == 0);
		dla_debug("sdp->npu_info.lut_address=0x%llx, not_used=%u\n",
			  sdp->npu_info.lut_address, not_used);
		if (executor->recent_lut_iova == sdp->npu_info.lut_address) {
			/* inform E31 to reuse recent lut */
			sdp->npu_info.lut_address = 0;
		} else {
			executor->recent_lut_iova = sdp->npu_info.lut_address;
		}
	}

	sdp_prog->reg[D_DATA_CUBE_WIDTH] = sdp_surface->src_data.width - 1;
	sdp_prog->reg[D_DATA_CUBE_HEIGHT] = sdp_surface->src_data.height - 1;
	sdp_prog->reg[D_DATA_CUBE_CHANNEL] = sdp_surface->src_data.channel - 1;
	npu_set_u64_bit(D_DATA_CUBE_WIDTH, &sdp_prog->u84_bitmap);
	npu_set_u64_bit(D_DATA_CUBE_HEIGHT, &sdp_prog->u84_bitmap);
	npu_set_u64_bit(D_DATA_CUBE_CHANNEL, &sdp_prog->u84_bitmap);
	if (out_dma_ena) {
		high = HIGH32BITS(dst_addr);
		low = LOW32BITS(dst_addr);
		sdp_prog->reg[D_DST_BASE_ADDR_HIGH] = high;
		sdp_prog->reg[D_DST_BASE_ADDR_LOW] = low;
		npu_set_u64_bit(D_DST_BASE_ADDR_HIGH, &sdp_prog->u84_bitmap);
		npu_set_u64_bit(D_DST_BASE_ADDR_LOW, &sdp_prog->u84_bitmap);

		sdp_prog->reg[D_DST_LINE_STRIDE] =
			sdp_surface->dst_data.line_stride;
		npu_set_u64_bit(D_DST_LINE_STRIDE, &sdp_prog->u84_bitmap);
		sdp_prog->reg[D_DST_SURFACE_STRIDE] =
			sdp_surface->dst_data.surf_stride;

		npu_set_u64_bit(D_DST_SURFACE_STRIDE, &sdp_prog->u84_bitmap);
	}

	/* Config BS module */
	reg = (map_bypass[x1_op->enable]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_BYPASS)) |
	      (map_bypass[x1_op->type != SDP_OP_MUL &&
			  x1_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_ALU_BYPASS)) |
	      (map_alu_op[x1_op->alu_type]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_ALU_ALGO)) |
	      (map_bypass[x1_op->type != SDP_OP_ADD &&
			  x1_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_MUL_BYPASS)) |
	      (map_prelu[x1_op->act == ACTIVATION_PRELU]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_MUL_PRELU)) |
	      (map_bypass[x1_op->act == ACTIVATION_RELU]
	       << SHIFT(SDP_D_DP_BS_CFG_0, BS_RELU_BYPASS));
	sdp_prog->reg[D_DP_BS_CFG] = reg;
	npu_set_u64_bit(D_DP_BS_CFG, &sdp_prog->u84_bitmap);

	if (x1_op->enable) {
		if (x1_op->type == SDP_OP_ADD || x1_op->type == SDP_OP_BOTH) {
			reg = (map_alu_src[x1_op->mode == SDP_OP_PER_LAYER]
			       << SHIFT(SDP_D_DP_BS_ALU_CFG_0, BS_ALU_SRC)) |
			      (x1_op->shift_value
			       << SHIFT(SDP_D_DP_BS_ALU_CFG_0,
					BS_ALU_SHIFT_VALUE));

			sdp_prog->reg[D_DP_BS_ALU_CFG] = reg;
			npu_set_u64_bit(D_DP_BS_ALU_CFG, &sdp_prog->u84_bitmap);
		}

		if (x1_op->mode == SDP_OP_PER_LAYER) {
			sdp_prog->reg[D_DP_BS_ALU_SRC_VALUE] =
				x1_op->alu_operand;
			npu_set_u64_bit(D_DP_BS_ALU_SRC_VALUE,
					&sdp_prog->u84_bitmap);
			sdp_prog->reg[D_DP_BS_MUL_SRC_VALUE] =
				x1_op->mul_operand;
			npu_set_u64_bit(D_DP_BS_MUL_SRC_VALUE,
					&sdp_prog->u84_bitmap);
		}

		/**
		 * MUL truncate will take effect no matter
		 * MUL is bypassed or not
		 */
		reg = (map_alu_src[x1_op->mode == SDP_OP_PER_LAYER]
		       << SHIFT(SDP_D_DP_BS_MUL_CFG_0, BS_MUL_SRC)) |
		      (x1_op->truncate
		       << SHIFT(SDP_D_DP_BS_MUL_CFG_0, BS_MUL_SHIFT_VALUE));
		sdp_prog->reg[D_DP_BS_MUL_CFG] = reg;
		npu_set_u64_bit(D_DP_BS_MUL_CFG, &sdp_prog->u84_bitmap);
	}

	/* Config BN module */
	reg = (map_bypass[x2_op->enable]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_BYPASS)) |
	      (map_bypass[x2_op->type != SDP_OP_MUL &&
			  x2_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_ALU_BYPASS)) |
	      (map_alu_op[x2_op->alu_type]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_ALU_ALGO)) |
	      (map_bypass[x2_op->type != SDP_OP_ADD &&
			  x2_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_MUL_BYPASS)) |
	      (map_prelu[x2_op->act == ACTIVATION_PRELU]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_MUL_PRELU)) |
	      (map_bypass[x2_op->act == ACTIVATION_RELU]
	       << SHIFT(SDP_D_DP_BN_CFG_0, BN_RELU_BYPASS));
	sdp_prog->reg[D_DP_BN_CFG] = reg;
	npu_set_u64_bit(D_DP_BN_CFG, &sdp_prog->u84_bitmap);

	if (x2_op->enable) {
		if (x2_op->type == SDP_OP_ADD || x2_op->type == SDP_OP_BOTH) {
			reg = (map_alu_src[x2_op->mode == SDP_OP_PER_LAYER]
			       << SHIFT(SDP_D_DP_BN_ALU_CFG_0, BN_ALU_SRC)) |
			      (x2_op->shift_value
			       << SHIFT(SDP_D_DP_BN_ALU_CFG_0,
					BN_ALU_SHIFT_VALUE));
			sdp_prog->reg[D_DP_BN_ALU_CFG] = reg;
			npu_set_u64_bit(D_DP_BN_ALU_CFG, &sdp_prog->u84_bitmap);
		}

		if (x2_op->mode == SDP_OP_PER_LAYER) {
			sdp_prog->reg[D_DP_BN_ALU_SRC_VALUE] =
				x2_op->alu_operand;
			npu_set_u64_bit(D_DP_BN_ALU_SRC_VALUE,
					&sdp_prog->u84_bitmap);

			sdp_prog->reg[D_DP_BN_MUL_SRC_VALUE] =
				x2_op->mul_operand;
			npu_set_u64_bit(D_DP_BN_MUL_SRC_VALUE,
					&sdp_prog->u84_bitmap);
		}

		reg = (map_alu_src[x2_op->mode == SDP_OP_PER_LAYER]
		       << SHIFT(SDP_D_DP_BN_MUL_CFG_0, BN_MUL_SRC)) |
		      (x2_op->truncate
		       << SHIFT(SDP_D_DP_BN_MUL_CFG_0, BN_MUL_SHIFT_VALUE));
		sdp_prog->reg[D_DP_BN_MUL_CFG] = reg;
		npu_set_u64_bit(D_DP_BN_MUL_CFG, &sdp_prog->u84_bitmap);
	}

	/* Config EW module */
	reg = (map_bypass[y_op->enable]
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_BYPASS)) |
	      (map_bypass[y_op->type != SDP_OP_MUL && y_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_ALU_BYPASS)) |
	      (map_alu_op[y_op->alu_type]
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_ALU_ALGO)) |
	      (map_bypass[y_op->type != SDP_OP_ADD && y_op->type != SDP_OP_NONE]
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_MUL_BYPASS)) |
	      ((map_prelu[y_op->act == ACTIVATION_PRELU])
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_MUL_PRELU)) |
	      (map_bypass[y_op->act == ACTIVATION_LUT]
	       << SHIFT(SDP_D_DP_EW_CFG_0, EW_LUT_BYPASS));
	sdp_prog->reg[D_DP_EW_CFG] = reg;
	npu_set_u64_bit(D_DP_EW_CFG, &sdp_prog->u84_bitmap);

	if (y_op->enable) {
		if (y_op->type == SDP_OP_ADD || y_op->type == SDP_OP_BOTH) {
			reg = (map_alu_src[y_op->mode == SDP_OP_PER_LAYER]
			       << SHIFT(SDP_D_DP_EW_ALU_CFG_0, EW_ALU_SRC)) |
			      (map_bypass[y_op->cvt.alu_cvt.enable]
			       << SHIFT(SDP_D_DP_EW_ALU_CFG_0,
					EW_ALU_CVT_BYPASS));
			sdp_prog->reg[D_DP_EW_ALU_CFG] = reg;
			npu_set_u64_bit(D_DP_EW_ALU_CFG, &sdp_prog->u84_bitmap);

			if (y_op->mode == SDP_OP_PER_LAYER) {
				sdp_prog->reg[D_DP_EW_ALU_SRC_VALUE] =
					y_op->alu_operand;
				npu_set_u64_bit(D_DP_EW_ALU_SRC_VALUE,
						&sdp_prog->u84_bitmap);

			} else {
				sdp_prog->reg[D_DP_EW_ALU_CVT_OFFSET_VALUE] =
					y_op->cvt.alu_cvt.offset;
				npu_set_u64_bit(D_DP_EW_ALU_CVT_OFFSET_VALUE,
						&sdp_prog->u84_bitmap);

				sdp_prog->reg[D_DP_EW_ALU_CVT_SCALE_VALUE] =
					y_op->cvt.alu_cvt.scale;
				npu_set_u64_bit(D_DP_EW_ALU_CVT_SCALE_VALUE,
						&sdp_prog->u84_bitmap);

				sdp_prog->reg[D_DP_EW_ALU_CVT_TRUNCATE_VALUE] =
					y_op->cvt.alu_cvt.truncate;
				npu_set_u64_bit(D_DP_EW_ALU_CVT_TRUNCATE_VALUE,
						&sdp_prog->u84_bitmap);
			}
		}

		if (y_op->type == SDP_OP_MUL || y_op->type == SDP_OP_BOTH) {
			reg = (map_alu_src[y_op->mode == SDP_OP_PER_LAYER]
			       << SHIFT(SDP_D_DP_EW_MUL_CFG_0, EW_MUL_SRC)) |
			      (map_bypass[y_op->cvt.mul_cvt.enable]
			       << SHIFT(SDP_D_DP_EW_MUL_CFG_0,
					EW_MUL_CVT_BYPASS));
			sdp_prog->reg[D_DP_EW_MUL_CFG] = reg;
			npu_set_u64_bit(D_DP_EW_MUL_CFG, &sdp_prog->u84_bitmap);

			if (y_op->mode == SDP_OP_PER_LAYER) {
				sdp_prog->reg[D_DP_EW_MUL_SRC_VALUE] =
					y_op->mul_operand;
				npu_set_u64_bit(D_DP_EW_MUL_SRC_VALUE,
						&sdp_prog->u84_bitmap);

			} else {
				sdp_prog->reg[D_DP_EW_MUL_CVT_OFFSET_VALUE] =
					y_op->cvt.mul_cvt.offset;
				npu_set_u64_bit(D_DP_EW_MUL_CVT_OFFSET_VALUE,
						&sdp_prog->u84_bitmap);

				sdp_prog->reg[D_DP_EW_MUL_CVT_SCALE_VALUE] =
					y_op->cvt.mul_cvt.scale;
				npu_set_u64_bit(D_DP_EW_MUL_CVT_SCALE_VALUE,
						&sdp_prog->u84_bitmap);

				sdp_prog->reg[D_DP_EW_MUL_CVT_TRUNCATE_VALUE] =
					y_op->cvt.mul_cvt.truncate;
				npu_set_u64_bit(D_DP_EW_MUL_CVT_TRUNCATE_VALUE,
						&sdp_prog->u84_bitmap);
			}
		}

		sdp_prog->reg[D_DP_EW_TRUNCATE_VALUE] = y_op->truncate;
		npu_set_u64_bit(D_DP_EW_TRUNCATE_VALUE, &sdp_prog->u84_bitmap);
	}

	reg = (map_fly[sdp_surface->src_data.type == DLA_MEM_HW]
	       << SHIFT(SDP_D_FEATURE_MODE_CFG_0, FLYING_MODE)) |
	      (map_dst[sdp_surface->dst_data.type == DLA_MEM_HW]
	       << SHIFT(SDP_D_FEATURE_MODE_CFG_0, OUTPUT_DST)) |
	      (map_wg[sdp_op->conv_mode == CONV_MODE_WINOGRAD]
	       << SHIFT(SDP_D_FEATURE_MODE_CFG_0, WINOGRAD)) |
	      ((sdp_op->batch_num - 1)
	       << SHIFT(SDP_D_FEATURE_MODE_CFG_0, BATCH_NUMBER));
	sdp_prog->reg[D_FEATURE_MODE_CFG] = reg;
	npu_set_u64_bit(D_FEATURE_MODE_CFG, &sdp_prog->u84_bitmap);

	if (sdp_op->batch_num > 1) {
		sdp_prog->reg[D_DST_BATCH_STRIDE] = sdp_op->batch_stride;
		npu_set_u64_bit(D_DST_BATCH_STRIDE, &sdp_prog->u84_bitmap);
	}

	reg = (map_proc_precision[sdp_op->dst_precision][sdp_op->src_precision]
	       << SHIFT(SDP_D_DATA_FORMAT_0, PROC_PRECISION)) |
	      (map_precision[sdp_op->dst_precision]
	       << SHIFT(SDP_D_DATA_FORMAT_0, OUT_PRECISION));
	sdp_prog->reg[D_DATA_FORMAT] = reg;
	sdp_prog->reg[D_CVT_OFFSET] = sdp_op->out_cvt.offset;
	sdp_prog->reg[D_CVT_SCALE] = sdp_op->out_cvt.scale;
	sdp_prog->reg[D_CVT_SHIFT] = sdp_op->out_cvt.truncate;
	npu_set_u64_bit(D_DATA_FORMAT, &sdp_prog->u84_bitmap);
	npu_set_u64_bit(D_CVT_OFFSET, &sdp_prog->u84_bitmap);
	npu_set_u64_bit(D_CVT_SCALE, &sdp_prog->u84_bitmap);
	npu_set_u64_bit(D_CVT_SHIFT, &sdp_prog->u84_bitmap);

	if (sdp_surface->dst_data.type != DLA_MEM_HW) {
		sdp_prog->sdp_drp_reg[DRP_D_REG_OP_EN_TRIG] = 1;
		npu_set_u64_bit(DRP_D_REG_OP_EN_TRIG,
				&sdp_prog->u84_drp_bitmap);
	}

exit:
	return ret;
}

int dla_sdp_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			      u16 op_idx,
			      union dla_operation_container *operation_desc,
			      union dla_surface_container *surface_desc)
{
	int32_t ret;

	ret = processor_sdp_program(executor, rdma, idx, operation_desc,
				    surface_desc);
	if (ret)
		goto exit;

exit:
	return ret;
}
