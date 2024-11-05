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

#ifndef __DRP_REGS_H__
#define __DRP_REGS_H__

#define SDP_POST_DRP_S_BASE_ADDR 0x13a000
#define SDP_POST_DRP_D_BASE_ADDR 0x13b000

#define PDP_POST_DRP_S_BASE_ADDR 0x13c000
#define PDP_POST_DRP_D_BASE_ADDR 0x13d000

#define POST_DRP_CLK_GATING_BIT_OFFSET 24

typedef enum {
    POST_DRP_TYPE_PDP = 0,
    POST_DRP_TYPE_SDP,
} POST_DRP_TYPE;

typedef enum {
    POST_DRP_S_REG_SOFT_RESET = 0x0,
    POST_DRP_S_REG_POINTER_FLAG = 0x4,
    POST_DRP_S_REG_PP_STATUS = 0x8,
    POST_DRP_S_REG_POST_NOC_AW_CNT = 0x100,
    POST_DRP_S_REG_POST_NOC_AW_BLOCK_CNT = 0x104,
    POST_DRP_S_REG_POST_NOC_W_CNT = 0x108,
    POST_DRP_S_REG_POST_NOC_W_BLOCK_CNT = 0x10c,
    POST_DRP_S_REG_POST_NOC_W_BUBBE_CNT = 0x110,
} POST_DRP_S_REG_E;

typedef enum {
    POST_DRP_D_REG_OP_EN_TRIG = 0x00,
    POST_DRP_D_REG_OP_STATUS = 0x04,
    POST_DRP_D_REG_G_STRIDE_SRAM = 0x08,
    POST_DRP_D_REG_N_STRIDE_SRAM = 0x0c,
    POST_DRP_D_REG_H_STRIDE_SRAM = 0x10,
    POST_DRP_D_REG_C_STRIDE_SRAM = 0x14,
    POST_DRP_D_REG_H_EXT_STRIDE = 0x18,
    POST_DRP_D_REG_W_EXT_STRIDE = 0x1c,
    POST_DRP_D_REG_OMAP_PARA_RSP_H = 0x20,
    POST_DRP_D_REG_OMAP_PARA_RSP_L = 0x24,
    POST_DRP_D_REG_LAYER_PARA_L = 0x28,
    POST_DRP_D_REG_LAYER_PARA_M = 0x2c,
    POST_DRP_D_REG_LAYER_PARA_H = 0x30,
    POST_DRP_D_REG_OMAP_PARA_L = 0x34,
    POST_DRP_D_REG_BASE_ADDR_H = 0x38,
    POST_DRP_D_REG_BASE_ADDR_L = 0x3c,
    POST_DRP_D_REG_CTRL = 0x40,
    POST_DRP_D_REG_SPLIT = 0x44,
    POST_DRP_D_REG_PARTIAL_WIDTH = 0x48,
    POST_DRP_D_REG_SRAM_LOOP_PARA_H = 0x4c,
    POST_DRP_D_REG_SRAM_LOOP_PARA_L = 0x50,
} POST_DRP_D_REG_E;

#define DRP_WR_D_OP_EN_TRIG 0x0
#define DRP_WR_D_OP_STATUS 0x4

#define DRP_WR_D_CTRL 0x8

/**
 * @brief The step value in the N2 direction, n2_stride = N1 * N0 * E3 * E2 * E1 * M2 * M1 * F0 * M0,
 *        is used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_N2_STRIDE 0x10

/**
 * @brief For Task 0, the step value in the G2 direction, g2_stride = N2 * N1 * E3 * E2 * E1 * M2 * M1 * F0 * M0 * GF * GMF,
 *        is used for calculating the DRP address written by the PE. Configuration decrement by 1.
 */
#define DRP_WR_D_G2_STRIDE 0x14

/**
 * @brief The step value in the E3 direction, e3_stride = E2 * E1 * M2 * M1 * F0 * M0,
 *        is used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_E3_STRIDE 0x18

/**
 * @brief The step value in the M2 direction, m2_stride = M1 * F0 * M0,
 *        is used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_M2_STRIDE 0x1C

/**
 * @brief The step value in the M direction, m_stride = F0 * M0,
 *        is used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_M_STRIDE 0x20

/**
 * @brief G3 threshold, used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_G3_THRESHOLD 0x24

/**
 * @brief N3 threshold, used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_N3_THRESHOLD 0x28

/**
 * @brief M3 threshold, used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_M3_THRESHOLD 0x2C

/**
 * @brief E4 threshold, used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_E4_THRESHOLD 0x30

/**
 * @brief F3 threshold, used for calculating the DRP address written by the PE.
 */
#define DRP_WR_D_F3_THRESHOLD 0x34

/**
 * @brief The lower 32 bits of the base address for writing to LSRAM/DDR.
 */
#define DRP_WR_D_BA_L 0x38

/**
 * @brief The high 32 bits of the base address for writing to LSRAM/DDR.
 */
#define DRP_WR_D_BA_H 0x3C

/**
 * @brief The number of PE participating in the task computation, used for last_pkt statistics.
 *        When the number of received last_pkt pulses equals pe_num,
 *        it indicates that all PE have completed the computation of the current ofmap cube.
 */
#define DRP_WR_D_PE_NUM 0x80

/**
 * @brief The size of the ofmap output by one GLB level loop,
 *        used in the calculation of the base address for the GLB level loop.
 */
#define DRP_WR_D_SIZE_GLB 0x94

#define DRP_WR_S_SOFT_RESET 0x0
#define DRP_WR_S_POINTER_FLAG 0x4
#define DRP_WR_S_PP_STATUS 0x8

#define DRP_RESHAPE_D_OP_EN_TRIG 0x0
#define DRP_RESHAPE_D_OP_STATUS 0x4
#define DRP_RESHAPE_D_CTRL 0x8
#define DRP_RESHAPE_D_GLB_G_STRIDE 0xC
#define DRP_RESHAPE_D_GLB_N_STRIDE 0x10
#define DRP_RESHAPE_D_GLB_E_STRIDE 0x14
#define DRP_RESHAPE_D_GLB_M_STRIDE 0x18
// #define DRP_RESHAPE_D_SRAM_G_STRIDE 0x1C
#define DRP_RESHAPE_D_SRAM_N_STRIDE 0x20
#define DRP_RESHAPE_D_SRAM_H_STRIDE 0x24
#define DRP_RESHAPE_D_SRAM_C_STRIDE 0x28
#define DRP_RESHAPE_D_IMPA_PARA_L 0x2C
// #define DRP_RESHAPE_D_IMPA_PAR_H 0x30
// #define DRP_RESHAPE_D_OMAP_RSP_H 0x34
#define DRP_RESHAPE_D_OMAP_PARA_RSP_W 0x38
#define DRP_RESHAPE_D_LAYER_PARA_L 0x3C
#define DRP_RESHAPE_D_LAYER_PARA_M 0x40
#define DRP_RESHAPE_D_LAYER_PARA_H 0x44
#define DRP_RESHAPE_D_GLB_PARA_L 0x48
#define DRP_RESHAPE_D_GLB_PARA_H 0x4C
#define DRP_RESHAPE_D_GLB_LAST_PARA_L 0x50
#define DRP_RESHAPE_D_GLB_LAST_PARA_H 0x54
#define DRP_RESHAPE_D_OMAP_L 0x58
#define DRP_RESHAPE_D_BASE_ADDR_IN_L 0x5C
#define DRP_RESHAPE_D_BASE_ADDR_IN_H 0x60
#define DRP_RESHAPE_D_BASE_ADDR_OUT_L 0x64
#define DRP_RESHAPE_D_BASE_ADDR_OUT_H 0x68
#define DRP_RESHAPE_D_SIZE_GLB 0x6C
#define DRP_RESHAPE_D_PRECISON_CTRL_L 0x80
#define DRP_RESHAPE_D_PRECISON_CTRL_H 0x84

#define DRP_RESHAPE_S_SOFT_RESET 0x0
#define DRP_RESHAPE_S_POINTER_FLAG 0x4
#define DRP_RESHAPE_S_PP_STATUS 0x8

#endif
