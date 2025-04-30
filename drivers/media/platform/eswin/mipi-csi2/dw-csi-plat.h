/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Synopsys, Inc.
 *
 * Synopsys DesignWare MIPI CSI-2 Host controller driver.
 * Supported bus formats
 *
 * Author: Luis Oliveira <Luis.Oliveira@synopsys.com>
 */

#ifndef _DW_CSI_PLAT_H__
#define _DW_CSI_PLAT_H__

#include <media/eswin/eswin_vi.h>
#include "dw-mipi-csi.h"

/** Color Space Converter Block **/
#define CSC_UNIT_NR 1
#define CSC_COREID 0x0000
#define CSC_CFG 0x0004
#define CSC_COEF_A(x) (0x0008 + ((x) * 4))
#define CSC_COEF_B(x) (0x0018 + ((x) * 4))
#define CSC_COEF_C(x) (0x0028 + ((x) * 4))
#define CSC_LIMIT_DN 0x0038
#define CSC_LIMIT_UP 0x003C
#define CSC_VID_CFG 0x0040

/** Demosaic Block **/
#define DEMO_CONTROL 0x0
#define DEMO_GLOBAL_INT_EN 0x4
#define DEMO_IP_INT_EN_REG 0x8
#define DEMO_IP_INT_SATUS_REG 0xC
#define DEMO_ACTIVE_WIDTH 0x10
#define DEMO_ACTIVE_HEIGHT 0x18
#define DEMO_BAYER_PHASE 0x28//0x20

#define BAYER_RGGB 0x0
#define BAYER_GRBG 0x1
#define BAYER_GBRG 0x2
#define BAYER_BGGR 0x3

/* Video formats supported by the MIPI CSI-2 */
struct mipi_fmt dw_mipi_csi_formats[] = {
	{
		/* RAW 8 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.depth = 8,
#if 0
	}, {
		/* RAW 6 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR6_1X8,
		.depth = 8,
	}, {
		/* RAW 7 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR7_1X8,
		.depth = 8,
#endif
	}, {
		/* RAW 10 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.depth = 10,
	}, {
		/* RAW 8 */
		.mbus_code = 0x3001,
		.depth = 8,
	}, {
		/* RAW 12 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.depth = 12,
	}, {
		/* RAW 14 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.depth = 14,
	}, {
		/* RAW 16 */
		.mbus_code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.depth = 16,
	}, {
		/* RGB 666 */
		.mbus_code = MEDIA_BUS_FMT_RGB666_1X18,
		.depth = 18,
	}, {
		/* RGB 565 */
		.mbus_code = MEDIA_BUS_FMT_RGB565_2X8_BE,
		.depth = 16,
	}, {
		/* BGR 565 */
		.mbus_code = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.depth = 16,
	}, {
		/* RGB 555 */
		.mbus_code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.depth = 16,
	}, {
		/* BGR 555 */
		.mbus_code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.depth = 16,
	}, {
		/* RGB 444 */
		.mbus_code = MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE,
		.depth = 16,
	}, {
		/* RGB 444 */
		.mbus_code = MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE,
		.depth = 16,
	}, {
		/* RGB 888 */
		.mbus_code = MEDIA_BUS_FMT_RGB888_2X12_LE,
		.depth = 24,
	}, {
		/* BGR 888 */
		.mbus_code = MEDIA_BUS_FMT_RGB888_2X12_BE,
		.depth = 24,
	}, {
		/* BGR 888 */
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
		.depth = 24,
	}, {
		/* YUV 422 8-bit */
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
		.depth = 16,
	}, {
		/* YUV 422 10-bit */
		.mbus_code = MEDIA_BUS_FMT_UYVY10_1X20,
		.depth = 24,
	}, {
		/* YUV 420 8-bit LEGACY */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.depth = 8,
	}, {
		/* YUV 420 8-bit LEGACY */
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
		.depth = 24,
	}, {
		/* YUV 420 10-bit */
		.mbus_code = MEDIA_BUS_FMT_VUY8_1X24,
		.depth = 24,
	}, {
		/* YUV 420 8-bit */
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.depth = 24,
	}, {
		/* YUV 420 10-bit */
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.depth = 10,
	},
};
#endif /* _DW_CSI_PLAT_H__ */
