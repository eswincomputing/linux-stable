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

#ifndef __RUBIK_REGS_H__
#define __RUBIK_REGS_H__

#if !defined(__KERNEL__)
#define OFFSET_ADDRESS_NPU_NVDLA 0x150000
#ifndef _MK_ADDR_CONST
#define _MK_ADDR_CONST(_constant_) (OFFSET_ADDRESS_NPU_NVDLA + _constant_)
#endif

#define RBK_S_POINTER_0 (_MK_ADDR_CONST(0x10004))

#define RBK_D_MISC_CFG_0 (_MK_ADDR_CONST(0x1000c))
#define RBK_D_DAIN_RAM_TYPE_0 (_MK_ADDR_CONST(0x10010))
#define RBK_D_DATAIN_SIZE_0_0 (_MK_ADDR_CONST(0x10014))
#define RBK_D_DATAIN_SIZE_1_0 (_MK_ADDR_CONST(0x10018))
#define RBK_D_DAIN_ADDR_LOW_0 (_MK_ADDR_CONST(0x10020))
#define RBK_D_DAIN_ADDR_HIGH_0 (_MK_ADDR_CONST(0x1001c))
#define RBK_D_DAIN_PLANAR_STRIDE_0 (_MK_ADDR_CONST(0x1002c))
#define RBK_D_DAIN_SURF_STRIDE_0 (_MK_ADDR_CONST(0x10028))
#define RBK_D_DAIN_LINE_STRIDE_0 (_MK_ADDR_CONST(0x10024))
#define RBK_D_DAOUT_RAM_TYPE_0 (_MK_ADDR_CONST(0x10030))
#define RBK_D_DATAOUT_SIZE_1_0 (_MK_ADDR_CONST(0x10034))
#define RBK_D_DAOUT_ADDR_LOW_0 (_MK_ADDR_CONST(0x1003c))
#define RBK_D_DAOUT_ADDR_HIGH_0 (_MK_ADDR_CONST(0x10038))
#define RBK_D_DAOUT_LINE_STRIDE_0 (_MK_ADDR_CONST(0x10040))
#define RBK_D_DAOUT_SURF_STRIDE_0 (_MK_ADDR_CONST(0x1004c))
#define RBK_D_CONTRACT_STRIDE_0_0 (_MK_ADDR_CONST(0x10044))
#define RBK_D_CONTRACT_STRIDE_1_0 (_MK_ADDR_CONST(0x10048))
#define RBK_D_DECONV_STRIDE_0 (_MK_ADDR_CONST(0x10054))
#define RBK_D_DAOUT_PLANAR_STRIDE_0 (_MK_ADDR_CONST(0x10050))

#define RBK_D_OP_ENABLE_0 (_MK_ADDR_CONST(0x10008))
#endif

/*reg index*/
enum {
    D_MISC_CFG,
    D_DAIN_RAM_TYPE,
    D_DATAIN_SIZE_0,
    D_DATAIN_SIZE_1,
    D_DAIN_ADDR_LOW,
    D_DAIN_ADDR_HIGH,
    D_DAIN_PLANAR_STRIDE,
    D_DAIN_SURF_STRIDE,
    D_DAIN_LINE_STRIDE,
    D_DAOUT_RAM_TYPE,
    D_DATAOUT_SIZE_1,
    D_DAOUT_ADDR_LOW,
    D_DAOUT_ADDR_HIGH,
    D_DAOUT_LINE_STRIDE,
    D_DAOUT_SURF_STRIDE,
    D_CONTRACT_STRIDE_0,
    D_CONTRACT_STRIDE_1,
    D_DECONV_STRIDE,
    D_DAOUT_PLANAR_STRIDE,
    RUBIK_REG_MAX,
};
#endif
