// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN NPU Clock Rate Definitaions
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Yang Wei <yangwei1@eswincomputing.com>
 */
#ifndef __LINUX_ESWIN_NPU_H
#define __LINUX_ESWIN_NPU_H

#define NPU_ACLK_RATE		800000000
#define NPU_DEFAULT_VOLTAGE 800000  //uV
#define NPU_LLC_CLK_RATE	800000000   //nvdla
#define NPU_CORE_CLK_RATE	1040000000  //npu and e31
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
#define NPU_1P5G_VOLTAGE    1080000  //uV
#else
#define NPU_1P5G_VOLTAGE    1050000  //uV
#endif
#define NPU_LLC_CLK_1P5G_RATE	1188000000    //nvdla
#define NPU_CORE_CLK_1P5G_RATE	1500000000  //npu and e31
#define NPU_E31_CLK_RATE	1040000000  //llc

#endif /* __LINUX_ESWIN_NPU_H */
