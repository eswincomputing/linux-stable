// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __EDMA_REGS_H__
#define __EDMA_REGS_H__

#if !defined(__KERNEL__)
#define EDMA_DESP_SIZE 0x18
#define EDMA_S_BASE_ADDR 0x110000
#define SRC_DESP_BASE 0x100
#define DST_DESP_BASE 0x300
#define EDMA_DESP_STRIDE 0x20
#define REG_SRC_DESP_MAIN_BASE (EDMA_S_BASE_ADDR + SRC_DESP_BASE)
#define REG_DST_DESP_MAIN_BASE (EDMA_S_BASE_ADDR + DST_DESP_BASE)
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
    EDMA_REG_AUX_REFRESH_TIMES = 0x64,
} EDMA_REG;
#endif

/*reg index*/
enum {
    EDMA_COLONY_NUM,
    EDMA_SRC_COLONY_STRIDE,
    EDMA_DST_COLONY_STRIDE,
    EDMA_REFRESH_TIMES,
    EDMA_RAM_SRC_BASE_0,
    EDMA_RAM_SRC_BASE_1,
    EDMA_RAM_SRC_BASE_2,
    EDMA_RAM_SRC_BASE_3,
    EDMA_RAM_SRC_BASE_4,
    EDMA_RAM_SRC_BASE_5,
    EDMA_RAM_DST_BASE_0,
    EDMA_RAM_DST_BASE_1,
    EDMA_RAM_DST_BASE_2,
    EDMA_RAM_DST_BASE_3,
    EDMA_RAM_DST_BASE_4,
    EDMA_RAM_DST_BASE_5,
    EDMA_REG_MAX,
};
#endif
