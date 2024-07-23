// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
