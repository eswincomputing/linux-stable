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

#ifndef __EDMA_H_
#define __EDMA_H_

#include "dla_interface.h"
// #define DESC_PREFETCH

// Define base address
#define EDMA_S_BASE_ADDR 0x110000
#define EDMA_D_BASE_ADDR 0x111000
#define SRAM_BASE_ADDR 0x59000000

#define EDMA_BUFFER_LEN 2048
#define EDMA_BUFFER_LEN_OFFSET (11)
#define EDMA_BUFFER_LEN_MASK (2048 - 1)
#define N_FORMAT_C0_OFFSET 5
#define N_FORMAT_C0 32
#define N_FORMAT_C0_MASK (32 - 1)

// Define REG_SOFT mask
#define REG_SOFT_MASK 0xffffffff
#define REG_SOFT_RST_MASK 0x00000001
#define REG_SOFT_RST_GATE_MASK 0x00000002
#define REG_SOFT_RST_GATE_CTL_MASK 0x00000004
#define REG_SOFT_RST_HALT_MASK 0x80000000

// Define REG_SOFT value
#define REG_SOFT_RST_VAL 0
#define REG_SOFT_RST_HALT_VAL 0

// Define REG_GENERAL_PARAMETER mask
#define REG_GENERAL_PARAMETER_MASK 0xffffffff
#define REG_GENERAL_PARAMETER_AUX_MASK 0x00000001
#define REG_GENERAL_PARAMETER_LEN_MASK 0xfc000000

// Define REG_GENERAL_PARAMETER value
#define REG_GENERAL_PARAMETER_AUX_VAL 0x00000001
#define REG_GENERAL_PARAMETER_LEN_VAL 0x04000000

// Define REG_SW_TRIG mask
#define REG_SW_TRIG_MASK 0xffffffff
#define REG_SW_TRIG_RD_NUM_MASK 0x0000000f
#define REG_SW_TRIG_WR_NUM_MASK 0x000f0000
#define REG_SW_TRIG_PRE_ADDR_RESET_MASK 0x20000000
#define REG_SW_TRIG_DESP_RAM_INVALID_MASK 0x40000000
#define REG_SW_TRIGGER_MASK 0x80000000

// Define REG_SW_TRIG value
#ifdef DESC_PREFETCH
#define REG_SW_TRIG_PRE_ADDR_RESET_VAL 0x20000000
#define REG_SW_TRIG_DESP_RAM_INVALID_VAL 0x40000000
#else
#define REG_SW_TRIG_PRE_ADDR_RESET_VAL 0x00000000
#define REG_SW_TRIG_DESP_RAM_INVALID_VAL 0x00000000
#endif
#define REG_SW_TRIGGER_VAL 0x80000000

// Define REG_AUX_SW_TRG mask
#define REG_SW_TRIG_AUX_RD_NUM_MASK 0x0000000f
#define REG_SW_TRIG_AUX_WR_NUM_MASK 0x000f0000
#define REG_SW_TRIG_AUX_DESP_RAM_INVALID_MASK 0x40000000
#define REG_SW_TRIGGER_AUX_MASK 0x80000000

// Define REG_AUX_SW_TRIG value
#define REG_SW_TRIG_AUX_DESP_RAM_INVALID_VAL 0x00000000
#define REG_SW_TRIGGER_AUX_VAL 0x80000000

// Define REG_PREFETCH_PARAMETER mask
#define REG_PREFETCH_PARAMETER_MASK
#define REG_PREFETCH_PARAMETER_TOTAL_DEPTH_MASK 0x0000ffff
#define REG_PREFETCH_PARAMETER_ADDR_OFFSET_MASK 0xffff0000

// Define REG_PREFETCH_PARAMETER value
#define REG_PREFETCH_PARAMETER_TOTAL_DEPTH_VAL \
	0x00000780  // 1920 = 384 * 5 < 2048

// Define REG_STATUS mask
#define REG_STATUS_BUSY 0x00000001
#define REG_STATUS_WR_BUSY 0x00000002
#define REG_STATUS_RD_BUSY 0x00000004
#define REG_STATUS_AUX_TRIG_BUSY 0x00000008
#define REG_STATUS_MAIN_TRIG_BUSY 0x00000010
#define REG_STATUS_AUX_ERR_ENTRY_NUM 0x0001fe00
#define REG_STATUS_ERR_ENTRY_NUM 0x01fe0000
#define REG_STATUS_ERR_TYPE 0x06000000
#define REG_STATUS_ERR_CAUSE 0x78000000
#define REG_STATUS_ERR_FLAG 0x80000000

// Define REG_INT_STATUS mask
#define REG_INT_STATUS_RD_INTR_MASK 0x00000001
#define REG_INT_STATUS_RD_ENTRY_NUM_MASK 0x0000001e
#define REG_INT_STATUS_WR_INTR_MASK 0x00000020
#define REG_INT_STATUS_WR_ENTRY_NUM_MASK 0x000003c0
#define REG_INT_STATUS_AUX_RD_INTR_MASK 0x00000400
#define REG_INT_STATUS_AUX_RD_ENTRY_NUM_MASK 0x00007800
#define REG_INT_STATUS_AUX_WR_INTR_MASK 0x00008000
#define REG_INT_STATUS_AUX_WR_ENTRY_NUM_MASK 0x000f0000

// Define REG_DST_COLONY_NUM offset
#define REG_DST_COLONY_NUM_OFFSET 16

// Define RAM_SRC_ADDRESS mask
#define RAM_SRC_ADDR_LOW_MASK 0xffffffff
#define RAM_SRC_ADDR_HIGH_MASK 0xffffffff

// Define RAM_SRC_LINE_LEN_WITH_NUM mask
#define RAM_SRC_LINE_LEN_WITH_NUM_MASK 0xffffffff
#define RAM_SRC_LINE_LEN_MASK 0x0000ffff
#define RAM_SRC_LINE_NUM_MASK 0xffff0000

// Define RAM_SRC_ADDR_HIGH_BIT_OFFSET
#define RAM_SRC_ADDR_HIGH_BIT_OFFSET 0
#define RAM_SRC_INPUT_C0_BITOFFSET 16

// Define RAM_SRC_LINE_LEN_WITH_NUM bit offset
#define RAM_SRC_LINE_LEN_BIT_OFFSET 0
#define RAM_SRC_LINE_NUM_BIT_OFFSET 16

// Define RAM_SRC_LINE_SLICE_STRIDE mask
#define RAM_SRC_LINE_SLICE_STRIDE_MASK 0xffffffff
#define RAM_SRC_LINE_STRIDE_MASK 0x0000ffff
#define RAM_SRC_SLICE_STRIDE_MASK 0xffff0000

// Define RAM_SRC_LINE_SLICE_STRIDE bit offset
#define RAM_SRC_LINE_STRIDE_BIT_OFFSET 0
#define RAM_SRC_SLICE_STRIDE_BIT_OFFSET 16

// Define RAM_SRC_SLICENUM_LINKITEM mask
#define RAM_SRC_SLICENUM_LINKITEM_MASK 0xffffffff
#define RAM_SRC_SLICE_NUM_MASK 0x0000ffff
#define RAM_SRC_LINK_ITEM_LINK_MODE_MASK 0x00070000
#define RAM_SRC_LINK_ITEM_ENTRY_NUM_MASK 0x00780000
#define RAM_SRC_LINK_ITEM_SUPPLY_DIM_MASK 0x08000000
#define RAM_SRC_LINK_ITEM_DATA_TYPE_MASK 0x04000000
#define RAM_SRC_LINK_ITEM_OP_MODE_MASK 0x78000000
#define RAM_SRC_LINK_ITEM_INTR_MASK 0x80000000

// Define RAM_SRC_SLICENUM_LINKITEM bit offset
#define RAM_SRC_SLICE_NUM_BIT_OFFSET 0
#define RAM_SRC_LINK_ITEM_LINK_MODE_BIT_OFFSET 16
#define RAM_SRC_LINK_ITEM_ENTRY_NUM_BIT_OFFSET 19
#define RAM_SRC_LINK_ITEM_SUPPLY_DIM_BIT_OFFSET 25
#define RAM_SRC_LINK_ITEM_DATA_TYPE_BIT_OFFSET 26
#define RAM_SRC_LINK_ITEM_OP_MODE_BIT_OFFSET 27
#define RAM_SRC_LINK_ITEM_INTR_BIT_OFFSET 31

// Define RAM_DST_ADDRESS mask
#define RAM_DST_ADDR_LOW_MASK 0xffffffff
#define RAM_DST_ADDR_HIGH_MASK 0xffffffff

// Define RAM_DST_LINE_LEN_WITH_NUM mask
#define RAM_DST_LINE_LEN_WITH_NUM_MASK 0xffffffff
#define RAM_DST_LINE_LEN_MASK 0x0000ffff
#define RAM_DST_LINE_NUM_MASK 0xffff0000

// Define RAM_DST_LINE_LEN_WITH_NUM bit offset
#define RAM_DST_LINE_LEN_BIT_OFFSET 0
#define RAM_DST_LINE_NUM_BIT_OFFSET 16

// Define RAM_DST_LINE_SLICE_STRIDE mask
#define RAM_DST_LINE_SLICE_STRIDE_MASK 0xffffffff
#define RAM_DST_LINE_STRIDE_MASK 0x0000ffff
#define RAM_DST_SLICE_STRIDE_MASK 0xffff0000

// Define COLONY_NUM bit offset
#define RAM_SRC_COLONY_NUM_BIT_OFFSET 0
#define RAM_DST_COLONY_NUM_BIT_OFFSET 16

// Define DST_ADDR_HIGH bit offset
#define RAM_DST_ADDR_HIGH_BIT_OFFSET 0
#define RAM_DST_SUPPLY_LINE_LEN_BIT_OFFSET 16

// Define RAM_DST_LINE_SLICE_STRIDE bit offset
#define RAM_DST_LINE_STRIDE_BIT_OFFSET 0
#define RAM_DST_SLICE_STRIDE_BIT_OFFSET 16

// Define RAM_DST_SLICENUM_LINKITEM mask
#define RAM_DST_SLICENUM_LINKITEM_MASK 0xffffffff
#define RAM_DST_SLICE_NUM_MASK 0x0000ffff
#define RAM_DST_LINK_ITEM_LINK_MODE_MASK 0x00070000
#define RAM_DST_LINK_ITEM_ENTRY_NUM_MASK 0x00780000
#define RAM_DST_LINK_ITEM_DATA_TYPE_MASK 0x04000000
#define RAM_DST_LINK_ITEM_OP_MODE_MASK 0x78000000
#define RAM_DST_LINK_ITEM_INTR_MASK 0x80000000

// Define RAM_DST_SLICENUM_LINKITEM bit offset
#define RAM_DST_SLICE_NUM_BIT_OFFSET 0
#define RAM_DST_LINK_ITEM_LINK_MODE_BIT_OFFSET 16
#define RAM_DST_LINK_ITEM_ENTRY_NUM_BIT_OFFSET 19
#define RAM_DST_LINK_ITEM_SUPPLY_DIM_BIT_OFFSET 25
#define RAM_DST_LINK_ITEM_DATA_TYPE_BIT_OFFSET 26
#define RAM_DST_LINK_ITEM_OP_MODE_BIT_OFFSET 27
#define RAM_DST_LINK_ITEM_INTR_BIT_OFFSET 31

#define SRC_DESP_BASE 0x100
#define DST_DESP_BASE 0x300

#define AUX_DESP_INDEX 8
#define EDMA_DESP_SIZE 0x18
#define EDMA_DESP_STRIDE 0x20

#define PREFETCH_DST_INDEX_OFFSET 8

#define AUX_PREFETCH_ADDR_OFFSET 2048

#define REG_SRC_DESP_MAIN_BASE (EDMA_S_BASE_ADDR + SRC_DESP_BASE)
#define REG_SRC_DESP_AUX_BASE \
	(EDMA_S_BASE_ADDR + SRC_DESP_BASE + AUX_DESP_INDEX * EDMA_DESP_STRIDE)
#define REG_DST_DESP_MAIN_BASE (EDMA_S_BASE_ADDR + DST_DESP_BASE)
#define REG_DST_DESP_AUX_BASE \
	(EDMA_S_BASE_ADDR + DST_DESP_BASE + AUX_DESP_INDEX * EDMA_DESP_STRIDE)

#define NUM_MAX_EDMA_OPS 8

typedef enum {
	// EDMA REG
	EDMA_REG_SOFT_RST = 0x0,
	EDMA_REG_GENERAL_PARAMETER = 0x4,
	EDMA_REG_STATUS = 0x8,
	EDMA_REG_SW_TRIG = 0xc,
	EDMA_REG_INT_EN = 0x10,
	EDMA_REG_INT_MASK = 0x14,
	EDMA_REG_INT_STATUS = 0x18,
	EDMA_REG_PREFETCH_ADDR_LOW = 0x1c,
	EDMA_REG_PREFETCH_ADDR_HIGH = 0x20,
	EDMA_REG_PREFETCH_PARAMETER = 0x24,
	EDMA_REG_ERR_ADDR_HIGH = 0x28,
	EDMA_REG_ERR_ADDR_LOW = 0x2c,
	EDMA_REG_AUX_PREFETCH_ADDR_LOW = 0x30,
	EDMA_REG_AUX_PREFETCH_ADDR_HIGH = 0x34,
	EDMA_REG_AUX_PREFETCH_PARAMETER = 0x38,
	EDMA_REG_AUX_SW_TRIG = 0x3c,
	EDMA_REG_COLONY_NUM = 0x40,
	EDMA_REG_SRC_COLONY_STRIDE = 0x44,
	EDMA_REG_DST_COLONY_STRIDE = 0x48,
	EDMA_REG_AUX_COLONY_NUM = 0x50,
	EDMA_REG_AUX_SRC_COLONY_STRIDE = 0x54,
	EDMA_REG_AUX_DST_COLONY_STRIDE = 0x58,
	EDMA_REG_REFRESH_TIMES = 0x60,
	EDMA_REG_AUX_REFRESH_TIMES = 0x64
} EDMA_REG;

struct edma_desc {
	struct npu_edma_op_desc *op;
	uint64_t src_base_addr;
	uint64_t dst_base_addr;
};

struct hw_desc_src {
	uint32_t src_addr_low;
	/* Not semantics related, Just comfort hardware arrangement */
	struct {
		uint16_t src_addr_high;
		uint16_t src_input_c0;
	};
	struct {
		uint16_t src_line_len;
		uint16_t src_line_num;
	};
	uint32_t src_line_stride;
	uint32_t src_slice_stride;
	union {
		struct {
			uint32_t src_slice_num : 16;
#define PING_PONG_NON_LINK 0x0
#define PING_PONG_NON_SEQ_LINK 0x1
/* TODO: Not really acknowledged */
#define PING_PONG_NON_SEQ_RELOAD 0x2
#define PING_PONG_NON_SEQ_REFRESH 0x4
#define NON_PING_PONG_SEQ_REFRESH_PREMETCH 0x5
#define PING_PONG_SEQ_AND_PREFETCH 0x6
			uint32_t src_link_item_link_mode : 3;
			uint32_t src_link_item_entryNum_or_reGen_times : 4;
			uint32_t rsv : 2;
			uint32_t src_link_item_supply_dim_en : 1;
			uint32_t src_data_type : 1;
			uint32_t src_link_item_op_mode_n2e : 1;
			uint32_t src_link_item_op_mode_c0 : 3;
			uint32_t src_link_item_finish_intr : 1;
		};
		uint32_t src_slice_num_link_item;
	};
} __attribute__((packed, aligned(4)));

struct hw_desc_dst {
	uint32_t dst_addr_low;
	/* Not semantics related, Just comfort hardware arrangement */
	struct {
		uint16_t dst_addr_high;
		/* n2e only */
		uint16_t dst_supply_line_len;
	};
	struct {
		uint16_t dst_line_len;
		uint16_t dst_line_num;
	};
	uint32_t dst_line_stride;
	uint32_t dst_slice_stride;
	union {
		struct {
			uint32_t dst_slice_num : 16;
			uint32_t dst_link_item_link_mode : 3;
			uint32_t dst_link_item_entryNum_or_reGen_times : 4;
			uint32_t rsv : 2;
			uint32_t dst_link_item_supply_dim_en : 1;
			uint32_t dst_data_type : 1;
			uint32_t dst_link_item_op_mode_n2e : 1;
			uint32_t dst_link_item_op_mode_c0 : 3;
			uint32_t dst_link_item_finish_intr : 1;
		};
		uint32_t dst_slice_num_link_item;
	};
} __attribute__((packed, aligned(4)));

struct hw_colony {
	union {
		struct {
			uint32_t src_colony_num : 16;
			uint32_t dst_colony_num : 16;
		};
#define COLONY_NONE (0x00010001)
		uint32_t colony_num;
	};
	uint32_t src_colony_stride;
	uint32_t dst_colony_stride;
	union {
		struct {
			uint32_t aux_src_colony_num : 16;
			uint32_t aux_dst_colony_num : 16;
		};
		uint32_t aux_colony_num;
	};
	uint32_t aux_src_colony_stride;
	uint32_t aux_dst_colony_stride;
} __attribute__((packed, aligned(4)));

struct hw_refresh {
	union {
		struct {
			uint32_t main_refresh_times : 16;
			uint32_t main_reserved : 16;
		};
		uint32_t main_refresh_times_32;
	};
	union {
		struct {
			uint32_t aux_refresh_times : 16;
			uint32_t aux_reserved : 16;
		};
		uint32_t aux_refresh_times_32;
	};
};

struct hw_desc {
	struct hw_desc_src *src;
	struct hw_desc_dst *dst;
	struct hw_colony colony;
	struct hw_refresh refresh;
	struct hw_desc *next_task;
	struct edma_desc *d;
};
int edma_init(struct nvdla_device *nvdla_dev);
void edma_free(struct nvdla_device *nvdla_dev);
#endif /* __EDMA_H_ */
