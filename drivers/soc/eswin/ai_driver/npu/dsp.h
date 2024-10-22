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

#ifndef _NPU_DSP_H_
#define _NPU_DSP_H_
#include "internal_interface.h"

/* npu add dsp device info. */
int resolve_dsp_data(struct win_executor *executor);

/* npu set frame dsp io info */
int npu_set_dsp_iobuf(struct win_executor *executor, struct host_frame_desc *f);

/* when destroy model, need destroy dsp info */
void dsp_resource_destroy(struct win_executor * executor);

/* when release frame info ,need free dsp io info */
void destroy_frame_dsp_info(struct win_executor *executor, struct host_frame_desc *f);

#endif
