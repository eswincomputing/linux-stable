// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "dla_log.h"
#include "dla_driver.h"

static const uint8_t map_lut_method[] = {
	FIELD_ENUM(CDP_S_LUT_CFG_0, LUT_LE_FUNCTION, EXPONENT),
	FIELD_ENUM(CDP_S_LUT_CFG_0, LUT_LE_FUNCTION, LINEAR)
};
static const uint8_t map_lut_out[] = {
	FIELD_ENUM(CDP_S_LUT_CFG_0, LUT_UFLOW_PRIORITY, LE),
	FIELD_ENUM(CDP_S_LUT_CFG_0, LUT_UFLOW_PRIORITY, LO)
};

static const uint16_t access_data_offset[] = {
	CDP_S_LUT_ACCESS_DATA_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_ACCESS_DATA_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lut_cfg_offset[] = {
	CDP_S_LUT_CFG_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_CFG_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lut_info_offset[] = {
	CDP_S_LUT_INFO_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_INFO_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t le_start_offset[] = {
	CDP_S_LUT_LE_START_LOW_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LE_START_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t le_end_offset[] = {
	CDP_S_LUT_LE_END_LOW_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LE_END_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lo_start_offset[] = {
	CDP_S_LUT_LO_START_LOW_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LO_START_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lo_end_offset[] = {
	CDP_S_LUT_LO_END_LOW_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LO_END_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t le_slope_scale_offset[] = {
	CDP_S_LUT_LE_SLOPE_SCALE_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LE_SLOPE_SCALE_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t le_slope_shift_offset[] = {
	CDP_S_LUT_LE_SLOPE_SHIFT_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LE_SLOPE_SHIFT_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lo_slope_scale_offset[] = {
	CDP_S_LUT_LO_SLOPE_SCALE_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LO_SLOPE_SCALE_0 - SDP_S_LUT_ACCESS_CFG_0,
};
static const uint16_t lo_slope_shift_offset[] = {
	CDP_S_LUT_LO_SLOPE_SHIFT_0 - CDP_S_LUT_ACCESS_CFG_0,
	SDP_S_LUT_LO_SLOPE_SHIFT_0 - SDP_S_LUT_ACCESS_CFG_0,
};

int validate_data_cube(struct dla_data_cube *src_data_cube,
		       struct dla_data_cube *dst_data_cube, uint8_t mem_type)
{
	int32_t ret = 0;

	if ((src_data_cube->width > DCUBE_MAX_WIDTH) ||
	    (src_data_cube->height > DCUBE_MAX_HEIGHT) ||
	    (src_data_cube->channel > DCUBE_MAX_CHANNEL)) {
		dla_error("Invalid SrcInput Cude[W: %u, H: %u, C: %u]",
			  src_data_cube->width, src_data_cube->height,
			  src_data_cube->channel);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if ((dst_data_cube->width > DCUBE_MAX_WIDTH) ||
	    (dst_data_cube->height > DCUBE_MAX_HEIGHT) ||
	    (dst_data_cube->channel > DCUBE_MAX_CHANNEL)) {
		dla_error("Invalid DstInput Cude[W: %u, H: %u, C: %u]",
			  dst_data_cube->width, dst_data_cube->height,
			  dst_data_cube->channel);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (src_data_cube->type > mem_type) {
		dla_error("Invalid src_data.mem_type: %u\n",
			  src_data_cube->type);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

	if (dst_data_cube->type > mem_type) {
		dla_error("Invalid dst_data.mem_type: %u\n",
			  dst_data_cube->type);
		ret = ERR(INVALID_INPUT);
		goto exit;
	}

exit:
	return ret;
}

int validate_precision(uint8_t precision, uint8_t map_precision)
{
	int32_t ret = 0;

	if (precision >= map_precision) {
		dla_error("Invalid precision: %u\n", precision);
		ret = ERR(INVALID_INPUT);
	}

	return ret;
}
