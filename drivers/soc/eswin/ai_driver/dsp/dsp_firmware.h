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

#ifndef DSP_FIRMWARE_H
#define DSP_FIRMWARE_H

struct es_dsp;

#if IS_ENABLED(CONFIG_FW_LOADER)
int dsp_request_firmware(struct es_dsp *dsp);
void dsp_release_firmware(struct es_dsp *dsp);
#else
static inline int dsp_request_firmware(struct es_dsp *dsp)
{
	(void)xvp;
	return -EINVAL;
}

static inline void dsp_release_firmware(struct es_dsp *dsp)
{
}
#endif

#endif
