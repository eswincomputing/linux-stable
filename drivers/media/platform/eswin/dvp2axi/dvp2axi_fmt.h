// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 */

#ifndef __DVP2AXI_FMT_H__
#define __DVP2AXI_FMT_H__
#include <media/v4l2-common.h>
#include "dev.h"

#define CSI_WRDDR_TYPE_RAW8		(0x0 << 1)
#define CSI_WRDDR_TYPE_RAW10		(0x1 << 1)
#define CSI_WRDDR_TYPE_RAW12		(0x2 << 1)
#define CSI_WRDDR_TYPE_RGB888		(0x3 << 1)
#define CSI_WRDDR_TYPE_YUV422		(0x4 << 1)
#define CSI_WRDDR_TYPE_YUV420SP		(0x5 << 1)
#define CSI_WRDDR_TYPE_YUV400		(0x6 << 1)
#define CSI_WRDDR_TYPE_RGB565		(0x7 << 1)

#define CSI_YUV_INPUT_ORDER_UYVY	(0x0 << 16)
#define CSI_YUV_INPUT_ORDER_VYUY	(0x1 << 16)
#define CSI_YUV_INPUT_ORDER_YUYV	(0x2 << 16)
#define CSI_YUV_INPUT_ORDER_YVYU	(0x3 << 16)


// DVP2AXI STORED BIT WIDTH
#define DVP2AXI_RAW_STORED_BIT_WIDTH	(8U)
#define DVP2AXI_YUV_STORED_BIT_WIDTH	(8U)

/* DVP2AXI FORMAT */

#define INPUT_MODE_RAW			(0x04 << 2)
#define INPUT_MODE_JPEG			(0x05 << 2)

#define YUV_INPUT_ORDER_UYVY		(0x00 << 5)
#define YUV_INPUT_ORDER_YVYU		(0x01 << 5)
#define YUV_INPUT_ORDER_VYUY		(0x10 << 5)
#define YUV_INPUT_ORDER_YUYV		(0x03 << 5)
#define YUV_INPUT_422			(0x00 << 7)
#define YUV_INPUT_420			(0x01 << 7)

#define RAW_DATA_WIDTH_8		(0x00 << 11)
#define RAW_DATA_WIDTH_10		(0x01 << 11)
#define RAW_DATA_WIDTH_12		(0x02 << 11)
#define RAW_DATA_WIDTH_14		(0x03 << 11)
#define RAW_DATA_WIDTH_16		(0x04 << 11)

#define YUV_OUTPUT_422			(0x00 << 16)
#define YUV_OUTPUT_420			(0x01 << 16)

#define UV_STORAGE_ORDER_UVUV		(0x00 << 19)
#define UV_STORAGE_ORDER_VUVU		(0x01 << 19)

const struct dvp2axi_output_fmt out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.cplanes = 2,
		.mplanes = 1,
		.fmt_val = YUV_OUTPUT_422 | UV_STORAGE_ORDER_UVUV,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_val = YUV_OUTPUT_422 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV420SP,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV420SP,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVYU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_Y210,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 10, 20 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_VYUY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB565,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR666,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 18 },
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_Y16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4l2_PIX_FMT_EBD8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4l2_PIX_FMT_SPD16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_Y12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_Y10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	}
	/* TODO: We can support NV12M/NV21M/NV16M/NV61M too */
};

static const struct dvp2axi_input_fmt in_fmts[] = {
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_YUYV,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_YUYV,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_YVYU,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_YVYU,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_UYVY,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_UYVY,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_VYUY,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_VYUY,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_INTERLACED,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU10_2X10,
		.dvp_fmt_val = YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order = CSI_YUV_INPUT_ORDER_YVYU,
		.fmt_type = DVP2AXI_FMT_TYPE_YUV,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_BGR888_1X24,
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_GBR888_1X24,
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_RGB565_1X16,
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB565,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_Y12_1X12,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_EBD_1X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SPD_2X8,
		.dvp_fmt_val = INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
		.field = V4L2_FIELD_NONE,
	}
};

#endif