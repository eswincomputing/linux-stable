// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
 * @brief N2方向的步进值，n2_stride=N1*N0*E3*E2*E1*M2*M1*F0*M0，用于PE写DRP地址计算.
 *
 */
#define DRP_WR_D_N2_STRIDE 0x10
/**
 * @brief 任务0，G2方向的步进值，g2_stride=N2*N1*E3*E2*E1*M2*M1*F0*M0*GF*GMF，用于PE写DRP地址计算。配置减1
 *
 */
#define DRP_WR_D_G2_STRIDE 0x14
/**
 * @brief E3方向的步进值，e3_stride=E2*E1*M2*M1*F0*M0，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_E3_STRIDE 0x18
/**
 * @brief M2方向的步进值，m2_stride=M1*F0*M0，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_M2_STRIDE 0x1C
/**
 * @brief M方向的步进值，m_stride=F0*M0，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_M_STRIDE 0x20
/**
 * @brief G3阈值，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_G3_THRESHOLD 0x24
/**
 * @brief N3阈值，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_N3_THRESHOLD 0x28
/**
 * @brief M3阈值，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_M3_THRESHOLD 0x2C
/**
 * @brief E4阈值，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_E4_THRESHOLD 0x30
/**
 * @brief F3阈值，用于PE写DRP地址计算
 *
 */
#define DRP_WR_D_F3_THRESHOLD 0x34
/**
 * @brief 写入lsram/ddr的基地址低32bit
 *
 */
#define DRP_WR_D_BA_L 0x38
/**
 * @brief 写入lsram/ddr的基地址高32bit
 *
 */
#define DRP_WR_D_BA_H 0x3C
/**
 * @brief 参与task计算的PE数量，用于last_pkt统计，当收到的last_pkt脉冲数等于pe_num则认为所有PE均完成当前ofmap cube的计算
 *
 */
#define DRP_WR_D_PE_NUM 0x80
/**
 * @brief task一个glb level loop输出的ofmap的大小，用与glb level loop的基地址计算
 *
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
