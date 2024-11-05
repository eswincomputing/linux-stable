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

#include <asm/errno.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <asm-generic/barrier.h>
#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "nvdla_linux.h"
#include "dla_log.h"
#include "nvdla_interface.h"
#include "opendla_initial.h"
#include "conv.h"
#include "debug.h"
#include "internal_interface.h"
#include "hetero_common.h"
#include "dla_buffer.h"

int dla_conv_rdma_check(struct dla_processor_group *group,
			union dla_operation_container *op,
			union dla_surface_container *surface)
{
	if (group) {
		group->is_rdma_needed = 0;
	}
	return 0;
}

int conv_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	return 0;
}

#ifdef CONV_DUMP
static inline void dump_conv_program_t(conv_program_t *conv_prog)
{
	dla_info("==========npu driver dump_conv_program_t================\n");
	dla_info("ifmap_base_addr =0x%llx \n", conv_prog->ifmap_base_addr);
	dla_info("weight_base_addr=0x%llx \n", conv_prog->weight_base_addr);
	dla_info("ofmap_base_addr =0x%llx \n", conv_prog->ofmap_base_addr);
	dla_info("used_num_pec_row=%u \n", conv_prog->used_num_pec_row);
	dla_info("used_num_pec_column=%u \n", conv_prog->used_num_pec_column);
	dla_info("input0_io_index=%u \n", conv_prog->input0_io_index);
	dla_info("output0_io_index=%u \n", conv_prog->output0_io_index);
}

static void dump_rdma_dev_com_inf_t(rdma_dev_com_inf_t *rdma_com)
{
	dla_info("N %d Ch0 %d(C0xG0 also CfxGfxCMfxGMf) H %d W %d\n",
		 rdma_com->N, rdma_com->Ch0, rdma_com->H, rdma_com->W);
	dla_info(
		"Che %d(G/G0 * C/C0 with div_up) total-channel %d(may padding0)\n",
		rdma_com->Che, rdma_com->Che * rdma_com->Ch0);
	dla_info("ifmap_type_fmt %d stride_h %d stride_w %d\n",
		 rdma_com->ifmap_type_fmt, rdma_com->stride_h,
		 rdma_com->stride_w);
	dla_info("pad_h_t %d pad_w_l %d\n", rdma_com->pad_h_t,
		 rdma_com->pad_w_l);
	dla_info("SRAM loop G3 %d N3 %d M3 %d E4 %d F3 %d C3 %d\n",
		 rdma_com->G3, rdma_com->N3, rdma_com->M3, rdma_com->E4,
		 rdma_com->F3, rdma_com->C3);
	dla_info("PEC loop G2 %d N2 %d C2 %d E3 %d R3 %d M2 %d\n", rdma_com->G2,
		 rdma_com->N2, rdma_com->C2, rdma_com->E3, rdma_com->R3,
		 rdma_com->M2);
	dla_info(
		"PEC space G1_C1(G1xC1) %d N1 %d M1 %d E2 %d R2 (constantly 1)\n",
		rdma_com->G1_C1, rdma_com->N1, rdma_com->M1, rdma_com->E2);
	dla_info("SPAD loop F0 %d S %d (GMF-CMF in Ch0)\n", rdma_com->F0,
		 rdma_com->S);
	dla_info("PE space E1 %d R1 %d Cv %d\n", rdma_com->E1, rdma_com->R1,
		 rdma_com->Cv);
	dla_info("More: E0 %d R %d(total r)\n", rdma_com->E0, rdma_com->R);
}
static void dump_rdma_dev_master_inf_t(rdma_dev_master_inf_t *rdma_mst)
{
	int i;
	u32 rdma_and_wig_bitmap = rdma_mst->rdma_and_wig_bitmap;
	u32 *reg = rdma_mst->regs;

	dla_info("rdma_and_wig_bitmap 0x%x(indicate valid reg)\n",
		 rdma_and_wig_bitmap);
	for (i = 0; i < 32; i++) {
		if ((1 << i) & rdma_and_wig_bitmap) {
			dla_info("    rdma IFM_WT_CR + %d 0x%x\n", i, *reg++);
		}
	}
}
static void dump_pec_dev_master_inf_t(pec_dev_master_inf_t *pec_mst)
{
	u32 *reg = pec_mst->regs;
	union pre_drp_regs_t *pre_drp = &pec_mst->pre_drp_regs;
	int i;

	dla_info("struct_len %d\n", pec_mst->struct_len);
	dla_info("pec_active_bitmap 0x%x\n", pec_mst->pec_active_bitmap);
	dla_info("reg_bcast_bitmap 0x%x\n", pec_mst->reg_bcast_bitmap);
	for (i = 0; i < 32; i++) {
		if ((1 << i) & pec_mst->reg_bcast_bitmap) {
			dla_info("    pec_spad_param0 + %d 0x%x(broadcast)\n",
				 i, *reg++);
		}
	}
	dla_info("predrp_ctrl         0x%x", pre_drp->predrp_ctrl);
	dla_info("n2_stride           0x%x", pre_drp->n2_stride);
	dla_info("g2_stride           0x%x", pre_drp->g2_stride);
	dla_info("e3_stride           0x%x", pre_drp->e3_stride);
	dla_info("m2_stride           0x%x", pre_drp->m2_stride);
	dla_info("m_stride            0x%x", pre_drp->m_stride);
	dla_info("g3_threshold	       0x%x", pre_drp->g3_threshold);
	dla_info("n3_threshold	       0x%x", pre_drp->n3_threshold);
	dla_info("m3_threshold	       0x%x", pre_drp->m3_threshold);
	dla_info("e4_threshold	       0x%x", pre_drp->e4_threshold);
	dla_info("f3_threshold	       0x%x", pre_drp->f3_threshold);
	dla_info("pe_num              0x%x", pre_drp->pe_num);
	dla_info("predrp_size_glb     0x%x", pre_drp->predrp_size_glb);
	dla_info("reshape_ctrl	       0x%x", pre_drp->reshape_ctrl);
	dla_info("g_stride_glb	       0x%x", pre_drp->g_stride_glb);
	dla_info("n_stride_glb	       0x%x", pre_drp->n_stride_glb);
	dla_info("e_stride_glb	       0x%x", pre_drp->e_stride_glb);
	dla_info("m_stride_glb	       0x%x", pre_drp->m_stride_glb);
	dla_info("n_stride_sram       0x%x", pre_drp->n_stride_sram);
	dla_info("h_stride_sram       0x%x", pre_drp->h_stride_sram);
	dla_info("c_stride_sram       0x%x", pre_drp->c_stride_sram);
	dla_info("imap_para_l         0x%x", pre_drp->imap_para_l);
	dla_info("omap_para_rsp_w     0x%x", pre_drp->omap_para_rsp_w);
	dla_info("layer_para_l	       0x%x", pre_drp->layer_para_l);
	dla_info("layer_para_m	       0x%x", pre_drp->layer_para_m);
	dla_info("layer_para_h	       0x%x", pre_drp->layer_para_h);
	dla_info("glb_para_l          0x%x", pre_drp->glb_para_l);
	dla_info("glb_para_h          0x%x", pre_drp->glb_para_h);
	dla_info("omap_para_l         0x%x", pre_drp->omap_para_l);
	dla_info("reshape_size_glb    0x%x", pre_drp->reshape_size_glb);
	dla_info("precision_ctrl_l    0x%x", pre_drp->precision_ctrl_l);
	dla_info("precision_ctrl_h    0x%x", pre_drp->precision_ctrl_h);
}
static void dump_rdma_dev_major_inf_t(rdma_dev_major_inf_t *rdma_maj)
{
	int i;
	union narrator_dev_desc_t *n = rdma_maj->narrator_desc;

	dla_info("struct_len %d\n", rdma_maj->struct_len);
	dla_info("rdma_en_flag %d\n", rdma_maj->rdma_en_flag);
	dla_info("is_ctrl_valid %d\n", rdma_maj->is_ctrl_valid);
	dla_info("ifmap_weight_ctrl 0x%x\n", rdma_maj->ifmap_weight_ctrl);
	dla_info("weight_narrator_bitmap 0x%x\n",
		 rdma_maj->weight_narrator_bitmap);
	for (i = 0; i < 8; i++) {
		if ((1 << i) & rdma_maj->weight_narrator_bitmap) {
			dla_info("    ch %d weight_base_addr 0x%x\n", i,
				 (n++)->weight_base_addr);
		}
	}
	dla_info("ifmap_narrator_bitmap 0x%x\n",
		 rdma_maj->ifmap_narrator_bitmap);
	for (i = 0; i < 8; i++) {
		if ((1 << i) & rdma_maj->ifmap_narrator_bitmap) {
			dla_info("    ch %d g1_c1 %d n1 %d m1 %d e2 %d\n", i,
				 n->g1_c1, n->n1, n->m1, n->e2);
		}
	}
}
static void dump_pec_dev_major_inf_t(pec_dev_major_inf_t *pec_maj)
{
	int i;
	u32 bitmap;
	u32 *reg;

	reg = pec_maj->regs;
	dla_info("struct_len %d\n", pec_maj->struct_len);
	bitmap = pec_maj->reg_mcast_bitmap;
	dla_info("reg_mcast_bitmap 0x%x\n", bitmap);
	for (i = 0; i < 8; i++) {
		if ((1 << i) & bitmap) {
			dla_info("    pec_spad_param0 + %d 0x%x\n", i, *reg++);
		}
	}
	bitmap = pec_maj->reg_ucast_bitmap;
	dla_info("reg_ucast_bitmap 0x%x\n", bitmap);
	for (i = 0; i < 8; i++) {
		if ((1 << i) & bitmap) {
			dla_info("    pec_spad_param0 + %d 0x%x\n", i, *reg++);
		}
	}
	bitmap = pec_maj->pec_en_bitmap;
	dla_info("pec_en_bitmap 0x%x\n", bitmap);
	for (i = 0; i < 8; i++) {
		if ((1 << i) & bitmap) {
			dla_info("    PEC idx %d in cur row trig enable\n", i);
		}
	}
}
#endif

int conv_prepare_io_tensor(struct win_executor *executor, int seq,
			   union dla_surface_container *surface_desc)
{
	conv_dev_hdr_t *conv_hdr = NULL;
	conv_program_t *conv_prog = NULL;
	conv_tensor_t *tensor = executor->tensor_set[IDX_CONV];
	conv_tensor_t *conv_tensor = NULL;
	struct npu_conv_surface_desc *surface;
	u8 *prog_start_va = (u8 *)(executor->prog_data_buf_bobj[IDX_CONV]) +
			    MAX_CONV_PROG_DATA_SIZE * seq;
	// get conv header info
	if (seq == 0) {
		conv_hdr = &executor->op_prog_addrs.next_conv_hdr;
	} else {
		conv_hdr = (conv_dev_hdr_t *)(prog_start_va -
					      MAX_CONV_PROG_DATA_SIZE +
					      sizeof(rdma_dev_com_inf_t) +
					      sizeof(npu_dep_info_t));
	}

	conv_prog =
		(conv_program_t *)(prog_start_va + sizeof(rdma_dev_com_inf_t) +
				   conv_hdr->emission_len * sizeof(u32) +
				   conv_hdr->master_len * sizeof(u32));
	surface = &surface_desc->conv_surface;
	conv_tensor = &tensor[seq];

	dla_info("input0_io_index=%u, output0_io_index=%u\n",
		 conv_prog->input0_io_index, conv_prog->output0_io_index);
	if (conv_prog->input0_io_index != invalid_tensor_idx) {
		conv_prog->input0_io_offset = surface->src_data.offset;
	}
	if (conv_prog->output0_io_index != invalid_tensor_idx) {
		conv_prog->output0_io_offset = surface->dst_data.offset;
	}
	return 0;
}

static void conv_dump(const char *label, const char *buf, const u32 len)
{
	dla_info("=======%s======\n", label);
	dump_data(buf, len);
}

static int processor_conv_program(struct win_executor *executor, int rdma,
				  int tensor_idx, u16 op_idx,
				  union dla_operation_container *operation_desc,
				  union dla_surface_container *surface_desc)
{
#ifdef CONV_DUMP
	char label[32];
	rdma_dev_master_inf_t *rdma_mst;
	pec_dev_master_inf_t *pec_mst;
#endif
	u32 not_used = 0;
	u16 consumer = invalid_op_index;
	u32 ifmap_idx = (u32)invalid_tensor_idx;
	u32 ofmap_idx = (u32)invalid_tensor_idx;
	int i = 0;
	int ret = 0;
	int effect_idx = 0;
	dma_addr_t iova, dma_addr;
	u32 size, model_conv_config_size, req_size;
	char *prog_start_va = NULL;
	char *src = NULL;
	char *dst = NULL;
	void *vaddr = NULL;
	conv_dev_hdr_t *conv_hdr = NULL;
	conv_program_t *conv_prog = NULL;
	npu_dep_info_t *dep_info = NULL;
	struct dla_common_op_desc *comm_desc = NULL;
	struct npu_conv_op_desc *conv_op = NULL;
	struct npu_conv_surface_desc *conv_surface = NULL;
	conv_dev_hdr_t *addr_for_cur_conv_hdr, *addr_for_prev_conv_hdr;

	conv_op = (struct npu_conv_op_desc *)&(operation_desc->npu_conv_op);
	conv_surface = &surface_desc->conv_surface;

	vaddr = executor->prog_data_buf_bobj[IDX_CONV];
	dma_addr = executor->dma_addr[IDX_CONV];
	conv_hdr = (conv_dev_hdr_t *)(conv_op->conv_config_data);
	model_conv_config_size =
		conv_hdr->total_len * sizeof(u32) - sizeof(conv_dev_t);
	// NOTE: conv_emission_t & conv_program_t has fixed length
	req_size = model_conv_config_size + sizeof(conv_emission_t) +
		   sizeof(conv_program_t);

	// e31 requires two new structures.
	conv_hdr->total_len = req_size / sizeof(u32);
	conv_hdr->emission_len =
		(sizeof(npu_dep_info_t) + sizeof(conv_dev_t)) / sizeof(u32);
	conv_hdr->program_len =
		conv_hdr->master_len + sizeof(conv_program_t) / sizeof(u32);

	/* calculate start position for program data */
	prog_start_va = (char *)vaddr + MAX_CONV_PROG_DATA_SIZE * tensor_idx;
	iova = dma_addr + MAX_CONV_PROG_DATA_SIZE * tensor_idx;
	dla_debug("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
#if NPU_DEV_SIM == NPU_REAL_ENV
	dla_debug("req_size=%u, alloc=%u, wasted=%u. %s %s %d\n", req_size,
#else
	dla_debug("req_size=%u, alloc=%lu, wasted=%lu. %s %s %d\n", req_size,
#endif
		  MAX_CONV_PROG_DATA_SIZE, MAX_CONV_PROG_DATA_SIZE - req_size,
		  __FILE__, __FUNCTION__, __LINE__);

	dla_debug("total_len=%uDW, master_len=%uDW, major_lens[0]=%uDW, \n",
		  conv_hdr->total_len, conv_hdr->master_len,
		  conv_hdr->major_lens[0]);
	dla_debug("emission_len=%uDW, program_len=%uDW\n",
		  conv_hdr->emission_len, conv_hdr->program_len);
	dla_debug(
		"conv_hdr->used_num_pec_row=%u, conv_hdr->used_num_pec_column=%u\n",
		conv_hdr->used_num_pec_row, conv_hdr->used_num_pec_column);
	dla_debug("prog_start_va=0x%p, iova=%llu\n", prog_start_va, iova);

	if (tensor_idx == 0) {
		executor->op_prog_addrs.program_addr[IDX_CONV] = (u64)iova;
		memcpy(&executor->op_prog_addrs.next_conv_hdr, conv_hdr,
		       sizeof(conv_dev_t));
		dla_debug("first conv_program called. total_len=%u\n",
			  conv_hdr->total_len);
	}

	src = (char *)(conv_op->conv_config_data) + sizeof(conv_dev_t);
	dst = prog_start_va;

	/* common info */
	memcpy(dst, src, sizeof(rdma_dev_com_inf_t));
#ifdef CONV_DUMP
	sprintf(label, "conv%d_op%hu_common_info", tensor_idx, op_idx);
	conv_dump(label, dst, sizeof(rdma_dev_com_inf_t));
	dump_rdma_dev_com_inf_t((rdma_dev_com_inf_t *)dst);
#endif
	src += sizeof(rdma_dev_com_inf_t);
	dst += sizeof(rdma_dev_com_inf_t);

	/* setup dep_info */
	dep_info = (npu_dep_info_t *)dst;
	dep_info->current_op_idx = op_idx;

	comm_desc = (struct dla_common_op_desc *)&executor->task
			    ->common_desc[op_idx];
	dep_info->enable_op_idx = invalid_op_index;
	if (comm_desc->fused_parent.index != invalid_op_index) {
		dep_info->enable_op_idx = comm_desc->fused_parent.index;
	}
	dep_info->completion_event_bitmap = 0;

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		dep_info->completion_op_idx[effect_idx] = invalid_op_index;
		consumer = comm_desc->consumers[i].index;
		if (consumer != invalid_op_index &&
		    comm_desc->consumers[i].event == DLA_EVENT_OP_COMPLETED) {
			if (unlikely(effect_idx >= MAX_KMD_DEPCNT)) {
				dla_error(
					"op index:%d dependency count over max\n",
					op_idx);
				BUG_ON(false);
				return -1;
			}
			dep_info->completion_event_bitmap |= (1 << i);
			dep_info->completion_op_idx[effect_idx] = consumer;
			effect_idx++;
		}
	}

	addr_for_cur_conv_hdr = (conv_dev_hdr_t *)&((conv_emission_t *)dst)
					->next_convolution_header;
	memset(addr_for_cur_conv_hdr, 0, sizeof(conv_dev_hdr_t));
	if (tensor_idx > 0) {
		addr_for_prev_conv_hdr =
			(conv_dev_hdr_t *)((char *)addr_for_cur_conv_hdr -
					   MAX_CONV_PROG_DATA_SIZE);
		memcpy(addr_for_prev_conv_hdr, conv_hdr,
		       sizeof(conv_dev_hdr_t));
	}
	dst += sizeof(conv_emission_t);
#ifdef CONV_DUMP
	sprintf(label, "conv%d_op%hu_emission_core", tensor_idx, op_idx);
	conv_dump(label, dst - sizeof(conv_emission_t),
		  sizeof(conv_emission_t));
#endif

	/* for program core*/
	size = conv_hdr->master_len * sizeof(u32);
	memcpy(dst, src, size);
	src += size;
	dst += size;

	conv_prog = (conv_program_t *)dst;
	ret = read_input_address(executor, &conv_surface->src_data,
				 &conv_prog->ifmap_base_addr, &ifmap_idx);
	conv_prog->input0_io_index = (u8)ifmap_idx;
	if (ret) {
		dla_error("Failed to read ifmap_base_addr\n");
		return -1;
	}

	if (conv_surface->dst_data.type != DLA_MEM_HW) {
		ret = dla_get_dma_cube_address(
			executor->driver_context, executor->mem_handles,
			conv_surface->dst_data.address,
			conv_surface->dst_data.offset,
			(void *)&conv_prog->ofmap_base_addr, &ofmap_idx);
		conv_prog->output0_io_index = (u8)ofmap_idx;
		if (ret) {
			dla_error("Failed to read ofmap_base_addr\n");
			return -1;
		}
	} else {
		conv_prog->ofmap_base_addr = 0;
	}
	dla_debug("actual conv ifmap_idx=%u, ofmap_idx=%u\n", ifmap_idx,
		  ofmap_idx);

	ret = read_input_address(executor, &conv_surface->weight_data,
				 &conv_prog->weight_base_addr, &not_used);
	if (ret) {
		dla_error("Failed to read weight_base_addr\n");
		return -1;
	}

	conv_prog->used_num_pec_row = conv_hdr->used_num_pec_row;
	conv_prog->used_num_pec_column = conv_hdr->used_num_pec_column;
	dst += sizeof(conv_program_t);
#ifdef CONV_DUMP
	dump_conv_program_t((conv_program_t *)dst - 0x1);
	sprintf(label, "conv%d_op%hu_program_core", tensor_idx, op_idx);
	conv_dump(label,
		  dst - sizeof(conv_program_t) -
			  conv_hdr->master_len * sizeof(u32),
		  sizeof(conv_program_t) + conv_hdr->master_len * sizeof(u32));
	rdma_mst =
		(rdma_dev_master_inf_t *)(dst - sizeof(conv_program_t) -
					  conv_hdr->master_len * sizeof(u32));
	dump_rdma_dev_master_inf_t(rdma_mst);
	pec_mst =
		(pec_dev_master_inf_t
			 *)(((u8 *)rdma_mst) + sizeof(rdma_dev_master_inf_t) +
			    sizeof(u32) *
				    (hweight32(rdma_mst->rdma_and_wig_bitmap)));
	dump_pec_dev_master_inf_t(pec_mst);
#endif

	/* for major 0-7 */
	for (i = 0; i <= NUM_MAJOR_CORES; i++) {
		size = conv_hdr->major_lens[i] * sizeof(u32);
		memcpy(dst, src, size);
#ifdef CONV_DUMP
		sprintf(label, "conv%d_op%hu_major_core_%d", tensor_idx, op_idx,
			i);
		conv_dump(label, dst, size);
		dump_rdma_dev_major_inf_t((rdma_dev_major_inf_t *)dst);
		dump_pec_dev_major_inf_t(
			(pec_dev_major_inf_t *)(dst +
						((rdma_dev_major_inf_t *)dst)
								->struct_len *
							sizeof(u32)));
#endif
		src += size;
		dst += size;
	}
	conv_prepare_io_tensor(executor, tensor_idx, surface_desc);
#if NPU_DEV_SIM != NPU_REAL_ENV
	//send op_desc and op_surf_desc to e31 for simulation
	dst = prog_start_va + MAX_CONV_PROG_SIZE;
	memcpy(dst, (char *)operation_desc, sizeof(struct npu_conv_op_desc));
	dst += sizeof(struct npu_conv_op_desc);
	memcpy(dst, (char *)surface_desc, sizeof(struct npu_conv_surface_desc));
#endif

	return 0;
}

int32_t
dla_conv_prepare_prog_data(struct win_executor *executor, int rdma,
			   int tensor_idx, u16 op_idx,
			   union dla_operation_container *operation_desc,
			   union dla_surface_container *surface_desc)
{
	int32_t ret;

	ret = processor_conv_program(executor, rdma, tensor_idx, op_idx,
				     operation_desc, surface_desc);
	if (ret)
		goto exit;

exit:
	return ret;
}
