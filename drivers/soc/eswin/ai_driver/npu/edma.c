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

#include <opendla.h>
#include <dla_log.h>
#include <dla_interface.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>

#include <linux/of.h>
#include <linux/of_address.h>

#include "common.h"
#include "dla_engine_internal.h"
#include "nvdla_linux.h"
#include <linux/delay.h>
#include <linux/slab.h>

#include "edma.h"
#include "npu_top_csr.h"
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include "dla_log.h"
#include "dla_buffer.h"
#include "internal_interface.h"

union hw_common_soft_reset {
	struct {
		uint32_t soft_rst : 1;
		uint32_t clk_gate : 1;
		/* Not used */
		uint32_t clk_gate_ctl : 1;
		uint32_t rsv0 : 28;
		uint32_t halt : 1;
	};
	/* RW */
	uint32_t soft_reset;
} __attribute__((packed, aligned(4)));
union hw_common_general_parameter {
	struct {
		uint32_t aux_mode : 1;
		uint32_t rsv1 : 25;
		uint32_t burst_length : 6;
	};
	/* RW */
	uint32_t general_parameter;
} __attribute__((packed, aligned(4)));
union hw_common_status {
	struct {
		uint32_t edma_busy : 1;
		uint32_t edma_wr_busy : 1;
		uint32_t edma_rd_busy : 1;
		uint32_t edma_aux_triger_busy : 1;
		uint32_t edma_main_triger_busy : 1;
		uint32_t rsv2 : 4;
		uint32_t aux_error_entry_num : 8;
		uint32_t error_entry_num : 8;
		uint32_t error_type : 2;
		uint32_t error_cause : 4;
		uint32_t error_flag : 1;
	};
	/* RO */
	uint32_t status;
} __attribute__((packed, aligned(4)));

union hw_common_intr_status {
#define INTR_ENTRY_NUM_OFFSET (3)
#define INTR_ENTRY_NUM (0x8)
#define INTR_ENTRY_NUM_MASK (INTR_ENTRY_NUM - 1)
	struct {
		/* RC */
		uint32_t rd_transport_finish_intr : 1;
		/* RO */
		uint32_t rd_int_entry_num : 4;
		/* RC */
		uint32_t wr_transport_finish_intr : 1;
		/* RO */
		uint32_t wr_int_entry_num : 4;
		/* RC */
		uint32_t aux_rd_transport_finish_intr : 1;
		/* RO */
		uint32_t aux_rd_int_entry_num : 4;
		/* RC */
		uint32_t aux_wr_transport_finish_intr : 1;
		/* RO */
		uint32_t aux_wr_int_entry_num : 4;
		uint32_t rsv5 : 12;
	};
	uint32_t int_status;
} __attribute__((packed, aligned(4)));

union hw_common_sw_trig {
	struct {
		uint32_t rd_triger_num : 4;
		uint32_t rsv3 : 12;
		uint32_t wr_triger_num : 4;
		uint32_t rsv4 : 9;
		uint32_t prefetch_addr_reset : 1;
		uint32_t desp_ram_invalid : 1;
		uint32_t sw_triger : 1;
	};
	/* RW */
	uint32_t sw_trig;
} __attribute__((packed, aligned(4)));
union hw_common_aux_sw_trig {
	struct {
		uint32_t aux_rd_triger_num : 4;
		uint32_t aux_rsv6 : 12;
		uint32_t aux_wr_triger_num : 4;
		uint32_t aux_rsv7 : 9;
		uint32_t aux_prefetch_addr_reset : 1;
		uint32_t aux_desp_ram_invalid : 1;
		uint32_t aux_sw_triger : 1;
	};
	/* RW */
	uint32_t aux_sw_trig;
} __attribute__((packed, aligned(4)));
struct hw_common_reg {
	union hw_common_soft_reset rst;
	union hw_common_general_parameter gparam;
	union hw_common_status hw_status;
	union hw_common_sw_trig sw_trig;
	/* As hardware enginer said: Not currently available */
	union {
		struct {
			uint32_t rd_int_en_entry_bit : 16;
			uint32_t wr_int_en_entry_bit : 16;
		};
		uint32_t int_en;
	};
	/* As hardware enginer said: Not currently available */
	union {
		struct {
			uint32_t rd_int_mask_entry_bit : 16;
			uint32_t wr_int_mask_entry_bit : 16;
		};
		uint32_t int_mask;
	};
	union hw_common_intr_status intr_status;
	/* 4K Byte align needed */
	uint32_t prefetch_addr_low;
	uint32_t prefetch_addr_high;
	union {
		struct {
			/* Need align with despRAM size */
			uint32_t prefetch_total_depth : 16; /* RO */
			uint32_t prefetch_current_addr_offset : 16;
		};
		uint32_t prefetch_parameter;
	};
	uint32_t err_addr_high;
	uint32_t err_addr_low;
	/* 64 Byte align needed */
	uint32_t aux_prefetch_addr_low;
	uint32_t aux_prefetch_addr_high;
	union {
		struct {
			/* Need align with despRAM size */
			uint32_t aux_prefetch_total_depth : 16;
			uint32_t aux_prefetch_current_addr_offset : 16;
		};
		/* RW */
		uint32_t aux_prefetch_parameter;
	};
	union hw_common_aux_sw_trig aux_sw_trig;
	struct hw_colony colony;
} __attribute__((packed, aligned(4)));

#define HW_PING_CH_NUM (8)
#define HW_PONG_CH_NUM (8)
#define MAX_HW_CH_NUM (HW_PING_CH_NUM + HW_PONG_CH_NUM)
struct edma_context {
	struct nvdla_device *dev;
	struct hw_common_reg common_reg;
	/**
	 * Lock protected!
	 * You Get the lock, you Gain the hardware and buffer
	 */
	spinlock_t hw_lock;
	void (*edma_restart)(struct edma_context *);
#define PREFETCH_BUFF_SIZE (4096)
#define PREFETCH_DEPTH (5)
#define PREFETCH_BUFF_STRIDE                                         \
	((sizeof(struct hw_desc_src) + sizeof(struct hw_desc_dst)) * \
	 HW_PING_CH_NUM)
};

static void dump_hw_common_soft_reset(const char *str,
				      union hw_common_soft_reset *rst)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s soft_rst:1;    [%d]\n", __func__, rst->soft_rst);
	dla_info("%s clk_gate:1;    [%d]\n", __func__, rst->clk_gate);
	dla_info("%s clk_gate_ctl:1;[%d]\n", __func__, rst->clk_gate_ctl);
	dla_info("%s rsv0:28;       [%d]\n", __func__, rst->rsv0);
	dla_info("%s halt:1;        [%d]\n", __func__, rst->halt);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}
static void
dump_hw_common_general_parameter(const char *str,
				 union hw_common_general_parameter *gparam)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s aux_mode:1;    [%d]\n", __func__, gparam->aux_mode);
	dla_info("%s rsv1:25;       [%d]\n", __func__, gparam->rsv1);
	dla_info("%s burst_length:6;[%d]\n", __func__, gparam->burst_length);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}
static void dump_hw_common_status(const char *str,
				  union hw_common_status *status)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s edma_busy:1;            [%d]\n", __func__,
		 status->edma_busy);
	dla_info("%s edma_wr_busy:1;         [%d]\n", __func__,
		 status->edma_wr_busy);
	dla_info("%s edma_rd_busy:1;         [%d]\n", __func__,
		 status->edma_rd_busy);
	dla_info("%s edma_aux_triger_busy:1; [%d]\n", __func__,
		 status->edma_aux_triger_busy);
	dla_info("%s edma_main_triger_busy:1;[%d]\n", __func__,
		 status->edma_main_triger_busy);
	dla_info("%s rsv2:4;                 [%d]\n", __func__, status->rsv2);
	dla_info("%s aux_error_entry_num:8;  [%d]\n", __func__,
		 status->aux_error_entry_num);
	dla_info("%s error_entry_num:8;      [%d]\n", __func__,
		 status->error_entry_num);
	dla_info("%s error_type:2;           [%d]\n", __func__,
		 status->error_type);
	dla_info("%s error_cause:4;          [%d]\n", __func__,
		 status->error_cause);
	dla_info("%s error_flag:1;           [%d]\n", __func__,
		 status->error_flag);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}
static void dump_hw_common_sw_trig(const char *str,
				   union hw_common_sw_trig *trig)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s rd_triger_num:4;      [%d]\n", __func__,
		 trig->rd_triger_num);
	dla_info("%s rsv3:12;              [%d]\n", __func__, trig->rsv3);
	dla_info("%s wr_triger_num:4;      [%d]\n", __func__,
		 trig->wr_triger_num);
	dla_info("%s rsv4:9;               [%d]\n", __func__, trig->rsv4);
	dla_info("%s prefetch_addr_reset:1;[%d]\n", __func__,
		 trig->prefetch_addr_reset);
	dla_info("%s desp_ram_invalid:1;   [%d]\n", __func__,
		 trig->desp_ram_invalid);
	dla_info("%s sw_triger:1;          [%d]\n", __func__, trig->sw_triger);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}

static void dump_hw_common_intr_status(const char *str,
				       union hw_common_intr_status *intr_status)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s rd_transport_finish_intr:1;    [%d]\n", __func__,
		 intr_status->rd_transport_finish_intr);
	dla_info("%s rd_int_entry_num:4;            [%d]\n", __func__,
		 intr_status->rd_int_entry_num);
	dla_info("%s wr_transport_finish_intr:1;    [%d]\n", __func__,
		 intr_status->wr_transport_finish_intr);
	dla_info("%s wr_int_entry_num:4;            [%d]\n", __func__,
		 intr_status->wr_int_entry_num);
	dla_info("%s aux_rd_transport_finish_intr:1;[%d]\n", __func__,
		 intr_status->aux_rd_transport_finish_intr);
	dla_info("%s aux_rd_int_entry_num:4;        [%d]\n", __func__,
		 intr_status->aux_rd_int_entry_num);
	dla_info("%s aux_wr_transport_finish_intr:1;[%d]\n", __func__,
		 intr_status->aux_wr_transport_finish_intr);
	dla_info("%s aux_wr_int_entry_num:4;        [%d]\n", __func__,
		 intr_status->aux_wr_int_entry_num);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}

static void dump_hw_common_aux_sw_trig(const char *str,
				       union hw_common_aux_sw_trig *trig)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s aux_rd_triger_num:4;      [%d]\n", __func__,
		 trig->aux_rd_triger_num);
	dla_info("%s aux_rsv3:12;              [%d]\n", __func__,
		 trig->aux_rsv6);
	dla_info("%s aux_wr_triger_num:4;      [%d]\n", __func__,
		 trig->aux_wr_triger_num);
	dla_info("%s aux_rsv4:9;               [%d]\n", __func__,
		 trig->aux_rsv7);
	dla_info("%s aux_prefetch_addr_reset:1;[%d]\n", __func__,
		 trig->aux_prefetch_addr_reset);
	dla_info("%s aux_desp_ram_invalid:1;   [%d]\n", __func__,
		 trig->aux_desp_ram_invalid);
	dla_info("%s aux_sw_triger:1;          [%d]\n", __func__,
		 trig->aux_sw_triger);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}

static void dump_hw_colony(const char *str, struct hw_colony *colony)
{
	if (str) {
		dla_info("****%s****\n", str);
	}
	dla_info("--------------------start-%s --------------------\n",
		 __func__);
	dla_info("%s src_colony_num:16;[%d]\n", __func__,
		 colony->src_colony_num);
	dla_info("%s dst_colony_num:16;[%d]\n", __func__,
		 colony->dst_colony_num);
	dla_info("%s src_colony_stride;[0x%x]\n", __func__,
		 colony->src_colony_stride);
	dla_info("%s dst_colony_stride;[0x%x]\n", __func__,
		 colony->dst_colony_stride);
	dla_info("%s aux_src_colony_num:16;[%d]\n", __func__,
		 colony->aux_src_colony_num);
	dla_info("%s aux_dst_colony_num:16;[%d]\n", __func__,
		 colony->aux_dst_colony_num);
	dla_info("%s aux_src_colony_stride;[0x%x]\n", __func__,
		 colony->aux_src_colony_stride);
	dla_info("%s aux_dst_colony_stride;[0x%x]\n", __func__,
		 colony->aux_dst_colony_stride);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}

static void dump_hw_common_reg_all_from_reg(const char *str,
					    struct edma_context *edma,
					    uint64_t base)
{
	int i;
	uint32_t *p;
	struct hw_common_reg reg;

	p = (uint32_t *)(&reg);
	for (i = 0; i < sizeof(struct hw_common_reg); i += 4) {
		*(p++) = dla_reg_read(edma->dev, base + i);
	}
	dump_hw_common_soft_reset(str, &reg.rst);
	dump_hw_common_general_parameter(str, &reg.gparam);
	dump_hw_common_status(str, &reg.hw_status);
	dump_hw_common_sw_trig(str, &reg.sw_trig);
	dla_info("%s rd_int_en_entry_bit:16;[%d]\n", __func__,
		 reg.rd_int_en_entry_bit);
	dla_info("%s wr_int_en_entry_bit:16;[%d]\n", __func__,
		 reg.wr_int_en_entry_bit);
	dla_info("%s rd_int_mask_entry_bit:16;[%d]\n", __func__,
		 reg.rd_int_mask_entry_bit);
	dla_info("%s wr_int_mask_entry_bit:16;[%d]\n", __func__,
		 reg.wr_int_mask_entry_bit);
	dump_hw_common_intr_status(str, &reg.intr_status);
	dla_info("%s prefetch_addr_low; [0x%x]\n", __func__,
		 reg.prefetch_addr_low);
	dla_info("%s prefetch_addr_high;[0x%x]\n", __func__,
		 reg.prefetch_addr_high);
	dla_info("%s prefetch_total_depth:16;        [%d]\n", __func__,
		 reg.prefetch_total_depth);
	dla_info("%s prefetch_current_addr_offset:16;[%d]\n", __func__,
		 reg.prefetch_current_addr_offset);
	dla_info("%s err_addr_high;         [0x%x]\n", __func__,
		 reg.err_addr_high);
	dla_info("%s err_addr_low;          [0x%x]\n", __func__,
		 reg.err_addr_low);
	dla_info("%s aux_prefetch_addr_low; [0x%x]\n", __func__,
		 reg.aux_prefetch_addr_low);
	dla_info("%s aux_prefetch_addr_high;[0x%x]\n", __func__,
		 reg.aux_prefetch_addr_high);
	dla_info("%s aux_prefetch_total_depth:16;     [%d]\n", __func__,
		 reg.aux_prefetch_total_depth);
	dla_info("%s aux_prefetch_current_addr_offset:[%d]\n", __func__,
		 reg.aux_prefetch_current_addr_offset);
	dump_hw_common_aux_sw_trig(str, &reg.aux_sw_trig);
	dump_hw_colony(str, &reg.colony);
	dla_info("--------------------end--%s --------------------\n",
		 __func__);
}

static void config_general_parameter(struct edma_context *edma,
				     struct nvdla_device *nvdla_dev)
{
	edma->common_reg.gparam.general_parameter = dla_reg_read(
		nvdla_dev, EDMA_S_BASE_ADDR + EDMA_REG_GENERAL_PARAMETER);
	edma->common_reg.gparam.aux_mode = 1;
	edma->common_reg.gparam.burst_length = 1;
	dla_reg_write(nvdla_dev, EDMA_S_BASE_ADDR + EDMA_REG_GENERAL_PARAMETER,
		      edma->common_reg.gparam.general_parameter);
	return;
}

static void restart(struct edma_context *edma)
{
	union hw_common_soft_reset reset;
	/* TODO:
	 *     1. clean hw_desc->status
	 *     2. invalid hw_desc->prev_link_index
	 *     3. trigger hw_common_reg->soft_rst
	 */
	dump_hw_common_reg_all_from_reg("edma restart trigger", edma,
					EDMA_S_BASE_ADDR);
	reset.soft_reset =
		dla_reg_read(edma->dev, EDMA_S_BASE_ADDR + EDMA_REG_SOFT_RST);
	reset.soft_rst = 1;
	reset.halt = 0;
	dla_reg_write(edma->dev, EDMA_S_BASE_ADDR + EDMA_REG_SOFT_RST,
		      reset.soft_reset);
	return;
}

static inline void setup_one_despram(struct nvdla_device *dev,
				     edma_program_t *edma_prog,
				     uint32_t desc_offset,
				     uint32_t *src_desc_start,
				     uint32_t *dst_desc_start)
{
	int i;
	int j;
	uint32_t src_base;
	uint32_t dst_base;

	src_base = REG_SRC_DESP_MAIN_BASE + desc_offset * EDMA_DESP_STRIDE;
	dst_base = REG_DST_DESP_MAIN_BASE + desc_offset * EDMA_DESP_STRIDE;
	for (i = 0, j = 0; i < EDMA_DESP_SIZE; i += 4, j++) {
		edma_prog->reg[EDMA_RAM_SRC_BASE_0 + j] = *src_desc_start;
		edma_prog->reg[EDMA_RAM_DST_BASE_0 + j] = *dst_desc_start;
		npu_set_u64_bit(EDMA_RAM_SRC_BASE_0 + j,
				&edma_prog->u84_bitmap);
		npu_set_u64_bit(EDMA_RAM_DST_BASE_0 + j,
				&edma_prog->u84_bitmap);
		src_desc_start++;
		dst_desc_start++;
	}
	return;
}

static inline void setup_ping_colony_register(struct nvdla_device *dev,
					      edma_program_t *edma_prog,
					      struct hw_colony *colony)
{
	edma_prog->reg[EDMA_COLONY_NUM] = colony->colony_num;
	npu_set_u64_bit(EDMA_COLONY_NUM, &edma_prog->u84_bitmap);

	edma_prog->reg[EDMA_SRC_COLONY_STRIDE] = colony->src_colony_stride;
	npu_set_u64_bit(EDMA_SRC_COLONY_STRIDE, &edma_prog->u84_bitmap);

	edma_prog->reg[EDMA_DST_COLONY_STRIDE] = colony->dst_colony_stride;
	npu_set_u64_bit(EDMA_DST_COLONY_STRIDE, &edma_prog->u84_bitmap);
	return;
}

static inline void setup_ping_refresh_register(struct nvdla_device *dev,
					       edma_program_t *edma_prog,
					       struct hw_refresh *refresh)
{
	edma_prog->reg[EDMA_REFRESH_TIMES] = refresh->main_refresh_times;
	npu_set_u64_bit(EDMA_REFRESH_TIMES, &edma_prog->u84_bitmap);
}

static int setup_hw_desc_src(struct edma_desc *desc, struct hw_desc_src *hw,
			     struct hw_colony *colony)
{
	hw->src_addr_low = LOW32BITS(desc->src_base_addr);
	hw->src_addr_high = (uint16_t)HIGH32BITS(desc->src_base_addr);
	/* Maybe Not really valid */
	hw->src_input_c0 = (uint16_t)(desc->op->input_c0_bytes >>
				      N_FORMAT_C0_OFFSET); /* /32 */
	hw->src_input_c0 = ((hw->src_input_c0 == 1) ? 0 : hw->src_input_c0);
	hw->src_line_len =
		(uint16_t)(desc->op->src_num_line * desc->op->input_c0_bytes);
	hw->src_line_stride = desc->op->src_stride_line_bytes;
	hw->src_line_num = (uint16_t)desc->op->src_num_surface;
	hw->src_slice_stride = desc->op->src_stride_surface_bytes;
	colony->src_colony_stride = desc->op->src_stride_cube_bytes;

	hw->src_slice_num = desc->op->src_num_cube;
	/* enable dst finish intr and disable src */
	hw->src_link_item_finish_intr = 0;

	/* Notice: Colony need supplementary_dimension_enable */
	if (desc->op->src_num_colony > 1) {
		hw->src_link_item_supply_dim_en = 1;
	} else {
		hw->src_link_item_supply_dim_en = 0;
	}
	colony->src_colony_num = desc->op->src_num_colony;
	/* we do't know ping or pong will work until now, set both */
	colony->aux_src_colony_num = colony->src_colony_num;

	/* n2e */
	if (desc->op->input_c0_bytes != desc->op->output_c0_bytes) {
		if (desc->op->input_c0_bytes & N_FORMAT_C0_MASK) {
			dla_error("%s, %d, invalid input c0\n", __func__,
				  __LINE__);
			return -EINVAL;
		}
		hw->src_link_item_op_mode_n2e = 1;
		switch (desc->op->output_c0_bytes &
			N_FORMAT_C0_MASK) { /* %32 */
		case 1:
			hw->src_link_item_op_mode_c0 = 0x4; /* 0b100 */
			break;
		case 2:
			hw->src_link_item_op_mode_c0 = 0; /* 0b000 */
			break;
		case 4:
			hw->src_link_item_op_mode_c0 = 0x1; /* 0b001 */
			break;
		case 8:
			hw->src_link_item_op_mode_c0 = 0x2; /* 0b010 */
			break;
		case 16:
			hw->src_link_item_op_mode_c0 = 0x3; /* 0b0111 */
			break;
		default:
			dla_error("%s %d bad output_c0 0x%x\n", __func__,
				  __LINE__, desc->op->output_c0_bytes);
			return -1;
		}

		if (desc->op->input_c0_bytes > N_FORMAT_C0) {
			hw->src_line_len = N_FORMAT_C0;
			hw->src_line_num = desc->op->src_num_line;
			hw->src_line_stride = desc->op->input_c0_bytes;
			hw->src_slice_num = desc->op->src_num_surface;
			hw->src_slice_stride = desc->op->src_stride_line_bytes;
		}

		/* n2e Need colony stride */
		if (desc->op->src_num_cube > 1 ||
		    desc->op->src_num_colony > 1) {
			if (desc->op->input_c0_bytes == N_FORMAT_C0) {
				hw->src_slice_num = 1;
				colony->src_colony_stride =
					hw->src_slice_stride;
			} else {
				colony->src_colony_stride =
					desc->op->src_stride_surface_bytes;
			}
		}
		colony->src_colony_num = 1;
		hw->src_link_item_supply_dim_en = 1;
		colony->aux_src_colony_num = colony->src_colony_num;
		colony->aux_src_colony_stride = colony->src_colony_stride;
	} else {
		hw->src_link_item_op_mode_n2e = 0;
		hw->src_link_item_op_mode_c0 = 0;
	}
	/* TODO: So what if compiler want u16 */
	hw->src_data_type = 0;
	/* If link mode is needed, setup this field in other func, not here */
	hw->src_link_item_link_mode = 0;
	colony->aux_src_colony_stride = colony->src_colony_stride;
	return 0;
}

static int setup_hw_desc_dst(struct edma_desc *desc, struct hw_desc_dst *dst,
			     struct hw_desc_src *src, struct hw_colony *colony)
{
	int piece;
	int line_num_offset;
	uint32_t data_size;
	struct npu_edma_op_desc *op;

	/* set default 0 */
	dst->dst_slice_num_link_item = 0;
	op = desc->op;
	dst->dst_addr_low = LOW32BITS(desc->dst_base_addr);
	dst->dst_addr_high = (uint16_t)HIGH32BITS(desc->dst_base_addr);
	/* enable dst finish intr and disable src */
	dst->dst_link_item_finish_intr = 1;
	/* copy src */
	dst->dst_link_item_op_mode_n2e = src->src_link_item_op_mode_n2e;

	if (desc->op->dst_num_colony > 1) {
		dst->dst_link_item_supply_dim_en = 1;
	} else {
		dst->dst_link_item_supply_dim_en = 0;
	}

	/* n2e */
	if (dst->dst_link_item_op_mode_n2e == 1) {
		data_size =
			N_FORMAT_C0 * op->src_num_line * op->src_num_surface;

		if (op->input_c0_bytes > N_FORMAT_C0) {
			colony->dst_colony_num = src->src_input_c0;
		} else {
			colony->dst_colony_num = 1;
		}
		dst->dst_link_item_supply_dim_en = 1;

		if (src->src_data_type == 0) {
			dst->dst_line_num = 32 / op->output_c0_bytes;
		} else {
			dst->dst_line_num = 16 / op->output_c0_bytes;
		}
		switch (dst->dst_line_num) {
		case 32:
			line_num_offset = 0x5;
			break;
		case 16:
			line_num_offset = 0x4;
			break;
		case 8:
			line_num_offset = 0x3;
			break;
		case 4:
			line_num_offset = 0x2;
			break;
		case 2:
			line_num_offset = 0x1;
			break;
		default:
			line_num_offset = 0x0;
			dla_error("unsupported fast divide\n");
			break;
		}
		dst->dst_line_stride = data_size >> line_num_offset;
		if (data_size >= EDMA_BUFFER_LEN) {
			//EDMA_BUFFER_LEN / dst->dst_line_num;
			dst->dst_line_len = EDMA_BUFFER_LEN >> line_num_offset;
			piece = data_size >> EDMA_BUFFER_LEN_OFFSET;
			/* roundup data_size / 2048 */
			dst->dst_slice_num =
				piece +
				((data_size & (EDMA_BUFFER_LEN_MASK)) ? 1 : 0);
			// data_size / dst->dst_line_num;
			dst->dst_supply_line_len = dst->dst_line_stride -
						   dst->dst_line_len * piece;
		} else {
			//data_size / dst->dst_line_num;
			dst->dst_line_len = dst->dst_line_stride;
			/* roundup data_size / 2048 */
			dst->dst_slice_num = 1;
			// data_size / dst->dst_line_num;
			dst->dst_supply_line_len = data_size >> line_num_offset;
		}
		dst->dst_slice_stride = dst->dst_line_len;
	} else {
		dst->dst_supply_line_len = 0x0;	 //0;
		dst->dst_line_len =
			(uint16_t)(op->dst_num_line * op->input_c0_bytes);
		dst->dst_line_num = (uint16_t)op->dst_num_surface;
		dst->dst_line_stride = op->dst_stride_line_bytes;
		dst->dst_slice_stride = op->dst_stride_surface_bytes;
		dst->dst_slice_num = op->dst_num_cube;
	}
	/* Should consistent with src */
	dst->dst_link_item_op_mode_c0 = src->src_link_item_op_mode_c0;
	/* copy src */
	dst->dst_link_item_link_mode = src->src_link_item_link_mode;
	/* Notice: Colony need supplementary_dimension_enable(dst) */
	if (dst->dst_link_item_op_mode_n2e == 1) {
		colony->dst_colony_stride = data_size;
	} else {
		colony->dst_colony_num = op->dst_num_colony;
		colony->dst_colony_stride = op->dst_stride_cube_bytes;
	}
	/* copy src */
	dst->dst_data_type = src->src_data_type;
	/* we do't know ping or pong will work until now, config both */
	colony->aux_dst_colony_num = colony->dst_colony_num;
	colony->aux_dst_colony_stride = colony->dst_colony_stride;
	return 0;
}

/**
 * return: number of hw_desc that construct and linked togetcher
 */
static int descs_constructor(struct edma_desc *desc[], struct hw_desc hw[],
			     int desc_num)
{
	uint8_t i;
	int ret, link_cnt;
	int refresh_times = 0;

	link_cnt = 0;
	for (i = 0; i < desc_num; i++) {
		ret = setup_hw_desc_src(desc[i], hw[i].src, &hw[i].colony);
		if (ret < 0) {
			dla_info("%s %d desc[%d] src construct fail\n",
				 __func__, __LINE__, i);
			return -EINVAL;
		}
		ret = setup_hw_desc_dst(desc[i], hw[i].dst, hw[i].src,
					&hw[i].colony);
		if (ret < 0) {
			dla_info("%s %d desc[%d] dst construct fail\n",
				 __func__, __LINE__, i);
			return -EINVAL;
		}

		if (desc[i]->op->input_c0_bytes !=
			    desc[i]->op->output_c0_bytes &&
		    (desc[i]->op->src_num_cube != 1 ||
		     desc[i]->op->src_num_colony != 1)) {
			if (desc[i]->op->src_num_cube *
				    desc[i]->op->src_stride_surface_bytes !=
			    desc[i]->op->src_stride_cube_bytes) {
				dla_error("%s %d n2e cube exist stride\n",
					  __func__, __LINE__);
				return -EINVAL;
			}
			refresh_times = desc[i]->op->src_num_cube *
					desc[i]->op->src_num_colony;

			if (refresh_times > 65535) {
				dla_error(
					"%s %d n2e dst cube x colony %d > 65535\n",
					__func__, __LINE__, refresh_times);
				return -EINVAL;
			}
			// hw need -1 config
			hw[i].refresh.main_refresh_times = refresh_times - 1;
			hw[i].refresh.aux_refresh_times = refresh_times - 1;
			hw[i].src->src_link_item_link_mode =
				PING_PONG_NON_SEQ_REFRESH;
			hw[i].dst->dst_link_item_link_mode =
				PING_PONG_NON_SEQ_REFRESH;
			goto exit;
		}

		/* Task with same colony could linked together */
		if (i > 0) {
			ret = memcmp(&hw[i].colony, &hw[i - 1].colony,
				     sizeof(struct hw_colony));
			if (ret == 0 && link_cnt < HW_PING_CH_NUM) {
				hw[i - 1].src->src_link_item_link_mode =
					PING_PONG_SEQ_AND_PREFETCH;
				hw[i - 1].dst->dst_link_item_link_mode =
					PING_PONG_SEQ_AND_PREFETCH;
				hw[i - 1]
					.src
					->src_link_item_entryNum_or_reGen_times =
					link_cnt;
				hw[i - 1]
					.dst
					->dst_link_item_entryNum_or_reGen_times =
					link_cnt;
				/* Linked desp only need last intr */
				hw[i - 1].dst->dst_link_item_finish_intr = 0;
				hw[i - 1].next_task = &hw[i];
				link_cnt++;
			} else {
				hw[i - 1].src->src_link_item_link_mode =
					PING_PONG_NON_LINK;
				hw[i - 1].dst->dst_link_item_link_mode =
					PING_PONG_NON_LINK;
				hw[i - 1]
					.src
					->src_link_item_entryNum_or_reGen_times =
					0;
				hw[i - 1]
					.dst
					->dst_link_item_entryNum_or_reGen_times =
					0;
				hw[i - 1].next_task = NULL;
				break;
			}
		} else {
			hw[i].src->src_link_item_link_mode = PING_PONG_NON_LINK;
			hw[i].dst->dst_link_item_link_mode = PING_PONG_NON_LINK;
			hw[i].src->src_link_item_entryNum_or_reGen_times = 0;
			hw[i].dst->dst_link_item_entryNum_or_reGen_times = 0;
			hw[i].next_task = NULL;
		}
	}
exit:
	return (link_cnt + 1);
}

int edma_rdma_check(struct dla_processor_group *group,
		    union dla_operation_container *op,
		    union dla_surface_container *surface)
{
	return 0;
}

int edma_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	struct npu_edma_surface_desc *surface = &surface_desc->edma_surface;
	edma_tensor_t *edma_tensor = tensor;
	int ret, cnt;
	u64 src_base_addr, dst_base_addr;
	struct edma_desc edesc;
	struct edma_desc *pdesc[1];
	edma_dev_t *edma = NULL;
	edma_dev_t *edma_dev = NULL;
	npu_dep_info_t *npu_info = NULL;
	edma_program_t *edma_prog;

	edma = (edma_dev_t *)executor->prog_data_buf_bobj[IDX_EDMA];
	edma_dev = (edma_dev_t *)&edma[idx];
	npu_info = &edma_dev->npu_info;

	edma_prog = &edma_dev->prog_data;

	edma_prog->input_tensor_idx = invalid_tensor_idx;
	edma_prog->output_tensor_idx = invalid_tensor_idx;

	ret = read_input_address(executor, &surface->src_data,
				 (void *)&src_base_addr,
				 &edma_tensor[idx].src_is_io_tensor);

	if (ret != 0) {
		dla_error("%s %d bad memory type %d\n", __func__, __LINE__,
			  surface->src_data.type);
		return -1;
	}
	ret = read_input_address(executor, &surface->dst_data,
				 (void *)&dst_base_addr,
				 &edma_tensor[idx].dst_is_io_tensor);

	if (ret != 0) {
		dla_error("%s %d bad memory type %d\n", __func__, __LINE__,
			  surface->dst_data.type);
		return -1;
	}
	if (edma_tensor[idx].src_is_io_tensor == invalid_tensor_idx) {
		if (unlikely(src_base_addr == -1)) {
			dla_error("%s %d bad memory type %d\n", __func__,
				  __LINE__, surface->src_data.type);
			return -1;
		} else {
			edesc.src_base_addr = src_base_addr;
		}
	} else {
		edesc.src_base_addr = surface->src_data.offset;
		edma_prog->input_tensor_idx = edma_tensor[idx].src_is_io_tensor;
	}

	if (edma_tensor[idx].dst_is_io_tensor == invalid_tensor_idx) {
		if (unlikely(dst_base_addr == -1)) {
			dla_error("%s %d bad memory type %d\n", __func__,
				  __LINE__, surface->dst_data.type);
			return -1;
		} else {
			edesc.dst_base_addr = dst_base_addr;
		}
	} else {
		edesc.dst_base_addr = surface->dst_data.offset;
		edma_prog->output_tensor_idx = edma_tensor[idx].dst_is_io_tensor;
	}
	edesc.op = &operation_desc->edma_op;
	pdesc[0] = &edesc;
	edma_tensor[idx].hw.src = &edma_tensor[idx].src;
	edma_tensor[idx].hw.dst = &edma_tensor[idx].dst;
	edma_tensor[idx].hw.next_task = NULL;
	edma_tensor[idx].hw.d = NULL;
	npu_info->current_op_idx = op_idx;
	cnt = descs_constructor(pdesc, &edma_tensor[idx].hw, 1);

	if (cnt < 0 || cnt != 1) {
		dla_error("cnt: %d desc_num: 1\n", cnt);
	}
	return 0;
}

int edma_prepare_prog_data(struct win_executor *executor, int rdma, int seq,
			   u16 op_idx,
			   union dla_operation_container *operation_desc,
			   union dla_surface_container *surface_desc)
{
	struct npu_edma_surface_desc *surface;
	struct nvdla_device *nvdla_dev;
	edma_tensor_t *tensor = executor->tensor_set[IDX_EDMA];
	edma_tensor_t *edma_tensor = NULL;
	edma_dev_t *edma =
		(edma_dev_t *)executor->prog_data_buf_bobj[IDX_EDMA];
	edma_dev_t *edma_dev = (edma_dev_t *)&edma[seq];
	edma_program_t *edma_prog = &edma_dev->prog_data;

	surface = &surface_desc->edma_surface;
	nvdla_dev = executor->driver_context;
	edma_tensor = &tensor[seq];

	setup_ping_colony_register(nvdla_dev, edma_prog,
				   &edma_tensor->hw.colony);
	setup_ping_refresh_register(nvdla_dev, edma_prog,
				    &edma_tensor->hw.refresh);
	setup_one_despram(nvdla_dev, edma_prog, 0,
			  (uint32_t *)edma_tensor->hw.src,
			  (uint32_t *)edma_tensor->hw.dst);
	return 0;
}

void edma_free(struct nvdla_device *nvdla_dev)
{
	struct edma_context *edma;

	edma = nvdla_dev->edma;
	if (edma == NULL) {
		return;
	}
	kfree(edma);
	nvdla_dev->edma = NULL;
	return;
}

int edma_init(struct nvdla_device *nvdla_dev)
{
	struct edma_context *edma;

	if (nvdla_dev->edma) {
		dla_info("%s %d edma already exist\n", __func__, __LINE__);
		return -1;
	}
	edma = kzalloc(sizeof(struct edma_context), GFP_KERNEL);
	if (edma == NULL) {
		dla_info("%s %d edma can't alloc memory\n", __func__, __LINE__);
		return -1;
	}
	spin_lock_init(&edma->hw_lock);
	config_general_parameter(edma, nvdla_dev);
	edma->edma_restart = restart;
	nvdla_dev->edma = edma;
	edma->dev = nvdla_dev;
	return 0;
}
