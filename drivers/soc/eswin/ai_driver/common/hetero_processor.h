// Copyright (c) 2023 ESWIN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
