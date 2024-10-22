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

#ifndef ESWIN_COMMON_HETERO_PROCESSOR_H_
#define ESWIN_COMMON_HETERO_PROCESSOR_H_
#include "dla_interface.h"

enum processors_list {
    IDX_START = DLA_OP_EDMA,
    IDX_EDMA = DLA_OP_EDMA,
    IDX_CONV = DLA_OP_CONV,
    IDX_SDP = DLA_OP_SDP,
    IDX_PDP = DLA_OP_PDP,
    IDX_RUBIK = DLA_OP_RUBIK,
    IDX_KMD_DSP0 = DLA_KMD_OP_DSP_0,
    IDX_KMD_DSP1 = DLA_KMD_OP_DSP_1,
    IDX_KMD_DSP2 = DLA_KMD_OP_DSP_2,
    IDX_KMD_DSP3 = DLA_KMD_OP_DSP_3,
    IDX_EVENT_SINK = DLA_OP_EVENT_SINK,
    IDX_EVENT_SOURCE = DLA_OP_EVENT_SOURCE,
    NUM_OP_TYPE,
    NUM_KMD_OP_TYPE = NUM_OP_TYPE,
    IDX_DSP0 = DLA_OP_DSP_0,
    IDX_DSP1 = DLA_OP_DSP_1,
    IDX_DSP2 = DLA_OP_DSP_2,
    IDX_DSP3 = DLA_OP_DSP_3,
    IDX_HAE = DLA_OP_HAE,
    IDX_GPU = DLA_OP_GPU,
    IDX_SWITCH = DLA_OP_SWITCH,
    IDX_MERGE = DLA_OP_MERGE,
    NUM_OP_ALL = HW_OP_NUM,
    IDX_LUT = NUM_OP_TYPE,
    IDX_LAST_OP = NUM_OP_TYPE - 1,
    IDX_KMD_LAST_OP = NUM_KMD_OP_TYPE - 1,
    MAX_KMD_DEPCNT = NUM_OP_TYPE - 3,  // In order to align memory and save space
    IDX_NONE = 0xFF,
};
#endif
