/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/
#ifndef _DWE_REGS_H_
#define _DWE_REGS_H_

#define REGISTER_NUM 100
#define DEWARP_REGISTER_BASE_ADDR 0x00000C00
#define DEWARP_CTRL 0x00000004
#define SWAP_CONTROL 0x00000058
#define VERTICAL_SPLIT_LINE 0x0000005C
#define HORIZON_SPLIT_LINE 0x00000060
#define SCALE_FACTOR 0x00000064
#define ROI_START 0x00000068
#define BOUNDRY_PIXEL 0x0000006C
#define INTERRUPT_STATUS 0x00000070

#define INT_FRAME_DONE (1 << 0)
#define INT_ERR_STATUS_MASK 0x000000FE
#define INT_ERR_STATUS_SHIFT 1
#define INT_MSK_STATUS_MASK 0x0000FF00
#define INT_MSK_STATUS_SHIFT 8
#define INT_FRAME_BUSY 0x00010000

#ifdef HAL_CMODEL
#define INT_CLR_MASK 0x00007F00
#else
#define INT_CLR_MASK 0x00000100
#endif

#define INT_RESET_MASK 0x01000000
#define BUS_CTRL 0x00000074
#define DEWRAP_BUS_CTRL_ENABLE_MASK (1U << 31)

//register mask
#define DWE_CLR_STATUS 0xff000000
#define DWE_FRAME_BUSY 0x00010000
#define DWE_INT_MASK_STATUS 0x0000ff00
#define DWE_INT_STATUS 0x000000ff

#define DWE_START
#define DWE_START_MASK 0x00000002U
#define DWE_START_SHIFT 1U

#define DWE_BUS_SW_EN
#define DWE_BUS_SW_EN_MASK 0x80000000U
#define DWE_BUS_SW_EN_SHIFT 31U

#define DWE_RD_ID_EN
#define DWE_RD_ID_EN_MASK 0x40000000U
#define DWE_RD_ID_EN_SHIFT 30U

#define DWE_RD_ID_CFG
#define DWE_RD_ID_CFG_MASK 0x0000FF00U
#define DWE_RD_ID_CFG_SHIFT 8U

#define DWE_WR_ID_CFG
#define DWE_WR_ID_CFG_MASK 0x00FF0000U
#define DWE_WR_ID_CFG_SHIFT 16U

#define BUS_CTRL1 0x00000078
#define BUS_TIME_OUT_CYCLE 0x0000007C
#define MAP_LUT_ADDR 0x00000008
#define MAP_LUT_SIZE 0x0000000C
#define SRC_IMG_Y_BASE 0x00000010
#define SRC_IMG_UV_BASE 0x00000014
#define SRC_IMG_SIZE 0x00000018
#define SRC_IMG_STRIDE 0x0000001C
#define MAP_LUT_ADDR2 0x00000020
#define MAP_LUT_SIZE2 0x00000024
#define SRC_IMG_Y_BASE2 0x00000028
#define SRC_IMG_UV_BASE2 0x0000002C
#define SRC_IMG_SIZE2 0x00000030
#define SRC_IMG_STRIDE2 0x00000034
#define DST_IMG_Y_BASE 0x00000038
#define DST_IMG_UV_BASE 0x0000003C
#define DST_IMG_SIZE 0x00000040
#define DST_IMG_STRIDE 0x00000044
#define DST_IMG_Y_BASE2 0x00000048
#define DST_IMG_UV_BASE2 0x0000004C
#define DST_IMG_SIZE2 0x00000050
#define DST_IMG_STRIDE2 0x00000054
#define DST_IMG_Y_SIZE1 0x00000080
#define DST_IMG_UV_SIZE1 0x00000084
#define DST_IMG_Y_SIZE2 0x00000088
#define DST_IMG_UV_SIZE2 0x0000008C
#define DST_IMG_WIDTH_BN 0x0000009C

#define DW200_REG_FE_CTL 0x00000D00
#define DW200_REG_FE_DMA_START 0x00000D04
#define DW200_REG_FE_DMA_AD 0x00000D08
#define DW200_REG_FE_IMSC 0x00000D0C
#define DW200_REG_FE_ISR 0x00000D10
#define DW200_REG_FE_MIS 0x00000D14
#define DW200_REG_FE_ICR 0x00000D18
#define DW200_REG_FE_RIS 0x00000D1C
#define DW200_REG_FE_ADDR_INTERVENE 0x00000D20

#ifndef REG_GET_MASK
/* =========================================================================================*/
/*! returns the value of slice \a name from register or variable \a reg
 * \note "parameter" \a reg could be a hardware register or a (32bit) variable, but not a pointer! \n
 *       each slice (specified as "parameter" \a name) requires two \#defines: \n
 *        - <tt>\<name\>_MASK  </tt>: defines the mask to use on register side
 *        - <tt>\<name\>_SHIFT </tt>: defines the shift value to use (left on write, right on read)
 */
#define REG_GET_SLICE(reg, name) (((reg) & (name##_MASK)) >> (name##_SHIFT))
/* =========================================================================================*/
/*! writes the value \a value into slice \a name of register or variable \a reg
 * \note "parameter" \a reg could be a hardware register or a (32bit) variable, but not a pointer! \n
 *       each slice (specified as "parameter" \a name) requires two \#defines: \n
 *        - <tt>\<name\>_MASK  </tt>: defines the mask to use on register side
 *        - <tt>\<name\>_SHIFT </tt>: defines the shift value to use (left on write, right on read)
 */
#define REG_SET_SLICE(reg, name, value)                                    \
	{                                                                  \
		((reg) = (((reg) & ~(name##_MASK)) |                       \
			  (((value) << (name##_SHIFT)) & (name##_MASK)))); \
	}
#endif

#endif /* _DWE_REGS_H_ */
