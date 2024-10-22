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
