/*
 * ESWIN NPU Clock Rate Definitaions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_ESWIN_NPU_H
#define __LINUX_ESWIN_NPU_H

#define NPU_ACLK_RATE		800000000
#define NPU_DEFAULT_VOLTAGE 800000  //uV
#define NPU_LLC_CLK_RATE	800000000   //nvdla
#define NPU_CORE_CLK_RATE	1040000000  //npu and e31
#define NPU_1P5G_VOLTAGE    1050000  //uV
#define NPU_LLC_CLK_1P5G_RATE	1188000000    //nvdla
#define NPU_CORE_CLK_1P5G_RATE	1500000000  //npu and e31
#define NPU_E31_CLK_RATE	1040000000  //llc

#endif /* __LINUX_ESWIN_NPU_H */
