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

#ifndef POST_DRP_H
#define POST_DRP_H

#include "dla_interface.h"

#define SDP_POST_DRP_S_BASE_ADDR 0x13a000
#define SDP_POST_DRP_D_BASE_ADDR 0x13b000

#define PDP_POST_DRP_S_BASE_ADDR 0x13c000
#define PDP_POST_DRP_D_BASE_ADDR 0x13d000

#define POST_DRP_CLK_GATING_BIT_OFFSET 24

typedef enum { POST_DRP_TYPE_PDP = 1, POST_DRP_TYPE_SDP } POST_DRP_TYPE;

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

#endif
