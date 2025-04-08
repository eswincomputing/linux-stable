/****************************************************************************
*
*	The MIT License (MIT)
*
*	Copyright (c) 2014 - 2024 Vivante Corporation
*
*	Permission is hereby granted, free of charge, to any person obtaining a
*	copy of this software and associated documentation files (the "Software"),
*	to deal in the Software without restriction, including without limitation
*	the rights to use, copy, modify, merge, publish, distribute, sublicense,
*	and/or sell copies of the Software, and to permit persons to whom the
*	Software is furnished to do so, subject to the following conditions:
*
*	The above copyright notice and this permission notice shall be included in
*	all copies or substantial portions of the Software.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*	DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*	The GPL License (GPL)
*
*	Copyright (C) 2014 - 2024 Vivante Corporation
*
*	This program is free software; you can redistribute it and/or
*	modify it under the terms of the GNU General Public License
*	as published by the Free Software Foundation; either version 2
*	of the License, or (at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program; if not, write to the Free Software Foundation,
*	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*	Note: This software is released under dual MIT and GPL licenses. A
*	recipient may use this file under the terms of either the MIT license or
*	GPL License. If you wish to use only one license not the other, you can
*	indicate your decision by deleting one of the above license notices in your
*	version of this file.
*
*****************************************************************************/

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_graph.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include "../../../../eswin_vi.h"
#include "vvcam_v4l2_common.h"
#include "vvcam_isp_driver.h"
#include "vvcam_isp_event.h"
#include "vvcam_isp_ctrl.h"
#include "vvcam_isp_procfs.h"
#include "vvcam_v4l2_std_exts.h"
#ifdef VVCAM_PLATFORM_REGISTER
#include "vvcam_isp_platform.h"
#endif
#define VVCAM_ISP_NAME		"vvcam-isp-subdev"
#define VVCAM_ISP_NAME_D1	"vvcam-isp-subdev-d1"

#ifdef VVCAM_PLATFORM_REGISTER
#define VVCAM_ISP_DEFAULT_SENSOR_LIB			"libos08a20.so"
#define VVCAM_ISP_DEFAULT_SENSOR_ISI_SYM		"OS08a20_IsiCamDrvConfig"
#define VVCAM_ISP_DEFAULT_SENSOR_MODE			0
#define VVCAM_ISP_DEFAULT_SENSOR_XML			"OS08a20_8M_02_1080p.xml"
#define VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON		"vvbcfg/project_json_file/manual_ext.json"
#define VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON		"vvbcfg/project_json_file/auto.json"
#endif

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation

struct vvcam_isp_format vvcam_isp_mp_fmts[] = {
	{
		.fourcc		= V4L2_PIX_FMT_NV16,
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.code		= MEDIA_BUS_FMT_YUYV8_1_5X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.code		= MEDIA_BUS_FMT_YUYV8_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.code		= MEDIA_BUS_FMT_SBGGR8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.code		= MEDIA_BUS_FMT_SGBRG8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.code		= MEDIA_BUS_FMT_SGRBG8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.code		= MEDIA_BUS_FMT_SRGGB8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P010,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.code		= MEDIA_BUS_FMT_Y8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10BPACK,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10DWA,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P00BPACK,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P00DWA,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P02BPACK,
		.code		= MEDIA_BUS_FMT_YUYV12_2X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P20BPACK,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P20DWA,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P210,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P22BPACK,
		.code		= MEDIA_BUS_FMT_YUYV12_2X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I20BPACK,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I210,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_M48BPACK,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I48BPACK,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I48DWA,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I40DWA,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.code		= MEDIA_BUS_FMT_RGB888_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24DWA,
		.code		= MEDIA_BUS_FMT_RGB888_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24P,
		.code		= MEDIA_BUS_FMT_RGB888_3X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10DWA,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10DWA,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10DWA,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10DWA,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc	= V4L2_PIX_FMT_SBGGR12DWA,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12DWA,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12DWA,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12DWA,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14DWA,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14DWA,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14DWA,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14DWA,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR16,
		.code		= MEDIA_BUS_FMT_SBGGR16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG16,
		.code		= MEDIA_BUS_FMT_SGBRG16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG16,
		.code		= MEDIA_BUS_FMT_SGRBG16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB16,
		.code		= MEDIA_BUS_FMT_SRGGB16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR24,
		.code		= MEDIA_BUS_FMT_SBGGR24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG24,
		.code		= MEDIA_BUS_FMT_SGBRG24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG24,
		.code		= MEDIA_BUS_FMT_SGRBG24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB24,
		.code		= MEDIA_BUS_FMT_SRGGB24_1X24,
	},
};

struct vvcam_isp_format vvcam_isp_sp_fmts[] = {
	{
		.fourcc		= V4L2_PIX_FMT_NV16,
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12,
		.code		= MEDIA_BUS_FMT_YUYV8_1_5X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.code		= MEDIA_BUS_FMT_YUYV8_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P010,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.code		= MEDIA_BUS_FMT_Y8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10BPACK,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10DWA,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y10,
		.code		= MEDIA_BUS_FMT_Y10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P00BPACK,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P00DWA,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P02BPACK,
		.code		= MEDIA_BUS_FMT_YUYV12_2X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P20BPACK,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P20DWA,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P210,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_P22BPACK,
		.code		= MEDIA_BUS_FMT_YUYV12_2X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I210,
		.code		= MEDIA_BUS_FMT_YUYV10_2X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_M48BPACK,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I48BPACK,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I48DWA,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_I40DWA,
		.code		= MEDIA_BUS_FMT_YUV8_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.code		= MEDIA_BUS_FMT_RGB888_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24DWA,
		.code		= MEDIA_BUS_FMT_RGB888_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGB24P,
		.code		= MEDIA_BUS_FMT_RGB888_3X8,
	},
};

// main path
struct vvcam_isp_format vvcam_isp_raw_fmts[] = {
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.code		= MEDIA_BUS_FMT_SBGGR8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.code		= MEDIA_BUS_FMT_SGBRG8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.code		= MEDIA_BUS_FMT_SGRBG8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.code		= MEDIA_BUS_FMT_SRGGB8_1X8,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR10DWA,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG10DWA,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG10DWA,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB10DWA,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR12DWA,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG12DWA,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG12DWA,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB12DWA,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14BPACK,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14BPACK,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14BPACK,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14BPACK,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14DWA,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14DWA,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14DWA,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14DWA,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR14,
		.code		= MEDIA_BUS_FMT_SBGGR14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG14,
		.code		= MEDIA_BUS_FMT_SGBRG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG14,
		.code		= MEDIA_BUS_FMT_SGRBG14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB14,
		.code		= MEDIA_BUS_FMT_SRGGB14_1X14,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR16,
		.code		= MEDIA_BUS_FMT_SBGGR16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG16,
		.code		= MEDIA_BUS_FMT_SGBRG16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG16,
		.code		= MEDIA_BUS_FMT_SGRBG16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB16,
		.code		= MEDIA_BUS_FMT_SRGGB16_1X16,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SBGGR24,
		.code		= MEDIA_BUS_FMT_SBGGR24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGBRG24,
		.code		= MEDIA_BUS_FMT_SGBRG24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SGRBG24,
		.code		= MEDIA_BUS_FMT_SGRBG24_1X24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_SRGGB24,
		.code		= MEDIA_BUS_FMT_SRGGB24_1X24,
	},

};

static int vvcam_isp_querycap(struct v4l2_subdev *sd, void *arg)
{
	struct v4l2_capability *cap = (struct v4l2_capability *)arg;

	strlcpy(cap->driver, sd->name, sizeof(cap->driver));
	strlcpy(cap->card, sd->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform:%s", sd->name);

	return 0;
}

static int vvcam_isp_pad_requbufs(struct v4l2_subdev *sd, void *arg)
{
	struct vvcam_pad_reqbufs *pad_requbufs = (struct vvcam_pad_reqbufs *)arg;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	return vvcam_isp_requebus_event(isp_dev, pad_requbufs->pad, pad_requbufs->num_buffers);
}

static int vvcam_isp_pad_buf_queue(struct v4l2_subdev *sd, void *arg)
{
	struct vvcam_pad_buf *pad_buf = (struct vvcam_pad_buf *)arg;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int ret;
	unsigned long flags;
	struct vvcam_isp_pad_data *cur_pad;

	cur_pad = &isp_dev->pad_data[pad_buf->pad];

	spin_lock_irqsave(&cur_pad->qlock, flags);

	list_add_tail(&pad_buf->buf->list, &cur_pad->queue);

	spin_unlock_irqrestore(&cur_pad->qlock, flags);

	ret = vvcam_isp_qbuf_event(isp_dev, pad_buf->pad, pad_buf->buf);

	return ret;
}
// static struct v4l2_subdev *find_remote_sensor(struct media_entity *entity)
// {
//	 struct v4l2_subdev *subdev;
// 	struct media_pad *pad;

// 	while (1) {
// 		pad = &entity->pads[0];
// 		if (!(pad->flags & MEDIA_PAD_FL_SINK))
// 			return NULL;

// 		pad = media_pad_remote_pad_first(pad);
// 		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
// 			return NULL;

// 		entity = pad->entity;
// 		if (entity->function == MEDIA_ENT_F_CAM_SENSOR) {
//			 subdev = media_entity_to_v4l2_subdev(entity);
//			 if (subdev) {
//				 //printk("Found remote sensor subdev: %s\n", subdev->name);
//				 return subdev;
//			 }
//		 }
// 	}
//	 return NULL;
// }
static int vvcam_isp_pad_s_stream(struct v4l2_subdev *sd, void *arg)
{
	struct vvcam_pad_stream_status *pad_stream = (struct vvcam_pad_stream_status *)arg;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	isp_dev->pad_data[pad_stream->pad].stream = pad_stream->status;

	if (pad_stream->status == 0 ) {
		INIT_LIST_HEAD(&isp_dev->pad_data[pad_stream->pad].queue);
	}

	return vvcam_isp_s_stream_event(isp_dev, pad_stream->pad, pad_stream->status);
}

static int vvcam_isp_buf_done(struct v4l2_subdev *sd, void *arg)
{
	struct vvcam_isp_buf ubuf;
	struct vvcam_isp_pad_data *cur_pad;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	unsigned long flags;
	struct vvcam_vb2_buffer *pos, *next;
	struct vvcam_vb2_buffer *buf = NULL;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct video_device *video;
	struct vvcam_pad_buf pad_buf;
	int ret;

	memcpy(&ubuf, arg, sizeof(struct vvcam_isp_buf));
	cur_pad = &isp_dev->pad_data[ubuf.pad];
	isp_dev->frame_idx++;

	if (list_empty(&cur_pad->queue) || (cur_pad->stream == 0))
		return -EINVAL;
	spin_lock_irqsave(&cur_pad->qlock, flags);
	list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
		if (pos && (pos->sequence == ubuf.index)) {
			buf = pos;
			list_del(&pos->list);
			break;
		}
	}
	spin_unlock_irqrestore(&cur_pad->qlock, flags);
	if (buf) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
		pad = media_pad_remote_pad_first(&isp_dev->pads[ubuf.pad]);
#else
		pad = media_entity_remote_pad(&isp_dev->pads[ubuf.pad]);
#endif
		if (!pad)
			return -EINVAL;
		if (is_media_entity_v4l2_subdev(pad->entity)) {
			subdev = media_entity_to_v4l2_subdev(pad->entity);
			memset(&pad_buf, 0, sizeof(pad_buf));
			pad_buf.pad = pad->index;
			pad_buf.buf = buf;
			ret = v4l2_subdev_call(subdev, core, ioctl, VVCAM_PAD_BUF_DONE, &pad_buf);
			if (ret)
				return ret;
		} else if (is_media_entity_v4l2_video_device(pad->entity)){
			video = media_entity_to_video_device(pad->entity);
			if (buf->sequence < video->queue->num_buffers) {
				if (buf->vb.vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
					buf->vb.sequence = isp_dev->frame_idx - 1;
					vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
					//printk("vb done addr = 0x%x\n", buf->vb.vb2_buf.planes[0].dma_addr + buf->vb.vb2_buf.planes[0].offset);
				}
			}
		}
	}

	return 0;
}

static int vvcam_isp_queryctrl(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_queryctrl *pad_querctrl =
									(struct vvcam_pad_queryctrl *)arg;
	ret = v4l2_queryctrl(&isp_dev->ctrl_handler, pad_querctrl->query_ctrl);

	return ret;
}

static int vvcam_isp_query_ext_ctrl(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_query_ext_ctrl *pad_quer_ext_ctrl =
									(struct vvcam_pad_query_ext_ctrl *)arg;
	ret = v4l2_query_ext_ctrl(&isp_dev->ctrl_handler,
						pad_quer_ext_ctrl->query_ext_ctrl);

	return ret;
}

static int vvcam_isp_querymenu(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_querymenu *pad_quermenu =
									(struct vvcam_pad_querymenu *)arg;
	ret = v4l2_querymenu(&isp_dev->ctrl_handler,
						pad_quermenu->querymenu);

	return ret;
}

static int vvcam_isp_get_mipi_id(struct v4l2_subdev *sd,void *arg)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	int port = *(int *)arg;
	struct media_pad *remote_pad;
	struct v4l2_subdev *mipi_sd;
	struct video_device *vdev;
	struct device *dev = isp_dev->dev;

	if (port < 0 || port >= VVCAM_ISP_PORT_NR)
		return -EINVAL;

	int base = port * VVCAM_ISP_PORT_PAD_NR;
	struct media_pad *pad = &isp_dev->pads[base];

	if (!pad->entity) {
		dev_err(dev, "ISP: base %d pad invalid (not bound to entity)\n", base);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	remote_pad = media_pad_remote_pad_first(&isp_dev->pads[base]);
#else
	remote_pad = media_entity_remote_pad(&isp_dev->pads[base]);
#endif
	if (!remote_pad) {
		dev_err(dev, "ISP: no remote pad found for MIPI CSI\n");
	} else {
		mipi_sd = media_entity_to_v4l2_subdev(remote_pad->entity);
		if (!mipi_sd || !mipi_sd->devnode) {
			isp_dev->mipi_subdev_index[base] = -1;
		dev_err(dev, "ISP: remote entity is not a valid MIPI subdev\n");
	} else {
		vdev = mipi_sd->devnode;
		isp_dev->mipi_subdev_index[base] = vdev->num;
		dev_dbg(dev, "MIPI CSI connected to /dev/v4l-subdev%d\n",
			isp_dev->mipi_subdev_index[base]);
	}
	}
    
	if(isp_dev->mipi_subdev_index[port] < 0)
		dev_err(dev, "port not connected\n");
	else
		*(int *)arg = isp_dev->mipi_subdev_index[port];

	return 0;
}

static int vvcam_isp_g_ctrl(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_control *pad_ctrl = (struct vvcam_pad_control *)arg;

	mutex_lock(&isp_dev->ctrl_lock);
	isp_dev->ctrl_pad = pad_ctrl->pad;
	ret = v4l2_g_ctrl(&isp_dev->ctrl_handler, pad_ctrl->control);
	mutex_unlock(&isp_dev->ctrl_lock);

	return ret;
}

static int vvcam_isp_s_ctrl(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_control *pad_ctrl = (struct vvcam_pad_control *)arg;

	mutex_lock(&isp_dev->ctrl_lock);
	isp_dev->ctrl_pad = pad_ctrl->pad;
	ret = v4l2_s_ctrl(NULL, &isp_dev->ctrl_handler, pad_ctrl->control);
	mutex_unlock(&isp_dev->ctrl_lock);

	return ret;
}

static int vvcam_isp_g_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_ext_controls *pad_ext_ctrls =
							(struct vvcam_pad_ext_controls *)arg;

	mutex_lock(&isp_dev->ctrl_lock);
	isp_dev->ctrl_pad = pad_ext_ctrls->pad;
	ret = v4l2_g_ext_ctrls(&isp_dev->ctrl_handler, sd->devnode,
							sd->v4l2_dev->mdev,
							pad_ext_ctrls->ext_controls);
	mutex_unlock(&isp_dev->ctrl_lock);

	return ret;
}

static int vvcam_isp_s_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_ext_controls *pad_ext_ctrls =
							(struct vvcam_pad_ext_controls *)arg;

	mutex_lock(&isp_dev->ctrl_lock);
	isp_dev->ctrl_pad = pad_ext_ctrls->pad;
	ret = v4l2_s_ext_ctrls(NULL, &isp_dev->ctrl_handler, sd->devnode,
							sd->v4l2_dev->mdev,
							pad_ext_ctrls->ext_controls);
	mutex_unlock(&isp_dev->ctrl_lock);

	return ret;
}

static int vvcam_isp_try_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
	int ret;
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_pad_ext_controls *pad_ext_ctrls =
							(struct vvcam_pad_ext_controls *)arg;
	ret = v4l2_try_ext_ctrls(&isp_dev->ctrl_handler, sd->devnode,
							sd->v4l2_dev->mdev,
							pad_ext_ctrls->ext_controls);

	return ret;
}

static long vvcam_isp_priv_ioctl(struct v4l2_subdev *sd,
								unsigned int cmd, void *arg)
{
	int ret = -EINVAL;
	switch (cmd) {
		case VIDIOC_QUERYCAP:
			ret = vvcam_isp_querycap(sd, arg);
			break;
		case VVCAM_PAD_REQUBUFS:
			ret = vvcam_isp_pad_requbufs(sd, arg);
			break;
		case VVCAM_PAD_BUF_QUEUE:
			ret = vvcam_isp_pad_buf_queue(sd, arg);
			break;
		case VVCAM_PAD_S_STREAM:
			ret = vvcam_isp_pad_s_stream(sd, arg);
			break;
		case VVCAM_ISP_IOC_BUFDONE:
			ret = vvcam_isp_buf_done(sd, arg);
			break;
		case VVCAM_PAD_QUERYCTRL:
			ret = vvcam_isp_queryctrl(sd, arg);
			break;
		case VVCAM_PAD_QUERY_EXT_CTRL:
			ret = vvcam_isp_query_ext_ctrl(sd, arg);
			break;
		case VVCAM_PAD_G_CTRL:
			ret = vvcam_isp_g_ctrl(sd, arg);
			break;
		case VVCAM_PAD_S_CTRL:
			ret = vvcam_isp_s_ctrl(sd, arg);
			break;
		case VVCAM_PAD_G_EXT_CTRLS:
			ret = vvcam_isp_g_ext_ctrls(sd, arg);
			break;
		case VVCAM_PAD_S_EXT_CTRLS:
			ret = vvcam_isp_s_ext_ctrls(sd, arg);
			break;
		case VVCAM_PAD_TRY_EXT_CTRLS:
			ret = vvcam_isp_try_ext_ctrls(sd, arg);
			break;
		case VVCAM_PAD_QUERYMENU:
			ret = vvcam_isp_querymenu(sd, arg);
			break;
		case VVAM_CMD_GET_MIPI_ID:
			ret = vvcam_isp_get_mipi_id(sd, arg);
			break;
		default:
			break;
	}
	return ret;
}

int vvcam_isp_subscribe_event(struct v4l2_subdev *sd,
							struct v4l2_fh *fh,
							struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
		case V4L2_EVENT_CTRL:
			return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
		case VVCAM_ISP_DEAMON_EVENT:
			return v4l2_event_subscribe(fh, sub, 2, NULL);
		default:
			return -EINVAL;
	}

}

int vvcam_isp_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static struct v4l2_subdev_core_ops vvcam_isp_core_ops = {
	.ioctl				= vvcam_isp_priv_ioctl,
	.subscribe_event	= vvcam_isp_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_video_ops vvcam_isp_video_ops = {
	.s_stream = vvcam_isp_s_stream,
};

static int vvcam_isp_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_format *format)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	uint32_t w, h;
	uint32_t sink_pad_index;
	struct vvcam_isp_pad_data *cur_pad = &isp_dev->pad_data[format->pad];
	struct vvcam_isp_pad_data *sink_pad;
	struct vvcam_isp_pad_data *source_pad;
	int i;
	int ret;
	uint32_t fourcc_code = 0;

	sink_pad_index = format->pad - (format->pad % VVCAM_ISP_PORT_PAD_NR);
	sink_pad = &isp_dev->pad_data[sink_pad_index];

	if (sink_pad == cur_pad) {
		cur_pad->sink_detected = 1;
		cur_pad->format = format->format;
		for (i = 1; i < VVCAM_ISP_PORT_PAD_NR; i++) {
			source_pad = &isp_dev->pad_data[sink_pad_index + i];
			source_pad->sink_detected = 1;
			source_pad->crop.left = 0;
			source_pad->crop.top = 0;
			source_pad->crop.width = format->format.width;
			source_pad->crop.height = format->format.height;
			source_pad->compose.left = 0;
			source_pad->compose.top = 0;
			source_pad->compose.width = format->format.width;
			source_pad->compose.height = format->format.height;

			switch (i) {
				case VVCAM_ISP_PORT_PAD_SOURCE_MP:
				case VVCAM_ISP_PORT_PAD_SOURCE_SP1:
				case VVCAM_ISP_PORT_PAD_SOURCE_SP2:
				case VVCAM_ISP_PORT_PAD_SOURCE_RAW:
					source_pad->format = format->format;
					source_pad->format.code = source_pad->fmts[0].code;
					memcpy(source_pad->format.reserved, &source_pad->fmts[0].fourcc, sizeof(uint32_t));
					source_pad->format.field = V4L2_FIELD_NONE;
					source_pad->format.quantization = V4L2_QUANTIZATION_DEFAULT;
					source_pad->format.colorspace = V4L2_COLORSPACE_DEFAULT;
					break;
				default:
					break;
			}
		}
		return 0;
	}

	w = ALIGN(format->format.width, VVCAM_ISP_WIDTH_ALIGN);
	h = ALIGN(format->format.height, VVCAM_ISP_HEIGHT_ALIGN);
	w = clamp_t(uint32_t, w, VVCAM_ISP_WIDTH_MIN, sink_pad->format.width);
	h = clamp_t(uint32_t, h, VVCAM_ISP_HEIGHT_MIN, sink_pad->format.height);

	format->format.width = w;
	format->format.height = h;

	memcpy(&fourcc_code, format->format.reserved, sizeof(uint32_t));

	for (i = 0; i < cur_pad->num_formats; i++) {
		if (format->format.code == cur_pad->fmts[i].code && fourcc_code == cur_pad->fmts[i].fourcc) {
			break;
		}
	}

	if (i >= cur_pad->num_formats) {
		format->format.code = cur_pad->fmts[0].code;
		memcpy(format->format.reserved, &cur_pad->fmts[0].fourcc, sizeof(uint32_t));
	}

	ret = vvcam_isp_set_fmt_event(isp_dev, format->pad, &format->format);
	if (ret) {
		printk("isp set fmt event failed %d\n", ret);
		return ret;
	}


	cur_pad->compose.left = 0;
	cur_pad->compose.top = 0;
	cur_pad->compose.width = w;
	cur_pad->compose.height = h;

	cur_pad->format = format->format;

	return 0;
}

static int vvcam_isp_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_format *format)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[format->pad];

	if (pad_data->sink_detected) {
		format->format = pad_data->format;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vvcam_isp_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[code->pad];

	if (code->index >= pad_data->num_formats) {
		return -EINVAL;
	}

	code->code = pad_data->fmts[code->index].code;
	code->reserved[0] = pad_data->fmts[code->index].fourcc;

	return 0;
}

static int vvcam_isp_set_selection(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_selection *sd_sel)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[sd_sel->pad];
	struct vvcam_isp_pad_data *sink_pad;
	struct vvcam_isp_selection isp_sel;
	uint32_t sink_pad_index;
	int ret = -EINVAL;

	sink_pad_index = sd_sel->pad - (sd_sel->pad % VVCAM_ISP_PORT_PAD_NR);
	sink_pad = &isp_dev->pad_data[sink_pad_index];

	sd_sel->r.width = ALIGN(sd_sel->r.width, VVCAM_ISP_WIDTH_ALIGN);
	sd_sel->r.height = ALIGN(sd_sel->r.height, VVCAM_ISP_HEIGHT_ALIGN);

	switch (sd_sel->target)
	{
		case V4L2_SEL_TGT_CROP:
			if (((sd_sel->r.left + sd_sel->r.width) < VVCAM_ISP_WIDTH_MIN) ||
				((sd_sel->r.left + sd_sel->r.width) > sink_pad->format.width) ||
				((sd_sel->r.top + sd_sel->r.height) < VVCAM_ISP_HEIGHT_MIN) ||
				((sd_sel->r.top + sd_sel->r.height) > sink_pad->format.height)) {
				return -EINVAL;
			}
			break;

		case V4L2_SEL_TGT_COMPOSE:
			if (((sd_sel->r.left + sd_sel->r.width) < VVCAM_ISP_WIDTH_MIN) ||
				((sd_sel->r.top + sd_sel->r.height) < VVCAM_ISP_HEIGHT_MIN)) {
				return -EINVAL;
			}
			break;

		default:
			return -EINVAL;
	}

	isp_sel.rect = sd_sel->r;
	if (sd_sel->target == V4L2_SEL_TGT_CROP) {
		isp_sel.type = VVCAM_ISP_SEL_TYPE_CROP;
	} else {
		isp_sel.type = VVCAM_ISP_SEL_TYPE_COMPOSE;
	}

	ret = vvcam_isp_s_selection_event(isp_dev, sd_sel->pad, &isp_sel);
	if (ret == 0) {
		if (sd_sel->target == V4L2_SEL_TGT_CROP) {
			pad_data->crop = isp_sel.rect;
		} else {
			pad_data->compose = isp_sel.rect;
		}
	}

	return ret;
}

static int vvcam_isp_get_selection(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_selection *sel)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
	struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[sel->pad];
	struct vvcam_isp_pad_data *sink_pad;
	uint32_t sink_pad_index;

	sink_pad_index = sel->pad - (sel->pad % VVCAM_ISP_PORT_PAD_NR);
	sink_pad = &isp_dev->pad_data[sink_pad_index];

	switch (sel->target)
	{
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP_BOUNDS:
			sel->r.left = 0;
			sel->r.top = 0;
			sel->r.width = sink_pad->format.width;
			sel->r.height = sink_pad->format.height;
			break;

		case V4L2_SEL_TGT_CROP:
			sel->r = pad_data->crop;
			break;

		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
			sel->r.left = pad_data->crop.left;
			sel->r.top = pad_data->crop.top;
			sel->r.width = pad_data->crop.width;
			sel->r.height = pad_data->crop.height;
			break;

		case V4L2_SEL_TGT_COMPOSE:
			sel->r = pad_data->compose;
			break;

		default:
			return -EINVAL;
			break;
	}
	return 0;
}

static const struct v4l2_subdev_pad_ops vvcam_isp_pad_ops = {
	.set_fmt		= vvcam_isp_set_fmt,
	.get_fmt		= vvcam_isp_get_fmt,
	.enum_mbus_code	= vvcam_isp_enum_mbus_code,
	.set_selection	= vvcam_isp_set_selection,
	.get_selection	= vvcam_isp_get_selection,
};

struct v4l2_subdev_ops vvcam_isp_subdev_ops = {
	.core	= &vvcam_isp_core_ops,
	.video	= &vvcam_isp_video_ops,
	.pad	= &vvcam_isp_pad_ops,
};

static int vvcam_isp_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&isp_dev->mlock);
	isp_dev->refcnt++;
	int ret = pm_runtime_resume_and_get(sd->dev);
	if (ret < 0) {
		dev_err(sd->dev, "Failed to get runtime pm, %d\n");
		return ret;
	}

	mutex_unlock(&isp_dev->mlock);
	return 0;
}

static int vvcam_isp_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&isp_dev->mlock);
	isp_dev->refcnt--;
	pm_runtime_mark_last_busy(sd->dev);
	pm_runtime_put_autosuspend(sd->dev);

	mutex_unlock(&isp_dev->mlock);

	return 0;
}


static struct v4l2_subdev_internal_ops vvcam_isp_internal_ops = {
	.open  = vvcam_isp_open,
	.close = vvcam_isp_close,
};

static int vvcam_isp_link_setup(struct media_entity *entity,
		const struct media_pad *local,
		const struct media_pad *remote, u32 flags)
{
	printk(KERN_ERR "%s %d\n", __func__, __LINE__);
	return 0;
}

static const struct media_entity_operations vvcam_isp_entity_ops = {
	.link_setup		= vvcam_isp_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
	.get_fwnode_pad	= v4l2_subdev_get_fwnode_pad_1_to_1,

};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int vvcam_isp_notifier_bound(struct v4l2_async_notifier *notifier,
									struct v4l2_subdev *sd,
									struct v4l2_async_connection *asc)
#else
static int vvcam_isp_notifier_bound(struct v4l2_async_notifier *notifier,
									struct v4l2_subdev *sd,
									struct v4l2_async_subdev *asd)
#endif
{
	int ret = 0;
	struct vvcam_isp_dev *isp_dev = container_of(notifier,
			struct vvcam_isp_dev, notifier);
	struct device *dev = isp_dev->dev;

	struct fwnode_handle *ep = NULL;
	struct v4l2_fwnode_link link;
	struct media_entity *source, *sink;
	unsigned int source_pad, sink_pad;
	struct fwnode_handle *remote_node;
	const char *name;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ep = asc->match.fwnode;
#else
	ep = asd->match.fwnode;
#endif
	if (!ep)
		return 0;

	remote_node = fwnode_get_parent(ep);
	ret = v4l2_fwnode_parse_link(ep, &link);
	name = fwnode_get_name(remote_node);

	if (ret < 0) {
		dev_err(dev, "failed to parse link for %pOF: %d\n",
		to_of_node(ep), ret);
	}

	if (sd->entity.pads[link.local_port].flags == MEDIA_PAD_FL_SINK)
		dev_err(dev, "not sink pad\n");

	dev_dbg(dev, "remote_port:%d,local_port:%d\n",link.remote_port,link.local_port);
	sink = &isp_dev->sd.entity;
	sink_pad = link.remote_port;
	source = &sd->entity;
	source_pad = link.local_port;

	v4l2_fwnode_put_link(&link);

	ret = media_create_pad_link(source, source_pad,
		sink, sink_pad, MEDIA_LNK_FL_ENABLED);

	if (ret) {
		dev_err(dev, "failed to create %s:%u -> %s:%u flags = %ld link\n",
		source->name, source_pad,
		sink->name, sink_pad, sd->entity.pads[source_pad].flags);
	}

	fwnode_handle_put(ep);

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void vvcam_isp_notifier_unbound(struct v4l2_async_notifier *notifier,
									struct v4l2_subdev *sd,
									struct v4l2_async_connection *asc)
{
	return;
}
#else
static void vvcam_isp_notifier_unbound(struct v4l2_async_notifier *notifier,
									struct v4l2_subdev *sd,
									struct v4l2_async_subdev *asd)
{
	return;
}
#endif

static const struct v4l2_async_notifier_operations vvcam_isp_notify_ops = {
	.bound		= vvcam_isp_notifier_bound,
	.unbind		= vvcam_isp_notifier_unbound,
};

static int vvcam_isp_async_notifier(struct vvcam_isp_dev *isp_dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *remote_ep;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct v4l2_async_connection *asc;
#else
	struct v4l2_async_subdev *asd;
#endif
	struct device *dev = isp_dev->dev;
	int ret = 0;
	int pad = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	v4l2_async_subdev_nf_init(&isp_dev->notifier, &isp_dev->sd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	v4l2_async_nf_init(&isp_dev->notifier);
#else
	v4l2_async_notifier_init(&isp_dev->notifier);
#endif

	isp_dev->notifier.ops = &vvcam_isp_notify_ops;

	if (dev_fwnode(isp_dev->dev) == NULL)
		return 0;

	for (pad = 0; pad < VVCAM_ISP_PAD_NR; pad++) {
		if (isp_dev->pads[pad].flags != MEDIA_PAD_FL_SINK)
			continue;

		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
			pad, 0, FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;
		dev_dbg(dev, "pad %d: found ep = %s\n", pad, fwnode_get_name(ep));

		remote_ep = fwnode_graph_get_remote_endpoint(ep);
		if (!remote_ep) {
			fwnode_handle_put(ep);
			continue;
		}
		dev_dbg(dev, "%s, %d remote_ep:%s\n", __func__, __LINE__,
			fwnode_get_name(remote_ep));

		dev_dbg(dev, "%s, %d ep:%s\n", __func__, __LINE__,
			fwnode_get_name(ep));

		fwnode_handle_put(remote_ep);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		asc = v4l2_async_nf_add_fwnode_remote(&isp_dev->notifier,
			ep, struct v4l2_async_connection);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
		asd = v4l2_async_nf_add_fwnode_remote(&isp_dev->notifier,
			ep, struct v4l2_async_subdev);
#else
		asd = v4l2_async_notifier_add_fwnode_remote_subdev(&isp_dev->notifier,
			ep, struct v4l2_async_subdev);
#endif

		fwnode_handle_put(ep);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		if (IS_ERR(asc)) {
			ret = PTR_ERR(asc);
#else
		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
#endif
			if (ret != -EEXIST) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
				v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
				v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
				return ret;
			}
		}
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ret = v4l2_async_nf_register(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	ret = v4l2_async_subdev_nf_register(&isp_dev->sd,
							&isp_dev->notifier);
#else
	ret = v4l2_async_subdev_notifier_register(&isp_dev->sd,
							&isp_dev->notifier);
#endif
	if (ret) {
		dev_err(dev, "Async notifier register error\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
		v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
		v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
	}

	return ret;
}

static int vvcam_isp_pads_init(struct vvcam_isp_dev *isp_dev)
{
	int pad = 0;

	for (pad = 0; pad < VVCAM_ISP_PAD_NR; pad++) {
		if ((pad % VVCAM_ISP_PORT_PAD_NR) == VVCAM_ISP_PORT_PAD_SINK) {
			isp_dev->pads[pad].flags = MEDIA_PAD_FL_SINK;
		} else {
			isp_dev->pads[pad].flags = MEDIA_PAD_FL_SOURCE;
		}

		switch (pad % VVCAM_ISP_PORT_PAD_NR) {
			case VVCAM_ISP_PORT_PAD_SINK:
				break;
			case VVCAM_ISP_PORT_PAD_SOURCE_MP:
				isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_mp_fmts);
				isp_dev->pad_data[pad].fmts = vvcam_isp_mp_fmts;
				break;
			case VVCAM_ISP_PORT_PAD_SOURCE_SP1:
				isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_sp_fmts);
				isp_dev->pad_data[pad].fmts = vvcam_isp_sp_fmts;
				break;
			case VVCAM_ISP_PORT_PAD_SOURCE_SP2:
				isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_sp_fmts);
				isp_dev->pad_data[pad].fmts = vvcam_isp_sp_fmts;
				break;
			case VVCAM_ISP_PORT_PAD_SOURCE_RAW:
				isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_raw_fmts);
				isp_dev->pad_data[pad].fmts = vvcam_isp_raw_fmts;
				break;
			default:
				break;
		}

		INIT_LIST_HEAD(&isp_dev->pad_data[pad].queue);
		spin_lock_init(&isp_dev->pad_data[pad].qlock);
	}

	return 0;
}

static int vvcam_isp_parse_params(struct vvcam_isp_dev *isp_dev,
						struct platform_device *pdev)
{

#ifdef VVCAM_PLATFORM_REGISTER
	int port = 0;
	isp_dev->id  = pdev->id;
	for (port = 0; port < VVCAM_ISP_PORT_NR; port++) {
		strncpy(isp_dev->sensor_info[port].lib, VVCAM_ISP_DEFAULT_SENSOR_LIB,
			strlen(VVCAM_ISP_DEFAULT_SENSOR_LIB));
		strncpy(isp_dev->sensor_info[port].isi_sym, VVCAM_ISP_DEFAULT_SENSOR_ISI_SYM,
			strlen(VVCAM_ISP_DEFAULT_SENSOR_ISI_SYM));
		strncpy(isp_dev->sensor_info[port].xml, VVCAM_ISP_DEFAULT_SENSOR_XML,
			strlen(VVCAM_ISP_DEFAULT_SENSOR_XML));
		isp_dev->sensor_info[port].mode = VVCAM_ISP_DEFAULT_SENSOR_MODE;
		strncpy(isp_dev->sensor_info[port].manu_json, VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON,
			strlen(VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON));
		strncpy(isp_dev->sensor_info[port].auto_json, VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON,
			strlen(VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON));
	}
#else
	fwnode_property_read_u32(of_fwnode_handle(pdev->dev.of_node),
			"id", &isp_dev->id);
#endif
	return 0;
}

#ifdef VVCAM_PLATFORM_REGISTER
struct v4l2_subdev *g_vvcam_isp_subdev[VVCAM_ISP_DEV_MAX] = {NULL};
EXPORT_SYMBOL_GPL(g_vvcam_isp_subdev);
#endif

static int isp_smmu_sid_cfg(struct device* dev)
{
    int ret = 0;
    struct regmap* regmap = NULL;
    int mmu_tbu0_vi_isp_reg = 0;
    u32 rdwr_sid_ssid = 0;
    u32 sid = 0;

    struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

    pr_info("%s enter\n", __func__);
	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	if (fwspec == NULL) {
		pr_info("isp is not behind smmu, skip configuration of sid\n");
		return 0;
	}

	sid = fwspec->ids[0];

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
	if (IS_ERR(regmap)) {
		pr_info("No vi_top_csr phandle specified\n");
		return 0;
	}

	ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
					&mmu_tbu0_vi_isp_reg);
	if (ret) {
		pr_err("can't get isp sid cfg reg offset (%d)\n", ret);
		return ret;
	}

	/* make the reading sid the same as writing sid, ssid is fixed to zero */
	rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
	regmap_write(regmap, mmu_tbu0_vi_isp_reg, rdwr_sid_ssid);

	ret = win2030_dynm_sid_enable(dev_to_node(dev));
	if (ret < 0)
		pr_err("failed to config isp streamID(%d)!\n", sid);
	else
		pr_info("success to config isp streamID(%d)!\n", sid);

    pr_info("%s exit\n", __func__);
    return ret;

}

static int vvcam_isp_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct vvcam_isp_dev *isp_dev;
	u32 reg_val;
    int ret;

#ifdef CONFIG_NUMA
	u32 numa_id = 0;
	ret = of_property_read_u32(dev->of_node, "numa-node-id", &numa_id);
	if(ret) {
		dev_warn(dev, "Could not get numa-node-id, use default 0\n");
		numa_id = 0;
	}
#endif
	isp_dev = devm_kzalloc(&pdev->dev,
		        sizeof(struct vvcam_isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;

	isp_dev->vi_topcsr_regmap = syscon_regmap_lookup_by_phandle(isp_dev->dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(isp_dev->vi_topcsr_regmap)) {
        pr_err("No vi_top_csr phandle specified, regmap=%ld\n", PTR_ERR(isp_dev->vi_topcsr_regmap));
		return PTR_ERR(isp_dev->vi_topcsr_regmap);
    }

	ret = of_property_read_u32_index(isp_dev->dev->of_node, "eswin,vi_top_csr", 2, &isp_dev->vi_topcsr_reg);
	if (ret) {
		pr_err("Failed to get isp vi top clk reg offset, ret=%d\n", ret);
		return ret;
	}

	regmap_read(isp_dev->vi_topcsr_regmap, isp_dev->vi_topcsr_reg, &reg_val);
	reg_val |= (ISP0_CLK_EN | ISP1_CLK_EN);
	regmap_write(isp_dev->vi_topcsr_regmap, isp_dev->vi_topcsr_reg, reg_val);

	isp_dev->num_clks = devm_clk_bulk_get_all(isp_dev->dev, &isp_dev->clks_bulk);
	if (isp_dev->num_clks < 0)
		return dev_err_probe(isp_dev->dev, -ENODEV,
				     "Failed to get isp clocks\n");

	ret = clk_bulk_prepare_enable(isp_dev->num_clks, isp_dev->clks_bulk);
	if (ret)
		return dev_err_probe(isp_dev->dev, ret,
				     "Failed to enable isp clocks\n");

	isp_dev->rstc = devm_reset_control_array_get_shared(&pdev->dev);
	if (IS_ERR_OR_NULL(isp_dev->rstc)) {
		dev_err_probe(dev, PTR_ERR(isp_dev->rstc), "unable to get isp rst_cfg\n");
	}

	reset_control_deassert(isp_dev->rstc);

	(void)isp_smmu_sid_cfg(&pdev->dev);

	win2030_tbu_power(dev, true);

	mutex_init(&isp_dev->mlock);
	mutex_init(&isp_dev->ctrl_lock);
	atomic_set(&isp_dev->event_seq, 0);
	isp_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, isp_dev);

	ret = vvcam_isp_parse_params(isp_dev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse params\n");
		return -EINVAL;
	}

	v4l2_subdev_init(&isp_dev->sd, &vvcam_isp_subdev_ops);
#ifdef CONFIG_NUMA
	if (numa_id == 1) {
		snprintf(isp_dev->sd.name, V4L2_SUBDEV_NAME_SIZE,
			"%s.%d",VVCAM_ISP_NAME_D1, isp_dev->id);
	} else {
		snprintf(isp_dev->sd.name, V4L2_SUBDEV_NAME_SIZE,
			"%s.%d",VVCAM_ISP_NAME, isp_dev->id);
	}
#else
	snprintf(isp_dev->sd.name, V4L2_SUBDEV_NAME_SIZE,
		"%s.%d",VVCAM_ISP_NAME, isp_dev->id);
#endif
	isp_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	isp_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	isp_dev->sd.dev = &pdev->dev;
	isp_dev->sd.owner = THIS_MODULE;
	isp_dev->sd.internal_ops = &vvcam_isp_internal_ops;
	isp_dev->sd.entity.ops = &vvcam_isp_entity_ops;
	isp_dev->sd.entity.function = MEDIA_ENT_F_IO_V4L;
	isp_dev->sd.entity.obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
	isp_dev->sd.entity.name = isp_dev->sd.name;
	v4l2_set_subdevdata(&isp_dev->sd, isp_dev);
	vvcam_isp_pads_init(isp_dev);
	ret = media_entity_pads_init(&isp_dev->sd.entity,
								VVCAM_ISP_PAD_NR, isp_dev->pads);
	if (ret)
		return ret;

	ret = vvcam_isp_async_notifier(isp_dev);
	if (ret)
		goto err_async_notifier;
#ifdef VVCAM_PLATFORM_REGISTER
	isp_dev->sd.fwnode = &isp_dev->fwnode;
	g_vvcam_isp_subdev[isp_dev->id] = &isp_dev->sd;
#endif
	ret = v4l2_async_register_subdev(&isp_dev->sd);
	if (ret) {
		dev_err(dev, "register subdev error\n");
		goto error_regiter_subdev;
	}

	ret = vvcam_v4l2_isp_procfs_register(isp_dev, &isp_dev->pde);
	if (ret) {
		dev_err(dev, "register procfs failed.\n");
		goto err_register_procfs;
	}

	dev_info(dev, "isp media device register ok, entity name = %s, pad num = %d major = %d, minor = %d\n",
	isp_dev->sd.entity.name, VVCAM_ISP_PAD_NR, isp_dev->sd.entity.info.dev.major, isp_dev->sd.entity.info.dev.minor);

	isp_dev->event_shm.virt_addr = (void *)__get_free_pages(GFP_KERNEL, 3);
	isp_dev->event_shm.size = PAGE_SIZE * 8;
	memset(isp_dev->event_shm.virt_addr, 0, isp_dev->event_shm.size);
	isp_dev->event_shm.phy_addr = virt_to_phys(isp_dev->event_shm.virt_addr);
	mutex_init(&isp_dev->event_shm.event_lock);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	vvcam_isp_ctrl_init(isp_dev);

	dev_info(&pdev->dev, "vvcam isp subdev driver probe success\n");

	return 0;

err_register_procfs:
	v4l2_async_unregister_subdev(&isp_dev->sd);

error_regiter_subdev:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	v4l2_async_nf_cleanup(&isp_dev->notifier);
	v4l2_async_nf_unregister(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	v4l2_async_nf_unregister(&isp_dev->notifier);
	v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
	v4l2_async_notifier_unregister(&isp_dev->notifier);
	v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
err_async_notifier:
	media_entity_cleanup(&isp_dev->sd.entity);

	return ret;
}

static int vvcam_isp_remove(struct platform_device *pdev)
{
	struct vvcam_isp_dev *isp_dev;

	isp_dev = platform_get_drvdata(pdev);

	vvcam_v4l2_isp_procfs_unregister(isp_dev->pde);
	v4l2_async_unregister_subdev(&isp_dev->sd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	v4l2_async_nf_cleanup(&isp_dev->notifier);
	v4l2_async_nf_unregister(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	v4l2_async_nf_unregister(&isp_dev->notifier);
	v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
	v4l2_async_notifier_unregister(&isp_dev->notifier);
	v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
	media_entity_cleanup(&isp_dev->sd.entity);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	free_pages((unsigned long)isp_dev->event_shm.virt_addr, 3);
	vvcam_isp_ctrl_destroy(isp_dev);
	dev_info(&pdev->dev, "vvcam isp driver remove\n");

	return 0;
}

static int __maybe_unused vvcam_isp_runtime_suspend(struct device *dev)
{
    struct vvcam_isp_dev *isp_dev = dev_get_drvdata(dev);

    win2030_tbu_power(dev, false);

	reset_control_assert(isp_dev->rstc);

    clk_bulk_disable_unprepare(isp_dev->num_clks, isp_dev->clks_bulk);

    return 0;
}

static int __maybe_unused vvcam_isp_runtime_resume(struct device *dev)
{
    struct vvcam_isp_dev *isp_dev = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	struct eswin_vi_device* es_vi_dev;
	u32 reg_val = 0;

	int ret = clk_bulk_prepare_enable(isp_dev->num_clks, isp_dev->clks_bulk);
	if (ret)
		return dev_err_probe(isp_dev->dev, ret,
				     "Failed to enable isp clocks\n");

	reset_control_deassert(isp_dev->rstc);

	win2030_tbu_power(dev, true);

	regmap_read(isp_dev->vi_topcsr_regmap, isp_dev->vi_topcsr_reg, &reg_val);
	reg_val |= (ISP0_CLK_EN | ISP1_CLK_EN);
	regmap_write(isp_dev->vi_topcsr_regmap, isp_dev->vi_topcsr_reg, reg_val);

	es_vi_dev = dev_get_drvdata(parent);
	if (!es_vi_dev) {
		return -ENODEV;
	}

	eic770x_vi_init(es_vi_dev);

	vitop_intf_cfg(es_vi_dev);

	isp_smmu_sid_cfg(dev);

	return 0;
}

static int __maybe_unused vvcam_isp_system_suspend(struct device *dev)
{
	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	vvcam_isp_runtime_suspend(dev);	

	return 0;
}

static int __maybe_unused vvcam_isp_system_resume(struct device *dev)
{
	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	vvcam_isp_runtime_resume(dev);

	return 0;
}

static const struct dev_pm_ops vvcam_isp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vvcam_isp_system_suspend, vvcam_isp_system_resume)
	SET_RUNTIME_PM_OPS(vvcam_isp_runtime_suspend, vvcam_isp_runtime_resume, NULL)
};

static const struct of_device_id vvcam_isp_of_match[] = {
	{.compatible = "verisilicon,isp-v4l2",},
	{ /* sentinel */ },
};

static struct platform_driver vvcam_isp_driver = {
	.probe	= vvcam_isp_probe,
	.remove	= vvcam_isp_remove,
	.driver = {
		.name			= VVCAM_ISP_NAME,
		.owner			= THIS_MODULE,
		.of_match_table	= vvcam_isp_of_match,
		.pm				= pm_sleep_ptr(&vvcam_isp_pm_ops),
	}
};

MODULE_DEVICE_TABLE(of, vvcam_isp_of_match);

static int __init vvcam_isp_init_module(void)
{
	int ret;
	ret = platform_driver_register(&vvcam_isp_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register isp driver\n");
		return ret;
	}

#ifdef VVCAM_PLATFORM_REGISTER
	printk(KERN_ERR "%s %d\n", __func__, __LINE__);
	ret = vvcam_isp_platform_device_register();
	if (ret) {
		platform_driver_unregister(&vvcam_isp_driver);
		printk(KERN_ERR "Failed to register vvcam isp platform devices\n");
		return ret;
	}
#endif

	return ret;
}

static void __exit vvcam_isp_exit_module(void)
{
	platform_driver_unregister(&vvcam_isp_driver);
#ifdef VVCAM_PLATFORM_REGISTER
	vvcam_isp_platform_device_unregister();
#endif
}

late_initcall(vvcam_isp_init_module);
module_exit(vvcam_isp_exit_module);

MODULE_DESCRIPTION("Verisilicon isp v4l2 driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
