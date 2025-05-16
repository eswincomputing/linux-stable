// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI capture driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Eswin VI team
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/iommu.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include "es-isp32-config.h"

#include "dev.h"
// #include "mipi-csi2.h"
#include "common.h"
#include "es-dvp2axi-externel.h"

/* eic770x */
#include "dvp2axi.h"
#include <media/eswin/common-def.h>

#define CSI2_ERR_FSFE_MASK	(0xff << 8)
#define CSI2_ERR_COUNT_ALL_MASK	(0xff)

#define ES_DVP2AXI_V4L2_EVENT_ELEMS 4

#define DVP2AXI_REQ_BUFS_MIN 1
#define DVP2AXI_MIN_WIDTH 64
#define DVP2AXI_MIN_HEIGHT 64
#define DVP2AXI_MAX_WIDTH 8192
#define DVP2AXI_MAX_HEIGHT 8192

#define OUTPUT_STEP_WISE 8

#define ES_DVP2AXI_PLANE_Y 0
#define ES_DVP2AXI_PLANE_CBCR 1
#define ES_DVP2AXI_MAX_PLANE 3

#define STREAM_PAD_SINK 0
#define STREAM_PAD_SOURCE 1

#define DVP2AXI_TIMEOUT_FRAME_NUM (2)

#define DVP2AXI_DVP_PCLK_DUAL_EDGE \
	(V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING)

/*
 * Round up height when allocate memory so that EIC770X encoder can
 * use DMA buffer directly, though this may waste a bit of memory.
 */
#define MEMORY_ALIGN_ROUND_UP_HEIGHT 16

/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */

static inline void DVP2AXI_HalWriteReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address, u32 data) {
    writel(data, dvp2axi_hw->base_addr + address);
	// pr_info("%s: address:0x%x, data:0x%x\n", __func__, address, data);
}

static inline u32 DVP2AXI_HalReadReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address) {
    u32 val;
    val = readl(dvp2axi_hw->base_addr + address);
    return val;
}

static int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct dvp2axi_output_fmt out_fmts[] = {
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
		.bpp = { 24 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 24 },
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

// static inline void DVP2AXI_HalWriteReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address, u32 data) {
//     writel(data, dvp2axi_hw->base_addr + address);
// }

// static inline u32 DVP2AXI_HalReadReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address) {
//     u32 val;
//     val = readl(dvp2axi_hw->base_addr + address);
//     return val;
// }


static int es_dvp2axi_output_fmt_check(struct es_dvp2axi_stream *stream,
				  const struct dvp2axi_output_fmt *output_fmt)
{
	const struct dvp2axi_input_fmt *input_fmt = stream->dvp2axi_fmt_in;
	struct csi_channel_info *channel =
		&stream->dvp2axidev->channels[stream->id];
	int ret = -EINVAL;

	switch (input_fmt->mbus_code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
		if (output_fmt->fourcc == V4L2_PIX_FMT_NV16 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_NV61 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_NV12 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_NV21 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_YUYV ||
		    output_fmt->fourcc == V4L2_PIX_FMT_YVYU ||
		    output_fmt->fourcc == V4L2_PIX_FMT_UYVY ||
		    output_fmt->fourcc == V4L2_PIX_FMT_VYUY ||
			output_fmt->fourcc == V4L2_PIX_FMT_Y210)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_YVYU10_2X10:
	case MEDIA_BUS_FMT_UYVY10_2X10:
	case MEDIA_BUS_FMT_VYUY10_2X10:
		if (output_fmt->fourcc == V4L2_PIX_FMT_Y210)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		if (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB8 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGRBG8 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGBRG8 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SBGGR8 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_GREY)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_Y10_1X10:
		if (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB10 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGRBG10 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGBRG10 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SBGGR10 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_Y10)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_Y12_1X12:
		if (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB12 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGRBG12 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SGBRG12 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_SBGGR12 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_Y12)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_GBR888_1X24:
		if (output_fmt->fourcc == V4L2_PIX_FMT_RGB24 ||
		    output_fmt->fourcc == V4L2_PIX_FMT_BGR24)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
		if (output_fmt->fourcc == V4L2_PIX_FMT_RGB565)
			ret = 0;
		break;
	case MEDIA_BUS_FMT_EBD_1X8:
		if (output_fmt->fourcc == V4l2_PIX_FMT_EBD8 ||
		    (channel->data_bit == 8 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR8)) ||
		    (channel->data_bit == 10 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR10)) ||
		    (channel->data_bit == 12 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR12)) ||
		    (channel->data_bit == 16 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR16)))
			ret = 0;
		break;
	case MEDIA_BUS_FMT_SPD_2X8:
		if (output_fmt->fourcc == V4l2_PIX_FMT_SPD16 ||
		    (channel->data_bit == 8 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG8 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR8)) ||
		    (channel->data_bit == 10 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG10 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR10)) ||
		    (channel->data_bit == 12 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG12 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR12)) ||
		    (channel->data_bit == 16 &&
		     (output_fmt->fourcc == V4L2_PIX_FMT_SRGGB16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGRBG16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SGBRG16 ||
		      output_fmt->fourcc == V4L2_PIX_FMT_SBGGR16)))
			ret = 0;
		break;
	default:
		break;
	}
	if (ret)
		v4l2_dbg(4, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "input mbus_code 0x%x, can't transform to %c%c%c%c\n",
			 input_fmt->mbus_code, output_fmt->fourcc & 0xff,
			 (output_fmt->fourcc >> 8) & 0xff,
			 (output_fmt->fourcc >> 16) & 0xff,
			 (output_fmt->fourcc >> 24) & 0xff);
	return ret;
}

static int es_dvp2axi_stop_dma_capture(struct es_dvp2axi_stream *stream);

struct es_dvp2axi_rx_buffer *to_dvp2axi_rx_buf(struct esisp_rx_buf *dbufs)
{
	return container_of(dbufs, struct es_dvp2axi_rx_buffer, dbufs);
}

static struct v4l2_subdev *get_remote_sensor(struct es_dvp2axi_stream *stream,
					     u16 *index)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct v4l2_subdev *sub = NULL;

	local = &stream->vnode.vdev.entity.pads[0];
	if (!local) {
		v4l2_err(&stream->dvp2axidev->v4l2_dev,
			 "%s: video pad[0] is null\n", __func__);
		return NULL;
	}

	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev, "%s: remote pad is null\n",
			 __func__);
		return NULL;
	}

	if (index)
		*index = remote->index;

	sensor_me = remote->entity;

	sub = media_entity_to_v4l2_subdev(sensor_me);
	pr_info("get_remote_sensors sd:%s\n", sub->name);

	return sub;
}

static void get_remote_terminal_sensor(struct es_dvp2axi_stream *stream,
				       struct v4l2_subdev **sensor_sd)
{
	struct media_graph graph;
	struct media_entity *entity = &stream->vnode.vdev.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	int ret;

	/* Walk the graph to locate sensor nodes. */
	mutex_lock(&mdev->graph_mutex);
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		*sensor_sd = NULL;
		return;
	}

	media_graph_walk_start(&graph, entity);
	while ((entity = media_graph_walk_next(&graph))) {
		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			break;
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);
	if (entity){
			*sensor_sd = media_entity_to_v4l2_subdev(entity);
			pr_debug("%s %d sd->name :%s\n", __func__, __LINE__, (*sensor_sd)->name);
	}
	else
		*sensor_sd = NULL;
}

static struct es_dvp2axi_sensor_info *sd_to_sensor(struct es_dvp2axi_device *dev,
					      struct v4l2_subdev *sd)
{
	u32 i;

	for (i = 0; i < dev->num_sensors; ++i)
		if (dev->sensors[i].sd == sd)
			return &dev->sensors[i];

	if (i == dev->num_sensors) {
		for (i = 0; i < dev->num_sensors; ++i) {
			if (dev->sensors[i].mbus.type == V4L2_MBUS_CCP2)
				return &dev->lvds_subdev.sensor_self;
		}
	}

	return NULL;
}

static unsigned char get_data_type(u32 pixelformat, u8 cmd_mode_en,
				   u8 dsi_input)
{
	switch (pixelformat) {
	/* csi raw8 */
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_Y8_1X8:
		return 0x2a;
	/* csi raw10 */
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_Y10_1X10:
		return 0x2b;
	/* csi raw12 */
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_Y12_1X12:
		return 0x2c;
	/* csi uyvy 422 */
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
		return 0x1e;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_GBR888_1X24:
		if (dsi_input) {
			if (cmd_mode_en) /* dsi command mode*/
				return 0x39;
			else /* dsi video mode */
				return 0x3e;
		} else {
			return 0x24;
		}
	case MEDIA_BUS_FMT_RGB565_1X16:
		if (dsi_input) {
			if (cmd_mode_en) /* dsi command mode*/
				return 0x39;
			else /* dsi video mode */
				return 0x0e;
		} else {
			return 0x22;
		}
	case MEDIA_BUS_FMT_EBD_1X8:
		return 0x12;
	case MEDIA_BUS_FMT_SPD_2X8:
		return 0x2f;

	default:
		return 0x2b;
	}
}

static int get_csi_crop_align(const struct dvp2axi_input_fmt *fmt_in)
{
	switch (fmt_in->csi_fmt_val) {
	case CSI_WRDDR_TYPE_RGB888:
		return 24;
	case CSI_WRDDR_TYPE_RGB565:
		return 16;
	case CSI_WRDDR_TYPE_RAW10:
	case CSI_WRDDR_TYPE_RAW12:
		return 4;
	case CSI_WRDDR_TYPE_RAW8:
	case CSI_WRDDR_TYPE_YUV422:
		return 8;
	default:
		return -1;
	}
}

const struct dvp2axi_input_fmt *
es_dvp2axi_get_input_fmt(struct es_dvp2axi_device *dev, struct v4l2_rect *rect,
		    u32 pad_id, struct csi_channel_info *csi_info)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *sd = dev->terminal_sensor.sd;
	struct esmodule_channel_info ch_info = { 0 };
	struct esmodule_capture_info capture_info;
	int ret;
	u32 i;

	fmt.pad = 0;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.reserved[0] = 0;
	fmt.format.field = V4L2_FIELD_NONE;
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
	if (ret < 0) {
		v4l2_warn(sd->v4l2_dev,
			  "sensor fmt invalid, set to default size\n");
		goto set_default;
	}
	ch_info.index = pad_id;
	ret = v4l2_subdev_call(sd, core, ioctl, ESMODULE_GET_CHANNEL_INFO,
			       &ch_info);
	if (!ret) {
		fmt.format.width = ch_info.width;
		fmt.format.height = ch_info.height;
		fmt.format.code = ch_info.bus_fmt;
		if (ch_info.vc >= 0 && ch_info.vc <= 3)
			csi_info->vc = ch_info.vc;
		else
			csi_info->vc = 0xff;
		if (ch_info.bus_fmt == MEDIA_BUS_FMT_SPD_2X8 ||
		    ch_info.bus_fmt == MEDIA_BUS_FMT_EBD_1X8) {
			if (ch_info.data_type > 0)
				csi_info->data_type = ch_info.data_type;
			if (ch_info.data_bit > 0)
				csi_info->data_bit = ch_info.data_bit;
		}
	} else {
		csi_info->vc = 0xff;
	}

	v4l2_dbg(1, es_dvp2axi_debug, sd->v4l2_dev,
		 "remote fmt: mbus code:0x%x, size:%dx%d, field: %d\n",
		 fmt.format.code, fmt.format.width, fmt.format.height,
		 fmt.format.field);
	rect->left = 0;
	rect->top = 0;
	rect->width = fmt.format.width;
	rect->height = fmt.format.height;
	ret = v4l2_subdev_call(sd, core, ioctl, ESMODULE_GET_CAPTURE_MODE,
			       &capture_info);
	if (!ret) {
		if (capture_info.mode == ESMODULE_MULTI_DEV_COMBINE_ONE &&
		    dev->hw_dev->is_eic770xs2) {
			for (i = 0; i < capture_info.multi_dev.dev_num; i++) {
				if (capture_info.multi_dev.dev_idx[i] == 0)
					capture_info.multi_dev.dev_idx[i] = 2;
				else if (capture_info.multi_dev.dev_idx[i] == 2)
					capture_info.multi_dev.dev_idx[i] = 4;
				else if (capture_info.multi_dev.dev_idx[i] == 3)
					capture_info.multi_dev.dev_idx[i] = 5;
			}
		}
		csi_info->capture_info = capture_info;
	} else {
		csi_info->capture_info.mode = ESMODULE_CAPTURE_MODE_NONE;
	}
	for (i = 0; i < ARRAY_SIZE(in_fmts); i++)
		if (fmt.format.code == in_fmts[i].mbus_code &&
		    fmt.format.field == in_fmts[i].field)
			return &in_fmts[i];

	v4l2_err(sd->v4l2_dev, "remote sensor mbus code not supported\n");

set_default:
	rect->left = 0;
	rect->top = 0;
	rect->width = ES_DVP2AXI_DEFAULT_WIDTH;
	rect->height = ES_DVP2AXI_DEFAULT_HEIGHT;

	return NULL;
}

const struct dvp2axi_output_fmt *es_dvp2axi_find_output_fmt(struct es_dvp2axi_stream *stream,
						   u32 pixelfmt)
{
	const struct dvp2axi_output_fmt *fmt;
	u32 i;

	pr_debug("%s pixelfmt =0x%x\n", __func__, pixelfmt);
	for (i = 0; i < ARRAY_SIZE(out_fmts); i++) {
		fmt = &out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

static enum dvp2axi_reg_index get_reg_index_of_id_ctrl0(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_ID1_CTRL0;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_ID2_CTRL0;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_ID3_CTRL0;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0;
		break;
	}

	return index;
}

#if 0
static enum dvp2axi_reg_index get_reg_index_of_lvds_id_ctrl0(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_LVDS_ID0_CTRL0;
		break;
	case 1:
		index = DVP2AXI_REG_LVDS_ID1_CTRL0;
		break;
	case 2:
		index = DVP2AXI_REG_LVDS_ID2_CTRL0;
		break;
	case 3:
		index = DVP2AXI_REG_LVDS_ID3_CTRL0;
		break;
	default:
		index = DVP2AXI_REG_LVDS_ID0_CTRL0;
		break;
	}

	return index;
}
#endif

static enum dvp2axi_reg_index get_reg_index_of_id_ctrl1(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CTRL1;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_ID1_CTRL1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_ID2_CTRL1;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_ID3_CTRL1;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CTRL1;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm0_y_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm1_y_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm0_uv_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm1_uv_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm0_y_vlw(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm1_y_vlw(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_Y_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_Y_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm0_uv_vlw(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_UV_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_UV_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_frm1_uv_vlw(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_UV_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_FRAME1_VLW_UV_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_id_crop_start(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CROP_START;
		break;
	case 1:
		index = DVP2AXI_REG_MIPI_LVDS_ID1_CROP_START;
		break;
	case 2:
		index = DVP2AXI_REG_MIPI_LVDS_ID2_CROP_START;
		break;
	case 3:
		index = DVP2AXI_REG_MIPI_LVDS_ID3_CROP_START;
		break;
	default:
		index = DVP2AXI_REG_MIPI_LVDS_ID0_CROP_START;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_lvds_sav_eav_act0(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT0_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT0_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT0_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT0_ID3;
		break;
	default:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT0_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_lvds_sav_eav_act1(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT1_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT1_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT1_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT1_ID3;
		break;
	default:
		index = DVP2AXI_REG_LVDS_SAV_EAV_ACT1_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_lvds_sav_eav_blk0(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK0_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK0_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK0_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK0_ID3;
		break;
	default:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK0_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_lvds_sav_eav_blk1(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK1_ID0;
		break;
	case 1:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK1_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK1_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK1_ID3;
		break;
	default:
		index = DVP2AXI_REG_LVDS_SAV_EAV_BLK1_ID0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_dvp_reg_index_of_frm0_y_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y;
		break;
	case 1:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_dvp_reg_index_of_frm1_y_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_Y;
		break;
	case 1:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID3;
		break;
	default:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_Y;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_dvp_reg_index_of_frm0_uv_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_UV;
		break;
	case 1:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_DVP_FRM0_ADDR_UV;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_dvp_reg_index_of_frm1_uv_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_UV;
		break;
	case 1:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID1;
		break;
	case 2:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID2;
		break;
	case 3:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID3;
		break;
	default:
		index = DVP2AXI_REG_DVP_FRM1_ADDR_UV;
		break;
	}

	return index;
}

int es_dvp2axi_get_linetime(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor = &dvp2axi_dev->terminal_sensor;
	u32 numerator, denominator;
	u32 def_fps = 0;
	int line_time = 0;
	int vblank_def = 0;
	int vblank_curr = 0;

	numerator = sensor->fi.interval.numerator;
	denominator = sensor->fi.interval.denominator;
	if (!numerator || !denominator) {
		v4l2_err(
			&dvp2axi_dev->v4l2_dev,
			"get frame interval fail, numerator %d, denominator %d\n",
			numerator, denominator);
		return -EINVAL;
	}
	def_fps = denominator / numerator;
	if (!def_fps) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			 "get fps fail, numerator %d, denominator %d\n",
			 numerator, denominator);
		return -EINVAL;
	}
	vblank_def = es_dvp2axi_get_sensor_vblank_def(dvp2axi_dev);
	vblank_curr = es_dvp2axi_get_sensor_vblank(dvp2axi_dev);
	if (!vblank_def || !vblank_curr) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			 "get vblank fail, vblank_def %d, vblank_curr %d\n",
			 vblank_def, vblank_curr);
		return -EINVAL;
	}
	line_time = div_u64(1000000000, def_fps);
	line_time = div_u64(line_time, vblank_def + sensor->raw_rect.height);
	return line_time;
}

/***************************** stream operations ******************************/
static int es_dvp2axi_assign_new_buffer_oneframe(struct es_dvp2axi_stream *stream,
					    enum es_dvp2axi_yuvaddr_state stat)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct es_dvp2axi_buffer *buffer = NULL;
	// u32 frm_addr_y = DVP2AXI_REG_DVP_FRM0_ADDR_Y;
	// u32 frm_addr_uv = DVP2AXI_REG_DVP_FRM0_ADDR_UV;
	unsigned long flags;
	int ret = 0;
	int hdr_id;
	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (stat == ES_DVP2AXI_YUV_ADDR_STATE_INIT) {
		
		if (!stream->curr_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->curr_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				list_del(&stream->curr_buf->queue);
			}
		}

		if (stream->curr_buf) {
			DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL9_CSR + stream->id * 0xc, stream->curr_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
		} else {
			if (dummy_buf->vaddr) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL9_CSR + stream->id * 0xc, dummy_buf->dma_addr);
			}
		}

		if(dev->hdr.hdr_mode == HDR_X2) {
			hdr_id = DVP2AXI_HalReadReg(dev->hw_dev, VI_DVP2AXI_CTRL36_CSR);
			hdr_id |= 0x1 << (VI_DVP2AXI_DVP0_LAST_ID_OFFSET + stream->id * 2);
			DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL36_CSR, 0);
			DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL36_CSR, 0);
			if (!stream->next_buf) {
				if (!list_empty(&stream->buf_head)) {
					stream->next_buf = list_first_entry(
						&stream->buf_head, struct es_dvp2axi_buffer,
						queue);
					list_del(&stream->next_buf->queue);
				}
			}
			if (stream->next_buf) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL10_CSR + stream->id * 0xc, stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else {
				if (dummy_buf->vaddr) {
					DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				}
			}
		} else if(dev->hdr.hdr_mode == HDR_X3) {
			hdr_id = DVP2AXI_HalReadReg(dev->hw_dev, VI_DVP2AXI_CTRL36_CSR);
			hdr_id |= 0x2 << (VI_DVP2AXI_DVP0_LAST_ID_OFFSET + stream->id * 2);
			if (!stream->next_buf) {
				if (!list_empty(&stream->buf_head)) {
					stream->next_buf = list_first_entry(
						&stream->buf_head, struct es_dvp2axi_buffer,
						queue);
					list_del(&stream->next_buf->queue);
				}
			}
			if (stream->next_buf) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else {
				if (dummy_buf->vaddr) {
					DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				}
			}

			if (!stream->last_buf) {
				if (!list_empty(&stream->buf_head)) {
					stream->last_buf = list_first_entry(
						&stream->buf_head, struct es_dvp2axi_buffer,
						queue);
					list_del(&stream->last_buf->queue);
				}
			}
			if (stream->last_buf) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else {
				if (dummy_buf->vaddr) {
					DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				}
			}

		}
	} else if (stat == ES_DVP2AXI_YUV_ADDR_STATE_UPDATE) {
		if (!list_empty(&stream->buf_head)) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				stream->curr_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
			} else if (stream->frame_phase ==
				   DVP2AXI_CSI_FRAME1_READY) {
				stream->next_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				list_del(&stream->next_buf->queue);
				buffer = stream->next_buf;
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
				stream->last_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				list_del(&stream->last_buf->queue);
				buffer = stream->last_buf;
			}
		} else {
			buffer = NULL;
		}
		

		if (buffer) {
			if(stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL9_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			}
		} else {
			// ret = -EINVAL;
			if(stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				stream->curr_buf = NULL;
			}
			if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
				stream->next_buf = NULL;
			}
			if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
				stream->last_buf = NULL;
			}
			if(dummy_buf->vaddr) {
				DVP2AXI_HalWriteReg(dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, dummy_buf->dma_addr);
			}
			dev->err_state |= (ES_DVP2AXI_ERR_ID0_NOT_BUF << stream->id);
			dev->irq_stats.not_active_buf_cnt[stream->id]++;
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	return ret;
}

static struct v4l2_subdev *get_esisp_sd(struct sditf_priv *priv)
{
	struct media_pad *pad = NULL;

	if (priv && priv->pads[0].entity->num_links) {
		if (priv->is_combine_mode)
			pad = media_pad_remote_pad_first(&priv->pads[1]);
		else
			pad = media_pad_remote_pad_first(&priv->pads[0]);
		if (pad)
			return media_entity_to_v4l2_subdev(pad->entity);
	}
	return NULL;
}

static void es_dvp2axi_rx_buffer_free(struct es_dvp2axi_stream *stream)
{
	struct v4l2_subdev *sd;
	struct esisp_rx_buf *dbufs;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;

	sd = get_esisp_sd(dev->sditf[0]);
	if (!sd)
		return;

	while (!list_empty(&stream->rx_buf_head_vicap)) {
		dbufs = list_first_entry(&stream->rx_buf_head_vicap,
					 struct esisp_rx_buf, list);
		if (dbufs->is_init)
			v4l2_subdev_call(sd, core, ioctl,
					 ESISP_VICAP_CMD_RX_BUFFER_FREE, dbufs);
		dma_buf_put(dbufs->dbuf);
		list_del(&dbufs->list);
		kfree(dbufs);
	}
}

static void es_dvp2axi_s_rx_buffer(struct es_dvp2axi_stream *stream,
			      struct esisp_rx_buf *dbufs)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_subdev *sd;
	struct es_dvp2axi_rx_buffer *rx_buf = NULL;

	sd = get_esisp_sd(dev->sditf[0]);
	if (!sd)
		return;
	if ((dev->rdbk_debug && dbufs->sequence < 15) || es_dvp2axi_debug == 3) {
		rx_buf = to_dvp2axi_rx_buf(dbufs);
		v4l2_info(&dev->v4l2_dev,
			  "s_buf seq %d type %d, dma addr %x, %lld\n",
			  dbufs->sequence, dbufs->type,
			  (u32)rx_buf->dummy.dma_addr, es_dvp2axi_time_get_ns(dev));
	}
	v4l2_subdev_call(sd, video, s_rx_buffer, dbufs, NULL);
}

#if 0
static void es_dvp2axi_enable_skip_frame(struct es_dvp2axi_stream *stream, int cap_m,
				    int skip_n)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	u32 val = 0;

	val = es_dvp2axi_read_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL);
	val &= 0xc00fffff;
	val |= cap_m << ES_DVP2AXI_CAP_SHIFT | skip_n << ES_DVP2AXI_SKIP_SHIFT |
	       ES_DVP2AXI_SKIP_EN(stream->id);
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL, val);
	stream->skip_info.skip_en = true;
}
#endif

#if 0
static void es_dvp2axi_disable_skip_frame(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	u32 val = 0;

	val = es_dvp2axi_read_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL);
	val &= ~(ES_DVP2AXI_SKIP_EN(stream->id));
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL, val);
	stream->skip_info.skip_en = false;
}
#endif

static void es_dvp2axi_rdbk_with_tools(struct es_dvp2axi_stream *stream,
				  struct es_dvp2axi_rx_buffer *active_buf)
{
	unsigned long flags;

	spin_lock_irqsave(&stream->tools_vdev->vbq_lock, flags);
	if (stream->tools_vdev->state == ES_DVP2AXI_STATE_STREAMING) {
		list_add_tail(&active_buf->list,
			      &stream->tools_vdev->buf_done_head);
		if (!work_busy(&stream->tools_vdev->work))
			schedule_work(&stream->tools_vdev->work);
	}
	spin_unlock_irqrestore(&stream->tools_vdev->vbq_lock, flags);
}

static void es_dvp2axi_rdbk_frame_end_toisp(struct es_dvp2axi_stream *stream,
				       struct es_dvp2axi_rx_buffer *buffer)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor = &stream->dvp2axidev->terminal_sensor;
	u32 denominator, numerator;
	u64 l_ts, m_ts, s_ts, time = 30000000LL;
	int ret, fps = -1;
	unsigned long flags;

	spin_lock_irqsave(&dev->hdr_lock, flags);
	if (dev->rdbk_rx_buf[stream->id]) {
		list_add_tail(&dev->rdbk_rx_buf[stream->id]->list,
			      &stream->rx_buf_head);
		dev->rdbk_rx_buf[stream->id] = buffer;
	} else {
		dev->rdbk_rx_buf[stream->id] = buffer;
	}

	numerator = sensor->fi.interval.numerator;
	denominator = sensor->fi.interval.denominator;
	if (denominator && numerator)
		time = numerator * 1000 / denominator * 1000 * 1000;

	if (dev->hdr.hdr_mode == HDR_X3 && dev->rdbk_rx_buf[RDBK_L] &&
	    dev->rdbk_rx_buf[RDBK_M] && dev->rdbk_rx_buf[RDBK_S]) {
		l_ts = dev->rdbk_rx_buf[RDBK_L]->fe_timestamp;
		m_ts = dev->rdbk_rx_buf[RDBK_M]->fe_timestamp;
		s_ts = dev->rdbk_rx_buf[RDBK_S]->fe_timestamp;

		if (m_ts < l_ts || s_ts < m_ts) {
			v4l2_err(
				&dev->v4l2_dev,
				"s/m/l frame err, timestamp s:%lld m:%lld l:%lld\n",
				s_ts, m_ts, l_ts);
			goto RDBK_TOISP_UNMATCH;
		}

		if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
			ret = v4l2_subdev_call(sensor->sd, video,
					       g_frame_interval, &sensor->fi);
			if (!ret) {
				denominator = sensor->fi.interval.denominator;
				numerator = sensor->fi.interval.numerator;
				if (denominator && numerator) {
					time = numerator * 1000 / denominator *
					       1000 * 1000;
					fps = denominator / numerator;
				}
			}

			if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
				v4l2_err(
					&dev->v4l2_dev,
					"timestamp no match, s:%lld m:%lld l:%lld, fps:%d\n",
					s_ts, m_ts, l_ts, fps);
				goto RDBK_TOISP_UNMATCH;
			}
		}
		dev->rdbk_rx_buf[RDBK_M]->dbufs.sequence =
			dev->rdbk_rx_buf[RDBK_L]->dbufs.sequence;
		dev->rdbk_rx_buf[RDBK_S]->dbufs.sequence =
			dev->rdbk_rx_buf[RDBK_L]->dbufs.sequence;
		es_dvp2axi_s_rx_buffer(&dev->stream[RDBK_L],
				  &dev->rdbk_rx_buf[RDBK_L]->dbufs);
		es_dvp2axi_s_rx_buffer(&dev->stream[RDBK_M],
				  &dev->rdbk_rx_buf[RDBK_M]->dbufs);
		es_dvp2axi_s_rx_buffer(&dev->stream[RDBK_S],
				  &dev->rdbk_rx_buf[RDBK_S]->dbufs);
		es_dvp2axi_rdbk_with_tools(&dev->stream[RDBK_L],
				      dev->rdbk_rx_buf[RDBK_L]);
		es_dvp2axi_rdbk_with_tools(&dev->stream[RDBK_M],
				      dev->rdbk_rx_buf[RDBK_M]);
		es_dvp2axi_rdbk_with_tools(&dev->stream[RDBK_S],
				      dev->rdbk_rx_buf[RDBK_S]);
		atomic_dec(&dev->stream[RDBK_L].buf_cnt);
		atomic_dec(&dev->stream[RDBK_M].buf_cnt);
		atomic_dec(&dev->stream[RDBK_S].buf_cnt);
		dev->rdbk_rx_buf[RDBK_L] = NULL;
		dev->rdbk_rx_buf[RDBK_M] = NULL;
		dev->rdbk_rx_buf[RDBK_S] = NULL;
	} else if (dev->hdr.hdr_mode == HDR_X2 && dev->rdbk_rx_buf[RDBK_L] &&
		   dev->rdbk_rx_buf[RDBK_M]) {
		l_ts = dev->rdbk_rx_buf[RDBK_L]->fe_timestamp;
		s_ts = dev->rdbk_rx_buf[RDBK_M]->fe_timestamp;

		if (s_ts < l_ts) {
			v4l2_err(&dev->v4l2_dev,
				 "s/l frame err, timestamp s:%lld l:%lld\n",
				 s_ts, l_ts);
			goto RDBK_TOISP_UNMATCH;
		}

		if ((s_ts - l_ts) > time) {
			ret = v4l2_subdev_call(sensor->sd, video,
					       g_frame_interval, &sensor->fi);
			if (!ret) {
				denominator = sensor->fi.interval.denominator;
				numerator = sensor->fi.interval.numerator;
				if (denominator && numerator) {
					time = numerator * 1000 / denominator *
					       1000 * 1000;
					fps = denominator / numerator;
				}
			}
			if ((s_ts - l_ts) > time) {
				v4l2_err(
					&dev->v4l2_dev,
					"timestamp no match, s:%lld l:%lld, fps:%d\n",
					s_ts, l_ts, fps);
				goto RDBK_TOISP_UNMATCH;
			}
		}
		dev->rdbk_rx_buf[RDBK_M]->dbufs.sequence =
			dev->rdbk_rx_buf[RDBK_L]->dbufs.sequence;
		es_dvp2axi_s_rx_buffer(&dev->stream[RDBK_L],
				  &dev->rdbk_rx_buf[RDBK_L]->dbufs);
		es_dvp2axi_s_rx_buffer(&dev->stream[RDBK_M],
				  &dev->rdbk_rx_buf[RDBK_M]->dbufs);
		es_dvp2axi_rdbk_with_tools(&dev->stream[RDBK_L],
				      dev->rdbk_rx_buf[RDBK_L]);
		es_dvp2axi_rdbk_with_tools(&dev->stream[RDBK_M],
				      dev->rdbk_rx_buf[RDBK_M]);
		atomic_dec(&dev->stream[RDBK_L].buf_cnt);
		atomic_dec(&dev->stream[RDBK_M].buf_cnt);
		dev->rdbk_rx_buf[RDBK_L] = NULL;
		dev->rdbk_rx_buf[RDBK_M] = NULL;
	}

	spin_unlock_irqrestore(&dev->hdr_lock, flags);
	return;

RDBK_TOISP_UNMATCH:
	spin_unlock_irqrestore(&dev->hdr_lock, flags);
}

static void es_dvp2axi_write_buff_addr_multi_dev_combine(
	struct es_dvp2axi_stream *stream, u32 frm_addr_y, u32 frm_addr_uv,
	u32 buff_addr_y, u32 buff_addr_cbcr, bool is_dummy_buf)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct esmodule_capture_info *capture_info =
		&dev->channels[stream->id].capture_info;
	u32 addr_y, addr_cbcr;
	int addr_offset = 0;
	int i = 0;
	int tmp_host_index = dev->csi_host_idx;

	for (i = 0; i < capture_info->multi_dev.dev_num; i++) {
		if (is_dummy_buf) {
			addr_y = buff_addr_y;
		} else {
			addr_offset =
				dev->channels[stream->id].left_virtual_width;
			addr_y = buff_addr_y + addr_offset * i;
		}
		dev->csi_host_idx = capture_info->multi_dev.dev_idx[i];
		es_dvp2axi_write_register(dev, frm_addr_y, addr_y);
		if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW &&
		    frm_addr_uv && buff_addr_cbcr) {
			if (is_dummy_buf) {
				addr_cbcr = buff_addr_cbcr;
			} else {
				addr_offset = dev->channels[stream->id]
						      .left_virtual_width;
				addr_cbcr = buff_addr_cbcr + addr_offset * i;
			}
			es_dvp2axi_write_register(dev, frm_addr_uv, addr_cbcr);
		}
	}
	dev->csi_host_idx = tmp_host_index;
}

static void es_dvp2axi_assign_new_buffer_init_toisp(struct es_dvp2axi_stream *stream,
					       int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_rx_buffer *rx_buf;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct esmodule_capture_info *capture_info =
		&dev->channels[channel_id].capture_info;
	u32 frm0_addr_y;
	u32 frm1_addr_y;
	u32 buff_addr_y;
	unsigned long flags;

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm0_addr_y = get_reg_index_of_frm0_y_addr(channel_id);
		frm1_addr_y = get_reg_index_of_frm1_y_addr(channel_id);
	} else {
		frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(channel_id);
		frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(channel_id);
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);

	if (!stream->curr_buf_toisp) {
		if (!list_empty(&stream->rx_buf_head)) {
			rx_buf = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (rx_buf) {
				list_del(&rx_buf->list);
				stream->curr_buf_toisp = rx_buf;
			}
		}
	}

	if (stream->curr_buf_toisp) {
		buff_addr_y = stream->curr_buf_toisp->dummy.dma_addr;
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm0_addr_y, 0, buff_addr_y, 0, false);
		} else {
			es_dvp2axi_write_register(dev, frm0_addr_y, buff_addr_y);
		}
	} else {
		if (stream->lack_buf_cnt < 2)
			stream->lack_buf_cnt++;
	}

	if (!stream->next_buf_toisp) {
		if (!list_empty(&stream->rx_buf_head)) {
			rx_buf = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (rx_buf) {
				list_del(&rx_buf->list);
				stream->next_buf_toisp = rx_buf;
			} else {
				stream->next_buf_toisp = stream->curr_buf_toisp;
			}
		} else {
			stream->next_buf_toisp = stream->curr_buf_toisp;
		}
	}

	if (stream->next_buf_toisp) {
		buff_addr_y = stream->next_buf_toisp->dummy.dma_addr;
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm1_addr_y, 0, buff_addr_y, 0, false);
		} else {
			es_dvp2axi_write_register(dev, frm1_addr_y, buff_addr_y);
		}
	} else {
		if (stream->lack_buf_cnt < 2)
			stream->lack_buf_cnt++;
	}

	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	stream->buf_owner = ES_DVP2AXI_DMAEN_BY_ISP;
}

static void es_dvp2axi_dphy_quick_stream(struct es_dvp2axi_device *dev, int on)
{
	struct es_dvp2axi_pipeline *p = NULL;
	int j = 0;
	if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
	    dev->active_sensor->mbus.type == V4L2_MBUS_CCP2) {
		p = &dev->pipe;
		for (j = 0; j < p->num_subdevs; j++) {
			if (p->subdevs[j] != dev->terminal_sensor.sd &&
			    p->subdevs[j] != dev->active_sensor->sd) {
				v4l2_subdev_call(p->subdevs[j], core, ioctl,
						 ESMODULE_SET_QUICK_STREAM,
						 &on);
				break;
			}
		}
	}
}

static int es_dvp2axi_assign_new_buffer_update_toisp(struct es_dvp2axi_stream *stream,
						int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct esmodule_capture_info *capture_info =
		&dev->channels[channel_id].capture_info;
	struct es_dvp2axi_rx_buffer *buffer = NULL;
	struct es_dvp2axi_rx_buffer *active_buf = NULL;
	struct sditf_priv *priv = dev->sditf[0];
	u32 frm_addr_y, buff_addr_y;
	unsigned long flags;

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				     get_reg_index_of_frm0_y_addr(channel_id) :
				     get_reg_index_of_frm1_y_addr(channel_id);
	} else {
		frm_addr_y =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_y_addr(channel_id) :
				get_dvp_reg_index_of_frm1_y_addr(channel_id);
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (stream->cur_skip_frame)
		goto out_get_buf;
	memset(&stream->toisp_buf_state, 0, sizeof(stream->toisp_buf_state));
	if (!list_empty(&stream->rx_buf_head)) {
		if (stream->curr_buf_toisp && stream->next_buf_toisp &&
		    stream->curr_buf_toisp != stream->next_buf_toisp)
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_ROTATE;
		else
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_LOSS;
		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
			active_buf = stream->curr_buf_toisp;

			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->curr_buf_toisp = buffer;
			}
			if (priv &&
			    priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
				if (!active_buf)
					goto out_get_buf;
				if (stream->frame_idx == 1)
					active_buf->dbufs.is_first = true;
				active_buf->dbufs.sequence =
					stream->frame_idx - 1;
				active_buf->dbufs.timestamp =
					stream->readout.fs_timestamp;
				active_buf->fe_timestamp =
					es_dvp2axi_time_get_ns(dev);
				stream->last_frame_idx = stream->frame_idx;
				if (dev->hdr.hdr_mode == NO_HDR) {
					es_dvp2axi_s_rx_buffer(stream,
							  &active_buf->dbufs);
					if (dev->is_support_tools &&
					    stream->tools_vdev)
						es_dvp2axi_rdbk_with_tools(
							stream, active_buf);
					atomic_dec(&stream->buf_cnt);
				} else {
					es_dvp2axi_rdbk_frame_end_toisp(stream,
								   active_buf);
				}
			} else {
				if (active_buf)
					es_dvp2axi_s_rx_buffer(stream,
							  &active_buf->dbufs);
				if (dev->is_support_tools && stream->tools_vdev)
					es_dvp2axi_rdbk_with_tools(stream,
							      active_buf);
			}
		} else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			active_buf = stream->next_buf_toisp;
			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->next_buf_toisp = buffer;
			}
			if (priv &&
			    priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
				if (!active_buf)
					goto out_get_buf;
				if (stream->frame_idx == 1)
					active_buf->dbufs.is_first = true;
				active_buf->dbufs.sequence =
					stream->frame_idx - 1;
				active_buf->dbufs.timestamp =
					stream->readout.fs_timestamp;
				active_buf->fe_timestamp =
					es_dvp2axi_time_get_ns(dev);
				stream->last_frame_idx = stream->frame_idx;
				if (dev->hdr.hdr_mode == NO_HDR) {
					es_dvp2axi_s_rx_buffer(stream,
							  &active_buf->dbufs);
					if (dev->is_support_tools &&
					    stream->tools_vdev)
						es_dvp2axi_rdbk_with_tools(
							stream, active_buf);
					atomic_dec(&stream->buf_cnt);
				} else {
					es_dvp2axi_rdbk_frame_end_toisp(stream,
								   active_buf);
				}
			} else {
				if (active_buf)
					es_dvp2axi_s_rx_buffer(stream,
							  &active_buf->dbufs);
				if (dev->is_support_tools && stream->tools_vdev)
					es_dvp2axi_rdbk_with_tools(stream,
							      active_buf);
			}
		}
		if (stream->lack_buf_cnt)
			stream->lack_buf_cnt--;
	} else {
		if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
			goto out_get_buf;
		if (stream->lack_buf_cnt < 2)
			stream->lack_buf_cnt++;
		if (dev->hw_dev->dummy_buf.vaddr) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				active_buf = stream->curr_buf_toisp;
			} else {
				active_buf = stream->next_buf_toisp;
			}
		} else if (stream->curr_buf_toisp && stream->next_buf_toisp &&
			   stream->curr_buf_toisp != stream->next_buf_toisp) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				active_buf = stream->curr_buf_toisp;
				stream->curr_buf_toisp = stream->next_buf_toisp;
				buffer = stream->next_buf_toisp;
			} else if (stream->frame_phase ==
				   DVP2AXI_CSI_FRAME1_READY) {
				active_buf = stream->next_buf_toisp;
				stream->next_buf_toisp = stream->curr_buf_toisp;
				buffer = stream->curr_buf_toisp;
			}
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_THESAME;
			if (stream->dvp2axidev->rdbk_debug)
				v4l2_info(&stream->dvp2axidev->v4l2_dev,
					  "stream[%d] hold buf %x\n",
					  stream->id,
					  (u32)stream->next_buf_toisp->dummy
						  .dma_addr);
		} else {
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_LOSS;
			if (stream->is_single_cap) {
				active_buf = stream->curr_buf_toisp;
				stream->curr_buf_toisp = NULL;
				stream->next_buf_toisp = NULL;
			}
		}

		if (active_buf) {
			if (stream->frame_idx == 1)
				active_buf->dbufs.is_first = true;
			active_buf->dbufs.sequence = stream->frame_idx - 1;
			active_buf->dbufs.timestamp =
				stream->readout.fs_timestamp;
			active_buf->fe_timestamp = es_dvp2axi_time_get_ns(dev);
			stream->last_frame_idx = stream->frame_idx;
			if (dev->hdr.hdr_mode == NO_HDR) {
				es_dvp2axi_s_rx_buffer(stream, &active_buf->dbufs);
				atomic_dec(&stream->buf_cnt);
			} else {
				es_dvp2axi_rdbk_frame_end_toisp(stream, active_buf);
			}
		} else {
			if (stream->dvp2axidev->rdbk_debug &&
			    dev->hw_dev->dummy_buf.vaddr)
				v4l2_info(&stream->dvp2axidev->v4l2_dev,
					  "stream[%d] loss frame %d\n",
					  stream->id, stream->frame_idx - 1);
		}
		if (dev->is_support_tools && stream->tools_vdev && active_buf)
			es_dvp2axi_rdbk_with_tools(stream, active_buf);
	}
out_get_buf:
	stream->frame_phase_cache = stream->frame_phase;
	if (buffer) {
		buff_addr_y = buffer->dummy.dma_addr;
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm_addr_y, 0, buff_addr_y, 0, false);
		} else {
			es_dvp2axi_write_register(dev, frm_addr_y, buff_addr_y);
		}
		if (dev->rdbk_debug > 1 && stream->frame_idx < 15)
			v4l2_info(&dev->v4l2_dev,
				  "stream[%d] update, seq %d, reg %x, buf %x\n",
				  stream->id, stream->frame_idx - 1, frm_addr_y,
				  (u32)buffer->dummy.dma_addr);
	} else if (dev->hw_dev->dummy_buf.vaddr && priv &&
		   priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
		buff_addr_y = dev->hw_dev->dummy_buf.dma_addr;
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm_addr_y, 0, buff_addr_y, 0, true);
		} else {
			es_dvp2axi_write_register(dev, frm_addr_y, buff_addr_y);
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	return 0;
}

static int es_dvp2axi_assign_new_buffer_pingpong_toisp(struct es_dvp2axi_stream *stream,
						  int init, int channel_id)
{
	int ret = 0;

	if (init)
		es_dvp2axi_assign_new_buffer_init_toisp(stream, channel_id);
	else
		ret = es_dvp2axi_assign_new_buffer_update_toisp(stream, channel_id);
	return ret;
}

void es_dvp2axi_assign_check_buffer_update_toisp(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct es_dvp2axi_rx_buffer *buffer = NULL;
	struct esmodule_capture_info *capture_info =
		&dev->channels[stream->id].capture_info;
	struct es_dvp2axi_rx_buffer *active_buf = NULL;
	u32 frm_addr_y, buff_addr_y;
	u32 vblank = 0;
	u32 vblank_ns = 0;
	u64 cur_time = 0;
	int frame_phase = 0;
	int frame_phase_next = 0;

	if (stream->toisp_buf_state.state == ES_DVP2AXI_TOISP_BUF_ROTATE ||
	    (stream->toisp_buf_state.state == ES_DVP2AXI_TOISP_BUF_THESAME &&
	     stream->toisp_buf_state.check_cnt >= 1) ||
	    (stream->toisp_buf_state.state == ES_DVP2AXI_TOISP_BUF_LOSS &&
	     stream->toisp_buf_state.check_cnt >= 2)) {
		if (dev->rdbk_debug > 2 && stream->frame_idx < 15)
			v4l2_info(&dev->v4l2_dev,
				  "stream[%d] addr check not equal 0x%x 0x%x\n",
				  stream->id,
				  (u32)stream->curr_buf_toisp->dummy.dma_addr,
				  (u32)stream->next_buf_toisp->dummy.dma_addr);
		return;
	}
	if (!dev->sensor_linetime)
		dev->sensor_linetime = es_dvp2axi_get_linetime(stream);
	vblank = es_dvp2axi_get_sensor_vblank(dev);
	vblank_ns = vblank * dev->sensor_linetime;
	cur_time = es_dvp2axi_time_get_ns(dev);

	if (stream->toisp_buf_state.state == ES_DVP2AXI_TOISP_BUF_THESAME) {
		frame_phase = stream->frame_phase;
	} else {
		if (stream->toisp_buf_state.state == ES_DVP2AXI_TOISP_BUF_LOSS &&
		    stream->toisp_buf_state.check_cnt == 0 &&
		    cur_time - stream->readout.fe_timestamp <
			    (vblank_ns - 500000)) {
			stream->toisp_buf_state.is_early_update = true;
			frame_phase = stream->frame_phase &
						      DVP2AXI_CSI_FRAME0_READY ?
					      DVP2AXI_CSI_FRAME1_READY :
					      DVP2AXI_CSI_FRAME0_READY;
			frame_phase_next =
				stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
					DVP2AXI_CSI_FRAME0_READY :
					DVP2AXI_CSI_FRAME1_READY;
		} else {
			if (stream->toisp_buf_state.check_cnt == 1 &&
			    (!stream->toisp_buf_state.is_early_update))
				return;
			frame_phase = stream->frame_phase;
			stream->toisp_buf_state.is_early_update = false;
		}
	}
	if (dev->rdbk_debug > 2 && stream->frame_idx < 15)
		v4l2_info(
			&dev->v4l2_dev,
			"stream[%d] check update, cur %lld, fe %lld, vb %u lack_buf %d\n",
			stream->id, cur_time, stream->readout.fe_timestamp,
			vblank_ns, stream->lack_buf_cnt);
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				     get_reg_index_of_frm0_y_addr(stream->id) :
				     get_reg_index_of_frm1_y_addr(stream->id);
	} else {
		frm_addr_y =
			frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_y_addr(stream->id) :
				get_dvp_reg_index_of_frm1_y_addr(stream->id);
	}
	if (!list_empty(&stream->rx_buf_head)) {
		if (frame_phase == DVP2AXI_CSI_FRAME0_READY) {
			active_buf = stream->curr_buf_toisp;
			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->curr_buf_toisp = buffer;
				buff_addr_y =
					stream->curr_buf_toisp->dummy.dma_addr;
				if (capture_info->mode ==
				    ESMODULE_MULTI_DEV_COMBINE_ONE) {
					es_dvp2axi_write_buff_addr_multi_dev_combine(
						stream, frm_addr_y, 0,
						buff_addr_y, 0, false);
				} else {
					es_dvp2axi_write_register(dev, frm_addr_y,
							     buff_addr_y);
				}
				if (dev->rdbk_debug > 1 &&
				    stream->frame_idx < 15)
					v4l2_info(
						&dev->v4l2_dev,
						"stream[%d] check update, seq %d, addr 0x%x, buf 0x%x\n",
						stream->id,
						stream->frame_idx - 1,
						frm_addr_y,
						(u32)stream->curr_buf_toisp
							->dummy.dma_addr);
			}
		} else if (frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			active_buf = stream->next_buf_toisp;
			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->next_buf_toisp = buffer;
				buff_addr_y =
					stream->next_buf_toisp->dummy.dma_addr;
				if (capture_info->mode ==
				    ESMODULE_MULTI_DEV_COMBINE_ONE) {
					es_dvp2axi_write_buff_addr_multi_dev_combine(
						stream, frm_addr_y, 0,
						buff_addr_y, 0, false);
				} else {
					es_dvp2axi_write_register(dev, frm_addr_y,
							     buff_addr_y);
				}
				if (dev->rdbk_debug > 1 &&
				    stream->frame_idx < 15)
					v4l2_info(
						&dev->v4l2_dev,
						"stream[%d] check update, seq %d, addr 0x%x, buf 0x%x\n",
						stream->id,
						stream->frame_idx - 1,
						frm_addr_y,
						(u32)stream->next_buf_toisp
							->dummy.dma_addr);
			}
		}
		if (stream->lack_buf_cnt)
			stream->lack_buf_cnt--;
	}
	if (stream->toisp_buf_state.is_early_update) {
		if (dev->rdbk_debug > 1 && stream->frame_idx < 15)
			v4l2_info(&dev->v4l2_dev,
				  "stream[%d] early update, seq %d\n",
				  stream->id, stream->frame_idx - 1);
		if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
		    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY)
			es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_CTRL,
						0x00010000);
		else
			es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_CTRL,
						0x00010000);
		if (active_buf) {
			active_buf->dbufs.sequence = stream->frame_idx - 1;
			active_buf->dbufs.timestamp =
				stream->readout.fs_timestamp;
			stream->last_frame_idx = stream->frame_idx;
			es_dvp2axi_s_rx_buffer(stream, &active_buf->dbufs);
		}
		if (dev->hw_dev->dummy_buf.vaddr)
			return;
		if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
		    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
		    mbus_cfg->type == V4L2_MBUS_CCP2) {
			frm_addr_y = frame_phase_next & DVP2AXI_CSI_FRAME0_READY ?
					     get_reg_index_of_frm0_y_addr(
						     stream->id) :
					     get_reg_index_of_frm1_y_addr(
						     stream->id);
		} else {
			frm_addr_y = frame_phase_next & DVP2AXI_CSI_FRAME0_READY ?
					     get_dvp_reg_index_of_frm0_y_addr(
						     stream->id) :
					     get_dvp_reg_index_of_frm1_y_addr(
						     stream->id);
		}
		if (frame_phase == DVP2AXI_CSI_FRAME0_READY)
			stream->next_buf_toisp = stream->curr_buf_toisp;
		else
			stream->curr_buf_toisp = stream->next_buf_toisp;
		buff_addr_y = stream->curr_buf_toisp->dummy.dma_addr;
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm_addr_y, 0, buff_addr_y, 0, false);
		} else {
			es_dvp2axi_write_register(dev, frm_addr_y, buff_addr_y);
		}
	}
	stream->toisp_buf_state.check_cnt++;
}

static void es_dvp2axi_assign_new_buffer_init(struct es_dvp2axi_stream *stream,
					 int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	u32 frm0_addr_y, frm0_addr_uv;
	u32 frm1_addr_y, frm1_addr_uv;
	u32 buff_addr_y, buff_addr_cbcr;
	unsigned long flags;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct csi_channel_info *channel = &dev->channels[channel_id];
	stream->lack_buf_cnt = 0;
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm0_addr_y = get_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_dvp_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);

	if (!stream->curr_buf) {
		if (!list_empty(&stream->buf_head)) {
			stream->curr_buf = list_first_entry(
				&stream->buf_head, struct es_dvp2axi_buffer, queue);
			v4l2_dbg(4, es_dvp2axi_debug, &dev->v4l2_dev,
				 "%s %d, stream[%d] buf idx %d\n", __func__,
				 __LINE__, stream->id,
				 stream->curr_buf->vb.vb2_buf.index);
			list_del(&stream->curr_buf->queue);
		}
	}

	if (stream->curr_buf) {
		buff_addr_y = stream->curr_buf->buff_addr[ES_DVP2AXI_PLANE_Y];
		buff_addr_cbcr = stream->curr_buf->buff_addr[ES_DVP2AXI_PLANE_CBCR];
		if (channel->capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm0_addr_y, frm0_addr_uv, buff_addr_y,
				buff_addr_cbcr, false);
		} else {
			es_dvp2axi_write_register(
				dev, frm0_addr_y,
				stream->curr_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(
					dev, frm0_addr_uv,
					stream->curr_buf
						->buff_addr[ES_DVP2AXI_PLANE_CBCR]);
		}
	} else {
		if (dummy_buf->vaddr) {
			buff_addr_y = dummy_buf->dma_addr;
			buff_addr_cbcr = dummy_buf->dma_addr;
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm0_addr_y, frm0_addr_uv,
					buff_addr_y, buff_addr_cbcr, true);
			} else {
				es_dvp2axi_write_register(dev, frm0_addr_y,
						     buff_addr_y);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(dev, frm0_addr_uv,
							     buff_addr_cbcr);
			}
		} else {
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
	}

	if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED) {
		stream->next_buf = stream->curr_buf;
		if (stream->next_buf) {
			buff_addr_y =
				stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y];
			buff_addr_cbcr =
				stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_CBCR];
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm1_addr_y, frm1_addr_uv,
					buff_addr_y, buff_addr_cbcr, false);
			} else {
				es_dvp2axi_write_register(
					dev, frm1_addr_y,
					buff_addr_y +
						(channel->virtual_width / 2));
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(
						dev, frm1_addr_uv,
						buff_addr_cbcr +
							(channel->virtual_width /
							 2));
			}
		}
	} else {
		if (!stream->next_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->next_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				v4l2_dbg(4, es_dvp2axi_debug, &dev->v4l2_dev,
					 "%s %d, stream[%d] buf idx %d\n",
					 __func__, __LINE__, stream->id,
					 stream->next_buf->vb.vb2_buf.index);
				list_del(&stream->next_buf->queue);
			} else if (stream->curr_buf) {
				stream->next_buf = stream->curr_buf;
			}
		}

		if (!stream->next_buf && dummy_buf->vaddr) {
			buff_addr_y = dummy_buf->dma_addr;
			buff_addr_cbcr = dummy_buf->dma_addr;
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm1_addr_y, frm1_addr_uv,
					buff_addr_y, buff_addr_cbcr, true);
			} else {
				es_dvp2axi_write_register(dev, frm1_addr_y,
						     dummy_buf->dma_addr);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(
						dev, frm1_addr_uv,
						dummy_buf->dma_addr);
			}

		} else if (!stream->next_buf && stream->curr_buf) {
			stream->next_buf = stream->curr_buf;
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
		if (stream->next_buf) {
			buff_addr_y =
				stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y];
			buff_addr_cbcr =
				stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_CBCR];
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm1_addr_y, frm1_addr_uv,
					buff_addr_y, buff_addr_cbcr, false);
			} else {
				es_dvp2axi_write_register(dev, frm1_addr_y,
						     buff_addr_y);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(dev, frm1_addr_uv,
							     buff_addr_cbcr);
			}
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	stream->is_dvp_yuv_addr_init = true;

	/* for BT.656/BT.1120 multi channels function,
	 * yuv addr of unused channel must be set
	 */
	if (mbus_cfg->type == V4L2_MBUS_BT656) {
		int ch_id;

		for (ch_id = 0; ch_id < ES_DVP2AXI_MAX_STREAM_DVP; ch_id++) {
			if (dev->stream[ch_id].is_dvp_yuv_addr_init)
				continue;
			if (dummy_buf->dma_addr) {
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm0_y_addr(ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm0_uv_addr(
						ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm1_y_addr(ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm1_uv_addr(
						ch_id),
					dummy_buf->dma_addr);
			}
		}
	}
	stream->buf_owner = ES_DVP2AXI_DMAEN_BY_VICAP;
}

static int es_dvp2axi_assign_new_buffer_update(struct es_dvp2axi_stream *stream,
					  int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct es_dvp2axi_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;
	struct csi_channel_info *channel = &dev->channels[channel_id];
	struct esisp_rx_buf *dbufs = NULL;
	struct dma_buf *dbuf = NULL;
	int ret = 0;
	u32 buff_addr_y, buff_addr_cbcr;
	unsigned long flags;

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				     get_reg_index_of_frm0_y_addr(channel_id) :
				     get_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_reg_index_of_frm0_uv_addr(channel_id) :
				get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm_addr_y =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_y_addr(channel_id) :
				get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_uv_addr(channel_id) :
				get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}

	if (stream->to_stop_dma && (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP)) {
		v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev, "%s %d\n", __func__,
			 __LINE__);
		goto stop_dma;
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (!list_empty(&stream->buf_head)) {
		if (!dummy_buf->vaddr && stream->curr_buf == stream->next_buf &&
		    stream->dvp2axi_fmt_in->field != V4L2_FIELD_INTERLACED)
			ret = -EINVAL;

		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
			if (!stream->curr_buf)
				ret = -EINVAL;
			stream->curr_buf = list_first_entry(
				&stream->buf_head, struct es_dvp2axi_buffer, queue);
			if (stream->curr_buf) {
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
				v4l2_dbg(
					3, es_dvp2axi_debug, &dev->v4l2_dev,
					"stream[%d] update curr_buf 0x%x, buf idx %d\n",
					stream->id, buffer->buff_addr[0],
					stream->curr_buf->vb.vb2_buf.index);
			}
		} else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			if (!stream->next_buf)
				ret = -EINVAL;
			if (stream->dvp2axi_fmt_in->field ==
			    V4L2_FIELD_INTERLACED) {
				if (stream->next_buf != stream->curr_buf) {
					stream->next_buf = stream->curr_buf;
					buffer = stream->next_buf;
				} else {
					buffer = NULL;
				}

			} else {
				stream->next_buf = list_first_entry(
					&stream->buf_head, struct es_dvp2axi_buffer,
					queue);
				if (stream->next_buf) {
					list_del(&stream->next_buf->queue);
					buffer = stream->next_buf;
					v4l2_dbg(
						3, es_dvp2axi_debug, &dev->v4l2_dev,
						"stream[%d] update next_buf 0x%x, buf idx %d\n",
						stream->id,
						buffer->buff_addr[0],
						stream->next_buf->vb.vb2_buf
							.index);
				}
			}
		}
	} else {
		buffer = NULL;
		if (!(stream->cur_stream_mode & ES_DVP2AXI_STREAM_MODE_TOISP) &&
		    dummy_buf->vaddr) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				stream->curr_buf = NULL;
			} else if (stream->frame_phase ==
				   DVP2AXI_CSI_FRAME1_READY) {
				if (stream->dvp2axi_fmt_in->field ==
				    V4L2_FIELD_INTERLACED) {
					stream->next_buf = stream->curr_buf;
					buffer = stream->next_buf;
				} else {
					stream->next_buf = NULL;
				}
			}
		} else if (stream->curr_buf != stream->next_buf) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY &&
			    stream->next_buf) {
				stream->curr_buf = stream->next_buf;
				buffer = stream->next_buf;
			} else if (stream->frame_phase ==
					   DVP2AXI_CSI_FRAME1_READY &&
				   stream->curr_buf) {
				stream->next_buf = stream->curr_buf;
				buffer = stream->curr_buf;
			}
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		} else {
			stream->curr_buf = NULL;
			stream->next_buf = NULL;
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
	}
	stream->frame_phase_cache = stream->frame_phase;

	if (buffer) {
		buff_addr_y = buffer->buff_addr[ES_DVP2AXI_PLANE_Y];
		buff_addr_cbcr = buffer->buff_addr[ES_DVP2AXI_PLANE_CBCR];
		if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED &&
		    stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm_addr_y, frm_addr_uv,
					buff_addr_y, buff_addr_cbcr, false);
			} else {
				es_dvp2axi_write_register(
					dev, frm_addr_y,
					buff_addr_y +
						(channel->virtual_width / 2));
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(
						dev, frm_addr_uv,
						buff_addr_cbcr +
							(channel->virtual_width /
							 2));
			}
		} else {
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm_addr_y, frm_addr_uv,
					buff_addr_y, buff_addr_cbcr, false);
			} else {
				es_dvp2axi_write_register(dev, frm_addr_y,
						     buff_addr_y);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(dev, frm_addr_uv,
							     buff_addr_cbcr);
			}
		}
		if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP) {
			if (stream->buf_replace_cnt < 2)
				stream->buf_replace_cnt++;
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY &&
			    stream->next_buf)
				dbuf = stream->next_buf->dbuf;
			else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY &&
				 stream->curr_buf)
				dbuf = stream->curr_buf->dbuf;

			if (dbuf) {
				list_for_each_entry(dbufs,
						    &stream->rx_buf_head_vicap,
						    list) {
					if (dbufs->dbuf == dbuf)
						break;
				}
			}
			if (dbufs)
				es_dvp2axi_s_rx_buffer(stream, dbufs);
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	return ret;
stop_dma:
	if (stream->buf_replace_cnt) {
		spin_lock_irqsave(&stream->vbq_lock, flags);
		buff_addr_y = stream->curr_buf_toisp->dummy.dma_addr;
		if (channel->capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE)
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm_addr_y, 0, buff_addr_y, 0, false);
		else
			es_dvp2axi_write_register(dev, frm_addr_y, buff_addr_y);
		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY &&
		    stream->next_buf)
			dbuf = stream->next_buf->dbuf;
		else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY &&
			 stream->curr_buf)
			dbuf = stream->curr_buf->dbuf;

		if (dbuf) {
			list_for_each_entry(dbufs, &stream->rx_buf_head_vicap,
					    list)
				if (dbufs->dbuf == dbuf)
					break;
		} else {
			dbufs = &stream->curr_buf_toisp->dbufs;
		}
		if (dbufs)
			es_dvp2axi_s_rx_buffer(stream, dbufs);

		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY &&
		    stream->curr_buf) {
			stream->curr_buf = NULL;
		} else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY &&
			   stream->next_buf) {
			stream->next_buf = NULL;
		}
		stream->buf_replace_cnt--;
		spin_unlock_irqrestore(&stream->vbq_lock, flags);
	}
	return -EINVAL;
}

static int es_dvp2axi_get_new_buffer_wake_up_mode(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (!list_empty(&stream->buf_head)) {
		if (!dummy_buf->vaddr && stream->curr_buf == stream->next_buf)
			ret = -EINVAL;
		if (stream->line_int_cnt % 2) {
			stream->curr_buf = list_first_entry(
				&stream->buf_head, struct es_dvp2axi_buffer, queue);
			if (stream->curr_buf)
				list_del(&stream->curr_buf->queue);
		} else {
			stream->next_buf = list_first_entry(
				&stream->buf_head, struct es_dvp2axi_buffer, queue);
			if (stream->next_buf)
				list_del(&stream->next_buf->queue);
		}
		stream->is_buf_active = true;
		if (stream->lack_buf_cnt)
			stream->lack_buf_cnt--;
	} else {
		stream->is_buf_active = false;
		if (dummy_buf->vaddr) {
			if (stream->line_int_cnt % 2)
				stream->curr_buf = NULL;
			else
				stream->next_buf = NULL;
		} else if (stream->curr_buf != stream->next_buf) {
			if (stream->line_int_cnt % 2) {
				stream->curr_buf = stream->next_buf;
				stream->frame_phase_cache =
					DVP2AXI_CSI_FRAME0_READY;
			} else {
				stream->next_buf = stream->curr_buf;
				stream->frame_phase_cache =
					DVP2AXI_CSI_FRAME1_READY;
			}
			stream->is_buf_active = true;
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		} else {
			ret = 0;
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	return ret;
}

static int es_dvp2axi_update_new_buffer_wake_up_mode(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct esmodule_capture_info *capture_info =
		&dev->channels[stream->id].capture_info;
	struct es_dvp2axi_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;
	u32 buff_addr_y, buff_addr_cbcr;
	int channel_id = stream->id;
	int ret = 0;
	unsigned long flags;

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				     get_reg_index_of_frm0_y_addr(channel_id) :
				     get_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_reg_index_of_frm0_uv_addr(channel_id) :
				get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm_addr_y =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_y_addr(channel_id) :
				get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_uv_addr(channel_id) :
				get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}
	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (stream->is_buf_active) {
		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY)
			buffer = stream->curr_buf;
		else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY)
			buffer = stream->next_buf;
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	if (buffer) {
		buff_addr_y = buffer->buff_addr[ES_DVP2AXI_PLANE_Y];
		buff_addr_cbcr = buffer->buff_addr[ES_DVP2AXI_PLANE_CBCR];
		if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
			es_dvp2axi_write_buff_addr_multi_dev_combine(
				stream, frm_addr_y, frm_addr_uv, buff_addr_y,
				buff_addr_cbcr, false);
		} else {
			es_dvp2axi_write_register(dev, frm_addr_y, buff_addr_y);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(dev, frm_addr_uv,
						     buff_addr_cbcr);
		}
	} else {
		if (dummy_buf->vaddr) {
			buff_addr_y = dummy_buf->dma_addr;
			buff_addr_cbcr = dummy_buf->dma_addr;
			if (capture_info->mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				es_dvp2axi_write_buff_addr_multi_dev_combine(
					stream, frm_addr_y, frm_addr_uv,
					buff_addr_y, buff_addr_cbcr, true);
			} else {
				es_dvp2axi_write_register(dev, frm_addr_y,
						     buff_addr_y);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(dev, frm_addr_uv,
							     buff_addr_cbcr);
			}
		} else {
			ret = 0;
		}
		dev->err_state |= (ES_DVP2AXI_ERR_ID0_NOT_BUF << stream->id);
		dev->irq_stats.not_active_buf_cnt[stream->id]++;
	}

	return ret;
}

static int es_dvp2axi_get_new_buffer_wake_up_mode_rdbk(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_rx_buffer *buffer = NULL;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	int ret = 0;
	unsigned long flags;
	u32 frm_addr_y;
	int frame_phase = 0;

	spin_lock_irqsave(&stream->vbq_lock, flags);
	memset(&stream->toisp_buf_state, 0, sizeof(stream->toisp_buf_state));
	if (!list_empty(&stream->rx_buf_head)) {
		if (stream->curr_buf_toisp && stream->next_buf_toisp &&
		    stream->curr_buf_toisp != stream->next_buf_toisp)
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_ROTATE;
		else
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_LOSS;
		if (stream->line_int_cnt % 2) {
			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->curr_buf_toisp = buffer;
			}
			frame_phase = DVP2AXI_CSI_FRAME0_READY;
		} else {
			buffer = list_first_entry(&stream->rx_buf_head,
						  struct es_dvp2axi_rx_buffer, list);
			if (buffer) {
				list_del(&buffer->list);
				stream->next_buf_toisp = buffer;
			}
			frame_phase = DVP2AXI_CSI_FRAME1_READY;
		}
		if (stream->lack_buf_cnt)
			stream->lack_buf_cnt--;
	} else {
		if (stream->lack_buf_cnt < 2)
			stream->lack_buf_cnt++;
		if (stream->curr_buf_toisp && stream->next_buf_toisp &&
		    stream->curr_buf_toisp != stream->next_buf_toisp) {
			if (stream->line_int_cnt % 2)
				stream->curr_buf_toisp = stream->next_buf_toisp;
			else
				stream->next_buf_toisp = stream->curr_buf_toisp;
			buffer = stream->curr_buf_toisp;
			ret = 0;
			if (stream->dvp2axidev->rdbk_debug)
				v4l2_info(&stream->dvp2axidev->v4l2_dev,
					  "stream[%d] hold buf %x\n",
					  stream->id,
					  (u32)stream->next_buf_toisp->dummy
						  .dma_addr);
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_THESAME;
		} else {
			ret = -EINVAL;
			stream->toisp_buf_state.state = ES_DVP2AXI_TOISP_BUF_LOSS;
		}
	}
	if (buffer) {
		if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
		    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
		    mbus_cfg->type == V4L2_MBUS_CCP2) {
			frm_addr_y = frame_phase & DVP2AXI_CSI_FRAME0_READY ?
					     get_reg_index_of_frm0_y_addr(
						     stream->id) :
					     get_reg_index_of_frm1_y_addr(
						     stream->id);
		} else {
			frm_addr_y = frame_phase & DVP2AXI_CSI_FRAME0_READY ?
					     get_dvp_reg_index_of_frm0_y_addr(
						     stream->id) :
					     get_dvp_reg_index_of_frm1_y_addr(
						     stream->id);
		}
		es_dvp2axi_write_register(dev, frm_addr_y, buffer->dummy.dma_addr);
		if (dev->rdbk_debug > 1 && stream->frame_idx < 15)
			v4l2_info(
				&dev->v4l2_dev,
				"stream[%d] rdbk update, seq %d, reg %x, buf %x\n",
				stream->id, stream->frame_idx - 1, frm_addr_y,
				(u32)buffer->dummy.dma_addr);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	return ret;
}

#if 0
static void es_dvp2axi_assign_dummy_buffer(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	unsigned long flags;

	spin_lock_irqsave(&stream->vbq_lock, flags);

	/* for BT.656/BT.1120 multi channels function,
	 * yuv addr of unused channel must be set
	 */
	if (mbus_cfg->type == V4L2_MBUS_BT656 && dummy_buf->vaddr) {
		es_dvp2axi_write_register(
			dev, get_dvp_reg_index_of_frm0_y_addr(stream->id),
			dummy_buf->dma_addr);
		es_dvp2axi_write_register(
			dev, get_dvp_reg_index_of_frm0_uv_addr(stream->id),
			dummy_buf->dma_addr);
		es_dvp2axi_write_register(
			dev, get_dvp_reg_index_of_frm1_y_addr(stream->id),
			dummy_buf->dma_addr);
		es_dvp2axi_write_register(
			dev, get_dvp_reg_index_of_frm1_uv_addr(stream->id),
			dummy_buf->dma_addr);
	}

	spin_unlock_irqrestore(&stream->vbq_lock, flags);
}
#endif

static int es_dvp2axi_assign_new_buffer_pingpong(struct es_dvp2axi_stream *stream,
					    int init, int channel_id)
{
	int ret = 0;

	if (init)
		es_dvp2axi_assign_new_buffer_init(stream, channel_id);
	else
		ret = es_dvp2axi_assign_new_buffer_update(stream, channel_id);
	return ret;
}

static void es_dvp2axi_assign_new_buffer_init_eskit(struct es_dvp2axi_stream *stream,
						int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	u32 frm0_addr_y, frm0_addr_uv;
	u32 frm1_addr_y, frm1_addr_uv;
	unsigned long flags;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct csi_channel_info *channel = &dev->channels[channel_id];

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm0_addr_y = get_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_dvp_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);

	if (!stream->curr_buf_eskit) {
		if (!list_empty(&stream->eskit_buf_head)) {
			stream->curr_buf_eskit =
				list_first_entry(&stream->eskit_buf_head,
						 struct es_dvp2axi_buffer, queue);
			list_del(&stream->curr_buf_eskit->queue);
		}
	}

	if (stream->curr_buf_eskit) {
		es_dvp2axi_write_register(
			dev, frm0_addr_y,
			stream->curr_buf_eskit->buff_addr[ES_DVP2AXI_PLANE_Y]);
		if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
			es_dvp2axi_write_register(
				dev, frm0_addr_uv,
				stream->curr_buf_eskit
					->buff_addr[ES_DVP2AXI_PLANE_CBCR]);
	} else {
		if (dummy_buf->vaddr) {
			es_dvp2axi_write_register(dev, frm0_addr_y,
					     dummy_buf->dma_addr);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(dev, frm0_addr_uv,
						     dummy_buf->dma_addr);
		} else {
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
	}

	if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED) {
		stream->next_buf_eskit = stream->curr_buf_eskit;
		if (stream->next_buf_eskit) {
			es_dvp2axi_write_register(
				dev, frm1_addr_y,
				stream->next_buf_eskit
						->buff_addr[ES_DVP2AXI_PLANE_Y] +
					(channel->virtual_width / 2));
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(
					dev, frm1_addr_uv,
					stream->next_buf_eskit->buff_addr
							[ES_DVP2AXI_PLANE_CBCR] +
						(channel->virtual_width / 2));
		}
	} else {
		if (!stream->next_buf_eskit) {
			if (!list_empty(&stream->eskit_buf_head)) {
				stream->next_buf_eskit = list_first_entry(
					&stream->eskit_buf_head,
					struct es_dvp2axi_buffer, queue);
				list_del(&stream->next_buf_eskit->queue);
			}
		}

		if (stream->next_buf_eskit) {
			es_dvp2axi_write_register(
				dev, frm1_addr_y,
				stream->next_buf_eskit
					->buff_addr[ES_DVP2AXI_PLANE_Y]);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(
					dev, frm1_addr_uv,
					stream->next_buf_eskit
						->buff_addr[ES_DVP2AXI_PLANE_CBCR]);
		} else {
			if (dummy_buf->vaddr) {
				es_dvp2axi_write_register(dev, frm1_addr_y,
						     dummy_buf->dma_addr);
				if (stream->dvp2axi_fmt_out->fmt_type !=
				    DVP2AXI_FMT_TYPE_RAW)
					es_dvp2axi_write_register(
						dev, frm1_addr_uv,
						dummy_buf->dma_addr);
			} else {
				if (stream->curr_buf_eskit) {
					stream->next_buf_eskit =
						stream->curr_buf_eskit;
					es_dvp2axi_write_register(
						dev, frm1_addr_y,
						stream->next_buf_eskit->buff_addr
							[ES_DVP2AXI_PLANE_Y]);
					if (stream->dvp2axi_fmt_out->fmt_type !=
					    DVP2AXI_FMT_TYPE_RAW)
						es_dvp2axi_write_register(
							dev, frm1_addr_uv,
							stream->next_buf_eskit->buff_addr
								[ES_DVP2AXI_PLANE_CBCR]);
				}
				if (stream->lack_buf_cnt < 2)
					stream->lack_buf_cnt++;
			}
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	stream->is_dvp_yuv_addr_init = true;

	/* for BT.656/BT.1120 multi channels function,
	 * yuv addr of unused channel must be set
	 */
	if (mbus_cfg->type == V4L2_MBUS_BT656) {
		int ch_id;

		for (ch_id = 0; ch_id < ES_DVP2AXI_MAX_STREAM_DVP; ch_id++) {
			if (dev->stream[ch_id].is_dvp_yuv_addr_init)
				continue;
			if (dummy_buf->dma_addr) {
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm0_y_addr(ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm0_uv_addr(
						ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm1_y_addr(ch_id),
					dummy_buf->dma_addr);
				es_dvp2axi_write_register(
					dev,
					get_dvp_reg_index_of_frm1_uv_addr(
						ch_id),
					dummy_buf->dma_addr);
			}
		}
	}
	stream->buf_owner = ES_DVP2AXI_DMAEN_BY_ESKIT;
}

static int es_dvp2axi_assign_new_buffer_update_eskit(struct es_dvp2axi_stream *stream,
						 int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct es_dvp2axi_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;
	struct csi_channel_info *channel = &dev->channels[channel_id];
	int ret = 0;
	unsigned long flags;

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				     get_reg_index_of_frm0_y_addr(channel_id) :
				     get_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_reg_index_of_frm0_uv_addr(channel_id) :
				get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm_addr_y =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_y_addr(channel_id) :
				get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv =
			stream->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
				get_dvp_reg_index_of_frm0_uv_addr(channel_id) :
				get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (!list_empty(&stream->eskit_buf_head)) {
		if (!dummy_buf->vaddr &&
		    stream->curr_buf_eskit == stream->next_buf_eskit &&
		    stream->dvp2axi_fmt_in->field != V4L2_FIELD_INTERLACED)
			ret = -EINVAL;

		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
			if (!stream->curr_buf_eskit)
				ret = -EINVAL;
			stream->curr_buf_eskit =
				list_first_entry(&stream->eskit_buf_head,
						 struct es_dvp2axi_buffer, queue);
			if (stream->curr_buf_eskit) {
				list_del(&stream->curr_buf_eskit->queue);
				buffer = stream->curr_buf_eskit;
			}
		} else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			if (!stream->next_buf_eskit)
				ret = -EINVAL;
			if (stream->dvp2axi_fmt_in->field ==
			    V4L2_FIELD_INTERLACED) {
				if (stream->next_buf_eskit !=
				    stream->curr_buf_eskit) {
					stream->next_buf_eskit =
						stream->curr_buf_eskit;
					buffer = stream->next_buf_eskit;
				} else {
					buffer = NULL;
				}

			} else {
				stream->next_buf_eskit = list_first_entry(
					&stream->eskit_buf_head,
					struct es_dvp2axi_buffer, queue);
				if (stream->next_buf_eskit) {
					list_del(
						&stream->next_buf_eskit->queue);
					buffer = stream->next_buf_eskit;
				}
			}
		}
	} else {	
		buffer = NULL;
		if (dummy_buf->vaddr) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				stream->curr_buf_eskit = NULL;
			} else if (stream->frame_phase ==
				   DVP2AXI_CSI_FRAME1_READY) {
				if (stream->dvp2axi_fmt_in->field ==
				    V4L2_FIELD_INTERLACED) {
					stream->next_buf_eskit =
						stream->curr_buf_eskit;
					buffer = stream->next_buf_eskit;
				} else {
					stream->next_buf_eskit = NULL;
				}
			}
		} else if (stream->curr_buf_eskit && stream->next_buf_eskit &&
			   stream->curr_buf_eskit != stream->next_buf_eskit) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				stream->curr_buf_eskit =
					stream->next_buf_eskit;
				buffer = stream->next_buf_eskit;
			} else if (stream->frame_phase ==
				   DVP2AXI_CSI_FRAME1_READY) {
				stream->next_buf_eskit =
					stream->curr_buf_eskit;
				buffer = stream->curr_buf_eskit;
			}
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		} else {
			if (stream->lack_buf_cnt < 2)
				stream->lack_buf_cnt++;
		}
	}
	stream->frame_phase_cache = stream->frame_phase;

	if (buffer) {
		if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED &&
		    stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			es_dvp2axi_write_register(
				dev, frm_addr_y,
				buffer->buff_addr[ES_DVP2AXI_PLANE_Y] +
					(channel->virtual_width / 2));
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(
					dev, frm_addr_uv,
					buffer->buff_addr[ES_DVP2AXI_PLANE_CBCR] +
						(channel->virtual_width / 2));
		} else {
			es_dvp2axi_write_register(dev, frm_addr_y,
					     buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(
					dev, frm_addr_uv,
					buffer->buff_addr[ES_DVP2AXI_PLANE_CBCR]);
		}
	} else {
		if (dummy_buf->vaddr) {
			es_dvp2axi_write_register(dev, frm_addr_y,
					     dummy_buf->dma_addr);
			if (stream->dvp2axi_fmt_out->fmt_type != DVP2AXI_FMT_TYPE_RAW)
				es_dvp2axi_write_register(dev, frm_addr_uv,
						     dummy_buf->dma_addr);
			dev->err_state |= (ES_DVP2AXI_ERR_ID0_NOT_BUF << stream->id);
			dev->irq_stats.not_active_buf_cnt[stream->id]++;
		} else {
			ret = -EINVAL;
			dev->err_state |= (ES_DVP2AXI_ERR_ID0_NOT_BUF << stream->id);
			dev->irq_stats.not_active_buf_cnt[stream->id]++;
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	return ret;
}

static int es_dvp2axi_assign_new_buffer_pingpong_eskit(struct es_dvp2axi_stream *stream,
						   int init, int channel_id)
{
	int ret = 0;

	if (init)
		es_dvp2axi_assign_new_buffer_init_eskit(stream, channel_id);
	else
		ret = es_dvp2axi_assign_new_buffer_update_eskit(stream, channel_id);
	return ret;
}

static void es_dvp2axi_csi_set_lvds_sav_eav(struct es_dvp2axi_stream *stream,
				       struct csi_channel_info *channel)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct esmodule_lvds_cfg *lvds_cfg = &channel->lvds_cfg;
	struct esmodule_lvds_frame_sync_code *frm_sync_code = NULL;
	struct esmodule_lvds_frm_sync_code *odd_sync_code = NULL;
	struct esmodule_lvds_frm_sync_code *even_sync_code = NULL;

	if (dev->hdr.hdr_mode == NO_HDR || dev->hdr.hdr_mode == HDR_COMPR) {
		frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LINEAR];
		odd_sync_code = &frm_sync_code->odd_sync_code;
		even_sync_code = odd_sync_code;
	} else {
		if (channel->id == ES_DVP2AXI_STREAM_MIPI_ID0)
			frm_sync_code =
				&lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LONG];

		if (dev->hdr.hdr_mode == HDR_X2) {
			if (channel->id == ES_DVP2AXI_STREAM_MIPI_ID1)
				frm_sync_code = &lvds_cfg->frm_sync_code
							 [LVDS_CODE_GRP_SHORT];
			else
				frm_sync_code = &lvds_cfg->frm_sync_code
							 [LVDS_CODE_GRP_LONG];
		} else if (dev->hdr.hdr_mode == HDR_X3) {
			if (channel->id == ES_DVP2AXI_STREAM_MIPI_ID1)
				frm_sync_code = &lvds_cfg->frm_sync_code
							 [LVDS_CODE_GRP_MEDIUM];
			else if (channel->id == ES_DVP2AXI_STREAM_MIPI_ID2)
				frm_sync_code = &lvds_cfg->frm_sync_code
							 [LVDS_CODE_GRP_SHORT];
			else
				frm_sync_code = &lvds_cfg->frm_sync_code
							 [LVDS_CODE_GRP_LONG];
		}

		odd_sync_code = &frm_sync_code->odd_sync_code;
		even_sync_code = &frm_sync_code->even_sync_code;
	}

	if (odd_sync_code && even_sync_code) {
		es_dvp2axi_write_register(
			stream->dvp2axidev,
			get_reg_index_of_lvds_sav_eav_act0(channel->id),
			SW_LVDS_EAV_ACT(odd_sync_code->act.eav) |
				SW_LVDS_SAV_ACT(odd_sync_code->act.sav));

		es_dvp2axi_write_register(
			stream->dvp2axidev,
			get_reg_index_of_lvds_sav_eav_blk0(channel->id),
			SW_LVDS_EAV_BLK(odd_sync_code->blk.eav) |
				SW_LVDS_SAV_BLK(odd_sync_code->blk.sav));

		es_dvp2axi_write_register(
			stream->dvp2axidev,
			get_reg_index_of_lvds_sav_eav_act1(channel->id),
			SW_LVDS_EAV_ACT(even_sync_code->act.eav) |
				SW_LVDS_SAV_ACT(even_sync_code->act.sav));

		es_dvp2axi_write_register(
			stream->dvp2axidev,
			get_reg_index_of_lvds_sav_eav_blk1(channel->id),
			SW_LVDS_EAV_BLK(even_sync_code->blk.eav) |
				SW_LVDS_SAV_BLK(even_sync_code->blk.sav));
	}
}

static unsigned char get_csi_fmt_val(const struct dvp2axi_input_fmt *dvp2axi_fmt_in,
				     struct csi_channel_info *csi_info)
{
	unsigned char csi_fmt_val = 0;

	if (dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8 ||
	    dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8) {
		switch (csi_info->data_bit) {
		case 8:
			csi_fmt_val = CSI_WRDDR_TYPE_RAW8;
			break;
		case 10:
			csi_fmt_val = CSI_WRDDR_TYPE_RAW10;
			break;
		case 12:
			csi_fmt_val = CSI_WRDDR_TYPE_RAW12;
			break;
		default:
			csi_fmt_val = CSI_WRDDR_TYPE_RAW12;
			break;
		}
	} else if (dvp2axi_fmt_in->csi_fmt_val == CSI_WRDDR_TYPE_RGB888 ||
		   dvp2axi_fmt_in->csi_fmt_val == CSI_WRDDR_TYPE_RGB565) {
		csi_fmt_val = CSI_WRDDR_TYPE_RAW8;
	} else {
		csi_fmt_val = dvp2axi_fmt_in->csi_fmt_val;
	}
	return csi_fmt_val;
}

static int es_dvp2axi_csi_channel_init(struct es_dvp2axi_stream *stream,
				  struct csi_channel_info *channel)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct sditf_priv *priv = dev->sditf[0];
	const struct dvp2axi_output_fmt *fmt;
	u32 fourcc;
	int vc = dev->channels[stream->id].vc;

	channel->enable = 1;
	channel->width = stream->pixm.width;
	channel->height = stream->pixm.height;

	channel->fmt_val = stream->dvp2axi_fmt_out->csi_fmt_val;

	channel->cmd_mode_en = 0; /* default use DSI Video Mode */
	channel->dsi_input = dev->terminal_sensor.dsi_input_en;

	if (stream->crop_enable) {
		channel->crop_en = 1;

		if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
			channel->crop_st_x =
				3 * stream->crop[CROP_SRC_ACT].left;
		else if (channel->fmt_val == CSI_WRDDR_TYPE_RGB565)
			channel->crop_st_x =
				2 * stream->crop[CROP_SRC_ACT].left;
		else
			channel->crop_st_x = stream->crop[CROP_SRC_ACT].left;

		channel->crop_st_y = stream->crop[CROP_SRC_ACT].top;
		if (priv && priv->is_combine_mode &&
		    dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF)
			channel->crop_st_y *= dev->sditf_cnt;
		channel->width = stream->crop[CROP_SRC_ACT].width;
		channel->height = stream->crop[CROP_SRC_ACT].height;
	} else {
		channel->width = stream->pixm.width;
		channel->height = stream->pixm.height;
		channel->crop_en = 0;
	}

	if (priv && priv->is_combine_mode && dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF)
		channel->height *= dev->sditf_cnt;

	fmt = es_dvp2axi_find_output_fmt(stream, stream->pixm.pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev, "can not find output format: 0x%x",
			 stream->pixm.pixelformat);
		return -EINVAL;
	}

	if (channel->capture_info.mode == ESMODULE_MULTI_DEV_COMBINE_ONE)
		channel->width /= channel->capture_info.multi_dev.dev_num;
	/*
	 * for mipi or lvds, when enable compact, the virtual width of raw10/raw12
	 * needs aligned with :ALIGN(bits_per_pixel * width / 8, 8), if enable 16bit mode
	 * needs aligned with :ALIGN(bits_per_pixel * width * 2, 8), to optimize reading and
	 * writing of ddr, aliged with 256
	 */
	if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW && stream->is_compact &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565) {
		if (channel->capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE) {
			channel->virtual_width = ALIGN(
				channel->width * 2 * fmt->raw_bpp / 8, 256);
			channel->left_virtual_width =
				channel->width * fmt->raw_bpp / 8;
		} else {
			channel->virtual_width =
				ALIGN(channel->width * fmt->raw_bpp / 8, 256);
		}
	} else {
		if (channel->capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE) {
			channel->virtual_width =
				ALIGN(channel->width * 2 * fmt->bpp[0] / 8, 8);
			channel->left_virtual_width =
				ALIGN(channel->width * fmt->bpp[0] / 8, 8);
		} else {
			channel->virtual_width =
				ALIGN(channel->width * fmt->bpp[0] / 8, 8);
		}
	}

	if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888 ||
	    channel->fmt_val == CSI_WRDDR_TYPE_RGB565)
		channel->width = channel->width * fmt->bpp[0] / 8;
	/*
	 * dvp2axi don't support output yuyv fmt data
	 * if user request yuyv fmt, the input mode must be RAW8
	 * and the width is double Because the real input fmt is
	 * yuyv
	 */
	fourcc = stream->dvp2axi_fmt_out->fourcc;
	if (fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_YVYU ||
	    fourcc == V4L2_PIX_FMT_UYVY || fourcc == V4L2_PIX_FMT_VYUY) {
		channel->virtual_width *= 2;
		if (channel->capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE)
			channel->left_virtual_width *= 2;
	}
	if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED) {
		channel->virtual_width *= 2;
		channel->height /= 2;
	}
	if (stream->dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8 ||
	    stream->dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8) {
		if (dev->channels[stream->id].data_type)
			channel->data_type =
				dev->channels[stream->id].data_type;
		else
			channel->data_type = get_data_type(
				stream->dvp2axi_fmt_in->mbus_code,
				channel->cmd_mode_en, channel->dsi_input);
	} else {
		channel->data_type =
			get_data_type(stream->dvp2axi_fmt_in->mbus_code,
				      channel->cmd_mode_en, channel->dsi_input);
	}
	channel->csi_fmt_val =
		get_csi_fmt_val(stream->dvp2axi_fmt_in, &dev->channels[stream->id]);

	if (dev->hdr.hdr_mode == NO_HDR || dev->hdr.hdr_mode == HDR_COMPR ||
	    (dev->hdr.hdr_mode == HDR_X2 && stream->id > 1) ||
	    (dev->hdr.hdr_mode == HDR_X3 && stream->id > 2))
		channel->vc = vc < 4 ? vc : channel->id;
	else
		channel->vc = channel->id;
	v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
		 "%s: channel width %d, height %d, virtual_width %d, vc %d\n",
		 __func__, channel->width, channel->height,
		 channel->virtual_width, channel->vc);
	return 0;
}

static int es_dvp2axi_csi_channel_set(struct es_dvp2axi_stream *stream,
				 struct csi_channel_info *channel,
				 enum v4l2_mbus_type mbus_type)
{
	unsigned int val = 0x0;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_stream *detect_stream = &dev->stream[0];
	unsigned int wait_line = 0x3fff;

	if (channel->id >= 4)
		return -EINVAL;

	if (!channel->enable) {
		es_dvp2axi_write_register(dev,
				     get_reg_index_of_id_ctrl0(channel->id),
				     CSI_DISABLE_CAPTURE);
		return 0;
	}

	es_dvp2axi_write_register_and(dev, DVP2AXI_REG_MIPI_LVDS_INTSTAT,
				 ~(CSI_START_INTSTAT(channel->id) |
				   CSI_DMA_END_INTSTAT(channel->id) |
				   CSI_LINE_INTSTAT(channel->id)));

	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
				CSI_START_INTEN(channel->id));

	if (detect_stream->is_line_wake_up) {
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					CSI_LINE_INTEN(channel->id));
		wait_line = dev->wait_line;
	}
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1,
			     wait_line << 16 | wait_line);
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID2_3,
			     wait_line << 16 | wait_line);

	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
				CSI_DMA_END_INTEN(channel->id));
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_WATER_LINE,
			     DVP2AXI_MIPI_LVDS_SW_WATER_LINE_25_ES1808 |
				     DVP2AXI_MIPI_LVDS_SW_WATER_LINE_ENABLE_ES1808 |
				     DVP2AXI_MIPI_LVDS_SW_HURRY_VALUE_ES1808(0x3) |
				     DVP2AXI_MIPI_LVDS_SW_HURRY_ENABLE_ES1808);

	val = DVP2AXI_MIPI_LVDS_SW_PRESS_VALUE(0x3) |
	      DVP2AXI_MIPI_LVDS_SW_PRESS_ENABLE |
	      DVP2AXI_MIPI_LVDS_SW_HURRY_VALUE(0x3) |
	      DVP2AXI_MIPI_LVDS_SW_HURRY_ENABLE | DVP2AXI_MIPI_LVDS_SW_WATER_LINE_25 |
	      DVP2AXI_MIPI_LVDS_SW_WATER_LINE_ENABLE;
	if (mbus_type == V4L2_MBUS_CSI2_DPHY) {
		val &= ~DVP2AXI_MIPI_LVDS_SW_SEL_LVDS;
	} else if (mbus_type == V4L2_MBUS_CCP2) {
		if (channel->fmt_val == CSI_WRDDR_TYPE_RAW12)
			val |= DVP2AXI_MIPI_LVDS_SW_LVDS_WIDTH_12BITS;
		else if (channel->fmt_val == CSI_WRDDR_TYPE_RAW10)
			val |= DVP2AXI_MIPI_LVDS_SW_LVDS_WIDTH_10BITS;
		else
			val |= DVP2AXI_MIPI_LVDS_SW_LVDS_WIDTH_8BITS;
		val |= DVP2AXI_MIPI_LVDS_SW_SEL_LVDS;
	}
	es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL, val);

	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
				CSI_ALL_ERROR_INTEN);

	es_dvp2axi_write_register(dev, get_reg_index_of_id_ctrl1(channel->id),
			     channel->width | (channel->height << 16));

	es_dvp2axi_write_register(dev, get_reg_index_of_frm0_y_vlw(channel->id),
			     channel->virtual_width);
	es_dvp2axi_write_register(dev, get_reg_index_of_frm1_y_vlw(channel->id),
			     channel->virtual_width);
	es_dvp2axi_write_register(dev, get_reg_index_of_frm0_uv_vlw(channel->id),
			     channel->virtual_width);
	es_dvp2axi_write_register(dev, get_reg_index_of_frm1_uv_vlw(channel->id),
			     channel->virtual_width);

	if (channel->crop_en)
		es_dvp2axi_write_register(
			dev, get_reg_index_of_id_crop_start(channel->id),
			channel->crop_st_y << 16 | channel->crop_st_x);

	/* Set up an buffer for the next frame */
	es_dvp2axi_assign_new_buffer_pingpong(stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT,
					 channel->id);

	if (mbus_type == V4L2_MBUS_CSI2_DPHY) {
		//need always enable crop
		val = CSI_ENABLE_CAPTURE | channel->fmt_val |
		      channel->cmd_mode_en << 4 | CSI_ENABLE_CROP |
		      channel->vc << 8 | channel->data_type << 10;
		if (stream->is_compact)
			val |= CSI_ENABLE_MIPI_COMPACT;
		else
			val &= ~CSI_ENABLE_MIPI_COMPACT;
	} else if (mbus_type == V4L2_MBUS_CCP2) {
		es_dvp2axi_csi_set_lvds_sav_eav(stream, channel);
		val = LVDS_ENABLE_CAPTURE | LVDS_MODE(channel->lvds_cfg.mode) |
		      LVDS_MAIN_LANE(0) | LVDS_FID(0) |
		      LVDS_LANES_ENABLED(dev->active_sensor->lanes);

		if (stream->is_compact)
			val |= LVDS_COMPACT;
		else
			val &= ~LVDS_COMPACT;
	}
	if (stream->is_high_align)
		val |= CSI_HIGH_ALIGN;
	else
		val &= ~CSI_HIGH_ALIGN;
	es_dvp2axi_write_register(dev, get_reg_index_of_id_ctrl0(channel->id), val);

	dev->intr_mask = es_dvp2axi_read_register(dev, DVP2AXI_REG_MIPI_LVDS_INTEN);
	return 0;
}

static int es_dvp2axi_dvp_get_input_yuv_order(struct es_dvp2axi_stream *stream)
{
	unsigned int mask;
	const struct dvp2axi_input_fmt *fmt = stream->dvp2axi_fmt_in;

	switch (fmt->mbus_code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		mask = CSI_YUV_INPUT_ORDER_UYVY >> 11;
		break;
	case MEDIA_BUS_FMT_VYUY8_2X8:
		mask = CSI_YUV_INPUT_ORDER_VYUY >> 11;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		mask = CSI_YUV_INPUT_ORDER_YUYV >> 11;
		break;
	case MEDIA_BUS_FMT_YVYU8_2X8:
		mask = CSI_YUV_INPUT_ORDER_YVYU >> 11;
		break;
	default:
		mask = CSI_YUV_INPUT_ORDER_UYVY >> 11;
		break;
	}
	return mask;
}

static int es_dvp2axi_csi_get_output_type_mask(struct es_dvp2axi_stream *stream)
{
	unsigned int mask;
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_NV16:
		mask = CSI_WRDDR_TYPE_YUV422SP_EIC770X |
		       CSI_YUV_OUTPUT_ORDER_UYVY;
		break;
	case V4L2_PIX_FMT_NV61:
		mask = CSI_WRDDR_TYPE_YUV422SP_EIC770X |
		       CSI_YUV_OUTPUT_ORDER_VYUY;
		break;
	case V4L2_PIX_FMT_NV12:
		mask = CSI_WRDDR_TYPE_YUV420SP_EIC770X |
		       CSI_YUV_OUTPUT_ORDER_UYVY;
		break;
	case V4L2_PIX_FMT_NV21:
		mask = CSI_WRDDR_TYPE_YUV420SP_EIC770X |
		       CSI_YUV_OUTPUT_ORDER_VYUY;
		break;
	case V4L2_PIX_FMT_YUYV:
		mask = CSI_WRDDR_TYPE_YUV_PACKET | CSI_YUV_OUTPUT_ORDER_YUYV;
		break;
	case V4L2_PIX_FMT_YVYU:
		mask = CSI_WRDDR_TYPE_YUV_PACKET | CSI_YUV_OUTPUT_ORDER_YVYU;
		break;
	case V4L2_PIX_FMT_UYVY:
		mask = CSI_WRDDR_TYPE_YUV_PACKET | CSI_YUV_OUTPUT_ORDER_UYVY;
		break;
	case V4L2_PIX_FMT_VYUY:
		mask = CSI_WRDDR_TYPE_YUV_PACKET | CSI_YUV_OUTPUT_ORDER_VYUY;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_BGR666:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT;
		break;
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
		if (stream->is_compact)
			mask = CSI_WRDDR_TYPE_RAW_COMPACT;
		else
			mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT;
		break;
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
	case V4L2_PIX_FMT_Y16:
		mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT;
		break;
	default:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT;
		break;
	}
	return mask;
}

#if 0
static int es_dvp2axi_lvds_get_output_type_mask(struct es_dvp2axi_stream *stream)
{
	unsigned int mask;
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;
	int wr_type_offset = 0;
	int yuvout_offset = 0;

	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_NV16:
		mask = (CSI_WRDDR_TYPE_YUV422SP_EIC770X << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_NV61:
		mask = (CSI_WRDDR_TYPE_YUV422SP_EIC770X << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_NV12:
		mask = (CSI_WRDDR_TYPE_YUV420SP_EIC770X << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_NV21:
		mask = (CSI_WRDDR_TYPE_YUV420SP_EIC770X << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_YUYV:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_YUYV << yuvout_offset);
		break;
	case V4L2_PIX_FMT_YVYU:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_YVYU << yuvout_offset);
		break;
	case V4L2_PIX_FMT_UYVY:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_VYUY:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << wr_type_offset) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << yuvout_offset);
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_BGR666:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT << wr_type_offset;
		break;
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
		if (stream->is_compact)
			mask = CSI_WRDDR_TYPE_RAW_COMPACT << wr_type_offset;
		else
			mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT << wr_type_offset;
		break;
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
	case V4L2_PIX_FMT_Y16:
		mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT << wr_type_offset;
		break;
	default:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT << wr_type_offset;
		break;
	}
	return mask;
}
#endif

#if 0
static void es_dvp2axi_modify_frame_skip_config(struct es_dvp2axi_stream *stream)
{
	if (stream->skip_info.skip_to_en) {
		es_dvp2axi_disable_skip_frame(stream);
		es_dvp2axi_enable_skip_frame(stream, stream->skip_info.cap_m,
					stream->skip_info.skip_n);
		stream->skip_info.skip_to_en = false;
	} else if (stream->skip_info.skip_to_dis) {
		es_dvp2axi_disable_skip_frame(stream);
	}
}
#endif

/*config reg for eic770x*/
static int es_dvp2axi_csi_channel_set_v1(struct es_dvp2axi_stream *stream,
				    struct csi_channel_info *channel,
				    enum v4l2_mbus_type mbus_type,
				    unsigned int mode, int index)
{
	unsigned int val = 0x0;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_stream *detect_stream = &dev->stream[0];
	struct sditf_priv *priv = dev->sditf[0];
	struct esmodule_capture_info *capture_info = &channel->capture_info;
	unsigned int wait_line = 0x3fff;
	unsigned int dma_en = 0;
	int offset = 0;

	// pr_info("%s:%d yfx !!!!! \n ", __func__, __LINE__);
	if (channel->id >= 4)
		return -EINVAL;

	if (!channel->enable) {
		es_dvp2axi_write_register(dev,
				     get_reg_index_of_id_ctrl0(channel->id),
				     CSI_DISABLE_CAPTURE);
		return 0;
	}

	es_dvp2axi_write_register_and(dev, DVP2AXI_REG_MIPI_LVDS_INTSTAT,
				 ~(CSI_START_INTSTAT(channel->id) |
				   CSI_DMA_END_INTSTAT(channel->id) |
				   CSI_LINE_INTSTAT_V1(channel->id)));

	if (!(capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE &&
	      index < capture_info->multi_dev.dev_num - 1)) {
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					CSI_START_INTEN(channel->id));

		if (priv && priv->mode.rdbk_mode &&
		    detect_stream->is_line_wake_up) {
			es_dvp2axi_write_register_or(
				dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
				CSI_LINE_INTEN_EIC770X(channel->id));
			wait_line = dev->wait_line;
		}
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1,
				     wait_line << 16 | wait_line);
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID2_3,
				     wait_line << 16 | wait_line);

		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					CSI_DMA_END_INTEN(channel->id));

		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					CSI_ALL_ERROR_INTEN_V1);
	}
	if (stream->dvp2axidev->id_use_cnt == 0) {
		val = DVP2AXI_MIPI_LVDS_SW_PRESS_VALUE_EIC770X(0x3) |
		      DVP2AXI_MIPI_LVDS_SW_PRESS_ENABLE |
		      DVP2AXI_MIPI_LVDS_SW_HURRY_VALUE_EIC770X(0x3) |
		      DVP2AXI_MIPI_LVDS_SW_HURRY_ENABLE |
		      DVP2AXI_MIPI_LVDS_SW_WATER_LINE_25 |
		      DVP2AXI_MIPI_LVDS_SW_WATER_LINE_ENABLE;
		if (mbus_type == V4L2_MBUS_CSI2_DPHY ||
		    mbus_type == V4L2_MBUS_CSI2_CPHY)
			val &= ~DVP2AXI_MIPI_LVDS_SW_SEL_LVDS_RV1106;
		else
			val |= DVP2AXI_MIPI_LVDS_SW_SEL_LVDS_RV1106;
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL, val);
	}
	if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE && priv &&
	    priv->mode.rdbk_mode == ESISP_VICAP_ONLINE &&
	    (dev->hdr.hdr_mode == NO_HDR ||
	     (dev->hdr.hdr_mode == HDR_X2 && stream->id == 1) ||
	     (dev->hdr.hdr_mode == HDR_X3 && stream->id == 2)))
		offset = channel->capture_info.multi_dev.pixel_offset;

	es_dvp2axi_write_register(dev, get_reg_index_of_id_ctrl1(channel->id),
			     (channel->width + offset) |
				     (channel->height << 16));

	if (channel->crop_en)
		es_dvp2axi_write_register(
			dev, get_reg_index_of_id_crop_start(channel->id),
			channel->crop_st_y << 16 | channel->crop_st_x);

	if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
		// pr_info("%s: %d  yfx !!!!! \n", __func__, __LINE__);
		// pr_info("%s: %d  yfx assing new buffer one frame !!!!! \n", __func__, __LINE__);

		es_dvp2axi_assign_new_buffer_oneframe(stream , ES_DVP2AXI_YUV_ADDR_STATE_INIT);
		// es_dvp2axi_assign_new_buffer_pingpong(
			// stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, channel->id);
	}
	else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP ||
		 mode == ES_DVP2AXI_STREAM_MODE_TOISP_RDBK)
		es_dvp2axi_assign_new_buffer_pingpong_toisp(
			stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, channel->id);
	else if (mode == ES_DVP2AXI_STREAM_MODE_ESKIT)
		es_dvp2axi_assign_new_buffer_pingpong_eskit(
			stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, channel->id);

	if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE &&
	    index == (capture_info->multi_dev.dev_num - 1) && priv &&
	    priv->mode.rdbk_mode != ESISP_VICAP_ONLINE)
		es_dvp2axi_write_register(
			dev, get_reg_index_of_id_crop_start(channel->id),
			channel->crop_st_y << 16 |
				(channel->crop_st_x +
				 capture_info->multi_dev.pixel_offset));

	es_dvp2axi_write_register(dev, get_reg_index_of_frm0_y_vlw(channel->id),
			     channel->virtual_width);

	if (stream->lack_buf_cnt == 2)
		stream->dma_en = 0;

	if (stream->dma_en) {
		// if (mbus_type == V4L2_MBUS_CSI2_DPHY ||
		//     mbus_type == V4L2_MBUS_CSI2_CPHY)
		// 	dma_en = CSI_DMA_ENABLE;

		dma_en = CSI_DMA_ENABLE;
	}
	if (mbus_type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_type == V4L2_MBUS_CSI2_CPHY) {
		if (stream->dvp2axidev->hdr.esp.mode == HDR_LINE_CNT ||
		    stream->dvp2axidev->hdr.esp.mode == HDR_ID_CODE)
			channel->vc = 0;

		val = CSI_ENABLE_CAPTURE | dma_en | channel->cmd_mode_en << 26 |
		      CSI_ENABLE_CROP_V1 | channel->vc << 8 |
		      channel->data_type << 10 | channel->csi_fmt_val;

		val |= stream->dvp2axi_fmt_in->csi_yuv_order;
		val |= es_dvp2axi_csi_get_output_type_mask(stream);
		if (stream->dvp2axidev->hdr.hdr_mode == NO_HDR ||
		    stream->dvp2axidev->hdr.hdr_mode == HDR_COMPR)
			val |= CSI_NO_HDR;
		else if (stream->dvp2axidev->hdr.hdr_mode == HDR_X2)
			val |= CSI_HDR2;
		else if (stream->dvp2axidev->hdr.hdr_mode == HDR_X3)
			val |= CSI_HDR3;
		if (stream->dvp2axidev->hdr.esp.mode == HDR_NORMAL_VC)
			val |= CSI_HDR_MODE_VC;
		else if (stream->dvp2axidev->hdr.esp.mode == HDR_LINE_CNT)
			val |= CSI_HDR_MODE_LINE_CNT;
		else if (stream->dvp2axidev->hdr.esp.mode == HDR_ID_CODE)
			val |= CSI_HDR_MODE_LINE_INFO;
		if (stream->dvp2axidev->hdr.hdr_mode != NO_HDR &&
		    stream->dvp2axidev->hdr.esp.mode == HDR_NORMAL_VC)
			val |= CSI_HDR_VC_MODE_PROTECT;
		if (stream->is_high_align)
			val |= CSI_HIGH_ALIGN_EIC770X;
		else
			val &= ~CSI_HIGH_ALIGN_EIC770X;
		es_dvp2axi_write_register(
			dev, get_reg_index_of_id_ctrl0(channel->id), val);
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_EFFECT_CODE_ID0,
				     0x02410251);
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_EFFECT_CODE_ID1,
				     0x02420252);
	} 
	if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE) {
		if (index == (capture_info->multi_dev.dev_num - 1))
			stream->dvp2axidev->id_use_cnt++;
	} else {
		stream->dvp2axidev->id_use_cnt++;
	}
	if (!(capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE &&
	      index < capture_info->multi_dev.dev_num - 1)) {
		if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
			// pr_info("%s:%d yfx!!!! \n", __func__, __LINE__);
			// es_dvp2axi_assign_new_buffer_pingpong(
			// 	stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, channel->id);
		}
		else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP ||
			 mode == ES_DVP2AXI_STREAM_MODE_TOISP_RDBK)
			es_dvp2axi_assign_new_buffer_pingpong_toisp(
				stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, channel->id);
	}
	dev->intr_mask = es_dvp2axi_read_register(dev, DVP2AXI_REG_MIPI_LVDS_INTEN);
	return 0;
}

static int es_dvp2axi_csi_stream_start(struct es_dvp2axi_stream *stream,
				  unsigned int mode)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *active_sensor = dev->active_sensor;
	enum v4l2_mbus_type mbus_type = active_sensor->mbus.type;
	struct csi_channel_info *channel;
	u32 ret = 0;
	int i;

	if (stream->state < ES_DVP2AXI_STATE_STREAMING) {
		stream->frame_idx = 0;
		stream->buf_wake_up_cnt = 0;
		stream->frame_phase = 0;
		stream->lack_buf_cnt = 0;
		stream->is_in_vblank = false;
		stream->is_change_toisp = false;
	}

	channel = &dev->channels[stream->id];
	channel->id = stream->id;
	if (mbus_type == V4L2_MBUS_CCP2) {
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_LVDS_CFG,
				       &channel->lvds_cfg);
		if (ret) {
			v4l2_err(&dev->v4l2_dev,
				 "Err: get lvds config failed!!\n");
			return ret;
		}
	}
	es_dvp2axi_csi_channel_init(stream, channel);
	if (stream->state != ES_DVP2AXI_STATE_STREAMING) {
		if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
			stream->dma_en |= ES_DVP2AXI_DMAEN_BY_VICAP;
		} else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP_RDBK) {
			stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ISP;
		} else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP) {
			if (dev->hdr.hdr_mode == HDR_X2 && stream->id == 0)
				stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ISP;
			else if (dev->hdr.hdr_mode == HDR_X3 &&
				 (stream->id == 0 || stream->id == 1))
				stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ISP;
		} else if (mode == ES_DVP2AXI_STREAM_MODE_ESKIT) {
			stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ESKIT;
		}
		if (stream->dvp2axidev->chip_id < CHIP_EIC770X_DVP2AXI) {
			es_dvp2axi_csi_channel_set(stream, channel, mbus_type);
		} else {
			if (channel->capture_info.mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE) {
				// pr_info("%s:%d yfx !!!!! combine one \n", __func__,__LINE__);
				for (i = 0;
				     i <
				     channel->capture_info.multi_dev.dev_num;
				     i++) {
					dev->csi_host_idx =
						channel->capture_info.multi_dev
							.dev_idx[i];
					es_dvp2axi_csi_channel_set_v1(stream,
								 channel,
								 mbus_type,
								 mode, i);
				}
			} else {
				// pr_info("%s:%d yfx !!!!! combine one \n", __func__,__LINE__);
				es_dvp2axi_csi_channel_set_v1(stream, channel,
							 mbus_type, mode, 0);
			}
		}
	} else {
		if (stream->dvp2axidev->chip_id >= CHIP_EIC770X_DVP2AXI) {
			if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
				stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_VICAP;
			} else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP_RDBK) {
				stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_ISP;
			} else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP) {
				if (dev->hdr.hdr_mode == HDR_X2 &&
				    stream->id == 0 && (!stream->dma_en))
					stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_ISP;
				else if (dev->hdr.hdr_mode == HDR_X3 &&
					 (stream->id == 0 || stream->id == 1) &&
					 (!stream->dma_en))
					stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_ISP;
			} else if (mode == ES_DVP2AXI_STREAM_MODE_ESKIT) {
				stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_ESKIT;
			}
		}
	}
	if (stream->state != ES_DVP2AXI_STATE_STREAMING) {
		stream->line_int_cnt = 0;
		if (stream->is_line_wake_up)
			stream->is_can_stop = false;
		else
			stream->is_can_stop = true;
		stream->state = ES_DVP2AXI_STATE_STREAMING;
		dev->workmode = ES_DVP2AXI_WORKMODE_PINGPONG;
	}

	return 0;
}

static void es_dvp2axi_stream_stop(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dvp2axi_dev->active_sensor->mbus;
	u32 val;
	int id;
	int i = 0;
	stream->dvp2axidev->id_use_cnt--;
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		id = stream->id;
		val = es_dvp2axi_read_register(dvp2axi_dev,
					  get_reg_index_of_id_ctrl0(id));
		if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
		    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY)
			val &= ~(CSI_ENABLE_CAPTURE | CSI_DMA_ENABLE);
		else
			val &= ~LVDS_ENABLE_CAPTURE;

		if (dvp2axi_dev->channels[id].capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE) {
			for (i = 0; i < dvp2axi_dev->channels[id]
						.capture_info.multi_dev.dev_num;
			     i++) {
				dvp2axi_dev->csi_host_idx =
					dvp2axi_dev->channels[id]
						.capture_info.multi_dev
						.dev_idx[i];
				es_dvp2axi_write_register(
					dvp2axi_dev, get_reg_index_of_id_ctrl0(id),
					val);
			}
		} else {
			es_dvp2axi_write_register(
				dvp2axi_dev, get_reg_index_of_id_ctrl0(id), val);
		}

		es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTSTAT,
					CSI_START_INTSTAT(id) |
						CSI_DMA_END_INTSTAT(id) |
						CSI_LINE_INTSTAT(id));

		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					 ~(CSI_START_INTEN(id) |
					   CSI_DMA_END_INTEN(id) |
					   CSI_LINE_INTEN(id)));

		if (stream->dvp2axidev->chip_id < CHIP_EIC770X_DVP2AXI) {
			es_dvp2axi_write_register_and(dvp2axi_dev,
						 DVP2AXI_REG_MIPI_LVDS_INTEN,
						 ~CSI_ALL_ERROR_INTEN);
		} else {
			if (stream->dvp2axidev->id_use_cnt == 0) {
				es_dvp2axi_write_register_and(
					dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					~CSI_ALL_ERROR_INTEN_V1);
				if (dvp2axi_dev->channels[id].capture_info.mode ==
				    ESMODULE_MULTI_DEV_COMBINE_ONE) {
					for (i = 0;
					     i < dvp2axi_dev->channels[id]
							 .capture_info.multi_dev
							 .dev_num;
					     i++) {
						dvp2axi_dev->csi_host_idx =
							dvp2axi_dev->channels[id]
								.capture_info
								.multi_dev
								.dev_idx[i];
						es_dvp2axi_write_register_and(
							dvp2axi_dev,
							DVP2AXI_REG_MIPI_LVDS_CTRL,
							~CSI_ENABLE_CAPTURE);
					}
				} else {
					es_dvp2axi_write_register_and(
						dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_CTRL,
						~CSI_ENABLE_CAPTURE);
				}
			}
		}

	} else {
		if (atomic_read(&dvp2axi_dev->pipe.stream_cnt) == 1) {
			val = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL);
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL,
					     val & (~ENABLE_CAPTURE));
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTEN, 0x0);
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT,
					     0x3ff);
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_FRAME_STATUS,
					     0x0);
		}
	}
	stream->state = ES_DVP2AXI_STATE_READY;
	stream->dma_en = 0;
}

static bool es_dvp2axi_is_extending_line_for_height(struct es_dvp2axi_device *dev,
					       struct es_dvp2axi_stream *stream,
					       const struct dvp2axi_input_fmt *fmt)
{
	bool is_extended = false;

	return is_extended;
}

static int es_dvp2axi_queue_setup(struct vb2_queue *queue, unsigned int *num_buffers,
			     unsigned int *num_planes, unsigned int sizes[],
			     struct device *alloc_ctxs[])
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_extend_info *extend_line = &stream->extend_line;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct dvp2axi_output_fmt *dvp2axi_fmt;
	const struct dvp2axi_input_fmt *in_fmt;
	bool is_extended = false;
	u32 i, height;

	pixm = &stream->pixm;
	dvp2axi_fmt = stream->dvp2axi_fmt_out;
	in_fmt = stream->dvp2axi_fmt_in;

	*num_planes = dvp2axi_fmt->mplanes;

	if (stream->crop_enable)
		height = stream->crop[CROP_SRC_ACT].height;
	else
		height = pixm->height;

	is_extended = es_dvp2axi_is_extending_line_for_height(dev, stream, in_fmt);
	if (is_extended && extend_line->is_extended) {
		height = extend_line->pixm.height;
		pixm = &extend_line->pixm;
	}

	for (i = 0; i < dvp2axi_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;
		int h = round_up(height, MEMORY_ALIGN_ROUND_UP_HEIGHT);

		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage / height * h;
	}
	stream->total_buf_num = *num_buffers;
	v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
		 "%s count %d, size %d, extended(%d, %d)\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0],
		 is_extended, extend_line->is_extended);

	return 0;
}

static void es_dvp2axi_check_buffer_update_pingpong(struct es_dvp2axi_stream *stream,
					       int channel_id)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct es_dvp2axi_buffer *buffer = NULL;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	u32 frm_addr_y = 0, frm_addr_uv = 0;
	u32 frm0_addr_y = 0, frm0_addr_uv = 0;
	u32 frm1_addr_y = 0, frm1_addr_uv = 0;
	u32 buff_addr_y = 0, buff_addr_cbcr = 0;
	struct esmodule_capture_info *capture_info =
		&dev->channels[channel_id].capture_info;
	unsigned long flags;
	int frame_phase = 0;
	bool is_dual_update_buf = false;

	spin_lock_irqsave(&stream->vbq_lock, flags);
	if (stream->state == ES_DVP2AXI_STATE_STREAMING &&
	    ((stream->curr_buf == stream->next_buf &&
	      stream->dvp2axi_fmt_in->field != V4L2_FIELD_INTERLACED &&
	      (!dummy_buf->vaddr)) ||
	     stream->curr_buf == NULL || stream->next_buf == NULL)) {
		frame_phase = stream->frame_phase_cache;
		if (!stream->is_line_wake_up ||
		    (stream->is_line_wake_up && stream->frame_idx < 2)) {
			if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
			    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
			    mbus_cfg->type == V4L2_MBUS_CCP2) {
				frm0_addr_y = get_reg_index_of_frm0_y_addr(
					channel_id);
				frm1_addr_y = get_reg_index_of_frm1_y_addr(
					channel_id);
				frm0_addr_uv = get_reg_index_of_frm0_uv_addr(
					channel_id);
				frm1_addr_uv = get_reg_index_of_frm1_uv_addr(
					channel_id);
			} else {
				frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(
					channel_id);
				frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(
					channel_id);
				frm0_addr_uv =
					get_dvp_reg_index_of_frm0_uv_addr(
						channel_id);
				frm1_addr_uv =
					get_dvp_reg_index_of_frm1_uv_addr(
						channel_id);
			}
			if (frame_phase & DVP2AXI_CSI_FRAME0_READY) {
				frm_addr_y = frm0_addr_y;
				frm_addr_uv = frm0_addr_uv;
			} else {
				frm_addr_y = frm1_addr_y;
				frm_addr_uv = frm1_addr_uv;
			}
			if (!stream->dma_en && stream->curr_buf == NULL &&
			    stream->next_buf == NULL)
				is_dual_update_buf = true;
			if (!list_empty(&stream->buf_head)) {
				if (frame_phase == DVP2AXI_CSI_FRAME0_READY) {
					stream->curr_buf = list_first_entry(
						&stream->buf_head,
						struct es_dvp2axi_buffer, queue);
					if (stream->curr_buf) {
						list_del(&stream->curr_buf
								  ->queue);
						buffer = stream->curr_buf;
					}
					if (buffer && is_dual_update_buf)
						stream->next_buf = buffer;
				} else if (frame_phase ==
					   DVP2AXI_CSI_FRAME1_READY) {
					if (stream->next_buf == NULL &&
					    stream->dvp2axi_fmt_in->field ==
						    V4L2_FIELD_INTERLACED) {
						stream->next_buf =
							stream->curr_buf;
					} else {
						stream->next_buf = list_first_entry(
							&stream->buf_head,
							struct es_dvp2axi_buffer,
							queue);
						if (stream->next_buf) {
							list_del(
								&stream->next_buf
									 ->queue);
							buffer =
								stream->next_buf;
						}
						if (buffer &&
						    is_dual_update_buf)
							stream->curr_buf =
								buffer;
					}
				}
			} else {
				v4l2_info(&dev->v4l2_dev, "%s %d\n", __func__,
					  __LINE__);
			}
			if (buffer) {
				if (is_dual_update_buf) {
					buff_addr_y =
						buffer->buff_addr[ES_DVP2AXI_PLANE_Y];
					buff_addr_cbcr =
						buffer->buff_addr
							[ES_DVP2AXI_PLANE_CBCR];
					if (capture_info->mode ==
					    ESMODULE_MULTI_DEV_COMBINE_ONE) {
						es_dvp2axi_write_buff_addr_multi_dev_combine(
							stream, frm0_addr_y,
							frm0_addr_uv,
							buff_addr_y,
							buff_addr_cbcr, false);
						es_dvp2axi_write_buff_addr_multi_dev_combine(
							stream, frm1_addr_y,
							frm1_addr_uv,
							buff_addr_y,
							buff_addr_cbcr, false);
					} else {
						es_dvp2axi_write_register(
							dev, frm0_addr_y,
							buff_addr_y);
						if (stream->dvp2axi_fmt_out
							    ->fmt_type !=
						    DVP2AXI_FMT_TYPE_RAW)
							es_dvp2axi_write_register(
								dev,
								frm0_addr_uv,
								buff_addr_cbcr);
						es_dvp2axi_write_register(
							dev, frm1_addr_y,
							buff_addr_y);
						if (stream->dvp2axi_fmt_out
							    ->fmt_type !=
						    DVP2AXI_FMT_TYPE_RAW)
							es_dvp2axi_write_register(
								dev,
								frm1_addr_uv,
								buff_addr_cbcr);
					}
				} else {
					buff_addr_y =
						buffer->buff_addr[ES_DVP2AXI_PLANE_Y];
					buff_addr_cbcr =
						buffer->buff_addr
							[ES_DVP2AXI_PLANE_CBCR];
					if (capture_info->mode ==
					    ESMODULE_MULTI_DEV_COMBINE_ONE) {
						es_dvp2axi_write_buff_addr_multi_dev_combine(
							stream, frm_addr_y,
							frm_addr_uv,
							buff_addr_y,
							buff_addr_cbcr, false);
					} else {
						es_dvp2axi_write_register(
							dev, frm_addr_y,
							buff_addr_y);
						if (stream->dvp2axi_fmt_out
							    ->fmt_type !=
						    DVP2AXI_FMT_TYPE_RAW)
							es_dvp2axi_write_register(
								dev,
								frm_addr_uv,
								buff_addr_cbcr);
					}
				}
			}
		} else {
			v4l2_dbg(3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
				 "%s %d, is_wake_up %d, frame_idx %d\n",
				 __func__, __LINE__, stream->is_line_wake_up,
				 stream->frame_idx);
			if (stream->curr_buf == stream->next_buf) {
				if (stream->frame_phase_cache ==
				    DVP2AXI_CSI_FRAME0_READY) {
					stream->curr_buf = list_first_entry(
						&stream->buf_head,
						struct es_dvp2axi_buffer, queue);
					v4l2_dbg(
						3, es_dvp2axi_debug, &dev->v4l2_dev,
						"%s %d, stream[%d] buf idx %d\n",
						__func__, __LINE__, stream->id,
						stream->curr_buf->vb.vb2_buf
							.index);
					if (stream->curr_buf)
						list_del(&stream->curr_buf
								  ->queue);
				} else if (stream->frame_phase_cache ==
					   DVP2AXI_CSI_FRAME1_READY) {
					stream->next_buf = list_first_entry(
						&stream->buf_head,
						struct es_dvp2axi_buffer, queue);
					v4l2_dbg(
						4, es_dvp2axi_debug, &dev->v4l2_dev,
						"%s %d, stream[%d] buf idx %d\n",
						__func__, __LINE__, stream->id,
						stream->next_buf->vb.vb2_buf
							.index);
					if (stream->next_buf)
						list_del(&stream->next_buf
								  ->queue);
				}
				stream->is_buf_active = true;
			}
		}
		v4l2_dbg(
			3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			"%s, stream[%d] update buffer, frame_phase %d, is_stop %s, lack_buf_cnt %d\n",
			__func__, stream->id, frame_phase,
			(stream->dma_en ? "false" : "true"),
			stream->lack_buf_cnt);
		if (!stream->dma_en) {
			if (stream->to_stop_dma) {
				stream->to_stop_dma = 0;
				wake_up(&stream->wq_stopped);
			} else {
				stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_VICAP;
				v4l2_dbg(
					3, es_dvp2axi_debug,
					&stream->dvp2axidev->v4l2_dev,
					"%s stream[%d] start dma capture, frame cnt %d\n",
					__func__, stream->id,
					stream->frame_idx);
			}
		} else {
			v4l2_dbg(3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
				 "%s %d, dma_en 0x%x, frame cnt %d\n", __func__,
				 __LINE__, stream->dma_en, stream->frame_idx);
		}
		if (stream->lack_buf_cnt)
			stream->lack_buf_cnt--;

	} else {
		v4l2_info(&dev->v4l2_dev,
			  "%s %d, state %d, curr_buf %p, next_buf %p\n",
			  __func__, __LINE__, stream->state, stream->curr_buf,
			  stream->next_buf);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	if (stream->to_en_dma)
		es_dvp2axi_enable_dma_capture(stream, true);
}

/*
 * The vb2_buffer are stored in es_dvp2axi_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
void es_dvp2axi_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct es_dvp2axi_buffer *dvp2axibuf = to_es_dvp2axi_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct v4l2_pix_format_mplane *pixm = &stream->pixm;
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;
	struct es_dvp2axi_hw *hw_dev = stream->dvp2axidev->hw_dev;
	struct es_dvp2axi_tools_buffer *tools_buf = NULL;
	struct es_dvp2axi_tools_vdev *tools_vdev = stream->tools_vdev;
	unsigned long flags;
	int i;
	// static int first = 1;
	bool is_find_tools_buf = false;

	if (tools_vdev) {
		spin_lock_irqsave(&stream->tools_vdev->vbq_lock, flags);
		if (!list_empty(&tools_vdev->src_buf_head)) {
			list_for_each_entry(tools_buf,
					    &tools_vdev->src_buf_head, list) {
				if (tools_buf->vb == vbuf) {
					is_find_tools_buf = true;
					break;
				}
			}
			if (is_find_tools_buf) {
				if (tools_buf->use_cnt)
					tools_buf->use_cnt--;
				if (tools_buf->use_cnt) {
					spin_unlock_irqrestore(
						&stream->tools_vdev->vbq_lock,
						flags);
					return;
				}
			}
		}
		spin_unlock_irqrestore(&stream->tools_vdev->vbq_lock, flags);
	}

	// uint32_t dvp2axi_out_addr_csr[] = { 0x24, 0x28, 0x2c };
	memset(dvp2axibuf->buff_addr, 0, sizeof(dvp2axibuf->buff_addr));
	/* If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < fmt->mplanes; i++) {
		void *addr = vb2_plane_vaddr(vb, i);

		if (hw_dev->is_dma_sg_ops) {
			struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, i);

			dvp2axibuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			dvp2axibuf->buff_addr[i] =
				vb2_dma_contig_plane_dma_addr(vb, i);
				// writel(dvp2axibuf->buff_addr[i], hw_dev->base_addr + dvp2axi_out_addr_csr[i%3]);
				// DVP2AXI_HalWriteReg(hw_dev, dvp2axi_out_addr_csr[i%3], dvp2axibuf->buff_addr[i]);
				//debug
				// memset(addr, 0xff, pixm->plane_fmt[i].sizeimage);
		}
		if (es_dvp2axi_debug && addr && !hw_dev->iommu_en) {
			//memset(addr, 255, pixm->plane_fmt[i].sizeimage);
			v4l2_dbg(3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
				 "Clear buffer, size: 0x%08x\n",
				 pixm->plane_fmt[i].sizeimage);
		}
	}

	if (fmt->mplanes == 1) {
		for (i = 0; i < fmt->cplanes - 1; i++)
			dvp2axibuf->buff_addr[i + 1] =
				dvp2axibuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline * pixm->height;
	}
	// pr_info("%s:%d yfx !!!!! dvp2axibuf->buff_addr[0] = 0x%x \n", __func__, __LINE__, dvp2axibuf->buff_addr[0]);
	spin_lock_irqsave(&stream->vbq_lock, flags);
	list_add_tail(&dvp2axibuf->queue, &stream->buf_head);
	spin_unlock_irqrestore(&stream->vbq_lock, flags);
	if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP && (!dvp2axibuf->dbuf)) {
		struct esisp_rx_buf *dbufs = NULL;

		dbufs = kzalloc(sizeof(struct esisp_rx_buf), GFP_KERNEL);

		memset(dbufs, 0, sizeof(struct esisp_rx_buf));
		if (stream->dvp2axidev->hdr.hdr_mode == HDR_X2 && stream->id == 0)
			dbufs->type = BUF_MIDDLE;
		else if (stream->dvp2axidev->hdr.hdr_mode == HDR_X3 &&
			 stream->id == 0)
			dbufs->type = BUF_LONG;
		else if (stream->dvp2axidev->hdr.hdr_mode == HDR_X3 &&
			 stream->id == 1)
			dbufs->type = BUF_MIDDLE;
		dvp2axibuf->dbuf = hw_dev->mem_ops->get_dmabuf(
			vb, vb->planes[0].mem_priv, O_RDWR);
		if (dvp2axibuf->dbuf)
			dbufs->dbuf = dvp2axibuf->dbuf;
		list_add_tail(&dbufs->list, &stream->rx_buf_head_vicap);
	}
	if (stream->dvp2axidev->workmode == ES_DVP2AXI_WORKMODE_PINGPONG &&
	    stream->lack_buf_cnt &&
	    stream->cur_stream_mode & ES_DVP2AXI_STREAM_MODE_CAPTURE)
		es_dvp2axi_check_buffer_update_pingpong(stream, stream->id);
	v4l2_dbg(3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "stream[%d] buf queue, index: %d, dma_addr 0x%x\n", stream->id,
		 vb->index, dvp2axibuf->buff_addr[0]);
	atomic_inc(&stream->buf_cnt);
}

void es_dvp2axi_free_rx_buf(struct es_dvp2axi_stream *stream, int buf_num)
{
	struct es_dvp2axi_rx_buffer *buf;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct sditf_priv *priv = dev->sditf[0];
	struct v4l2_subdev *sd;
	int i = 0;
	unsigned long flags;

	if (!priv)
		return;

	sd = get_esisp_sd(dev->sditf[0]);
	if (!sd)
		return;

	if ((dev->is_rtt_suspend || dev->is_aov_reserved) &&
	    dev->is_thunderboot) {
		stream->curr_buf_toisp = NULL;
		stream->next_buf_toisp = NULL;
		INIT_LIST_HEAD(&stream->rx_buf_head);

		for (i = 0; i < buf_num; i++) {
			buf = &stream->rx_buf[i];
			if (buf->dbufs.is_init)
				v4l2_subdev_call(sd, core, ioctl,
						 ESISP_VICAP_CMD_RX_BUFFER_FREE,
						 &buf->dbufs);
			buf->dummy.is_free = true;
		}
		atomic_set(&stream->buf_cnt, 0);
		stream->total_buf_num = 0;
		stream->rx_buf_num = 0;

		return;
	}

	spin_lock_irqsave(&stream->vbq_lock, flags);
	stream->curr_buf_toisp = NULL;
	stream->next_buf_toisp = NULL;
	INIT_LIST_HEAD(&stream->rx_buf_head);
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	if (dev->is_thunderboot)
		spin_lock_irqsave(&dev->buffree_lock, flags);
	for (i = 0; i < buf_num; i++) {
		buf = &stream->rx_buf[i];
		if (buf->dummy.is_free)
			continue;
		if (buf->dbufs.is_init)
			v4l2_subdev_call(sd, core, ioctl,
					 ESISP_VICAP_CMD_RX_BUFFER_FREE,
					 &buf->dbufs);
		if (!dev->is_thunderboot)
			es_dvp2axi_free_buffer(dev, &buf->dummy);
		else
			list_add_tail(&buf->list_free, &priv->buf_free_list);
		atomic_dec(&stream->buf_cnt);
		stream->total_buf_num--;
	}
	stream->rx_buf_num = 0;

	if (dev->is_thunderboot) {
		spin_unlock_irqrestore(&dev->buffree_lock, flags);
		schedule_work(&priv->buffree_work.work);
	}
	stream->dma_en &= ~ES_DVP2AXI_DMAEN_BY_ISP;
	v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "free rx_buf, buf_num %d\n", buf_num);
}

static void es_dvp2axi_get_resmem_head(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_init_rx_buf(struct es_dvp2axi_stream *stream, int buf_num)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_pix_format_mplane *pixm = &stream->pixm;
	struct es_dvp2axi_dummy_buffer *dummy;
	struct es_dvp2axi_rx_buffer *buf;
	struct sditf_priv *priv = dev->sditf[0];
	int frm_type = 0;
	int i = 0;
	int ret = 0;

	if (!priv)
		return -EINVAL;

	if (buf_num > ESISP_VICAP_BUF_CNT_MAX)
		return -EINVAL;

	if (dev->hdr.hdr_mode == NO_HDR) {
		if (stream->id == 0)
			frm_type = BUF_SHORT;
		else
			return -EINVAL;
	} else if (dev->hdr.hdr_mode == HDR_X2) {
		if (stream->id == 0)
			frm_type = BUF_MIDDLE;
		else if (stream->id == 1)
			frm_type = BUF_SHORT;
		else
			return -EINVAL;
	} else if (dev->hdr.hdr_mode == HDR_X3) {
		if (stream->id == 0)
			frm_type = BUF_LONG;
		else if (stream->id == 1)
			frm_type = BUF_MIDDLE;
		else if (stream->id == 2)
			frm_type = BUF_SHORT;
		else
			return -EINVAL;
	}
	while (true) {
		buf = &stream->rx_buf[i];
		memset(buf, 0, sizeof(*buf));
		dummy = &buf->dummy;
		dummy->size = pixm->plane_fmt[0].sizeimage;
		dummy->is_need_vaddr = true;
		dummy->is_need_dbuf = true;
		if (dev->is_thunderboot || dev->is_rtt_suspend ||
		    dev->is_aov_reserved) {
			if (i == 0)
				es_dvp2axi_get_resmem_head(dev);
			buf->buf_idx = i;
			ret = es_dvp2axi_alloc_reserved_mem_buf(dev, buf);
			if (ret) {
				stream->rx_buf_num = i;
				v4l2_info(
					&dev->v4l2_dev,
					"reserved mem support alloc buf num %d, require buf num %d\n",
					i, buf_num);
				break;
			}
			if (dev->rdbk_debug)
				v4l2_info(&dev->v4l2_dev,
					  "stream[%d] buf addr 0x%llx\n",
					  stream->id, (u64)dummy->dma_addr);
		} else {
			ret = es_dvp2axi_alloc_buffer(dev, dummy);
			if (ret) {
				stream->rx_buf_num = i;
				v4l2_info(
					&dev->v4l2_dev,
					"alloc buf num %d, require buf num %d\n",
					i, buf_num);
				break;
			}
			buf->dbufs.dbuf = dummy->dbuf;
		}
		buf->dbufs.is_init = false;
		buf->dbufs.type = frm_type;
		list_add_tail(&buf->list, &stream->rx_buf_head);
		dummy->is_free = false;
		if (stream->is_compact)
			buf->dbufs.is_uncompact = false;
		else
			buf->dbufs.is_uncompact = true;
		if (priv && i == 0) {
			buf->dbufs.is_first = true;
			if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
				es_dvp2axi_s_rx_buffer(stream, &buf->dbufs);
		}
		i++;
		if (!dev->is_thunderboot && i >= buf_num) {
			stream->rx_buf_num = buf_num;
			break;
		} else if (i >= ESISP_VICAP_BUF_CNT_MAX) {
			stream->rx_buf_num = i;
			v4l2_info(&dev->v4l2_dev,
				  "reserved mem alloc buf num %d\n", i);
			break;
		}
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
			 "init rx_buf,dma_addr 0x%llx size: 0x%x\n",
			 (u64)dummy->dma_addr, pixm->plane_fmt[0].sizeimage);
	}
	if (stream->rx_buf_num) {
		stream->total_buf_num = stream->rx_buf_num;
		atomic_set(&stream->buf_cnt, stream->rx_buf_num);
		return 0;
	} else {
		return -EINVAL;
	}
}

static int es_dvp2axi_create_dummy_buf(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_hw *hw = dev->hw_dev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &hw->dummy_buf;
	struct es_dvp2axi_device *tmp_dev = NULL;
	struct v4l2_subdev_frame_interval_enum fie;
	struct v4l2_subdev_format fmt;
	u32 max_size = 0;
	u32 size = 0;
	int ret = 0;
	int i, j;
	
	for (i = 0; i < hw->dev_num; i++) {
		tmp_dev = hw->dvp2axi_dev[i];
		if (tmp_dev->terminal_sensor.sd) {
			for (j = 0; j < 32; j++) {
				memset(&fie, 0, sizeof(fie));
				fie.index = j;
				fie.pad = 0;
				fie.which = V4L2_SUBDEV_FORMAT_ACTIVE;
				ret = v4l2_subdev_call(
					tmp_dev->terminal_sensor.sd, pad,
					enum_frame_interval, NULL, &fie);
				if (!ret) {
					if (fie.code ==
						    MEDIA_BUS_FMT_RGB888_1X24 ||
					    fie.code ==
						    MEDIA_BUS_FMT_BGR888_1X24 ||
					    fie.code ==
						    MEDIA_BUS_FMT_GBR888_1X24)
						size = fie.width * fie.height *
						       3;
					else
						size = fie.width * fie.height *
						       2;
					v4l2_dbg(
						1, es_dvp2axi_debug, &dev->v4l2_dev,
						"%s enum fmt, width %d, height %d\n",
						__func__, fie.width,
						fie.height);
				} else {
					break;
				}
				if (size > max_size)
					max_size = size;
			}
		} else {
			continue;
		}
	}

	if (max_size == 0 && dev->terminal_sensor.sd) {
		fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, pad, get_fmt,
				       NULL, &fmt);
		if (!ret) {
			if (fmt.format.code == MEDIA_BUS_FMT_RGB888_1X24 ||
			    fmt.format.code == MEDIA_BUS_FMT_BGR888_1X24 ||
			    fmt.format.code == MEDIA_BUS_FMT_GBR888_1X24)
				size = fmt.format.width * fmt.format.height * 3;
			else
				size = fmt.format.width * fmt.format.height * 2;
			if (size > max_size)
				max_size = size;
		}
	}

	dummy_buf->size = max_size;

	dummy_buf->is_need_vaddr = true;
	dummy_buf->is_need_dbuf = true;
	ret = es_dvp2axi_alloc_buffer(dev, dummy_buf);
	if (ret) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to allocate the memory for dummy buffer\n");
		return -ENOMEM;
	}

	v4l2_info(&dev->v4l2_dev, "Allocate dummy buffer, size: 0x%08x\n",
		  dummy_buf->size);

	return ret;
}

static void es_dvp2axi_destroy_dummy_buf(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;

	if (dummy_buf->vaddr)
		es_dvp2axi_free_buffer(dev, dummy_buf);
	dummy_buf->dma_addr = 0;
	dummy_buf->vaddr = NULL;
}

static void es_dvp2axi_do_cru_reset(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev->hw_dev;

	unsigned int val, i;
	if (dev->luma_vdev.enable)
		es_dvp2axi_stop_luma(&dev->luma_vdev);

	if (dev->hdr.hdr_mode != NO_HDR) {
		val = es_dvp2axi_read_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL);
		val |= DVP2AXI_MIPI_LVDS_SW_DMA_IDLE;
		es_dvp2axi_write_register(dev, DVP2AXI_REG_MIPI_LVDS_CTRL, val);

		udelay(5);
	}

	for (i = 0; i < ARRAY_SIZE(dvp2axi_hw->dvp2axi_rst); i++)
		if (dvp2axi_hw->dvp2axi_rst[i])
			reset_control_assert(dvp2axi_hw->dvp2axi_rst[i]);

	udelay(10);

	for (i = 0; i < ARRAY_SIZE(dvp2axi_hw->dvp2axi_rst); i++)
		if (dvp2axi_hw->dvp2axi_rst[i])
			reset_control_deassert(dvp2axi_hw->dvp2axi_rst[i]);
}

void es_dvp2axi_do_soft_reset(struct es_dvp2axi_device *dev)
{
	if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
	    dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_MIPI_LVDS_CTRL,
					0x000A0000);
	else
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_CTRL, 0x000A0000);
	usleep_range(10, 20);
	v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev, "vicap do soft reset 0x%x\n",
		 0x000A0000);
}

static void es_dvp2axi_release_rdbk_buf(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_buffer *rdbk_buf = NULL;
	struct es_dvp2axi_buffer *tmp_buf = NULL;
	unsigned long flags;
	bool has_added;
	int index = 0;

	if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID0)
		index = RDBK_L;
	else if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID1)
		index = RDBK_M;
	else if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID2)
		index = RDBK_S;
	else
		return;

	spin_lock_irqsave(&dev->hdr_lock, flags);
	rdbk_buf = dev->rdbk_buf[index];
	if (rdbk_buf) {
		if (rdbk_buf != stream->curr_buf &&
		    rdbk_buf != stream->next_buf) {
			has_added = false;

			list_for_each_entry(tmp_buf, &stream->buf_head, queue) {
				if (tmp_buf == rdbk_buf) {
					has_added = true;
					break;
				}
			}

			if (!has_added)
				list_add_tail(&rdbk_buf->queue,
					      &stream->buf_head);
		}
		dev->rdbk_buf[index] = NULL;
	}
	spin_unlock_irqrestore(&dev->hdr_lock, flags);
}

static void es_dvp2axi_detach_sync_mode(struct es_dvp2axi_device *dvp2axi_dev)
{
	int i = 0;
	struct es_dvp2axi_hw *hw = dvp2axi_dev->hw_dev;
	struct es_dvp2axi_device *tmp_dev;
	struct es_dvp2axi_multi_sync_config *sync_config;

	if ((!dvp2axi_dev->sync_cfg.type) ||
	    (atomic_read(&dvp2axi_dev->pipe.stream_cnt) != 0))
		return;
	mutex_lock(&hw->dev_lock);
	memset(&dvp2axi_dev->sync_cfg, 0, sizeof(dvp2axi_dev->sync_cfg));
	sync_config = &hw->sync_config[dvp2axi_dev->sync_cfg.group];
	sync_config->streaming_cnt--;
	if (dvp2axi_dev->sync_cfg.type == EXTERNAL_MASTER_MODE) {
		for (i = 0; i < sync_config->ext_master.count; i++) {
			tmp_dev = sync_config->ext_master.dvp2axi_dev[i];
			if (tmp_dev == dvp2axi_dev) {
				sync_config->ext_master.is_streaming[i] = false;
				break;
			}
		}
	}
	if (dvp2axi_dev->sync_cfg.type == INTERNAL_MASTER_MODE)
		sync_config->int_master.is_streaming[0] = false;
	if (dvp2axi_dev->sync_cfg.type == SLAVE_MODE) {
		for (i = 0; i < sync_config->slave.count; i++) {
			tmp_dev = sync_config->slave.dvp2axi_dev[i];
			if (tmp_dev == dvp2axi_dev) {
				sync_config->slave.is_streaming[i] = false;
				break;
			}
		}
	}

	if (!sync_config->streaming_cnt && sync_config->is_attach) {
		sync_config->is_attach = false;
		sync_config->mode = ES_DVP2AXI_NOSYNC_MODE;
		sync_config->dev_cnt = 0;
	}
	mutex_unlock(&hw->dev_lock);
}

void es_dvp2axi_do_stop_stream(struct es_dvp2axi_stream *stream,
			  enum es_dvp2axi_stream_mode mode)
{
	struct es_dvp2axi_vdev_node *node = &stream->vnode;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct es_dvp2axi_buffer *buf = NULL;
	int ret;
	struct es_dvp2axi_hw *hw_dev = dev->hw_dev;
	bool can_reset = true;
	int i;
	unsigned long flags;
	u32 vblank = 0;
	u32 frame_time_ns = 0;
	u64 cur_time = 0;
	u64 fs_time = 0;

	mutex_lock(&dev->stream_lock);

	v4l2_info(&dev->v4l2_dev,
		  "stream[%d] start stopping, total mode 0x%x, cur 0x%x\n",
		  stream->id, stream->cur_stream_mode, mode);

	if (mode == stream->cur_stream_mode) {
		if (stream->dma_en) {
			if (!dev->sensor_linetime)
				dev->sensor_linetime =
					es_dvp2axi_get_linetime(stream);
			vblank = es_dvp2axi_get_sensor_vblank(dev);
			if (vblank) {
				frame_time_ns =
					(vblank +
					 dev->terminal_sensor.raw_rect.height) *
					dev->sensor_linetime;
				spin_lock_irqsave(&stream->fps_lock, flags);
				fs_time = stream->readout.fs_timestamp;
				spin_unlock_irqrestore(&stream->fps_lock,
						       flags);
				cur_time = es_dvp2axi_time_get_ns(dev);
				if (cur_time > fs_time &&
				    cur_time - fs_time <
					    (frame_time_ns - 10000000)) {
					spin_lock_irqsave(&stream->vbq_lock,
							  flags);
					if (stream->dma_en &
					    ES_DVP2AXI_DMAEN_BY_VICAP)
						stream->to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_VICAP;
					else if (stream->dma_en &
						 ES_DVP2AXI_DMAEN_BY_ISP)
						stream->to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_ISP;
					stream->is_stop_capture = true;
					es_dvp2axi_stop_dma_capture(stream);
					spin_unlock_irqrestore(
						&stream->vbq_lock, flags);
				}
			}
		}
		stream->stopping = true;
		ret = wait_event_timeout(stream->wq_stopped,
					 stream->state != ES_DVP2AXI_STATE_STREAMING,
					 msecs_to_jiffies(500));
		if (!ret) {
			es_dvp2axi_stream_stop(stream);
			stream->stopping = false;
		}

		video_device_pipeline_stop(&node->vdev);
		ret = dev->pipe.set_stream(&dev->pipe, false);
		if (ret < 0)
			v4l2_err(v4l2_dev,
				 "pipeline stream-off failed error:%d\n", ret);

		dev->is_start_hdr = false;
		stream->is_dvp_yuv_addr_init = false;
		if (stream->skip_info.skip_en) {
			stream->skip_info.skip_en = false;
			stream->skip_info.skip_to_en = true;
		}
	} else if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE &&
		   stream->dma_en & ES_DVP2AXI_DMAEN_BY_VICAP) {
		//only stop dma
		stream->to_stop_dma = ES_DVP2AXI_DMAEN_BY_VICAP;
		stream->is_wait_dma_stop = true;
		wait_event_timeout(stream->wq_stopped,
				   !stream->is_wait_dma_stop,
				   msecs_to_jiffies(1000));
	} else if (mode == ES_DVP2AXI_STREAM_MODE_TOISP &&
		   stream->dma_en & ES_DVP2AXI_DMAEN_BY_VICAP) {
		//only stop dma
		stream->to_stop_dma = ES_DVP2AXI_DMAEN_BY_ISP;
		stream->is_wait_dma_stop = true;
		wait_event_timeout(stream->wq_stopped,
				   !stream->is_wait_dma_stop,
				   msecs_to_jiffies(1000));
	}
	if ((mode & ES_DVP2AXI_STREAM_MODE_CAPTURE) == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
		/* release buffers */
		spin_lock_irqsave(&stream->vbq_lock, flags);
		if (stream->curr_buf)
			list_add_tail(&stream->curr_buf->queue,
				      &stream->buf_head);
		if (stream->next_buf && stream->next_buf != stream->curr_buf)
			list_add_tail(&stream->next_buf->queue,
				      &stream->buf_head);
		spin_unlock_irqrestore(&stream->vbq_lock, flags);

		if (dev->hdr.hdr_mode == HDR_X2 || dev->hdr.hdr_mode == HDR_X3)
			es_dvp2axi_release_rdbk_buf(stream);

		stream->curr_buf = NULL;
		stream->next_buf = NULL;

		es_dvp2axi_rx_buffer_free(stream);
		list_for_each_entry(buf, &stream->buf_head, queue) {
			v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
				 "stream[%d] buf return addr 0x%x\n",
				 stream->id, buf->buff_addr[0]);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}
		INIT_LIST_HEAD(&stream->buf_head);
		while (!list_empty(&stream->vb_done_list)) {
			buf = list_first_entry(&stream->vb_done_list,
					       struct es_dvp2axi_buffer, queue);
			if (buf) {
				list_del(&buf->queue);
				vb2_buffer_done(&buf->vb.vb2_buf,
						VB2_BUF_STATE_ERROR);
			}
		}
		stream->total_buf_num = 0;
		atomic_set(&stream->buf_cnt, 0);
		stream->lack_buf_cnt = 0;
		stream->dma_en &= ~ES_DVP2AXI_DMAEN_BY_VICAP;
	}

	if (mode == stream->cur_stream_mode) {
		ret = dev->pipe.close(&dev->pipe);
		if (ret < 0)
			v4l2_err(v4l2_dev, "pipeline close failed error:%d\n",
				 ret);
		if (dev->hdr.hdr_mode == HDR_X2) {
			if (dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].state ==
				    ES_DVP2AXI_STATE_READY &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1].state ==
				    ES_DVP2AXI_STATE_READY) {
				dev->can_be_reset = true;
			}
		} else if (dev->hdr.hdr_mode == HDR_X3) {
			if (dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].state ==
				    ES_DVP2AXI_STATE_READY &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1].state ==
				    ES_DVP2AXI_STATE_READY &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID2].state ==
				    ES_DVP2AXI_STATE_READY) {
				dev->can_be_reset = true;
			}
		} else {
			dev->can_be_reset = true;
		}
		mutex_lock(&hw_dev->dev_lock);
		for (i = 0; i < hw_dev->dev_num; i++) {
			if (atomic_read(&hw_dev->dvp2axi_dev[i]->pipe.stream_cnt) !=
			    0) {
				can_reset = false;
				break;
			}
		}
		mutex_unlock(&hw_dev->dev_lock);
		if (dev->can_be_reset && dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
			es_dvp2axi_do_soft_reset(dev);
			atomic_set(&dev->streamoff_cnt, 0);
		}
		if (dev->can_be_reset && can_reset) {
			dev->can_be_reset = false;
			dev->reset_work_cancel = true;
			dev->early_line = 0;
			dev->sensor_linetime = 0;
			dev->wait_line = 0;
			stream->is_line_wake_up = false;
		}
		if (can_reset && hw_dev->dummy_buf.vaddr)
			es_dvp2axi_destroy_dummy_buf(stream);
	}
	if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE)
		tasklet_disable(&stream->vb_done_tasklet);

	stream->cur_stream_mode &= ~mode;
	INIT_LIST_HEAD(&stream->vb_done_list);
	v4l2_info(&dev->v4l2_dev, "stream[%d] stopping finished, dma_en 0x%x\n",
		  stream->id, stream->dma_en);
	mutex_unlock(&dev->stream_lock);
	es_dvp2axi_detach_sync_mode(dev);
}

static void es_dvp2axi_stop_streaming(struct vb2_queue *queue)
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	int count = 0;
	uint32_t soft_rstn;
	uint32_t csr0 = DVP2AXI_HalReadReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR);

	pr_info("%s IN\n",  __func__);

	csr0 &= (~0x1f);  // disable dvp chn
    DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, csr0);

	
	dvp2axi_hw_int_mask(stream->dvp2axidev->hw_dev, 1);

	// do software reset
	soft_rstn = DVP2AXI_HalReadReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR) & (~VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
	DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, soft_rstn);
	do {
		soft_rstn = DVP2AXI_HalReadReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR);
		udelay(1000);
    } while ((soft_rstn & VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK) != VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK && count++ < 100);
    	// release reset
    DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
	
	if(count >= 100)
		pr_err("dvp2axi soft reset timeout\n");

    DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
	
	es_dvp2axi_do_stop_stream(stream, ES_DVP2AXI_STREAM_MODE_CAPTURE);
	
	if (dev->irq_stats.not_active_buf_cnt[stream->id] > 0)
		pr_warn("%s: stream %d discard frame num %llu \n", __func__, stream->id, dev->irq_stats.not_active_buf_cnt[stream->id]);

	pr_info("%s OUT\n",  __func__);
}

/*
 * DVP2AXI supports the following input modes,
 *    YUV, the default mode
 *    PAL,
 *    NTSC,
 *    RAW, if the input format is raw bayer
 *    JPEG, TODO
 *    MIPI, TODO
 */
static u32 es_dvp2axi_determine_input_mode(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor_info = dev->active_sensor;
	u32 mode = INPUT_MODE_YUV;
	v4l2_std_id std;
	int ret;

	ret = v4l2_subdev_call(sensor_info->sd, video, querystd, &std);
	if (ret == 0) {
		/* retrieve std from sensor if exist */
		switch (std) {
		case V4L2_STD_NTSC:
			mode = INPUT_MODE_NTSC;
			break;
		case V4L2_STD_PAL:
			mode = INPUT_MODE_PAL;
			break;
		case V4L2_STD_ATSC:
			mode = INPUT_MODE_BT1120;
			break;
		default:
			v4l2_err(&dev->v4l2_dev, "std: %lld is not supported",
				 std);
		}
	} else {
		/* determine input mode by mbus_code (fmt_type) */
		switch (stream->dvp2axi_fmt_in->fmt_type) {
		case DVP2AXI_FMT_TYPE_YUV:
			mode = INPUT_MODE_YUV;
			break;
		case DVP2AXI_FMT_TYPE_RAW:
			mode = INPUT_MODE_RAW;
			break;
		}
	}

	return mode;
}

static u32 es_dvp2axi_determine_input_mode_eic770x(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor_info = dev->active_sensor;
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	__u32 intf = BT656_STD_RAW;
	u32 mode = INPUT_MODE_YUV;
	v4l2_std_id std;
	int ret;

	ret = v4l2_subdev_call(sensor_info->sd, video, querystd, &std);
	if (ret == 0) {
		/* retrieve std from sensor if exist */
		switch (std) {
		case V4L2_STD_NTSC:
		case V4L2_STD_PAL:
			mode = INPUT_BT656_YUV422;
			break;
		case V4L2_STD_ATSC:
			mode = INPUT_BT1120_YUV422;
			break;
		default:
			v4l2_err(&dev->v4l2_dev, "std: %lld is not supported",
				 std);
		}
		mode |= CSI_WRDDR_TYPE_RAW8 << 6;
	} else {
		/* determine input mode by mbus_code (fmt_type) */
		switch (stream->dvp2axi_fmt_in->fmt_type) {
		case DVP2AXI_FMT_TYPE_YUV:
			if (sensor_info->mbus.type == V4L2_MBUS_BT656) {
				if ((sensor_info->mbus.bus.parallel.flags &
				     DVP2AXI_DVP_PCLK_DUAL_EDGE) ==
				    DVP2AXI_DVP_PCLK_DUAL_EDGE)
					mode = INPUT_BT1120_YUV422;
				else
					mode = INPUT_BT656_YUV422;
			} else {
				mode = INPUT_BT601_YUV422;
			}
			mode |= CSI_WRDDR_TYPE_RAW8 << 6;
			break;
		case DVP2AXI_FMT_TYPE_RAW:
			ret = v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
					       ESMODULE_GET_BT656_INTF_TYPE,
					       &intf);
			if (!ret) {
				if (intf == BT656_SONY_RAW)
					mode = INPUT_SONY_RAW;
				else
					mode = INPUT_BT601_RAW;
			} else {
				mode = INPUT_BT601_RAW;
			}
			mode |= stream->dvp2axi_fmt_in->csi_fmt_val << 6;
			break;
		}
	}
	if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_NONE)
		mode |= TRANSMIT_PROGRESS_EIC770X;
	else
		mode |= TRANSMIT_INTERFACE_EIC770X;
	return mode;
}

static inline u32 es_dvp2axi_scl_ctl(struct es_dvp2axi_stream *stream)
{
	u32 fmt_type = stream->dvp2axi_fmt_in->fmt_type;
	return (fmt_type == DVP2AXI_FMT_TYPE_YUV) ? ENABLE_YUV_16BIT_BYPASS :
						ENABLE_RAW_16BIT_BYPASS;
}

/**
 * es_dvp2axi_align_bits_per_pixel() - return the bit width of per pixel for stored
 * In raw or jpeg mode, data is stored by 16-bits,so need to align it.
 */
static u32 es_dvp2axi_align_bits_per_pixel(struct es_dvp2axi_stream *stream,
				      const struct dvp2axi_output_fmt *fmt,
				      int plane_index)
{
	u32 bpp = 0, i, cal = 0;

	if (fmt) {
		switch (fmt->fourcc) {
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_GREY:
		case V4L2_PIX_FMT_Y16:
			bpp = fmt->bpp[plane_index];
			break;
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_VYUY:
		case V4L2_PIX_FMT_Y210:
			if (stream->dvp2axidev->chip_id < CHIP_EIC770X_DVP2AXI)
				bpp = fmt->bpp[plane_index];
			else
				bpp = fmt->bpp[plane_index + 1];
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_BGR24:
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_BGR666:
		case V4L2_PIX_FMT_SRGGB8:
		case V4L2_PIX_FMT_SGRBG8:
		case V4L2_PIX_FMT_SGBRG8:
		case V4L2_PIX_FMT_SBGGR8:
		case V4L2_PIX_FMT_SRGGB10:
		case V4L2_PIX_FMT_SGRBG10:
		case V4L2_PIX_FMT_SGBRG10:
		case V4L2_PIX_FMT_SBGGR10:
		case V4L2_PIX_FMT_SRGGB12:
		case V4L2_PIX_FMT_SGRBG12:
		case V4L2_PIX_FMT_SGBRG12:
		case V4L2_PIX_FMT_SBGGR12:
		case V4L2_PIX_FMT_SBGGR16:
		case V4L2_PIX_FMT_SGBRG16:
		case V4L2_PIX_FMT_SGRBG16:
		case V4L2_PIX_FMT_SRGGB16:
		case V4l2_PIX_FMT_SPD16:
		case V4l2_PIX_FMT_EBD8:
		case V4L2_PIX_FMT_Y10:
		case V4L2_PIX_FMT_Y12:
			bpp = max(fmt->bpp[plane_index],
			(u8)DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126);
	  		cal = DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126;
			for (i = 1; i < 5; i++) {
				if (i * cal >= bpp) {
					bpp = i * cal;
					break;
				}
			}
			break;
		default:
			v4l2_err(&stream->dvp2axidev->v4l2_dev,
				 "fourcc: %d is not supported!\n", fmt->fourcc);
			break;
		}
	}

	return bpp;
}

/**
 * es_dvp2axi_cal_raw_vir_line_ratio() - return ratio for virtual line width setting
 * In raw or jpeg mode, data is stored by 16-bits,
 * so need to align virtual line width.
 */
static u32 es_dvp2axi_cal_raw_vir_line_ratio(struct es_dvp2axi_stream *stream,
					const struct dvp2axi_output_fmt *fmt)
{
	u32 ratio = 0, bpp = 0;
	if (fmt) {
		bpp = es_dvp2axi_align_bits_per_pixel(stream, fmt, 0);
		ratio = bpp / DVP2AXI_YUV_STORED_BIT_WIDTH;
	}

	return ratio;
}

static void es_dvp2axi_sync_crop_info(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_subdev_selection input_sel;
	int ret;
	if (dev->terminal_sensor.sd) {
		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		input_sel.pad = 0;
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, pad,
				       get_selection, NULL, &input_sel);
		if (!ret) {
			stream->crop[CROP_SRC_SENSOR] = input_sel.r;
			stream->crop_enable = true;
			stream->crop_mask |= CROP_SRC_SENSOR_MASK;
			dev->terminal_sensor.selection = input_sel;
		} else {
			stream->crop_mask &= ~CROP_SRC_SENSOR_MASK;
			dev->terminal_sensor.selection.r =
				dev->terminal_sensor.raw_rect;
		}
	}

	if ((stream->crop_mask & 0x3) ==
	    (CROP_SRC_USR_MASK | CROP_SRC_SENSOR_MASK)) {
		if (stream->crop[CROP_SRC_USR].left +
				    stream->crop[CROP_SRC_USR].width >
			    stream->crop[CROP_SRC_SENSOR].width ||
		    stream->crop[CROP_SRC_USR].top +
				    stream->crop[CROP_SRC_USR].height >
			    stream->crop[CROP_SRC_SENSOR].height)
			stream->crop[CROP_SRC_USR] =
				stream->crop[CROP_SRC_SENSOR];
	}

	if (stream->crop_mask & CROP_SRC_USR_MASK) {
		stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_USR];
		if (stream->crop_mask & CROP_SRC_SENSOR_MASK) {
			stream->crop[CROP_SRC_ACT].left =
				stream->crop[CROP_SRC_USR].left +
				stream->crop[CROP_SRC_SENSOR].left;
			stream->crop[CROP_SRC_ACT].top =
				stream->crop[CROP_SRC_USR].top +
				stream->crop[CROP_SRC_SENSOR].top;
		}
	} else if (stream->crop_mask & CROP_SRC_SENSOR_MASK) {
		stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_SENSOR];
	} else {
		stream->crop[CROP_SRC_ACT] = dev->terminal_sensor.raw_rect;
	}
}

/**es_dvp2axi_sanity_check_fmt - check fmt for setting
 * @stream - the stream for setting
 * @s_crop - the crop information
 */
static int es_dvp2axi_sanity_check_fmt(struct es_dvp2axi_stream *stream,
				  const struct v4l2_rect *s_crop)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct v4l2_rect input, *crop;

	if (dev->terminal_sensor.sd) {
		stream->dvp2axi_fmt_in = es_dvp2axi_get_input_fmt(
			dev, &input, stream->id, &dev->channels[stream->id]);
		if (!stream->dvp2axi_fmt_in) {
			v4l2_err(v4l2_dev, "Input fmt is invalid\n");
			return -EINVAL;
		}
		pr_info("%s:%d input.width %d, input.height %d\n", __func__, __LINE__, input.width, input.height);
	} else {
		v4l2_err(v4l2_dev, "terminal_sensor is invalid\n");
		return -EINVAL;
	}

	if (stream->dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8 ||
	    stream->dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8) {
		stream->crop_enable = false;
		return 0;
	}

	if (s_crop)
		crop = (struct v4l2_rect *)s_crop;
	else
		crop = &stream->crop[CROP_SRC_ACT];

	if (crop->width + crop->left > input.width ||
	    crop->height + crop->top > input.height) {
		pr_debug("%s:%d crop->width %d, crop->left %d, crop->height %d,crop->top %d, input.width %d, input.height %d\n", __func__, __LINE__, crop->width, crop->left, crop->height, crop->top, input.width, input.height);
		v4l2_err(v4l2_dev, "crop size is bigger than input\n");
		return -EINVAL;
	}

	if (dev->active_sensor &&
	    (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	     dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY)) {
		if (crop->left > 0) {
			int align_x = get_csi_crop_align(stream->dvp2axi_fmt_in);

			if (align_x > 0 && crop->left % align_x != 0) {
				v4l2_err(v4l2_dev,
					 "ERROR: crop left must align %d\n",
					 align_x);
				return -EINVAL;
			}
		}
	} else if (dev->active_sensor &&
		   dev->active_sensor->mbus.type == V4L2_MBUS_CCP2) {
		if (crop->left % 4 != 0 && crop->width % 4 != 0) {
			v4l2_err(
				v4l2_dev,
				"ERROR: lvds crop left and width must align %d\n",
				4);
			return -EINVAL;
		}
	}

	return 0;
}

int es_dvp2axi_update_sensor_info(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_sensor_info *sensor, *terminal_sensor;
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	sensor_sd = get_remote_sensor(stream, NULL);
	if (!sensor_sd) {
		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "%s: stream[%d] get remote sensor_sd failed!\n",
			 __func__, stream->id);
		return -ENODEV;
	}

	sensor = sd_to_sensor(stream->dvp2axidev, sensor_sd);
	if (!sensor) {
		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "%s: stream[%d] get remote sensor failed!\n", __func__,
			 stream->id);
		return -ENODEV;
	}
	ret = v4l2_subdev_call(sensor->sd, pad, get_mbus_config, 0,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD) {
		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "%s: get remote %s mbus failed!\n", __func__,
			 sensor->sd->name);
		return ret;
	}

	stream->dvp2axidev->active_sensor = sensor;

	terminal_sensor = &stream->dvp2axidev->terminal_sensor;
	get_remote_terminal_sensor(stream, &terminal_sensor->sd);
	if (terminal_sensor->sd) {
		ret = v4l2_subdev_call(terminal_sensor->sd, pad,
				       get_mbus_config, 0,
				       &terminal_sensor->mbus);
		if (ret && ret != -ENOIOCTLCMD) {
			v4l2_err(&stream->dvp2axidev->v4l2_dev,
				 "%s: get terminal %s mbus failed!\n", __func__,
				 terminal_sensor->sd->name);
			return ret;
		}
		ret = v4l2_subdev_call(terminal_sensor->sd, video,
				       g_frame_interval, &terminal_sensor->fi);
		if (ret) {
			v4l2_err(
				&stream->dvp2axidev->v4l2_dev,
				"%s: get terminal %s g_frame_interval failed!\n",
				__func__, terminal_sensor->sd->name);
			return ret;
		}
		// if (v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
		// 		     ESMODULE_GET_CSI_DSI_INFO,
		// 		     &terminal_sensor->dsi_input_en)) {
		// 	v4l2_dbg(
		// 		1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		// 		"%s: get terminal %s CSI/DSI sel failed, default csi input!\n",
		// 		__func__, terminal_sensor->sd->name);
		// 	terminal_sensor->dsi_input_en = 0;
		// }
		terminal_sensor->dsi_input_en= 1;
	} else {
		v4l2_err(&stream->dvp2axidev->v4l2_dev,
			 "%s: stream[%d] get remote terminal sensor failed!\n",
			 __func__, stream->id);
		return -ENODEV;
	}

	if (terminal_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	    terminal_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY)
		terminal_sensor->lanes =
			terminal_sensor->mbus.bus.mipi_csi2.num_data_lanes;
	else if (terminal_sensor->mbus.type == V4L2_MBUS_CCP2)
		terminal_sensor->lanes =
			terminal_sensor->mbus.bus.mipi_csi1.data_lane;
	return ret;
}

static int es_dvp2axi_dvp_get_output_type_mask(struct es_dvp2axi_stream *stream)
{
	unsigned int mask;
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;
	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_NV16:
		mask = (CSI_WRDDR_TYPE_YUV422SP_EIC770X << 11) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << 1);
		break;
	case V4L2_PIX_FMT_NV61:
		mask = (CSI_WRDDR_TYPE_YUV422SP_EIC770X << 11) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << 1);
		break;
	case V4L2_PIX_FMT_NV12:
		mask = (CSI_WRDDR_TYPE_YUV420SP_EIC770X << 11) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << 1);
		break;
	case V4L2_PIX_FMT_NV21:
		mask = (CSI_WRDDR_TYPE_YUV420SP_EIC770X << 11) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << 1);
		break;
	case V4L2_PIX_FMT_YUYV:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << 11) |
		       (CSI_YUV_OUTPUT_ORDER_YUYV << 1);
		break;
	case V4L2_PIX_FMT_YVYU:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << 11) |
		       (CSI_YUV_OUTPUT_ORDER_YVYU << 1);
		break;
	case V4L2_PIX_FMT_UYVY:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << 11) |
		       (CSI_YUV_OUTPUT_ORDER_UYVY << 1);
		break;
	case V4L2_PIX_FMT_VYUY:
		mask = (CSI_WRDDR_TYPE_YUV_PACKET << 11) |
		       (CSI_YUV_OUTPUT_ORDER_VYUY << 1);
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_BGR666:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT << 11;
		break;
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
		if (stream->is_compact)
			mask = CSI_WRDDR_TYPE_RAW_COMPACT << 11;
		else
			mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT << 11;
		break;
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
	case V4L2_PIX_FMT_Y16:
		mask = CSI_WRDDR_TYPE_RAW_UNCOMPACT << 11;
		break;
	default:
		mask = CSI_WRDDR_TYPE_RAW_COMPACT << 11;
		break;
	}
	return mask;
}

static int es_dvp2axi_stream_start(struct es_dvp2axi_stream *stream, unsigned int mode)
{
	u32 val, mbus_flags, href_pol, vsync_pol,
		xfer_mode = 0, yc_swap = 0, inputmode = 0, mipimode = 0,
		workmode = 0, multi_id = 0,
		multi_id_en = BT656_1120_MULTI_ID_DISABLE,
		multi_id_mode = BT656_1120_MULTI_ID_MODE_1,
		multi_id_sel = BT656_1120_MULTI_ID_SEL_LSB,
		bt1120_edge_mode = BT1120_CLOCK_SINGLE_EDGES, bt1120_flags = 0,
		out_fmt_mask = 0, in_fmt_yuv_order = 0;
	struct esmodule_bt656_mbus_info bt1120_info;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor_info;
	struct v4l2_mbus_config *mbus;
	struct es_dvp2axi_dvp_sof_subdev *sof_sd = &dev->dvp_sof_subdev;
	const struct dvp2axi_output_fmt *fmt;
	unsigned int dma_en = 0;
	unsigned int dma_state = 0;
	int i = 0;
	u32 sav_detect = BT656_DETECT_SAV;
	u32 reserved = 0;

	if (stream->state < ES_DVP2AXI_STATE_STREAMING) {
		stream->frame_idx = 0;
		stream->buf_wake_up_cnt = 0;
		stream->lack_buf_cnt = 0;
		stream->frame_phase = 0;
		stream->is_in_vblank = false;
		stream->is_change_toisp = false;
	}

	sensor_info = dev->active_sensor;
	mbus = &sensor_info->mbus;

	dma_state = stream->dma_en;
	if ((mode & ES_DVP2AXI_STREAM_MODE_CAPTURE) == ES_DVP2AXI_STREAM_MODE_CAPTURE)
		stream->dma_en |= ES_DVP2AXI_DMAEN_BY_VICAP;
	else if ((mode & ES_DVP2AXI_STREAM_MODE_TOISP_RDBK) ==
		 ES_DVP2AXI_STREAM_MODE_TOISP_RDBK)
		stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ISP;
	else if ((mode & ES_DVP2AXI_STREAM_MODE_ESKIT) == ES_DVP2AXI_STREAM_MODE_ESKIT)
		stream->dma_en |= ES_DVP2AXI_DMAEN_BY_ESKIT;

	if (dma_state)
		return 0;

	mbus_flags = mbus->bus.parallel.flags;
	if ((mbus_flags & DVP2AXI_DVP_PCLK_DUAL_EDGE) == DVP2AXI_DVP_PCLK_DUAL_EDGE) {
		bt1120_edge_mode = (dev->chip_id < CHIP_EIC770X_DVP2AXI ?
					    BT1120_CLOCK_DOUBLE_EDGES :
					    BT1120_CLOCK_DOUBLE_EDGES_EIC770X);
		es_dvp2axi_enable_dvp_clk_dual_edge(dev, true);
	} else {
		bt1120_edge_mode = dev->chip_id < CHIP_EIC770X_DVP2AXI ?
					   BT1120_CLOCK_SINGLE_EDGES :
					   BT1120_CLOCK_SINGLE_EDGES_EIC770X;
		es_dvp2axi_enable_dvp_clk_dual_edge(dev, false);
	}

	if (mbus_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		es_dvp2axi_config_dvp_clk_sampling_edge(dev, ES_DVP2AXI_CLK_RISING);
	else
		es_dvp2axi_config_dvp_clk_sampling_edge(dev, ES_DVP2AXI_CLK_FALLING);

	if (sensor_info->sd && mbus->type == V4L2_MBUS_BT656) {
		int ret;

		multi_id_en = BT656_1120_MULTI_ID_ENABLE;

		ret = v4l2_subdev_call(sensor_info->sd, core, ioctl,
				       ESMODULE_GET_BT656_MBUS_INFO,
				       &bt1120_info);
		if (ret) {
			v4l2_warn(&dev->v4l2_dev,
				  "waring: no muti channel info for BT.656\n");
		} else {
			bt1120_flags = bt1120_info.flags;
			if (bt1120_flags & ESMODULE_CAMERA_BT656_PARSE_ID_LSB)
				multi_id_sel = BT656_1120_MULTI_ID_SEL_LSB;
			else
				multi_id_sel = BT656_1120_MULTI_ID_SEL_MSB;

			if (((bt1120_flags & ESMODULE_CAMERA_BT656_CHANNELS) >>
			     2) > 3)
				multi_id_mode = BT656_1120_MULTI_ID_MODE_4;
			else if (((bt1120_flags &
				   ESMODULE_CAMERA_BT656_CHANNELS) >>
				  2) > 1)
				multi_id_mode = BT656_1120_MULTI_ID_MODE_2;
			for (i = 0; i < 4; i++)
				multi_id |= DVP_SW_MULTI_ID(
					i, i, bt1120_info.id_en_bits);
			es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_MULTI_ID,
						multi_id);
		}
	}

	href_pol = (mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) ?
			   HSY_HIGH_ACTIVE :
			   HSY_LOW_ACTIVE;
	vsync_pol = (mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) ?
			    VSY_HIGH_ACTIVE :
			    VSY_LOW_ACTIVE;

	if (dev->chip_id < CHIP_EIC770X_DVP2AXI)
		inputmode = es_dvp2axi_determine_input_mode(stream);
	else
		inputmode = es_dvp2axi_determine_input_mode_eic770x(stream);
	if ((inputmode & INPUT_BT1120_YUV422) == INPUT_BT1120_YUV422)
	if (DVP2AXI_FETCH_IS_Y_FIRST(
			stream->dvp2axi_fmt_in->dvp_fmt_val))
		yc_swap = BT1120_YC_SWAP_EIC770X;

	if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY) {
		inputmode = INPUT_MODE_MIPI;

		/* if dvp2axi is linked with mipi,
		 * href pol must be set as high active,
		 * vsyn pol must be set as low active.
		 */
		href_pol = HSY_HIGH_ACTIVE;
		vsync_pol = VSY_LOW_ACTIVE;

		if (stream->dvp2axi_fmt_in->fmt_type == DVP2AXI_FMT_TYPE_YUV)
			mipimode = MIPI_MODE_YUV;
		else if (stream->dvp2axi_fmt_in->fmt_type == DVP2AXI_FMT_TYPE_RAW)
			mipimode = MIPI_MODE_RGB;
		else
			mipimode = MIPI_MODE_32BITS_BYPASS;
	}

	if (dev->chip_id < CHIP_EIC770X_DVP2AXI) {
		val = vsync_pol | href_pol | inputmode | mipimode |
		      stream->dvp2axi_fmt_out->fmt_val |
		      stream->dvp2axi_fmt_in->dvp_fmt_val | xfer_mode | yc_swap |
		      multi_id_en | multi_id_sel | multi_id_mode |
		      bt1120_edge_mode;
		if (stream->is_high_align)
			val |= DVP2AXI_HIGH_ALIGN;
		else
			val &= ~DVP2AXI_HIGH_ALIGN;
	} else {
		out_fmt_mask = es_dvp2axi_dvp_get_output_type_mask(stream);
		in_fmt_yuv_order = es_dvp2axi_dvp_get_input_yuv_order(stream);
		val = vsync_pol | href_pol | inputmode | yc_swap |
		      out_fmt_mask | in_fmt_yuv_order | multi_id_en |
		      sav_detect | multi_id_sel | multi_id_mode |
		      bt1120_edge_mode;
		if (stream->is_high_align)
			val |= DVP2AXI_HIGH_ALIGN_EIC770X;
		else
			val &= ~DVP2AXI_HIGH_ALIGN_EIC770X;
	}
	es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_FOR, val);

	if (dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
		val = stream->pixm.plane_fmt[0].bytesperline;
	} else {
		fmt = es_dvp2axi_find_output_fmt(stream, stream->pixm.pixelformat);
		if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW &&
		    fmt->csi_fmt_val == CSI_WRDDR_TYPE_RAW8)
			val = ALIGN(stream->pixm.width * fmt->raw_bpp / 8, 256);
		else
			val = stream->pixm.width *
			      es_dvp2axi_cal_raw_vir_line_ratio(stream, fmt);
	}

	if (stream->crop_enable) {
		dev->channels[stream->id].crop_en = 1;
		dev->channels[stream->id].crop_st_x =
			stream->crop[CROP_SRC_ACT].left;
		dev->channels[stream->id].crop_st_y =
			stream->crop[CROP_SRC_ACT].top;
		dev->channels[stream->id].width =
			stream->crop[CROP_SRC_ACT].width;
		dev->channels[stream->id].height =
			stream->crop[CROP_SRC_ACT].height;
	} else {
		dev->channels[stream->id].crop_st_y = 0;
		dev->channels[stream->id].crop_st_x = 0;
		dev->channels[stream->id].width = stream->pixm.width;
		dev->channels[stream->id].height = stream->pixm.height;
		dev->channels[stream->id].crop_en = 0;
	}

	es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_VIR_LINE_WIDTH, val);
	es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_SET_SIZE,
			     dev->channels[stream->id].width |
				     (dev->channels[stream->id].height << 16));
	es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_CROP,
			     dev->channels[stream->id].crop_st_y
					     << DVP2AXI_CROP_Y_SHIFT |
				     dev->channels[stream->id].crop_st_x);

	if (atomic_read(&dev->pipe.stream_cnt) <= 1)
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_FRAME_STATUS,
				     FRAME_STAT_CLS);

	if (dev->chip_id < CHIP_EIC770X_DVP2AXI) {
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_INTSTAT, INTSTAT_CLS);
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_SCL_CTRL,
				     es_dvp2axi_scl_ctl(stream));
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_INTEN,
					DVP_DMA_END_INTSTAT(stream->id) |
						INTSTAT_ERR |
						PST_INF_FRAME_END);
		/* enable line int for sof */
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_LINE_INT_NUM, 0x1);
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_INTEN, LINE_INT_EN);
	} else {
		reserved = 0;
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_INTSTAT,
				     0x3c3ffff | reserved);
		es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_INTEN,
					0x033ffff); //0x3c3ffff
	}

	// if (stream->dma_en) {
	// 	pr_info("%s:%d yfx !!!! dma enable \n", __func__, __LINE__);
	// 	pr_info("%s:%d \n", __func__, __LINE__);

	// }
	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_INTEN,
				DVP_DMA_END_INTSTAT(stream->id) | INTSTAT_ERR |
					PST_INF_FRAME_END);

	/* enable line int for sof */
	es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_LINE_INT_NUM, 0x1);
	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_DVP_INTEN, LINE_INT_EN);

	if (dev->workmode == ES_DVP2AXI_WORKMODE_ONEFRAME)
		workmode = MODE_ONEFRAME;
	else if (dev->workmode == ES_DVP2AXI_WORKMODE_PINGPONG)
		workmode = MODE_PINGPONG;
	else
		workmode = MODE_LINELOOP;

	if ((inputmode & INPUT_MODE_BT1120) == INPUT_MODE_BT1120) {
		workmode = MODE_PINGPONG;
		dev->workmode = ES_DVP2AXI_WORKMODE_PINGPONG;
	}

	if (dev->chip_id == CHIP_EIC770X_DVP2AXI) {
		if (stream->dma_en)
			dma_en = DVP_DMA_EN;
		if (stream->lack_buf_cnt == 2)
			dma_en = 0;
		es_dvp2axi_write_register(
			dev, DVP2AXI_REG_DVP_CTRL,
			DVP_SW_WATER_LINE_25 | dma_en | DVP_PRESS_EN |
				DVP_HURRY_EN | DVP_SW_WATER_LINE_25 |
				DVP_SW_PRESS_VALUE(3) | DVP_SW_HURRY_VALUE(3) |
				ENABLE_CAPTURE);
	} else {
		es_dvp2axi_write_register(dev, DVP2AXI_REG_DVP_CTRL,
				     AXI_BURST_16 | workmode | ENABLE_CAPTURE);
	}
	dev->intr_mask = es_dvp2axi_read_register(dev, DVP2AXI_REG_DVP_INTSTAT);

	atomic_set(&sof_sd->frm_sync_seq, 0);
	stream->state = ES_DVP2AXI_STATE_STREAMING;
	stream->dvp2axidev->dvp_sof_in_oneframe = 1;

	return 0;
}

static void es_dvp2axi_attach_sync_mode(struct es_dvp2axi_device *dvp2axidev)
{
	struct es_dvp2axi_hw *hw = dvp2axidev->hw_dev;
	struct es_dvp2axi_device *dev;
	struct sditf_priv *priv;
	int i = 0, j = 0;
	int ret = 0;
	int count = 0;
	int sync_type = NO_SYNC_MODE;
	int sync_group = 0;
	struct es_dvp2axi_sync_cfg sync_cfg;
	struct es_dvp2axi_multi_sync_config *sync_config;

	mutex_lock(&hw->dev_lock);
	if (dvp2axidev->sditf_cnt <= 1) {
		ret = v4l2_subdev_call(dvp2axidev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_SYNC_MODE, &sync_type);
		if (!ret)
			sync_cfg.type = sync_type;
		else
			sync_cfg.type = NO_SYNC_MODE;
		ret = v4l2_subdev_call(dvp2axidev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_GROUP_ID, &sync_group);
		if (!ret && sync_group < ES_DVP2AXI_MAX_GROUP)
			sync_cfg.group = sync_group;
		else
			sync_cfg.group = 0;
	} else {
		for (j = 0; j < dvp2axidev->sditf_cnt; j++) {
			ret |= v4l2_subdev_call(dvp2axidev->sditf[j]->sensor_sd,
						core, ioctl,
						ESMODULE_GET_SYNC_MODE,
						&sync_type);
			if (!ret && sync_type)
				break;
		}
		if (!ret)
			sync_cfg.type = sync_type;
		else
			sync_cfg.type = NO_SYNC_MODE;
		ret = v4l2_subdev_call(dvp2axidev->sditf[j]->sensor_sd, core, ioctl,
				       ESMODULE_GET_GROUP_ID, &sync_group);
		if (!ret && sync_group < ES_DVP2AXI_MAX_GROUP)
			sync_cfg.group = sync_group;
		else
			sync_cfg.group = 0;
	}
	dvp2axidev->sync_cfg = sync_cfg;
	if (sync_cfg.type == NO_SYNC_MODE ||
	    hw->sync_config[sync_cfg.group].is_attach) {
		mutex_unlock(&hw->dev_lock);
		return;
	}

	sync_config = &hw->sync_config[sync_cfg.group];
	memset(sync_config, 0, sizeof(struct es_dvp2axi_multi_sync_config));
	for (i = 0; i < hw->dev_num; i++) {
		dev = hw->dvp2axi_dev[i];
		if (dev->sditf_cnt <= 1) {
			ret = v4l2_subdev_call(dev->terminal_sensor.sd, core,
					       ioctl, ESMODULE_GET_SYNC_MODE,
					       &sync_type);
			if (!ret)
				sync_cfg.type = sync_type;
			else
				sync_cfg.type = NO_SYNC_MODE;
			ret = v4l2_subdev_call(dev->terminal_sensor.sd, core,
					       ioctl, ESMODULE_GET_GROUP_ID,
					       &sync_group);
			if (!ret && sync_group < ES_DVP2AXI_MAX_GROUP)
				sync_cfg.group = sync_group;
			else
				sync_cfg.group = 0;
		} else {
			priv = dev->sditf[0];
			if (priv && priv->is_combine_mode &&
			    dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF) {
				for (j = 0; j < dev->sditf_cnt; j++) {
					ret |= v4l2_subdev_call(
						dev->sditf[j]->sensor_sd, core,
						ioctl, ESMODULE_GET_SYNC_MODE,
						&sync_type);
					if (!ret && sync_type) {
						priv = dev->sditf[j];
						break;
					}
				}
				if (!ret)
					sync_cfg.type = sync_type;
				else
					sync_cfg.type = NO_SYNC_MODE;
				ret = v4l2_subdev_call(priv->sensor_sd, core,
						       ioctl,
						       ESMODULE_GET_GROUP_ID,
						       &sync_group);
				if (!ret && sync_group < ES_DVP2AXI_MAX_GROUP)
					sync_cfg.group = sync_group;
				else
					sync_cfg.group = 0;
			}
		}
		if (sync_cfg.group == dvp2axidev->sync_cfg.group) {
			if (sync_cfg.type == EXTERNAL_MASTER_MODE) {
				count = sync_config->ext_master.count;
				sync_config->ext_master.dvp2axi_dev[count] = dev;
				sync_config->ext_master.count++;
				sync_config->dev_cnt++;
				sync_config->sync_mask |=
					BIT(dev->csi_host_idx);
			} else if (sync_cfg.type == INTERNAL_MASTER_MODE) {
				count = sync_config->int_master.count;
				sync_config->int_master.dvp2axi_dev[count] = dev;
				sync_config->int_master.count++;
				sync_config->dev_cnt++;
				sync_config->sync_mask |=
					BIT(dev->csi_host_idx);
			} else if (sync_cfg.type == SLAVE_MODE) {
				count = sync_config->slave.count;
				sync_config->slave.dvp2axi_dev[count] = dev;
				sync_config->slave.count++;
				sync_config->dev_cnt++;
				sync_config->sync_mask |=
					BIT(dev->csi_host_idx);
			}
			dev->sync_cfg = sync_cfg;
		} else {
			ret = v4l2_subdev_call(dev->terminal_sensor.sd, core,
					       ioctl, ESMODULE_GET_SYNC_MODE,
					       &sync_type);
		}
	}
	if (sync_config->int_master.count == 1) {
		if (sync_config->ext_master.count) {
			sync_config->mode = ES_DVP2AXI_MASTER_MASTER;
			sync_config->is_attach = true;
		} else if (sync_config->slave.count) {
			sync_config->mode = ES_DVP2AXI_MASTER_SLAVE;
			sync_config->is_attach = true;
		} else {
			dev_info(
				hw->dev,
				"Missing slave device, do not use sync mode\n");
		}
		if (sync_config->is_attach)
			dev_info(
				hw->dev,
				"group %d, int_master %d, ext_master %d, slave %d\n",
				i, sync_config->int_master.count,
				sync_config->ext_master.count,
				sync_config->slave.count);
	}
	mutex_unlock(&hw->dev_lock);
}

static void es_dvp2axi_monitor_reset_event(struct es_dvp2axi_device *dev);

int es_dvp2axi_do_start_stream(struct es_dvp2axi_stream *stream,
			  enum es_dvp2axi_stream_mode mode)
{
	struct es_dvp2axi_vdev_node *node = &stream->vnode;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_hw *hw_dev = dev->hw_dev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct es_dvp2axi_sensor_info *sensor_info = dev->active_sensor;
	struct es_dvp2axi_sensor_info *terminal_sensor = NULL;
	struct esmodule_hdr_cfg hdr_cfg;
	struct es_dvp2axi_csi_info csi_info = { 0 };
	int esmodule_stream_seq = ESMODULE_START_STREAM_DEFAULT;
	int ret;
	int i = 0;
	u32 skip_frame = 0;

	v4l2_info(&dev->v4l2_dev, "stream[%d] start streaming\n", stream->id);

	es_dvp2axi_attach_sync_mode(dev);
	mutex_lock(&dev->stream_lock);

	if ((stream->cur_stream_mode & ES_DVP2AXI_STREAM_MODE_CAPTURE) == mode) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}
	if (stream->dma_en == 0)
		stream->fs_cnt_in_single_frame = 0;
	if (stream->is_line_wake_up)
		stream->is_line_inten = true;
	else
		stream->is_line_inten = false;

	if (!dev->active_sensor) {
		ret = es_dvp2axi_update_sensor_info(stream);
		if (ret < 0) {
			v4l2_dbg(1, es_dvp2axi_debug,v4l2_dev, "update sensor info failed %d\n",
				 ret);
			goto out;
		}
	}
	terminal_sensor = &dev->terminal_sensor;
	if (terminal_sensor->sd) {
		ret = v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
				       ESMODULE_GET_HDR_CFG, &hdr_cfg);
		if (!ret)
			dev->hdr = hdr_cfg;
		else
			dev->hdr.hdr_mode = NO_HDR;

		ret = v4l2_subdev_call(terminal_sensor->sd, video,
				       g_frame_interval, &terminal_sensor->fi);
		if (ret)
			terminal_sensor->fi.interval =
				(struct v4l2_fract){ 1, 30 };

		ret = v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
				       ESMODULE_GET_START_STREAM_SEQ,
				       &esmodule_stream_seq);
		if (ret)
			esmodule_stream_seq = ESMODULE_START_STREAM_DEFAULT;

		es_dvp2axi_sync_crop_info(stream);
	}

	ret = es_dvp2axi_sanity_check_fmt(stream, NULL);
	if (ret < 0)
		goto destroy_buf;

	mutex_lock(&hw_dev->dev_lock);
	if (atomic_read(&dev->pipe.stream_cnt) == 0 && dev->active_sensor &&
	    (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	     dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
	     dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)) {
		if (dev->channels[0].capture_info.mode ==
		    ESMODULE_MULTI_DEV_COMBINE_ONE) {
			csi_info.csi_num =
				dev->channels[0].capture_info.multi_dev.dev_num;
			if (csi_info.csi_num > ES_DVP2AXI_MAX_CSI_NUM) {
				v4l2_err(v4l2_dev, "csi num %d, max %d\n",
					 csi_info.csi_num, ES_DVP2AXI_MAX_CSI_NUM);
				goto out;
			}
			for (i = 0; i < csi_info.csi_num; i++) {
				csi_info.csi_idx[i] =
					dev->channels[0]
						.capture_info.multi_dev
						.dev_idx[i];
				if (dev->hw_dev->is_eic770xs2)
					v4l2_info(
						v4l2_dev,
						"eic770xs2 combine mode attach to mipi%d\n",
						csi_info.csi_idx[i]);
			}
		} else {
			csi_info.csi_num = 1;
			dev->csi_host_idx = dev->csi_host_idx_def;
			csi_info.csi_idx[0] = dev->csi_host_idx;
		}
		ret = v4l2_subdev_call(dev->active_sensor->sd, core, ioctl,
				       ES_DVP2AXI_CMD_SET_CSI_IDX, &csi_info);
		if (ret)
			v4l2_err(&dev->v4l2_dev, "set csi idx %d fail\n",
				 dev->csi_host_idx);
	}

	if (((dev->active_sensor &&
	      dev->active_sensor->mbus.type == V4L2_MBUS_BT656) ||
	     dev->is_use_dummybuf) &&
	    (!dev->hw_dev->dummy_buf.vaddr) &&
	    mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
		ret = es_dvp2axi_create_dummy_buf(stream);
		if (ret < 0) {
			mutex_unlock(&hw_dev->dev_lock);
			v4l2_err(v4l2_dev, "Failed to create dummy_buf, %d\n",
				 ret);
			goto destroy_buf;
		}
	}
	mutex_unlock(&hw_dev->dev_lock);

	if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
		// pr_info("%s:%d yfx!!!! enable tasklet \n", __func__, __LINE__);
		tasklet_enable(&stream->vb_done_tasklet);
	}

	if (stream->cur_stream_mode == ES_DVP2AXI_STREAM_MODE_NONE) {
		ret = dev->pipe.open(&dev->pipe, &node->vdev.entity, true);
		if (ret < 0) {
			v4l2_err(v4l2_dev, "open dvp2axi pipeline failed %d\n",
				 ret);
			goto destroy_buf;
		}

		/*
		 * start sub-devices
		 * When use bt601, the sampling edge of dvp2axi is random,
		 * can be rising or fallling after powering on dvp2axi.
		 * To keep the coherence of edge, open sensor in advance.
		 */
		if (sensor_info->mbus.type == V4L2_MBUS_PARALLEL ||
		    esmodule_stream_seq == ESMODULE_START_STREAM_FRONT) {
			ret = dev->pipe.set_stream(&dev->pipe, true);
			if (ret < 0)
				goto destroy_buf;
		}
		ret = v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
				       ESMODULE_GET_SKIP_FRAME, &skip_frame);
		if (!ret && skip_frame < ES_DVP2AXI_SKIP_FRAME_MAX)
			stream->skip_frame = skip_frame;
		else
			stream->skip_frame = 0;
		stream->cur_skip_frame = stream->skip_frame;
	}
	if (dev->active_sensor &&
		(dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
		 dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
		 dev->active_sensor->mbus.type == V4L2_MBUS_CCP2))
		ret = es_dvp2axi_csi_stream_start(stream, mode);
	else
		ret = es_dvp2axi_stream_start(stream, mode);

	if (ret < 0)
		goto destroy_buf;

	if (stream->cur_stream_mode == ES_DVP2AXI_STREAM_MODE_NONE) {
		ret = video_device_pipeline_start(&node->vdev, &dev->pipe.pipe);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "start pipeline failed %d\n",
				 ret);
			goto pipe_stream_off;
		}

		if (sensor_info->mbus.type != V4L2_MBUS_PARALLEL &&
		    esmodule_stream_seq != ESMODULE_START_STREAM_FRONT) {
			ret = dev->pipe.set_stream(&dev->pipe, true);
			if (ret < 0)
				goto stop_stream;
		}
	}
	dev->reset_work_cancel = false;
	stream->cur_stream_mode |= mode;
	es_dvp2axi_monitor_reset_event(dev);
	goto out;

stop_stream:
	es_dvp2axi_stream_stop(stream);
pipe_stream_off:
	dev->pipe.set_stream(&dev->pipe, false);

destroy_buf:
	if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE)
		tasklet_disable(&stream->vb_done_tasklet);
	if (stream->curr_buf)
		list_add_tail(&stream->curr_buf->queue, &stream->buf_head);
	if (stream->next_buf && stream->next_buf != stream->curr_buf)
		list_add_tail(&stream->next_buf->queue, &stream->buf_head);

	stream->curr_buf = NULL;
	stream->next_buf = NULL;
	atomic_set(&stream->buf_cnt, 0);
	while (!list_empty(&stream->buf_head)) {
		struct es_dvp2axi_buffer *buf;

		buf = list_first_entry(&stream->buf_head, struct es_dvp2axi_buffer,
				       queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}

out:
	mutex_unlock(&dev->stream_lock);
	return ret;
}

/* CTRL1 */
#define DVP2AXI_DVP0_PWIDTH 16
#define DVP2AXI_DVP1_PWIDTH 16

#define DVP2AXI_IO_DVP_DIS  0
#define DVP2AXI_IO_DVP_ENA  1
#define DVP2AXI_PIXEL_MODE_1WORD 0
#define DVP2AXI_PIXEL_MODE_2WORD 1
#define DVP2AXI_PIXEL_MODE_3WORD 2

/* CTRL2 */
#define DVP2AXI_DVP2_PWIDTH 16
#define DVP2AXI_DVP3_PWIDTH 16
#define DVP2AXI_DVP4_PWIDTH 16
#define DVP2AXI_DVP5_PWIDTH 16
#define DVP2AXI_OUTSTANDING_SIZE 16
#define DVP2AXI_WQOS_CFG 0

static int es_dvp2axi_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_hw *dvp2axi_hw = stream->dvp2axidev->hw_dev;

	uint32_t outstand_reg;
	// uint32_t int0, int1, int_err;
	// uint32_t stride, shnum;
	u32 dvp2axi_bpp;
	dvp2axi_bpp = (DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR) & VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK ) >> 20;

	/* outstanding 16 */
	outstand_reg = (DVP2AXI_OUTSTANDING_SIZE-1) << 24;
	DVP2AXI_HalWriteReg( dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, outstand_reg);

	#ifdef DVP2AXI_DVP0_ENABLE
	DVP2AXI_HalWriteReg( dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR,   (stream->pixm.width )| (stream->pixm.height << 16));//DVP0
	#endif
	
	uint32_t bpl = 0;
	uint32_t dvpx_bpl = 0;
	#ifdef DVP2AXI_DVP0_ENABLE
	if(stream->crop_enable) {
		bpl = ALIGN(stream->crop[CROP_SRC_ACT].width * dvp2axi_bpp / 8, 256);
		dvpx_bpl |= bpl;
	} else {
		bpl = ALIGN(stream->pixm.width * dvp2axi_bpp / 8, 256);
		dvpx_bpl |= bpl;
	}
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR, dvpx_bpl);
	dvpx_bpl = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR);
	pr_info("%s:%d dvpx_bpl:%x\n", __func__, __LINE__, dvpx_bpl);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR, dvpx_bpl);
	#endif

	int ret = 0;
	ret = es_dvp2axi_do_start_stream(stream, ES_DVP2AXI_STREAM_MODE_CAPTURE);
	stream->frame_phase = DVP2AXI_CSI_FRAME_UNREADY;
	dvp2axi_hw_int_mask(stream->dvp2axidev->hw_dev, 0);
	dvp2axi_hw_soft_reset(stream->dvp2axidev->hw_dev);
	uint32_t csr0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
	#ifdef DVP2AXI_DVP0_ENABLE
    csr0 = csr0 | 1;
    #endif
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, csr0);
	pr_info("dvp2axi enabled=0x%x\n", csr0);

	return ret;
}

static struct vb2_ops es_dvp2axi_vb2_ops = {
	.queue_setup = es_dvp2axi_queue_setup,
	.buf_queue = es_dvp2axi_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = es_dvp2axi_stop_streaming,
	.start_streaming = es_dvp2axi_start_streaming,
};

static int es_dvp2axi_init_vb2_queue(struct vb2_queue *q,
				struct es_dvp2axi_stream *stream,
				enum v4l2_buf_type buf_type)
{
	struct es_dvp2axi_hw *hw_dev = stream->dvp2axidev->hw_dev;

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &es_dvp2axi_vb2_ops;
	// q->mem_ops = hw_dev->mem_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct es_dvp2axi_buffer);
	if (stream->dvp2axidev->is_use_dummybuf)
		q->min_buffers_needed = 1;
	else
		q->min_buffers_needed = DVP2AXI_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vnode.vlock;
	q->dev = hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	// q->gfp_flags = GFP_DMA32;
	dma_set_mask_and_coherent(q->dev, DMA_BIT_MASK(41));
	if (hw_dev->is_dma_contig)
		q->dma_attrs = 0;//DMA_ATTR_FORCE_CONTIGUOUS;

	return vb2_queue_init(q);
}

int es_dvp2axi_set_fmt(struct es_dvp2axi_stream *stream,
		  struct v4l2_pix_format_mplane *pixm, bool try)
{
	// struct v4l2_subdev_format subdev_fmt;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct sditf_priv *priv = dev->sditf[0];
	const struct dvp2axi_output_fmt *fmt;
	const struct dvp2axi_input_fmt *dvp2axi_fmt_in = NULL;
	struct v4l2_rect input_rect;
	unsigned int imagesize = 0, ex_imagesize = 0, planes;
	u32 xsubs = 1, ysubs = 1, i;
	struct esmodule_hdr_cfg hdr_cfg;
	struct es_dvp2axi_extend_info *extend_line = &stream->extend_line;
	struct csi_channel_info *channel_info = &dev->channels[stream->id];
	int ret;
	struct es_dvp2axi_hw *hw_dev = stream->dvp2axidev->hw_dev;
	int fmt_w_h_val;
	// int dvp2axi_bpp;
	int dvp2axi_bpp_clear;
	int dvp2axi_mode_clear;

	for (i = 0; i < ES_DVP2AXI_MAX_PLANE; i++)
		memset(&pixm->plane_fmt[i], 0,
		       sizeof(struct v4l2_plane_pix_format));
	fmt = es_dvp2axi_find_output_fmt(stream, pixm->pixelformat);
	if (!fmt)
		fmt = &out_fmts[0];

	// input_rect.width = ES_DVP2AXI_DEFAULT_WIDTH;
	// input_rect.height = ES_DVP2AXI_DEFAULT_HEIGHT;
	input_rect.width = pixm->width;
	input_rect.height = pixm->height;
	if (dev->terminal_sensor.sd) {
		dvp2axi_fmt_in = es_dvp2axi_get_input_fmt(dev, &input_rect, stream->id,
						 channel_info);

		stream->dvp2axi_fmt_in = dvp2axi_fmt_in;
	} else {
		pr_debug("terminal subdev does not exist\n");
		return -EINVAL;
	}
	ret = es_dvp2axi_output_fmt_check(stream, fmt);
	if (ret)
		return -EINVAL;

	pr_debug("%s:%d input_rect.width:%d height:%d \n", __func__, __LINE__, input_rect.width, input_rect.height);
	fmt_w_h_val = (input_rect.height << 16) | (input_rect.width);
	DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL3_CSR, fmt_w_h_val);

	if (dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_HDR_CFG, &hdr_cfg);
		if (!ret)
			dev->hdr = hdr_cfg;
		else
			dev->hdr.hdr_mode = NO_HDR;

		dev->terminal_sensor.raw_rect = input_rect;
	}

	/* DVP2AXI has not scale function,
	 * the size should not be larger than input
	 */
	pr_debug("input_rect.width = 0x%x \n", input_rect.width);
	pr_debug("input_rect.height = 0x%x \n", input_rect.height);
	pr_debug("pixm->width = 0x%x \n", pixm->width);
	pr_debug("pixm->height = 0x%x \n", pixm->height);
	pixm->width =
		clamp_t(u32, pixm->width, DVP2AXI_MIN_WIDTH, input_rect.width);
	pixm->height =
		clamp_t(u32, pixm->height, DVP2AXI_MIN_HEIGHT, input_rect.height);
	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	pixm->quantization = V4L2_QUANTIZATION_DEFAULT;
	es_dvp2axi_sync_crop_info(stream);
	/* calculate plane size and image size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;

	if (dvp2axi_fmt_in && (dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8 ||
			   dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8))
		stream->crop_enable = false;

	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		int width, height, bpl, size, bpp, ex_size;

		if (i == 0) {
			if (stream->crop_enable) {
				width = stream->crop[CROP_SRC_ACT].width;
				height = stream->crop[CROP_SRC_ACT].height;
			} else {
				width = pixm->width;
				height = pixm->height;
			}
		} else {
			if (stream->crop_enable) {
				width = stream->crop[CROP_SRC_ACT].width /
					xsubs;
				height = stream->crop[CROP_SRC_ACT].height /
					 ysubs;
			} else {
				width = pixm->width / xsubs;
				height = pixm->height / ysubs;
			}
		}

		if (priv && priv->is_combine_mode &&
		    dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF)
			height *= dev->sditf_cnt;

		extend_line->pixm.height = height + ESMODULE_EXTEND_LINE;

		/* compact mode need bytesperline 4bytes align,
		 * align 8 to bring into correspondence with virtual width.
		 * to optimize reading and writing of ddr, aliged with 256.
		 */
		if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW && dvp2axi_fmt_in &&
		    (dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8 ||
		     dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8)) {
			stream->is_compact = false;
		}
		pr_debug("*** modet fmt->fmt_type = 0x%x ***\n",fmt->fmt_type);
		if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW && stream->is_compact &&
		    (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
		     dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
		     dev->active_sensor->mbus.type == V4L2_MBUS_CCP2) &&
		    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
		    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565) {
			// bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
			// dvp2axi_bpp = (readl(dev->hw_dev->base_addr+VI_DVP2AXI_CTRL1_CSR) & VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK ) >> 20;
			// if(dvp2axi_bpp == 16) {
			// 	bpl = ALIGN(width * ALIGN(fmt->raw_bpp, 16) / 8, 256);
			// } else {
			// 	bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
			// }
			dvp2axi_bpp_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK;
			if(fmt->raw_bpp == 10) {
				bpl = ALIGN(width * ALIGN(fmt->raw_bpp, 16) / 8, 256);
				DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_bpp_clear | ((16 & 0x1F) << 20));
			} else {
				bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
				DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_bpp_clear | ((fmt->raw_bpp & 0x1F) << 20));
			}
			dvp2axi_mode_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~0x300;
			DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_mode_clear | ((DVP_PIXEL_MODE_RAW & 0x3) << 8));
		} else {
			if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW &&
			    stream->is_compact &&
			    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
			    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565 &&
			    dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
				bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
			} else {
				if(fmt->fmt_type == DVP2AXI_FMT_TYPE_YUV){
					//YUV
					pr_debug("*** mode yuv ***\n");
					dvp2axi_mode_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~0x300;
					DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_mode_clear | ((DVP_PIXEL_MODE_YUV & 0x3) << 8));
					bpp = es_dvp2axi_align_bits_per_pixel(stream, fmt,
										i);
					dvp2axi_bpp_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK;
					DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_bpp_clear | ((bpp & 0x1F) << 20));
					bpl = width * bpp / DVP2AXI_YUV_STORED_BIT_WIDTH;
					pr_debug("*** mode yuv width = 0x%x ***\n", width);
					pr_debug("*** mode yuv bpp = 0x%x ***\n", bpp);
					pr_debug("*** mode yuv DVP2AXI_YUV_STORED_BIT_WIDTH = 0x%x ***\n", DVP2AXI_YUV_STORED_BIT_WIDTH);
					pr_debug("*** mode yuv bpl = 0x%x ***\n", bpl);
				}
				if(fmt->csi_fmt_val == CSI_WRDDR_TYPE_RGB888){
					//RGB888
					dvp2axi_mode_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~0x300;
					DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_mode_clear | ((DVP_PIXEL_MODE_RGB & 0x3) << 8));
					bpp = es_dvp2axi_align_bits_per_pixel(stream, fmt,
										i);
					dvp2axi_bpp_clear = DVP2AXI_HalReadReg(hw_dev, VI_DVP2AXI_CTRL1_CSR) & ~VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK;
					DVP2AXI_HalWriteReg(hw_dev, VI_DVP2AXI_CTRL1_CSR, dvp2axi_bpp_clear | ((bpp & 0x1F) << 20));
					bpl = width * bpp / DVP2AXI_YUV_STORED_BIT_WIDTH;
					pr_debug("*** mode rgb width = 0x%x ***\n", width);
					pr_debug("*** mode rgb bpp = 0x%x ***\n", bpp);
					pr_debug("*** mode rgb DVP2AXI_YUV_STORED_BIT_WIDTH = 0x%x ***\n", DVP2AXI_YUV_STORED_BIT_WIDTH);
					pr_debug("*** mode rgb bpl = 0x%x ***\n", bpl);
				}
			}
		}
		
		pr_debug("***** bpl = 0x%x *****\n", bpl);
		
		size = bpl * height;
		imagesize += size;
		ex_size = bpl * extend_line->pixm.height;
		ex_imagesize += ex_size;

		pr_debug("*** mode size = 0x%x ***\n", size);
		pr_debug("*** mode imagesize = 0x%x ***\n", imagesize);
		pr_debug("*** mode ex_size = 0x%x ***\n", ex_size);
		pr_debug("*** mode ex_imagesize = 0x%x ***\n", ex_imagesize);

		if (fmt->mplanes > i) {
			/* Set bpl and size for each mplane */
			plane_fmt = pixm->plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = size;

			plane_fmt = extend_line->pixm.plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = ex_size;
		}
		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "C-Plane %i size: %d, Total imagesize: %d\n", i, size,
			 imagesize);
	}

	/* convert to non-MPLANE format.
	 * It's important since we want to unify non-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1) {
		pixm->plane_fmt[0].sizeimage = imagesize;
		extend_line->pixm.plane_fmt[0].sizeimage = ex_imagesize;
	}

	if (!try) {
		stream->dvp2axi_fmt_out = fmt;
		stream->pixm = *pixm;

		v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
			 "%s: req(%d, %d) out(%d, %d)\n", __func__, pixm->width,
			 pixm->height, stream->pixm.width, stream->pixm.height);
	}
	return 0;
}

void es_dvp2axi_stream_init(struct es_dvp2axi_device *dev, u32 id)
{
	struct es_dvp2axi_stream *stream = &dev->stream[id];
	struct v4l2_pix_format_mplane pixm;
	int i;

	memset(stream, 0, sizeof(*stream));
	memset(&pixm, 0, sizeof(pixm));
	stream->id = id;
	stream->dvp2axidev = dev;

	INIT_LIST_HEAD(&stream->buf_head);
	INIT_LIST_HEAD(&stream->rx_buf_head);
	INIT_LIST_HEAD(&stream->rx_buf_head_vicap);
	INIT_LIST_HEAD(&stream->eskit_buf_head);
	spin_lock_init(&stream->vbq_lock);
	spin_lock_init(&stream->fps_lock);
	stream->state = ES_DVP2AXI_STATE_READY;
	init_waitqueue_head(&stream->wq_stopped);

	/* Set default format */
	pixm.pixelformat = V4L2_PIX_FMT_NV12;
	pixm.width = ES_DVP2AXI_DEFAULT_WIDTH;
	pixm.height = ES_DVP2AXI_DEFAULT_HEIGHT;
	es_dvp2axi_set_fmt(stream, &pixm, false);

	for (i = 0; i < CROP_SRC_MAX; i++) {
		stream->crop[i].left = 0;
		stream->crop[i].top = 0;
		stream->crop[i].width = ES_DVP2AXI_DEFAULT_WIDTH;
		stream->crop[i].height = ES_DVP2AXI_DEFAULT_HEIGHT;
	}

	stream->crop_enable = false;
	stream->crop_dyn_en = false;
	stream->crop_mask = 0x0;

	if (dev->inf_id == ES_DVP2AXI_DVP) {
		stream->is_compact = true;
	} else {
		stream->is_compact = true;
	}

	stream->is_high_align = false;
	stream->is_finish_stop_dma = false;
	stream->is_wait_dma_stop = false;

	stream->extend_line.is_extended = false;

	stream->is_dvp_yuv_addr_init = false;
	stream->is_fs_fe_not_paired = false;
	stream->fs_cnt_in_single_frame = 0;
	if (dev->wait_line) {
		dev->wait_line_cache = dev->wait_line;
		dev->wait_line_bak = dev->wait_line;
		stream->is_line_wake_up = true;
	} else {
		stream->is_line_wake_up = false;
		dev->wait_line_cache = 0;
		dev->wait_line_bak = 0;
	}
	stream->cur_stream_mode = 0;
	stream->dma_en = 0;
	stream->to_en_dma = 0;
	stream->to_stop_dma = 0;
	stream->to_en_scale = false;
	stream->buf_owner = 0;
	stream->buf_replace_cnt = 0;
	stream->is_stop_capture = false;
	stream->is_single_cap = false;
	atomic_set(&stream->buf_cnt, 0);
	stream->rx_buf_num = 0;
	init_completion(&stream->stop_complete);
	stream->is_wait_stop_complete = false;
}

static int es_dvp2axi_fh_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_stream *stream = to_es_dvp2axi_stream(vnode);
	struct es_dvp2axi_device *dvp2axidev = stream->dvp2axidev;
	int ret;
	int i = 0;

	stream->is_first_flush = true;
	ret = es_dvp2axi_attach_hw(dvp2axidev);
	if (ret)
		return ret;

	/* Make sure active sensor is valid before .set_fmt() */
	ret = es_dvp2axi_update_sensor_info(stream);
	if (ret < 0) {
		v4l2_dbg(1, es_dvp2axi_debug, vdev, "update sensor info failed %d\n", ret);

		return ret;
	}

	ret = pm_runtime_resume_and_get(dvp2axidev->dev);
	if (ret < 0) {
		v4l2_err(vdev, "Failed to get runtime pm, %d\n", ret);
		return ret;
	}

	ret = v4l2_fh_open(filp);
	if (!ret) {
		mutex_lock(&dvp2axidev->stream_lock);
		ret = v4l2_pipeline_pm_get(&vnode->vdev.entity);
		v4l2_info(vdev, "open video, entity use_countt %d\n",
			  vnode->vdev.entity.use_count);
		mutex_unlock(&dvp2axidev->stream_lock);
		if (ret < 0)
			vb2_fop_release(filp);
	}
	if (dvp2axidev->sditf_cnt > 1) {
		for (i = 0; i < dvp2axidev->sditf_cnt; i++) {
			if (dvp2axidev->sditf[i]->sensor_sd)
				ret |= v4l2_subdev_call(
					dvp2axidev->sditf[i]->sensor_sd, core,
					s_power, 1);
		}
		if (ret < 0)
			v4l2_err(vdev, "set sensor power on fail, ret %d\n",
				 ret);
	}
	return ret;
}

static int es_dvp2axi_fh_release(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_stream *stream = to_es_dvp2axi_stream(vnode);
	struct es_dvp2axi_device *dvp2axidev = stream->dvp2axidev;
	int ret = 0;
	int i = 0;

	stream->is_first_flush = true;

	ret = vb2_fop_release(filp);
	if (!ret) {
		mutex_lock(&dvp2axidev->stream_lock);
		v4l2_pipeline_pm_put(&vnode->vdev.entity);
		v4l2_info(vdev, "close video, entity use_count %d\n",
			  vnode->vdev.entity.use_count);
			// dump_stack();
		mutex_unlock(&dvp2axidev->stream_lock);
	}

	pm_runtime_put_sync(dvp2axidev->dev);
	if (dvp2axidev->sditf_cnt > 1) {
		for (i = 0; i < dvp2axidev->sditf_cnt; i++) {
			if (dvp2axidev->sditf[i]->sensor_sd)
				ret |= v4l2_subdev_call(
					dvp2axidev->sditf[i]->sensor_sd, core,
					s_power, 0);
		}
		if (ret < 0)
			v4l2_err(vdev, "set sensor power on fail, ret %d\n",
				 ret);
	}
	return ret;
}

unsigned int eic770x_vb2_fop_poll(struct file *file, poll_table *wait)
{
	int int0, int1, int_err;
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct es_dvp2axi_hw	*dvp2axi_hw =  stream->dvp2axidev->hw_dev;
	//unsigned long req_events = poll_requested_events(wait);
	unsigned int mask = 0;
	int buf_idx = 0;
	for(int i=0; i<100; i++) {
		int0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR);
		int1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR);
		int_err = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR);

		if ((int0 >> 9) & 0x7) {  // done handle
			// clear interrupt
			DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, int0);
			//send q to donelist
			buf_idx = (((int0 >> 9) & 0x7) & 0x1) ? 0 : (((int0 >> 9) & 0x7) & 0x2)? 1: 2;
			vdev->queue->bufs[buf_idx]->state = VB2_BUF_STATE_DONE;
			vdev->queue->bufs[buf_idx]->planes[0].bytesused = stream->pixm.plane_fmt[0].sizeimage;
			list_add_tail(&vdev->queue->bufs[buf_idx]->done_entry, &vdev->queue->done_list); 
			mask = POLLIN | POLLRDNORM;
			break;
		}

		if (int0 & 0x1ff) {  // flush handle
			// clear interrupt
			DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, int0);
		}
		if (int1) {  // done handle
			// clear interrupt
			DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, int1);
			mask = POLLIN | POLLRDNORM;
			break;

		}

		if (int1 & 0x1ff) {  // flush handle
			// clear interrupt
			DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, int1);
		}

		if (int_err) {  // axi error: reset required
			DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR, int_err);
		}
	}
	return mask;
}

void dvp2axi_hw_int_mask(struct es_dvp2axi_hw *dvp2axi_hw, int mask)
{

	
    if(mask) {
		pr_info("%s: mask dvp2axi interrupt\n", __func__);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR, 0xffffffff);
    	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR, 0xffffffff);
    	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, 0xffffffff);
	} else {
		pr_info("%s: unmask dvp2axi interrupt\n", __func__);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR, 0x0);
    	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR, 0x0);
    	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, 0x0);
	}

}


static const struct v4l2_file_operations es_dvp2axi_fops = {
	.open = es_dvp2axi_fh_open,
	.release = es_dvp2axi_fh_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	// .poll = eic770x_vb2_fop_poll,
	.mmap = vb2_fop_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
};

static int es_dvp2axi_enum_input(struct file *file, void *priv,
			    struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int es_dvp2axi_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	int ret = 0;
	ret = es_dvp2axi_set_fmt(stream, &f->fmt.pix_mp, true);
	return ret;
}

static int es_dvp2axi_enum_framesizes(struct file *file, void *prov,
				 struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_frmsize_discrete *d = &fsize->discrete;
	struct v4l2_frmsize_stepwise *s = &fsize->stepwise;
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_rect input_rect;
	struct csi_channel_info csi_info;
	
	
	if (fsize->index != 0)
		return -EINVAL;

	pr_debug("***** fsize->pixel_format =0x%x *****\n", fsize->pixel_format);
	if (!es_dvp2axi_find_output_fmt(stream, fsize->pixel_format))
		return -EINVAL;

	input_rect.width = ES_DVP2AXI_DEFAULT_WIDTH;
	input_rect.height = ES_DVP2AXI_DEFAULT_HEIGHT;

	if (dev->terminal_sensor.sd)
		es_dvp2axi_get_input_fmt(dev, &input_rect, stream->id, &csi_info);

	if (dev->hw_dev->adapt_to_usbcamerahal) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		d->width = input_rect.width;
		d->height = input_rect.height;
	} else {
		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		s->min_width = DVP2AXI_MIN_WIDTH;
		s->min_height = DVP2AXI_MIN_HEIGHT;
		s->max_width = input_rect.width;
		s->max_height = input_rect.height;
		s->step_width = OUTPUT_STEP_WISE;
		s->step_height = OUTPUT_STEP_WISE;
	}

	return 0;
}

static int es_dvp2axi_enum_frameintervals(struct file *file, void *fh,
				     struct v4l2_frmivalenum *fival)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev_frame_interval fi;
	int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor || !sensor->sd) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(&dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	if (ret && ret != -ENOIOCTLCMD) {
		return ret;
	} else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	}

	if (dev->hw_dev->adapt_to_usbcamerahal) {
		fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		fival->discrete.numerator = fi.interval.numerator;
		fival->discrete.denominator = fi.interval.denominator;
	} else {
		fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
		fival->stepwise.step.numerator = 1;
		fival->stepwise.step.denominator = 1;
		fival->stepwise.max.numerator = 1;
		fival->stepwise.max.denominator = 1;
		fival->stepwise.min.numerator = fi.interval.numerator;
		fival->stepwise.min.denominator = fi.interval.denominator;
	}

	return 0;
}

static int es_dvp2axi_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_fmtdesc *f)
{
	const struct dvp2axi_output_fmt *fmt = NULL;
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	const struct dvp2axi_input_fmt *dvp2axi_fmt_in = NULL;
	struct v4l2_rect input_rect;
	int i = 0;
	int ret = 0;
	int fource_idx = 0;

	if (f->index >= ARRAY_SIZE(out_fmts))
		return -EINVAL;

	if (dev->terminal_sensor.sd) {
		dvp2axi_fmt_in = es_dvp2axi_get_input_fmt(dev, &input_rect, stream->id,
						 &dev->channels[stream->id]);
		stream->dvp2axi_fmt_in = dvp2axi_fmt_in;
		pr_debug("*** stream->dvp2axi_fmt_in->mbus_code = %d ***\n", stream->dvp2axi_fmt_in->mbus_code);
	} else {
		pr_debug("terminal subdev does not exist\n");
		return -EINVAL;
	}

	if (f->index != 0)
		fource_idx = stream->new_fource_idx;

	for (i = fource_idx; i < ARRAY_SIZE(out_fmts); i++) {
		fmt = &out_fmts[i];
		ret = es_dvp2axi_output_fmt_check(stream, fmt);
		if (!ret) {
			f->pixelformat = fmt->fourcc;
			stream->new_fource_idx = i + 1;
			break;
		}
	}
	if (i == ARRAY_SIZE(out_fmts))
		return -EINVAL;

	switch (f->pixelformat) {
	case V4l2_PIX_FMT_EBD8:
		strscpy(f->description, "Embedded data 8-bit",
			sizeof(f->description));
		break;
	case V4l2_PIX_FMT_SPD16:
		strscpy(f->description, "Shield pix data 16-bit",
			sizeof(f->description));
		break;
	default:
		break;
	}
	return 0;
}

static int es_dvp2axi_s_fmt_vid_cap_mplane(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	int ret = 0;

	if (vb2_is_busy(&stream->vnode.buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}
	ret = es_dvp2axi_set_fmt(stream, &f->fmt.pix_mp, false);
	return ret;
}

static int es_dvp2axi_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	f->fmt.pix_mp = stream->pixm;

	return 0;
}

static int es_dvp2axi_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct device *dev = stream->dvp2axidev->dev;

	strlcpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strlcpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(dev));

	return 0;
}

static __maybe_unused int es_dvp2axi_cropcap(struct file *file, void *fh,
					struct v4l2_cropcap *cap)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	int ret = 0;

	if (stream->crop_mask & CROP_SRC_SENSOR) {
		cap->bounds.left = stream->crop[CROP_SRC_SENSOR].left;
		cap->bounds.top = stream->crop[CROP_SRC_SENSOR].top;
		cap->bounds.width = stream->crop[CROP_SRC_SENSOR].width;
		cap->bounds.height = stream->crop[CROP_SRC_SENSOR].height;
	} else {
		cap->bounds.left = raw_rect->left;
		cap->bounds.top = raw_rect->top;
		cap->bounds.width = raw_rect->width;
		cap->bounds.height = raw_rect->height;
	}

	cap->defrect = cap->bounds;
	cap->pixelaspect.numerator = 1;
	cap->pixelaspect.denominator = 1;

	return ret;
}

static int es_dvp2axi_s_selection(struct file *file, void *fh,
			     struct v4l2_selection *s)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_subdev_selection sd_sel;
	const struct v4l2_rect *rect = &s->r;
	struct v4l2_rect sensor_crop;
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	u16 pad = 0;
	int ret = 0;

	if (!s) {
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev, "sel is null\n");
		goto err;
	}

	if (s->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sensor_sd = get_remote_sensor(stream, &pad);

		sd_sel.r = s->r;
		sd_sel.pad = pad;
		sd_sel.target = s->target;
		sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		pr_debug("%s:%d s->r.left %d,  s->r.top %d,  s->r.width %d, s->r.height %d \n", __func__, __LINE__, s->r.left, s->r.top, s->r.width, s->r.height);
		ret = v4l2_subdev_call(sensor_sd, pad, set_selection, NULL,
				       &sd_sel);
		if (!ret) {
			s->r = sd_sel.r;
			v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
				 "%s: pad:%d, which:%d, target:%d\n", __func__,
				 pad, sd_sel.which, sd_sel.target);
		}
	} else if (s->target == V4L2_SEL_TGT_CROP) {
		pr_debug("%s:%d start check fmt\n", __func__, __LINE__);
		ret = es_dvp2axi_sanity_check_fmt(stream, rect);
		if (ret) {
			v4l2_err(&dev->v4l2_dev, "set crop failed\n");
			return ret;
		}

		if (stream->crop_mask & CROP_SRC_SENSOR) {
			sensor_crop = stream->crop[CROP_SRC_SENSOR];
			if (rect->left + rect->width > sensor_crop.width ||
			    rect->top + rect->height > sensor_crop.height) {
				v4l2_err(
					&dev->v4l2_dev,
					"crop size is bigger than sensor input:left:%d, top:%d, width:%d, height:%d\n",
					sensor_crop.left, sensor_crop.top,
					sensor_crop.width, sensor_crop.height);
				return -EINVAL;
			}
		} else {
			if (rect->left + rect->width > raw_rect->width ||
			    rect->top + rect->height > raw_rect->height) {
				v4l2_err(
					&dev->v4l2_dev,
					"crop size is bigger than sensor raw input:left:%d, top:%d, width:%d, height:%d\n",
					raw_rect->left, raw_rect->top,
					raw_rect->width, raw_rect->height);
				return -EINVAL;
			}
		}

		stream->crop[CROP_SRC_USR] = *rect;
		stream->crop_enable = true;
		stream->crop_mask |= CROP_SRC_USR_MASK;
		stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_USR];
		if (stream->crop_mask & CROP_SRC_SENSOR) {
			sensor_crop = stream->crop[CROP_SRC_SENSOR];
			stream->crop[CROP_SRC_ACT].left =
				sensor_crop.left +
				stream->crop[CROP_SRC_USR].left;
			stream->crop[CROP_SRC_ACT].top =
				sensor_crop.top +
				stream->crop[CROP_SRC_USR].top;
		}

		if (stream->state == ES_DVP2AXI_STATE_STREAMING) {
			stream->crop_dyn_en = true;

			v4l2_info(
				&dev->v4l2_dev,
				"enable dynamic crop, S_SELECTION(%ux%u@%u:%u) target: %d\n",
				rect->width, rect->height, rect->left,
				rect->top, s->target);
		} else {
			v4l2_info(
				&dev->v4l2_dev,
				"static crop, S_SELECTION(%ux%u@%u:%u) target: %d\n",
				rect->width, rect->height, rect->left,
				rect->top, s->target);
		}
	} else {
		goto err;
	}

	return ret;

err:
	return -EINVAL;
}

static int es_dvp2axi_g_selection(struct file *file, void *fh,
			     struct v4l2_selection *s)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_subdev_selection sd_sel;
	u16 pad = 0;
	int ret = 0;

	if (!s) {
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev, "sel is null\n");
		goto err;
	}

	if (s->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sensor_sd = get_remote_sensor(stream, &pad);

		sd_sel.pad = pad;
		sd_sel.target = s->target;
		sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;

		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
			 "%s(line:%d): sd:%s pad:%d, which:%d, target:%d\n",
			 __func__, __LINE__, sensor_sd->name, pad, sd_sel.which,
			 sd_sel.target);

		ret = v4l2_subdev_call(sensor_sd, pad, get_selection, NULL,
				       &sd_sel);
		if (!ret) {
			s->r = sd_sel.r;
		} else {
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = stream->pixm.width;
			s->r.height = stream->pixm.height;
		}
	} else if (s->target == V4L2_SEL_TGT_CROP) {
		if (stream->crop_mask &
		    (CROP_SRC_USR_MASK | CROP_SRC_SENSOR_MASK)) {
			s->r = stream->crop[CROP_SRC_ACT];
		} else {
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = stream->pixm.width;
			s->r.height = stream->pixm.height;
		}
	} else {
		goto err;
	}

	return ret;
err:
	return -EINVAL;
}

static int es_dvp2axi_get_max_common_div(int a, int b)
{
	int remainder = a % b;

	while (remainder != 0) {
		a = b;
		b = remainder;
		remainder = a % b;
	}
	return b;
}

void es_dvp2axi_set_fps(struct es_dvp2axi_stream *stream, struct es_dvp2axi_fps *fps)
{
	struct es_dvp2axi_sensor_info *sensor = &stream->dvp2axidev->terminal_sensor;
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct es_dvp2axi_stream *tmp_stream = NULL;
	u32 numerator, denominator;
	u32 def_fps = 0;
	u32 cur_fps = 0;
	int cap_m, skip_n;
	int i = 0;
	int max_common_div;
	bool skip_en = false;
	s32 vblank_def = 0;
	s32 vblank_curr = 0;
	int ret = 0;

	if (!stream->dvp2axidev->terminal_sensor.sd) {
		ret = es_dvp2axi_update_sensor_info(stream);
		if (ret) {
			v4l2_dbg(1, es_dvp2axi_debug ,&stream->dvp2axidev->v4l2_dev,
				 "%s update sensor info fail\n", __func__);
			return;
		}
	}
	if (!stream->dvp2axidev->terminal_sensor.sd)
		return;
	numerator = sensor->fi.interval.numerator;
	denominator = sensor->fi.interval.denominator;
	def_fps = denominator / numerator;

	vblank_def = es_dvp2axi_get_sensor_vblank_def(dvp2axi_dev);
	vblank_curr = es_dvp2axi_get_sensor_vblank(dvp2axi_dev);
	if (vblank_def)
		cur_fps = def_fps *
			  (u32)(vblank_def + sensor->raw_rect.height) /
			  (u32)(vblank_curr + sensor->raw_rect.height);
	else
		cur_fps = def_fps;

	if (fps->fps == 0 || fps->fps > cur_fps) {
		v4l2_err(&stream->dvp2axidev->v4l2_dev,
			 "set fps %d fps failed, current fps %d fps\n",
			 fps->fps, cur_fps);
		return;
	}
	cap_m = fps->fps;
	skip_n = cur_fps - fps->fps;
	max_common_div = es_dvp2axi_get_max_common_div(cap_m, skip_n);
	cap_m /= max_common_div;
	skip_n /= max_common_div;
	if (cap_m > 64) {
		skip_n = skip_n / (cap_m / 64);
		if (skip_n == 0)
			skip_n = 1;
		cap_m = 64;
	}
	if (skip_n > 7) {
		cap_m = cap_m / (skip_n / 7);
		if (cap_m == 0)
			cap_m = 1;
		skip_n = 7;
	}

	if (fps->fps == cur_fps)
		skip_en = false;
	else
		skip_en = true;

	if (fps->ch_num > 1 && fps->ch_num < 4) {
		for (i = 0; i < fps->ch_num; i++) {
			tmp_stream = &dvp2axi_dev->stream[i];
			if (skip_en) {
				tmp_stream->skip_info.skip_to_en = true;
				tmp_stream->skip_info.cap_m = cap_m;
				tmp_stream->skip_info.skip_n = skip_n;
			} else {
				tmp_stream->skip_info.skip_to_dis = true;
			}
		}
	} else {
		if (skip_en) {
			stream->skip_info.skip_to_en = true;
			stream->skip_info.cap_m = cap_m;
			stream->skip_info.skip_n = skip_n;
		} else {
			stream->skip_info.skip_to_dis = true;
		}
	}
	v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "skip_to_en %d, cap_m %d, skip_n %d\n",
		 stream->skip_info.skip_to_en, cap_m, skip_n);
}

static bool es_dvp2axi_check_can_be_online(struct es_dvp2axi_device *dvp2axi_dev)
{
	return true;
}

static int es_dvp2axi_do_reset_work(struct es_dvp2axi_device *dvp2axi_dev,
			       enum esmodule_reset_src reset_src);
static long es_dvp2axi_ioctl_default(struct file *file, void *fh, bool valid_prio,
				unsigned int cmd, void *arg)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	const struct dvp2axi_input_fmt *in_fmt;
	struct v4l2_rect rect;
	struct csi_channel_info csi_info;
	struct es_dvp2axi_fps fps;
	int reset_src;
	struct es_dvp2axi_quick_stream_param *stream_param;
	bool is_single_dev = false;
	struct v4l2_subdev *sd;
	int ret = -EINVAL;
	int i = 0;
	int stream_num = 0;
	bool is_can_be_online = false;

	switch (cmd) {
	case ES_DVP2AXI_CMD_GET_CSI_MEMORY_MODE:
		if (stream->is_compact) {
			*(int *)arg = CSI_LVDS_MEM_COMPACT;
		} else {
			if (stream->is_high_align)
				*(int *)arg = CSI_LVDS_MEM_WORD_HIGH_ALIGN;
			else
				*(int *)arg = CSI_LVDS_MEM_WORD_LOW_ALIGN;
		}
		break;
	case ES_DVP2AXI_CMD_SET_CSI_MEMORY_MODE:
		if (dev->terminal_sensor.sd) {
			in_fmt = es_dvp2axi_get_input_fmt(dev, &rect, 0, &csi_info);
			if (in_fmt == NULL) {
				v4l2_err(&dev->v4l2_dev,
					 "can't get sensor input format\n");
				return -EINVAL;
			}
		} else {
			v4l2_err(&dev->v4l2_dev, "can't get sensor device\n");
			return -EINVAL;
		}
		if (*(int *)arg == CSI_LVDS_MEM_COMPACT) {
			stream->is_compact = true;
			stream->is_high_align = false;
		} else if (*(int *)arg == CSI_LVDS_MEM_WORD_HIGH_ALIGN) {
			stream->is_compact = false;
			stream->is_high_align = true;
		} else {
			stream->is_compact = false;
			stream->is_high_align = false;
		}
		break;
	case ES_DVP2AXI_CMD_SET_FPS:
		fps = *(struct es_dvp2axi_fps *)arg;
		es_dvp2axi_set_fps(stream, &fps);
		break;
	case ES_DVP2AXI_CMD_SET_RESET:
		reset_src = *(int *)arg;
		return es_dvp2axi_do_reset_work(dev, reset_src);
	case ES_DVP2AXI_CMD_SET_QUICK_STREAM:
		stream_param = (struct es_dvp2axi_quick_stream_param *)arg;
		if (!dev->sditf[0])
			return -EINVAL;
		if (dev->hdr.hdr_mode == HDR_X2)
			stream_num = 2;
		else if (dev->hdr.hdr_mode == HDR_X3)
			stream_num = 3;
		else
			stream_num = 1;
		if (stream_param->on) {
			for (i = 0; i < stream_num; i++)
				dev->stream[i].cur_skip_frame =
					dev->stream[i].skip_frame;
			is_single_dev =
				es_dvp2axi_check_single_dev_stream_on(dev->hw_dev);
			is_can_be_online = es_dvp2axi_check_can_be_online(dev);
			if (is_single_dev && is_can_be_online) {
				for (i = 0; i < stream_num - 1; i++) {
					dev->stream[i].to_en_dma =
						ES_DVP2AXI_DMAEN_BY_ISP;
					es_dvp2axi_enable_dma_capture(
						&dev->stream[i], true);
				}
				dev->sditf[0]->mode.rdbk_mode =
					ESISP_VICAP_ONLINE;
				sditf_change_to_online(dev->sditf[0]);
				sd = get_esisp_sd(dev->sditf[0]);
				if (sd)
					ret = v4l2_subdev_call(
						sd, core, ioctl,
						ESISP_VICAP_CMD_MODE,
						&dev->sditf[0]->mode);
				if (ret) {
					v4l2_err(
						&dev->v4l2_dev,
						"set isp work mode online fail\n");
					return -EINVAL;
				}
			} else {
				sditf_disable_immediately(dev->sditf[0]);
				dev->sditf[0]->mode.rdbk_mode =
					ESISP_VICAP_RDBK_AUTO;
				sd = get_esisp_sd(dev->sditf[0]);
				if (sd)
					ret = v4l2_subdev_call(
						sd, core, ioctl,
						ESISP_VICAP_CMD_MODE,
						&dev->sditf[0]->mode);
				for (i = 0; i < stream_num; i++) {
					if (dev->sditf[0]->mode.rdbk_mode ==
					    ESISP_VICAP_RDBK_AUTO)
						dev->stream[i].to_en_dma =
							ES_DVP2AXI_DMAEN_BY_ISP;
					else
						dev->stream[i].to_en_dma =
							ES_DVP2AXI_DMAEN_BY_VICAP;
					es_dvp2axi_enable_dma_capture(
						&dev->stream[i], true);
				}
			}
			es_dvp2axi_dphy_quick_stream(dev, stream_param->on);
			v4l2_subdev_call(dev->terminal_sensor.sd, core, ioctl,
					 ESMODULE_SET_QUICK_STREAM,
					 &stream_param->on);
		} else {
			if (dev->sditf[0]->mode.rdbk_mode ==
			    ESISP_VICAP_ONLINE) {
				for (i = 0; i < stream_num - 1; i++) {
					reinit_completion(
						&dev->stream[i].stop_complete);
					dev->stream[i].is_wait_stop_complete =
						true;
					dev->stream[i].to_stop_dma =
						ES_DVP2AXI_DMAEN_BY_ISP;
					wait_for_completion_timeout(
						&dev->stream[i].stop_complete,
						msecs_to_jiffies(
							ES_DVP2AXI_STOP_MAX_WAIT_TIME_MS));
				}
				stream->dvp2axidev->sensor_state = stream_param->on;
				stream->dvp2axidev->sensor_state_change = true;
				dev->stream[i].is_wait_stop_complete = true;
				wait_for_completion_timeout(
					&dev->stream[i].stop_complete,
					msecs_to_jiffies(
						ES_DVP2AXI_STOP_MAX_WAIT_TIME_MS));
			} else {
				for (i = 0; i < stream_num; i++) {
					dev->stream[i].is_wait_stop_complete =
						true;
					reinit_completion(
						&dev->stream[i].stop_complete);
					if (dev->sditf[0]->mode.rdbk_mode ==
					    ESISP_VICAP_RDBK_AUTO)
						dev->stream[i].to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_ISP;
					else
						dev->stream[i].to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_VICAP;
					wait_for_completion_timeout(
						&dev->stream[i].stop_complete,
						msecs_to_jiffies(
							ES_DVP2AXI_STOP_MAX_WAIT_TIME_MS));
				}
				es_dvp2axi_dphy_quick_stream(dev, stream_param->on);
				v4l2_subdev_call(dev->terminal_sensor.sd, core,
						 ioctl,
						 ESMODULE_SET_QUICK_STREAM,
						 &stream_param->on);
			}
			stream_param->frame_num = dev->stream[0].frame_idx - 1;
			if (!dev->is_rtt_suspend) {
				dev->resume_mode = stream_param->resume_mode;
				v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
					 "set resume mode %d\n",
					 dev->resume_mode);
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ioctl_ops es_dvp2axi_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = es_dvp2axi_enum_input,
	.vidioc_try_fmt_vid_cap_mplane = es_dvp2axi_try_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = es_dvp2axi_enum_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = es_dvp2axi_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = es_dvp2axi_g_fmt_vid_cap_mplane,
	.vidioc_querycap = es_dvp2axi_querycap,
	.vidioc_s_selection = es_dvp2axi_s_selection,
	.vidioc_g_selection = es_dvp2axi_g_selection,
	.vidioc_enum_frameintervals = es_dvp2axi_enum_frameintervals,
	.vidioc_enum_framesizes = es_dvp2axi_enum_framesizes,
	.vidioc_default = es_dvp2axi_ioctl_default,
};

void es_dvp2axi_vb_done_oneframe(struct es_dvp2axi_stream *stream,
			    struct vb2_v4l2_buffer *vb_done)
{
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;
	u32 i;

	/* Dequeue a filled buffer */
	for (i = 0; i < fmt->mplanes; i++) {
		vb2_set_plane_payload(&vb_done->vb2_buf, i,
				      stream->pixm.plane_fmt[i].sizeimage);
	}

	// pr_info("%s:%d yfx!!!!! \n", __func__, __LINE__);
	vb2_buffer_done(&vb_done->vb2_buf, VB2_BUF_STATE_DONE);
	v4l2_dbg(2, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "stream[%d] vb done, index: %d, sequence %d\n", stream->id,
		 vb_done->vb2_buf.index, vb_done->sequence);
	atomic_dec(&stream->buf_cnt);
}

static void es_dvp2axi_tasklet_handle(unsigned long data)
{
	struct es_dvp2axi_stream *stream = (struct es_dvp2axi_stream *)data;
	struct es_dvp2axi_buffer *buf = NULL;
	unsigned long flags = 0;
	LIST_HEAD(local_list);

	spin_lock_irqsave(&stream->vbq_lock, flags);
	list_replace_init(&stream->vb_done_list, &local_list);
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	while (!list_empty(&local_list)) {
		buf = list_first_entry(&local_list, struct es_dvp2axi_buffer, queue);
		list_del(&buf->queue);
		es_dvp2axi_vb_done_oneframe(stream, &buf->vb);
	}
}

void es_dvp2axi_vb_done_tasklet(struct es_dvp2axi_stream *stream,
			   struct es_dvp2axi_buffer *buf)
{
	unsigned long flags = 0;

	if (!stream || !buf)
		return;
	spin_lock_irqsave(&stream->vbq_lock, flags);
	list_add_tail(&buf->queue, &stream->vb_done_list);
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	tasklet_schedule(&stream->vb_done_tasklet);
}

static void es_dvp2axi_unregister_stream_vdev(struct es_dvp2axi_stream *stream)
{
	tasklet_kill(&stream->vb_done_tasklet);
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int es_dvp2axi_register_stream_vdev(struct es_dvp2axi_stream *stream,
				      bool is_multi_input)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct es_dvp2axi_vdev_node *node;
	int ret = 0;
	char *vdev_name;

	if (dev->inf_id == ES_DVP2AXI_MIPI_LVDS) {
		switch (stream->id) {
		case ESDVP2AXI_STREAM_MIPI_ID0:
			vdev_name = DVP2AXI_MIPI_ID0_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID1:
			vdev_name = DVP2AXI_MIPI_ID1_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID2:
			vdev_name = DVP2AXI_MIPI_ID2_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID3:
			vdev_name = DVP2AXI_MIPI_ID3_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID4:
			vdev_name = DVP2AXI_MIPI_ID4_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID5:
			vdev_name = DVP2AXI_MIPI_ID5_VDEV_NAME;
			break;
		default:
			ret = -EINVAL;
			v4l2_err(v4l2_dev, "Invalid stream\n");
			goto unreg;
		}
	} else {
		switch (stream->id) {
		case ESDVP2AXI_STREAM_MIPI_ID0:
			vdev_name = DVP2AXI_MIPI_ID0_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID1:
			vdev_name = DVP2AXI_MIPI_ID1_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID2:
			vdev_name = DVP2AXI_MIPI_ID2_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID3:
			vdev_name = DVP2AXI_MIPI_ID3_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID4:
			vdev_name = DVP2AXI_MIPI_ID4_VDEV_NAME;
			break;
		case ESDVP2AXI_STREAM_MIPI_ID5:
			vdev_name = DVP2AXI_MIPI_ID5_VDEV_NAME;
			break;
		default:
			ret = -EINVAL;
			v4l2_err(v4l2_dev, "Invalid stream\n");
			goto unreg;
		}
	}

	strlcpy(vdev->name, vdev_name, sizeof(vdev->name));
	v4l2_err(v4l2_dev, "vdev->name %s \n", vdev->name);
	node = vdev_to_node(vdev);
	mutex_init(&node->vlock);
	vdev->ioctl_ops = &es_dvp2axi_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &es_dvp2axi_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &node->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
	es_dvp2axi_init_vb2_queue(&node->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &node->buf_queue;
	
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video_register_device failed with error %d\n", ret);
		return ret;
	}


	INIT_LIST_HEAD(&stream->vb_done_list);
	tasklet_init(&stream->vb_done_tasklet, es_dvp2axi_tasklet_handle,
		     (unsigned long)stream);
	tasklet_disable(&stream->vb_done_tasklet);
	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

void es_dvp2axi_unregister_stream_vdevs(struct es_dvp2axi_device *dev, int stream_num)
{
	struct es_dvp2axi_stream *stream;
	int i;

	for (i = 0; i < stream_num; i++) {
		stream = &dev->stream[i];
		es_dvp2axi_unregister_stream_vdev(stream);
	}
}

int es_dvp2axi_register_stream_vdevs(struct es_dvp2axi_device *dev, int stream_num,
				bool is_multi_input)
{
	struct es_dvp2axi_stream *stream;
	int i, j, ret;

	for (i = 0; i < stream_num; i++) {
		stream = &dev->stream[i];
		stream->dvp2axidev = dev;
		ret = es_dvp2axi_register_stream_vdev(stream, is_multi_input);
		if (ret < 0)
			goto err;
	}
	dev->num_channels = stream_num;
	return 0;
err:
	for (j = 0; j < i; j++) {
		stream = &dev->stream[j];
		es_dvp2axi_unregister_stream_vdev(stream);
	}

	return ret;
}

static struct v4l2_subdev *get_lvds_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[ES_DVP2AXI_LVDS_PAD_SINK];
	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor with lvds\n");
		return NULL;
	}

	sensor_me = media_pad_remote_pad_first(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static int es_dvp2axi_lvds_subdev_link_setup(struct media_entity *entity,
					const struct media_pad *local,
					const struct media_pad *remote,
					u32 flags)
{
	return 0;
}

static int es_dvp2axi_lvds_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	return v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
}

static int es_dvp2axi_lvds_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct es_dvp2axi_lvds_subdev *subdev =
		container_of(sd, struct es_dvp2axi_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	int ret;

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
	if (!ret)
		subdev->in_fmt = fmt->format;

	return ret;
}

static struct v4l2_rect *
es_dvp2axi_lvds_sd_get_crop(struct es_dvp2axi_lvds_subdev *subdev,
		       struct v4l2_subdev_state *sd_state,
		       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&subdev->sd, sd_state,
						ES_DVP2AXI_LVDS_PAD_SINK);
	else
		return &subdev->crop;
}

static int es_dvp2axi_lvds_sd_set_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_selection *sel)
{
	struct es_dvp2axi_lvds_subdev *subdev =
		container_of(sd, struct es_dvp2axi_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	int ret = 0;

	ret = v4l2_subdev_call(sensor, pad, set_selection, sd_state, sel);
	if (!ret)
		subdev->crop = sel->r;

	return ret;
}

static int es_dvp2axi_lvds_sd_get_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_selection *sel)
{
	struct es_dvp2axi_lvds_subdev *subdev =
		container_of(sd, struct es_dvp2axi_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	struct v4l2_subdev_format fmt;
	int ret = 0;

	if (!sel) {
		v4l2_dbg(1, es_dvp2axi_debug, sd, "sel is null\n");
		goto err;
	}

	if (sel->pad > ES_DVP2AXI_LVDS_PAD_SRC_ID3) {
		v4l2_dbg(1, es_dvp2axi_debug, sd, "pad[%d] isn't matched\n",
			 sel->pad);
		goto err;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			ret = v4l2_subdev_call(sensor, pad, get_selection,
					       sd_state, sel);
			if (ret) {
				fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
				ret = v4l2_subdev_call(sensor, pad, get_fmt,
						       NULL, &fmt);
				if (!ret) {
					subdev->in_fmt = fmt.format;
					sel->r.top = 0;
					sel->r.left = 0;
					sel->r.width = subdev->in_fmt.width;
					sel->r.height = subdev->in_fmt.height;
					subdev->crop = sel->r;
				} else {
					sel->r = subdev->crop;
				}
			} else {
				subdev->crop = sel->r;
			}
		} else {
			sel->r = *v4l2_subdev_get_try_crop(sd, sd_state,
							   sel->pad);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *es_dvp2axi_lvds_sd_get_crop(subdev, sd_state, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
err:
	return -EINVAL;
}

static int es_dvp2axi_lvds_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				    struct v4l2_mbus_config *mbus)
{
	struct v4l2_subdev *sensor_sd = get_lvds_remote_sensor(sd);
	int ret;

	ret = v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0, mbus);
	if (ret)
		return ret;

	return 0;
}

static int es_dvp2axi_lvds_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct es_dvp2axi_lvds_subdev *subdev =
		container_of(sd, struct es_dvp2axi_lvds_subdev, sd);

	if (on)
		atomic_set(&subdev->frm_sync_seq, 0);

	return 0;
}

static int es_dvp2axi_lvds_sd_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

#define ESCIF_V4L2_EVENT_ELEMS 4

static int es_dvp2axi_sof_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				     struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_FRAME_SYNC ||
	    sub->type == V4L2_EVENT_RESET_DEV)
		return v4l2_event_subscribe(fh, sub, ES_DVP2AXI_V4L2_EVENT_ELEMS,
					    NULL);
	else
		return -EINVAL;
}

static const struct media_entity_operations es_dvp2axi_lvds_sd_media_ops = {
	.link_setup = es_dvp2axi_lvds_subdev_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops es_dvp2axi_lvds_sd_pad_ops = {
	.set_fmt = es_dvp2axi_lvds_sd_set_fmt,
	.get_fmt = es_dvp2axi_lvds_sd_get_fmt,
	.set_selection = es_dvp2axi_lvds_sd_set_selection,
	.get_selection = es_dvp2axi_lvds_sd_get_selection,
	.get_mbus_config = es_dvp2axi_lvds_g_mbus_config,
};

static const struct v4l2_subdev_video_ops es_dvp2axi_lvds_sd_video_ops = {
	.s_stream = es_dvp2axi_lvds_sd_s_stream,
};

static const struct v4l2_subdev_core_ops es_dvp2axi_lvds_sd_core_ops = {
	.s_power = es_dvp2axi_lvds_sd_s_power,
	.subscribe_event = es_dvp2axi_sof_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_ops es_dvp2axi_lvds_sd_ops = {
	.core = &es_dvp2axi_lvds_sd_core_ops,
	.video = &es_dvp2axi_lvds_sd_video_ops,
	.pad = &es_dvp2axi_lvds_sd_pad_ops,
};

static void es_dvp2axi_lvds_event_inc_sof(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_lvds_subdev *subdev = &dev->lvds_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&subdev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static u32 es_dvp2axi_lvds_get_sof(struct es_dvp2axi_device *dev)
{
	if (dev)
		return atomic_read(&dev->lvds_subdev.frm_sync_seq) - 1;

	return 0;
}

static u32 es_dvp2axi_lvds_set_sof(struct es_dvp2axi_device *dev, u32 seq)
{
	if (dev)
		atomic_set(&dev->lvds_subdev.frm_sync_seq, seq);

	return 0;
}

int es_dvp2axi_register_lvds_subdev(struct es_dvp2axi_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct es_dvp2axi_lvds_subdev *lvds_subdev = &dev->lvds_subdev;
	struct v4l2_subdev *sd;
	int ret;
	int pad_num = 4;

	memset(lvds_subdev, 0, sizeof(*lvds_subdev));
	lvds_subdev->dvp2axidev = dev;
	sd = &lvds_subdev->sd;
	lvds_subdev->state = ES_DVP2AXI_LVDS_STOP;
	v4l2_subdev_init(sd, &es_dvp2axi_lvds_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &es_dvp2axi_lvds_sd_media_ops;

	snprintf(sd->name, sizeof(sd->name), "es_dvp2axi-lvds-subdev");

	lvds_subdev->pads[ES_DVP2AXI_LVDS_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	lvds_subdev->pads[ES_DVP2AXI_LVDS_PAD_SRC_ID0].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[ES_DVP2AXI_LVDS_PAD_SRC_ID1].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[ES_DVP2AXI_LVDS_PAD_SRC_ID2].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[ES_DVP2AXI_LVDS_PAD_SRC_ID3].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->in_fmt.width = ES_DVP2AXI_DEFAULT_WIDTH;
	lvds_subdev->in_fmt.height = ES_DVP2AXI_DEFAULT_HEIGHT;
	lvds_subdev->crop.left = 0;
	lvds_subdev->crop.top = 0;
	lvds_subdev->crop.width = ES_DVP2AXI_DEFAULT_WIDTH;
	lvds_subdev->crop.height = ES_DVP2AXI_DEFAULT_HEIGHT;

	ret = media_entity_pads_init(&sd->entity, pad_num, lvds_subdev->pads);
	if (ret < 0)
		return ret;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, lvds_subdev);
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0)
		goto free_media;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto free_subdev;
	return ret;
free_subdev:
	v4l2_device_unregister_subdev(sd);
free_media:
	media_entity_cleanup(&sd->entity);
	v4l2_err(sd, "Failed to register subdev, ret:%d\n", ret);
	return ret;
}

void es_dvp2axi_unregister_lvds_subdev(struct es_dvp2axi_device *dev)
{
	struct v4l2_subdev *sd = &dev->lvds_subdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

static void es_dvp2axi_dvp_event_inc_sof(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_dvp_sof_subdev *subdev = &dev->dvp_sof_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&subdev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static u32 es_dvp2axi_dvp_get_sof(struct es_dvp2axi_device *dev)
{
	if (dev)
		return atomic_read(&dev->dvp_sof_subdev.frm_sync_seq) - 1;

	return 0;
}

static u32 es_dvp2axi_dvp_set_sof(struct es_dvp2axi_device *dev, u32 seq)
{
	if (dev)
		atomic_set(&dev->dvp_sof_subdev.frm_sync_seq, seq);

	return 0;
}

static const struct v4l2_subdev_core_ops es_dvp2axi_dvp_sof_sd_core_ops = {
	.subscribe_event = es_dvp2axi_sof_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_ops es_dvp2axi_dvp_sof_sd_ops = {
	.core = &es_dvp2axi_dvp_sof_sd_core_ops,
};

int es_dvp2axi_register_dvp_sof_subdev(struct es_dvp2axi_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct es_dvp2axi_dvp_sof_subdev *subdev = &dev->dvp_sof_subdev;
	struct v4l2_subdev *sd;
	int ret;

	memset(subdev, 0, sizeof(*subdev));
	subdev->dvp2axidev = dev;
	sd = &subdev->sd;
	v4l2_subdev_init(sd, &es_dvp2axi_dvp_sof_sd_ops);
	sd->owner = THIS_MODULE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, sizeof(sd->name), "es_dvp2axi-dvp-sof");

	v4l2_set_subdevdata(sd, subdev);
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0)
		goto end;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto free_subdev;

	return ret;

free_subdev:
	v4l2_device_unregister_subdev(sd);

end:
	v4l2_err(sd, "Failed to register subdev, ret:%d\n", ret);
	return ret;
}

void es_dvp2axi_unregister_dvp_sof_subdev(struct es_dvp2axi_device *dev)
{
	struct v4l2_subdev *sd = &dev->dvp_sof_subdev.sd;

	v4l2_device_unregister_subdev(sd);
}

void dvp2axi_hw_soft_reset(struct es_dvp2axi_hw *dvp2axi_hw)
{
	uint32_t soft_rstn;
	u32 count = 0;
		// do software reset
	soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR) & (~VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn);
	do {
		volatile uint32_t cycle = 0;
		while (cycle++ < 100);
		soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
		udelay(1);
	} while ((soft_rstn & VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK) != VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK && count++ < 100);
    	// release reset
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
}

void es_irq_oneframe(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev)
{
	u32 vi_dvp2axi_int0, vi_dvp2axi_int1;
	u32 vi_dvp2axi_int_err;
	int ret = 0;
	struct es_dvp2axi_hw	*dvp2axi_hw =  dev_get_drvdata(dev);
	struct es_dvp2axi_stream *stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];
	//struct es_dvp2axi_stream *stream;
	struct es_dvp2axi_buffer *active_buf = NULL;
	//int dvp_chn;
	int es_dvp2axi_addr_state;
	
	vi_dvp2axi_int0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR);
	vi_dvp2axi_int1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR);
	vi_dvp2axi_int_err = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR);
	
	//TODO add error process into err tasklet
	if (vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_AFULL || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP5_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP4_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP3_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP2_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP1_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP0_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_RESP_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_FULL) {  // error handle
		
		uint32_t soft_rstn;
		u32 count = 0;
		// do software reset
		soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR) & (~VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn);
		do {
			volatile uint32_t cycle = 0;
			while (cycle++ < 100);
			soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
			udelay(1);
		} while ((soft_rstn & VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK) != VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK && count++ < 100);

    	// release reset
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
	}

	if (vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID0_FRAME_DONE_MASK) {  // done handle
		active_buf = stream->curr_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME0_READY;
	}
	if (vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID1_FRAME_DONE_MASK) {
		active_buf = stream->next_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME1_READY;
	} 
	if (vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID2_FRAME_DONE_MASK) {
		//TODO not use now
		active_buf = stream->last_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME2_READY;
	}



	if(stream->is_first_flush) {
		stream->is_first_flush = false;
	} 
	es_dvp2axi_addr_state = ES_DVP2AXI_YUV_ADDR_STATE_UPDATE;
	//TODO need to check whether the frame is done but not get flush signal
	// if ((vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID0_FRAME_FLUSH_MASK) && (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY)) {  // done handle
	// 	ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	// }
	// if((vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID1_FRAME_FLUSH_MASK) && (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY)) {
	// 	ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	// }
	// if((vi_dvp2axi_int0 & VI_DVP2AXI_DVP0_ID2_FRAME_FLUSH_MASK) && (stream->frame_phase == DVP2AXI_CSI_FRAME2_READY)) {
	// 		//TODO not use now
	// 	ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	// }

	if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {  // done handle
		ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	}
	if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
		ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	}
	if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
		ret = es_dvp2axi_assign_new_buffer_oneframe(stream, es_dvp2axi_addr_state);
	}

	if (active_buf && (!ret)) {
		active_buf->vb.sequence = stream->frame_idx - 1;
		es_dvp2axi_vb_done_tasklet(stream, active_buf);
		dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
	}
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, vi_dvp2axi_int0);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, vi_dvp2axi_int1);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR, vi_dvp2axi_int_err);
	stream->frame_phase = DVP2AXI_CSI_FRAME_UNREADY;
}

void es_irq_oneframe_new(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev)
{
	u32 vi_dvp2axi_int0, vi_dvp2axi_int1;
	u32 vi_dvp2axi_int_err;
	int ret = 0;
	struct es_dvp2axi_hw	*dvp2axi_hw =  dev_get_drvdata(dev);
	// struct es_dvp2axi_stream *stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];
	struct es_dvp2axi_stream *stream;
	struct es_dvp2axi_buffer *active_buf[ESDVP2AXI_MAX_STREAM_MIPI][3] = {NULL};
	int dvp_chn;
	int esdvp2axi_addr_state;
	dvp2axi_irq_status multi_dvp_irq_status[ESDVP2AXI_MAX_STREAM_MIPI];

	memset(multi_dvp_irq_status, 0, sizeof(dvp2axi_irq_status)* ESDVP2AXI_MAX_STREAM_MIPI);
	vi_dvp2axi_int0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR);
	vi_dvp2axi_int1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR);
	vi_dvp2axi_int_err = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR);

	pr_info("%s: vi_dvp2axi_int0 0x%x, vi_dvp2axi_int1 0x%x, vi_dvp2axi_int_err 0x%x\n", __func__, vi_dvp2axi_int0, vi_dvp2axi_int1, vi_dvp2axi_int_err);
	//TODO add error process into err tasklet
	if (vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_AFULL || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP5_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP4_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP3_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP2_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP1_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP0_FRAME_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_RESP_ERROR || \
		vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_FULL) {  // error handle

		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, vi_dvp2axi_int0);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, vi_dvp2axi_int1);

		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR, vi_dvp2axi_int_err);
		
		uint32_t soft_rstn;

		// do software reset
		soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR) & (~VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn);
		do {
			volatile uint32_t cycle = 0;
			while (cycle++ < 100);
			soft_rstn = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
    	} while ((soft_rstn & VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK) != VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK);
    	// release reset
    	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);

		//return ;
	}

	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEDONE_DVP0) {
		dvp_chn = 0;
		multi_dvp_irq_status[0].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[0].chn_frame_done[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID0_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP0_ID0_FRAME_DONE:0;
		multi_dvp_irq_status[0].chn_frame_done[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID1_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP0_ID1_FRAME_DONE:0;
		multi_dvp_irq_status[0].chn_frame_done[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID2_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP0_ID2_FRAME_DONE:0;
	} 
	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEDONE_DVP1) {
		dvp_chn = 1;
		multi_dvp_irq_status[1].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[1].chn_frame_done[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID0_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP1_ID0_FRAME_DONE: 0 ;
		multi_dvp_irq_status[1].chn_frame_done[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID1_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP1_ID1_FRAME_DONE: 0;
		multi_dvp_irq_status[1].chn_frame_done[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID2_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP1_ID2_FRAME_DONE: 0;
	} 
	
	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEDONE_DVP2) {
		dvp_chn = 2;
		multi_dvp_irq_status[2].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[2].chn_frame_done[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID0_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP2_ID0_FRAME_DONE: 0;
		multi_dvp_irq_status[2].chn_frame_done[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID1_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP2_ID1_FRAME_DONE: 0;
		multi_dvp_irq_status[2].chn_frame_done[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID2_FRAME_DONE) ? VI_DVP2AXI_INT0_DVP2_ID2_FRAME_DONE: 0;
	}

	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEDONE_DVP3) {
		dvp_chn = 3;
		multi_dvp_irq_status[3].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[3].chn_frame_done[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID0_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP3_ID0_FRAME_DONE:0;
		multi_dvp_irq_status[3].chn_frame_done[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID1_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP3_ID1_FRAME_DONE:0;
		multi_dvp_irq_status[3].chn_frame_done[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID2_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP3_ID2_FRAME_DONE:0;
	} 
	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEDONE_DVP4) {
		dvp_chn = 4;
		multi_dvp_irq_status[4].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[4].chn_frame_done[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP4_ID0_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP4_ID0_FRAME_DONE:0;
		multi_dvp_irq_status[4].chn_frame_done[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP4_ID1_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP4_ID1_FRAME_DONE:0;
		multi_dvp_irq_status[4].chn_frame_done[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP4_ID2_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP4_ID2_FRAME_DONE:0;
	} 
	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEDONE_DVP5) {
		dvp_chn = 5;
		multi_dvp_irq_status[5].dvp_frame_done_irq = 1;
		multi_dvp_irq_status[5].chn_frame_done[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP5_ID0_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP5_ID0_FRAME_DONE:0;
		multi_dvp_irq_status[5].chn_frame_done[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP5_ID1_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP5_ID1_FRAME_DONE:0;
		multi_dvp_irq_status[5].chn_frame_done[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP5_ID2_FRAME_DONE) ? VI_DVP2AXI_INT1_DVP5_ID2_FRAME_DONE:0;
	}

	// flush 

	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP0) {
		dvp_chn = 0;
		multi_dvp_irq_status[0].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[0].chn_frame_flush[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP0_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[0].chn_frame_flush[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP0_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[0].chn_frame_flush[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP0_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP0_ID2_FRAME_FLUSH: 0;
	} 
	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP1) {
		dvp_chn = 1;
		multi_dvp_irq_status[1].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[1].chn_frame_flush[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP1_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[1].chn_frame_flush[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP1_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[1].chn_frame_flush[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP1_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP1_ID2_FRAME_FLUSH: 0;
	} 
	
	if(vi_dvp2axi_int0 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP2) {
		dvp_chn = 2;
		multi_dvp_irq_status[2].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[2].chn_frame_flush[0] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP2_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[2].chn_frame_flush[1] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP2_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[2].chn_frame_flush[2] = (vi_dvp2axi_int0 & VI_DVP2AXI_INT0_DVP2_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT0_DVP2_ID2_FRAME_FLUSH: 0;
	}

	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP3) {
		dvp_chn = 3;
		multi_dvp_irq_status[3].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[3].chn_frame_flush[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[3].chn_frame_flush[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[3].chn_frame_flush[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH: 0;
	} 
	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP4) {
		dvp_chn = 4;
		multi_dvp_irq_status[4].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[4].chn_frame_flush[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[4].chn_frame_flush[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[4].chn_frame_flush[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH: 0;
	} 
	if(vi_dvp2axi_int1 & VI_DVP2AXI_INT_FRAMEFLUSH_DVP5) {
		dvp_chn = 5;
		multi_dvp_irq_status[5].dvp_frame_flush_irq = 1;
		multi_dvp_irq_status[5].chn_frame_flush[0] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID0_FRAME_FLUSH: 0;
		multi_dvp_irq_status[5].chn_frame_flush[1] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID1_FRAME_FLUSH: 0;
		multi_dvp_irq_status[5].chn_frame_flush[2] = (vi_dvp2axi_int1 & VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH) ? VI_DVP2AXI_INT1_DVP3_ID2_FRAME_FLUSH: 0;
	}

	
	for(int i = 0; i < ESDVP2AXI_MAX_STREAM_MIPI; i++) {
		stream = &dvp2axi_dev->stream[i];
		
		if(multi_dvp_irq_status[i].dvp_frame_done_irq) {
			for(int j=0; j<3; j++) {
				if(multi_dvp_irq_status[i].chn_frame_done[j] != 0) {
					active_buf[i][j] = (j == 0)? stream->curr_buf: (j == 1)? stream->next_buf: (j == 2)? stream->last_buf: NULL;
					stream->frame_phase = j;
					if(i <3) {
						pr_info("%s:%d frame done %d \n", __func__, __LINE__, multi_dvp_irq_status[i].chn_frame_done[j]);
						DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, multi_dvp_irq_status[i].chn_frame_done[j]);
					}
					else {
						DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, multi_dvp_irq_status[i].chn_frame_done[j]);
					}
				}
			}
		}
		if(multi_dvp_irq_status[i].dvp_frame_flush_irq) {
			if(stream->is_first_flush) {
				stream->is_first_flush = false;
				if(i <3)
					DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, multi_dvp_irq_status[i].chn_frame_flush[0] | multi_dvp_irq_status[i].chn_frame_flush[1] | multi_dvp_irq_status[i].chn_frame_flush[2]);
				else {
					DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, multi_dvp_irq_status[i].chn_frame_flush[0] | multi_dvp_irq_status[i].chn_frame_flush[1] | multi_dvp_irq_status[i].chn_frame_flush[2]);
				}
			} else {
				for(int j=0; j<3; j++) {
					if(multi_dvp_irq_status[i].chn_frame_flush[j] != 0) {
						esdvp2axi_addr_state = ES_DVP2AXI_YUV_ADDR_STATE_UPDATE;
						ret = es_dvp2axi_assign_new_buffer_oneframe(stream, esdvp2axi_addr_state);
						if( i < 3) {
							DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR,multi_dvp_irq_status[i].chn_frame_flush[j]);
						} else {
							DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, multi_dvp_irq_status[i].chn_frame_flush[j]);
						}
					}
				}
			}
		}

		

		for(int j=0; j<3; j++) {
			if (active_buf[i][j] && (!ret)) {
				active_buf[i][j]->vb.sequence = stream->frame_idx - 1;
				es_dvp2axi_vb_done_tasklet(stream, active_buf[i][j]);
				dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
			}
		}
	}
	

}



void es_dvp2axi_irq_oneframe(struct es_dvp2axi_device *dvp2axi_dev)
{
	/* TODO: xuhf-debug: add stream type */
	struct es_dvp2axi_stream *stream;
	u32 lastline, lastpix, ctl, dvp2axi_frmst, intstat, frmid;
	int ret = 0;
	



	intstat = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT);
	dvp2axi_frmst = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_FRAME_STATUS);
	lastline = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_LAST_LINE);
	lastpix = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_LAST_PIX);
	ctl = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL);
	frmid = DVP2AXI_GET_FRAME_ID(dvp2axi_frmst);

	/* There are two irqs enabled:
	 *  - PST_INF_FRAME_END: dvp2axi FIFO is ready, this is prior to FRAME_END
	 *  -         FRAME_END: dvp2axi has saved frame to memory, a frame ready
	 */
	stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];

	if ((intstat & PST_INF_FRAME_END)) {
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT,
				     PST_INF_FRAME_END_CLR);

		if (stream->stopping)
			/* To stop DVP2AXI ASAP, before FRAME_END irq */
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL,
					     ctl & (~ENABLE_CAPTURE));
	}

	if ((intstat & FRAME_END)) {
		struct es_dvp2axi_buffer *active_buf = NULL;

		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT,
				     FRAME_END_CLR);

		if (stream->stopping) {
			es_dvp2axi_stream_stop(stream);
			stream->stopping = false;
			wake_up(&stream->wq_stopped);
			return;
		}

		if (lastline != stream->pixm.height ||
		    !(dvp2axi_frmst & DVP2AXI_F0_READY)) {
			/* Clearing status must be complete before fe packet
			 * arrives while dvp2axi is connected with mipi,
			 * so it should be placed before printing log here,
			 * otherwise it would be delayed.
			 * At the same time, don't clear the frame id
			 * for switching address.
			 */
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_FRAME_STATUS,
					     FRM0_STAT_CLS);
			v4l2_err(&dvp2axi_dev->v4l2_dev,
				 "Bad frame, irq:0x%x frmst:0x%x size:%dx%d\n",
				 intstat, dvp2axi_frmst, lastline, lastpix);

			return;
		}

		if (frmid % 2 != 0) {
			stream->frame_phase = DVP2AXI_CSI_FRAME0_READY;
			if (stream->curr_buf)
				active_buf = stream->curr_buf;
		} else {
			stream->frame_phase = DVP2AXI_CSI_FRAME1_READY;
			if (stream->next_buf)
				active_buf = stream->next_buf;
		}

		/* In one-frame mode:
		 * 1,must clear status manually by writing 0 to enable
		 * the next frame end irq;
		 * 2,do not clear the frame id for switching address.
		 */
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_FRAME_STATUS,
				     dvp2axi_frmst & FRM0_STAT_CLS);
		ret = es_dvp2axi_assign_new_buffer_oneframe(
			stream, ES_DVP2AXI_YUV_ADDR_STATE_UPDATE);

		if (active_buf && (!ret)) {
			active_buf->vb.sequence = stream->frame_idx - 1;
			es_dvp2axi_vb_done_tasklet(stream, active_buf);
		}

		dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
	}
}

static int es_dvp2axi_csi_g_mipi_id(struct v4l2_device *v4l2_dev,
			       unsigned int intstat)
{
	if (intstat & CSI_FRAME_END_ID0)
		return ES_DVP2AXI_STREAM_MIPI_ID0;

	if (intstat & CSI_FRAME_END_ID1)
		return ES_DVP2AXI_STREAM_MIPI_ID1;

	if (intstat & CSI_FRAME_END_ID2)
		return ES_DVP2AXI_STREAM_MIPI_ID2;
	if (intstat & CSI_FRAME_END_ID3)
		return ES_DVP2AXI_STREAM_MIPI_ID3;

	return -EINVAL;
}

static int es_dvp2axi_dvp_g_ch_id(struct v4l2_device *v4l2_dev, u32 *intstat,
			     u32 frm_stat)
{
	if (*intstat & DVP_FRAME_END_ID0) {
		*intstat &= ~DVP_FRAME_END_ID0;
		return ES_DVP2AXI_STREAM_MIPI_ID0;
	}

	if (*intstat & DVP_FRAME_END_ID1) {
		*intstat &= ~DVP_FRAME_END_ID1;
		return ES_DVP2AXI_STREAM_MIPI_ID1;
	}

	if (*intstat & DVP_FRAME_END_ID2) {
		*intstat &= ~DVP_FRAME_END_ID2;
		return ES_DVP2AXI_STREAM_MIPI_ID2;
	}

	if (*intstat & DVP_FRAME_END_ID3) {
		*intstat &= ~DVP_FRAME_END_ID3;
		return ES_DVP2AXI_STREAM_MIPI_ID3;
	}

	return -EINVAL;
}

static int es_dvp2axi_dvp_g_ch_id_by_fe(struct v4l2_device *v4l2_dev, u32 intstat)
{
	if (intstat & DVP_ALL_END_ID0)
		return ES_DVP2AXI_STREAM_MIPI_ID0;

	if (intstat & DVP_ALL_END_ID1)
		return ES_DVP2AXI_STREAM_MIPI_ID1;

	if (intstat & DVP_ALL_END_ID2)
		return ES_DVP2AXI_STREAM_MIPI_ID2;

	if (intstat & DVP_ALL_END_ID3)
		return ES_DVP2AXI_STREAM_MIPI_ID3;

	return -EINVAL;
}

static bool es_dvp2axi_is_csi2_err_trigger_reset(struct es_dvp2axi_timer *timer)
{
	struct es_dvp2axi_device *dev =
		container_of(timer, struct es_dvp2axi_device, reset_watchdog_timer);
	struct es_dvp2axi_stream *stream = &dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0];
	bool is_triggered = false, is_assign_triggered = false,
	     is_first_err = false;
	unsigned long flags;
	u64 cur_time, diff_time;

	spin_lock_irqsave(&timer->csi2_err_lock, flags);

	if (timer->csi2_err_cnt_even != 0 && timer->csi2_err_cnt_odd != 0) {
		timer->csi2_err_cnt_odd = 0;
		timer->csi2_err_cnt_even = 0;
		timer->reset_src = ES_RESET_SRC_ERR_CSI2;
		timer->csi2_err_triggered_cnt++;
		if (timer->csi2_err_triggered_cnt == 1) {
			is_first_err = true;
			timer->csi2_first_err_timestamp =
				es_dvp2axi_time_get_ns(dev);
		}

		is_assign_triggered = true;

		v4l2_info(&dev->v4l2_dev, "find csi2 err cnt is:%d\n",
			  timer->csi2_err_triggered_cnt);
	}

	if (!is_first_err) {
		if (timer->csi2_err_triggered_cnt >= 1) {
			cur_time = es_dvp2axi_time_get_ns(dev);
			diff_time = cur_time - timer->csi2_first_err_timestamp;
			diff_time = div_u64(diff_time, 1000000);
			if (diff_time >= timer->err_time_interval) {
				is_triggered = true;
				v4l2_info(
					&dev->v4l2_dev,
					"trigger reset for time out of csi err\n");
				goto end_judge;
			}

			if (!is_assign_triggered &&
			    (timer->csi2_err_cnt_odd == 0 ||
			     timer->csi2_err_cnt_even == 0)) {
				is_triggered = true;
				v4l2_info(&dev->v4l2_dev,
					  "trigger reset for csi err\n");
				goto end_judge;
			}
		}
	}

	/*
	 * when fs cnt is beyond 2, it indicates that frame end is not coming,
	 * or fs and fe had been not paired.
	 */
	if (stream->is_fs_fe_not_paired ||
	    stream->fs_cnt_in_single_frame > ES_DVP2AXI_FS_DETECTED_NUM) {
		is_triggered = true;
		v4l2_info(&dev->v4l2_dev, "reset for fs & fe not paired\n");
	}
end_judge:
	spin_unlock_irqrestore(&timer->csi2_err_lock, flags);

	return is_triggered;
}

static bool es_dvp2axi_is_triggered_monitoring(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_timer *timer = &dev->reset_watchdog_timer;
	struct es_dvp2axi_stream *stream = &dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0];
	bool ret = false;

	if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_IDLE)
		ret = false;

	if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_CONTINUE ||
	    timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_HOTPLUG) {
		if (stream->frame_idx >= timer->triggered_frame_num)
			ret = true;
	}

	if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_TRIGGER) {
		timer->is_csi2_err_occurred =
			es_dvp2axi_is_csi2_err_trigger_reset(timer);
		ret = timer->is_csi2_err_occurred;
	}

	return ret;
}

s32 es_dvp2axi_get_sensor_vblank(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sd->ctrl_handler;
	struct v4l2_ctrl *ctrl = NULL;

	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_VBLANK)
				return ctrl->val;
		}
	}

	return 0;
}

s32 es_dvp2axi_get_sensor_vblank_def(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sd->ctrl_handler;
	struct v4l2_ctrl *ctrl = NULL;

	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_VBLANK)
				return ctrl->default_value;
		}
	}

	return 0;
}

static void es_dvp2axi_cal_csi_crop_width_vwidth(struct es_dvp2axi_stream *stream,
					    u32 raw_width, u32 *crop_width,
					    u32 *crop_vwidth)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct csi_channel_info *channel = &dev->channels[stream->id];
	const struct dvp2axi_output_fmt *fmt;
	u32 fourcc;

	fmt = es_dvp2axi_find_output_fmt(stream, stream->pixm.pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev, "can not find output format: 0x%x",
			 stream->pixm.pixelformat);
		return;
	}

	*crop_width = raw_width;

	/*
	 * for mipi or lvds, when enable compact, the virtual width of raw10/raw12
	 * needs aligned with :ALIGN(bits_per_pixel * width / 8, 8), if enable 16bit mode
	 * needs aligned with :ALIGN(bits_per_pixel * width * 2, 8), to optimize reading and
	 * writing of ddr, aliged with 256
	 */
	if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW && stream->is_compact &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565) {
		*crop_vwidth = ALIGN(raw_width * fmt->raw_bpp / 8, 256);
	} else {
		*crop_vwidth = ALIGN(raw_width * fmt->bpp[0] / 8, 8);
	}

	if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888 ||
	    channel->fmt_val == CSI_WRDDR_TYPE_RGB565)
		*crop_width = raw_width * fmt->bpp[0] / 8;
	/*
	 * es dvp2axi don't support output yuyv fmt data
	 * if user request yuyv fmt, the input mode must be RAW8
	 * and the width is double Because the real input fmt is
	 * yuyv
	 */
	fourcc = stream->dvp2axi_fmt_out->fourcc;
	if (fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_YVYU ||
	    fourcc == V4L2_PIX_FMT_UYVY || fourcc == V4L2_PIX_FMT_VYUY) {
		*crop_width = 2 * raw_width;
		*crop_vwidth *= 2;
	}
}

static void es_dvp2axi_dynamic_crop(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus;
	const struct dvp2axi_output_fmt *fmt;
	u32 raw_width, crop_width = 64, crop_vwidth = 64, crop_height = 64,
		       crop_x = 0, crop_y = 0;

	if (!dvp2axi_dev->active_sensor)
		return;

	mbus = &dvp2axi_dev->active_sensor->mbus;
	if (mbus->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus->type == V4L2_MBUS_CSI2_CPHY || mbus->type == V4L2_MBUS_CCP2) {
		struct csi_channel_info *channel =
			&dvp2axi_dev->channels[stream->id];

		if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
			crop_x = 3 * stream->crop[CROP_SRC_ACT].left;
		else if (channel->fmt_val == CSI_WRDDR_TYPE_RGB565)
			crop_x = 2 * stream->crop[CROP_SRC_ACT].left;
		else
			crop_x = stream->crop[CROP_SRC_ACT].left;

		crop_y = stream->crop[CROP_SRC_ACT].top;
		raw_width = stream->crop[CROP_SRC_ACT].width;
		crop_height = stream->crop[CROP_SRC_ACT].height;

		es_dvp2axi_cal_csi_crop_width_vwidth(stream, raw_width, &crop_width,
						&crop_vwidth);
		es_dvp2axi_write_register(
			dvp2axi_dev, get_reg_index_of_id_crop_start(channel->id),
			crop_y << 16 | crop_x);
		es_dvp2axi_write_register(dvp2axi_dev,
				     get_reg_index_of_id_ctrl1(channel->id),
				     crop_height << 16 | crop_width);

		es_dvp2axi_write_register(dvp2axi_dev,
				     get_reg_index_of_frm0_y_vlw(channel->id),
				     crop_vwidth);
		es_dvp2axi_write_register(dvp2axi_dev,
				     get_reg_index_of_frm1_y_vlw(channel->id),
				     crop_vwidth);
		es_dvp2axi_write_register(dvp2axi_dev,
				     get_reg_index_of_frm0_uv_vlw(channel->id),
				     crop_vwidth);
		es_dvp2axi_write_register(dvp2axi_dev,
				     get_reg_index_of_frm1_uv_vlw(channel->id),
				     crop_vwidth);
	} else {
		raw_width = stream->crop[CROP_SRC_ACT].width;
		crop_width = raw_width;
		crop_vwidth = raw_width;
		crop_height = stream->crop[CROP_SRC_ACT].height;
		crop_x = stream->crop[CROP_SRC_ACT].left;
		crop_y = stream->crop[CROP_SRC_ACT].top;

		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CROP,
				     crop_y << DVP2AXI_CROP_Y_SHIFT | crop_x);

		if (stream->dvp2axi_fmt_in->fmt_type == DVP2AXI_FMT_TYPE_RAW) {
			fmt = es_dvp2axi_find_output_fmt(stream,
						    stream->pixm.pixelformat);
			crop_vwidth = raw_width *
				      es_dvp2axi_cal_raw_vir_line_ratio(stream, fmt);
		}
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_VIR_LINE_WIDTH,
				     crop_vwidth);

		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_SET_SIZE,
				     crop_height << 16 | crop_width);
	}

	stream->crop_dyn_en = false;
}

static void es_dvp2axi_monitor_reset_event(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_stream *stream = NULL;
	struct es_dvp2axi_timer *timer = &dev->reset_watchdog_timer;
	unsigned int cycle = 0;
	u64 fps, timestamp0, timestamp1;
	unsigned long flags, fps_flags;
	int i = 0;

	if (timer->is_running)
		return;

	if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_IDLE)
		return;

	for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
		stream = &dev->stream[i];
		if (stream->state == ES_DVP2AXI_STATE_STREAMING)
			break;
	}

	if (i >= ES_DVP2AXI_MAX_STREAM_MIPI)
		return;

	timer->is_triggered = es_dvp2axi_is_triggered_monitoring(dev);

	if (timer->is_triggered) {
		struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
		enum es_dvp2axi_monitor_mode mode;
		s32 vblank = 0;
		u32 vts = 0;
		u64 numerator = 0;
		u64 denominator = 0;

		if (stream->frame_idx > 2) {
			spin_lock_irqsave(&stream->fps_lock, fps_flags);
			timestamp0 = stream->fps_stats.frm0_timestamp;
			timestamp1 = stream->fps_stats.frm1_timestamp;
			spin_unlock_irqrestore(&stream->fps_lock, fps_flags);

			fps = timestamp0 > timestamp1 ?
				      timestamp0 - timestamp1 :
				      timestamp1 - timestamp0;
			fps = div_u64(fps, 1000);
		} else {
			numerator = dev->terminal_sensor.fi.interval.numerator;
			denominator =
				dev->terminal_sensor.fi.interval.denominator;
			fps = div_u64(1000000 * numerator, denominator);
		}
		spin_lock_irqsave(&timer->timer_lock, flags);

		timer->frame_end_cycle_us = fps;

		vblank = es_dvp2axi_get_sensor_vblank(dev);
		timer->raw_height = raw_rect->height;
		vts = timer->raw_height + vblank;
		timer->vts = vts;

		timer->line_end_cycle =
			div_u64(timer->frame_end_cycle_us, timer->vts);
		fps = div_u64(timer->frame_end_cycle_us, 1000);
		cycle = fps * timer->frm_num_of_monitor_cycle;
		timer->cycle = msecs_to_jiffies(cycle);

		timer->run_cnt = 0;
		timer->is_running = true;
		timer->is_buf_stop_update = false;
		for (i = 0; i < dev->num_channels; i++) {
			stream = &dev->stream[i];
			if (stream->state == ES_DVP2AXI_STATE_STREAMING)
				timer->last_buf_wakeup_cnt[i] =
					stream->buf_wake_up_cnt;
		}
		/* in trigger mode, monitoring count is fps */
		mode = timer->monitor_mode;
		if (mode == ES_DVP2AXI_MONITOR_MODE_CONTINUE ||
		    mode == ES_DVP2AXI_MONITOR_MODE_HOTPLUG)
			timer->max_run_cnt = 0xffffffff - DVP2AXI_TIMEOUT_FRAME_NUM;
		else
			timer->max_run_cnt = div_u64(1000, fps) * 1;

		timer->timer.expires = jiffies + timer->cycle;
		mod_timer(&timer->timer, timer->timer.expires);

		spin_unlock_irqrestore(&timer->timer_lock, flags);

		v4l2_dbg(
			3, es_dvp2axi_debug, &dev->v4l2_dev,
			"%s:mode:%d, raw height:%d,vblank:%d, cycle:%ld, fps:%llu\n",
			__func__, timer->monitor_mode, raw_rect->height, vblank,
			timer->cycle, div_u64(1000, fps));
	}
}

static void es_dvp2axi_rdbk_frame_end(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor = &stream->dvp2axidev->terminal_sensor;
	u32 denominator, numerator;
	u64 l_ts, m_ts, s_ts, time = 30000000LL;
	int ret, fps = -1;
	int i = 0;
	unsigned long flags;

	if (dev->hdr.hdr_mode == HDR_X2) {
		if (stream->id != ES_DVP2AXI_STREAM_MIPI_ID1 ||
		    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].state !=
			    ES_DVP2AXI_STATE_STREAMING ||
		    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1].state !=
			    ES_DVP2AXI_STATE_STREAMING)
			return;
	} else if (dev->hdr.hdr_mode == HDR_X3) {
		if (stream->id != ES_DVP2AXI_STREAM_MIPI_ID2 ||
		    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].state !=
			    ES_DVP2AXI_STATE_STREAMING ||
		    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1].state !=
			    ES_DVP2AXI_STATE_STREAMING ||
		    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID2].state !=
			    ES_DVP2AXI_STATE_STREAMING)
			return;
	}

	numerator = sensor->fi.interval.numerator;
	denominator = sensor->fi.interval.denominator;
	if (denominator && numerator)
		time = numerator * 1000 / denominator * 1000 * 1000;

	if (dev->hdr.hdr_mode == HDR_X3) {
		if (dev->rdbk_buf[RDBK_L] && dev->rdbk_buf[RDBK_M] &&
		    dev->rdbk_buf[RDBK_S]) {
			l_ts = dev->rdbk_buf[RDBK_L]->fe_timestamp;
			m_ts = dev->rdbk_buf[RDBK_M]->fe_timestamp;
			s_ts = dev->rdbk_buf[RDBK_S]->fe_timestamp;

			if (m_ts < l_ts || s_ts < m_ts) {
				v4l2_err(
					&dev->v4l2_dev,
					"s/m/l frame err, timestamp s:%lld m:%lld l:%lld\n",
					s_ts, m_ts, l_ts);
				return;
			}

			if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd, video,
						       g_frame_interval,
						       &sensor->fi);
				if (!ret) {
					denominator =
						sensor->fi.interval.denominator;
					numerator =
						sensor->fi.interval.numerator;
					if (denominator && numerator) {
						time = numerator * 1000 /
						       denominator * 1000 *
						       1000;
						fps = denominator / numerator;
					}
				}

				if ((m_ts - l_ts) > time ||
				    (s_ts - m_ts) > time) {
					v4l2_err(
						&dev->v4l2_dev,
						"timestamp no match, s:%lld m:%lld l:%lld, fps:%d\n",
						s_ts, m_ts, l_ts, fps);
					return;
				}
			}
			dev->rdbk_buf[RDBK_M]->vb.sequence =
				dev->rdbk_buf[RDBK_L]->vb.sequence;
			dev->rdbk_buf[RDBK_S]->vb.sequence =
				dev->rdbk_buf[RDBK_L]->vb.sequence;
			if (dev->is_support_tools &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0]
					    .tools_vdev->state ==
				    ES_DVP2AXI_STATE_STREAMING &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1]
					    .tools_vdev->state ==
				    ES_DVP2AXI_STATE_STREAMING &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID2]
					    .tools_vdev->state ==
				    ES_DVP2AXI_STATE_STREAMING) {
				for (i = 0; i < 3; i++) {
					spin_lock_irqsave(
						&dev->stream[i]
							 .tools_vdev->vbq_lock,
						flags);
					list_add_tail(&dev->rdbk_buf[i]->queue,
						      &dev->stream[i]
							       .tools_vdev
							       ->buf_done_head);
					if (!work_busy(
						    &dev->stream[i]
							     .tools_vdev->work))
						schedule_work(
							&dev->stream[i]
								 .tools_vdev
								 ->work);
					spin_unlock_irqrestore(
						&dev->stream[i]
							 .tools_vdev->vbq_lock,
						flags);
				}
			} else {
				es_dvp2axi_vb_done_tasklet(
					&dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0],
					dev->rdbk_buf[RDBK_L]);
				es_dvp2axi_vb_done_tasklet(
					&dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1],
					dev->rdbk_buf[RDBK_M]);
				es_dvp2axi_vb_done_tasklet(
					&dev->stream[ES_DVP2AXI_STREAM_MIPI_ID2],
					dev->rdbk_buf[RDBK_S]);
			}
		} else {
			if (!dev->rdbk_buf[RDBK_L])
				v4l2_err(&dev->v4l2_dev, "lost long frames\n");
			if (!dev->rdbk_buf[RDBK_M])
				v4l2_err(&dev->v4l2_dev,
					 "lost medium frames\n");
			if (!dev->rdbk_buf[RDBK_S])
				v4l2_err(&dev->v4l2_dev, "lost short frames\n");
			return;
		}
	} else if (dev->hdr.hdr_mode == HDR_X2) {
		if (dev->rdbk_buf[RDBK_L] && dev->rdbk_buf[RDBK_M]) {
			l_ts = dev->rdbk_buf[RDBK_L]->fe_timestamp;
			s_ts = dev->rdbk_buf[RDBK_M]->fe_timestamp;

			if (s_ts < l_ts) {
				v4l2_err(
					&dev->v4l2_dev,
					"s/l frame err, timestamp s:%lld l:%lld\n",
					s_ts, l_ts);
				return;
			}

			if ((s_ts - l_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd, video,
						       g_frame_interval,
						       &sensor->fi);
				if (!ret) {
					denominator =
						sensor->fi.interval.denominator;
					numerator =
						sensor->fi.interval.numerator;
					if (denominator && numerator) {
						time = numerator * 1000 /
						       denominator * 1000 *
						       1000;
						fps = denominator / numerator;
					}
				}
				if ((s_ts - l_ts) > time) {
					v4l2_err(
						&dev->v4l2_dev,
						"timestamp no match, s:%lld l:%lld, fps:%d\n",
						s_ts, l_ts, fps);
					return;
				}
			}
			dev->rdbk_buf[RDBK_M]->vb.sequence =
				dev->rdbk_buf[RDBK_L]->vb.sequence;
			if (dev->is_support_tools &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0]
					    .tools_vdev->state ==
				    ES_DVP2AXI_STATE_STREAMING &&
			    dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1]
					    .tools_vdev->state ==
				    ES_DVP2AXI_STATE_STREAMING) {
				for (i = 0; i < 2; i++) {
					spin_lock_irqsave(
						&dev->stream[i]
							 .tools_vdev->vbq_lock,
						flags);
					list_add_tail(&dev->rdbk_buf[i]->queue,
						      &dev->stream[i]
							       .tools_vdev
							       ->buf_done_head);
					if (!work_busy(
						    &dev->stream[i]
							     .tools_vdev->work))
						schedule_work(
							&dev->stream[i]
								 .tools_vdev
								 ->work);
					spin_unlock_irqrestore(
						&dev->stream[i]
							 .tools_vdev->vbq_lock,
						flags);
				}
			} else {
				es_dvp2axi_vb_done_tasklet(
					&dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0],
					dev->rdbk_buf[RDBK_L]);
				es_dvp2axi_vb_done_tasklet(
					&dev->stream[ES_DVP2AXI_STREAM_MIPI_ID1],
					dev->rdbk_buf[RDBK_M]);
			}
		} else {
			if (!dev->rdbk_buf[RDBK_L])
				v4l2_err(&dev->v4l2_dev, "lost long frames\n");
			if (!dev->rdbk_buf[RDBK_M])
				v4l2_err(&dev->v4l2_dev, "lost short frames\n");
			return;
		}
	}

	dev->rdbk_buf[RDBK_L] = NULL;
	dev->rdbk_buf[RDBK_M] = NULL;
	dev->rdbk_buf[RDBK_S] = NULL;
}

static void es_dvp2axi_buf_done_with_tools(struct es_dvp2axi_stream *stream,
				      struct es_dvp2axi_buffer *active_buf)
{
	unsigned long flags;

	spin_lock_irqsave(&stream->tools_vdev->vbq_lock, flags);
	if (stream->tools_vdev->state == ES_DVP2AXI_STATE_STREAMING) {
		list_add_tail(&active_buf->queue,
			      &stream->tools_vdev->buf_done_head);
		if (!work_busy(&stream->tools_vdev->work))
			schedule_work(&stream->tools_vdev->work);
	} else {
		es_dvp2axi_vb_done_tasklet(stream, active_buf);
	}
	spin_unlock_irqrestore(&stream->tools_vdev->vbq_lock, flags);
}

static void es_dvp2axi_buf_done_prepare(struct es_dvp2axi_stream *stream,
				   struct es_dvp2axi_buffer *active_buf, int mipi_id,
				   u32 mode)
{
	unsigned long flags;
	struct vb2_v4l2_buffer *vb_done = NULL;
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;

	if (active_buf) {
		vb_done = &active_buf->vb;
		if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI &&
		    dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_BT656)
			vb_done->vb2_buf.timestamp =
				stream->readout.fe_timestamp;
		else
			vb_done->vb2_buf.timestamp =
				stream->readout.fs_timestamp;
		vb_done->sequence = stream->frame_idx - 1;
		active_buf->fe_timestamp = es_dvp2axi_time_get_ns(dvp2axi_dev);
		if (stream->is_line_wake_up) {
			spin_lock_irqsave(&stream->fps_lock, flags);
			if (mode)
				stream->fps_stats.frm0_timestamp =
					vb_done->vb2_buf.timestamp;
			else
				stream->fps_stats.frm1_timestamp =
					vb_done->vb2_buf.timestamp;
			stream->readout.wk_timestamp =
				vb_done->vb2_buf.timestamp;
			spin_unlock_irqrestore(&stream->fps_lock, flags);
		}
		if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED)
			vb_done->sequence /= 2;
		if (stream->cur_skip_frame) {
			es_dvp2axi_buf_queue(&active_buf->vb.vb2_buf);
			return;
		}
	} else if (dvp2axi_dev->rdbk_buf[stream->id]) {
		vb_done = &dvp2axi_dev->rdbk_buf[stream->id]->vb;
		if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI &&
		    dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_BT656)
			vb_done->vb2_buf.timestamp =
				stream->readout.fe_timestamp;
		else
			vb_done->vb2_buf.timestamp =
				stream->readout.fs_timestamp;
		vb_done->sequence = stream->frame_idx - 1;
		dvp2axi_dev->rdbk_buf[stream->id]->fe_timestamp =
			es_dvp2axi_time_get_ns(dvp2axi_dev);
	}

	if (dvp2axi_dev->hdr.hdr_mode == NO_HDR ||
	    dvp2axi_dev->hdr.hdr_mode == HDR_COMPR) {
		if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED) {
			if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY &&
			    active_buf) {
				if (dvp2axi_dev->is_support_tools &&
				    stream->tools_vdev)
					es_dvp2axi_buf_done_with_tools(stream,
								  active_buf);
				else
					es_dvp2axi_vb_done_tasklet(stream,
							      active_buf);
			}
		} else {
			if (active_buf) {
				if (dvp2axi_dev->is_support_tools &&
				    stream->tools_vdev)
					es_dvp2axi_buf_done_with_tools(stream,
								  active_buf);
				else
					es_dvp2axi_vb_done_tasklet(stream,
							      active_buf);
			}
		}
	} else {
		if (dvp2axi_dev->is_start_hdr) {
			spin_lock_irqsave(&dvp2axi_dev->hdr_lock, flags);
			if (mipi_id == ES_DVP2AXI_STREAM_MIPI_ID0) {
				if (dvp2axi_dev->rdbk_buf[RDBK_L] && active_buf) {
					v4l2_err(
						&dvp2axi_dev->v4l2_dev,
						"multiple long data in %s frame,frm_idx:%d,state:0x%x\n",
						dvp2axi_dev->hdr.hdr_mode ==
								HDR_X2 ?
							"hdr_x2" :
							"hdr_x3",
						stream->frame_idx,
						dvp2axi_dev->rdbk_buf[RDBK_L]
							->vb.vb2_buf.state);
					dvp2axi_dev->rdbk_buf[RDBK_L]
						->vb.vb2_buf.state =
						VB2_BUF_STATE_ACTIVE;
					es_dvp2axi_buf_queue(
						&dvp2axi_dev->rdbk_buf[RDBK_L]
							 ->vb.vb2_buf);
					dvp2axi_dev->rdbk_buf[RDBK_L] = NULL;
				}
				if (active_buf)
					dvp2axi_dev->rdbk_buf[RDBK_L] = active_buf;
			} else if (mipi_id == ES_DVP2AXI_STREAM_MIPI_ID1) {
				if (dvp2axi_dev->rdbk_buf[RDBK_M] && active_buf) {
					v4l2_err(
						&dvp2axi_dev->v4l2_dev,
						"multiple %s frame,frm_idx:%d,state:0x%x\n",
						dvp2axi_dev->hdr.hdr_mode ==
								HDR_X2 ?
							"short data in hdr_x2" :
							"medium data in hdr_x3",
						stream->frame_idx,
						dvp2axi_dev->rdbk_buf[RDBK_M]
							->vb.vb2_buf.state);
					dvp2axi_dev->rdbk_buf[RDBK_M]
						->vb.vb2_buf.state =
						VB2_BUF_STATE_ACTIVE;
					es_dvp2axi_buf_queue(
						&dvp2axi_dev->rdbk_buf[RDBK_M]
							 ->vb.vb2_buf);
					dvp2axi_dev->rdbk_buf[RDBK_M] = NULL;
				}
				if (active_buf)
					dvp2axi_dev->rdbk_buf[RDBK_M] = active_buf;
				if (dvp2axi_dev->hdr.hdr_mode == HDR_X2)
					es_dvp2axi_rdbk_frame_end(stream);
			} else if (mipi_id == ES_DVP2AXI_STREAM_MIPI_ID2) {
				if (dvp2axi_dev->rdbk_buf[RDBK_S] && active_buf) {
					v4l2_err(
						&dvp2axi_dev->v4l2_dev,
						"multiple %s frame, frm_idx:%d,state:0x%x\n",
						dvp2axi_dev->hdr.hdr_mode ==
								HDR_X2 ?
							"err short data in hdr_x3" :
							"short data in hdr_x3",
						stream->frame_idx,
						dvp2axi_dev->rdbk_buf[RDBK_S]
							->vb.vb2_buf.state);
					dvp2axi_dev->rdbk_buf[RDBK_S]
						->vb.vb2_buf.state =
						VB2_BUF_STATE_ACTIVE;
					es_dvp2axi_buf_queue(
						&dvp2axi_dev->rdbk_buf[RDBK_S]
							 ->vb.vb2_buf);
					dvp2axi_dev->rdbk_buf[RDBK_S] = NULL;
				}
				if (active_buf)
					dvp2axi_dev->rdbk_buf[RDBK_S] = active_buf;
				if (dvp2axi_dev->hdr.hdr_mode == HDR_X3)
					es_dvp2axi_rdbk_frame_end(stream);
			}
			spin_unlock_irqrestore(&dvp2axi_dev->hdr_lock, flags);
		} else {
			if (active_buf) {
				vb_done->vb2_buf.state = VB2_BUF_STATE_ACTIVE;
				es_dvp2axi_buf_queue(&vb_done->vb2_buf);
			}

			v4l2_info(
				&dvp2axi_dev->v4l2_dev,
				"warning:hdr runs stream[%d], stream[0]:%s stream[1]:%s stream[2]:%s stream[3]:%s\n",
				stream->id,
				dvp2axi_dev->stream[0].state !=
						ES_DVP2AXI_STATE_STREAMING ?
					"stopped" :
					"running",
				dvp2axi_dev->stream[1].state !=
						ES_DVP2AXI_STATE_STREAMING ?
					"stopped" :
					"running",
				dvp2axi_dev->stream[2].state !=
						ES_DVP2AXI_STATE_STREAMING ?
					"stopped" :
					"running",
				dvp2axi_dev->stream[3].state !=
						ES_DVP2AXI_STATE_STREAMING ?
					"stopped" :
					"running");
		}
	}
}

static void es_dvp2axi_line_wake_up(struct es_dvp2axi_stream *stream, int mipi_id)
{
	u32 mode;
	struct es_dvp2axi_buffer *active_buf = NULL;
	int ret = 0;

	mode = stream->line_int_cnt % 2;
	if (mode) {
		if (stream->curr_buf)
			active_buf = stream->curr_buf;
	} else {
		if (stream->next_buf)
			active_buf = stream->next_buf;
	}

	if (stream->stopping) {
		stream->is_can_stop = true;
		return;
	}
	ret = es_dvp2axi_get_new_buffer_wake_up_mode(stream);
	if (ret)
		return;
	es_dvp2axi_buf_done_prepare(stream, active_buf, mipi_id, mode);
}

static void es_dvp2axi_store_last_buf_for_online(struct es_dvp2axi_stream *stream,
					    struct es_dvp2axi_rx_buffer *buf)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	u32 frm0_addr_y, frm1_addr_y;

	INIT_LIST_HEAD(&stream->rx_buf_head);
	stream->curr_buf_toisp = buf;
	stream->next_buf_toisp = buf;
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm0_addr_y = get_reg_index_of_frm0_y_addr(stream->id);
		frm1_addr_y = get_reg_index_of_frm1_y_addr(stream->id);
	} else {
		frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(stream->id);
		frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(stream->id);
	}
	es_dvp2axi_write_register(dev, frm0_addr_y, buf->dummy.dma_addr);
	es_dvp2axi_write_register(dev, frm1_addr_y, buf->dummy.dma_addr);
}

static void
es_dvp2axi_release_unnecessary_buf_for_online(struct es_dvp2axi_stream *stream,
					 struct es_dvp2axi_rx_buffer *buf)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct sditf_priv *priv = dev->sditf[0];
	struct es_dvp2axi_rx_buffer *rx_buf = NULL;
	unsigned long flags;
	int i = 0;

	spin_lock_irqsave(&priv->dvp2axi_dev->buffree_lock, flags);
	for (i = 0; i < priv->buf_num; i++) {
		rx_buf = &stream->rx_buf[i];
		if (rx_buf && (!rx_buf->dummy.is_free) && rx_buf != buf) {
			list_add_tail(&rx_buf->list_free, &priv->buf_free_list);
			stream->total_buf_num--;
		}
	}
	spin_unlock_irqrestore(&priv->dvp2axi_dev->buffree_lock, flags);
	schedule_work(&priv->buffree_work.work);
}

static void es_dvp2axi_line_wake_up_rdbk(struct es_dvp2axi_stream *stream, int mipi_id)
{
	u32 mode;
	struct es_dvp2axi_rx_buffer *active_buf = NULL;
	struct sditf_priv *priv = NULL;
	unsigned long flags;
	int ret = 0;

	mode = stream->line_int_cnt % 2;
	if (mode) {
		if (stream->curr_buf_toisp)
			active_buf = stream->curr_buf_toisp;
		stream->frame_phase = DVP2AXI_CSI_FRAME0_READY;
	} else {
		if (stream->next_buf_toisp)
			active_buf = stream->next_buf_toisp;
		stream->frame_phase = DVP2AXI_CSI_FRAME1_READY;
	}

	if (!active_buf) {
		v4l2_err(&stream->dvp2axidev->v4l2_dev, "err buffer state in %s\n",
			 __func__);
		return;
	}

	if (stream->stopping) {
		stream->is_can_stop = true;
		return;
	}
	ret = es_dvp2axi_get_new_buffer_wake_up_mode_rdbk(stream);
	v4l2_dbg(3, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "%d frame_idx %d, last_rx_buf_idx %d cur dma buf %x\n",
		 __LINE__, stream->frame_idx, stream->last_rx_buf_idx,
		 (u32)active_buf->dummy.dma_addr);
	if (!ret) {
		priv = stream->dvp2axidev->sditf[0];
		if (stream->cur_stream_mode & ES_DVP2AXI_STREAM_MODE_TOISP_RDBK) {
			spin_lock_irqsave(&stream->vbq_lock, flags);
			if (stream->dvp2axidev->is_thunderboot &&
			    (stream->frame_idx - 1) ==
				    stream->last_rx_buf_idx &&
			    stream->dvp2axidev->is_rdbk_to_online) {
				stream->cur_stream_mode &=
					~ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
				stream->cur_stream_mode |=
					ES_DVP2AXI_STREAM_MODE_TOISP;
				stream->dvp2axidev->wait_line = 0;
				stream->is_line_wake_up = false;
				if (stream->dvp2axidev->hdr.hdr_mode == NO_HDR ||
				    (priv->hdr_cfg.hdr_mode == HDR_X2 &&
				     stream->id == 1) ||
				    (priv->hdr_cfg.hdr_mode == HDR_X3 &&
				     stream->id == 2)) {
					stream->to_stop_dma =
						ES_DVP2AXI_DMAEN_BY_ISP;
					es_dvp2axi_stop_dma_capture(stream);
				}
				active_buf->dbufs.is_switch = true;
				if ((priv->hdr_cfg.hdr_mode == HDR_X2 &&
				     stream->id != 1) ||
				    (priv->hdr_cfg.hdr_mode == HDR_X3 &&
				     stream->id != 2)) {
					es_dvp2axi_store_last_buf_for_online(
						stream, active_buf);
					stream->is_change_toisp = true;
				}
			}
			spin_unlock_irqrestore(&stream->vbq_lock, flags);
			active_buf->dbufs.sequence = stream->frame_idx - 1;
			active_buf->dbufs.timestamp =
				stream->readout.fs_timestamp;
			active_buf->fe_timestamp =
				es_dvp2axi_time_get_ns(stream->dvp2axidev);
			stream->last_frame_idx = stream->frame_idx;
			if (stream->dvp2axidev->hdr.hdr_mode == NO_HDR) {
				es_dvp2axi_s_rx_buffer(stream, &active_buf->dbufs);
				if (stream->dvp2axidev->is_support_tools &&
				    stream->tools_vdev)
					es_dvp2axi_rdbk_with_tools(stream,
							      active_buf);
			} else {
				es_dvp2axi_rdbk_frame_end_toisp(stream, active_buf);
			}
		}
	}
}

static void es_dvp2axi_deal_readout_time(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct es_dvp2axi_stream *detect_stream = &dvp2axi_dev->stream[0];
	unsigned long flags;

	spin_lock_irqsave(&stream->fps_lock, flags);
	stream->readout.fe_timestamp = es_dvp2axi_time_get_ns(dvp2axi_dev);

	if (dvp2axi_dev->inf_id == ES_DVP2AXI_DVP) {
		spin_unlock_irqrestore(&stream->fps_lock, flags);
		return;
	}

	if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID0)
		detect_stream->readout.readout_time =
			stream->readout.fe_timestamp -
			stream->readout.fs_timestamp;

	if ((dvp2axi_dev->hdr.hdr_mode == NO_HDR ||
	     dvp2axi_dev->hdr.hdr_mode == HDR_COMPR) &&
	    (stream->id == ES_DVP2AXI_STREAM_MIPI_ID0)) {
		detect_stream->readout.early_time =
			stream->readout.fe_timestamp -
			stream->readout.wk_timestamp;

	} else if ((dvp2axi_dev->hdr.hdr_mode == HDR_X2) &&
		   (stream->id == ES_DVP2AXI_STREAM_MIPI_ID1)) {
		detect_stream->readout.early_time =
			stream->readout.fe_timestamp -
			stream->readout.wk_timestamp;
		detect_stream->readout.total_time =
			stream->readout.fe_timestamp -
			detect_stream->readout.fe_timestamp;
		detect_stream->readout.total_time +=
			detect_stream->readout.readout_time;
	} else if ((dvp2axi_dev->hdr.hdr_mode == HDR_X3) &&
		   (stream->id == ES_DVP2AXI_STREAM_MIPI_ID2)) {
		detect_stream->readout.early_time =
			stream->readout.fe_timestamp -
			stream->readout.wk_timestamp;
		detect_stream->readout.total_time =
			stream->readout.fe_timestamp -
			detect_stream->readout.fe_timestamp;
		detect_stream->readout.total_time +=
			detect_stream->readout.readout_time;
	}
	if (!stream->is_line_wake_up)
		detect_stream->readout.early_time = 0;
	spin_unlock_irqrestore(&stream->fps_lock, flags);
}

static void es_dvp2axi_update_stream(struct es_dvp2axi_device *dvp2axi_dev,
				struct es_dvp2axi_stream *stream, int mipi_id)
{
	struct es_dvp2axi_buffer *active_buf = NULL;
	unsigned long flags;
	int ret = 0;

	if (stream->frame_phase ==
	    (DVP2AXI_CSI_FRAME0_READY | DVP2AXI_CSI_FRAME1_READY)) {
		dvp2axi_dev->err_state |= (ES_DVP2AXI_ERR_ID0_TRIG_SIMULT << stream->id);
		dvp2axi_dev->irq_stats.trig_simult_cnt[stream->id]++;
		return;
	}
	if (!stream->is_line_wake_up) {
		spin_lock_irqsave(&stream->fps_lock, flags);
		if (stream->frame_phase & DVP2AXI_CSI_FRAME0_READY) {
			if (stream->curr_buf)
				active_buf = stream->curr_buf;
			stream->fps_stats.frm0_timestamp =
				es_dvp2axi_time_get_ns(dvp2axi_dev);
		} else if (stream->frame_phase & DVP2AXI_CSI_FRAME1_READY) {
			if (stream->next_buf)
				active_buf = stream->next_buf;
			stream->fps_stats.frm1_timestamp =
				es_dvp2axi_time_get_ns(dvp2axi_dev);
		}
		spin_unlock_irqrestore(&stream->fps_lock, flags);
	}

	es_dvp2axi_deal_readout_time(stream);

	if (!stream->is_line_wake_up) {
		ret = es_dvp2axi_assign_new_buffer_pingpong(
			stream, ES_DVP2AXI_YUV_ADDR_STATE_UPDATE, mipi_id);
		if (ret && dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI)
			return;
	} else {
		ret = es_dvp2axi_update_new_buffer_wake_up_mode(stream);
		if (ret && dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI)
			return;
	}
	if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI &&
	    dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_BT656 &&
	    stream->id != 0)
		stream->frame_idx++;
	if (!stream->is_line_wake_up)
		es_dvp2axi_buf_done_prepare(stream, active_buf, mipi_id, 0);
}

static void es_dvp2axi_update_stream_toisp(struct es_dvp2axi_device *dvp2axi_dev,
				      struct es_dvp2axi_stream *stream, int mipi_id)
{
	if (stream->frame_phase ==
	    (DVP2AXI_CSI_FRAME0_READY | DVP2AXI_CSI_FRAME1_READY)) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			 "stream[%d], frm0/frm1 end simultaneously,frm id:%d\n",
			 stream->id, stream->frame_idx);
		return;
	}

	spin_lock(&stream->fps_lock);
	if (stream->frame_phase & DVP2AXI_CSI_FRAME0_READY)
		stream->fps_stats.frm0_timestamp = es_dvp2axi_time_get_ns(dvp2axi_dev);
	else if (stream->frame_phase & DVP2AXI_CSI_FRAME1_READY)
		stream->fps_stats.frm1_timestamp = es_dvp2axi_time_get_ns(dvp2axi_dev);
	spin_unlock(&stream->fps_lock);

	if (dvp2axi_dev->inf_id == ES_DVP2AXI_MIPI_LVDS)
		es_dvp2axi_deal_readout_time(stream);

	if (!stream->is_line_wake_up)
		es_dvp2axi_assign_new_buffer_pingpong_toisp(
			stream, ES_DVP2AXI_YUV_ADDR_STATE_UPDATE, mipi_id);
}

// static void es_dvp2axi_update_stream_eskit(struct es_dvp2axi_device *dvp2axi_dev,
// 				       struct es_dvp2axi_stream *stream, int mipi_id)
// {
// 	struct es_dvp2axi_buffer *active_buf = NULL;
// 	unsigned long flags;
// 	int ret = 0;

// 	if (stream->frame_phase ==
// 	    (DVP2AXI_CSI_FRAME0_READY | DVP2AXI_CSI_FRAME1_READY)) {
// 		v4l2_err(&dvp2axi_dev->v4l2_dev,
// 			 "stream[%d], frm0/frm1 end simultaneously,frm id:%d\n",
// 			 stream->id, stream->frame_idx);
// 		return;
// 	}
// 	if (!stream->is_line_wake_up) {
// 		spin_lock_irqsave(&stream->fps_lock, flags);
// 		if (stream->frame_phase & DVP2AXI_CSI_FRAME0_READY) {
// 			if (stream->curr_buf_eskit)
// 				active_buf = stream->curr_buf_eskit;
// 			stream->fps_stats.frm0_timestamp =
// 				es_dvp2axi_time_get_ns(dvp2axi_dev);
// 		} else if (stream->frame_phase & DVP2AXI_CSI_FRAME1_READY) {
// 			if (stream->next_buf_eskit)
// 				active_buf = stream->next_buf_eskit;
// 			stream->fps_stats.frm1_timestamp =
// 				es_dvp2axi_time_get_ns(dvp2axi_dev);
// 		}
// 		spin_unlock_irqrestore(&stream->fps_lock, flags);
// 	}

// 	if (dvp2axi_dev->inf_id == ES_DVP2AXI_MIPI_LVDS)
// 		es_dvp2axi_deal_readout_time(stream);

// 	ret = es_dvp2axi_assign_new_buffer_pingpong_eskit(
// 		stream, ES_DVP2AXI_YUV_ADDR_STATE_UPDATE, mipi_id);
// 	if (ret)
// 		return;

// 	if (active_buf) {
// 		active_buf->vb.vb2_buf.timestamp = stream->readout.fs_timestamp;
// 		active_buf->vb.sequence = stream->frame_idx - 1;
// 		es_dvp2axi_eskit_buf_done(stream, active_buf);
// 	}
// }

static u32 es_dvp2axi_get_sof(struct es_dvp2axi_device *dvp2axi_dev)
{
	u32 val = 0x0;
	struct es_dvp2axi_sensor_info *sensor = dvp2axi_dev->active_sensor;
	// struct csi2_dev *csi;

	if (sensor->mbus.type == V4L2_MBUS_CCP2) {
		val = es_dvp2axi_lvds_get_sof(dvp2axi_dev);
	} else if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		   sensor->mbus.type == V4L2_MBUS_BT656) {
		val = es_dvp2axi_dvp_get_sof(dvp2axi_dev);
	}
	return val;
}

static void es_dvp2axi_set_sof(struct es_dvp2axi_device *dvp2axi_dev, u32 seq)
{
	struct es_dvp2axi_sensor_info *sensor = dvp2axi_dev->active_sensor;
	// struct csi2_dev *csi;

	if (sensor->mbus.type == V4L2_MBUS_CCP2) {
		es_dvp2axi_lvds_set_sof(dvp2axi_dev, seq);
	} else if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		   sensor->mbus.type == V4L2_MBUS_BT656) {
		es_dvp2axi_dvp_set_sof(dvp2axi_dev, seq);
	}
}

static void es_dvp2axi_toisp_set_stream(struct es_dvp2axi_device *dev, int on)
{
	struct v4l2_subdev *sd = get_esisp_sd(dev->sditf[0]);

	if (sd)
		v4l2_subdev_call(sd, core, ioctl, ESISP_VICAP_CMD_SET_STREAM,
				 &on);
}

static int es_dvp2axi_do_reset_work(struct es_dvp2axi_device *dvp2axi_dev,
			       enum esmodule_reset_src reset_src)
{
	struct es_dvp2axi_pipeline *p = &dvp2axi_dev->pipe;
	struct es_dvp2axi_stream *stream = NULL;
	struct es_dvp2axi_stream *resume_stream[ES_DVP2AXI_MAX_STREAM_MIPI] = { NULL };
	struct es_dvp2axi_sensor_info *terminal_sensor = &dvp2axi_dev->terminal_sensor;
	struct es_dvp2axi_resume_info *resume_info =
		&dvp2axi_dev->reset_work.resume_info;
	struct es_dvp2axi_timer *timer = &dvp2axi_dev->reset_watchdog_timer;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	int i, j, ret = 0;
	u32 on, sof_cnt;
	int capture_mode = 0;

	mutex_lock(&dvp2axi_dev->stream_lock);
	if (dvp2axi_dev->reset_work_cancel) {
		ret = 0;
		goto unlock_stream;
	}
	v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev, "do es_dvp2axi reset\n");

	for (i = 0, j = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
		stream = &dvp2axi_dev->stream[i];

		if (stream->state == ES_DVP2AXI_STATE_STREAMING) {
			v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				 "stream[%d] stopping\n", stream->id);

			es_dvp2axi_stream_stop(stream);

			if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID0) {
				sof_cnt = es_dvp2axi_get_sof(dvp2axi_dev);
				v4l2_dbg(
					1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					"%s: stream[%d] sync frmid & csi_sof, frm_id:%d, csi_sof:%d\n",
					__func__, stream->id, stream->frame_idx,
					sof_cnt);

				resume_info->frm_sync_seq = stream->frame_idx;
			}

			stream->state = ES_DVP2AXI_STATE_RESET_IN_STREAMING;
			stream->is_fs_fe_not_paired = false;
			stream->fs_cnt_in_single_frame = 0;
			resume_stream[j] = stream;
			j += 1;

			v4l2_dbg(
				1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				"%s stop stream[%d] in streaming, frm_id:%d, csi_sof:%d\n",
				__func__, stream->id, stream->frame_idx,
				es_dvp2axi_get_sof(dvp2axi_dev));
		}
	}

	on = 0;
	for (i = 0; i < p->num_subdevs; i++) {
		if (p->subdevs[i] == terminal_sensor->sd) {
			if (reset_src != ES_RESET_SRC_ERR_APP) {
				ret = v4l2_subdev_call(
					p->subdevs[i], core, ioctl,
					ESMODULE_SET_QUICK_STREAM, &on);
				if (ret)
					v4l2_dbg(
						1, es_dvp2axi_debug,
						&dvp2axi_dev->v4l2_dev,
						"quick stream off subdev:%s failed\n",
						p->subdevs[i]->name);
			}
		} else {
			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream,
					       on);
		}
		if (ret)
			v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				 "%s:stream %s subdev:%s failed\n", __func__,
				 on ? "on" : "off", p->subdevs[i]->name);
	}

	if (priv && priv->is_combine_mode &&
	    dvp2axi_dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF) {
		for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
			if (dvp2axi_dev->sditf[i] && dvp2axi_dev->sditf[i]->sensor_sd)
				ret = v4l2_subdev_call(
					dvp2axi_dev->sditf[i]->sensor_sd, core,
					ioctl, ESMODULE_SET_QUICK_STREAM, &on);
		}
	}

	if (dvp2axi_dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
		es_dvp2axi_do_soft_reset(dvp2axi_dev);
	} else {
		es_dvp2axi_do_cru_reset(dvp2axi_dev);

		es_dvp2axi_disable_sys_clk(dvp2axi_dev->hw_dev);

		udelay(5);

		ret = es_dvp2axi_enable_sys_clk(dvp2axi_dev->hw_dev);

		if (ret < 0) {
			v4l2_err(&dvp2axi_dev->v4l2_dev,
				 "%s:resume dvp2axi clk failed\n", __func__);
			goto unlock_stream;
		}
	}

	if (priv && priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
		es_dvp2axi_toisp_set_stream(dvp2axi_dev, 1);

	for (i = 0; i < j; i++) {
		stream = resume_stream[i];
		stream->fs_cnt_in_single_frame = 0;
		if (stream->dvp2axi_fmt_in->field == V4L2_FIELD_INTERLACED) {
			if (stream->curr_buf == stream->next_buf) {
				if (stream->curr_buf)
					list_add_tail(&stream->curr_buf->queue,
						      &stream->buf_head);
			} else {
				if (stream->curr_buf)
					list_add_tail(&stream->curr_buf->queue,
						      &stream->buf_head);
				if (stream->next_buf)
					list_add_tail(&stream->next_buf->queue,
						      &stream->buf_head);
			}
			stream->curr_buf = NULL;
			stream->next_buf = NULL;
		}
		if (!dvp2axi_dev->sditf[0] ||
		    dvp2axi_dev->sditf[0]->mode.rdbk_mode == ESISP_VICAP_RDBK_AIQ)
			capture_mode = ES_DVP2AXI_STREAM_MODE_CAPTURE;
		else {
			if (dvp2axi_dev->sditf[0]->mode.rdbk_mode ==
			    ESISP_VICAP_ONLINE)
				capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP;
			else
				capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
		}
		if (dvp2axi_dev->active_sensor &&
		    (dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
		     dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
		     dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)) {
			//  pr_info("%s:%d yfx !!!!!! call es_dvp2axi csi stream start \n", __func__, __LINE__);
			ret = es_dvp2axi_csi_stream_start(stream, capture_mode);
		}
		else
			ret = es_dvp2axi_stream_start(stream, capture_mode);
		if (ret) {
			v4l2_err(&dvp2axi_dev->v4l2_dev,
				 "%s:resume stream[%d] failed\n", __func__,
				 stream->id);
			goto unlock_stream;
		}

		v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "resume stream[%d], frm_idx:%d, csi_sof:%d\n",
			 stream->id, stream->frame_idx, es_dvp2axi_get_sof(dvp2axi_dev));
	}

	on = 1;
	for (i = 0; i < p->num_subdevs; i++) {
		if (p->subdevs[i] == terminal_sensor->sd) {
			es_dvp2axi_set_sof(dvp2axi_dev, resume_info->frm_sync_seq);

			if (reset_src != ES_RESET_SRC_ERR_APP) {
				ret = v4l2_subdev_call(
					p->subdevs[i], core, ioctl,
					ESMODULE_SET_QUICK_STREAM, &on);
				if (ret)
					v4l2_err(
						&dvp2axi_dev->v4l2_dev,
						"quick stream on subdev:%s failed\n",
						p->subdevs[i]->name);
			}
		} else {
			if (p->subdevs[i] == terminal_sensor->sd)
				es_dvp2axi_set_sof(dvp2axi_dev,
					      resume_info->frm_sync_seq);

			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream,
					       on);
		}

		if (ret)
			v4l2_err(&dvp2axi_dev->v4l2_dev, "reset subdev:%s failed\n",
				 p->subdevs[i]->name);
	}

	if (priv && priv->is_combine_mode &&
	    dvp2axi_dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF) {
		for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
			if (dvp2axi_dev->sditf[i] && dvp2axi_dev->sditf[i]->sensor_sd)
				v4l2_subdev_call(dvp2axi_dev->sditf[i]->sensor_sd,
						 core, ioctl,
						 ESMODULE_SET_QUICK_STREAM,
						 &on);
		}
	}

	if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI)
		es_dvp2axi_start_luma(
			&dvp2axi_dev->luma_vdev,
			dvp2axi_dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].dvp2axi_fmt_in);

	timer->csi2_err_triggered_cnt = 0;
	es_dvp2axi_monitor_reset_event(dvp2axi_dev);

	v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
		 "do es_dvp2axi reset successfully!\n");
	mutex_unlock(&dvp2axi_dev->stream_lock);
	return 0;

unlock_stream:
	mutex_unlock(&dvp2axi_dev->stream_lock);
	return ret;
}

void es_dvp2axi_reset_work(struct work_struct *work)
{
	struct es_dvp2axi_work_struct *reset_work =
		container_of(work, struct es_dvp2axi_work_struct, work);
	struct es_dvp2axi_device *dev =
		container_of(reset_work, struct es_dvp2axi_device, reset_work);
	int ret;

	ret = es_dvp2axi_do_reset_work(dev, reset_work->reset_src);
	if (ret)
		v4l2_info(&dev->v4l2_dev, "do reset work failed!\n");
}

static bool es_dvp2axi_is_reduced_frame_rate(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_timer *timer = &dev->reset_watchdog_timer;
	struct es_dvp2axi_stream *stream = &dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0];
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	u64 fps, timestamp0, timestamp1, diff_time;
	unsigned long fps_flags;
	unsigned int deviation = 1;
	bool is_reduced = false;
	u64 cur_time = 0;
	u64 time_distance = 0;

	spin_lock_irqsave(&stream->fps_lock, fps_flags);
	timestamp0 = stream->fps_stats.frm0_timestamp;
	timestamp1 = stream->fps_stats.frm1_timestamp;
	spin_unlock_irqrestore(&stream->fps_lock, fps_flags);

	fps = timestamp0 > timestamp1 ? timestamp0 - timestamp1 :
					timestamp1 - timestamp0;
	fps = div_u64(fps, 1000);
	diff_time = fps > timer->frame_end_cycle_us ?
			    fps - timer->frame_end_cycle_us :
			    0;
	deviation = DIV_ROUND_UP(timer->vts, 100);

	v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
		 "diff_time:%lld,devi_t:%ld,devi_h:%d\n", diff_time,
		 timer->line_end_cycle * deviation, deviation);

	cur_time = es_dvp2axi_time_get_ns(dev);
	time_distance = timestamp0 > timestamp1 ? cur_time - timestamp0 :
						  cur_time - timestamp1;
	time_distance = div_u64(time_distance, 1000);
	if (time_distance > fps * 2)
		return false;

	if (diff_time > timer->line_end_cycle * deviation) {
		s32 vblank = 0;
		unsigned int vts;

		is_reduced = true;
		vblank = es_dvp2axi_get_sensor_vblank(dev);
		vts = vblank + timer->raw_height;

		v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
			 "old vts:%d,new vts:%d\n", timer->vts, vts);

		v4l2_dbg(
			3, es_dvp2axi_debug, &dev->v4l2_dev,
			"reduce frame rate,vblank:%d, height(raw output):%d, fps:%lld, frm_end_t:%ld, line_t:%ld, diff:%lld\n",
			es_dvp2axi_get_sensor_vblank(dev), raw_rect->height, fps,
			timer->frame_end_cycle_us, timer->line_end_cycle,
			diff_time);

		timer->vts = vts;
		timer->frame_end_cycle_us = fps;
		timer->line_end_cycle =
			div_u64(timer->frame_end_cycle_us, timer->vts);
	} else {
		is_reduced = false;
	}

	timer->frame_end_cycle_us = fps;

	fps = div_u64(fps, 1000);
	fps = fps * timer->frm_num_of_monitor_cycle;
	timer->cycle = msecs_to_jiffies(fps);
	timer->timer.expires = jiffies + timer->cycle;

	return is_reduced;
}

static void es_dvp2axi_dvp_event_reset_pipe(struct es_dvp2axi_device *dev, int reset_src)
{
	struct es_dvp2axi_dvp_sof_subdev *subdev = &dev->dvp_sof_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_RESET_DEV,
			.reserved[0] = reset_src,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static void es_dvp2axi_lvds_event_reset_pipe(struct es_dvp2axi_device *dev, int reset_src)
{
	struct es_dvp2axi_lvds_subdev *subdev = &dev->lvds_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_RESET_DEV,
			.reserved[0] = reset_src,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static void es_dvp2axi_send_reset_event(struct es_dvp2axi_device *dvp2axi_dev, int reset_src)
{
	struct v4l2_mbus_config *mbus = &dvp2axi_dev->active_sensor->mbus;
	// struct csi2_dev *csi;

	if (mbus->type == V4L2_MBUS_CCP2) {
		es_dvp2axi_lvds_event_reset_pipe(dvp2axi_dev, reset_src);
	} else {
		es_dvp2axi_dvp_event_reset_pipe(dvp2axi_dev, reset_src);
	}
	v4l2_dbg(3, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
		 "send reset event,bus type 0x%x\n", mbus->type);
}

static void es_dvp2axi_init_reset_work(struct es_dvp2axi_timer *timer)
{
	struct es_dvp2axi_device *dev =
		container_of(timer, struct es_dvp2axi_device, reset_watchdog_timer);
	struct es_dvp2axi_stream *stream = NULL;
	unsigned long flags;
	int i = 0;

	v4l2_info(&dev->v4l2_dev,
		  "do reset work schedule, run_cnt:%d, reset source:%d\n",
		  timer->run_cnt, timer->reset_src);

	spin_lock_irqsave(&timer->timer_lock, flags);
	timer->is_running = false;
	timer->is_triggered = false;
	timer->csi2_err_cnt_odd = 0;
	timer->csi2_err_cnt_even = 0;
	timer->csi2_err_fs_fe_cnt = 0;
	timer->notifer_called_cnt = 0;
	dev->is_toisp_reset = false;
	for (i = 0; i < dev->num_channels; i++) {
		stream = &dev->stream[i];
		if (stream->state == ES_DVP2AXI_STATE_STREAMING)
			timer->last_buf_wakeup_cnt[stream->id] =
				stream->buf_wake_up_cnt;
	}
	spin_unlock_irqrestore(&timer->timer_lock, flags);
	if (timer->is_ctrl_by_user) {
		es_dvp2axi_send_reset_event(dev, timer->reset_src);
	} else {
		dev->reset_work.reset_src = timer->reset_src;
		if (!schedule_work(&dev->reset_work.work))
			v4l2_info(&dev->v4l2_dev,
				  "schedule reset work failed\n");
	}
}

static int es_dvp2axi_detect_reset_event(struct es_dvp2axi_stream *stream,
				    struct es_dvp2axi_timer *timer, int check_cnt,
				    bool *is_mod_timer)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	unsigned long flags;
	int ret, is_reset = 0;
	struct esmodule_vicap_reset_info rst_info;

	if (dev->is_toisp_reset) {
		is_reset = 1;
		timer->reset_src = ES_RESET_SRC_ERR_ISP;
	}
	if (is_reset) {
		es_dvp2axi_init_reset_work(timer);
		return is_reset;
	}

	if (timer->last_buf_wakeup_cnt[stream->id] < stream->buf_wake_up_cnt &&
	    check_cnt == 0) {
		v4l2_dbg(
			1, es_dvp2axi_debug, &dev->v4l2_dev,
			"info: frame end still update(%d, %d) in detecting cnt:%d, mode:%d\n",
			timer->last_buf_wakeup_cnt[stream->id],
			stream->frame_idx, timer->run_cnt, timer->monitor_mode);

		timer->last_buf_wakeup_cnt[stream->id] =
			stream->buf_wake_up_cnt;

		if (stream->frame_idx > 2)
			es_dvp2axi_is_reduced_frame_rate(dev);

		if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_HOTPLUG) {
			ret = v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
					       ESMODULE_GET_VICAP_RST_INFO,
					       &rst_info);
			if (ret)
				is_reset = 0;
			else
				is_reset = rst_info.is_reset;
			rst_info.is_reset = 0;
			if (is_reset)
				timer->reset_src = ES_RESET_SRC_ERR_HOTPLUG;
			v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
					 ESMODULE_SET_VICAP_RST_INFO,
					 &rst_info);
			if (!is_reset) {
				is_reset =
					es_dvp2axi_is_csi2_err_trigger_reset(timer);
				if (is_reset)
					timer->reset_src =
						ES_RESET_SRC_ERR_CSI2;
			}
		} else if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_CONTINUE) {
			is_reset = es_dvp2axi_is_csi2_err_trigger_reset(timer);
		} else if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_TRIGGER) {
			is_reset = timer->is_csi2_err_occurred;
			if (is_reset)
				timer->reset_src = ES_RESET_SRC_ERR_CSI2;
			timer->is_csi2_err_occurred = false;
		}

		if (is_reset) {
			es_dvp2axi_init_reset_work(timer);
			return is_reset;
		}

		if (timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_CONTINUE ||
		    timer->monitor_mode == ES_DVP2AXI_MONITOR_MODE_HOTPLUG) {
			if (timer->run_cnt == timer->max_run_cnt)
				timer->run_cnt = 0x0;
			*is_mod_timer = true;
		} else {
			if (timer->run_cnt <= timer->max_run_cnt) {
				*is_mod_timer = true;
			} else {
				spin_lock_irqsave(&timer->timer_lock, flags);
				timer->is_triggered = false;
				timer->is_running = false;
				spin_unlock_irqrestore(&timer->timer_lock,
						       flags);
				v4l2_info(&dev->v4l2_dev,
					  "stop reset detecting!\n");
			}
		}
	} else if (timer->last_buf_wakeup_cnt[stream->id] ==
		   stream->buf_wake_up_cnt) {
		bool is_reduced = false;

		if (stream->frame_idx > 2)
			is_reduced = es_dvp2axi_is_reduced_frame_rate(dev);
		else if (timer->run_cnt < 20)
			is_reduced = true;

		if (is_reduced) {
			*is_mod_timer = true;
			v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
				 "%s fps is reduced\n", __func__);
		} else {
			v4l2_info(
				&dev->v4l2_dev,
				"do reset work due to frame end is stopped, run_cnt:%d\n",
				timer->run_cnt);

			timer->reset_src = ES_RESET_SRC_ERR_CUTOFF;
			es_dvp2axi_init_reset_work(timer);
			is_reset = true;
		}
	}

	return is_reset;
}

void es_dvp2axi_reset_watchdog_timer_handler(struct timer_list *t)
{
	struct es_dvp2axi_timer *timer = container_of(t, struct es_dvp2axi_timer, timer);
	struct es_dvp2axi_device *dev =
		container_of(timer, struct es_dvp2axi_device, reset_watchdog_timer);
	struct es_dvp2axi_stream *stream = NULL;
	unsigned long flags;
	unsigned int i;
	int is_reset = 0;
	int check_cnt = 0;
	bool is_mod_timer = false;

	for (i = 0; i < dev->num_channels; i++) {
		stream = &dev->stream[i];
		if (stream->state == ES_DVP2AXI_STATE_STREAMING) {
			is_reset = es_dvp2axi_detect_reset_event(
				stream, timer, check_cnt, &is_mod_timer);
			check_cnt++;
			if (is_reset)
				break;
		}
	}
	if (!is_reset && is_mod_timer)
		mod_timer(&timer->timer, jiffies + timer->cycle);

	timer->run_cnt += 1;

	if (!check_cnt) {
		spin_lock_irqsave(&timer->timer_lock, flags);
		timer->is_triggered = false;
		timer->is_running = false;
		spin_unlock_irqrestore(&timer->timer_lock, flags);

		v4l2_info(&dev->v4l2_dev,
			  "all stream is stopped, stop reset detect!\n");
	}
}

int es_dvp2axi_reset_notifier(struct notifier_block *nb, unsigned long action,
			 void *data)
{
	struct es_dvp2axi_hw *hw = container_of(nb, struct es_dvp2axi_hw, reset_notifier);
	struct es_dvp2axi_device *dev = NULL;
	struct es_dvp2axi_timer *timer = NULL;
	unsigned long flags, val;
	u32 *csi_idx = data;
	int i = 0;
	bool is_match_dev = false;

	for (i = 0; i < hw->dev_num; i++) {
		dev = hw->dvp2axi_dev[i];
		if (*csi_idx == dev->csi_host_idx) {
			is_match_dev = true;
			break;
		}
	}
	if (!is_match_dev)
		return -EINVAL;
	timer = &dev->reset_watchdog_timer;
	if (timer->is_running) {
		val = action & CSI2_ERR_COUNT_ALL_MASK;
		spin_lock_irqsave(&timer->csi2_err_lock, flags);
		if ((val % timer->csi2_err_ref_cnt) == 0) {
			timer->notifer_called_cnt++;
			if ((timer->notifer_called_cnt % 2) == 0)
				timer->csi2_err_cnt_even = val;
			else
				timer->csi2_err_cnt_odd = val;
		}

		timer->csi2_err_fs_fe_cnt = (action & CSI2_ERR_FSFE_MASK) >> 8;
		spin_unlock_irqrestore(&timer->csi2_err_lock, flags);
	}

	return 0;
}

static void es_dvp2axi_modify_line_int(struct es_dvp2axi_stream *stream, bool en)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	u32 line_intr_en = 0;

	if (dvp2axi_dev->chip_id >= CHIP_EIC770X_DVP2AXI)
		line_intr_en = CSI_LINE_INTEN_EIC770X(stream->id);
	else
		line_intr_en = CSI_LINE_INTEN(stream->id);
	if (en) {
		if (dvp2axi_dev->wait_line_bak != dvp2axi_dev->wait_line) {
			dvp2axi_dev->wait_line_bak = dvp2axi_dev->wait_line;
			es_dvp2axi_write_register(
				dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1,
				dvp2axi_dev->wait_line << 16 | dvp2axi_dev->wait_line);
			es_dvp2axi_write_register(
				dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID2_3,
				dvp2axi_dev->wait_line << 16 | dvp2axi_dev->wait_line);
		}
		es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					line_intr_en);
		stream->is_line_inten = true;
	} else {
		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					 ~line_intr_en);
	}
}

static void es_dvp2axi_detect_wake_up_mode_change(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	bool is_change = false;
	int ch = 0;
	int i = 0;

	if (!priv || priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
		return;

	if ((dvp2axi_dev->hdr.hdr_mode == NO_HDR ||
	     dvp2axi_dev->hdr.hdr_mode == HDR_COMPR) &&
	    stream->id == ES_DVP2AXI_STREAM_MIPI_ID0) {
		if (dvp2axi_dev->wait_line != dvp2axi_dev->wait_line_cache)
			dvp2axi_dev->wait_line = dvp2axi_dev->wait_line_cache;
	} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X2 &&
		   stream->id == ES_DVP2AXI_STREAM_MIPI_ID1) {
		if (dvp2axi_dev->wait_line != dvp2axi_dev->wait_line_cache)
			dvp2axi_dev->wait_line = dvp2axi_dev->wait_line_cache;
	} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X3 &&
		   stream->id == ES_DVP2AXI_STREAM_MIPI_ID2) {
		if (dvp2axi_dev->wait_line != dvp2axi_dev->wait_line_cache)
			dvp2axi_dev->wait_line = dvp2axi_dev->wait_line_cache;
	}

	if (dvp2axi_dev->wait_line && (!stream->is_line_wake_up)) {
		is_change = true;
		stream->is_line_wake_up = true;
		if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY)
			stream->line_int_cnt = 1;
		else if (stream->frame_phase == DVP2AXI_CSI_FRAME1_READY)
			stream->line_int_cnt = 0;
		if (dvp2axi_dev->hdr.hdr_mode == HDR_X2) {
			dvp2axi_dev->stream[0].is_line_wake_up = true;
			dvp2axi_dev->stream[0].line_int_cnt = stream->line_int_cnt;
		} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X3) {
			dvp2axi_dev->stream[0].is_line_wake_up = true;
			dvp2axi_dev->stream[1].is_line_wake_up = true;
			dvp2axi_dev->stream[0].line_int_cnt = stream->line_int_cnt;
			dvp2axi_dev->stream[1].line_int_cnt = stream->line_int_cnt;
		}
	} else if ((dvp2axi_dev->wait_line == 0) && stream->is_line_wake_up) {
		stream->is_line_wake_up = false;
	}
	if (stream->is_line_wake_up) {
		if (is_change) {
			if (dvp2axi_dev->hdr.hdr_mode == HDR_X2)
				ch = 2;
			else if (dvp2axi_dev->hdr.hdr_mode == HDR_X3)
				ch = 3;
			else
				ch = 1;
			for (i = 0; i < ch; i++)
				es_dvp2axi_modify_line_int(&dvp2axi_dev->stream[i],
						      true);
		} else {
			es_dvp2axi_modify_line_int(stream, true);
		}
	}
}

u32 es_dvp2axi_mbus_pixelcode_to_v4l2(u32 pixelcode)
{
	u32 pixelformat;
	switch (pixelcode) {
	case MEDIA_BUS_FMT_Y8_1X8:
		pixelformat = V4L2_PIX_FMT_GREY;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		pixelformat = V4L2_PIX_FMT_SBGGR8;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		pixelformat = V4L2_PIX_FMT_SGBRG8;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		pixelformat = V4L2_PIX_FMT_SGRBG8;
		break;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		pixelformat = V4L2_PIX_FMT_SRGGB8;
		break;
	case MEDIA_BUS_FMT_Y10_1X10:
		pixelformat = V4L2_PIX_FMT_Y10;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		pixelformat = V4L2_PIX_FMT_SBGGR10;
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		pixelformat = V4L2_PIX_FMT_SGBRG10;
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		pixelformat = V4L2_PIX_FMT_SGRBG10;
		break;
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		pixelformat = V4L2_PIX_FMT_SRGGB10;
		break;
	case MEDIA_BUS_FMT_Y12_1X12:
		pixelformat = V4L2_PIX_FMT_Y12;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		pixelformat = V4L2_PIX_FMT_SBGGR12;
		break;
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		pixelformat = V4L2_PIX_FMT_SGBRG12;
		break;
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		pixelformat = V4L2_PIX_FMT_SGRBG12;
		break;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		pixelformat = V4L2_PIX_FMT_SRGGB12;
		break;
	case MEDIA_BUS_FMT_SPD_2X8:
		pixelformat = V4l2_PIX_FMT_SPD16;
		break;
	case MEDIA_BUS_FMT_EBD_1X8:
		pixelformat = V4l2_PIX_FMT_EBD8;
		break;
	case MEDIA_BUS_FMT_YVYU8_2X8:
		pixelformat = V4L2_PIX_FMT_YVYU;
		break;
	case MEDIA_BUS_FMT_YVYU10_2X10:
		pixelformat = V4L2_PIX_FMT_Y210;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		pixelformat = V4L2_PIX_FMT_RGB24;
		break;
	default:
		pixelformat = V4L2_PIX_FMT_SRGGB10;
	}

	return pixelformat;
}

void es_dvp2axi_set_default_fmt(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct v4l2_subdev_selection input_sel;
	struct v4l2_pix_format_mplane pixm;
	struct v4l2_subdev_format fmt;
	int stream_num = 0;
	int ret, i;

	stream_num = ES_DVP2AXI_MAX_STREAM_MIPI;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		for (i = 0; i < stream_num; i++) {
			if (i == ES_DVP2AXI_STREAM_MIPI_ID3)
				dvp2axi_dev->stream[i].is_compact = false;
			memset(&fmt, 0, sizeof(fmt));
			fmt.pad = i;
			fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
					 get_fmt, NULL, &fmt);
			memset(&pixm, 0, sizeof(pixm));
			pixm.pixelformat =
				es_dvp2axi_mbus_pixelcode_to_v4l2(fmt.format.code);
			pixm.width = fmt.format.width;
			pixm.height = fmt.format.height;
			memset(&input_sel, 0, sizeof(input_sel));
			input_sel.pad = i;
			input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
			input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
					       get_selection, NULL, &input_sel);
			if (!ret) {
				pixm.width = input_sel.r.width;
				pixm.height = input_sel.r.height;
			}
			pr_debug("pixm.pixelformat:%d, pixm.width:%d, pixm.height:%d\n",pixm.pixelformat, pixm.width, pixm.height);
			pr_debug("dvp2axi_dev->terminal_sensor.sd->name :%s\n",dvp2axi_dev->terminal_sensor.sd->name);
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[i], &pixm, false);
		}
	}
}

void es_dvp2axi_enable_dma_capture(struct es_dvp2axi_stream *stream, bool is_only_enable)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dvp2axi_dev->active_sensor->mbus;
	struct csi_channel_info *channel = &dvp2axi_dev->channels[stream->id];
	u32 val = 0;

	if (stream->buf_owner == ES_DVP2AXI_DMAEN_BY_ISP)
		stream->buf_owner = ES_DVP2AXI_DMAEN_BY_ISP_TO_VICAP;

	atomic_dec(&dvp2axi_dev->streamoff_cnt);
	if (stream->dma_en) {
		stream->dma_en |= stream->to_en_dma;
		stream->to_en_dma = 0;
		return;
	}

	stream->dma_en |= stream->to_en_dma;
	if (!is_only_enable) {
		if (stream->to_en_dma == ES_DVP2AXI_DMAEN_BY_VICAP) {
			es_dvp2axi_assign_new_buffer_pingpong(
				stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, stream->id);
			es_dvp2axi_write_register(
				dvp2axi_dev,
				get_reg_index_of_frm0_y_vlw(stream->id),
				channel->virtual_width);
		} else if (stream->to_en_dma == ES_DVP2AXI_DMAEN_BY_ISP) {
			es_dvp2axi_assign_new_buffer_pingpong_toisp(
				stream, ES_DVP2AXI_YUV_ADDR_STATE_INIT, stream->id);
		}
	}
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY)
		es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_CTRL,
					0x00010000);
	else
		es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL, 0x00010000);
	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY) {
		val = es_dvp2axi_read_register(
			dvp2axi_dev, get_reg_index_of_id_ctrl0(stream->id));
		if (!stream->is_compact)
			val |= CSI_WRDDR_TYPE_RAW_UNCOMPACT;
		else
			val &= ~CSI_WRDDR_TYPE_RAW_UNCOMPACT;
		val |= CSI_DMA_ENABLE;
		es_dvp2axi_write_register(
			dvp2axi_dev, get_reg_index_of_id_ctrl0(stream->id), val);
	} else {
		val = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_FOR);
		if (!stream->is_compact)
			val |= CSI_WRDDR_TYPE_RAW_UNCOMPACT << 11;
		else
			val &= ~(CSI_WRDDR_TYPE_RAW_UNCOMPACT << 11);
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_FOR, val);
		val = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL);
		if (dvp2axi_dev->chip_id == CHIP_EIC770X_DVP2AXI)
			val |= DVP_DMA_EN;
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL, val);
	}

	stream->to_en_dma = 0;
}

static int es_dvp2axi_stop_dma_capture(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct v4l2_mbus_config *mbus_cfg = &dvp2axi_dev->active_sensor->mbus;
	u32 val = 0;

	if (stream->buf_replace_cnt)
		return -EINVAL;

	stream->dma_en &= ~stream->to_stop_dma;
	atomic_inc(&dvp2axi_dev->streamoff_cnt);
	if (stream->dma_en != 0) {
		if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP)
			stream->buf_owner = ES_DVP2AXI_DMAEN_BY_ISP;
		stream->to_stop_dma = 0;
		return 0;
	}

	if (mbus_cfg->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus_cfg->type == V4L2_MBUS_CSI2_CPHY) {
		val = es_dvp2axi_read_register(
			dvp2axi_dev, get_reg_index_of_id_ctrl0(stream->id));
		val &= ~CSI_DMA_ENABLE;
		if (stream->is_stop_capture) {
			val &= ~CSI_ENABLE_CAPTURE;
			stream->is_stop_capture = false;
		}
		es_dvp2axi_write_register(
			dvp2axi_dev, get_reg_index_of_id_ctrl0(stream->id), val);
	} else {
		val = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL);
		if (dvp2axi_dev->chip_id == CHIP_EIC770X_DVP2AXI)
			val &= ~DVP_DMA_EN;
		if (stream->is_stop_capture) {
			val &= ~ENABLE_CAPTURE;
			stream->is_stop_capture = false;
		}
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL, val);
	}
	stream->to_stop_dma = 0;
	v4l2_dbg(4, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
		 "stream[%d] replace_cnt %d, y_addr 0x%x, 0x%x\n", stream->id,
		 stream->buf_replace_cnt,
		 es_dvp2axi_read_register(dvp2axi_dev,
				     get_reg_index_of_frm0_y_addr(stream->id)),
		 es_dvp2axi_read_register(dvp2axi_dev,
				     get_reg_index_of_frm1_y_addr(stream->id)));
	return 0;
}

static void es_dvp2axi_send_sof(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct v4l2_mbus_config *mbus = &dvp2axi_dev->active_sensor->mbus;
	// struct csi2_dev *csi;

	if (mbus->type == V4L2_MBUS_CCP2) {
		es_dvp2axi_lvds_event_inc_sof(dvp2axi_dev);
	} else {
		es_dvp2axi_dvp_event_inc_sof(dvp2axi_dev);
	}
}

static int es_dvp2axi_g_toisp_ch(unsigned int intstat_glb, int index)
{
	if (intstat_glb & TOISP_END_CH0(index))
		return ES_DVP2AXI_TOISP_CH0;
	if (intstat_glb & TOISP_END_CH1(index))
		return ES_DVP2AXI_TOISP_CH1;
	if (intstat_glb & TOISP_END_CH2(index))
		return ES_DVP2AXI_TOISP_CH2;

	return -EINVAL;
}

static int es_dvp2axi_g_toisp_fs(unsigned int intstat_glb, int index)
{
	if (intstat_glb & TOISP_FS_CH0(index))
		return ES_DVP2AXI_TOISP_CH0;
	if (intstat_glb & TOISP_FS_CH1(index))
		return ES_DVP2AXI_TOISP_CH1;
	if (intstat_glb & TOISP_FS_CH2(index))
		return ES_DVP2AXI_TOISP_CH2;

	return -EINVAL;
}

static void es_dvp2axi_toisp_check_stop_status(struct sditf_priv *priv,
					  unsigned int intstat_glb, int index)
{
	int ch = 0;
	struct es_dvp2axi_stream *stream;
	int src_id = 0;
	int i = 0;
	u32 val = 0;
	u64 cur_time = 0;
	int on = 0;

	for (i = 0; i < TOISP_CH_MAX; i++) {
		ch = es_dvp2axi_g_toisp_ch(intstat_glb, index);
		if (ch >= 0) {
			src_id = priv->toisp_inf.ch_info[ch].id;
			if (src_id == 24)
				stream = &priv->dvp2axi_dev->stream[0];
			else
				stream = &priv->dvp2axi_dev->stream[src_id % 4];
			if (stream->stopping) {
				v4l2_dbg(3, es_dvp2axi_debug,
					 &priv->dvp2axi_dev->v4l2_dev,
					 "stream[%d] stop\n", stream->id);
				es_dvp2axi_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
			}
			if (stream->dvp2axidev->sensor_state_change) {
				es_dvp2axi_dphy_quick_stream(stream->dvp2axidev, on);
				stream->dvp2axidev->sensor_work.on =
					stream->dvp2axidev->sensor_state;
				schedule_work(
					&stream->dvp2axidev->sensor_work.work);
				stream->dvp2axidev->sensor_state_change = false;
				if (stream->is_wait_stop_complete) {
					stream->is_wait_stop_complete = false;
					complete(&stream->stop_complete);
				}
			}
			if (stream->is_single_cap &&
			    (!stream->cur_skip_frame) &&
			    (stream->dvp2axidev->hdr.hdr_mode == NO_HDR ||
			     (stream->dvp2axidev->hdr.hdr_mode == HDR_X2 &&
			      stream->id == 1) ||
			     (stream->dvp2axidev->hdr.hdr_mode == HDR_X3 &&
			      stream->id == 2))) {
				es_dvp2axi_dphy_quick_stream(stream->dvp2axidev, on);
				stream->dvp2axidev->sensor_work.on = 0;
				schedule_work(
					&stream->dvp2axidev->sensor_work.work);
				stream->is_single_cap = false;
			}
			if (stream->cur_skip_frame &&
			    (stream->dvp2axidev->hdr.hdr_mode == NO_HDR ||
			     (stream->dvp2axidev->hdr.hdr_mode == HDR_X2 &&
			      stream->id == 1) ||
			     (stream->dvp2axidev->hdr.hdr_mode == HDR_X3 &&
			      stream->id == 2)))
				stream->cur_skip_frame--;
			if (stream->dvp2axidev->rdbk_debug &&
			    stream->frame_idx < 15)
				v4l2_info(&priv->dvp2axi_dev->v4l2_dev,
					  "stream[%d] toisp fe %d\n",
					  stream->id, stream->frame_idx - 1);

			switch (ch) {
			case ES_DVP2AXI_TOISP_CH0:
				val = TOISP_END_CH0(index);
				intstat_glb = intstat_glb & (~val);
				break;
			case ES_DVP2AXI_TOISP_CH1:
				val = TOISP_END_CH1(index);
				intstat_glb = intstat_glb & (~val);
				break;
			case ES_DVP2AXI_TOISP_CH2:
				val = TOISP_END_CH2(index);
				intstat_glb = intstat_glb & (~val);
				break;
			default:
				break;
			}
		}
		ch = es_dvp2axi_g_toisp_fs(intstat_glb, index);
		if (ch >= 0) {
			src_id = priv->toisp_inf.ch_info[ch].id;
			if (src_id == 24)
				stream = &priv->dvp2axi_dev->stream[0];
			else
				stream = &priv->dvp2axi_dev->stream[src_id % 4];
			if (stream->id == 0)
				es_dvp2axi_send_sof(stream->dvp2axidev);
			stream->frame_idx++;
			cur_time = es_dvp2axi_time_get_ns(stream->dvp2axidev);
			stream->readout.readout_time =
				cur_time - stream->readout.fs_timestamp;
			stream->readout.fs_timestamp = cur_time;
			stream->buf_wake_up_cnt++;
			if (stream->frame_idx % 2)
				stream->fps_stats.frm0_timestamp =
					es_dvp2axi_time_get_ns(stream->dvp2axidev);
			else
				stream->fps_stats.frm1_timestamp =
					es_dvp2axi_time_get_ns(stream->dvp2axidev);
			if (stream->dvp2axidev->rdbk_debug &&
			    stream->frame_idx < 15)
				v4l2_info(&priv->dvp2axi_dev->v4l2_dev,
					  "stream[%d] toisp sof seq %d\n",
					  stream->id, stream->frame_idx - 1);
			if (stream->to_en_dma)
				es_dvp2axi_enable_dma_capture(stream, false);
			if (stream->to_en_scale) {
				stream->to_en_scale = false;
				es_dvp2axi_scale_start(stream->scale_vdev);
			}
			switch (ch) {
			case ES_DVP2AXI_TOISP_CH0:
				val = TOISP_FS_CH0(index);
				intstat_glb = intstat_glb & (~val);
				break;
			case ES_DVP2AXI_TOISP_CH1:
				val = TOISP_FS_CH1(index);
				intstat_glb = intstat_glb & (~val);
				break;
			case ES_DVP2AXI_TOISP_CH2:
				val = TOISP_FS_CH2(index);
				intstat_glb = intstat_glb & (~val);
				break;
			default:
				break;
			}
		}
	}
}

void es_dvp2axi_irq_handle_toisp(struct es_dvp2axi_device *dvp2axi_dev,
			    unsigned int intstat_glb)
{
	int i = 0;
	bool to_check = false;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];

	if (!priv || priv->mode.rdbk_mode != ESISP_VICAP_ONLINE)
		return;

	for (i = 0; i < 2; i++) {
		if (priv->toisp_inf.link_mode == TOISP0 && i == 0) {
			to_check = true;
		} else if (priv->toisp_inf.link_mode == TOISP1 && i == 1) {
			to_check = true;
		} else if (priv->toisp_inf.link_mode == TOISP_UNITE && i == 1) {
			to_check = true;
		}
		if (to_check)
			es_dvp2axi_toisp_check_stop_status(priv, intstat_glb, i);
	}
}

static int es_dvp2axi_check_group_sync_state(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_stream *detect_stream = &dvp2axi_dev->stream[0];
	struct es_dvp2axi_stream *next_stream = NULL;
	struct es_dvp2axi_hw *hw = dvp2axi_dev->hw_dev;
	u64 fs_interval = 0;
	int i = 0;
	int ret = 0;
	struct es_dvp2axi_multi_sync_config *sync_config;

	sync_config = &hw->sync_config[dvp2axi_dev->sync_cfg.group];
	sync_config->sync_code |= BIT(dvp2axi_dev->csi_host_idx);
	v4l2_dbg(
		3, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
		"sync code 0x%x, mask 0x%x, update 0x%x, cache 0x%x, timestamp %llu\n",
		sync_config->sync_code, sync_config->sync_mask,
		sync_config->update_code, sync_config->update_cache,
		detect_stream->readout.fs_timestamp);

	if (sync_config->sync_code != sync_config->sync_mask)
		return -EINVAL;

	for (i = 0; i < sync_config->dev_cnt; i++) {
		if (sync_config->mode == ES_DVP2AXI_MASTER_MASTER) {
			if (i < sync_config->ext_master.count)
				next_stream =
					&sync_config->ext_master.dvp2axi_dev[i]
						 ->stream[0];
			else
				next_stream =
					&sync_config->int_master.dvp2axi_dev[0]
						 ->stream[0];
		} else if (sync_config->mode == ES_DVP2AXI_MASTER_SLAVE) {
			if (i < sync_config->slave.count)
				next_stream = &sync_config->slave.dvp2axi_dev[i]
						       ->stream[0];
			else
				next_stream =
					&sync_config->int_master.dvp2axi_dev[0]
						 ->stream[0];
		} else {
			v4l2_err(&dvp2axi_dev->v4l2_dev,
				 "ERROR: invalid group sync mode\n");
			ret = -EINVAL;
			break;
		}
		if (detect_stream == next_stream)
			continue;
		fs_interval = abs(detect_stream->readout.fs_timestamp -
				  next_stream->readout.fs_timestamp);
		if (fs_interval > ES_DVP2AXI_MAX_INTERVAL_NS) {
			ret = -EINVAL;
			break;
		}
	}
	return ret;
}

static void es_dvp2axi_deal_sof(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_stream *detect_stream = &dvp2axi_dev->stream[0];
	struct es_dvp2axi_hw *hw = dvp2axi_dev->hw_dev;
	struct es_dvp2axi_device *tmp_dev = NULL;
	unsigned long flags;
	int i = 0;
	int ret = 0;

	if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI)
		detect_stream->fs_cnt_in_single_frame++;
	spin_lock_irqsave(&detect_stream->fps_lock, flags);
	detect_stream->readout.fs_timestamp = es_dvp2axi_time_get_ns(dvp2axi_dev);
	spin_unlock_irqrestore(&detect_stream->fps_lock, flags);

	if (dvp2axi_dev->sync_cfg.type != ES_DVP2AXI_NOSYNC_MODE) {
		struct es_dvp2axi_multi_sync_config *sync_config;

		sync_config = &hw->sync_config[dvp2axi_dev->sync_cfg.group];
		ret = es_dvp2axi_check_group_sync_state(dvp2axi_dev);
		if (!ret) {
			sync_config->sync_code = 0;
			sync_config->frame_idx++;
			spin_lock_irqsave(&hw->group_lock, flags);
			sync_config->update_cache = sync_config->sync_mask;
			if (!sync_config->update_code) {
				sync_config->update_code =
					sync_config->update_cache;
				sync_config->update_cache = 0;
			}
			spin_unlock_irqrestore(&hw->group_lock, flags);
			for (i = 0; i < sync_config->dev_cnt; i++) {
				if (sync_config->mode == ES_DVP2AXI_MASTER_MASTER) {
					if (i < sync_config->ext_master.count)
						tmp_dev =
							sync_config->ext_master
								.dvp2axi_dev[i];
					else
						tmp_dev =
							sync_config->int_master
								.dvp2axi_dev[0];
				} else if (sync_config->mode ==
					   ES_DVP2AXI_MASTER_SLAVE) {
					if (i < sync_config->slave.count)
						tmp_dev = sync_config->slave
								  .dvp2axi_dev[i];
					else
						tmp_dev =
							sync_config->int_master
								.dvp2axi_dev[0];
				} else {
					v4l2_err(
						&dvp2axi_dev->v4l2_dev,
						"ERROR: invalid group sync mode\n");
				}
				if (tmp_dev) {
					es_dvp2axi_send_sof(tmp_dev);
					tmp_dev->stream[0].frame_idx =
						sync_config->frame_idx;
				}
			}
		}
	} else {
		if (!dvp2axi_dev->sditf[0] || dvp2axi_dev->sditf[0]->mode.rdbk_mode) {
			es_dvp2axi_send_sof(dvp2axi_dev);
			detect_stream->frame_idx++;
		}
		if (detect_stream->dvp2axidev->rdbk_debug &&
		    detect_stream->frame_idx < 15 &&
		    (!dvp2axi_dev->sditf[0] || dvp2axi_dev->sditf[0]->mode.rdbk_mode))
			v4l2_info(&dvp2axi_dev->v4l2_dev,
				  "stream[%d] sof %d %lld\n", detect_stream->id,
				  detect_stream->frame_idx - 1,
				  es_dvp2axi_time_get_ns(dvp2axi_dev));
	}
}

unsigned int es_dvp2axi_irq_global(struct es_dvp2axi_device *dvp2axi_dev)
{
	unsigned int intstat_glb = 0;

	intstat_glb = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_GLB_INTST);
	if (intstat_glb)
		v4l2_dbg(2, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "intstat_glb 0x%x\n", intstat_glb);
	else
		return intstat_glb;

	if (intstat_glb & SCALE_TOISP_AXI0_ERR) {
		v4l2_err(
			&dvp2axi_dev->v4l2_dev,
			"ERROR: scale channel, AXI0 bus err intstat_glb:0x%x !!\n",
			intstat_glb);
		return 0;
	}
	if (intstat_glb & SCALE_TOISP_AXI1_ERR) {
		v4l2_err(
			&dvp2axi_dev->v4l2_dev,
			"ERROR: scale channel, AXI1 bus err intstat_glb:0x%x !!\n",
			intstat_glb);
		return 0;
	}
	es_dvp2axi_irq_handle_scale(dvp2axi_dev, intstat_glb);
	return intstat_glb;
}

static bool es_dvp2axi_check_buffer_prepare(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	unsigned long flags;
	bool is_update = false;
	struct es_dvp2axi_multi_sync_config *sync_config;

	spin_lock_irqsave(&dvp2axi_dev->hw_dev->group_lock, flags);
	sync_config = &dvp2axi_dev->hw_dev->sync_config[dvp2axi_dev->sync_cfg.group];
	if (stream->id == 0 &&
	    sync_config->update_code & BIT(dvp2axi_dev->csi_host_idx)) {
		is_update = true;
		sync_config->update_code &= ~(BIT(dvp2axi_dev->csi_host_idx));
		if (!sync_config->update_code && sync_config->update_cache) {
			sync_config->update_code = sync_config->update_cache;
			sync_config->update_cache = 0;
		}
	} else {
		if (dvp2axi_dev->rdbk_buf[RDBK_L])
			is_update = true;
	}

	spin_unlock_irqrestore(&dvp2axi_dev->hw_dev->group_lock, flags);
	return is_update;
}

bool es_dvp2axi_check_single_dev_stream_on(struct es_dvp2axi_hw *hw)
{
	struct es_dvp2axi_device *dvp2axi_dev = NULL;
	struct es_dvp2axi_stream *stream = NULL;
	int i = 0, j = 0;
	int stream_cnt = 0;

	if (hw->dev_num == 1)
		return true;
	for (i = 0; i < hw->dev_num; i++) {
		dvp2axi_dev = hw->dvp2axi_dev[i];
		for (j = 0; j < ES_DVP2AXI_MAX_STREAM_MIPI; j++) {
			stream = &dvp2axi_dev->stream[j];
			if (stream->state == ES_DVP2AXI_STATE_STREAMING ||
			    stream->state == ES_DVP2AXI_STATE_RESET_IN_STREAMING) {
				stream_cnt++;
				break;
			}
		}
	}
	if (stream_cnt > 1)
		return false;
	return true;
}

static void es_dvp2axi_get_resmem_head(struct es_dvp2axi_device *dvp2axi_dev)
{
	// void *resmem_va = phys_to_virt(dvp2axi_dev->resmem_pa);
	// struct esisp_thunderboot_resmem_head *head = NULL;
	int size = 0;
	int offset = 0;
	int ret = 0;
	int cam_idx = 0;
	char cam_idx_str[3] = { 0 };

	if (!dvp2axi_dev->is_rtt_suspend)
		return;
	strscpy(cam_idx_str, dvp2axi_dev->terminal_sensor.sd->name + 1, 2);
	cam_idx_str[2] = '\0';
	ret = kstrtoint(cam_idx_str, 0, &cam_idx);
	if (ret) {
		v4l2_err(&dvp2axi_dev->v4l2_dev, "get camera index fail\n");
		return;
	}

	/* currently, thunderboot with mcu only run one camera */
	offset = 0;

	if (size && size < dvp2axi_dev->resmem_size) {
		dma_sync_single_for_cpu(dvp2axi_dev->dev,
					dvp2axi_dev->resmem_addr + offset, size,
					DMA_FROM_DEVICE);
	}
	v4l2_err(&dvp2axi_dev->v4l2_dev,
		 "get camera index %02x, resume_mode 0x%x, nr_buf_size %d\n",
		 cam_idx, dvp2axi_dev->resume_mode, dvp2axi_dev->nr_buf_size);
}

static int es_dvp2axi_subdevs_set_power(struct es_dvp2axi_device *dvp2axi_dev, int on)
{
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	int ret = 0;
	int i = 0;

	if (dvp2axi_dev->terminal_sensor.sd)
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, core,
				       s_power, on);
	if (priv && priv->is_combine_mode &&
	    dvp2axi_dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF) {
		for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
			if (dvp2axi_dev->sditf[i] && dvp2axi_dev->sditf[i]->sensor_sd)
				v4l2_subdev_call(dvp2axi_dev->sditf[i]->sensor_sd,
						 core, s_power, on);
		}
	}
	return ret;
}

#if 0
static void es_dvp2axi_sensor_quick_streaming_cb(void *data)
{
	struct v4l2_subdev *subdevs = (struct v4l2_subdev *)data;
	int on = 1;

	v4l2_subdev_call(subdevs, core, ioctl, ESMODULE_SET_QUICK_STREAM, &on);
}
#endif

static int es_dvp2axi_subdevs_set_stream(struct es_dvp2axi_device *dvp2axi_dev, int on)
{
	struct es_dvp2axi_pipeline *p = &dvp2axi_dev->pipe;
	struct es_dvp2axi_sensor_info *terminal_sensor = &dvp2axi_dev->terminal_sensor;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	int i = 0;
	int ret = 0;

	for (i = 0; i < p->num_subdevs; i++) {
		if (p->subdevs[i] == terminal_sensor->sd && on)
			es_dvp2axi_set_sof(dvp2axi_dev, dvp2axi_dev->stream[0].frame_idx);
		ret = v4l2_subdev_call(p->subdevs[i], video, s_stream,
				on);
		if (ret)
			v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				"%s:stream %s subdev:%s failed\n",
				__func__, on ? "on" : "off",
				p->subdevs[i]->name);
	}

	if (priv && priv->is_combine_mode &&
	    dvp2axi_dev->sditf_cnt <= ES_DVP2AXI_MAX_SDITF) {
		for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
			if (dvp2axi_dev->sditf[i] && dvp2axi_dev->sditf[i]->sensor_sd) {
				ret = v4l2_subdev_call(
					dvp2axi_dev->sditf[i]->sensor_sd, video,
					s_stream, on);
				if (ret)
					v4l2_dbg(
						1, es_dvp2axi_debug,
						&dvp2axi_dev->v4l2_dev,
						"%s:stream %s subdev:%s failed\n",
						__func__, on ? "on" : "off",
						dvp2axi_dev->sditf[i]
							->sensor_sd->name);
			}
		}
	}
	return ret;
}

int es_dvp2axi_stream_suspend(struct es_dvp2axi_device *dvp2axi_dev, int mode)
{
	struct es_dvp2axi_stream *stream = NULL;
	struct es_dvp2axi_resume_info *resume_info =
		&dvp2axi_dev->reset_work.resume_info;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	int ret = 0;
	int i = 0;
	int sof_cnt = 0;
	int on = 0;
	int suspend_cnt = 0;

	mutex_lock(&dvp2axi_dev->stream_lock);

	if (priv && priv->mode.rdbk_mode == ESISP_VICAP_ONLINE &&
	    mode == ES_DVP2AXI_RESUME_DVP2AXI)
		goto out_suspend;

	for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
		stream = &dvp2axi_dev->stream[i];

		if (stream->state == ES_DVP2AXI_STATE_STREAMING) {
			suspend_cnt++;
			v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				 "stream[%d] stopping\n", stream->id);
			if (stream->dma_en) {
				stream->stopping = true;
				ret = wait_event_timeout(
					stream->wq_stopped,
					stream->state != ES_DVP2AXI_STATE_STREAMING,
					msecs_to_jiffies(500));
				if (!ret) {
					es_dvp2axi_stream_stop(stream);
					stream->stopping = false;
				}
			} else {
				es_dvp2axi_stream_stop(stream);
			}

			if (stream->id == ES_DVP2AXI_STREAM_MIPI_ID0) {
				sof_cnt = es_dvp2axi_get_sof(dvp2axi_dev);
				v4l2_dbg(
					1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					"%s: stream[%d] sync frmid & csi_sof, frm_id:%d, csi_sof:%d\n",
					__func__, stream->id, stream->frame_idx,
					sof_cnt);

				resume_info->frm_sync_seq = stream->frame_idx;
			}

			stream->state = ES_DVP2AXI_STATE_RESET_IN_STREAMING;
			stream->is_fs_fe_not_paired = false;
			stream->fs_cnt_in_single_frame = 0;

			v4l2_dbg(
				1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				"%s stop stream[%d] in streaming, frm_id:%d, csi_sof:%d\n",
				__func__, stream->id, stream->frame_idx,
				es_dvp2axi_get_sof(dvp2axi_dev));
		}
	}

	if (suspend_cnt == 0)
		goto out_suspend;

	if (!dvp2axi_dev->resume_mode)
		es_dvp2axi_subdevs_set_power(dvp2axi_dev, on);

	es_dvp2axi_subdevs_set_stream(dvp2axi_dev, on);

out_suspend:
	mutex_unlock(&dvp2axi_dev->stream_lock);
	return 0;
}

int es_dvp2axi_stream_resume(struct es_dvp2axi_device *dvp2axi_dev, int mode)
{
	struct es_dvp2axi_stream *stream = NULL;
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	struct v4l2_subdev *sd = NULL;
	int ret = 0;
	int i = 0;
	u32 capture_mode = 0;
	int on = 1;
	int resume_cnt = 0;
	unsigned long flags;
	bool is_single_dev = false;
	bool is_can_be_online = false;
	struct esisp_vicap_mode vicap_mode;

	mutex_lock(&dvp2axi_dev->stream_lock);

	es_dvp2axi_get_resmem_head(dvp2axi_dev);
	is_single_dev = es_dvp2axi_check_single_dev_stream_on(dvp2axi_dev->hw_dev);
	is_can_be_online = es_dvp2axi_check_can_be_online(dvp2axi_dev);
	if (dvp2axi_dev->resume_mode == ESISP_RTT_MODE_ONE_FRAME) {
		if (dvp2axi_dev->is_rtt_suspend) {
			capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
			if (priv)
				priv->mode.rdbk_mode = ESISP_VICAP_RDBK_AUTO;
		} else {
			if (priv &&
			    priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
				capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP;
			} else if (priv &&
				   (priv->mode.rdbk_mode ==
					    ESISP_VICAP_RDBK_AUTO ||
				    priv->mode.rdbk_mode ==
					    ESISP_VICAP_RDBK_AUTO_ONE_FRAME)) {
				capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
			} else {
				capture_mode = ES_DVP2AXI_STREAM_MODE_CAPTURE;
			}
		}
	} else if (dvp2axi_dev->resume_mode == ESISP_RTT_MODE_MULTI_FRAME) {
		if (is_single_dev && is_can_be_online) {
			capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP;
			if (priv)
				priv->mode.rdbk_mode = ESISP_VICAP_ONLINE;
		} else {
			if (dvp2axi_dev->is_thunderboot) {
				capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
				if (priv)
					priv->mode.rdbk_mode =
						ESISP_VICAP_RDBK_AUTO;
			} else {
				capture_mode = ES_DVP2AXI_STREAM_MODE_CAPTURE;
				if (priv)
					priv->mode.rdbk_mode =
						ESISP_VICAP_RDBK_AIQ;
			}
		}
	} else {
		if (priv && priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
			capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP;
		else if (priv &&
			 (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO ||
			  priv->mode.rdbk_mode ==
				  ESISP_VICAP_RDBK_AUTO_ONE_FRAME))
			capture_mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
		else
			capture_mode = ES_DVP2AXI_STREAM_MODE_CAPTURE;
	}
	if (priv && priv->mode.rdbk_mode == ESISP_VICAP_ONLINE &&
	    mode == ES_DVP2AXI_RESUME_DVP2AXI)
		goto out_resume;

	for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
		stream = &dvp2axi_dev->stream[i];
		if (stream->state != ES_DVP2AXI_STATE_RESET_IN_STREAMING)
			continue;

		stream->fs_cnt_in_single_frame = 0;
		if (dvp2axi_dev->resume_mode == ESISP_RTT_MODE_ONE_FRAME)
			stream->is_single_cap = true;
		spin_lock_irqsave(&stream->vbq_lock, flags);
		if (!priv || priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AIQ) {
			if (stream->dvp2axi_fmt_in->field ==
			    V4L2_FIELD_INTERLACED) {
				if (stream->curr_buf == stream->next_buf) {
					if (stream->curr_buf)
						list_add_tail(
							&stream->curr_buf->queue,
							&stream->buf_head);
				} else {
					if (stream->curr_buf)
						list_add_tail(
							&stream->curr_buf->queue,
							&stream->buf_head);
					if (stream->next_buf)
						list_add_tail(
							&stream->next_buf->queue,
							&stream->buf_head);
				}
				stream->curr_buf = NULL;
				stream->next_buf = NULL;
			}
		} else {
			if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
				if (stream->curr_buf_toisp ==
				    stream->next_buf_toisp) {
					if (stream->curr_buf_toisp)
						list_add_tail(
							&stream->curr_buf_toisp
								 ->list,
							&stream->rx_buf_head);
				} else {
					if (stream->curr_buf_toisp)
						list_add_tail(
							&stream->curr_buf_toisp
								 ->list,
							&stream->rx_buf_head);
					if (stream->next_buf_toisp)
						list_add_tail(
							&stream->next_buf_toisp
								 ->list,
							&stream->rx_buf_head);
				}
				stream->curr_buf_toisp = NULL;
				stream->next_buf_toisp = NULL;
			} else {
				if (stream->curr_buf_toisp ==
				    stream->next_buf_toisp) {
					if (stream->curr_buf_toisp)
						list_add_tail(
							&stream->curr_buf_toisp
								 ->list,
							&stream->rx_buf_head);
				} else {
					if (stream->curr_buf_toisp)
						list_add_tail(
							&stream->curr_buf_toisp
								 ->list,
							&stream->rx_buf_head);
					if (stream->next_buf_toisp)
						list_add_tail(
							&stream->next_buf_toisp
								 ->list,
							&stream->rx_buf_head);
				}
				stream->curr_buf_toisp = NULL;
				stream->next_buf_toisp = NULL;
			}
		}

		spin_unlock_irqrestore(&stream->vbq_lock, flags);

		if (priv) {
			if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
				sditf_change_to_online(priv);
				if (dvp2axi_dev->resume_mode ==
					    ESISP_RTT_MODE_MULTI_FRAME &&
				    stream->rx_buf_num &&
				    (priv->hdr_cfg.hdr_mode == NO_HDR ||
				     (priv->hdr_cfg.hdr_mode == HDR_X2 &&
				      stream->id == 1) ||
				     (priv->hdr_cfg.hdr_mode == HDR_X3 &&
				      stream->id == 2)))
					es_dvp2axi_free_rx_buf(stream,
							  priv->buf_num);
				else if (!stream->rx_buf_num &&
					 ((priv->hdr_cfg.hdr_mode == HDR_X2 &&
					   stream->id == 0) ||
					  (priv->hdr_cfg.hdr_mode == HDR_X3 &&
					   (stream->id == 0 ||
					    stream->id == 1))))
					es_dvp2axi_init_rx_buf(stream, 1);
			} else {
				if (stream->is_single_cap && stream->id == 0) {
					vicap_mode = priv->mode;
					vicap_mode.rdbk_mode =
						ESISP_VICAP_RDBK_AUTO_ONE_FRAME;
					sd = get_esisp_sd(priv);
					if (sd) {
						ret = v4l2_subdev_call(
							sd, core, ioctl,
							ESISP_VICAP_CMD_MODE,
							&vicap_mode);
						if (ret)
							v4l2_err(
								&dvp2axi_dev->v4l2_dev,
								"set isp work mode rdbk aotu oneframe fail\n");
					}
				}
				sditf_disable_immediately(priv);
				if (!stream->rx_buf_num &&
				    capture_mode ==
					    ES_DVP2AXI_STREAM_MODE_TOISP_RDBK) {
					if (dvp2axi_dev->resume_mode ==
					    ESISP_RTT_MODE_ONE_FRAME)
						es_dvp2axi_init_rx_buf(stream, 1);
					else
						es_dvp2axi_init_rx_buf(
							stream, priv->buf_num);
				}
			}
		}

		stream->lack_buf_cnt = 0;
		if (dvp2axi_dev->active_sensor &&
		    (dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
		     dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
		     dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CCP2))
			ret = es_dvp2axi_csi_stream_start(stream, capture_mode);
		else
			ret = es_dvp2axi_stream_start(stream, capture_mode);
		if (ret)
			v4l2_err(&dvp2axi_dev->v4l2_dev,
				 "%s:resume stream[%d] failed\n", __func__,
				 stream->id);

		resume_cnt++;
		stream->cur_skip_frame = stream->skip_frame;
		v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "resume stream[%d], frm_idx:%d, csi_sof:%d\n",
			 stream->id, stream->frame_idx, es_dvp2axi_get_sof(dvp2axi_dev));
	}

	if (resume_cnt == 0)
		goto out_resume;

	if (!dvp2axi_dev->resume_mode)
		es_dvp2axi_subdevs_set_power(dvp2axi_dev, on);

	atomic_set(&dvp2axi_dev->streamoff_cnt, 0);
	es_dvp2axi_subdevs_set_stream(dvp2axi_dev, on);

	if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI)
		es_dvp2axi_start_luma(
			&dvp2axi_dev->luma_vdev,
			dvp2axi_dev->stream[ES_DVP2AXI_STREAM_MIPI_ID0].dvp2axi_fmt_in);
out_resume:
	mutex_unlock(&dvp2axi_dev->stream_lock);
	return 0;
}

void es_dvp2axi_err_print_work(struct work_struct *work)
{
	struct es_dvp2axi_err_state_work *err_state_work =
		container_of(work, struct es_dvp2axi_err_state_work, work);
	struct es_dvp2axi_device *dev = container_of(
		err_state_work, struct es_dvp2axi_device, err_state_work);
	u32 err_state = 0;
	int intstat = 0;
	int lastline = 0;
	int lastpixel = 0;
	u64 cur_time = 0;
	bool is_print = false;

	cur_time = es_dvp2axi_time_get_ns(dev);
	if (err_state_work->last_timestamp == 0) {
		is_print = true;
	} else {
		if (cur_time - err_state_work->last_timestamp > 500000000)
			is_print = true;
	}
	err_state_work->last_timestamp = cur_time;
	err_state = err_state_work->err_state;
	intstat = err_state_work->intstat;
	lastline = err_state_work->lastline;
	lastpixel = err_state_work->lastpixel;
	if (err_state & ES_DVP2AXI_ERR_ID0_NOT_BUF && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[0] not active buffer, frame num %d, cnt %llu\n",
			dev->stream[0].frame_idx,
			dev->irq_stats.not_active_buf_cnt[0]);
	if (err_state & ES_DVP2AXI_ERR_ID1_NOT_BUF && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[1] not active buffer, frame num %d, cnt %llu\n",
			dev->stream[1].frame_idx,
			dev->irq_stats.not_active_buf_cnt[1]);
	if (err_state & ES_DVP2AXI_ERR_ID2_NOT_BUF && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[2] not active buffer, frame num %d, cnt %llu\n",
			dev->stream[2].frame_idx,
			dev->irq_stats.not_active_buf_cnt[2]);
	if (err_state & ES_DVP2AXI_ERR_ID3_NOT_BUF && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[3] not active buffer, frame num %d, cnt %llu\n",
			dev->stream[3].frame_idx,
			dev->irq_stats.not_active_buf_cnt[3]);
	if (err_state & ES_DVP2AXI_ERR_ID0_TRIG_SIMULT && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[0], frm0/frm1 end simultaneously,frm id:%d, cnt %llu\n",
			dev->stream[0].frame_idx,
			dev->irq_stats.trig_simult_cnt[0]);
	if (err_state & ES_DVP2AXI_ERR_ID1_TRIG_SIMULT && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[1], frm0/frm1 end simultaneously,frm id:%d, cnt %llu\n",
			dev->stream[1].frame_idx,
			dev->irq_stats.trig_simult_cnt[1]);
	if (err_state & ES_DVP2AXI_ERR_ID2_TRIG_SIMULT && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[2], frm0/frm1 end simultaneously,frm id:%d, cnt %llu\n",
			dev->stream[2].frame_idx,
			dev->irq_stats.trig_simult_cnt[2]);
	if (err_state & ES_DVP2AXI_ERR_ID3_TRIG_SIMULT && is_print)
		v4l2_err(
			&dev->v4l2_dev,
			"stream[3], frm0/frm1 end simultaneously,frm id:%d, cnt %llu\n",
			dev->stream[3].frame_idx,
			dev->irq_stats.trig_simult_cnt[3]);
	if (err_state & ES_DVP2AXI_ERR_SIZE) {
		if (dev->chip_id >= CHIP_EIC770X_DVP2AXI)
			v4l2_err(
				&dev->v4l2_dev,
				"ERROR: csi size err, intstat:0x%x, size:0x%x,0x%x,0x%x,0x%x, cnt %llu\n",
				intstat, err_state_work->size_id0,
				err_state_work->size_id1,
				err_state_work->size_id2,
				err_state_work->size_id3,
				dev->irq_stats.csi_size_err_cnt);
		else
			v4l2_err(
				&dev->v4l2_dev,
				"ERROR: csi size err, intstat:0x%x, lastline:0x%x, cnt %llu\n",
				intstat, lastline,
				dev->irq_stats.csi_size_err_cnt);
	}
	if (err_state & ES_DVP2AXI_ERR_OVERFLOW)
		v4l2_err(
			&dev->v4l2_dev,
			"ERROR: csi fifo overflow, intstat:0x%x, lastline:0x%x, cnt %llu\n",
			intstat, lastline, dev->irq_stats.csi_overflow_cnt);
	if (err_state & ES_DVP2AXI_ERR_BANDWIDTH_LACK)
		v4l2_err(
			&dev->v4l2_dev,
			"ERROR: csi bandwidth lack, intstat:0x%x, lastline:0x%x, cnt %llu\n",
			intstat, lastline, dev->irq_stats.csi_bwidth_lack_cnt);
	if (err_state & ES_DVP2AXI_ERR_ID0_MULTI_FS)
		v4l2_err(&dev->v4l2_dev,
			 "ERR: multi fs in oneframe in id0, fs_num:%u\n",
			 dev->stream[0].fs_cnt_in_single_frame);
	if (err_state & ES_DVP2AXI_ERR_BUS)
		v4l2_err(&dev->v4l2_dev,
			 "dvp bus err, intstat 0x%x, last line 0x%x\n", intstat,
			 lastline);
	if (err_state & ES_DVP2AXI_ERR_PIXEL)
		v4l2_err(&dev->v4l2_dev,
			 "dvp pix err, intstat 0x%x, last pixel 0x%x\n",
			 intstat, lastpixel);
	if (err_state & ES_DVP2AXI_ERR_LINE)
		v4l2_err(&dev->v4l2_dev,
			 "dvp line err, intstat 0x%x, last line 0x%x\n",
			 intstat, lastline);
}

/* pingpong irq for eic770x and next */
void es_dvp2axi_irq_pingpong_v1(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_stream *stream;
	struct es_dvp2axi_stream *detect_stream = &dvp2axi_dev->stream[0];
	struct v4l2_mbus_config *mbus;
	unsigned int intstat, i = 0xff;
	unsigned long flags;
	bool is_update = false;
	int ret = 0;
	int on = 0;

	if (!dvp2axi_dev->active_sensor)
		return;

	mbus = &dvp2axi_dev->active_sensor->mbus;
	if (mbus->type == V4L2_MBUS_CSI2_DPHY ||
	    mbus->type == V4L2_MBUS_CSI2_CPHY || mbus->type == V4L2_MBUS_CCP2) {
		int mipi_id;
		u32 lastline = 0;

		intstat =
			es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTSTAT);
		lastline = es_dvp2axi_read_register(
			dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_LINE_LINE_CNT_ID0_1);
		dvp2axi_dev->err_state_work.lastline = lastline;
		dvp2axi_dev->err_state_work.intstat = intstat;

		/* clear all interrupts that has been triggered */
		if (intstat) {
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTSTAT,
					     intstat);
			v4l2_dbg(2, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
				 "intstat 0x%x\n", intstat);
		} else {
			return;
		}

		if (intstat & CSI_SIZE_ERR) {
			if (dvp2axi_dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
				dvp2axi_dev->err_state_work.size_id0 =
					es_dvp2axi_read_register(
						dvp2axi_dev,
						DVP2AXI_REG_MIPI_FRAME_SIZE_ID0);
				dvp2axi_dev->err_state_work.size_id1 =
					es_dvp2axi_read_register(
						dvp2axi_dev,
						DVP2AXI_REG_MIPI_FRAME_SIZE_ID1);
				dvp2axi_dev->err_state_work.size_id2 =
					es_dvp2axi_read_register(
						dvp2axi_dev,
						DVP2AXI_REG_MIPI_FRAME_SIZE_ID2);
				dvp2axi_dev->err_state_work.size_id3 =
					es_dvp2axi_read_register(
						dvp2axi_dev,
						DVP2AXI_REG_MIPI_FRAME_SIZE_ID3);
			}
			dvp2axi_dev->irq_stats.csi_size_err_cnt++;
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_SIZE;
			es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_CTRL,
						0x000A0000);
			return;
		}

		if (intstat & CSI_FIFO_OVERFLOW_V1) {
			dvp2axi_dev->irq_stats.csi_overflow_cnt++;
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_OVERFLOW;
			return;
		}

		if (intstat & CSI_BANDWIDTH_LACK_V1 &&
		    dvp2axi_dev->intr_mask & CSI_BANDWIDTH_LACK_V1) {
			dvp2axi_dev->irq_stats.csi_bwidth_lack_cnt++;
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_BANDWIDTH_LACK;
			if (dvp2axi_dev->irq_stats.csi_bwidth_lack_cnt > 10) {
				es_dvp2axi_write_register_and(
					dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
					~(CSI_BANDWIDTH_LACK_V1));
				dvp2axi_dev->intr_mask &= ~(CSI_BANDWIDTH_LACK_V1);
				schedule_delayed_work(&dvp2axi_dev->work_deal_err,
						      msecs_to_jiffies(1000));
			}
		}

		if (intstat & CSI_ALL_ERROR_INTEN_V1) {
			dvp2axi_dev->irq_stats.all_err_cnt++;
			return;
		}

		for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
			mipi_id = es_dvp2axi_csi_g_mipi_id(&dvp2axi_dev->v4l2_dev,
						      intstat);
			if (mipi_id < 0)
				continue;

			stream = &dvp2axi_dev->stream[mipi_id];
			if (!dvp2axi_dev->sditf[0] ||
			    dvp2axi_dev->sditf[0]->mode.rdbk_mode)
				stream->buf_wake_up_cnt++;

			if (stream->stopping && (!stream->dma_en)) {
				es_dvp2axi_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
				continue;
			}

			if (stream->state != ES_DVP2AXI_STATE_STREAMING)
				continue;
			stream->is_in_vblank = true;
			switch (mipi_id) {
			case ES_DVP2AXI_STREAM_MIPI_ID0:
				stream->frame_phase = SW_FRM_END_ID0(intstat);
				intstat &= ~CSI_FRAME_END_ID0;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID1:
				stream->frame_phase = SW_FRM_END_ID1(intstat);
				intstat &= ~CSI_FRAME_END_ID1;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID2:
				stream->frame_phase = SW_FRM_END_ID2(intstat);
				intstat &= ~CSI_FRAME_END_ID2;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID3:
				stream->frame_phase = SW_FRM_END_ID3(intstat);
				intstat &= ~CSI_FRAME_END_ID3;
				break;
			}
			if (stream->dvp2axidev->rdbk_debug &&
			    stream->frame_idx < 15 &&
			    (!dvp2axi_dev->sditf[0] ||
			     dvp2axi_dev->sditf[0]->mode.rdbk_mode))
				v4l2_info(&dvp2axi_dev->v4l2_dev,
					  "stream[%d] fe %d, phase %d, %lld\n",
					  stream->id, stream->frame_idx - 1,
					  stream->frame_phase,
					  es_dvp2axi_time_get_ns(dvp2axi_dev));
			if (stream->is_finish_stop_dma &&
			    stream->is_wait_dma_stop) {
				stream->is_wait_dma_stop = false;
				wake_up(&stream->wq_stopped);
				stream->is_finish_stop_dma = false;
				continue;
			}

			if (stream->is_finish_stop_dma &&
			    stream->is_wait_stop_complete) {
				stream->is_finish_stop_dma = false;
				stream->is_wait_stop_complete = false;
				complete(&stream->stop_complete);
			}

			if (stream->crop_dyn_en)
				es_dvp2axi_dynamic_crop(stream);

			if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_VICAP) {
				if (dvp2axi_dev->sync_cfg.type == ES_DVP2AXI_NOSYNC_MODE)
					is_update = true;
				else
					is_update = es_dvp2axi_check_buffer_prepare(
						stream);
				v4l2_dbg(
					4, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					"dma capture by vicap, is_updata %d, group mode %d, dma_en %d\n",
					is_update, dvp2axi_dev->sync_cfg.type,
					stream->dma_en);
				if (is_update)
					es_dvp2axi_update_stream(dvp2axi_dev, stream,
							    mipi_id);
			} else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP) {
				v4l2_dbg(4, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					 "dma capture by isp, dma_en 0x%x\n",
					 stream->dma_en);
				es_dvp2axi_update_stream_toisp(dvp2axi_dev, stream,
							  mipi_id);
			} 
			// else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ESKIT) {
			// 	v4l2_dbg(4, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			// 		 "dma capture by eskit, dma_en 0x%x\n",
			// 		 stream->dma_en);
			// 	es_dvp2axi_update_stream_eskit(dvp2axi_dev, stream,
			// 				   mipi_id);
			// }
			if (stream->is_single_cap && !stream->cur_skip_frame) {
				if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP)
					stream->to_stop_dma =
						ES_DVP2AXI_DMAEN_BY_ISP;
				else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_VICAP)
					stream->to_stop_dma =
						ES_DVP2AXI_DMAEN_BY_VICAP;
				else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ESKIT)
					stream->to_stop_dma =
						ES_DVP2AXI_DMAEN_BY_ESKIT;
				es_dvp2axi_stop_dma_capture(stream);
				stream->is_single_cap = false;
				if ((dvp2axi_dev->hdr.hdr_mode == NO_HDR &&
				     atomic_read(&dvp2axi_dev->streamoff_cnt) ==
					     1) ||
				    (dvp2axi_dev->hdr.hdr_mode == HDR_X2 &&
				     atomic_read(&dvp2axi_dev->streamoff_cnt) ==
					     2) ||
				    (dvp2axi_dev->hdr.hdr_mode == HDR_X3 &&
				     atomic_read(&dvp2axi_dev->streamoff_cnt) ==
					     3)) {
					es_dvp2axi_dphy_quick_stream(stream->dvp2axidev,
								on);
					dvp2axi_dev->sensor_work.on = 0;
					schedule_work(
						&dvp2axi_dev->sensor_work.work);
				}
			}

			if (stream->cur_skip_frame)
				stream->cur_skip_frame--;

			if (stream->is_change_toisp) {
				stream->is_change_toisp = false;
				if ((dvp2axi_dev->hdr.hdr_mode == HDR_X2 &&
				     stream->id != 1) ||
				    (dvp2axi_dev->hdr.hdr_mode == HDR_X3 &&
				     stream->id != 2))
					es_dvp2axi_release_unnecessary_buf_for_online(
						stream, stream->curr_buf_toisp);
				else
					sditf_change_to_online(
						dvp2axi_dev->sditf[0]);
			}

			spin_lock_irqsave(&stream->vbq_lock, flags);
			if (!(stream->cur_stream_mode &
			      ES_DVP2AXI_STREAM_MODE_TOISP) &&
			    stream->lack_buf_cnt == 2) {
				stream->to_stop_dma = ES_DVP2AXI_DMAEN_BY_VICAP;
				es_dvp2axi_stop_dma_capture(stream);
				v4l2_dbg(
					4, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					"stream[%d] to stop dma, lack_buf_cnt %d\n",
					stream->id, stream->lack_buf_cnt);
			}
			spin_unlock_irqrestore(&stream->vbq_lock, flags);
			if (stream->to_en_scale) {
				stream->to_en_scale = false;
				es_dvp2axi_scale_start(stream->scale_vdev);
			}
			es_dvp2axi_detect_wake_up_mode_change(stream);
			if (dvp2axi_dev->chip_id < CHIP_EIC770X_DVP2AXI &&
			    mipi_id == ES_DVP2AXI_STREAM_MIPI_ID0) {
				if ((intstat & (CSI_FRAME1_START_ID0 |
						CSI_FRAME0_START_ID0)) == 0 &&
				    detect_stream->fs_cnt_in_single_frame > 1) {
					dvp2axi_dev->err_state |=
						ES_DVP2AXI_ERR_ID0_MULTI_FS;
					detect_stream->is_fs_fe_not_paired =
						true;
					detect_stream->fs_cnt_in_single_frame =
						0;
				} else {
					detect_stream->fs_cnt_in_single_frame--;
				}
			}
			es_dvp2axi_monitor_reset_event(dvp2axi_dev);
			dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
		}

		for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
			if (intstat & CSI_START_INTSTAT(i)) {
				stream = &dvp2axi_dev->stream[i];
				if (i == 0) {
					if (!stream->cur_skip_frame)
						es_dvp2axi_deal_sof(dvp2axi_dev);
				} else {
					spin_lock_irqsave(&stream->fps_lock,
							  flags);
					stream->readout.fs_timestamp =
						es_dvp2axi_time_get_ns(dvp2axi_dev);
					stream->frame_idx++;
					spin_unlock_irqrestore(
						&stream->fps_lock, flags);
				}
				stream->is_in_vblank = false;
				spin_lock_irqsave(&stream->vbq_lock, flags);
				if (stream->stopping && stream->dma_en) {
					if (stream->dma_en &
					    ES_DVP2AXI_DMAEN_BY_VICAP)
						stream->to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_VICAP;
					else if (stream->dma_en &
						 ES_DVP2AXI_DMAEN_BY_ISP)
						stream->to_stop_dma =
							ES_DVP2AXI_DMAEN_BY_ISP;
					stream->is_stop_capture = true;
				}
				if (stream->to_stop_dma) {
					ret = es_dvp2axi_stop_dma_capture(stream);
					if (!ret)
						stream->is_finish_stop_dma =
							true;
				}
				if (stream->to_en_dma)
					es_dvp2axi_enable_dma_capture(stream, false);
				spin_unlock_irqrestore(&stream->vbq_lock,
						       flags);
			}
			if (intstat & CSI_LINE_INTSTAT_V1(i)) {
				stream = &dvp2axi_dev->stream[i];
				if (stream->is_line_inten) {
					stream->line_int_cnt++;
					if (dvp2axi_dev->rdbk_debug > 1 &&
					    stream->frame_idx < 15)
						v4l2_info(&dvp2axi_dev->v4l2_dev,
							  "line int %lld\n",
							  stream->line_int_cnt);
					if (dvp2axi_dev->sditf[0] &&
					    (dvp2axi_dev->sditf[0]->mode.rdbk_mode ==
						     ESISP_VICAP_RDBK_AUTO ||
					     dvp2axi_dev->sditf[0]->mode.rdbk_mode ==
						     ESISP_VICAP_RDBK_AUTO_ONE_FRAME))
						es_dvp2axi_line_wake_up_rdbk(
							stream, stream->id);
					else
						es_dvp2axi_line_wake_up(stream,
								   stream->id);
					es_dvp2axi_modify_line_int(stream, false);
					stream->is_line_inten = false;
				}
				v4l2_dbg(3, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
					 "%s: id0 cur line:%d\n", __func__,
					 lastline & 0x3fff);
			}
		}
	} else {
		struct es_dvp2axi_stream *stream;
		int ch_id;
		int lastline;

		intstat = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT);
		if (intstat)
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT,
					     intstat);
		else
			return;
		lastline = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_LINE_CNT);
		dvp2axi_dev->err_state_work.lastline = lastline;
		dvp2axi_dev->err_state_work.intstat = intstat;
		stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];

		if (intstat & DVP_SIZE_ERR) {
			dvp2axi_dev->irq_stats.dvp_size_err_cnt++;
			es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL,
						0x000A0000);
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_SIZE;
		}

		if (intstat & DVP_FIFO_OVERFLOW) {
			dvp2axi_dev->irq_stats.dvp_overflow_cnt++;
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_OVERFLOW;
		}

		if (intstat & DVP_BANDWIDTH_LACK) {
			dvp2axi_dev->irq_stats.dvp_bwidth_lack_cnt++;
			dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_BANDWIDTH_LACK;
		}

		if (intstat & INTSTAT_ERR_EIC770X) {
			dvp2axi_dev->irq_stats.all_err_cnt++;
		}

		for (i = 0; i < ES_DVP2AXI_MAX_STREAM_DVP; i++) {
			ch_id = es_dvp2axi_dvp_g_ch_id_by_fe(&dvp2axi_dev->v4l2_dev,
							intstat);

			if (ch_id < 0)
				continue;

			stream = &dvp2axi_dev->stream[ch_id];
			if (!dvp2axi_dev->sditf[0] ||
			    dvp2axi_dev->sditf[0]->mode.rdbk_mode)
				stream->buf_wake_up_cnt++;

			if (stream->stopping) {
				es_dvp2axi_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
				continue;
			}

			if (stream->state != ES_DVP2AXI_STATE_STREAMING)
				continue;
			stream->is_in_vblank = true;
			switch (ch_id) {
			case ES_DVP2AXI_STREAM_MIPI_ID0:
				stream->frame_phase = SW_FRM_END_ID0(intstat);
				intstat &= ~DVP_ALL_END_ID0;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID1:
				stream->frame_phase = SW_FRM_END_ID1(intstat);
				intstat &= ~DVP_ALL_END_ID1;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID2:
				stream->frame_phase = SW_FRM_END_ID2(intstat);
				intstat &= ~DVP_ALL_END_ID2;
				break;
			case ES_DVP2AXI_STREAM_MIPI_ID3:
				stream->frame_phase = SW_FRM_END_ID3(intstat);
				intstat &= ~DVP_ALL_END_ID3;
				break;
			}
			if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_VICAP) {
				if (dvp2axi_dev->sync_cfg.type == ES_DVP2AXI_NOSYNC_MODE)
					is_update = true;
				else
					is_update = es_dvp2axi_check_buffer_prepare(
						stream);
				if (is_update)
					es_dvp2axi_update_stream(dvp2axi_dev, stream,
							    ch_id);
			} else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ISP) {
				es_dvp2axi_update_stream_toisp(dvp2axi_dev, stream,
							  ch_id);
			} 
			// else if (stream->dma_en & ES_DVP2AXI_DMAEN_BY_ESKIT) {
			// 	es_dvp2axi_update_stream_eskit(dvp2axi_dev, stream,
			// 				   ch_id);
			// }

			if (stream->to_en_dma)
				es_dvp2axi_enable_dma_capture(stream, false);
			if (stream->to_stop_dma) {
				es_dvp2axi_stop_dma_capture(stream);
				wake_up(&stream->wq_stopped);
			}
			dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
		}

		if (intstat & DVP_FRAME0_START_ID0 ||
		    intstat & DVP_FRAME1_START_ID0) {
			stream->is_in_vblank = false;
			if (!stream->cur_skip_frame)
				es_dvp2axi_deal_sof(dvp2axi_dev);
		}

		if (stream->crop_dyn_en)
			es_dvp2axi_dynamic_crop(stream);
	}
}

void es_dvp2axi_irq_pingpong(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_stream *stream;
	// struct es_dvp2axi_stream *detect_stream = &dvp2axi_dev->stream[0];
	struct v4l2_mbus_config *mbus;
	unsigned int intstat = 0x0, i = 0xff;
	// unsigned long flags;
	// int ret = 0;

	if (!dvp2axi_dev->active_sensor)
		return;

	mbus = &dvp2axi_dev->active_sensor->mbus;

	u32 lastline, lastpix, ctl;
	u32 dvp2axi_frmst, frmid, int_en;
	int ch_id;

	intstat = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT);
	dvp2axi_frmst =
		es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_FRAME_STATUS);
	lastline = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_LAST_LINE);
	lastline = DVP2AXI_FETCH_Y_LAST_LINE(lastline);
	lastpix = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_LAST_PIX);
	lastpix = DVP2AXI_FETCH_Y_LAST_LINE(lastpix);
	ctl = es_dvp2axi_read_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL);
	dvp2axi_dev->err_state_work.lastline = lastline;
	dvp2axi_dev->err_state_work.lastpixel = lastpix;
	dvp2axi_dev->err_state_work.intstat = intstat;

	es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_INTSTAT, intstat);

	stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];

	if (intstat & BUS_ERR) {
		dvp2axi_dev->irq_stats.dvp_bus_err_cnt++;
		dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_BUS;
	}

	if (intstat & DVP_ALL_OVERFLOW) {
		dvp2axi_dev->irq_stats.dvp_overflow_cnt++;
		dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_OVERFLOW;
	}

	if (intstat & LINE_ERR) {
		dvp2axi_dev->irq_stats.dvp_line_err_cnt++;
		dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_LINE;
	}

	if (intstat & PIX_ERR) {
		dvp2axi_dev->irq_stats.dvp_pix_err_cnt++;
		dvp2axi_dev->err_state |= ES_DVP2AXI_ERR_PIXEL;
	}

	if (intstat & INTSTAT_ERR)
		dvp2axi_dev->irq_stats.all_err_cnt++;

	/* There are two irqs enabled:
		*  - PST_INF_FRAME_END: dvp2axi FIFO is ready,
		*    this is prior to FRAME_END
		*  - FRAME_END: dvp2axi has saved frame to memory,
		*    a frame ready
		*/
	if ((intstat & PST_INF_FRAME_END)) {
		stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];
		if (stream->stopping)
			/* To stop DVP2AXI ASAP, before FRAME_END irq */
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_DVP_CTRL,
							ctl & (~ENABLE_CAPTURE));
	}

	for (i = 0; i < ES_DVP2AXI_MAX_STREAM_DVP; i++) {
		ch_id = es_dvp2axi_dvp_g_ch_id(&dvp2axi_dev->v4l2_dev,
						&intstat, dvp2axi_frmst);

		if (ch_id < 0)
			continue;

		if (ch_id == ES_DVP2AXI_STREAM_MIPI_ID0) {
			int_en = es_dvp2axi_read_register(
				dvp2axi_dev, DVP2AXI_REG_DVP_INTEN);
			int_en |= LINE_INT_EN;
			es_dvp2axi_write_register(dvp2axi_dev,
							DVP2AXI_REG_DVP_INTEN,
							int_en);
			dvp2axi_dev->dvp_sof_in_oneframe = 0;
		}

		stream = &dvp2axi_dev->stream[ch_id];

		if (stream->stopping) {
			es_dvp2axi_stream_stop(stream);
			stream->stopping = false;
			wake_up(&stream->wq_stopped);
			continue;
		}

		if (stream->state != ES_DVP2AXI_STATE_STREAMING)
			continue;

		switch (ch_id) {
		case ES_DVP2AXI_STREAM_MIPI_ID0:
			stream->frame_phase =
				DVP_FRM_STS_ID0(dvp2axi_frmst);
			break;
		case ES_DVP2AXI_STREAM_MIPI_ID1:
			stream->frame_phase =
				DVP_FRM_STS_ID1(dvp2axi_frmst);
			break;
		case ES_DVP2AXI_STREAM_MIPI_ID2:
			stream->frame_phase =
				DVP_FRM_STS_ID2(dvp2axi_frmst);
			break;
		case ES_DVP2AXI_STREAM_MIPI_ID3:
			stream->frame_phase =
				DVP_FRM_STS_ID3(dvp2axi_frmst);
			break;
		}

		frmid = DVP2AXI_GET_FRAME_ID(dvp2axi_frmst);
		if ((frmid == 0xfffd) || (frmid == 0xfffe)) {
			v4l2_info(&dvp2axi_dev->v4l2_dev,
					"frmid:%d, frmstat:0x%x\n",
					frmid, dvp2axi_frmst);
			es_dvp2axi_write_register(
				dvp2axi_dev,
				DVP2AXI_REG_DVP_FRAME_STATUS,
				FRAME_STAT_CLS);
		}
		es_dvp2axi_update_stream(dvp2axi_dev, stream, ch_id);
		dvp2axi_dev->irq_stats.frm_end_cnt[stream->id]++;
	}

	if ((intstat & LINE_INT_END) && !(intstat & FRAME_END) &&
		(dvp2axi_dev->dvp_sof_in_oneframe == 0)) {
		if ((intstat &
				(PRE_INF_FRAME_END | PST_INF_FRAME_END)) == 0x0) {
			if ((intstat & INTSTAT_ERR) == 0x0) {
				if (!stream->cur_skip_frame)
					es_dvp2axi_deal_sof(dvp2axi_dev);
				int_en = es_dvp2axi_read_register(
					dvp2axi_dev, DVP2AXI_REG_DVP_INTEN);
				int_en &= ~LINE_INT_EN;
				es_dvp2axi_write_register(dvp2axi_dev,
								DVP2AXI_REG_DVP_INTEN,
								int_en);
				dvp2axi_dev->dvp_sof_in_oneframe = 1;
			}
		}
	}

	if (stream->crop_dyn_en)
		es_dvp2axi_dynamic_crop(stream);
}

int es_dvp2axi_sditf_disconnect(struct video_device *vdev)
{
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_stream *stream = to_es_dvp2axi_stream(vnode);
	struct es_dvp2axi_device *dvp2axidev = stream->dvp2axidev;
	struct media_link *link;
	int ret;

	link = list_first_entry(&dvp2axidev->sditf[0]->sd.entity.links,
				struct media_link, list);
	ret = media_entity_setup_link(link, 0);
	if (ret)
		dev_err(dvp2axidev->dev,
			"failed to disable link of sditf with isp");

	return ret;
}
EXPORT_SYMBOL(es_dvp2axi_sditf_disconnect);
