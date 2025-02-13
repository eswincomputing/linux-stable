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

#ifndef __SDP_REGS_H__
#define __SDP_REGS_H__

#if !defined(__KERNEL__)
#define OFFSET_ADDRESS_NPU_NVDLA 0x150000
#ifndef _MK_ADDR_CONST
#define _MK_ADDR_CONST(_constant_) (OFFSET_ADDRESS_NPU_NVDLA + _constant_)
#endif

#define SDP_S_POINTER_0 (_MK_ADDR_CONST(0xb004))
#define SDP_RDMA_S_POINTER_0 (_MK_ADDR_CONST(0xa004))

#define SDP_D_OP_ENABLE_0 (_MK_ADDR_CONST(0xb038))
#define SDP_RDMA_D_OP_ENABLE_0 (_MK_ADDR_CONST(0xa008))

#define SDP_RDMA_D_FEATURE_MODE_CFG_0 (_MK_ADDR_CONST(0xa070))
#define SDP_RDMA_D_BRDMA_CFG_0 (_MK_ADDR_CONST(0xa028))
#define SDP_RDMA_D_NRDMA_CFG_0 (_MK_ADDR_CONST(0xa040))
#define SDP_RDMA_D_ERDMA_CFG_0 (_MK_ADDR_CONST(0xa058))
#define SDP_RDMA_D_DATA_CUBE_WIDTH_0 (_MK_ADDR_CONST(0xa00c))
#define SDP_RDMA_D_DATA_CUBE_HEIGHT_0 (_MK_ADDR_CONST(0xa010))
#define SDP_RDMA_D_DATA_CUBE_CHANNEL_0 (_MK_ADDR_CONST(0xa014))
#define SDP_RDMA_D_SRC_BASE_ADDR_LOW_0 (_MK_ADDR_CONST(0xa018))
#define SDP_RDMA_D_SRC_BASE_ADDR_HIGH_0 (_MK_ADDR_CONST(0xa01c))
#define SDP_RDMA_D_SRC_LINE_STRIDE_0 (_MK_ADDR_CONST(0xa020))
#define SDP_RDMA_D_SRC_SURFACE_STRIDE_0 (_MK_ADDR_CONST(0xa024))
#define SDP_RDMA_D_SRC_DMA_CFG_0 (_MK_ADDR_CONST(0xa074))

#define SDP_RDMA_D_BS_BASE_ADDR_LOW_0 (_MK_ADDR_CONST(0xa02c))
#define SDP_RDMA_D_BS_BASE_ADDR_HIGH_0 (_MK_ADDR_CONST(0xa030))
#define SDP_RDMA_D_BS_LINE_STRIDE_0 (_MK_ADDR_CONST(0xa034))
#define SDP_RDMA_D_BS_SURFACE_STRIDE_0 (_MK_ADDR_CONST(0xa038))
#define SDP_RDMA_D_BS_BATCH_STRIDE_0 (_MK_ADDR_CONST(0xa03c))

#define SDP_RDMA_D_BN_BASE_ADDR_LOW_0 (_MK_ADDR_CONST(0xa044))
#define SDP_RDMA_D_BN_BASE_ADDR_HIGH_0 (_MK_ADDR_CONST(0xa048))
#define SDP_RDMA_D_BN_LINE_STRIDE_0 (_MK_ADDR_CONST(0xa04c))
#define SDP_RDMA_D_BN_SURFACE_STRIDE_0 (_MK_ADDR_CONST(0xa050))
#define SDP_RDMA_D_BN_BATCH_STRIDE_0 (_MK_ADDR_CONST(0xa054))

#define SDP_RDMA_D_EW_BASE_ADDR_LOW_0 (_MK_ADDR_CONST(0xa05c))
#define SDP_RDMA_D_EW_BASE_ADDR_HIGH_0 (_MK_ADDR_CONST(0xa060))
#define SDP_RDMA_D_EW_LINE_STRIDE_0 (_MK_ADDR_CONST(0xa064))
#define SDP_RDMA_D_EW_SURFACE_STRIDE_0 (_MK_ADDR_CONST(0xa068))
#define SDP_RDMA_D_EW_BATCH_STRIDE_0 (_MK_ADDR_CONST(0xa06c))

#define SDP_D_DATA_CUBE_WIDTH_0 (_MK_ADDR_CONST(0xb03c))
#define SDP_D_DATA_CUBE_HEIGHT_0 (_MK_ADDR_CONST(0xb040))
#define SDP_D_DATA_CUBE_CHANNEL_0 (_MK_ADDR_CONST(0xb044))
#define SDP_D_DST_BASE_ADDR_HIGH_0 (_MK_ADDR_CONST(0xb04c))
#define SDP_D_DST_BASE_ADDR_LOW_0 (_MK_ADDR_CONST(0xb048))
#define SDP_D_DST_LINE_STRIDE_0 (_MK_ADDR_CONST(0xb050))
#define SDP_D_DST_SURFACE_STRIDE_0 (_MK_ADDR_CONST(0xb054))
#define SDP_D_DP_BS_CFG_0 (_MK_ADDR_CONST(0xb058))
#define SDP_D_DP_BS_ALU_CFG_0 (_MK_ADDR_CONST(0xb05c))
#define SDP_D_DP_BS_ALU_SRC_VALUE_0 (_MK_ADDR_CONST(0xb060))
#define SDP_D_DP_BS_MUL_SRC_VALUE_0 (_MK_ADDR_CONST(0xb068))
#define SDP_D_DP_BS_MUL_CFG_0 (_MK_ADDR_CONST(0xb064))
#define SDP_D_DP_BN_CFG_0 (_MK_ADDR_CONST(0xb06c))
#define SDP_D_DP_BN_ALU_CFG_0 (_MK_ADDR_CONST(0xb070))
#define SDP_D_DP_BN_ALU_SRC_VALUE_0 (_MK_ADDR_CONST(0xb074))
#define SDP_D_DP_BN_MUL_SRC_VALUE_0 (_MK_ADDR_CONST(0xb07c))
#define SDP_D_DP_BN_MUL_CFG_0 (_MK_ADDR_CONST(0xb078))
#define SDP_D_DP_EW_CFG_0 (_MK_ADDR_CONST(0xb080))
#define SDP_D_DP_EW_ALU_CFG_0 (_MK_ADDR_CONST(0xb084))
#define SDP_D_DP_EW_ALU_SRC_VALUE_0 (_MK_ADDR_CONST(0xb088))
#define SDP_D_DP_EW_ALU_CVT_OFFSET_VALUE_0 (_MK_ADDR_CONST(0xb08c))
#define SDP_D_DP_EW_ALU_CVT_SCALE_VALUE_0 (_MK_ADDR_CONST(0xb090))
#define SDP_D_DP_EW_ALU_CVT_TRUNCATE_VALUE_0 (_MK_ADDR_CONST(0xb094))
#define SDP_D_DP_EW_MUL_CFG_0 (_MK_ADDR_CONST(0xb098))
#define SDP_D_DP_EW_MUL_SRC_VALUE_0 (_MK_ADDR_CONST(0xb09c))
#define SDP_D_DP_EW_MUL_CVT_OFFSET_VALUE_0 (_MK_ADDR_CONST(0xb0a0))
#define SDP_D_DP_EW_MUL_CVT_SCALE_VALUE_0 (_MK_ADDR_CONST(0xb0a4))
#define SDP_D_DP_EW_MUL_CVT_TRUNCATE_VALUE_0 (_MK_ADDR_CONST(0xb0a8))
#define SDP_D_DP_EW_TRUNCATE_VALUE_0 (_MK_ADDR_CONST(0xb0ac))

#define SDP_D_FEATURE_MODE_CFG_0 (_MK_ADDR_CONST(0xb0b0))
#define SDP_D_DST_DMA_CFG_0 (_MK_ADDR_CONST(0xb0b4))
#define SDP_D_DST_BATCH_STRIDE_0 (_MK_ADDR_CONST(0xb0b8))
#define SDP_D_DATA_FORMAT_0 (_MK_ADDR_CONST(0xb0bc))
#define SDP_D_CVT_OFFSET_0 (_MK_ADDR_CONST(0xb0c0))
#define SDP_D_CVT_SCALE_0 (_MK_ADDR_CONST(0xb0c4))
#define SDP_D_CVT_SHIFT_0 (_MK_ADDR_CONST(0xb0c8))

// LUT register define
#define SDP_S_LUT_ACCESS_CFG_0 (_MK_ADDR_CONST(0xb008))
#define SDP_S_LUT_ACCESS_DATA_0 (_MK_ADDR_CONST(0xb00c))
#define SDP_S_LUT_CFG_0 (_MK_ADDR_CONST(0xb010))
#define SDP_S_LUT_INFO_0 (_MK_ADDR_CONST(0xb014))
#define SDP_S_LUT_LE_START_0 (_MK_ADDR_CONST(0xb018))
#define SDP_S_LUT_LE_END_0 (_MK_ADDR_CONST(0xb01c))
#define SDP_S_LUT_LO_START_0 (_MK_ADDR_CONST(0xb020))
#define SDP_S_LUT_LO_END_0 (_MK_ADDR_CONST(0xb024))
#define SDP_S_LUT_LE_SLOPE_SCALE_0 (_MK_ADDR_CONST(0xb028))
#define SDP_S_LUT_LO_SLOPE_SCALE_0 (_MK_ADDR_CONST(0xb030))
#define SDP_S_LUT_LE_SLOPE_SHIFT_0 (_MK_ADDR_CONST(0xb02c))
#define SDP_S_LUT_LO_SLOPE_SHIFT_0 (_MK_ADDR_CONST(0xb034))

#define LUT_LINEAR_EXP_TABLE_ENTRY_LOG2 6
#define SDP_S_LUT_ACCESS_DATA_OFFSET (_MK_ADDR_CONST(0x0004))

static const u32 SWITCH_RAW_TAB = 0x20000;
static const u32 SWITCH_DENSTIY_TAB = 0x30000;
#endif
/*reg index*/
enum {
    // all values should be configured. default as 0x0;
    // should post-drp be included?
    D_DATA_CUBE_WIDTH,
    D_DATA_CUBE_HEIGHT,
    D_DATA_CUBE_CHANNEL,
    D_DST_BASE_ADDR_HIGH,
    D_DST_BASE_ADDR_LOW,
    D_DST_LINE_STRIDE,
    D_DST_SURFACE_STRIDE,
    D_DP_BS_CFG,
    D_DP_BS_ALU_CFG,
    D_DP_BS_ALU_SRC_VALUE,
    D_DP_BS_MUL_SRC_VALUE,
    D_DP_BS_MUL_CFG,
    D_DP_BN_CFG,
    D_DP_BN_ALU_CFG,
    D_DP_BN_ALU_SRC_VALUE,
    D_DP_BN_MUL_SRC_VALUE,
    D_DP_BN_MUL_CFG,
    D_DP_EW_CFG,
    D_DP_EW_ALU_CFG,
    D_DP_EW_ALU_SRC_VALUE,
    D_DP_EW_ALU_CVT_OFFSET_VALUE,
    D_DP_EW_ALU_CVT_SCALE_VALUE,
    D_DP_EW_ALU_CVT_TRUNCATE_VALUE,
    D_DP_EW_MUL_CFG,
    D_DP_EW_MUL_SRC_VALUE,
    D_DP_EW_MUL_CVT_OFFSET_VALUE,
    D_DP_EW_MUL_CVT_SCALE_VALUE,
    D_DP_EW_MUL_CVT_TRUNCATE_VALUE,
    D_DP_EW_TRUNCATE_VALUE,
    D_FEATURE_MODE_CFG,
    D_DST_DMA_CFG,
    D_DST_BATCH_STRIDE,
    D_DATA_FORMAT,
    D_CVT_OFFSET,
    D_CVT_SCALE,
    D_CVT_SHIFT,
    SDP_REG_MAX,
};

enum {
    // 15 regs for sdp_post_drp
    DRP_D_REG_G_STRIDE_SRAM = 0,
    DRP_D_REG_N_STRIDE_SRAM,
    DRP_D_REG_H_STRIDE_SRAM,
    DRP_D_REG_C_STRIDE_SRAM,
    DRP_D_REG_W_EXT_STRIDE,
    DRP_D_REG_LAYER_PARA_L,
    DRP_D_REG_LAYER_PARA_H,
    DRP_D_REG_OMAP_PARA_L,
    DRP_D_REG_CTRL,
    DRP_D_REG_SPLIT,
    DRP_D_REG_PARTIAL_WIDTH,
    DRP_D_REG_SRAM_LOOP_PARA_H,
    DRP_D_REG_SRAM_LOOP_PARA_L,
    DRP_D_REG_BASE_ADDR_H,
    DRP_D_REG_BASE_ADDR_L,
    DRP_D_REG_OP_EN_TRIG,
    POST_DRP_REG_MAX,
};

// sdp_rdma
enum {
    SDP_RDMA_D_DATA_CUBE_WIDTH,
    SDP_RDMA_D_DATA_CUBE_HEIGHT,
    SDP_RDMA_D_DATA_CUBE_CHANNEL,
    SDP_RDMA_D_SRC_BASE_ADDR_LOW,
    SDP_RDMA_D_SRC_BASE_ADDR_HIGH,
    SDP_RDMA_D_SRC_LINE_STRIDE,
    SDP_RDMA_D_SRC_SURFACE_STRIDE,
    SDP_RDMA_D_BRDMA_CFG,
    SDP_RDMA_D_NRDMA_CFG,
    SDP_RDMA_D_BS_BASE_ADDR_LOW,
    SDP_RDMA_D_BS_BASE_ADDR_HIGH,
    SDP_RDMA_D_BS_LINE_STRIDE,
    SDP_RDMA_D_BS_SURFACE_STRIDE,
    SDP_RDMA_D_BS_BATCH_STRIDE,
    SDP_RDMA_D_BN_BASE_ADDR_LOW,
    SDP_RDMA_D_BN_BASE_ADDR_HIGH,
    SDP_RDMA_D_BN_LINE_STRIDE,
    SDP_RDMA_D_BN_SURFACE_STRIDE,
    SDP_RDMA_D_BN_BATCH_STRIDE,
    SDP_RDMA_D_ERDMA_CFG,
    SDP_RDMA_D_EW_BASE_ADDR_LOW,
    SDP_RDMA_D_EW_BASE_ADDR_HIGH,
    SDP_RDMA_D_EW_LINE_STRIDE,
    SDP_RDMA_D_EW_SURFACE_STRIDE,
    SDP_RDMA_D_EW_BATCH_STRIDE,
    SDP_RDMA_D_FEATURE_MODE_CFG,
    SDP_RDMA_D_SRC_DMA_CFG,
    SDP_RDMA_REG_MAX,
};
#endif
