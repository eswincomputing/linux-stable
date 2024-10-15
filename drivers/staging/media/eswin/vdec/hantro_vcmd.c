// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 *    The MIT License (MIT)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a
 *    copy of this software and associated documentation files (the "Software"),
 *    to deal in the Software without restriction, including without limitation
 *    the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *    and/or sell copies of the Software, and to permit persons to whom the
 *    Software is furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 *    The GPL License (GPL)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version 2
 *    of the License, or (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *****************************************************************************
 *
 *    Note: This software is released under dual MIT and GPL licenses. A
 *    recipient may use this file under the terms of either the MIT license or
 *    GPL License. If you wish to use only one license not the other, you can
 *    indicate your decision by deleting one of the above license notices
 *    in your version of this file.
 *
 *****************************************************************************
 */

#include <linux/kernel.h>
#include <linux/module.h>
/* needed for __init,__exit directives */
#include <linux/init.h>
/* needed for remap_page_range
 * SetPageReserved
 * ClearPageReserved
 */
#include <linux/mm.h>
/* obviously, for kmalloc */
#include <linux/slab.h>
/* for struct file_operations, register_chrdev() */
#include <linux/fs.h>
/* standard error codes */
#include <linux/errno.h>

#include <linux/moduleparam.h>
/* request_irq(), free_irq() */
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <linux/semaphore.h>
#include <linux/spinlock.h>
/* needed for virt_to_phys() */
#include <asm/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>

#include <asm/irq.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

/* our own stuff */
#include <linux/platform_device.h>
#include "vcmdswhwregisters.h"
#include "bidirect_list.h"
#include "vcmdswhwregisters.h"
#include "hantrovcmd.h"
#include "subsys.h"
#include "hantroaxife.h"

#define LOG_TAG DEC_DEV_NAME ":vc"
#include "vc_drv_log.h"
/*
 * Macros to help debugging
 */

#undef PDEBUG /* undef it, just in case */
#ifdef HANTRO_VCMD_DRIVER_DEBUG
#ifdef __KERNEL__
/* This one if debugging is on, and kernel space */
#define PDEBUG(fmt, args...) pr_info("es_vdec_vcmd: " fmt, ##args)
#else
/* This one for user space */
#define PDEBUG(fmt, args...) printf(__FILE__ ":%d: " fmt, __LINE__, ##args)
#endif
#else
#define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

/*********VCMD CONFIGURATION BY CUSTOMER*********/

/* video encoder vcmd configuration */

/*customer specify according to own platform*/
#define VCMD_ENC_IO_ADDR_0              0x90000
#define VCMD_ENC_IO_SIZE_0    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_ENC_INT_PIN_0              -1
#define VCMD_ENC_MODULE_TYPE_0          0
/*customer specify according to own platform*/
#define VCMD_ENC_MODULE_MAIN_ADDR_0     0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_DEC400_ADDR_0   0XFFFF
#define VCMD_ENC_MODULE_L2CACHE_ADDR_0  0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_0      0XFFFF

/*customer specify according to own platform*/
#define VCMD_ENC_IO_ADDR_1              0x91000
#define VCMD_ENC_IO_SIZE_1    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_ENC_INT_PIN_1             -1
#define VCMD_ENC_MODULE_TYPE_1          0
/*customer specify according to own platform*/
#define VCMD_ENC_MODULE_MAIN_ADDR_1     0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_DEC400_ADDR_1   0XFFFF
#define VCMD_ENC_MODULE_L2CACHE_ADDR_1  0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_1      0XFFFF

/*customer specify according to own platform*/
#define VCMD_ENC_IO_ADDR_2              0x92000
#define VCMD_ENC_IO_SIZE_2    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_ENC_INT_PIN_2             -1
#define VCMD_ENC_MODULE_TYPE_2         0
/*customer specify according to own platform*/
#define VCMD_ENC_MODULE_MAIN_ADDR_2    0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_DEC400_ADDR_2  0XFFFF
#define VCMD_ENC_MODULE_L2CACHE_ADDR_2 0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_2     0XFFFF

/*customer specify according to own platform*/
#define VCMD_ENC_IO_ADDR_3             0x93000
#define VCMD_ENC_IO_SIZE_3       (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_ENC_INT_PIN_3             -1
#define VCMD_ENC_MODULE_TYPE_3         0
/*customer specify according to own platform*/
#define VCMD_ENC_MODULE_MAIN_ADDR_3     0x0000
#define VCMD_ENC_MODULE_DEC400_ADDR_3   0XFFFF
/*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_L2CACHE_ADDR_3  0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_3      0XFFFF

/* video encoder cutree/IM  vcmd configuration */

/*customer specify according to own platform*/
#define VCMD_IM_IO_ADDR_0               0xa0000
#define VCMD_IM_IO_SIZE_0   (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_IM_INT_PIN_0               -1
#define VCMD_IM_MODULE_TYPE_0           1
/*customer specify according to own platform*/
#define VCMD_IM_MODULE_MAIN_ADDR_0      0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_DEC400_ADDR_0    0XFFFF
#define VCMD_IM_MODULE_L2CACHE_ADDR_0   0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_0       0XFFFF

/*customer specify according to own platform*/
#define VCMD_IM_IO_ADDR_1               0xa1000
#define VCMD_IM_IO_SIZE_1   (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_IM_INT_PIN_1              -1
#define VCMD_IM_MODULE_TYPE_1           1
/*customer specify according to own platform*/
#define VCMD_IM_MODULE_MAIN_ADDR_1       0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_DEC400_ADDR_1     0XFFFF
#define VCMD_IM_MODULE_L2CACHE_ADDR_1    0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_1        0XFFFF

/*customer specify according to own platform*/
#define VCMD_IM_IO_ADDR_2                0xa2000
#define VCMD_IM_IO_SIZE_2    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_IM_INT_PIN_2                -1
#define VCMD_IM_MODULE_TYPE_2            1
/*customer specify according to own platform*/
#define VCMD_IM_MODULE_MAIN_ADDR_2       0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_DEC400_ADDR_2     0XFFFF
#define VCMD_IM_MODULE_L2CACHE_ADDR_2    0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_2        0XFFFF

/*customer specify according to own platform*/
#define VCMD_IM_IO_ADDR_3              0xa3000
#define VCMD_IM_IO_SIZE_3     (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_IM_INT_PIN_3               -1
#define VCMD_IM_MODULE_TYPE_3           1
/*customer specify according to own platform*/
#define VCMD_IM_MODULE_MAIN_ADDR_3      0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_DEC400_ADDR_3    0XFFFF
#define VCMD_IM_MODULE_L2CACHE_ADDR_3   0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_3       0XFFFF

//video decoder vcmd configuration

/*customer specify according to own platform*/
#define VCMD_DEC_IO_ADDR_0               0x600000
#define VCMD_DEC_IO_SIZE_0   (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_DEC_INT_PIN_0              -1
#define VCMD_DEC_MODULE_TYPE_0           2
/*customer specify according to own platform*/
#define VCMD_DEC_MODULE_MAIN_ADDR_0      0x1000
/*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_DEC400_ADDR_0    0XFFFF
#define VCMD_DEC_MODULE_L2CACHE_ADDR_0   0X2000
#define VCMD_DEC_MODULE_MMU_ADDR_0       0XFFFF

/*customer specify according to own platform*/
#define VCMD_DEC_IO_ADDR_1               0x700000
#define VCMD_DEC_IO_SIZE_1        (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_DEC_INT_PIN_1                -1
#define VCMD_DEC_MODULE_TYPE_1            2
/*customer specify according to own platform*/
#define VCMD_DEC_MODULE_MAIN_ADDR_1       0x1000
/*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_DEC400_ADDR_1     0XFFFF
#define VCMD_DEC_MODULE_L2CACHE_ADDR_1    0X2000
#define VCMD_DEC_MODULE_MMU_ADDR_1        0XFFFF

/*customer specify according to own platform*/
#define VCMD_DEC_IO_ADDR_2                0xb2000
#define VCMD_DEC_IO_SIZE_2 (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_DEC_INT_PIN_2                -1
#define VCMD_DEC_MODULE_TYPE_2            2
/*customer specify according to own platform*/
#define VCMD_DEC_MODULE_MAIN_ADDR_2       0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_DEC400_ADDR_2     0XFFFF
#define VCMD_DEC_MODULE_L2CACHE_ADDR_2    0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_2        0XFFFF

/*customer specify according to own platform*/
#define VCMD_DEC_IO_ADDR_3                0xb3000
#define VCMD_DEC_IO_SIZE_3       (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_DEC_INT_PIN_3                -1
#define VCMD_DEC_MODULE_TYPE_3            2
/*customer specify according to own platform*/
#define VCMD_DEC_MODULE_MAIN_ADDR_3       0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_DEC400_ADDR_3     0XFFFF
#define VCMD_DEC_MODULE_L2CACHE_ADDR_3    0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_3        0XFFFF

/* JPEG encoder vcmd configuration */

/*customer specify according to own platform*/
#define VCMD_JPEGE_IO_ADDR_0              0x90000
#define VCMD_JPEGE_IO_SIZE_0      (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGE_INT_PIN_0              -1
#define VCMD_JPEGE_MODULE_TYPE_0          3
/*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_MAIN_ADDR_0     0x1000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_0   0XFFFF
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_0  0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_0      0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGE_IO_ADDR_1              0xC1000
#define VCMD_JPEGE_IO_SIZE_1    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGE_INT_PIN_1              -1
#define VCMD_JPEGE_MODULE_TYPE_1          3
/*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_MAIN_ADDR_1     0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_1    0XFFFF
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_1   0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_1       0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGE_IO_ADDR_2               0xC2000
#define VCMD_JPEGE_IO_SIZE_2   (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGE_INT_PIN_2               -1
#define VCMD_JPEGE_MODULE_TYPE_2           3
/*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_MAIN_ADDR_2      0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_2    0XFFFF
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_2   0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_2       0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGE_IO_ADDR_3               0xC3000
#define VCMD_JPEGE_IO_SIZE_3    (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGE_INT_PIN_3               -1
#define VCMD_JPEGE_MODULE_TYPE_3            3
/*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_MAIN_ADDR_3        0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_3      0XFFFF
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_3     0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_3         0XFFFF

/* JPEG decoder vcmd configuration */

/*customer specify according to own platform*/
#define VCMD_JPEGD_IO_ADDR_0                 0x600000
#define VCMD_JPEGD_IO_SIZE_0   (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGD_INT_PIN_0                 -1
#define VCMD_JPEGD_MODULE_TYPE_0              4
/*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_MAIN_ADDR_0         0x1000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_0       0XFFFF
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_0      0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_0          0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGD_IO_ADDR_1                  0xD1000
#define VCMD_JPEGD_IO_SIZE_1 (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGD_INT_PIN_1                  -1
#define VCMD_JPEGD_MODULE_TYPE_1              4
/*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_MAIN_ADDR_1         0x0000
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_1       0XFFFF
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_1      0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_1          0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGD_IO_ADDR_2                  0xD2000
#define VCMD_JPEGD_IO_SIZE_2 (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGD_INT_PIN_2                  -1
#define VCMD_JPEGD_MODULE_TYPE_2               4
/*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_MAIN_ADDR_2          0x0000
#define VCMD_JPEGD_MODULE_DEC400_ADDR_2        0XFFFF
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_2       0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_2           0XFFFF

/*customer specify according to own platform*/
#define VCMD_JPEGD_IO_ADDR_3                   0xD3000
#define VCMD_JPEGD_IO_SIZE_3      (ASIC_VCMD_SWREG_AMOUNT * 4) /* bytes */
#define VCMD_JPEGD_INT_PIN_3                   -1
#define VCMD_JPEGD_MODULE_TYPE_3                4
/*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_MAIN_ADDR_3            0x0000
#define VCMD_JPEGD_MODULE_DEC400_ADDR_3          0XFFFF
/*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_3              0XFFFF

#ifndef CONFIG_RISCV_UNCACHED_OFFSET
#define CONFIG_RISCV_UNCACHED_OFFSET 0xc000000000
#endif

#define VCMD_SYSTEM_PORT_OFFSET (CONFIG_RISCV_UNCACHED_OFFSET - 0x80000000)

/*for all vcmds, the core info should be listed here for subsequent use*/
struct vcmd_config vcmd_core_array[MAX_SUBSYS_NUM] = {
#if 0
	//encoder configuration
	{VCMD_ENC_IO_ADDR_0,
	VCMD_ENC_IO_SIZE_0,
	VCMD_ENC_INT_PIN_0,
	VCMD_ENC_MODULE_TYPE_0,
	VCMD_ENC_MODULE_MAIN_ADDR_0,
	VCMD_ENC_MODULE_DEC400_ADDR_0,
	VCMD_ENC_MODULE_L2CACHE_ADDR_0,
	VCMD_ENC_MODULE_MMU_ADDR_0},

	{VCMD_ENC_IO_ADDR_1,
	VCMD_ENC_IO_SIZE_1,
	VCMD_ENC_INT_PIN_1,
	VCMD_ENC_MODULE_TYPE_1,
	VCMD_ENC_MODULE_MAIN_ADDR_1,
	VCMD_ENC_MODULE_DEC400_ADDR_1,
	VCMD_ENC_MODULE_L2CACHE_ADDR_1,
	VCMD_ENC_MODULE_MMU_ADDR_1},

	{VCMD_ENC_IO_ADDR_2,
	VCMD_ENC_IO_SIZE_2,
	VCMD_ENC_INT_PIN_2,
	VCMD_ENC_MODULE_TYPE_2,
	VCMD_ENC_MODULE_MAIN_ADDR_2,
	VCMD_ENC_MODULE_DEC400_ADDR_2,
	VCMD_ENC_MODULE_L2CACHE_ADDR_2,
	VCMD_ENC_MODULE_MMU_ADDR_2},

	{VCMD_ENC_IO_ADDR_3,
	VCMD_ENC_IO_SIZE_3,
	VCMD_ENC_INT_PIN_3,
	VCMD_ENC_MODULE_TYPE_3,
	VCMD_ENC_MODULE_MAIN_ADDR_3,
	VCMD_ENC_MODULE_DEC400_ADDR_3,
	VCMD_ENC_MODULE_L2CACHE_ADDR_3,
	VCMD_ENC_MODULE_MMU_ADDR_3},

	//cutree/IM configuration

	{VCMD_IM_IO_ADDR_0,
	VCMD_IM_IO_SIZE_0,
	VCMD_IM_INT_PIN_0,
	VCMD_IM_MODULE_TYPE_0,
	VCMD_IM_MODULE_MAIN_ADDR_0,
	VCMD_IM_MODULE_DEC400_ADDR_0,
	VCMD_IM_MODULE_L2CACHE_ADDR_0,
	VCMD_IM_MODULE_MMU_ADDR_0},

	{VCMD_IM_IO_ADDR_1,
	VCMD_IM_IO_SIZE_1,
	VCMD_IM_INT_PIN_1,
	VCMD_IM_MODULE_TYPE_1,
	VCMD_IM_MODULE_MAIN_ADDR_1,
	VCMD_IM_MODULE_DEC400_ADDR_1,
	VCMD_IM_MODULE_L2CACHE_ADDR_1,
	VCMD_IM_MODULE_MMU_ADDR_1},

	{VCMD_IM_IO_ADDR_2,
	VCMD_IM_IO_SIZE_2,
	VCMD_IM_INT_PIN_2,
	VCMD_IM_MODULE_TYPE_2,
	VCMD_IM_MODULE_MAIN_ADDR_2,
	VCMD_IM_MODULE_DEC400_ADDR_2,
	VCMD_IM_MODULE_L2CACHE_ADDR_2,
	VCMD_IM_MODULE_MMU_ADDR_2},

	{VCMD_IM_IO_ADDR_3,
	VCMD_IM_IO_SIZE_3,
	VCMD_IM_INT_PIN_3,
	VCMD_IM_MODULE_TYPE_3,
	VCMD_IM_MODULE_MAIN_ADDR_3,
	VCMD_IM_MODULE_DEC400_ADDR_3,
	VCMD_IM_MODULE_L2CACHE_ADDR_3,
	VCMD_IM_MODULE_MMU_ADDR_3},
#endif

	//decoder configuration
	{VCMD_DEC_IO_ADDR_0,
	VCMD_DEC_IO_SIZE_0,
	VCMD_DEC_INT_PIN_0,
	VCMD_DEC_MODULE_TYPE_0,
	VCMD_DEC_MODULE_MAIN_ADDR_0,
	VCMD_DEC_MODULE_DEC400_ADDR_0,
	VCMD_DEC_MODULE_L2CACHE_ADDR_0,
	VCMD_DEC_MODULE_MMU_ADDR_0},

	{VCMD_DEC_IO_ADDR_1,
	VCMD_DEC_IO_SIZE_1,
	VCMD_DEC_INT_PIN_1,
	VCMD_DEC_MODULE_TYPE_1,
	VCMD_DEC_MODULE_MAIN_ADDR_1,
	VCMD_DEC_MODULE_DEC400_ADDR_1,
	VCMD_DEC_MODULE_L2CACHE_ADDR_1,
	VCMD_DEC_MODULE_MMU_ADDR_1},

#if 0
	{VCMD_DEC_IO_ADDR_2,
	VCMD_DEC_IO_SIZE_2,
	VCMD_DEC_INT_PIN_2,
	VCMD_DEC_MODULE_TYPE_2,
	VCMD_DEC_MODULE_MAIN_ADDR_2,
	VCMD_DEC_MODULE_DEC400_ADDR_2,
	VCMD_DEC_MODULE_L2CACHE_ADDR_2,
	VCMD_DEC_MODULE_MMU_ADDR_2},

	{VCMD_DEC_IO_ADDR_3,
	VCMD_DEC_IO_SIZE_3,
	VCMD_DEC_INT_PIN_3,
	VCMD_DEC_MODULE_TYPE_3,
	VCMD_DEC_MODULE_MAIN_ADDR_3,
	VCMD_DEC_MODULE_DEC400_ADDR_3,
	VCMD_DEC_MODULE_L2CACHE_ADDR_3,
	VCMD_DEC_MODULE_MMU_ADDR_3},
#endif
#if 0
	//JPEG encoder configuration
	{VCMD_JPEGE_IO_ADDR_0,
	VCMD_JPEGE_IO_SIZE_0,
	VCMD_JPEGE_INT_PIN_0,
	VCMD_JPEGE_MODULE_TYPE_0,
	VCMD_JPEGE_MODULE_MAIN_ADDR_0,
	VCMD_JPEGE_MODULE_DEC400_ADDR_0,
	VCMD_JPEGE_MODULE_L2CACHE_ADDR_0,
	VCMD_JPEGE_MODULE_MMU_ADDR_0},
#endif
#if 0
	{VCMD_JPEGE_IO_ADDR_1,
	VCMD_JPEGE_IO_SIZE_1,
	VCMD_JPEGE_INT_PIN_1,
	VCMD_JPEGE_MODULE_TYPE_1,
	VCMD_JPEGE_MODULE_MAIN_ADDR_1,
	VCMD_JPEGE_MODULE_DEC400_ADDR_1,
	VCMD_JPEGE_MODULE_L2CACHE_ADDR_1,
	VCMD_JPEGE_MODULE_MMU_ADDR_1},

	{VCMD_JPEGE_IO_ADDR_2,
	VCMD_JPEGE_IO_SIZE_2,
	VCMD_JPEGE_INT_PIN_2,
	VCMD_JPEGE_MODULE_TYPE_2,
	VCMD_JPEGE_MODULE_MAIN_ADDR_2,
	VCMD_JPEGE_MODULE_DEC400_ADDR_2,
	VCMD_JPEGE_MODULE_L2CACHE_ADDR_2,
	VCMD_JPEGE_MODULE_MMU_ADDR_2},

	{VCMD_JPEGE_IO_ADDR_3,
	VCMD_JPEGE_IO_SIZE_3,
	VCMD_JPEGE_INT_PIN_3,
	VCMD_JPEGE_MODULE_TYPE_3,
	VCMD_JPEGE_MODULE_MAIN_ADDR_3,
	VCMD_JPEGE_MODULE_DEC400_ADDR_3,
	VCMD_JPEGE_MODULE_L2CACHE_ADDR_3,
	VCMD_JPEGE_MODULE_MMU_ADDR_3},
	//JPEG decoder configuration
	{VCMD_JPEGD_IO_ADDR_0,
	VCMD_JPEGD_IO_SIZE_0,
	VCMD_JPEGD_INT_PIN_0,
	VCMD_JPEGD_MODULE_TYPE_0,
	VCMD_JPEGD_MODULE_MAIN_ADDR_0,
	VCMD_JPEGD_MODULE_DEC400_ADDR_0,
	VCMD_JPEGD_MODULE_L2CACHE_ADDR_0,
	VCMD_JPEGD_MODULE_MMU_ADDR_0},

	{VCMD_JPEGD_IO_ADDR_1,
	VCMD_JPEGD_IO_SIZE_1,
	VCMD_JPEGD_INT_PIN_1,
	VCMD_JPEGD_MODULE_TYPE_1,
	VCMD_JPEGD_MODULE_MAIN_ADDR_1,
	VCMD_JPEGD_MODULE_DEC400_ADDR_1,
	VCMD_JPEGD_MODULE_L2CACHE_ADDR_1,
	VCMD_JPEGD_MODULE_MMU_ADDR_1},

	{VCMD_JPEGD_IO_ADDR_2,
	VCMD_JPEGD_IO_SIZE_2,
	VCMD_JPEGD_INT_PIN_2,
	VCMD_JPEGD_MODULE_TYPE_2,
	VCMD_JPEGD_MODULE_MAIN_ADDR_2,
	VCMD_JPEGD_MODULE_DEC400_ADDR_2,
	VCMD_JPEGD_MODULE_L2CACHE_ADDR_2,
	VCMD_JPEGD_MODULE_MMU_ADDR_2},

	{VCMD_JPEGD_IO_ADDR_3,
	VCMD_JPEGD_IO_SIZE_3,
	VCMD_JPEGD_INT_PIN_3,
	VCMD_JPEGD_MODULE_TYPE_3,
	VCMD_JPEGD_MODULE_MAIN_ADDR_3,
	VCMD_JPEGD_MODULE_DEC400_ADDR_3,
	VCMD_JPEGD_MODULE_L2CACHE_ADDR_3,
	VCMD_JPEGD_MODULE_MMU_ADDR_3},
#endif
};

/*these size need to be modified according to hw config.*/
#define VCMD_ENCODER_REGISTER_SIZE              (479 * 4)
#define VCMD_DECODER_REGISTER_SIZE              (MAX_REG_COUNT * 4)
#define VCMD_IM_REGISTER_SIZE                   (479 * 4)
#define VCMD_JPEG_ENCODER_REGISTER_SIZE         (479 * 4)
#define VCMD_JPEG_DECODER_REGISTER_SIZE         (MAX_REG_COUNT * 4)

#define MAX_VCMD_NUMBER (MAX_VCMD_TYPE * MAX_SAME_MODULE_TYPE_CORE_NUMBER)

#define HW_WORK_STATE_PEND            3

#define MAX_CMDBUF_INT_NUMBER         1
#define INT_MIN_SUM_OF_IMAGE_SIZE                                              \
	(4096 * 2160 * MAX_SAME_MODULE_TYPE_CORE_NUMBER * MAX_CMDBUF_INT_NUMBER)
#define MAX_PROCESS_CORE_NUMBER (4 * 8)
#define PROCESS_MAX_VIDEO_SIZE                                                 \
	(4096 * 2160 * MAX_SAME_MODULE_TYPE_CORE_NUMBER *                      \
	 MAX_PROCESS_CORE_NUMBER)
#define PROCESS_MAX_JPEG_SIZE (2147483648U) //32K*32K*2
#define PROCESS_MAX_SUM_OF_IMAGE_SIZE                                          \
	(PROCESS_MAX_VIDEO_SIZE > PROCESS_MAX_JPEG_SIZE ?                      \
		 PROCESS_MAX_VIDEO_SIZE :                                      \
		 PROCESS_MAX_JPEG_SIZE)

#define MAX_SAME_MODULE_TYPE_CORE_NUMBER 1

#ifdef EMU
/*******************PCIE CONFIG*************************/

  #define PCI_VENDOR_ID_HANTRO            0x1d9b//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI      0xface// 0x7011

  /* Base address got control register */
  #define PCI_H2_BAR              2

  /* Base address DDR register */
  #define PCI_DDR_BAR             4
#else
/*******************PCIE CONFIG*************************/
#ifdef PLATFORM_GEN7
  #define PCI_VENDOR_ID_HANTRO            0x10ee//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI      0x9014// 0x7011

  /* Base address got control register */
  #define PCI_H2_BAR              0

  /* Base address DDR register */
  #define PCI_DDR_BAR             2
#else
  #define PCI_VENDOR_ID_HANTRO            0x10ee//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI      0x8014// 0x7011

  /* Base address got control register */
  #define PCI_H2_BAR              4

  /* Base address DDR register */
  #define PCI_DDR_BAR             0
#endif
#endif
/*pcie address need to substract this
 *value then can be put to register
 */
static size_t base_ddr_addr = 0;

/********variables declaration related with race condition**********/

#define CMDBUF_MAX_SIZE (512 * 4 * 4)

//approximately=128x(320x240)=128x2k=128x8kbyte=1Mbytes
#define CMDBUF_POOL_TOTAL_SIZE (2 * 1024 * 1024)
#define TOTAL_DISCRETE_CMDBUF_NUM (CMDBUF_POOL_TOTAL_SIZE / CMDBUF_MAX_SIZE)
#define CMDBUF_VCMD_REGISTER_TOTAL_SIZE                                        \
	(9 * 1024 * 1024 - CMDBUF_POOL_TOTAL_SIZE * 2)
#define VCMD_REGISTER_SIZE (128 * 4)

struct noncache_mem {
	u32 *virtual_address;
	dma_addr_t bus_address; /* IO virtual address - decoder accessing address */
	dma_addr_t phy_address; /* memory bus address - send to userspace to do mmap, better to use dma_mmap_coherent() */
	size_t mmu_bus_address; /* buffer physical address in MMU*/
	u32 size;
	u16 cmdbuf_id;
};

struct process_manager_obj {
	struct file *filp;
	u64 total_exe_time;
	spinlock_t spinlock;
	wait_queue_head_t wait_queue;
};

struct cmdbuf_obj {
	/* current CMDBUF type: input vc8000e=0,
	 * IM=1,vc8000d=2, pege=3, jpegd=4
	 */
	u32 module_type;
	/* current CMDBUFpriority: normal=0, high=1 */
	u32 priority;
	/* current CMDBUFexecuting_time=encoded_image_size*(rdoLevel+1)*(rdoq+1);*/
	u64 executing_time;
	/* current CMDBUF size */
	u32 cmdbuf_size;
	/* current CMDBUF start virtual address.*/
	u32 *cmdbuf_virtual_address;
	/* current CMDBUF start physical address. */
	size_t cmdbuf_bus_address;
	//current CMDBUF start mmu mapping address.
	size_t mmu_cmdbuf_bus_address;
	/*current status CMDBUF start virtual address.*/
	u32 *status_virtual_address;
	/*current status CMDBUF start physical address.*/
	size_t status_bus_address;
	//current status CMDBUF start mmu mapping address.
	size_t mmu_status_bus_address;
	/*current status CMDBUF size*/
	u32 status_size;
	/*current CMDBUF executing status.*/
	u32 executing_status;
	/*file pointer in the same process.*/
	struct file *filp;
	/* which vcmd core is used.*/
	u16 core_id;
	/* used to manage CMDBUF in driver.It is a handle
	 * to identify cmdbuf.also is an interrupt vector.
	 * position in pool,same as status position.
	 */
	u16 cmdbuf_id;
	/* 0 means sw has not copied data into this CMDBUF;
	 * 1 means sw has copied data into this CMDBUF
	 */
	u8 cmdbuf_data_loaded;
	/* 0 :not linked, 1:linked into a vcmd core list*/
	u8 cmdbuf_data_linked;
	/* if 0,waiting for CMDBUF finish; if 1,
	 * op code in CMDBUF has finished one by one.
	 * HANTRO_VCMD_IOCH_WAIT_CMDBUF will check this variable.
	 */
	u8 cmdbuf_run_done;
	/* if 0, not need to remove CMDBUF; 1 CMDBUF
	 * can be removed if it is not the last CMDBUF;
	 */
	u8 cmdbuf_need_remove;
	/* if 0, the cmd buf hasn't been waited,
	 * otherwise, has been waited.
	 */
	u32 waited;
	/* if 1, the last opcode is end opCode. */
	u8 has_end_cmdbuf;
	/* if 1, JMP will not send normal interrupt. */
	u8 no_normal_int_cmdbuf;
	struct process_manager_obj *process_manager_obj;
};

struct hantrovcmd_dev {
	//config of each core,such as base addr, irq,etc
	struct vcmd_config vcmd_core_cfg;
	//vcmd core id for driver and sw internal use
	u32 core_id;
	u32 sw_cmdbuf_rdy_num;
	spinlock_t *spinlock;
	wait_queue_head_t *wait_queue;
	wait_queue_head_t *wait_abort_queue;
	bi_list list_manager;

	volatile u8 *hwregs; /* IO mem base */
	u32 reg_mirror[ASIC_VCMD_SWREG_AMOUNT];
	//number of cmdbufs without interrupt.
	u32 duration_without_int;
	volatile u8 working_state;
	u64 total_exe_time;
	u16 status_cmdbuf_id; //used for analyse configuration in cwl.
	u32 hw_version_id; /*megvii 0x43421001, later 0x43421102*/
	//start virtual address of vcmd registers memory of  CMDBUF.
	u32 *vcmd_reg_mem_virtual_address;
	//start physical address of vcmd registers memory of  CMDBUF.
	size_t vcmd_reg_mem_bus_address;
	//start mmu mapping address of vcmd registers memory of CMDBUF.
	unsigned int mmu_vcmd_reg_mem_bus_address;
	// size of vcmd registers memory of CMDBUF.
	u32 vcmd_reg_mem_size;
};

/*
 * Ioctl definitions
 */
#define VCMD_HW_ID 0x4342

static struct noncache_mem vcmd_buf_mem_pool;
static struct noncache_mem vcmd_status_buf_mem_pool;
static struct noncache_mem vcmd_registers_mem_pool;

static u16 cmdbuf_used[TOTAL_DISCRETE_CMDBUF_NUM];
static u16 cmdbuf_used_pos;
static u16 cmdbuf_used_residual;

static struct hantrovcmd_dev *vcmd_manager[MAX_VCMD_TYPE][MAX_VCMD_NUMBER];
static bi_list_node *global_cmdbuf_node[TOTAL_DISCRETE_CMDBUF_NUM]; //256 discrete cmdbuf number

static bi_list global_process_manager;

static u16 vcmd_position[MAX_VCMD_TYPE];
static int vcmd_type_core_num[MAX_VCMD_TYPE];

#define EXECUTING_CMDBUF_ID_ADDR 26
#define VCMD_EXE_CMDBUF_COUNT 3

#define WORKING_STATE_IDLE 0
#define WORKING_STATE_WORKING 1
#define WORKING_STATE_STALL 2
#define CMDBUF_EXE_STATUS_OK 0
#define CMDBUF_EXE_STATUS_CMDERR 1
#define CMDBUF_EXE_STATUS_BUSERR 2

static struct semaphore vcmd_reserve_cmdbuf_sem[MAX_VCMD_TYPE]; //for reserve

#define CMDBUF_LOCK_NUM 16
static spinlock_t cmdbuf_lock[CMDBUF_LOCK_NUM];
#define LOCK_CMDBUF_NODE(cmdbuf_id, flags) \
	spin_lock_irqsave(&cmdbuf_lock[(cmdbuf_id) % CMDBUF_LOCK_NUM], flags)
#define UNLOCK_CMDBUF_NODE(cmdbuf_id, flags) \
	spin_unlock_irqrestore(&cmdbuf_lock[(cmdbuf_id) % CMDBUF_LOCK_NUM], flags)

/* PCI base register address (memalloc) */
//extern unsigned long gBaseDDRHw;
//extern unsigned int mmu_enable;
/* whether working in pcie env, passed from hantro_dec.c */
//extern unsigned int pcie;
//extern unsigned long alloc_base;
//extern unsigned long alloc_size;

/******TYPE AND FUNCTION DECLARATION********/

/* here's all the must remember stuff */

static int vcmd_reserve_IO(void);
static void vcmd_release_IO(void);
static void vcmd_reset_asic(struct hantrovcmd_dev *dev);
static void vcmd_reset_current_asic(struct hantrovcmd_dev *dev);
static int allocate_cmdbuf(struct noncache_mem *new_cmdbuf_addr,
			   struct noncache_mem *new_status_cmdbuf_addr);
static void vcmd_link_cmdbuf(struct hantrovcmd_dev *dev,
			     bi_list_node *last_linked_cmdbuf_node);
static void vcmd_start(struct hantrovcmd_dev *dev,
		       bi_list_node *first_linked_cmdbuf_node);
static void create_kernel_process_manager(void);

#if (KERNEL_VERSION(2, 6, 18) > LINUX_VERSION_CODE)
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id);
#endif

static void printk_vcmd_register_debug(const void *hwregs, char *info);

/********local variable declaration********/
/* and this is our MAJOR; use 0
 * for dynamic allocation (recommended)
 */
u32 total_vcmd_core_num;
/* dynamic allocation*/
static struct hantrovcmd_dev *hantrovcmd_data;

//#define VCMD_DEBUG_INTERNAL

//#define IRQ_SIMULATION

#ifdef IRQ_SIMULATION
struct timer_manager {
	u32 core_id; //vcmd core id for driver and sw internal use
	u32 timer_id;
	struct timer_list *timer;
};

static struct timer_list timer[10000];
struct timer_manager timer_reserve[10000];
#if 0
static struct timer_list timer0;
static struct timer_list timer1;
#endif
#endif

//hw_queue can be used for reserve cmdbuf memory
static DECLARE_WAIT_QUEUE_HEAD(vcmd_cmdbuf_memory_wait);

static DEFINE_SPINLOCK(vcmd_cmdbuf_alloc_lock);
static DEFINE_SPINLOCK(vcmd_process_manager_lock);

static spinlock_t owner_lock_vcmd[MAX_VCMD_NUMBER];

static wait_queue_head_t wait_queue_vcmd[MAX_VCMD_NUMBER];

static wait_queue_head_t abort_queue_vcmd[MAX_VCMD_NUMBER];

/* mc wait queue, used in wait_cmdbuf_ready with ANY_CMDBUF_ID. */
static wait_queue_head_t mc_wait_queue;

//extern struct platform_device *platformdev;

#ifdef VCMD_DEBUG_INTERNAL
static void PrintInstr(u32 i, u32 instr, u32 *size)
{
	if ((instr & 0xF8000000) == OPCODE_WREG) {
		int length = ((instr >> 16) & 0x3FF);

		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s %s %d 0x%x]\n",
			i, instr, "WREG", ((instr >> 26) & 0x1) ? "FIX" : "",
		       length, (instr & 0xFFFF));
		*size = ((length + 2) >> 1) << 1;
	} else if ((instr & 0xF8000000) == OPCODE_END) {
		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s]\n", i,
			instr, "END");
		*size = 2;
	} else if ((instr & 0xF8000000) == OPCODE_NOP) {
		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s]\n", i,
			instr, "NOP");
		*size = 2;
	} else if ((instr & 0xF8000000) == OPCODE_RREG) {
		int length = ((instr >> 16) & 0x3FF);

		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s %s %d 0x%x]\n",
			i, instr, "RREG", ((instr >> 26) & 0x1) ? "FIX" : "",
		       length, (instr & 0xFFFF));
		*size = 4;
	} else if ((instr & 0xF8000000) == OPCODE_JMP) {
		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s %s %s]\n",
			i, instr, "JMP", ((instr >> 26) & 0x1) ? "RDY" : "",
		       ((instr >> 25) & 0x1) ? "IE" : "");
		*size = 4;
	} else if ((instr & 0xF8000000) == OPCODE_STALL) {
		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s %s 0x%x]\n",
			i, instr, "STALL", ((instr >> 26) & 0x1) ? "IM" : "",
		       (instr & 0xFFFF));
		*size = 2;
	} else if ((instr & 0xF8000000) == OPCODE_CLRINT) {
		LOG_INFO("current cmdbuf data %d = 0x%08x => [%s %d 0x%x]\n",
			i, instr, "CLRINT", (instr >> 25) & 0x3,
		       (instr & 0xFFFF));
		*size = 2;
	} else
		*size = 1;
}
#endif

/*********cmdbuf object management********/

static struct cmdbuf_obj *create_cmdbuf_obj(void)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	cmdbuf_obj = kmalloc(sizeof(*cmdbuf_obj), GFP_ATOMIC);
	if (!cmdbuf_obj) {
		LOG_ERR("%s\n", "kmalloc for cmdbuf_obj fail!");
		return cmdbuf_obj;
	}
	memset(cmdbuf_obj, 0, sizeof(struct cmdbuf_obj));
	return cmdbuf_obj;
}

static void free_cmdbuf_obj(struct cmdbuf_obj *cmdbuf_obj)
{
	if (!cmdbuf_obj) {
		LOG_DBG("%s\n", "remove_cmdbuf_obj NULL");
		return;
	}
	//free current cmdbuf_obj
	kfree(cmdbuf_obj);
	return;
}

static void free_cmdbuf_mem(u16 cmdbuf_id)
{
	unsigned long flags;

	spin_lock_irqsave(&vcmd_cmdbuf_alloc_lock, flags);
	cmdbuf_used[cmdbuf_id] = 0;
	cmdbuf_used_residual += 1;
	spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
	wake_up_interruptible_all(&vcmd_cmdbuf_memory_wait);
}

static bi_list_node *create_cmdbuf_node(void)
{
	bi_list_node *current_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	struct noncache_mem new_cmdbuf_addr;
	struct noncache_mem new_status_cmdbuf_addr;

	if (wait_event_interruptible(vcmd_cmdbuf_memory_wait,
				     allocate_cmdbuf(&new_cmdbuf_addr,
						     &new_status_cmdbuf_addr)))
		return NULL;
	cmdbuf_obj = create_cmdbuf_obj();
	if (!cmdbuf_obj) {
		LOG_DBG("%s\n", "create_cmdbuf_obj fail!");
		free_cmdbuf_mem(new_cmdbuf_addr.cmdbuf_id);
		return NULL;
	}
	cmdbuf_obj->cmdbuf_bus_address = new_cmdbuf_addr.bus_address;
	cmdbuf_obj->mmu_cmdbuf_bus_address = new_cmdbuf_addr.mmu_bus_address;
	cmdbuf_obj->cmdbuf_virtual_address = new_cmdbuf_addr.virtual_address;
	cmdbuf_obj->cmdbuf_size = new_cmdbuf_addr.size;
	cmdbuf_obj->cmdbuf_id = new_cmdbuf_addr.cmdbuf_id;
	cmdbuf_obj->status_bus_address = new_status_cmdbuf_addr.bus_address;
	cmdbuf_obj->mmu_status_bus_address =
		new_status_cmdbuf_addr.mmu_bus_address;
	cmdbuf_obj->status_virtual_address =
		new_status_cmdbuf_addr.virtual_address;
	cmdbuf_obj->status_size = new_status_cmdbuf_addr.size;
	current_node = bi_list_create_node();
	if (!current_node) {
		LOG_DBG("%s\n", "bi_list_create_node fail!");
		free_cmdbuf_mem(new_cmdbuf_addr.cmdbuf_id);
		free_cmdbuf_obj(cmdbuf_obj);
		return NULL;
	}
	current_node->data = (void *)cmdbuf_obj;
	current_node->next = NULL;
	current_node->previous = NULL;
	return current_node;
}

static void free_cmdbuf_node(bi_list_node *cmdbuf_node)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	if (!cmdbuf_node) {
		LOG_DBG("%s\n", "remove_cmdbuf_node NULL");
		return;
	}
	cmdbuf_obj = (struct cmdbuf_obj *)cmdbuf_node->data;
	//free cmdbuf mem in pool
	free_cmdbuf_mem(cmdbuf_obj->cmdbuf_id);

	//free struct cmdbuf_obj
	free_cmdbuf_obj(cmdbuf_obj);
	//free current cmdbuf_node entity.
	bi_list_free_node(cmdbuf_node);
	return;
}

//just remove, not free the node.
static bi_list_node *remove_cmdbuf_node_from_list(bi_list *list,
						  bi_list_node *cmdbuf_node)
{
	if (!cmdbuf_node) {
		LOG_DBG("%s\n", "remove_cmdbuf_node_from_list  NULL");
		return NULL;
	}
	if (cmdbuf_node->next) {
		bi_list_remove_node(list, cmdbuf_node);
		return cmdbuf_node;
	}
	//the last one, should not be removed.
	return NULL;
}

//calculate executing_time of each vcmd
static u64 calculate_executing_time_after_node(bi_list_node *exe_cmdbuf_node)
{
	u64 time_run_all = 0;
	struct cmdbuf_obj *cmdbuf_obj_temp = NULL;

	while (1) {
		if (!exe_cmdbuf_node)
			break;
		cmdbuf_obj_temp = (struct cmdbuf_obj *)exe_cmdbuf_node->data;
		time_run_all += cmdbuf_obj_temp->executing_time;
		exe_cmdbuf_node = exe_cmdbuf_node->next;
	}
	return time_run_all;
}

static u64
calculate_executing_time_after_node_high_priority(bi_list_node *exe_cmdbuf_node)
{
	u64 time_run_all = 0;
	struct cmdbuf_obj *cmdbuf_obj_temp = NULL;

	if (!exe_cmdbuf_node)
		return time_run_all;
	cmdbuf_obj_temp = (struct cmdbuf_obj *)exe_cmdbuf_node->data;
	time_run_all += cmdbuf_obj_temp->executing_time;
	exe_cmdbuf_node = exe_cmdbuf_node->next;
	while (1) {
		if (!exe_cmdbuf_node)
			break;
		cmdbuf_obj_temp = (struct cmdbuf_obj *)exe_cmdbuf_node->data;
		if (cmdbuf_obj_temp->priority == CMDBUF_PRIORITY_NORMAL)
			break;
		time_run_all += cmdbuf_obj_temp->executing_time;
		exe_cmdbuf_node = exe_cmdbuf_node->next;
	}
	return time_run_all;
}

/**********cmdbuf pool management*************/

static int allocate_cmdbuf(struct noncache_mem *new_cmdbuf_addr,
			   struct noncache_mem *new_status_cmdbuf_addr)
{
	unsigned long flags;

	spin_lock_irqsave(&vcmd_cmdbuf_alloc_lock, flags);
	if (cmdbuf_used_residual == 0) {
		spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
		//no empty cmdbuf
		return 0;
	}
	//there is one cmdbuf at least
	while (1) {
		if (cmdbuf_used[cmdbuf_used_pos] == 0 &&
		    !global_cmdbuf_node[cmdbuf_used_pos]) {
			cmdbuf_used[cmdbuf_used_pos] = 1;
			cmdbuf_used_residual -= 1;
			new_cmdbuf_addr->virtual_address =
				vcmd_buf_mem_pool.virtual_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE / 4;
			new_cmdbuf_addr->bus_address =
				vcmd_buf_mem_pool.bus_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE;

			new_cmdbuf_addr->mmu_bus_address =
				vcmd_buf_mem_pool.mmu_bus_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE;

			new_cmdbuf_addr->size = CMDBUF_MAX_SIZE;
			new_cmdbuf_addr->cmdbuf_id = cmdbuf_used_pos;
			new_status_cmdbuf_addr->virtual_address =
				vcmd_status_buf_mem_pool.virtual_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE / 4;
			new_status_cmdbuf_addr->bus_address =
				vcmd_status_buf_mem_pool.bus_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE;

			new_status_cmdbuf_addr->mmu_bus_address =
				vcmd_status_buf_mem_pool.mmu_bus_address +
				cmdbuf_used_pos * CMDBUF_MAX_SIZE;

			new_status_cmdbuf_addr->size = CMDBUF_MAX_SIZE;
			new_status_cmdbuf_addr->cmdbuf_id = cmdbuf_used_pos;
			cmdbuf_used_pos++;
			if (cmdbuf_used_pos >= TOTAL_DISCRETE_CMDBUF_NUM)
				cmdbuf_used_pos = 0;
			spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
			return 1;
		}
		cmdbuf_used_pos++;
		if (cmdbuf_used_pos >= TOTAL_DISCRETE_CMDBUF_NUM)
			cmdbuf_used_pos = 0;
	}
	return 0;
}

static bi_list_node *get_cmdbuf_node_in_list_by_addr(size_t cmdbuf_addr,
						     bi_list *list)
{
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	new_cmdbuf_node = list->head;
	while (1) {
		if (!new_cmdbuf_node)
			return NULL;
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (((cmdbuf_obj->cmdbuf_bus_address - base_ddr_addr) <=
		     cmdbuf_addr) &&
		    (((cmdbuf_obj->cmdbuf_bus_address - base_ddr_addr +
		       cmdbuf_obj->cmdbuf_size) > cmdbuf_addr))) {
			return new_cmdbuf_node;
		}
		new_cmdbuf_node = new_cmdbuf_node->next;
	}
	return NULL;
}

static int wait_abort_rdy(struct hantrovcmd_dev *dev)
{
	return dev->working_state == WORKING_STATE_IDLE;
}

static int select_vcmd(bi_list_node *new_cmdbuf_node)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	bi_list_node *curr_cmdbuf_node = NULL;
	bi_list *list = NULL;
	struct hantrovcmd_dev *dev = NULL;
	struct hantrovcmd_dev *smallest_dev = NULL;
	u64 executing_time = 0xffffffffffffffff;

	int counter = 0;
	unsigned long flags = 0;
	u32 hw_rdy_cmdbuf_num = 0;
	size_t exe_cmdbuf_addr = 0;
	struct cmdbuf_obj *cmdbuf_obj_temp = NULL;
	u32 cmdbuf_id = 0;

	cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
	//there is an empty vcmd to be used
	while (1) {
		dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
		list = &dev->list_manager;
		spin_lock_irqsave(dev->spinlock, flags);
		if (!list->tail) {
			bi_list_insert_node_tail(list, new_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			cmdbuf_obj->core_id = dev->core_id;
			return 0;
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		vcmd_position[cmdbuf_obj->module_type]++;
		if (vcmd_position[cmdbuf_obj->module_type] >=
				vcmd_type_core_num[cmdbuf_obj->module_type])
			vcmd_position[cmdbuf_obj->module_type] = 0;
		counter++;

		if (counter >= vcmd_type_core_num[cmdbuf_obj->module_type])
			break;
	}
	/* there is a vcmd which tail node -> cmdbuf_run_done == 1.
	 * It means this vcmd has nothing to do, so we select it.
	 */
	counter = 0;
	while (1) {
		dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
		list = &dev->list_manager;
		spin_lock_irqsave(dev->spinlock, flags);
		curr_cmdbuf_node = list->tail;
		if (!curr_cmdbuf_node) {
			bi_list_insert_node_tail(list, new_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			cmdbuf_obj->core_id = dev->core_id;
			return 0;
		}
		cmdbuf_obj_temp = (struct cmdbuf_obj *)curr_cmdbuf_node->data;
		if (cmdbuf_obj_temp->cmdbuf_run_done == 1) {
			bi_list_insert_node_tail(list, new_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			cmdbuf_obj->core_id = dev->core_id;
			return 0;
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		vcmd_position[cmdbuf_obj->module_type]++;
		if (vcmd_position[cmdbuf_obj->module_type] >=
				vcmd_type_core_num[cmdbuf_obj->module_type])
			vcmd_position[cmdbuf_obj->module_type] = 0;
		counter++;

		if (counter >= vcmd_type_core_num[cmdbuf_obj->module_type])
			break;
	}

	/* another case, tail = executing node, and
	 * vcmd=pend state (finish but not generate interrupt)
	 */
	counter = 0;
	while (1) {
		dev = vcmd_manager[cmdbuf_obj->module_type]
				  [vcmd_position[cmdbuf_obj->module_type]];
		list = &dev->list_manager;
		//read executing cmdbuf address
		if (dev->hw_version_id <= HW_ID_1_0_C)
			hw_rdy_cmdbuf_num = vcmd_get_register_value(
				(const void *)dev->hwregs, dev->reg_mirror,
				HWIF_VCMD_EXE_CMDBUF_COUNT);
		else {
			hw_rdy_cmdbuf_num =
				*(dev->vcmd_reg_mem_virtual_address +
				  VCMD_EXE_CMDBUF_COUNT);
			if (hw_rdy_cmdbuf_num != dev->sw_cmdbuf_rdy_num)
				hw_rdy_cmdbuf_num += 1;
		}
		spin_lock_irqsave(dev->spinlock, flags);
		curr_cmdbuf_node = list->tail;
		if (!curr_cmdbuf_node) {
			bi_list_insert_node_tail(list, new_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			cmdbuf_obj->core_id = dev->core_id;
			return 0;
		}

		if (dev->sw_cmdbuf_rdy_num == hw_rdy_cmdbuf_num) {
			bi_list_insert_node_tail(list, new_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			cmdbuf_obj->core_id = dev->core_id;
			return 0;
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		vcmd_position[cmdbuf_obj->module_type]++;
		if (vcmd_position[cmdbuf_obj->module_type] >=
				vcmd_type_core_num[cmdbuf_obj->module_type])
			vcmd_position[cmdbuf_obj->module_type] = 0;
		counter++;

		if (counter >= vcmd_type_core_num[cmdbuf_obj->module_type])
			break;
	}

	// there is no idle vcmd,if low priority,
	// calculate exe time, select the least one.
	// or if high priority, calculate the exe time,
	// select the least one and abort it.
	if (cmdbuf_obj->priority == CMDBUF_PRIORITY_NORMAL) {
		counter = 0;
		//calculate total execute time of all devices
		while (1) {
			dev = vcmd_manager
				[cmdbuf_obj->module_type]
				[vcmd_position[cmdbuf_obj->module_type]];
			//read executing cmdbuf address
			if (dev->hw_version_id <= HW_ID_1_0_C) {
				exe_cmdbuf_addr = VCMDGetAddrRegisterValue(
					(const void *)dev->hwregs,
					dev->reg_mirror,
					HWIF_VCMD_EXECUTING_CMD_ADDR);
				list = &dev->list_manager;
				spin_lock_irqsave(dev->spinlock, flags);
				//get the executing cmdbuf node.
				curr_cmdbuf_node =
					get_cmdbuf_node_in_list_by_addr(
						exe_cmdbuf_addr, list);

				//calculate total execute time of this device
				dev->total_exe_time =
					calculate_executing_time_after_node(
						curr_cmdbuf_node);
				spin_unlock_irqrestore(dev->spinlock, flags);
			} else {
				//cmdbuf_id = vcmd_get_register_value((const void *)dev->hwregs,
				//dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
				cmdbuf_id =
					*(dev->vcmd_reg_mem_virtual_address +
					  EXECUTING_CMDBUF_ID_ADDR + 1);
				spin_lock_irqsave(dev->spinlock, flags);
				if (cmdbuf_id >= TOTAL_DISCRETE_CMDBUF_NUM ||
				    cmdbuf_id == 0) {
					LOG_ERR("cmdbuf_id greater than the ceiling !!\n");
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return -1;
				}
				//get the executing cmdbuf node.
				curr_cmdbuf_node =
					global_cmdbuf_node[cmdbuf_id];
				if (!curr_cmdbuf_node) {
					list = &dev->list_manager;
					curr_cmdbuf_node = list->head;
					while (1) {
						if (!curr_cmdbuf_node)
							break;
						cmdbuf_obj_temp =
							(struct cmdbuf_obj *)
								curr_cmdbuf_node
									->data;
						if (cmdbuf_obj_temp
							    ->cmdbuf_data_linked &&
						    cmdbuf_obj_temp->cmdbuf_run_done ==
							    0)
							break;
						curr_cmdbuf_node =
							curr_cmdbuf_node->next;
					}
				}

				//calculate total execute time of this device
				dev->total_exe_time =
					calculate_executing_time_after_node(
						curr_cmdbuf_node);
				spin_unlock_irqrestore(dev->spinlock, flags);
			}
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			counter++;
			if (counter >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				break;
		}
		//find the device with the least total_exe_time.
		counter = 0;
		executing_time = 0xffffffffffffffff;
		while (1) {
			dev = vcmd_manager
				[cmdbuf_obj->module_type]
				[vcmd_position[cmdbuf_obj->module_type]];
			if (dev->total_exe_time <= executing_time) {
				executing_time = dev->total_exe_time;
				smallest_dev = dev;
			}
			vcmd_position[cmdbuf_obj->module_type]++;
			if (vcmd_position[cmdbuf_obj->module_type] >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				vcmd_position[cmdbuf_obj->module_type] = 0;
			counter++;
			if (counter >=
			    vcmd_type_core_num[cmdbuf_obj->module_type])
				break;
		}
		//insert list
		list = &smallest_dev->list_manager;
		spin_lock_irqsave(smallest_dev->spinlock, flags);
		bi_list_insert_node_tail(list, new_cmdbuf_node);
		spin_unlock_irqrestore(smallest_dev->spinlock, flags);
		cmdbuf_obj->core_id = smallest_dev->core_id;
		return 0;
	}
	//CMDBUF_PRIORITY_HIGH
	counter = 0;
	//calculate total execute time of all devices
	while (1) {
		dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
		if (dev->hw_version_id <= HW_ID_1_0_C) {
			//read executing cmdbuf address
			exe_cmdbuf_addr = VCMDGetAddrRegisterValue((const void *)dev->hwregs,
								   dev->reg_mirror,
				HWIF_VCMD_EXECUTING_CMD_ADDR);
			list = &dev->list_manager;
			spin_lock_irqsave(dev->spinlock, flags);
			//get the executing cmdbuf node.
			curr_cmdbuf_node =
				get_cmdbuf_node_in_list_by_addr(exe_cmdbuf_addr,
								list);

			//calculate total execute time of this device
			dev->total_exe_time =
				calculate_executing_time_after_node_high_priority(curr_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
		} else {
			//cmdbuf_id = vcmd_get_register_value((const void *)dev->hwregs,
			//dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
			cmdbuf_id = *(dev->vcmd_reg_mem_virtual_address +
					EXECUTING_CMDBUF_ID_ADDR);
			spin_lock_irqsave(dev->spinlock, flags);
			if (cmdbuf_id >= TOTAL_DISCRETE_CMDBUF_NUM ||
			    cmdbuf_id == 0) {
				LOG_ERR("cmdbuf_id greater than the ceiling !!\n");
				spin_unlock_irqrestore(dev->spinlock,
						       flags);
				return -1;
			}
			//get the executing cmdbuf node.
			curr_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!curr_cmdbuf_node) {
				list = &dev->list_manager;
				curr_cmdbuf_node = list->head;
				while (1) {
					if (!curr_cmdbuf_node)
						break;
					cmdbuf_obj_temp = (struct cmdbuf_obj *)curr_cmdbuf_node->data;
					if (cmdbuf_obj_temp->cmdbuf_data_linked &&
					    cmdbuf_obj_temp->cmdbuf_run_done ==
						0)
						break;
					curr_cmdbuf_node = curr_cmdbuf_node->next;
				}
			}

			//calculate total execute time of this device
			dev->total_exe_time =
				calculate_executing_time_after_node(curr_cmdbuf_node);
			spin_unlock_irqrestore(dev->spinlock, flags);
		}
		vcmd_position[cmdbuf_obj->module_type]++;
		if (vcmd_position[cmdbuf_obj->module_type] >=
			vcmd_type_core_num[cmdbuf_obj->module_type])
			vcmd_position[cmdbuf_obj->module_type] = 0;
		counter++;
		if (counter >= vcmd_type_core_num[cmdbuf_obj->module_type])
			break;
	}
	//find the smallest device.
	counter = 0;
	executing_time = 0xffffffffffffffff;
	while (1) {
		dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
		if (dev->total_exe_time <= executing_time) {
			executing_time = dev->total_exe_time;
			smallest_dev = dev;
		}
		vcmd_position[cmdbuf_obj->module_type]++;
		if (vcmd_position[cmdbuf_obj->module_type] >=
			vcmd_type_core_num[cmdbuf_obj->module_type])
			vcmd_position[cmdbuf_obj->module_type] = 0;
		counter++;
		if (counter >= vcmd_type_core_num[cmdbuf_obj->module_type])
			break;
	}
	//abort the vcmd and wait
	vcmd_write_register_value((const void *)smallest_dev->hwregs,
				  smallest_dev->reg_mirror,
					HWIF_VCMD_START_TRIGGER, 0);
	if (wait_event_interruptible(*smallest_dev->wait_abort_queue,
				     wait_abort_rdy(smallest_dev)))
		return -ERESTARTSYS;
	// need to select inserting position again
	// because hw maybe have run to the next node.
	// CMDBUF_PRIORITY_HIGH
	spin_lock_irqsave(smallest_dev->spinlock, flags);
	curr_cmdbuf_node = smallest_dev->list_manager.head;
	while (1) {
		//if list is empty or tail,insert to tail
		if (!curr_cmdbuf_node)
			break;
		cmdbuf_obj_temp =
			(struct cmdbuf_obj *)curr_cmdbuf_node->data;
		//if find the first node which priority is normal,
		//insert node prior to  the node
		if ((cmdbuf_obj_temp->priority ==
			CMDBUF_PRIORITY_NORMAL) &&
			(cmdbuf_obj_temp->cmdbuf_run_done == 0))
			break;
		curr_cmdbuf_node = curr_cmdbuf_node->next;
	}
	bi_list_insert_node_before(list, curr_cmdbuf_node,
				   new_cmdbuf_node);
	cmdbuf_obj->core_id = smallest_dev->core_id;
	spin_unlock_irqrestore(smallest_dev->spinlock, flags);
	return 0;
}

static int
wait_process_resource_rdy(struct process_manager_obj *process_manager_obj)
{
	return process_manager_obj->total_exe_time <=
	       PROCESS_MAX_SUM_OF_IMAGE_SIZE;
}

static long reserve_cmdbuf(struct file *filp,
			   struct exchange_parameter *input_para)
{
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	bi_list_node *process_manager_node = NULL;
	struct process_manager_obj *process_manager_obj = NULL;
	unsigned long flags;

	input_para->cmdbuf_id = 0;
	if (input_para->cmdbuf_size > CMDBUF_MAX_SIZE)
		return -1;
	LOG_DBG("reserve cmdbuf filp %p\n", (void *)filp);
	spin_lock_irqsave(&vcmd_process_manager_lock, flags);
	process_manager_node = global_process_manager.head;
	while (1) {
		if (!process_manager_node) {
			//should not happen
			LOG_ERR("ERROR process_manager_node !!\n");
			spin_unlock_irqrestore(&vcmd_process_manager_lock,
					       flags);
			return -1;
		}
		process_manager_obj = (struct process_manager_obj *)
						   process_manager_node->data;
		LOG_DBG("reserve loop: node %p, filp %p\n",
		       (void *)process_manager_node,
		       (void *)process_manager_obj->filp);
		if (filp == process_manager_obj->filp)
			break;

		process_manager_node = process_manager_node->next;
	}
	spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
	spin_lock_irqsave(&process_manager_obj->spinlock, flags);
	process_manager_obj->total_exe_time += input_para->executing_time;
	spin_unlock_irqrestore(&process_manager_obj->spinlock, flags);
	if (wait_event_interruptible(
		    process_manager_obj->wait_queue,
		    wait_process_resource_rdy(process_manager_obj)))
		return -1;

	new_cmdbuf_node = create_cmdbuf_node();
	if (!new_cmdbuf_node)
		return -1;

	cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
	cmdbuf_obj->module_type = input_para->module_type;
	cmdbuf_obj->priority = input_para->priority;
	cmdbuf_obj->executing_time = input_para->executing_time;
	cmdbuf_obj->cmdbuf_size = CMDBUF_MAX_SIZE;
	input_para->cmdbuf_size = CMDBUF_MAX_SIZE;
	cmdbuf_obj->filp = filp;
	cmdbuf_obj->process_manager_obj = process_manager_obj;

	input_para->cmdbuf_id = cmdbuf_obj->cmdbuf_id;

	LOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
	global_cmdbuf_node[input_para->cmdbuf_id] = new_cmdbuf_node;
	UNLOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);

	return 0;
}

static long release_cmdbuf(struct file *filp, u16 cmdbuf_id)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	bi_list_node *last_cmdbuf_node = NULL;
	bi_list_node *new_cmdbuf_node = NULL;
	bi_list *list = NULL;
	u32 module_type;

	unsigned long flags;
	struct hantrovcmd_dev *dev = NULL;
	struct filp_priv *fp_priv = NULL;
	if (!filp || !filp->private_data) {
		LOG_ERR("invalid filp\n");
		return -1;
	}
	fp_priv = (struct filp_priv *)filp->private_data;

	/*get cmdbuf object according to cmdbuf_id*/
	LOCK_CMDBUF_NODE(cmdbuf_id, flags);
	new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
	if (!new_cmdbuf_node) {
		UNLOCK_CMDBUF_NODE(cmdbuf_id, flags);
		//should not happen
		LOG_ERR("ERROR cmdbuf_id !!\n");
		return -1;
	}
	global_cmdbuf_node[cmdbuf_id] = NULL;
	UNLOCK_CMDBUF_NODE(cmdbuf_id, flags);

	cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
	if (cmdbuf_obj->filp != filp) {
		//should not happen
		LOG_ERR("ERROR cmdbuf_id !!\n");
		return -1;
	}
	module_type = cmdbuf_obj->module_type;
	//TODO
	if (down_interruptible(&vcmd_reserve_cmdbuf_sem[module_type]))
		return -ERESTARTSYS;
	dev = &hantrovcmd_data[cmdbuf_obj->core_id];

	//spin_lock_irqsave(dev->spinlock, flags);
	list = &dev->list_manager;
	cmdbuf_obj->cmdbuf_need_remove = 1;
	last_cmdbuf_node = new_cmdbuf_node->previous;
	while (1) {
		//remove current node
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (cmdbuf_obj->cmdbuf_need_remove == 1) {
			new_cmdbuf_node = remove_cmdbuf_node_from_list(
				list, new_cmdbuf_node);
			if (new_cmdbuf_node) {
				//free node
				LOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
				global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
				UNLOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);

				if (cmdbuf_obj->process_manager_obj) {
					spin_lock_irqsave(&cmdbuf_obj->process_manager_obj->spinlock,
							flags);
					cmdbuf_obj->process_manager_obj->total_exe_time -=
						cmdbuf_obj->executing_time;
					spin_unlock_irqrestore(&cmdbuf_obj->process_manager_obj->spinlock,
								flags);
					wake_up_interruptible_all(&cmdbuf_obj->process_manager_obj->wait_queue);
				}
				free_cmdbuf_node(new_cmdbuf_node);
			}
		}
		if (!last_cmdbuf_node)
			break;
		new_cmdbuf_node = last_cmdbuf_node;
		last_cmdbuf_node = new_cmdbuf_node->previous;
	}
	//spin_unlock_irqrestore(dev->spinlock, flags);
	up(&vcmd_reserve_cmdbuf_sem[module_type]);
	/** release for the pm*/
	if (atomic_dec_return(&(fp_priv->core_tasks[dev->core_id])) >= 0) {
		vdec_pm_runtime_put(dev->core_id);
	}

	return 0;
}

static long release_cmdbuf_node(bi_list *list, bi_list_node *cmdbuf_node)
{
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	unsigned long flags = 0;
	/*get cmdbuf object according to cmdbuf_id*/
	new_cmdbuf_node = cmdbuf_node;
	if (!new_cmdbuf_node)
		return -1;
	//remove node from list
	new_cmdbuf_node = remove_cmdbuf_node_from_list(list, new_cmdbuf_node);
	if (new_cmdbuf_node) {
		//free node
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		LOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
		global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
		UNLOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
		free_cmdbuf_node(new_cmdbuf_node);
		return 0;
	}
	return 1;
}

static long release_cmdbuf_node_cleanup(bi_list *list)
{
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	unsigned long flags = 0;

	while (1) {
		new_cmdbuf_node = list->head;
		if (!new_cmdbuf_node)
			return 0;
		//remove node from list
		bi_list_remove_node(list, new_cmdbuf_node);
		//free node
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		LOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
		global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
		UNLOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
		free_cmdbuf_node(new_cmdbuf_node);
	}
	return 0;
}

static bi_list_node *find_last_linked_cmdbuf(bi_list_node *current_node)
{
	bi_list_node *new_cmdbuf_node = current_node;
	bi_list_node *last_cmdbuf_node;
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	if (!current_node)
		return NULL;
	last_cmdbuf_node = new_cmdbuf_node;
	new_cmdbuf_node = new_cmdbuf_node->previous;
	while (1) {
		if (!new_cmdbuf_node)
			return last_cmdbuf_node;
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (cmdbuf_obj->cmdbuf_data_linked)
			return new_cmdbuf_node;
		last_cmdbuf_node = new_cmdbuf_node;
		new_cmdbuf_node = new_cmdbuf_node->previous;
	}
	return NULL;
}

static long link_and_run_cmdbuf(struct file *filp,
				struct exchange_parameter *input_para)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	bi_list_node *new_cmdbuf_node = NULL;
	bi_list_node *last_cmdbuf_node;
	u32 *jmp_addr = NULL;
	u32 opCode;
	u32 tempOpcode;
	u32 record_last_cmdbuf_rdy_num;
	struct hantrovcmd_dev *dev = NULL;
	unsigned long flags;
	int return_value;
	u16 cmdbuf_id = input_para->cmdbuf_id;

	new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
	if (!new_cmdbuf_node) {
		//should not happen
		LOG_ERR("ERROR cmdbuf_id !!\n");
		return -1;
	}
	cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
	if (cmdbuf_obj->filp != filp) {
		//should not happen
		LOG_ERR("ERROR cmdbuf_id !!\n");
		return -1;
	}
	cmdbuf_obj->cmdbuf_data_loaded = 1;
	cmdbuf_obj->cmdbuf_size = input_para->cmdbuf_size;
	cmdbuf_obj->waited = 0;
#ifdef VCMD_DEBUG_INTERNAL
	{
		u32 i, inst = 0, size = 0;

		LOG_INFO("vcmd link, current cmdbuf content\n");
		for (i = 0; i < cmdbuf_obj->cmdbuf_size / 4; i++) {
			if (i == inst) {
				PrintInstr(i,
					   *(cmdbuf_obj->cmdbuf_virtual_address + i),
					&size);
				inst += size;
			} else {
				LOG_INFO("current cmdbuf data %d = 0x%x\n",
					i,
					*(cmdbuf_obj->cmdbuf_virtual_address + i));
			}
		}
	}
#endif
	//test nop and end opcode, then assign value.
	//0: has jmp opcode,1 has end code
	cmdbuf_obj->has_end_cmdbuf = 0;
	//0: interrupt when JMP,1 not interrupt when JMP
	cmdbuf_obj->no_normal_int_cmdbuf = 0;
	jmp_addr = cmdbuf_obj->cmdbuf_virtual_address +
		   (cmdbuf_obj->cmdbuf_size / 4);
	opCode = tempOpcode = *(jmp_addr - 4);
	opCode >>= 27;
	opCode <<= 27;

	/* we can't identify END opcode or JMP opcode, so we
	 * don't support END opcode in control sw and driver.
	 */
	if (opCode == OPCODE_JMP) {
		//jmp
		opCode = tempOpcode;
		opCode &= 0x02000000;
		if (opCode == JMP_IE_1)
			cmdbuf_obj->no_normal_int_cmdbuf = 0;
		else
			cmdbuf_obj->no_normal_int_cmdbuf = 1;
	} else
		//not support other opcode
		return -1;

	if (down_interruptible(
		    &vcmd_reserve_cmdbuf_sem[cmdbuf_obj->module_type]))
		return -ERESTARTSYS;

	return_value = select_vcmd(new_cmdbuf_node);
	if (return_value)
		return return_value;

	dev = &hantrovcmd_data[cmdbuf_obj->core_id];
	input_para->core_id = cmdbuf_obj->core_id;
	LOG_TRACE("Vdec Allocate cmd buffer [%d] to core [%d]\n", cmdbuf_id, input_para->core_id);
	if (filp) {
		struct filp_priv *fp_priv = (struct filp_priv *)filp->private_data;
		vdec_pm_runtime_sync(dev->core_id);
		atomic_inc(&(fp_priv->core_tasks[dev->core_id]));
	}
	//set ddr address for vcmd registers copy.
	if (dev->hw_version_id > HW_ID_1_0_C) {
		//read vcmd executing register into ddr memory.
		//now core id is got and output ddr address of
		//vcmd register can be filled in.each core has
		//its own fixed output ddr address of vcmd registers.
		jmp_addr = cmdbuf_obj->cmdbuf_virtual_address;
		if (mmu_enable) {
			*(jmp_addr + 2) = 0;
			*(jmp_addr + 1) =
				(u32)((dev->mmu_vcmd_reg_mem_bus_address +
				       (EXECUTING_CMDBUF_ID_ADDR + 1) * 4));
		} else {
			if (sizeof(size_t) == 8) {
				*(jmp_addr + 2) = (u32)(
					(u64)(dev->vcmd_reg_mem_bus_address +
					      (EXECUTING_CMDBUF_ID_ADDR + 1) *
						      4) >>
					32);
			} else {
				*(jmp_addr + 2) = 0;
			}
			*(jmp_addr + 1) =
				(u32)((dev->vcmd_reg_mem_bus_address +
				       (EXECUTING_CMDBUF_ID_ADDR + 1) * 4));
		}

		jmp_addr = cmdbuf_obj->cmdbuf_virtual_address +
			   (cmdbuf_obj->cmdbuf_size / 4);
		//read vcmd all registers into ddr memory.
		//now core id is got and output ddr address of
		//vcmd registers can be filled in.
		//each core has its own fixed output ddr address
		// of vcmd registers.
		if (mmu_enable) {
			if (sizeof(size_t) == 8)
				*(jmp_addr - 6) = 0;
			*(jmp_addr - 7) =
				(u32)(dev->mmu_vcmd_reg_mem_bus_address);
		} else {
			if (sizeof(size_t) == 8)
				*(jmp_addr - 6) = (u32)(
					(u64)dev->vcmd_reg_mem_bus_address >>
					32);
			else
				*(jmp_addr - 6) = 0;
			*(jmp_addr - 7) = (u32)(dev->vcmd_reg_mem_bus_address);
		}
	}
	//start to link and/or run
	spin_lock_irqsave(dev->spinlock, flags);
	last_cmdbuf_node = find_last_linked_cmdbuf(new_cmdbuf_node);
	record_last_cmdbuf_rdy_num = dev->sw_cmdbuf_rdy_num;
	vcmd_link_cmdbuf(dev, last_cmdbuf_node);
	if (dev->working_state == WORKING_STATE_IDLE) {
		//run
		while (last_cmdbuf_node &&
		       ((struct cmdbuf_obj *)last_cmdbuf_node->data)
			       ->cmdbuf_run_done)
			last_cmdbuf_node = last_cmdbuf_node->next;

		if (last_cmdbuf_node && last_cmdbuf_node->data)
			LOG_DBG("vcmd start for cmdbuf id %d, cmdbuf_run_done = %d\n",
			       ((struct cmdbuf_obj *)last_cmdbuf_node->data)
				       ->cmdbuf_id,
			       ((struct cmdbuf_obj *)last_cmdbuf_node->data)
				       ->cmdbuf_run_done);
		vcmd_start(dev, last_cmdbuf_node);
	} else {
		//just update cmdbuf ready number
		if (record_last_cmdbuf_rdy_num != dev->sw_cmdbuf_rdy_num)
			vcmd_write_register_value((const void *)dev->hwregs,
						  dev->reg_mirror,
						  HWIF_VCMD_RDY_CMDBUF_COUNT,
						  dev->sw_cmdbuf_rdy_num);
	}
	spin_unlock_irqrestore(dev->spinlock, flags);

	up(&vcmd_reserve_cmdbuf_sem[cmdbuf_obj->module_type]);

	return 0;
}

static int check_cmdbuf_irq(struct hantrovcmd_dev *dev,
			    struct cmdbuf_obj *cmdbuf_obj, u32 *irq_status_ret)
{
	int rdy = 0;
	unsigned long flags;

	spin_lock_irqsave(dev->spinlock, flags);
	if (cmdbuf_obj->cmdbuf_run_done) {
		rdy = 1;
		//need to decide how to assign this variable
		*irq_status_ret = cmdbuf_obj->executing_status;
	}
	spin_unlock_irqrestore(dev->spinlock, flags);
	return rdy;
}

static int check_mc_cmdbuf_irq(struct file *filp, struct cmdbuf_obj *cmdbuf_obj,
			       u32 *irq_status_ret)
{
	int k;
	bi_list_node *new_cmdbuf_node = NULL;
	struct hantrovcmd_dev *dev = NULL;
	unsigned long flags = 0;

	for (k = 0; k < TOTAL_DISCRETE_CMDBUF_NUM; k++) {
		LOCK_CMDBUF_NODE(k, flags);
		new_cmdbuf_node = global_cmdbuf_node[k];
		if (!new_cmdbuf_node) {
			UNLOCK_CMDBUF_NODE(k, flags);
			continue;
		}

		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (!cmdbuf_obj || cmdbuf_obj->filp != filp) {
			UNLOCK_CMDBUF_NODE(k, flags);
			continue;
		}
		dev = &hantrovcmd_data[cmdbuf_obj->core_id];
		if (check_cmdbuf_irq(dev, cmdbuf_obj, irq_status_ret) == 1) {
			/* Return cmdbuf_id when ANY_CMDBUF_ID is used. */
			if (!cmdbuf_obj->waited) {
				*irq_status_ret = cmdbuf_obj->cmdbuf_id;
				cmdbuf_obj->waited = 1;
				UNLOCK_CMDBUF_NODE(k, flags);
				return 1;
			}
		}
		UNLOCK_CMDBUF_NODE(k, flags);
	}

	return 0;
}

#ifdef IRQ_SIMULATION
void get_random_bytes(void *buf, int nbytes);
#if 0
void hantrovcmd_trigger_irq_0(struct timer_list *timer)
{
	LOG_DBG("trigger core 0 irq\n");
	del_timer(timer);
	hantrovcmd_isr(0, (void *)&hantrovcmd_data[0]);
}

void hantrovcmd_trigger_irq_1(struct timer_list *timer)
{
	LOG_DBG("trigger core 1 irq\n");
	del_timer(timer);
	hantrovcmd_isr(0, (void *)&hantrovcmd_data[1]);
}
#else
void hantrovcmd_trigger_irq(struct timer_list *timer)
{
	u32 timer_id = 0;
	u32 core_id = 0;
	u32 i;

	for (i = 0; i < 10000; i++) {
		if (timer_reserve[i].timer == timer) {
			timer_id = timer_reserve[i].timer_id;
			core_id = timer_reserve[i].core_id;
			break;
		}
	}
	LOG_DBG("trigger core 0 irq\n");
	hantrovcmd_isr(core_id, (void *)&hantrovcmd_data[core_id]);
	del_timer(timer);
	timer_reserve[timer_id].timer = NULL;
}

#endif
#endif

static unsigned int wait_cmdbuf_ready(struct file *filp, u16 cmdbuf_id,
				      u32 *irq_status_ret)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	bi_list_node *new_cmdbuf_node = NULL;
	struct hantrovcmd_dev *dev = NULL;
	unsigned int ret = 0;

	if (cmdbuf_id != ANY_CMDBUF_ID) {
		LOG_DBG("wait_cmdbuf_ready\n");
		new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
		if (!new_cmdbuf_node) {
			//should not happen
			LOG_ERR("ERROR cmdbuf_id !!\n");
			return -1;
		}
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (cmdbuf_obj->filp != filp) {
			//should not happen
			LOG_ERR("ERROR cmdbuf_id !!\n");
			return -1;
		}
		dev = &hantrovcmd_data[cmdbuf_obj->core_id];
#ifdef IRQ_SIMULATION
		{
			u64 random_num;
			//get_random_bytes(&random_num, sizeof(u32));
			random_num =
				(u32)((u64)100 * cmdbuf_obj->executing_time /
					      (4096 * 2160) +
				      50);
			LOG_DBG("random_num=%d\n", random_num);
#if 0
			/*init a timer to trigger irq*/
			if (cmdbuf_obj->core_id == 0) {
				//init_timer(&timer0);
				//timer0.function = hantrovcmd_trigger_irq_0;
				timer_setup(&timer0, hantrovcmd_trigger_irq_0, 0);
				//the expires time is 1s
				timer0.expires =  jiffies + random_num * HZ / 10;
				add_timer(&timer0);
			}

			if (cmdbuf_obj->core_id == 1) {
				//init_timer(&timer1);
				//timer1.function = hantrovcmd_trigger_irq_1;
				timer_setup(&timer1, hantrovcmd_trigger_irq_1, 0);
				//the expires time is 1s
				timer1.expires =  jiffies + random_num * HZ / 10;
				add_timer(&timer1);
			}
#else
			{
				u32 i;
				struct timer_list *temp_timer = NULL;

				for (i = 0; i < 10000; i++) {
					if (!timer_reserve[i].timer) {
						timer_reserve[i].timer_id = i;
						timer_reserve[i].core_id =
							cmdbuf_obj->core_id;
						temp_timer =
							timer_reserve[i].timer =
								&timer[i];
						break;
					}
				}
				//if (cmdbuf_obj->core_id==0)
				{
					//init_timer(&timer0);
					//timer0.function =
					//hantrovcmd_trigger_irq_0;
					timer_setup(temp_timer,
						    hantrovcmd_trigger_irq, 0);
					//the expires time is 1s
					temp_timer->expires =
						jiffies + random_num * HZ / 10;
					add_timer(temp_timer);
				}
			}
#endif
		}
#endif

		if (wait_event_interruptible(*dev->wait_queue,
					     check_cmdbuf_irq(dev, cmdbuf_obj,
							      irq_status_ret))) {
			LOG_DBG("vcmd_wait_queue_0 interrupted\n");
			return -ERESTARTSYS;
		}
		return 0;
	}
	if (check_mc_cmdbuf_irq(filp, cmdbuf_obj, irq_status_ret))
		return 0;
	ret = wait_event_interruptible_timeout(
				mc_wait_queue,
				check_mc_cmdbuf_irq(filp, cmdbuf_obj, irq_status_ret),
				msecs_to_jiffies(600));

	if (ret <= 0) {
		LOG_DBG("multicore wait queue interrupted or timeout, ret = %u\n", ret);
		return -ERESTARTSYS;
	}

	return 0;
}

long hantrovcmd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	long retval = 0;
	static int last_polling_cmd;

	if (cmd != HANTRO_VCMD_IOCH_POLLING_CMDBUF) {
		last_polling_cmd = 0;
		LOG_DBG("ioctl cmd 0x%08x\n", cmd);
	} else {
		if (!last_polling_cmd)
			LOG_DBG("ioctl cmd 0x%08x\n", cmd);
		last_polling_cmd = 1;
	}
	/*
	 * extract the type and number bitfields, and don't encode
	 * wrong cmds: return ENOTTY (inappropriate ioctl)
	 * before access_ok()
	 */
	if (_IOC_TYPE(cmd) != HANTRO_VCMD_IOC_MAGIC)
		return -ENOTTY;
	if ((_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC &&
	     _IOC_NR(cmd) > HANTRO_VCMD_IOC_MAXNR))
		return -ENOTTY;

		/*
		 * the direction is a bitmask, and VERIFY_WRITE catches R/W
		 * transfers. `Type' is user-oriented, while
		 * access_ok is kernel-oriented, so the concept of "read" and
		 * "write" is reversed
		 */
#if (KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE)
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));
#else
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
#endif
	if (err)
		return -EFAULT;

	switch (cmd) {
	case HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER: {
		struct cmdbuf_mem_parameter local_cmdbuf_mem_data;

		LOG_DBG(" VCMD Reserve CMDBUF\n");
		local_cmdbuf_mem_data.cmdbuf_unit_size = CMDBUF_MAX_SIZE;
		local_cmdbuf_mem_data.status_cmdbuf_unit_size = CMDBUF_MAX_SIZE;
		local_cmdbuf_mem_data.cmdbuf_total_size =
			CMDBUF_POOL_TOTAL_SIZE;
		local_cmdbuf_mem_data.status_cmdbuf_total_size =
			CMDBUF_POOL_TOTAL_SIZE;
		local_cmdbuf_mem_data.phy_status_cmdbuf_addr =
			vcmd_status_buf_mem_pool.phy_address; // + VCMD_SYSTEM_PORT_OFFSET;
		local_cmdbuf_mem_data.phy_cmdbuf_addr =
			vcmd_buf_mem_pool.phy_address; // + VCMD_SYSTEM_PORT_OFFSET;
		if (mmu_enable) {
			local_cmdbuf_mem_data.mmu_phy_status_cmdbuf_addr =
				vcmd_status_buf_mem_pool.mmu_bus_address;
			local_cmdbuf_mem_data.mmu_phy_cmdbuf_addr =
				vcmd_buf_mem_pool.mmu_bus_address;
		} else {
			/* When SMMU enabled, the bus_address actually is IOVA */
			local_cmdbuf_mem_data.mmu_phy_status_cmdbuf_addr = vcmd_status_buf_mem_pool.bus_address;
			local_cmdbuf_mem_data.mmu_phy_cmdbuf_addr = vcmd_buf_mem_pool.bus_address;
		}
		local_cmdbuf_mem_data.base_ddr_addr = base_ddr_addr;
		retval = copy_to_user((struct cmdbuf_mem_parameter __user *)arg,
			     &local_cmdbuf_mem_data,
			     sizeof(struct cmdbuf_mem_parameter));
		if (retval) {
			LOG_DBG("copy_to_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		break;
	}
	case HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER: {
		struct config_parameter input_para;

		LOG_DBG(" VCMD get vcmd config parameter\n");
		retval = copy_from_user(&input_para,
			       (struct config_parameter __user *)arg,
			       sizeof(struct config_parameter));
		if (retval) {
			LOG_DBG("copy_from_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		if (vcmd_type_core_num[input_para.module_type]) {
			input_para.submodule_main_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_main_addr;
			input_para.submodule_dec400_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_dec400_addr;
			input_para.submodule_L2Cache_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_L2Cache_addr;
			input_para.submodule_MMU_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_MMU_addr;
			input_para.submodule_MMUWrite_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_MMUWrite_addr;
			input_para.submodule_axife_addr =
				vcmd_manager[input_para.module_type][0]
					->vcmd_core_cfg.submodule_axife_addr;
			input_para.config_status_cmdbuf_id =
				vcmd_manager[input_para.module_type][0]
					->status_cmdbuf_id;
			input_para.vcmd_hw_version_id =
				vcmd_manager[input_para.module_type][0]
					->hw_version_id;
			input_para.vcmd_core_num =
				vcmd_type_core_num[input_para.module_type];
		} else {
			input_para.submodule_main_addr = 0xffff;
			input_para.submodule_dec400_addr = 0xffff;
			input_para.submodule_L2Cache_addr = 0xffff;
			input_para.submodule_MMU_addr = 0xffff;
			input_para.submodule_MMUWrite_addr = 0xffff;
			input_para.submodule_axife_addr = 0xffff;
			input_para.config_status_cmdbuf_id = 0;
			input_para.vcmd_core_num = 0;
			input_para.vcmd_hw_version_id = HW_ID_1_0_C;
		}
		retval = copy_to_user((struct config_parameter __user *)arg, &input_para,
			     sizeof(struct config_parameter));
		if (retval) {
			LOG_DBG("copy_to_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		break;
	}
	case HANTRO_VCMD_IOCH_RESERVE_CMDBUF: {
		int ret;
		struct exchange_parameter input_para;

		retval = copy_from_user(&input_para,
			       (struct exchange_parameter __user *)arg,
			       sizeof(struct exchange_parameter));
		if (retval) {
			LOG_DBG("copy_from_user failed, returned %li\n", retval);
			return -EFAULT;
		}
		ret = reserve_cmdbuf(filp, &input_para);
		if (ret == 0) {
			retval = copy_to_user((struct exchange_parameter __user *)arg,
					&input_para,
					sizeof(struct exchange_parameter));
			if (retval) {
				LOG_DBG("copy_to_user failed, returned %li\n", retval);
				return -EFAULT;
			}
		}
		LOG_DBG("VCMD Reserve CMDBUF %d\n", input_para.cmdbuf_id);
		return ret;
	}

	case HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF: {
		struct exchange_parameter input_para;
		long ret;

		LOG_DBG(" VCMD Reserve CMDBUF\n");
		retval = copy_from_user(&input_para,
			       (struct exchange_parameter __user *)arg,
			       sizeof(struct exchange_parameter));
		if (retval) {
			LOG_DBG("copy_from_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		LOG_DBG("VCMD link and run cmdbuf\n");
		ret = link_and_run_cmdbuf(filp, &input_para);
		retval = copy_to_user((struct exchange_parameter __user *)arg,
			     &input_para, sizeof(struct exchange_parameter));
		if (retval) {
			LOG_DBG("copy_to_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		return ret;
		//break;
	}

	case HANTRO_VCMD_IOCH_WAIT_CMDBUF: {
		u16 cmdbuf_id;
		unsigned int tmp;
		u32 irq_status_ret = 0;

		__get_user(cmdbuf_id, (u16 __user *)arg);
		/*high 16 bits are core id, low 16 bits are cmdbuf_id*/

		LOG_DBG("VCMD wait for CMDBUF finishing.\n");

		//TODO
		tmp = wait_cmdbuf_ready(filp, cmdbuf_id, &irq_status_ret);
		LOG_TRACE("wait_cmdbuf_ready for decoder [pid=%d][%u], status:%x\n", current->pid, cmdbuf_id, irq_status_ret);
		cmdbuf_id = (u16)irq_status_ret;
		if (tmp == 0) {
			__put_user(cmdbuf_id, (u16 __user *)arg);
			return tmp; //return core_id
		}
		//__put_user(0, (u16 __user *)arg);
		return -1;

		break;
	}
	case HANTRO_VCMD_IOCH_RELEASE_CMDBUF: {
		u16 cmdbuf_id;

		__get_user(cmdbuf_id, (u16 __user *)arg);
		/*16 bits are cmdbuf_id*/

		LOG_DBG("VCMD release CMDBUF\n");

		release_cmdbuf(filp, cmdbuf_id);
		return 0;
		//break;
	}
	case HANTRO_VCMD_IOCH_POLLING_CMDBUF: {
		u16 core_id;

		__get_user(core_id, (u16 __user *)arg);
		/*16 bits are cmdbuf_id*/
		if (core_id >= total_vcmd_core_num)
			return -1;
		hantrovcmd_isr(core_id, &hantrovcmd_data[core_id]);
		return 0;
		//break;
	}
	default:
		break;
	}
	return 0;
}

int hantrovcmd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = 0;
	dma_addr_t phy_addr = (vma->vm_pgoff << PAGE_SHIFT);
	dma_addr_t bus_addr = 0;
	u32* cpu_vaddr = NULL;
	int ret = 0;

	LOG_DBG("hantrovcmd_mmap, phy_addr=%llx\n", (long long unsigned int)phy_addr);

	/** find the dma_addr that will be mmapped*/
	if (phy_addr == vcmd_status_buf_mem_pool.phy_address) {
		cpu_vaddr = vcmd_status_buf_mem_pool.virtual_address;
		bus_addr = vcmd_status_buf_mem_pool.bus_address;
		size = vcmd_status_buf_mem_pool.size;
	} else if(phy_addr == vcmd_buf_mem_pool.phy_address) {
		cpu_vaddr = vcmd_buf_mem_pool.virtual_address;
		bus_addr = vcmd_buf_mem_pool.bus_address;
		size = vcmd_buf_mem_pool.size;
	} else {
		LOG_ERR("hantrovcmd_mmap, Unknown phy_addr(%llx), mmap failed\n", (long long unsigned int)phy_addr);
		return -ENXIO;
	}

	LOG_DBG("hantrovcmd_mmap, bus_addr=0x%llx, phy_addr=0x%llx, cpu_vaddr=0x%llx, size=%lx\n"
		, (long long unsigned int)bus_addr, (long long unsigned int)phy_addr, (unsigned long long)cpu_vaddr, size);

	/** vm_pgoff must be set as 0*/
	vma->vm_pgoff = 0;
	ret = dma_mmap_coherent(&platformdev->dev, vma, cpu_vaddr, bus_addr, size);
	if (ret) {
		LOG_ERR("hantrovcmd_mmap, dma_mmap_coherent failed, ret:%d\n", ret);
		return ret;
	}

	return ret;
}

/*******process manager object management*******/

static struct process_manager_obj *create_process_manager_obj(void)
{
	struct process_manager_obj *process_manager_obj = NULL;

	process_manager_obj = kmalloc(sizeof(*process_manager_obj), GFP_ATOMIC);
	if (!process_manager_obj) {
		LOG_DBG("%s\n", "kmalloc for process_manager_obj fail!");
		return process_manager_obj;
	}
	memset(process_manager_obj, 0, sizeof(struct process_manager_obj));
	return process_manager_obj;
}

static void
free_process_manager_obj(struct process_manager_obj *process_manager_obj)
{
	if (!process_manager_obj) {
		LOG_DBG("%s\n", "free_process_manager_obj NULL");
		return;
	}
	LOG_DBG("free process_manager_obj %px, filp=%px\n", process_manager_obj, process_manager_obj->filp);
	//free current cmdbuf_obj
	kfree(process_manager_obj);
	return;
}

static bi_list_node *create_process_manager_node(void)
{
	bi_list_node *current_node = NULL;
	struct process_manager_obj *process_manager_obj = NULL;

	process_manager_obj = create_process_manager_obj();
	if (!process_manager_obj) {
		LOG_ERR("%s\n", "create_process_manager_obj fail!");
		return NULL;
	}
	process_manager_obj->total_exe_time = 0;
	spin_lock_init(&process_manager_obj->spinlock);
	init_waitqueue_head(&process_manager_obj->wait_queue);
	current_node = bi_list_create_node();
	if (!current_node) {
		LOG_ERR("%s\n", "bi_list_create_node fail!");
		free_process_manager_obj(process_manager_obj);
		return NULL;
	}
	current_node->data = (void *)process_manager_obj;
	return current_node;
}

static void free_process_manager_node(bi_list_node *process_node)
{
	struct process_manager_obj *process_manager_obj = NULL;

	if (!process_node) {
		LOG_DBG("%s\n", "free_process_manager_node NULL");
		return;
	}
	process_manager_obj = (struct process_manager_obj *)process_node->data;
	//free struct process_manager_obj
	free_process_manager_obj(process_manager_obj);
	//free current process_manager_obj entity.
	bi_list_free_node(process_node);
	return;
}

static long release_process_node_cleanup(bi_list *list)
{
	bi_list_node *new_process_node = NULL;

	while (1) {
		new_process_node = list->head;
		if (!new_process_node)
			break;
		//remove node from list
		bi_list_remove_node(list, new_process_node);
		//remove node from list
		free_process_manager_node(new_process_node);
	}
	return 0;
}

static void create_kernel_process_manager(void)
{
	bi_list_node *process_manager_node;
	struct process_manager_obj *process_manager_obj = NULL;

	process_manager_node = create_process_manager_node();
	process_manager_obj =
		(struct process_manager_obj *)process_manager_node->data;
	process_manager_obj->filp = NULL;
	LOG_DBG("create root process_manager_obj %px\n", process_manager_obj);
	bi_list_insert_node_tail(&global_process_manager, process_manager_node);
}

/* Update the last JMP cmd in cmdbuf_ojb in
 * order to jump to next_cmdbuf_obj.
 */
static void cmdbuf_update_jmp_cmd(int hw_version_id,
				  struct cmdbuf_obj *cmdbuf_obj,
				  struct cmdbuf_obj *next_cmdbuf_obj,
				  int jmp_IE_1)
{
	u32 *jmp_addr;
	u32 operation_code;

	if (!cmdbuf_obj)
		return;

	if (cmdbuf_obj->has_end_cmdbuf == 0) {
		//need to link, current cmdbuf link to next cmdbuf
		jmp_addr = cmdbuf_obj->cmdbuf_virtual_address +
			   (cmdbuf_obj->cmdbuf_size / 4);
		if (!next_cmdbuf_obj) {
			// If next cmdbuf is not available, set the RDY to 0.
			operation_code = *(jmp_addr - 4);
			operation_code >>= 16;
			operation_code <<= 16;
			*(jmp_addr - 4) = (u32)(operation_code & ~JMP_RDY_1);
		} else {
			if (hw_version_id > HW_ID_1_0_C)
				//set next cmdbuf id
				*(jmp_addr - 1) = next_cmdbuf_obj->cmdbuf_id;
			if (mmu_enable) {
				if (sizeof(size_t) == 8) {
					*(jmp_addr - 2) = (u32)(
						(u64)(next_cmdbuf_obj
							      ->mmu_cmdbuf_bus_address) >>
						32);
				} else {
					*(jmp_addr - 2) = 0;
				}
				*(jmp_addr - 3) = (u32)(
					next_cmdbuf_obj->mmu_cmdbuf_bus_address);
			} else {
				if (sizeof(size_t) == 8) {
					*(jmp_addr - 2) = (u32)(
						(u64)(next_cmdbuf_obj
							      ->cmdbuf_bus_address -
						      base_ddr_addr) >>
						32);
				} else
					*(jmp_addr - 2) = 0;

				*(jmp_addr - 3) = (u32)(
					next_cmdbuf_obj->cmdbuf_bus_address -
					base_ddr_addr);
			}
			operation_code = *(jmp_addr - 4);
			operation_code >>= 16;
			operation_code <<= 16;
			*(jmp_addr - 4) =
				(u32)(operation_code | JMP_RDY_1 | jmp_IE_1 |
				      ((next_cmdbuf_obj->cmdbuf_size + 7) / 8));
		}

#ifdef VCMD_DEBUG_INTERNAL
		{
			u32 i;

			LOG_INFO("vcmd link, last cmdbuf content\n");
			for (i = cmdbuf_obj->cmdbuf_size / 4 - 8;
			     i < cmdbuf_obj->cmdbuf_size / 4; i++) {
				LOG_INFO("current linked cmdbuf data %d =0x%x\n",
					i,
				       *(cmdbuf_obj->cmdbuf_virtual_address +
					 i));
			}
		}
#endif
	}
}

/* delink given cmd buffer (cmdbuf_node) and remove it from list.
 *	 Also modify the last JMP of buf P to point to cmdbuf N.
 *	 Used when a process is terminated but there are pending cmd
 *	 bufs in vmcd list.
 *	 E.g.,
 *	 before:

 *		 L->L->...->P->X->N->        ...        ->L
 *		 ^             ^                          ^
 *		 head          cmdbuf_node                 tail

 *	 end:

 *		 L->L->...->P->N->        ...        ->L
 *		 ^                                     ^
 *		 head                                  tail

 *	 Return: pointer to N or NULL if N doesn't exist.
 */
static void vcmd_delink_rm_cmdbuf(struct hantrovcmd_dev *dev,
				  bi_list_node *cmdbuf_node)
{
	bi_list *list = &dev->list_manager;
	struct cmdbuf_obj *cmdbuf_obj = (struct cmdbuf_obj *)cmdbuf_node->data;
	bi_list_node *prev = cmdbuf_node->previous;
	bi_list_node *next = cmdbuf_node->next;
	unsigned long flags = 0;

	LOG_DBG("Delink and remove cmdbuf [%d] from vcmd list.\n",
	       cmdbuf_obj->cmdbuf_id);
#ifdef HANTRO_VCMD_DRIVER_DEBUG
	if (prev) {
		LOG_DBG("prev cmdbuf [%d].\n",
		       ((struct cmdbuf_obj *)prev->data)->cmdbuf_id);
	} else {
		LOG_DBG("NO prev cmdbuf.\n");
	}
	if (next) {
		LOG_DBG("next cmdbuf [%d].\n",
		       ((struct cmdbuf_obj *)next->data)->cmdbuf_id);
	} else {
		LOG_DBG("NO next cmdbuf.\n");
	}
#endif

	bi_list_remove_node(list, cmdbuf_node);
	LOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
	global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
	UNLOCK_CMDBUF_NODE(cmdbuf_obj->cmdbuf_id, flags);
	free_cmdbuf_node(cmdbuf_node);

	cmdbuf_update_jmp_cmd(dev->hw_version_id, prev ? prev->data : NULL,
			      next ? next->data : NULL,
			      dev->duration_without_int >
				      INT_MIN_SUM_OF_IMAGE_SIZE);
}

int hantrovcmd_open(struct inode *inode, struct file *filp)
{
	int result = 0;
	struct hantrovcmd_dev *dev = hantrovcmd_data;
	bi_list_node *process_manager_node;
	unsigned long flags;
	struct process_manager_obj *process_manager_obj = NULL;
	struct filp_priv *fp_priv = (struct filp_priv *)filp->private_data;

	fp_priv->dev = (void *)dev;

	process_manager_node = create_process_manager_node();
	if (!process_manager_node) {
		pr_err("%d: create_process_manager_node failed\n", __LINE__);
		return -1;
	}
	process_manager_obj =
		(struct process_manager_obj *)process_manager_node->data;
	process_manager_obj->filp = filp;
	spin_lock_irqsave(&vcmd_process_manager_lock, flags);
	bi_list_insert_node_tail(&global_process_manager, process_manager_node);
	spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);

	LOG_DBG("dev opened\n");
	LOG_DBG("opened::process obj %px for filp %px\n", (void *)process_manager_obj, (void *)filp);
	return result;
}

int hantrovcmd_release(struct inode *inode, struct file *filp)
{
	struct filp_priv *fp_priv = NULL;
	struct hantrovcmd_dev *dev = NULL;
	u32 core_id = 0;
	u32 release_cmdbuf_num = 0;
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj_temp = NULL;
	bi_list_node *process_manager_node = NULL;
	struct process_manager_obj *process_manager_obj = NULL;
	int vcmd_aborted = 0; // vcmd is aborted in this function
	struct cmdbuf_obj *restart_cmdbuf = NULL;
	bi_list_node *next_cmdbuf = NULL;
	bi_list_node *removed_node = NULL;

	unsigned long flags;
	long retVal = 0;

	if (!filp || !filp->private_data) {
		LOG_ERR("vcmd release, filp=%px, private_data=%px\n", filp, filp ? filp->private_data : NULL);
		return EFAULT;
	}

	fp_priv = (struct filp_priv *)filp->private_data;
	dev = (struct hantrovcmd_dev *)fp_priv->dev;

	if (dev->hw_version_id >= HW_ID_1_2_1) {
		for (core_id = 0; core_id < total_vcmd_core_num; core_id++) {
			if (!(&dev[core_id]))
				continue;

			if (down_interruptible(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]))
				return -ERESTARTSYS;

			spin_lock_irqsave(dev[core_id].spinlock, flags);
			new_cmdbuf_node = dev[core_id].list_manager.head;
			while (1) {
				if (!new_cmdbuf_node)
					break;
				next_cmdbuf = new_cmdbuf_node->next;
				cmdbuf_obj_temp = (struct cmdbuf_obj *)new_cmdbuf_node->data;
				LOG_DBG("Process %p is releasing: checking cmdbuf %d of process %p.\n",
						filp,
						cmdbuf_obj_temp->cmdbuf_id,
						cmdbuf_obj_temp->filp);

				if (dev[core_id].hwregs && (cmdbuf_obj_temp->filp == filp)) {
					if (cmdbuf_obj_temp->cmdbuf_run_done) {
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked == 0) {
						cmdbuf_obj_temp->cmdbuf_data_linked = 1;
						cmdbuf_obj_temp->cmdbuf_run_done = 1;
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked == 1 &&
								dev[core_id].working_state == WORKING_STATE_IDLE) {
						vcmd_delink_rm_cmdbuf(&dev[core_id], new_cmdbuf_node);
						if (restart_cmdbuf == cmdbuf_obj_temp) {
							restart_cmdbuf = next_cmdbuf ? next_cmdbuf->data : NULL;
						}
						if (restart_cmdbuf) {
							LOG_DBG("Set restart cmdbuf [%d].\n", restart_cmdbuf->cmdbuf_id);
						} else {
							LOG_DBG("Set restart cmdbuf to NULL.\n");
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked == 1 &&
						       dev[core_id].working_state == WORKING_STATE_WORKING) {
						bi_list_node *last_cmdbuf_node = NULL;
						bi_list_node *done_cmdbuf_node = NULL;
						int abort_cmdbuf_id;
						int loop_count = 0;

						//abort the vcmd and wait
						LOG_DBG("Abort due to linked cmdbuf %d of current process.\n", cmdbuf_obj_temp->cmdbuf_id);
						printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "Before trigger to 0");
						// disable abort interrupt
						//vcmd_write_register_value((const void *)dev[core_id].hwregs,
						//dev[core_id].reg_mirror,HWIF_VCMD_IRQ_ABORT_EN,0);
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_START_TRIGGER,
							0);
						vcmd_aborted = 1;

						printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "After trigger to 0");
						// Wait vcmd core aborted and vcmd enters IDLE mode.
						//while (dev[core_id].working_state != WORKING_STATE_IDLE) {
						while (vcmd_get_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_WORK_STATE)) {
							loop_count++;
							if (!(loop_count % 10)) {
								LOG_ERR("expected idle state, but irq status = 0x%0x\n",
									vcmd_read_reg((const void *)dev[core_id].hwregs,
									VCMD_REGISTER_INT_STATUS_OFFSET));
								LOG_ERR("vcmd current status is %d\n",
										vcmd_get_register_value((const void *)dev[core_id].hwregs,
											dev[core_id].reg_mirror,
											HWIF_VCMD_WORK_STATE));
							}
							mdelay(10); // wait 10ms
							 // too long
							if (loop_count > 100) {
								LOG_ERR("too long before vcmd core to IDLE state\n");
								spin_unlock_irqrestore(dev[core_id].spinlock, flags);
								up(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]);
								return -ERESTARTSYS;
							}
						}
						dev[core_id].working_state = WORKING_STATE_IDLE;
						// clear interrupt & restore abort_e
						if (vcmd_get_register_value((const void *)dev[core_id].hwregs,
								dev[core_id].reg_mirror,
							    HWIF_VCMD_IRQ_ABORT)) {
							LOG_DBG("Abort interrupt triggered, now clear it to avoid abort int...\n");
							vcmd_write_reg((const void *)dev[core_id].hwregs,
								VCMD_REGISTER_INT_STATUS_OFFSET,
								0x1 << 4);
							LOG_DBG("Now irq status = 0x%0x.\n",
								vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET));
						}
						//printk_vcmd_register_debug((const void *)dev->hwregs, "vcmd status to IDLE");

						abort_cmdbuf_id = vcmd_get_register_value(
							(const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_CMDBUF_EXECUTING_ID);
						LOG_DBG("Abort when executing cmd buf %d.\n", abort_cmdbuf_id);
						dev[core_id].sw_cmdbuf_rdy_num = 0;
						dev[core_id].duration_without_int = 0;
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_EXE_CMDBUF_COUNT,
							0);
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_RDY_CMDBUF_COUNT,
							0);

						/* Mark cmdbuf_run_done to 1 for all
						 * the cmd buf executed.
						 */
						done_cmdbuf_node = dev[core_id].list_manager.head;
						while (done_cmdbuf_node) {
							if (!((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_run_done) {
								((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_run_done = 1;
								((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_data_linked = 0;
								LOG_DBG("Set cmdbuf [%d] cmdbuf_run_done to 1.\n",
									((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_id);
							}
							if (((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_id == abort_cmdbuf_id)
								break;
							done_cmdbuf_node = done_cmdbuf_node->next;
						}
						if (cmdbuf_obj_temp->cmdbuf_run_done) {
							/* current cmdbuf is in fact has been executed,
							 * but due to interrupt is not triggered
							 * the status is not updated.
							 * Just delink and remove it from the list.
							 */
							if (done_cmdbuf_node && done_cmdbuf_node->data) {
								LOG_DBG("done_cmdbuf_node is cmdbuf [%d].\n",
									((struct cmdbuf_obj *)done_cmdbuf_node->data)->cmdbuf_id);
							}
							done_cmdbuf_node = done_cmdbuf_node->next;
							if (done_cmdbuf_node)
								restart_cmdbuf = (struct cmdbuf_obj *)done_cmdbuf_node->data;
							if (restart_cmdbuf) {
								LOG_DBG("Set restart cmdbuf [%d] via if.\n", restart_cmdbuf->cmdbuf_id);
							}
						} else {
							last_cmdbuf_node = new_cmdbuf_node;
							/* cmd buf num from aborted cmd buf to current cmdbuf_obj_temp */
							if (cmdbuf_obj_temp->cmdbuf_id != abort_cmdbuf_id) {
								last_cmdbuf_node = new_cmdbuf_node->previous;
								while (last_cmdbuf_node &&
										((struct cmdbuf_obj *)last_cmdbuf_node->data)->cmdbuf_id != abort_cmdbuf_id) {
									restart_cmdbuf = (struct cmdbuf_obj *)last_cmdbuf_node->data;
									last_cmdbuf_node = last_cmdbuf_node->previous;
									dev[core_id].sw_cmdbuf_rdy_num++;
									dev[core_id].duration_without_int += restart_cmdbuf->executing_time;
									LOG_DBG("Keep valid cmdbuf [%d] in the list.\n", restart_cmdbuf->cmdbuf_id);
								}
							}
							if (restart_cmdbuf) {
								LOG_DBG("Set restart cmdbuf [%d] via else.\n", restart_cmdbuf->cmdbuf_id);
							}
						}

						//remove first linked cmdbuf from list
						vcmd_delink_rm_cmdbuf(&dev[core_id], new_cmdbuf_node);
					}
					release_cmdbuf_num++;
					LOG_DBG("release reserved cmdbuf\n");
				} else if (vcmd_aborted && !cmdbuf_obj_temp->cmdbuf_run_done) {
					/* VCMD is aborted, need to re-calculate
					 * the duration_without_int
					 */
					if (!restart_cmdbuf)
						/* first cmdbuf to be restarted */
						restart_cmdbuf = cmdbuf_obj_temp;
					dev[core_id].duration_without_int += cmdbuf_obj_temp->executing_time;
					dev[core_id].sw_cmdbuf_rdy_num++;
				}
				new_cmdbuf_node = next_cmdbuf;
			}

			if (restart_cmdbuf) {
				u32 irq_status1, irq_status2;

				LOG_DBG("Restart from cmdbuf [%d] after aborting.\n", restart_cmdbuf->cmdbuf_id);

				irq_status1 = vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET);
				vcmd_write_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET, irq_status1);
				irq_status2 = vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET);
				LOG_DBG("Clear irq status from 0x%0x -> 0x%0x\n", irq_status1, irq_status2);
				if (mmu_enable) {
					vcmd_write_register_value((const void *)dev[core_id].hwregs,
						dev[core_id].reg_mirror,
						HWIF_VCMD_EXECUTING_CMD_ADDR,
						(u32)(restart_cmdbuf->mmu_cmdbuf_bus_address));
					if (sizeof(size_t) == 8)
						vcmd_write_register_value(
							(const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
							(u32)((u64)(restart_cmdbuf->mmu_cmdbuf_bus_address) >> 32));
					else
						vcmd_write_register_value(
							(const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
							0);
				} else {
					vcmd_write_register_value(
						(const void *)dev[core_id].hwregs,
						dev[core_id].reg_mirror,
						HWIF_VCMD_EXECUTING_CMD_ADDR,
						(u32)(restart_cmdbuf->cmdbuf_bus_address -
						      base_ddr_addr));
					if (sizeof(size_t) == 8)
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
							(u32)((u64)(restart_cmdbuf->cmdbuf_bus_address - base_ddr_addr) >> 32));
					else
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
							0);
				}
				vcmd_write_register_value(
					(const void *)dev[core_id].hwregs,
					dev[core_id].reg_mirror,
					HWIF_VCMD_EXE_CMDBUF_COUNT, 0);
				vcmd_write_register_value(
					(const void *)dev[core_id].hwregs,
					dev[core_id].reg_mirror,
					HWIF_VCMD_EXE_CMDBUF_LENGTH,
					(u32)((restart_cmdbuf->cmdbuf_size + 7) / 8));
				vcmd_write_register_value(
					(const void *)dev[core_id].hwregs,
					dev[core_id].reg_mirror,
					HWIF_VCMD_CMDBUF_EXECUTING_ID,
					restart_cmdbuf->cmdbuf_id);
				LOG_DBG("====dev->sw_cmdbuf_rdy_num is %d\n", dev[core_id].sw_cmdbuf_rdy_num);
				vcmd_write_register_value(
					(const void *)dev[core_id].hwregs,
					dev[core_id].reg_mirror,
					HWIF_VCMD_RDY_CMDBUF_COUNT,
					dev[core_id].sw_cmdbuf_rdy_num);
				printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "before restart");
				vcmd_write_register_value(
					(const void *)dev[core_id].hwregs,
					dev[core_id].reg_mirror,
					HWIF_VCMD_START_TRIGGER, 1);

				LOG_DBG("Restart from cmdbuf [%d] after aborting: start trigger = %d.\n",
					restart_cmdbuf->cmdbuf_id,
					vcmd_get_register_value(
						(const void *)dev[core_id].hwregs,
						dev[core_id].reg_mirror,
						HWIF_VCMD_START_TRIGGER));
				LOG_DBG("dev state from %d -> WORKING.\n", dev[core_id].working_state);
				dev[core_id].working_state = WORKING_STATE_WORKING;
				printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "after restart");
			} else {
				LOG_DBG("No more command buffer to be restarted!\n");
			}
			spin_unlock_irqrestore(dev[core_id].spinlock, flags);
			// VCMD aborted but not restarted, nedd to wake up
			if (vcmd_aborted && !restart_cmdbuf)
				wake_up_interruptible_all(dev[core_id].wait_queue);

			up(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]);
		}
	} else {
		for (core_id = 0; core_id < total_vcmd_core_num; core_id++) {
			if ((&dev[core_id]) == NULL)
				continue;

			if (down_interruptible(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]))
				return -ERESTARTSYS;

			spin_lock_irqsave(dev[core_id].spinlock, flags);
			new_cmdbuf_node = dev[core_id].list_manager.head;
			while (1) {
				if (!new_cmdbuf_node)
					break;
				cmdbuf_obj_temp = (struct cmdbuf_obj *)new_cmdbuf_node->data;
				next_cmdbuf = new_cmdbuf_node->next;
				if (dev[core_id].hwregs && (cmdbuf_obj_temp->filp == filp)) {
					if (cmdbuf_obj_temp->cmdbuf_run_done) {
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked == 0) {
						cmdbuf_obj_temp->cmdbuf_data_linked = 1;
						cmdbuf_obj_temp->cmdbuf_run_done = 1;
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked == 1 &&
								dev[core_id].working_state == WORKING_STATE_IDLE) {
						cmdbuf_obj_temp->cmdbuf_run_done = 1;
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
					} else if (cmdbuf_obj_temp->cmdbuf_data_linked ==1 &&
							dev[core_id].working_state == WORKING_STATE_WORKING) {
						bi_list_node *last_cmdbuf_node;
						u32 record_last_cmdbuf_rdy_num;
						//abort the vcmd and wait
						vcmd_write_register_value((const void *)dev[core_id].hwregs,
							dev[core_id].reg_mirror,
							HWIF_VCMD_START_TRIGGER,
							0);
						if (wait_event_interruptible(*dev[core_id].wait_abort_queue, wait_abort_rdy(&dev[core_id]))) {
							spin_unlock_irqrestore(dev[core_id].spinlock, flags);
							up(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]);
							return -ERESTARTSYS;
						}
						cmdbuf_obj_temp->cmdbuf_run_done = 1;
						cmdbuf_obj_temp->cmdbuf_need_remove = 1;
						retVal = release_cmdbuf_node(&dev[core_id].list_manager, new_cmdbuf_node);
						if (retVal == 1) {
							cmdbuf_obj_temp->process_manager_obj = NULL;
						}
						//link
						last_cmdbuf_node = find_last_linked_cmdbuf(dev[core_id].list_manager.tail);
						record_last_cmdbuf_rdy_num = dev[core_id].sw_cmdbuf_rdy_num;
						vcmd_link_cmdbuf(&dev[core_id], last_cmdbuf_node);
						//re-run
						if (dev[core_id].sw_cmdbuf_rdy_num)
							vcmd_start(&dev[core_id], last_cmdbuf_node);
					}
					release_cmdbuf_num++;
					LOG_DBG("release reserved cmdbuf\n");
				}
				new_cmdbuf_node = next_cmdbuf;
			}
			spin_unlock_irqrestore(dev[core_id].spinlock, flags);

			up(&vcmd_reserve_cmdbuf_sem[dev[core_id].vcmd_core_cfg.sub_module_type]);
		}
	}

	//pr_info("release_cmdbuf_num\n");
	if (release_cmdbuf_num)
		wake_up_interruptible_all(&vcmd_cmdbuf_memory_wait);
	spin_lock_irqsave(&vcmd_process_manager_lock, flags);
	process_manager_node = global_process_manager.head;
	while (1) {
		if (!process_manager_node)
			break;
		process_manager_obj = (struct process_manager_obj *)process_manager_node->data;
		if (process_manager_obj->filp == filp) {
			removed_node = process_manager_node;
			break;
		}
		process_manager_node = process_manager_node->next;
	}
	//remove node from list
	if (removed_node) {
		LOG_DBG("process node %p for filp to be removed: %px\n",
			(void *)removed_node, (void *)process_manager_obj->filp);
		bi_list_remove_node(&global_process_manager, removed_node);
	}
	spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
	if (removed_node) {
		free_process_manager_node(removed_node);
	} else {
		LOG_WARN("no process node release for filp %px\n", filp);
	}

	return 0;
}

/*-------------------------------------------
 * Function name   : vcmd_pcie_init
 * Description     : Initialize PCI Hw access

 * Return type     : int
 *　-------------------------------------------
 */
static int vcmd_init(void)
{
	int i = 0;
	struct kernel_addr_desc addr;
	/* PCI device structure. */
	struct pci_dev *g_vcmd_dev = NULL;
	/* PCI base register address (Hardware address) */
	unsigned long g_vcmd_base_hdwr;
	/* PCI base register address (memalloc) */
	unsigned long g_vcmd_base_ddr_hw;
	/* Base register address Length */
	u32 g_vcmd_base_len;

	if (pcie) {
		g_vcmd_dev =
			pci_get_device(PCI_VENDOR_ID_HANTRO,
				       PCI_DEVICE_ID_HANTRO_PCI, g_vcmd_dev);
		if (!g_vcmd_dev) {
			LOG_ERR("Init: Hardware not found.\n");
			goto out;
		}

		if (pci_enable_device(g_vcmd_dev) < 0) {
			LOG_ERR("Init: Device not enabled.\n");
			goto out;
		}

		g_vcmd_base_hdwr = pci_resource_start(g_vcmd_dev, PCI_H2_BAR);
		if (g_vcmd_base_hdwr < 0) {
			LOG_INFO("Init: Base Address not set.\n");
			goto out_pci_disable_device;
		}
		LOG_INFO("Base hw val 0x%lx\n", g_vcmd_base_hdwr);

		g_vcmd_base_len = pci_resource_len(g_vcmd_dev, PCI_H2_BAR);
		LOG_INFO("Base hw len 0x%x\n", g_vcmd_base_len);

		for (i = 0; i < total_vcmd_core_num; i++)
			//the offset is based on which bus interface is chosen
			vcmd_core_array[i].vcmd_base_addr =
				g_vcmd_base_hdwr +
				vcmd_core_array[i].vcmd_base_addr;

		g_vcmd_base_ddr_hw =
			pci_resource_start(g_vcmd_dev, PCI_DDR_BAR);
		if (g_vcmd_base_ddr_hw == 0) {
			LOG_INFO("PcieInit: Base Address not set.\n");
			goto out_pci_disable_device;
		}
		base_ddr_addr = g_vcmd_base_ddr_hw;
		g_vcmd_base_len = pci_resource_len(g_vcmd_dev, PCI_DDR_BAR);

		LOG_INFO("Base memory val 0x%lx\n", g_vcmd_base_ddr_hw);
		LOG_INFO("Base memory len 0x%x\n", g_vcmd_base_len);

#ifdef EMU
		vcmd_buf_mem_pool.bus_address = g_vcmd_base_ddr_hw + 0x4800000;
#else
		vcmd_buf_mem_pool.bus_address = g_vcmd_base_ddr_hw + 0x800000;
#endif
		vcmd_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
		if (!request_mem_region(vcmd_buf_mem_pool.bus_address,
					vcmd_buf_mem_pool.size,
					DEC_VCMD_DRV_NAME)) {
			LOG_INFO("Init: failed to request hw region vcmd_buf_mem_pool.\n");
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_buf_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)vcmd_buf_mem_pool.bus_address);
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
		vcmd_buf_mem_pool.virtual_address =
			(u32 __force *)ioremap_nocache(
				vcmd_buf_mem_pool.bus_address,
				vcmd_buf_mem_pool.size);
#else
		vcmd_buf_mem_pool.virtual_address = (u32 __force *)ioremap(
			vcmd_buf_mem_pool.bus_address, vcmd_buf_mem_pool.size);
#endif

		if (!vcmd_buf_mem_pool.virtual_address) {
			LOG_INFO("Init: failed to ioremap.\n");
			release_mem_region(vcmd_buf_mem_pool.bus_address,
					   vcmd_buf_mem_pool.size);
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_buf_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)vcmd_buf_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address =
				vcmd_buf_mem_pool.bus_address - gBaseDDRHw;
			addr.size = vcmd_buf_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK)
				goto out_pci_disable_device;
			vcmd_buf_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_buf_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)
				       vcmd_buf_mem_pool.mmu_bus_address);
		}

#ifdef EMU
		vcmd_status_buf_mem_pool.bus_address =
			g_vcmd_base_ddr_hw + 0x4800000 + CMDBUF_POOL_TOTAL_SIZE;
#else
		vcmd_status_buf_mem_pool.bus_address =
			g_vcmd_base_ddr_hw + 0x800000 + CMDBUF_POOL_TOTAL_SIZE;
#endif
		vcmd_status_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
		if (!request_mem_region(vcmd_status_buf_mem_pool.bus_address,
					vcmd_status_buf_mem_pool.size,
					DEC_VCMD_DRV_NAME)) {
			LOG_INFO("Init: failed to request hw region vcmd_status_buf_mem_pool.\n");
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_status_buf_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)
			  vcmd_status_buf_mem_pool.bus_address);
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
		vcmd_status_buf_mem_pool.virtual_address =
			(u32 __force *)ioremap_nocache(vcmd_status_buf_mem_pool.bus_address,
				vcmd_status_buf_mem_pool.size);
#else
		vcmd_status_buf_mem_pool.virtual_address =
			(u32 __force *)ioremap(vcmd_status_buf_mem_pool.bus_address,
				vcmd_status_buf_mem_pool.size);
#endif
		if (!vcmd_status_buf_mem_pool.virtual_address) {
			LOG_INFO("Init: failed to ioremap.\n");
			release_mem_region(vcmd_status_buf_mem_pool.bus_address,
					   vcmd_status_buf_mem_pool.size);
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_status_buf_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)
			       vcmd_status_buf_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address =
				vcmd_status_buf_mem_pool.bus_address -
				gBaseDDRHw;
			addr.size = vcmd_status_buf_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK)
				goto out_pci_disable_device;

			vcmd_status_buf_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_status_buf_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)vcmd_status_buf_mem_pool.mmu_bus_address);
		}

#ifdef EMU
		vcmd_registers_mem_pool.bus_address =
			g_vcmd_base_ddr_hw + 0x4800000 +
			CMDBUF_POOL_TOTAL_SIZE * 2;
#else
		vcmd_registers_mem_pool.bus_address =
			g_vcmd_base_ddr_hw + 0x800000 +
			CMDBUF_POOL_TOTAL_SIZE * 2;
#endif
		vcmd_registers_mem_pool.size = CMDBUF_VCMD_REGISTER_TOTAL_SIZE;
		if (!request_mem_region(vcmd_registers_mem_pool.bus_address,
					vcmd_registers_mem_pool.size,
					DEC_VCMD_DRV_NAME)) {
			LOG_INFO("Init: failed to request hw region vcmd_registers_mem_pool.\n");
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_registers_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)vcmd_registers_mem_pool.bus_address);
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
		vcmd_registers_mem_pool.virtual_address =
			(u32 __force *)ioremap_nocache(
				vcmd_registers_mem_pool.bus_address,
				vcmd_registers_mem_pool.size);
#else
		vcmd_registers_mem_pool.virtual_address =
			(u32 __force *)ioremap(vcmd_registers_mem_pool.bus_address,
				vcmd_registers_mem_pool.size);
#endif

		if (!vcmd_registers_mem_pool.virtual_address) {
			LOG_INFO("Init: failed to ioremap.\n");
			release_mem_region(vcmd_registers_mem_pool.bus_address,
					   vcmd_registers_mem_pool.size);
			goto out_pci_disable_device;
		}
		LOG_DBG("Init: vcmd_registers_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)
			       vcmd_registers_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address = vcmd_registers_mem_pool.bus_address -
					   gBaseDDRHw;
			addr.size = vcmd_registers_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK)
				goto out_pci_disable_device;

			vcmd_registers_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_registers_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)
				       vcmd_registers_mem_pool.mmu_bus_address);
		}
	} else {
		/* use dma_alloc_coherent for non-pcie env */
#if 0
		g_vcmd_base_ddr_hw = alloc_base;
		g_vcmd_base_len = alloc_size * 1024 * 1024;
#else
		dma_addr_t dma_handle = 0;
		dma_addr_t dma_handle_d1 = 0;

		vcmd_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;

		/* command buffer */
		vcmd_buf_mem_pool.virtual_address = (u32 *)dma_alloc_coherent(&platformdev->dev,
			vcmd_buf_mem_pool.size, &dma_handle,
			GFP_KERNEL | __GFP_DMA32);
		vcmd_buf_mem_pool.bus_address = (unsigned long long)dma_handle;
		vcmd_buf_mem_pool.phy_address = pfn_to_phys(vmalloc_to_pfn(vcmd_buf_mem_pool.virtual_address));

		if (platformdev_d1) {
			dma_handle_d1 = dma_map_page(&platformdev_d1->dev,
					vmalloc_to_page(vcmd_buf_mem_pool.virtual_address), 0, vcmd_buf_mem_pool.size, DMA_BIDIRECTIONAL);
			if (dma_handle != dma_handle_d1) {
				LOG_ERR("vdec_vcmd: dma address of vcmd buf not the same between d0 and d1\n");
				return -1;
			}
		}

		LOG_DBG("Init: vcmd_buf_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)vcmd_buf_mem_pool.bus_address);
		LOG_DBG("Init: vcmd_buf_mem_pool.phy_address=0x%llx.\n",
			(unsigned long long)vcmd_buf_mem_pool.phy_address);
		LOG_DBG("Init: vcmd_buf_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)vcmd_buf_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address = vcmd_buf_mem_pool.bus_address;
			addr.size = vcmd_buf_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK)
				goto out_pci_disable_device;
			vcmd_buf_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_buf_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)
				       vcmd_buf_mem_pool.mmu_bus_address);
		}

		/* status buffer */
		vcmd_status_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
		vcmd_status_buf_mem_pool.virtual_address =
			(u32 *)dma_alloc_coherent(&platformdev->dev,
						  vcmd_status_buf_mem_pool.size,
						  &dma_handle,
						  GFP_KERNEL | __GFP_DMA32);
		vcmd_status_buf_mem_pool.bus_address = (unsigned long long)dma_handle;
		vcmd_status_buf_mem_pool.phy_address = pfn_to_phys(vmalloc_to_pfn(vcmd_status_buf_mem_pool.virtual_address));

		if (platformdev_d1) {
			dma_handle_d1 = dma_map_page(&platformdev_d1->dev,
					vmalloc_to_page(vcmd_status_buf_mem_pool.virtual_address), 0, vcmd_status_buf_mem_pool.size, DMA_BIDIRECTIONAL);
			if (dma_handle != dma_handle_d1) {
				LOG_ERR("vdec_vcmd: dma address of status buf not the same between d0 and d1\n");
				return -1;
			}
		}

		LOG_DBG("Init: vcmd_status_buf_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)vcmd_status_buf_mem_pool.bus_address);
		LOG_DBG("Init: vcmd_status_buf_mem_pool.phy_address=0x%llx.\n",
			(unsigned long long)vcmd_status_buf_mem_pool.phy_address);
		LOG_DBG("Init: vcmd_status_buf_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)
			       vcmd_status_buf_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address = vcmd_status_buf_mem_pool.bus_address;
			addr.size = vcmd_status_buf_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK)
				goto out_pci_disable_device;

			vcmd_status_buf_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_status_buf_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)vcmd_status_buf_mem_pool
				       .mmu_bus_address);
		}

		/* register buffer */
		vcmd_registers_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
		vcmd_registers_mem_pool.virtual_address =
			(u32 *)dma_alloc_coherent(&platformdev->dev,
						  vcmd_registers_mem_pool.size,
						  &dma_handle,
						  GFP_KERNEL | __GFP_DMA32);
		vcmd_registers_mem_pool.bus_address = (unsigned long long)dma_handle;
		vcmd_registers_mem_pool.phy_address = pfn_to_phys(vmalloc_to_pfn(vcmd_registers_mem_pool.virtual_address));

		if (platformdev_d1) {
			dma_handle_d1 = dma_map_page(&platformdev_d1->dev,
					vmalloc_to_page(vcmd_registers_mem_pool.virtual_address), 0, vcmd_registers_mem_pool.size, DMA_BIDIRECTIONAL);
			if (dma_handle != dma_handle_d1) {
				LOG_ERR("vdec_vcmd: dma address of registers buf not the same between d0 and d1\n");
				return -1;
			}
		}

		LOG_DBG("Init: vcmd_registers_mem_pool.bus_address=0x%llx.\n",
			(unsigned long long)vcmd_registers_mem_pool.bus_address);
		LOG_DBG("Init: vcmd_registers_mem_pool.phy_address=0x%llx.\n",
			(unsigned long long)vcmd_registers_mem_pool.phy_address);
		LOG_DBG("Init: vcmd_registers_mem_pool.virtual_address=0x%llx.\n",
			(unsigned long long)
			       vcmd_registers_mem_pool.virtual_address);
		if (mmu_enable) {
			addr.bus_address = vcmd_registers_mem_pool.bus_address;
			addr.size = vcmd_registers_mem_pool.size;
			if (MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK) {
				goto out_pci_disable_device;
			}
			vcmd_registers_mem_pool.mmu_bus_address =
				addr.mmu_bus_address;
			LOG_DBG("Init: vcmd_registers_mem_pool.mmu_bus_address=0x%llx.\n",
				(unsigned long long)
				       vcmd_registers_mem_pool.mmu_bus_address);
		}
#endif
	}

	return 0;

out_pci_disable_device:
	if (pcie)
		pci_disable_device(g_vcmd_dev);
out:
	return -1;
}

static void vcmd_link_cmdbuf(struct hantrovcmd_dev *dev,
			     bi_list_node *last_linked_cmdbuf_node)
{
	bi_list_node *new_cmdbuf_node = NULL;
	bi_list_node *next_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	struct cmdbuf_obj *next_cmdbuf_obj = NULL;
	u32 *jmp_addr = NULL;
	u32 operation_code;

	new_cmdbuf_node = last_linked_cmdbuf_node;
	//for the first cmdbuf.
	if (new_cmdbuf_node) {
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (cmdbuf_obj->cmdbuf_data_linked == 0) {
			dev->sw_cmdbuf_rdy_num++;
			cmdbuf_obj->cmdbuf_data_linked = 1;
			dev->duration_without_int = 0;
			if (cmdbuf_obj->has_end_cmdbuf == 0) {
				if (cmdbuf_obj->no_normal_int_cmdbuf == 1) {
					dev->duration_without_int =
						cmdbuf_obj->executing_time;
					//maybe nop is modified, so write back.
					if (dev->duration_without_int >=
					    INT_MIN_SUM_OF_IMAGE_SIZE) {
						jmp_addr = cmdbuf_obj->cmdbuf_virtual_address +
							(cmdbuf_obj->cmdbuf_size / 4);
						operation_code =
							*(jmp_addr - 4);
						operation_code = JMP_IE_1 |
								 operation_code;
						*(jmp_addr - 4) =
							operation_code;
						dev->duration_without_int = 0;
					}
				}
			}
		}
	}
	while (1) {
		if (!new_cmdbuf_node)
			break;
		if (!new_cmdbuf_node->next)
			break;
		next_cmdbuf_node = new_cmdbuf_node->next;
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		next_cmdbuf_obj = (struct cmdbuf_obj *)next_cmdbuf_node->data;
		if (cmdbuf_obj->has_end_cmdbuf == 0 &&
		    !next_cmdbuf_obj->cmdbuf_run_done) {
			//need to link, current cmdbuf link to next cmdbuf
			LOG_DBG("Link cmdbuf %d to cmdbuf %d",
			       cmdbuf_obj->cmdbuf_id,
			       next_cmdbuf_obj->cmdbuf_id);
			jmp_addr = cmdbuf_obj->cmdbuf_virtual_address +
				       (cmdbuf_obj->cmdbuf_size / 4);
			if (dev->hw_version_id > HW_ID_1_0_C)

				//set next cmdbuf id
				*(jmp_addr - 1) = next_cmdbuf_obj->cmdbuf_id;

			if (mmu_enable) {
				if (sizeof(size_t) == 8)
					*(jmp_addr - 2) = (u32)((u64)(next_cmdbuf_obj->mmu_cmdbuf_bus_address) >> 32);
				else
					*(jmp_addr - 2) = 0;

				*(jmp_addr - 3) = (u32)(next_cmdbuf_obj->mmu_cmdbuf_bus_address);
			} else {
				if (sizeof(size_t) == 8)
					*(jmp_addr - 2) = (u32)(
						(u64)(next_cmdbuf_obj->cmdbuf_bus_address -
						      base_ddr_addr) >> 32);
				else
					*(jmp_addr - 2) = 0;

				*(jmp_addr - 3) = (u32)(next_cmdbuf_obj->cmdbuf_bus_address - base_ddr_addr);
			}
			operation_code = *(jmp_addr - 4);
			operation_code >>= 16;
			operation_code <<= 16;
			*(jmp_addr - 4) =
				(u32)(operation_code | JMP_RDY_1 |
				      ((next_cmdbuf_obj->cmdbuf_size + 7) / 8));
			next_cmdbuf_obj->cmdbuf_data_linked = 1;
			dev->sw_cmdbuf_rdy_num++;
			//modify nop code of next cmdbuf
			if (next_cmdbuf_obj->has_end_cmdbuf == 0) {
				if (next_cmdbuf_obj->no_normal_int_cmdbuf ==
				    1) {
					dev->duration_without_int +=
						next_cmdbuf_obj->executing_time;
					//maybe we see the modified nop before abort, so need to write back.
					if (dev->duration_without_int >=
					    INT_MIN_SUM_OF_IMAGE_SIZE) {
						jmp_addr =
							next_cmdbuf_obj->cmdbuf_virtual_address +
							(next_cmdbuf_obj->cmdbuf_size /  4);
						operation_code =
							*(jmp_addr - 4);
						operation_code = JMP_IE_1 |
								 operation_code;
						*(jmp_addr - 4) =
							operation_code;
						dev->duration_without_int = 0;
					}
				}
			} else
				dev->duration_without_int = 0;
#ifdef VCMD_DEBUG_INTERNAL
			{
				u32 i;

				LOG_INFO("vcmd link, last cmdbuf content\n");
				for (i = cmdbuf_obj->cmdbuf_size / 4 - 8;
				     i < cmdbuf_obj->cmdbuf_size / 4; i++) {
					LOG_INFO("current linked cmdbuf data %d =0x%x\n",
						i,
					       *(cmdbuf_obj->cmdbuf_virtual_address +
					       i));
				}
			}
#endif
		}
		new_cmdbuf_node = new_cmdbuf_node->next;
	}
	return;
}

/* delink all the cmd buffers from the cmdbuf in front oflast_linked_cmdbuf_node
 * to head of the list. All the cmd bufs marked as X will be delinked.
 * E.g.,
 * X->X->...->X->L->L->        ...        ->L
 * ^             ^                          ^
 * head          last_linked_cmdbuf_node    tail
 */
static void vcmd_delink_cmdbuf(struct hantrovcmd_dev *dev,
			       bi_list_node *last_linked_cmdbuf_node)
{
	bi_list_node *new_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	new_cmdbuf_node = last_linked_cmdbuf_node;
	while (1) {
		if (!new_cmdbuf_node)
			break;
		cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
		if (cmdbuf_obj->cmdbuf_data_linked)
			cmdbuf_obj->cmdbuf_data_linked = 0;
		else
			break;
		new_cmdbuf_node = new_cmdbuf_node->next;
	}
	dev->sw_cmdbuf_rdy_num = 0;
}

static void ConfigAIIXFE_MMU_BYVCMD(struct hantrovcmd_dev **device)
{
#ifdef HANTROVCMD_ENABLE_IP_SUPPORT
	u32 i = 0;
	u64 address = 0, address_ext = 0;
	u32 mirror_index, register_index, register_value;
	u32 write_command = 0;
	u16 mmu_offset_address = 0;
	struct hantrovcmd_dev *dev;

	if (!device)
		return;

	dev = *device;
	mirror_index = VCMD_REGISTER_INDEX_SW_INIT_CMD0;
	write_command = OPCODE_WREG | (1 << 26) | (1 << 16);
	//enable AXIFE by VCMD
	if (dev->vcmd_core_cfg.submodule_axife_addr != 0xffff) {
		register_index = AXI_REG10_SW_FRONTEND_EN;
		register_value = 0x02;
		dev->reg_mirror[mirror_index++] =
			write_command |
			(dev->vcmd_core_cfg.submodule_axife_addr +
			 register_index);
		dev->reg_mirror[mirror_index++] = register_value;

		register_index = AXI_REG11_SW_WORK_MODE;
		register_value = 0x00;
		dev->reg_mirror[mirror_index++] =
			write_command |
			(dev->vcmd_core_cfg.submodule_axife_addr +
			 register_index);
		dev->reg_mirror[mirror_index++] = register_value;
	}

	//enable MMU by VCMD
	address = GetMMUAddress();
	if (!pcie)
		address_ext = ((u32)(address >> 32)) & 0xff;
	LOG_INFO("ConfigAIIXFE_MMU_BYVCMD: address = 0x%llx", address);
	for (i = 0; i < 2; i++) {
		mmu_offset_address =
			(i == 0) ? dev->vcmd_core_cfg.submodule_MMU_addr :
				   dev->vcmd_core_cfg.submodule_MMUWrite_addr;
		if (mmu_offset_address != 0xffff) {
			register_index = MMU_REG_ADDRESS;
			register_value = address;
			dev->reg_mirror[mirror_index++] =
				write_command |
				(mmu_offset_address + register_index);
			dev->reg_mirror[mirror_index++] = register_value;

			register_index = MMU_REG_ADDRESS_MSB;
			register_value = address_ext;
			dev->reg_mirror[mirror_index++] =
				write_command |
				(mmu_offset_address + register_index);
			dev->reg_mirror[mirror_index++] = register_value;

			register_index = MMU_REG_PAGE_TABLE_ID;
			register_value = 0x10000;
			dev->reg_mirror[mirror_index++] =
				write_command |
				(mmu_offset_address + register_index);
			dev->reg_mirror[mirror_index++] = register_value;

			register_index = MMU_REG_PAGE_TABLE_ID;
			register_value = 0x00000;
			dev->reg_mirror[mirror_index++] =
				write_command |
				(mmu_offset_address + register_index);
			dev->reg_mirror[mirror_index++] = register_value;

			register_index = MMU_REG_CONTROL;
			register_value = 1;
			dev->reg_mirror[mirror_index++] =
				write_command |
				(mmu_offset_address + register_index);
			dev->reg_mirror[mirror_index++] = register_value;
		}
	}
	//END command
	dev->reg_mirror[mirror_index++] = OPCODE_END;
	dev->reg_mirror[mirror_index] = 0x00;

	for (i = 0; i < mirror_index - VCMD_REGISTER_INDEX_SW_INIT_CMD0; i++) {
		register_index = (i + VCMD_REGISTER_INDEX_SW_INIT_CMD0) * 4;
		vcmd_write_reg(
			(const void *)dev->hwregs, register_index,
			dev->reg_mirror[i + VCMD_REGISTER_INDEX_SW_INIT_CMD0]);
	}
#endif
}

static void vcmd_start(struct hantrovcmd_dev *dev,
		       bi_list_node *first_linked_cmdbuf_node)
{
	struct cmdbuf_obj *cmdbuf_obj = NULL;

	if (dev->working_state == WORKING_STATE_IDLE) {
		if ((first_linked_cmdbuf_node) && dev->sw_cmdbuf_rdy_num) {
			cmdbuf_obj = (struct cmdbuf_obj *)
					     first_linked_cmdbuf_node->data;
			printk_vcmd_register_debug((const void *)dev->hwregs,
						   "vcmd_start enters");
			//0x40
#ifdef HANTROVCMD_ENABLE_IP_SUPPORT
			//when start vcmd, first vcmd is init mode
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_INIT_MODE, 1);
#endif
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_AXI_CLK_GATE_DISABLE,
				0);
			//this bit should be set 1 only
			//when need to reset dec400
			vcmd_set_register_mirror_value(
				dev->reg_mirror,
				HWIF_VCMD_MASTER_OUT_CLK_GATE_DISABLE, 1);
			vcmd_set_register_mirror_value(
				dev->reg_mirror,
				HWIF_VCMD_CORE_CLK_GATE_DISABLE, 0);
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_ABORT_MODE, 0);
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_RESET_CORE, 0);
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_RESET_ALL, 0);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_START_TRIGGER, 0);
			//0x48
			if (dev->hw_version_id <= HW_ID_1_0_C)
				vcmd_set_register_mirror_value(
					dev->reg_mirror,
					HWIF_VCMD_IRQ_INTCMD_EN, 0xffff);
			else {
				vcmd_set_register_mirror_value(
					dev->reg_mirror, HWIF_VCMD_IRQ_JMPP_EN,
					1);
				vcmd_set_register_mirror_value(
					dev->reg_mirror, HWIF_VCMD_IRQ_JMPD_EN,
					1);
			}

			if (dev->hw_version_id < HW_ID_1_1_1)
				vcmd_set_register_mirror_value(
					dev->reg_mirror, HWIF_VCMD_IRQ_RESET_EN,
					1);

			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_IRQ_ABORT_EN, 1);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_IRQ_CMDERR_EN, 1);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_IRQ_TIMEOUT_EN, 1);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_IRQ_BUSERR_EN, 1);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_IRQ_ENDCMD_EN, 1);
			//0x4c
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_TIMEOUT_EN, 1);
			vcmd_set_register_mirror_value(dev->reg_mirror,
						       HWIF_VCMD_TIMEOUT_CYCLES,
						       0x1dcd6500);
			if (mmu_enable) {
				vcmd_set_register_mirror_value(
				  dev->reg_mirror,
				  HWIF_VCMD_EXECUTING_CMD_ADDR,
				  (u32)(cmdbuf_obj->mmu_cmdbuf_bus_address));
				if (sizeof(size_t) == 8)
					vcmd_set_register_mirror_value(
					  dev->reg_mirror,
					  HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
					  (u32)((u64)(
					    cmdbuf_obj->mmu_cmdbuf_bus_address) >>
					    32));
				else
					vcmd_set_register_mirror_value(
					  dev->reg_mirror,
					  HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
					  0);
			} else {
				vcmd_set_register_mirror_value(
				  dev->reg_mirror,
				  HWIF_VCMD_EXECUTING_CMD_ADDR,
				  (u32)(cmdbuf_obj->cmdbuf_bus_address -
					base_ddr_addr));
				if (sizeof(size_t) == 8)
					vcmd_set_register_mirror_value(
					  dev->reg_mirror,
					  HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
					  (u32)((u64)
					    (cmdbuf_obj->cmdbuf_bus_address -
					    base_ddr_addr) >> 32));
				else
					vcmd_set_register_mirror_value(
					  dev->reg_mirror,
					  HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,
					  0);
			}
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_EXE_CMDBUF_LENGTH,
				(u32)((cmdbuf_obj->cmdbuf_size + 7) / 8));
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_RDY_CMDBUF_COUNT,
				dev->sw_cmdbuf_rdy_num);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_MAX_BURST_LEN, 0x10);
			if (dev->hw_version_id > HW_ID_1_0_C)
				vcmd_write_register_value(
					(const void *)dev->hwregs,
					dev->reg_mirror,
					HWIF_VCMD_CMDBUF_EXECUTING_ID,
					(u32)cmdbuf_obj->cmdbuf_id);

			vcmd_write_reg((const void *)dev->hwregs, 0x44,
				       vcmd_read_reg((const void *)dev->hwregs,
						     0x44));
			vcmd_write_reg((const void *)dev->hwregs, 0x40,
				       dev->reg_mirror[0x40 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x48,
				       dev->reg_mirror[0x48 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x4c,
				       dev->reg_mirror[0x4c / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x50,
				       dev->reg_mirror[0x50 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x54,
				       dev->reg_mirror[0x54 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x58,
				       dev->reg_mirror[0x58 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x5c,
				       dev->reg_mirror[0x5c / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x60,
				       dev->reg_mirror[0x60 / 4]);
			vcmd_write_reg((const void *)dev->hwregs, 0x64,
				       0xffffffff); //not interrupt cpu

			dev->working_state = WORKING_STATE_WORKING;

			if (dev->hw_version_id >= HW_ID_1_2_1)
				ConfigAIIXFE_MMU_BYVCMD(&dev);

			//start
			//this bit should be set 1 only
			//when need to reset dec400
			vcmd_set_register_mirror_value(
				dev->reg_mirror,
				HWIF_VCMD_MASTER_OUT_CLK_GATE_DISABLE, 0);
			vcmd_set_register_mirror_value(
				dev->reg_mirror, HWIF_VCMD_START_TRIGGER, 1);
			vcmd_write_reg((const void *)dev->hwregs, 0x40,
				       dev->reg_mirror[0x40 / 4]);

			printk_vcmd_register_debug((const void *)dev->hwregs,
						   "vcmd_start exits ");
		}
	}
}

static void
create_read_all_registers_cmdbuf(struct exchange_parameter *input_para)
{
	u32 register_range[] = { VCMD_ENCODER_REGISTER_SIZE,
				 VCMD_IM_REGISTER_SIZE,
				 VCMD_DECODER_REGISTER_SIZE,
				 VCMD_JPEG_ENCODER_REGISTER_SIZE,
				 VCMD_JPEG_DECODER_REGISTER_SIZE };
	u32 counter_cmdbuf_size = 0;
	u32 *set_base_addr = vcmd_buf_mem_pool.virtual_address +
			     input_para->cmdbuf_id * CMDBUF_MAX_SIZE / 4;
	//u32 *status_base_virt_addr=vcmd_status_buf_mem_pool.virtual_address +
	//input_para->cmdbuf_id*CMDBUF_MAX_SIZE/4+
	//(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr/2/4+0);
	addr_t status_base_phy_addr =
		vcmd_status_buf_mem_pool.bus_address +
		input_para->cmdbuf_id * CMDBUF_MAX_SIZE +
		(vcmd_manager[input_para->module_type][input_para->core_id]
				 ->vcmd_core_cfg.submodule_main_addr /
			 2 +
		 0);
	u32 map_status_base_phy_addr =
		vcmd_status_buf_mem_pool.mmu_bus_address +
		input_para->cmdbuf_id * CMDBUF_MAX_SIZE +
		(vcmd_manager[input_para->module_type][input_para->core_id]
				 ->vcmd_core_cfg.submodule_main_addr /
			 2 +
		 0);
	u32 offset_inc = 0;

	if (vcmd_manager[input_para->module_type][input_para->core_id]->hw_version_id >
	    HW_ID_1_0_C) {
		LOG_DBG("driver:create cmdbuf data when hw_version_id = 0x%x\n",
			vcmd_manager[input_para->module_type][input_para->core_id]->hw_version_id);

		//read vcmd executing cmdbuf id registers
		//to ddr for balancing core load.
		*(set_base_addr + 0) = (OPCODE_RREG) | (1 << 16) |
				       (EXECUTING_CMDBUF_ID_ADDR * 4);
		counter_cmdbuf_size += 4;
		*(set_base_addr + 1) = (u32)0; //will be changed in link stage
		counter_cmdbuf_size += 4;
		*(set_base_addr + 2) = (u32)0; //will be changed in link stage
		counter_cmdbuf_size += 4;
		//alignment
		*(set_base_addr + 3) = 0;
		counter_cmdbuf_size += 4;

		//read main IP all registers
		*(set_base_addr + 4) =
			(OPCODE_RREG) |
			((register_range[input_para->module_type] / 4) << 16) |
			(vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_main_addr +  0);
		counter_cmdbuf_size += 4;
		if (mmu_enable)
			*(set_base_addr + 5) = map_status_base_phy_addr;
		else
			*(set_base_addr + 5) =
				(u32)(status_base_phy_addr - base_ddr_addr);

		counter_cmdbuf_size += 4;
		if (mmu_enable)
			*(set_base_addr + 6) = 0;
		else {
			if (sizeof(size_t) == 8)
				*(set_base_addr + 6) = (u32)((u64)(status_base_phy_addr - base_ddr_addr) >> 32);
			else
				*(set_base_addr + 6) = 0;
		}
		counter_cmdbuf_size += 4;
		//alignment
		*(set_base_addr + 7) = 0;
		counter_cmdbuf_size += 4;
		if (vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_L2Cache_addr != 0xffff) {
			offset_inc = 4;
			status_base_phy_addr =
				vcmd_status_buf_mem_pool.bus_address +
				input_para->cmdbuf_id * CMDBUF_MAX_SIZE +
				(vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_L2Cache_addr / 2 + 0);
			map_status_base_phy_addr =
				vcmd_status_buf_mem_pool.mmu_bus_address +
				input_para->cmdbuf_id * CMDBUF_MAX_SIZE +
				(vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_L2Cache_addr / 2 + 0);
			//read L2cache IP first register
			*(set_base_addr + 8) = (OPCODE_RREG) | (1 << 16) |
			       (vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_L2Cache_addr + 0);

			counter_cmdbuf_size += 4;
			if (mmu_enable)
				*(set_base_addr + 9) = map_status_base_phy_addr;
			else
				*(set_base_addr + 9) = (u32)(status_base_phy_addr - base_ddr_addr);

			counter_cmdbuf_size += 4;
			if (mmu_enable)
				*(set_base_addr + 10) = 0;
			else {
				if (sizeof(size_t) == 8)
					*(set_base_addr + 10) = (u32)((u64)(status_base_phy_addr - base_ddr_addr) >> 32);
				else
					*(set_base_addr + 10) = 0;
			}
			counter_cmdbuf_size += 4;
			//alignment
			*(set_base_addr + 11) = 0;
			counter_cmdbuf_size += 4;
		}

#if 0
			//INT code, interrupt immediately
			*(set_base_addr + 4) = (OPCODE_INT) |
			0 | input_para->cmdbuf_id;
			counter_cmdbuf_size += 4;
			//alignment
			*(set_base_addr + 5) = 0;
			counter_cmdbuf_size += 4;
#endif
		//read vcmd registers to ddr
		*(set_base_addr + 8 + offset_inc) =
			(OPCODE_RREG) | (27 << 16) | (0);
		counter_cmdbuf_size += 4;
		*(set_base_addr + 9 + offset_inc) =
			(u32)0; //will be changed in link stage
		counter_cmdbuf_size += 4;
		*(set_base_addr + 10 + offset_inc) =
			(u32)0; //will be changed in link stage
		counter_cmdbuf_size += 4;
		//alignment
		*(set_base_addr + 11 + offset_inc) = 0;
		counter_cmdbuf_size += 4;
		//JMP RDY = 0
		*(set_base_addr + 12 + offset_inc) =
			(OPCODE_JMP_RDY0) | 0 | JMP_IE_1 | 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 13 + offset_inc) = 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 14 + offset_inc) = 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 15 + offset_inc) = input_para->cmdbuf_id;
		//don't add the last alignment DWORD in order
		//to  identify END command or JMP command.
		//counter_cmdbuf_size += 4;
		input_para->cmdbuf_size = (16 + offset_inc) * 4;
	} else {
		LOG_DBG("driver:create cmdbuf data when hw_version_id = 0x%x\n",
			vcmd_manager[input_para->module_type][input_para->core_id]->hw_version_id);
		//read all registers
		*(set_base_addr + 0) =
			(OPCODE_RREG) |
			((register_range[input_para->module_type] / 4) << 16) |
			(vcmd_manager[input_para->module_type][input_para->core_id]->vcmd_core_cfg.submodule_main_addr + 0);
		counter_cmdbuf_size += 4;
		if (mmu_enable)
			*(set_base_addr + 1) = map_status_base_phy_addr;
		else
			*(set_base_addr + 1) =
				(u32)(status_base_phy_addr - base_ddr_addr);

		counter_cmdbuf_size += 4;
		if (mmu_enable) {
			*(set_base_addr + 2) = 0;
		} else {
			if (sizeof(size_t) == 8)
				*(set_base_addr + 2) =
					(u32)((u64)(status_base_phy_addr -
						    base_ddr_addr) >> 32);
			else
				*(set_base_addr + 2) = 0;
		}
		counter_cmdbuf_size += 4;
		//alignment
		*(set_base_addr + 3) = 0;
		counter_cmdbuf_size += 4;
#if 0
			//INT code, interrupt immediately
			*(set_base_addr + 4) = (OPCODE_INT) |
			0 | input_para->cmdbuf_id;
			counter_cmdbuf_size += 4;
			//alignment
			*(set_base_addr + 5) = 0;
			counter_cmdbuf_size += 4;
#endif
		//JMP RDY = 0
		*(set_base_addr + 4) = (OPCODE_JMP_RDY0) | 0 | JMP_IE_1 | 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 5) = 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 6) = 0;
		counter_cmdbuf_size += 4;
		*(set_base_addr + 7) = input_para->cmdbuf_id;
		//don't add the last alignment DWORD in order to
		//identify END command or JMP command.
		//counter_cmdbuf_size += 4;
		input_para->cmdbuf_size = 8 * 4;
	}
}

static void read_main_module_all_registers(u32 main_module_type)
{
	int ret, i;
	struct exchange_parameter input_para;
	u32 irq_status_ret = 0;
	u32 *status_base_virt_addr;

	for (i = 0; i < vcmd_type_core_num[main_module_type]; i++) {
		input_para.executing_time = 0;
		input_para.priority = CMDBUF_PRIORITY_NORMAL;
		input_para.module_type = main_module_type;
		input_para.cmdbuf_size = 0;
		input_para.core_id = i;
		ret = reserve_cmdbuf(NULL, &input_para);
		vcmd_manager[main_module_type][i]->status_cmdbuf_id =
			input_para.cmdbuf_id;
		create_read_all_registers_cmdbuf(&input_para);

		link_and_run_cmdbuf(NULL, &input_para);
		/*
		   hantrovcmd_isr(input_para.core_id,
		   &hantrovcmd_data[input_para.core_id]);*/
		wait_cmdbuf_ready(NULL, input_para.cmdbuf_id, &irq_status_ret);
		status_base_virt_addr =
			vcmd_status_buf_mem_pool.virtual_address +
			input_para.cmdbuf_id * CMDBUF_MAX_SIZE / 4 +
			(vcmd_manager[input_para.module_type][i]->vcmd_core_cfg.submodule_main_addr / 2 / 4 + 0);
		LOG_DBG("driver: main module register 0:0x%x\n",
				*status_base_virt_addr);
		LOG_DBG("driver: main module register 50:0x%08x\n",
				*(status_base_virt_addr + 50));
		LOG_DBG("driver: main module register 54:0x%08x\n",
				*(status_base_virt_addr + 54));
		LOG_DBG("driver: main module register 56:0x%08x\n",
				*(status_base_virt_addr + 56));
		LOG_DBG("driver: main module register 309:0x%x\n",
				*(status_base_virt_addr + 309));
		LOG_DBG("driver: status_cmdbuf_id:%u\n", input_para.cmdbuf_id);
		//don't release cmdbuf because it can be used repeatedly
		//release_cmdbuf(input_para.cmdbuf_id);
	}
}

int hantrovcmd_init(void)
{
	int i, k;
	int result;
	struct kernel_addr_desc addr;
/*	struct device_node *np;


	np = of_find_compatible_node(NULL, NULL, "eswin,video-decoder0");
	if (!np) {
		pr_err("Unable to find video decoder device");
		goto err;
	}

	irq_num = of_property_count_u32_elems(np, "interrupts");
	if (irq_num <= 0) {
		pr_err("Failed to get irq number");
		goto err;
	}*/

	//total_vcmd_core_num =
	//sizeof(vcmd_core_array)/sizeof(struct vcmd_config);
	result = vcmd_init();
	if (result) {
		LOG_ERR("vcmd init failed, result: %d\n", result);
		goto err;
	}

	for (i = 0; i < total_vcmd_core_num; i++) {
		LOG_INFO("module init - vcore[%d] addr =0x%llx\n",
			i,
		       (unsigned long long)vcmd_core_array[i].vcmd_base_addr);
	}
	hantrovcmd_data = vmalloc(sizeof(*hantrovcmd_data) * total_vcmd_core_num);
	if (!hantrovcmd_data) {
		LOG_ERR("malloc vcmd dataf failed, total_vcmd_core_num=%u\n", total_vcmd_core_num);
		result = ENOMEM;
		goto err;
	}
	memset(hantrovcmd_data, 0,
	       sizeof(struct hantrovcmd_dev) * total_vcmd_core_num);
	for (k = 0; k < MAX_VCMD_TYPE; k++) {
		vcmd_type_core_num[k] = 0;
		vcmd_position[k] = 0;
		for (i = 0; i < MAX_VCMD_NUMBER; i++)
			vcmd_manager[k][i] = NULL;
	}

	init_bi_list(&global_process_manager);

	for (i = 0; i < total_vcmd_core_num; i++) {
		hantrovcmd_data[i].vcmd_core_cfg = vcmd_core_array[i];
		hantrovcmd_data[i].hwregs = NULL;
		hantrovcmd_data[i].core_id = i;
		hantrovcmd_data[i].working_state = WORKING_STATE_IDLE;
		hantrovcmd_data[i].sw_cmdbuf_rdy_num = 0;
		hantrovcmd_data[i].spinlock = &owner_lock_vcmd[i];
		spin_lock_init(&owner_lock_vcmd[i]);
		hantrovcmd_data[i].wait_queue = &wait_queue_vcmd[i];
		init_waitqueue_head(&wait_queue_vcmd[i]);
		hantrovcmd_data[i].wait_abort_queue = &abort_queue_vcmd[i];
		init_waitqueue_head(&abort_queue_vcmd[i]);
		init_bi_list(&hantrovcmd_data[i].list_manager);
		hantrovcmd_data[i].duration_without_int = 0;
		vcmd_manager[vcmd_core_array[i].sub_module_type][vcmd_type_core_num[vcmd_core_array[i].sub_module_type]] =
				    &hantrovcmd_data[i];
		LOG_DBG("---------- vmcd_init %d ----vcmd sub module type %d ", __LINE__, vcmd_core_array[i].sub_module_type);
		vcmd_type_core_num[vcmd_core_array[i].sub_module_type]++;
		LOG_DBG("---------- vmcd_init %d ----vcmd type core num %d ", __LINE__, vcmd_type_core_num[vcmd_core_array[i].sub_module_type]);
		hantrovcmd_data[i].vcmd_reg_mem_bus_address =
			vcmd_registers_mem_pool.bus_address +
			i * VCMD_REGISTER_SIZE - base_ddr_addr;
		hantrovcmd_data[i].mmu_vcmd_reg_mem_bus_address =
			vcmd_registers_mem_pool.mmu_bus_address +
			i * VCMD_REGISTER_SIZE;
		hantrovcmd_data[i].vcmd_reg_mem_virtual_address =
			vcmd_registers_mem_pool.virtual_address +
			i * VCMD_REGISTER_SIZE / 4;
		hantrovcmd_data[i].vcmd_reg_mem_size = VCMD_REGISTER_SIZE;
		memset(hantrovcmd_data[i].vcmd_reg_mem_virtual_address, 0,
		       VCMD_REGISTER_SIZE);
	}
	init_waitqueue_head(&mc_wait_queue);

#if 0
	result = register_chrdev(hantrovcmd_major,
				 DEC_DEV_NAME, &hantrovcmd_fops);
	if (result < 0) {
		LOG_INFO("driver: unable to get major <%d>\n",
			hantrovcmd_major);
		goto err1;
	} else if (result != 0) /* this is for dynamic major */
		hantrovcmd_major = result;
#endif
	result = vcmd_reserve_IO();
	if (result < 0)
		goto err;

	vcmd_reset_asic(hantrovcmd_data);

	/* get the IRQ line */
	for (i = 0; i < total_vcmd_core_num; i++) {
		if (!hantrovcmd_data[i].hwregs)
			continue;

//		hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq = irq_of_parse_and_map(np, i);
		LOG_INFO("request mapped irq %d\n", hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq);
		if (hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq != -1) {
			result = request_irq(
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq,
				hantrovcmd_isr,
#if (KERNEL_VERSION(2, 6, 18) > LINUX_VERSION_CODE)
				SA_INTERRUPT | SA_SHIRQ,
#else
				IRQF_SHARED,
#endif
				DEC_VCMD_DRV_NAME,
				(void *)&hantrovcmd_data[i]);
			if (result == -EINVAL) {
				LOG_ERR("Bad vcmd_irq number or handler. core_id=%d\n",
				       i);
				vcmd_release_IO();
				goto err;
			} else if (result == -EBUSY) {
				LOG_ERR("IRQ <%d> busy, change your config. core_id=%d\n",
				       hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq,
				       i);
				vcmd_release_IO();
				goto err;
			} else {
				LOG_INFO("request IRQ <%d> successfully for subsystem %d\n",
					hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq,
					i);
			}
		} else {
			LOG_INFO("IRQ not in use!\n");
		}
	}
	//cmdbuf pool allocation
	//init_vcmd_non_cachable_memory_allocate();
	//for cmdbuf management
	cmdbuf_used_pos = 0;
	for (k = 0; k < TOTAL_DISCRETE_CMDBUF_NUM; k++) {
		cmdbuf_used[k] = 0;
		global_cmdbuf_node[k] = NULL;
	}
	//cmdbuf_used[0] not be used, because int vector must non-zero
	cmdbuf_used_residual = TOTAL_DISCRETE_CMDBUF_NUM;
	cmdbuf_used_pos = 1;
	cmdbuf_used[0] = 1;
	cmdbuf_used_residual -= 1;

	for (i = 0; i < CMDBUF_LOCK_NUM; i++) {
		spin_lock_init(&cmdbuf_lock[i]);
	}

	create_kernel_process_manager();
	for (i = 0; i < MAX_VCMD_TYPE; i++) {
		if (vcmd_type_core_num[i] == 0)
			continue;
		sema_init(&vcmd_reserve_cmdbuf_sem[i], 1);
	}
#ifdef IRQ_SIMULATION
	for (i = 0; i < 10000; i++)
		timer_reserve[i].timer = NULL;
#endif
	/* read all registers for each type of module
	 * for analyzing configuration in cwl
	 */
	for (i = 0; i < MAX_VCMD_TYPE; i++) {
		if (vcmd_type_core_num[i] == 0)
			continue;
		read_main_module_all_registers(i);
	}

	return 0;
err:
	if (vcmd_buf_mem_pool.virtual_address) {
		if (mmu_enable) {
			addr.bus_address = vcmd_buf_mem_pool.bus_address;
			addr.size = vcmd_buf_mem_pool.size;
			MMUKernelMemNodeUnmap(&addr);
		}
		iounmap((void __iomem *)vcmd_buf_mem_pool.virtual_address);
		release_mem_region(vcmd_buf_mem_pool.bus_address,
				   vcmd_buf_mem_pool.size);
	}
	if (vcmd_status_buf_mem_pool.virtual_address) {
		if (mmu_enable) {
			addr.bus_address = vcmd_status_buf_mem_pool.bus_address;
			addr.size = vcmd_status_buf_mem_pool.size;
			MMUKernelMemNodeUnmap(&addr);
		}
		iounmap((void __iomem *)
				vcmd_status_buf_mem_pool.virtual_address);
		release_mem_region(vcmd_status_buf_mem_pool.bus_address,
				   vcmd_status_buf_mem_pool.size);
	}
	if (vcmd_registers_mem_pool.virtual_address) {
		if (mmu_enable) {
			addr.bus_address = vcmd_registers_mem_pool.bus_address;
			addr.size = vcmd_registers_mem_pool.size;
			MMUKernelMemNodeUnmap(&addr);
		}
		iounmap((void __iomem *)
		vcmd_registers_mem_pool.virtual_address);
		release_mem_region(vcmd_registers_mem_pool.bus_address,
				   vcmd_registers_mem_pool.size);
	}
	if (hantrovcmd_data)
		vfree(hantrovcmd_data);

	LOG_WARN("driver: module not inserted\n");
	return result;
}

void hantrovcmd_cleanup(void)
{
	int i = 0;
	u32 result;

	for (i = 0; i < total_vcmd_core_num; i++) {
		if (!hantrovcmd_data[i].hwregs)
			continue;
		//disable interrupt at first
		vcmd_write_reg((const void *)hantrovcmd_data[i].hwregs,
			       VCMD_REGISTER_INT_CTL_OFFSET, 0x0000);
		//disable HW
		vcmd_write_reg((const void *)hantrovcmd_data[i].hwregs,
			       VCMD_REGISTER_CONTROL_OFFSET, 0x0000);
		//read status register
		result = vcmd_read_reg((const void *)hantrovcmd_data[i].hwregs,
				       VCMD_REGISTER_INT_STATUS_OFFSET);
		//clean status register
		vcmd_write_reg((const void *)hantrovcmd_data[i].hwregs,
			       VCMD_REGISTER_INT_STATUS_OFFSET, result);

		/* free the vcmd IRQ */
		if (hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq != -1)
			free_irq(hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq,
				 (void *)&hantrovcmd_data[i]);
		release_cmdbuf_node_cleanup(&hantrovcmd_data[i].list_manager);
	}

	release_process_node_cleanup(&global_process_manager);

	vcmd_release_IO();
	vfree(hantrovcmd_data);

	//release_vcmd_non_cachable_memory();
	if (pcie) {
		iounmap((void __iomem *)vcmd_buf_mem_pool.virtual_address);
		release_mem_region(vcmd_buf_mem_pool.bus_address,
				   vcmd_buf_mem_pool.size);
		iounmap((void __iomem *)
				vcmd_status_buf_mem_pool.virtual_address);
		release_mem_region(vcmd_status_buf_mem_pool.bus_address,
				   vcmd_status_buf_mem_pool.size);
		iounmap((void __iomem *)
		vcmd_registers_mem_pool.virtual_address);
		release_mem_region(vcmd_registers_mem_pool.bus_address,
				   vcmd_registers_mem_pool.size);
	} else {
		if (vcmd_buf_mem_pool.virtual_address)
			dma_free_coherent(
				&platformdev->dev, vcmd_buf_mem_pool.size,
				vcmd_buf_mem_pool.virtual_address,
				(dma_addr_t)vcmd_buf_mem_pool.bus_address);
		if (vcmd_status_buf_mem_pool.virtual_address)
			dma_free_coherent(
				&platformdev->dev,
				vcmd_status_buf_mem_pool.size,
				vcmd_status_buf_mem_pool.virtual_address,
				(dma_addr_t)
					vcmd_status_buf_mem_pool.bus_address);
		if (vcmd_registers_mem_pool.virtual_address)
			dma_free_coherent(
				&platformdev->dev, vcmd_registers_mem_pool.size,
				vcmd_registers_mem_pool.virtual_address,
				(dma_addr_t)
					vcmd_registers_mem_pool.bus_address);

		if (platformdev_d1) {
			dma_unmap_page(&platformdev_d1->dev,
					(dma_addr_t)vcmd_buf_mem_pool.bus_address, vcmd_buf_mem_pool.size, DMA_BIDIRECTIONAL);
			dma_unmap_page(&platformdev_d1->dev,
					(dma_addr_t)vcmd_status_buf_mem_pool.bus_address, vcmd_status_buf_mem_pool.size, DMA_BIDIRECTIONAL);
			dma_unmap_page(&platformdev_d1->dev,
					(dma_addr_t)vcmd_registers_mem_pool.bus_address, vcmd_registers_mem_pool.size, DMA_BIDIRECTIONAL);
		}
	}
	LOG_INFO("module removed\n");
	return;
}

static int vcmd_reserve_IO(void)
{
	u32 hwid;
	int i;
	u32 found_hw = 0;

	for (i = 0; i < total_vcmd_core_num; i++) {
		hantrovcmd_data[i].hwregs = NULL;

		if (!request_mem_region(
			    hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
			    hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize,
			    DEC_VCMD_DRV_NAME)) {
			LOG_INFO("failed to reserve HW regs for vcmd %d\n",
				i);
			LOG_INFO("vcmd_base_addr = 0x%08lx, iosize = %d\n",
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
			       hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
			continue;
		}
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
		hantrovcmd_data[i].hwregs =
			(volatile __force u8 *)ioremap_nocache(
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
#else
		hantrovcmd_data[i].hwregs = (volatile __force u8 *)
			ioremap(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
#endif

		if (!hantrovcmd_data[i].hwregs) {
			LOG_INFO("failed to ioremap HW regs\n");
			release_mem_region(
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
			continue;
		}

		/*read hwid and check validness and store it*/
		hwid = (u32)ioread32((void __iomem *)hantrovcmd_data[i].hwregs);
		LOG_DBG("hwid=0x%08x\n", hwid);
		hantrovcmd_data[i].hw_version_id = hwid;

		/* check for vcmd HW ID */
		if (((hwid >> 16) & 0xFFFF) != VCMD_HW_ID) {
			LOG_INFO("HW not found at 0x%llx\n",
				(unsigned long long)
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr);
			iounmap((void __iomem *)hantrovcmd_data[i].hwregs);
			release_mem_region(
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
			hantrovcmd_data[i].hwregs = NULL;
			continue;
		}
		found_hw = 1;
		LOG_DBG("HW at base <0x%llx> with ID <0x%08x>\n",
			(unsigned long long)hantrovcmd_data[i]
			       .vcmd_core_cfg.vcmd_base_addr,
		       hwid);
	}

	if (found_hw == 0) {
		LOG_ERR("NO ANY HW found!!\n");
		return -1;
	}

	return 0;
}

static void vcmd_release_IO(void)
{
	u32 i;

	for (i = 0; i < total_vcmd_core_num; i++) {
		if (hantrovcmd_data[i].hwregs) {
			iounmap((void __iomem *)hantrovcmd_data[i].hwregs);
			release_mem_region(
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
				hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
			hantrovcmd_data[i].hwregs = NULL;
		}
	}
}

#if (KERNEL_VERSION(2, 6, 18) > LINUX_VERSION_CODE)
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id)
#endif
{
	unsigned int handled = 0;
	struct hantrovcmd_dev *dev = (struct hantrovcmd_dev *)dev_id;
	u32 irq_status = 0;
	unsigned long flags;
	bi_list_node *new_cmdbuf_node = NULL;
	bi_list_node *base_cmdbuf_node = NULL;
	struct cmdbuf_obj *cmdbuf_obj = NULL;
	size_t exe_cmdbuf_bus_address;
	u32 cmdbuf_processed_num = 0;
	u32 cmdbuf_id = 0;

	/*If core is not reserved by any user,
	 * but irq is received, just clean it
	 */
	spin_lock_irqsave(dev->spinlock, flags);
	if (!dev->list_manager.head) {
		LOG_DBG("vcmd_isr:received IRQ but \
		       core has nothing to do.\n");
		irq_status = vcmd_read_reg((const void *)dev->hwregs,
					   VCMD_REGISTER_INT_STATUS_OFFSET);
		vcmd_write_reg((const void *)dev->hwregs,
			       VCMD_REGISTER_INT_STATUS_OFFSET, irq_status);
		spin_unlock_irqrestore(dev->spinlock, flags);
		return IRQ_HANDLED;
	}

	LOG_DBG("vcmd_isr: received IRQ!\n");
	irq_status = vcmd_read_reg((const void *)dev->hwregs,
				   VCMD_REGISTER_INT_STATUS_OFFSET);
	LOG_TRACE("Vdec vcmd_isr: received IRQ! %d\n", irq_status);
#ifdef VCMD_DEBUG_INTERNAL
	{
		u32 i, fordebug;

		for (i = 0; i < ASIC_VCMD_SWREG_AMOUNT; i++) {
			fordebug =
				vcmd_read_reg((const void *)dev->hwregs, i * 4);
			LOG_INFO("vcmd register %d:0x%x\n", i,
				fordebug);
		}
	}
#endif

	if (!irq_status) {
		//pr_info("hantrovcmd_isr error,
		//irq_status :0x%x",irq_status);
		spin_unlock_irqrestore(dev->spinlock, flags);
		return IRQ_HANDLED;
	}

	LOG_DBG("irq_status of %d is: 0x%x\n", dev->core_id, irq_status);
	vcmd_write_reg((const void *)dev->hwregs,
		       VCMD_REGISTER_INT_STATUS_OFFSET, irq_status);
	dev->reg_mirror[VCMD_REGISTER_INT_STATUS_OFFSET / 4] = irq_status;

	if ((dev->hw_version_id > HW_ID_1_0_C) && (irq_status & 0x3f)) {
		//if error,read from register directly.
		cmdbuf_id =
			vcmd_get_register_value((const void *)dev->hwregs,
						dev->reg_mirror,
						HWIF_VCMD_CMDBUF_EXECUTING_ID);
		if (cmdbuf_id >= TOTAL_DISCRETE_CMDBUF_NUM) {
			LOG_ERR("isr error cmdbuf_id greater than the ceiling !! %d\n", __LINE__);
			spin_unlock_irqrestore(dev->spinlock, flags);
			return IRQ_HANDLED;
		}
	} else if (dev->hw_version_id > HW_ID_1_0_C) {
		//read cmdbuf id from ddr
#ifdef VCMD_DEBUG_INTERNAL
		{
			u32 i, fordebug;

			LOG_INFO("ddr vcmd register phy_addr=0x%x\n",
				dev->vcmd_reg_mem_bus_address);
			LOG_INFO("ddr vcmd register virt_addr=0x%x\n",
				dev->vcmd_reg_mem_virtual_address);
			for (i = 0; i < ASIC_VCMD_SWREG_AMOUNT; i++) {
				fordebug = *(dev->vcmd_reg_mem_virtual_address +
					     i);
				LOG_INFO("ddr vcmd register %d:0x%x\n",
					i, fordebug);
			}
		}
#endif

		cmdbuf_id = *(dev->vcmd_reg_mem_virtual_address +
			      EXECUTING_CMDBUF_ID_ADDR);
		if (cmdbuf_id >= TOTAL_DISCRETE_CMDBUF_NUM) {
			LOG_ERR("isr error cmdbuf_id greater than the ceiling !! %d\n", __LINE__);
			spin_unlock_irqrestore(dev->spinlock, flags);
			return IRQ_HANDLED;
		}
	}

	if (dev->hw_version_id < HW_ID_1_1_1) {
		if (vcmd_get_register_mirror_value(dev->reg_mirror,
						   HWIF_VCMD_IRQ_RESET)) {
			//reset error,all cmdbuf that is
			//not done will be run again.
			new_cmdbuf_node = dev->list_manager.head;
			dev->working_state = WORKING_STATE_IDLE;
			//find the first run_done=0
			while (1) {
				if (!new_cmdbuf_node)
					break;
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
			base_cmdbuf_node = new_cmdbuf_node;
			vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
			vcmd_link_cmdbuf(dev, base_cmdbuf_node);
			if (dev->sw_cmdbuf_rdy_num != 0)
				//restart new command
				vcmd_start(dev, base_cmdbuf_node);
			handled++;
			spin_unlock_irqrestore(dev->spinlock, flags);
			return IRQ_HANDLED;
		}
	}

	if (vcmd_get_register_mirror_value(dev->reg_mirror,
					   HWIF_VCMD_IRQ_ABORT)) {
		//abort error,don't need to reset
		new_cmdbuf_node = dev->list_manager.head;
		dev->working_state = WORKING_STATE_IDLE;
		if (dev->hw_version_id > HW_ID_1_0_C) {
			new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!new_cmdbuf_node) {
				LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		} else {
			exe_cmdbuf_bus_address = VCMDGetAddrRegisterValue(
				(const void *)dev->hwregs, dev->reg_mirror,
				HWIF_VCMD_EXECUTING_CMD_ADDR);
			//find the cmdbuf that triggers ABORT
			while (1) {
				if (!new_cmdbuf_node) {
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return IRQ_HANDLED;
				}
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((((cmdbuf_obj->cmdbuf_bus_address -
				       base_ddr_addr) <=
				      exe_cmdbuf_bus_address) &&
				     (((cmdbuf_obj->cmdbuf_bus_address -
					base_ddr_addr +
					cmdbuf_obj->cmdbuf_size) >
				    exe_cmdbuf_bus_address))) &&
				    (cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
		}
		base_cmdbuf_node = new_cmdbuf_node;
		// this cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if ((cmdbuf_obj->cmdbuf_run_done == 0)) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else
				break;
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		base_cmdbuf_node = base_cmdbuf_node->next;
		vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
		spin_unlock_irqrestore(dev->spinlock, flags);
		if (cmdbuf_processed_num)
			wake_up_interruptible_all(dev->wait_queue);
		//to let high priority cmdbuf be inserted
		wake_up_interruptible_all(dev->wait_abort_queue);
		wake_up_interruptible_all(&mc_wait_queue);
		handled++;
		return IRQ_HANDLED;
	}
	if (vcmd_get_register_mirror_value(dev->reg_mirror,
					   HWIF_VCMD_IRQ_BUSERR)) {
		//bus error, don't need to reset where to record status?
		new_cmdbuf_node = dev->list_manager.head;
		dev->working_state = WORKING_STATE_IDLE;
		if (dev->hw_version_id > HW_ID_1_0_C) {
			new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!new_cmdbuf_node) {
				LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		} else {
			exe_cmdbuf_bus_address = VCMDGetAddrRegisterValue(
				(const void *)dev->hwregs, dev->reg_mirror,
				HWIF_VCMD_EXECUTING_CMD_ADDR);
			//find the buserr cmdbuf
			while (1) {
				if (!new_cmdbuf_node) {
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return IRQ_HANDLED;
				}
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((((cmdbuf_obj->cmdbuf_bus_address -
				       base_ddr_addr) <=
				      exe_cmdbuf_bus_address) &&
				     (((cmdbuf_obj->cmdbuf_bus_address -
					base_ddr_addr +
					cmdbuf_obj->cmdbuf_size) >
				       exe_cmdbuf_bus_address))) &&
				    (cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
		}
		base_cmdbuf_node = new_cmdbuf_node;
		// this cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if ((cmdbuf_obj->cmdbuf_run_done == 0)) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else
				break;
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		new_cmdbuf_node = base_cmdbuf_node;
		if (new_cmdbuf_node) {
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_BUSERR;
		}
		base_cmdbuf_node = base_cmdbuf_node->next;
		vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
		vcmd_link_cmdbuf(dev, base_cmdbuf_node);
		if (dev->sw_cmdbuf_rdy_num != 0) {
			//restart new command
			vcmd_start(dev, base_cmdbuf_node);
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		if (cmdbuf_processed_num)
			wake_up_interruptible_all(dev->wait_queue);
		handled++;
		wake_up_interruptible_all(&mc_wait_queue);
		return IRQ_HANDLED;
	}
	if (vcmd_get_register_mirror_value(dev->reg_mirror,
					   HWIF_VCMD_IRQ_TIMEOUT)) {
		//time out,need to reset
		new_cmdbuf_node = dev->list_manager.head;
		dev->working_state = WORKING_STATE_IDLE;
		if (dev->hw_version_id > HW_ID_1_0_C) {
			new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!new_cmdbuf_node) {
				LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		} else {
			exe_cmdbuf_bus_address = VCMDGetAddrRegisterValue(
				(const void *)dev->hwregs, dev->reg_mirror,
				HWIF_VCMD_EXECUTING_CMD_ADDR);
			/* find the timeout cmdbuf */
			while (1) {
				if (!new_cmdbuf_node) {
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return IRQ_HANDLED;
				}
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((((cmdbuf_obj->cmdbuf_bus_address -
				    base_ddr_addr) <=
				    exe_cmdbuf_bus_address) &&
				    (((cmdbuf_obj->cmdbuf_bus_address -
				    base_ddr_addr +
				    cmdbuf_obj->cmdbuf_size) >
				    exe_cmdbuf_bus_address))) &&
				    (cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
		}
		base_cmdbuf_node = new_cmdbuf_node;
		new_cmdbuf_node = new_cmdbuf_node->previous;
		// this cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if (cmdbuf_obj->cmdbuf_run_done == 0) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else
				break;
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
		vcmd_link_cmdbuf(dev, base_cmdbuf_node);
		if (dev->sw_cmdbuf_rdy_num != 0) {
			//reset
			vcmd_reset_current_asic(dev);
			//restart new command
			vcmd_start(dev, base_cmdbuf_node);
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		if (cmdbuf_processed_num)
			wake_up_interruptible_all(dev->wait_queue);
		handled++;
		wake_up_interruptible_all(&mc_wait_queue);
		return IRQ_HANDLED;
	}
	if (vcmd_get_register_mirror_value(dev->reg_mirror,
					   HWIF_VCMD_IRQ_CMDERR)) {
		//command error,don't need to reset
		new_cmdbuf_node = dev->list_manager.head;
		dev->working_state = WORKING_STATE_IDLE;
		if (dev->hw_version_id > HW_ID_1_0_C) {
			new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!new_cmdbuf_node) {
				LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		} else {
			exe_cmdbuf_bus_address = VCMDGetAddrRegisterValue(
				(const void *)dev->hwregs, dev->reg_mirror,
				HWIF_VCMD_EXECUTING_CMD_ADDR);
			//find the cmderror cmdbuf
			while (1) {
				if (!new_cmdbuf_node) {
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return IRQ_HANDLED;
				}
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((((cmdbuf_obj->cmdbuf_bus_address -
				       base_ddr_addr) <=
				      exe_cmdbuf_bus_address) &&
				     (((cmdbuf_obj->cmdbuf_bus_address -
					base_ddr_addr +
					cmdbuf_obj->cmdbuf_size) >
				       exe_cmdbuf_bus_address))) &&
				    (cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
		}
		base_cmdbuf_node = new_cmdbuf_node;
		// this cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if (cmdbuf_obj->cmdbuf_run_done == 0) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else {
				break;
			}
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		new_cmdbuf_node = base_cmdbuf_node;
		if (new_cmdbuf_node) {
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			cmdbuf_obj->executing_status =
				CMDBUF_EXE_STATUS_CMDERR; //cmderr
		}
		base_cmdbuf_node = base_cmdbuf_node->next;
		vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
		vcmd_link_cmdbuf(dev, base_cmdbuf_node);
		if (dev->sw_cmdbuf_rdy_num != 0) {
			//restart new command
			vcmd_start(dev, base_cmdbuf_node);
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		if (cmdbuf_processed_num)
			wake_up_interruptible_all(dev->wait_queue);
		handled++;
		wake_up_interruptible_all(&mc_wait_queue);
		return IRQ_HANDLED;
	}

	if (vcmd_get_register_mirror_value(dev->reg_mirror,
					   HWIF_VCMD_IRQ_ENDCMD)) {
		//end command interrupt
		new_cmdbuf_node = dev->list_manager.head;
		dev->working_state = WORKING_STATE_IDLE;
		if (dev->hw_version_id > HW_ID_1_0_C) {
			new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
			if (!new_cmdbuf_node) {
				LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		} else {
			//find the end cmdbuf
			while (1) {
				if (!new_cmdbuf_node) {
					spin_unlock_irqrestore(dev->spinlock,
							       flags);
					return IRQ_HANDLED;
				}
				cmdbuf_obj = (struct cmdbuf_obj *)
						     new_cmdbuf_node->data;
				if ((cmdbuf_obj->has_end_cmdbuf == 1) &&
				    (cmdbuf_obj->cmdbuf_run_done == 0))
					break;
				new_cmdbuf_node = new_cmdbuf_node->next;
			}
		}
		base_cmdbuf_node = new_cmdbuf_node;
		// this cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if ((cmdbuf_obj->cmdbuf_run_done == 0)) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else
				break;
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		base_cmdbuf_node = base_cmdbuf_node->next;
		vcmd_delink_cmdbuf(dev, base_cmdbuf_node);
		vcmd_link_cmdbuf(dev, base_cmdbuf_node);
		if (dev->sw_cmdbuf_rdy_num != 0) {
			//restart new command
			vcmd_start(dev, base_cmdbuf_node);
		}
		spin_unlock_irqrestore(dev->spinlock, flags);
		if (cmdbuf_processed_num)
			wake_up_interruptible_all(dev->wait_queue);
		handled++;
		wake_up_interruptible_all(&mc_wait_queue);
		return IRQ_HANDLED;
	}
	if (dev->hw_version_id <= HW_ID_1_0_C)
		cmdbuf_id = vcmd_get_register_mirror_value(dev->reg_mirror,
							   HWIF_VCMD_IRQ_INTCMD);
	if (cmdbuf_id) {
		if (dev->hw_version_id <= HW_ID_1_0_C) {
			if (cmdbuf_id >= TOTAL_DISCRETE_CMDBUF_NUM) {
				LOG_ERR("isr error cmdbuf_id greater than the ceiling !! %d\n", __LINE__);
				spin_unlock_irqrestore(dev->spinlock, flags);
				return IRQ_HANDLED;
			}
		}
		new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
		if (!new_cmdbuf_node) {
			LOG_ERR("ERROR cmdbuf_id line=%d, cmdbuf_id=%u!!\n", __LINE__, cmdbuf_id);
			spin_unlock_irqrestore(dev->spinlock, flags);
			return IRQ_HANDLED;
		}
		// interrupt cmdbuf and cmdbufs prior to itself, run_done = 1
		while (1) {
			if (!new_cmdbuf_node)
				break;
			cmdbuf_obj = (struct cmdbuf_obj *)new_cmdbuf_node->data;
			if ((cmdbuf_obj->cmdbuf_run_done == 0)) {
				cmdbuf_obj->cmdbuf_run_done = 1;
				cmdbuf_obj->executing_status =
					CMDBUF_EXE_STATUS_OK;
				cmdbuf_processed_num++;
			} else {
				break;
			}
			new_cmdbuf_node = new_cmdbuf_node->previous;
		}
		handled++;
	}

	spin_unlock_irqrestore(dev->spinlock, flags);
	if (cmdbuf_processed_num)
		wake_up_interruptible_all(dev->wait_queue);
	if (!handled)
		LOG_DBG("IRQ received, but not hantro's!\n");

	wake_up_interruptible_all(&mc_wait_queue);
	return IRQ_HANDLED;
}

static void vcmd_reset_asic(struct hantrovcmd_dev *dev)
{
	int i, n;
	u32 result;

	for (n = 0; n < total_vcmd_core_num; n++) {
		if (dev[n].hwregs) {
			//disable interrupt at first
			vcmd_write_reg((const void *)dev[n].hwregs,
				       VCMD_REGISTER_INT_CTL_OFFSET, 0x0000);
			//reset core
			vcmd_write_reg((const void *)dev[n].hwregs,
				       VCMD_REGISTER_CONTROL_OFFSET, 0x0004);
			//read status register
			result = vcmd_read_reg((const void *)dev[n].hwregs,
					       VCMD_REGISTER_INT_STATUS_OFFSET);
			//clean status register
			vcmd_write_reg((const void *)dev[n].hwregs,
				       VCMD_REGISTER_INT_STATUS_OFFSET, result);
			for (i = VCMD_REGISTER_CONTROL_OFFSET;
			     i < dev[n].vcmd_core_cfg.vcmd_iosize; i += 4) {
				//set all register 0
				vcmd_write_reg((const void *)dev[n].hwregs, i,
					       0x0000);
			}
			//enable all interrupt
			vcmd_write_reg((const void *)dev[n].hwregs,
				       VCMD_REGISTER_INT_CTL_OFFSET,
				       0xffffffff);
			// gate all external interrupts
			vcmd_write_reg((const void *)dev[n].hwregs,
				       VCMD_REGISTER_EXT_INT_GATE_OFFSET,
				       0xffffffff);
		}
	}
}

static void vcmd_reset_current_asic(struct hantrovcmd_dev *dev)
{
	u32 result;

	if (dev->hwregs) {
		//disable interrupt at first
		vcmd_write_reg((const void *)dev->hwregs,
			       VCMD_REGISTER_INT_CTL_OFFSET, 0x0000);
		//reset core
		vcmd_write_reg((const void *)dev->hwregs,
			       VCMD_REGISTER_CONTROL_OFFSET, 0x0004);
		//read status register
		result = vcmd_read_reg((const void *)dev->hwregs,
				       VCMD_REGISTER_INT_STATUS_OFFSET);
		//clean status register
		vcmd_write_reg((const void *)dev->hwregs,
			       VCMD_REGISTER_INT_STATUS_OFFSET, result);
	}
}

static void printk_vcmd_register_debug(const void *hwregs, char *info)
{
#ifdef HANTRO_VCMD_DRIVER_DEBUG
	u32 i, fordebug;

	for (i = 0; i < ASIC_VCMD_SWREG_AMOUNT; i++) {
		fordebug = vcmd_read_reg((const void *)hwregs, i * 4);
		LOG_INFO("%s vcmd register %d:0x%x\n", info, i,
			fordebug);
	}
#endif
}

static int check_dev_idle(struct hantrovcmd_dev *dev) {
	int idle = 0;

	u8 vcmd_state = vcmd_get_register_value((const void *)dev->hwregs,
						dev->reg_mirror, HWIF_VCMD_WORK_STATE);
	if (WORKING_STATE_STALL != vcmd_state && WORKING_STATE_WORKING != vcmd_state) {
        idle = 1;
    } else {
        // LOG_WARN("check_dev_idle, vcmd_state = %u\n", vcmd_state);
    }
    LOG_WARN("check_dev_idle, vcmd_state = %u\n", vcmd_state);
	return idle;
}

int hantrovcmd_wait_core_idle(u32 core_id, long timeout) {
	struct hantrovcmd_dev *dev = NULL;

	if (core_id >= total_vcmd_core_num) {
		LOG_ERR("invalid core_id = %u, vcmd_core_num = %u\n", core_id, total_vcmd_core_num);
		return -ERESTARTSYS;
	}

	dev = &hantrovcmd_data[core_id];
	return wait_event_interruptible_timeout(*dev->wait_queue, check_dev_idle(dev), timeout);
}
