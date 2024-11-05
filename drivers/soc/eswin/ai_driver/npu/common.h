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

#ifndef __FIRMWARE_COMMON_H_
#define __FIRMWARE_COMMON_H_

#include <dla_interface.h>

#define DCUBE_MAX_WIDTH 8192
#define DCUBE_MAX_HEIGHT 8192
#define DCUBE_MAX_CHANNEL 8192

int32_t validate_data_cube(struct dla_data_cube *src_data_cube,
			   struct dla_data_cube *dst_data_cube,
			   uint8_t mem_type);
int32_t validate_precision(uint8_t precision, uint8_t map_precision);

#endif /* __FIRMWARE_COMMON_H_ */
