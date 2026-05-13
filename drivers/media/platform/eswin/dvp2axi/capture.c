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

#include "dev.h"
#include "dvp2axi.h"
#include "dvp2axi_vb2.h"
#include "dvp2axi_fmt.h"

#define OF_CAMERA_HDR_MODE		"eswin,camera-hdr-mode"

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

#define	DVP2AXI_OUTSTANDING_SIZE 16

/*
 * Round up height when allocate memory so that EIC770X encoder can
 * use DMA buffer directly, though this may waste a bit of memory.
 */
#define MEMORY_ALIGN_ROUND_UP_HEIGHT 16

static inline void DVP2AXI_HalWriteReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address, u32 data) {
    writel(data, dvp2axi_hw->base_addr + address);
}

static inline u32 DVP2AXI_HalReadReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address) {
    return readl(dvp2axi_hw->base_addr + address);
}

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
		v4l2_dbg(4, es_dvp2axi_debug, stream->dvp2axidev->v4l2_dev,
			 "input mbus_code 0x%x, can't transform to %c%c%c%c\n",
			 input_fmt->mbus_code, output_fmt->fourcc & 0xff,
			 (output_fmt->fourcc >> 8) & 0xff,
			 (output_fmt->fourcc >> 16) & 0xff,
			 (output_fmt->fourcc >> 24) & 0xff);
	return ret;
}

static struct v4l2_subdev *get_remote_sensor(struct es_dvp2axi_stream *stream,
					     u16 *index)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct v4l2_subdev *sub = NULL;
	struct v4l2_device *v4l2_dev = stream->dvp2axidev->v4l2_dev;

	local = &stream->vnode.vdev.entity.pads[0];
	if (!local) {
		v4l2_err(v4l2_dev, "%s: video pad[0] is null\n", __func__);
		return NULL;
	}

	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev, "%s: remote pad is null\n", __func__);
		return NULL;
	}

	if (index)
		*index = remote->index;

	sensor_me = remote->entity;
	sub = media_entity_to_v4l2_subdev(sensor_me);
	return sub;
}

static void get_remote_sensor_sd(struct es_dvp2axi_stream *stream,
				       struct v4l2_subdev **sensor_sd)
{
	struct media_pad *local = NULL;
	struct media_pad *remote = NULL;
	struct media_entity *entity = &stream->vnode.vdev.entity;

	local = &entity->pads[0];

	while (1) {
		remote = media_pad_remote_pad_first(local);
		if (!remote) {
			dev_warn(stream->dvp2axidev->dev, "No remote pad found\n");
			break;
		}

		entity = remote->entity;

		if (entity->function == MEDIA_ENT_F_CAM_SENSOR) {
			*sensor_sd = media_entity_to_v4l2_subdev(entity);
			dev_dbg(stream->dvp2axidev->dev, "Found sensor device: %s\n", (*sensor_sd)->name);
			break;
		}

		local = &entity->pads[0];
		if (!(local->flags & MEDIA_PAD_FL_SINK)) {
			dev_dbg(stream->dvp2axidev->dev, "No SINK pad found in entity %s\n", entity->name);
			break;
		}
	}
}

static struct es_dvp2axi_sensor_info *sd_to_sensor(struct es_dvp2axi_device *dev,
					      struct v4l2_subdev *sd)
{
	u32 i;

	for (i = 0; i < dev->num_sensors; ++i)
		if (dev->sensors[i].sd == sd)
			return &dev->sensors[i];

	return NULL;
}

const struct dvp2axi_input_fmt *
es_dvp2axi_get_input_fmt(struct es_dvp2axi_device *dev, struct v4l2_rect *rect,
		    u32 pad_id, struct csi_channel_info *csi_info)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *sd = dev->terminal_sensor.sd;
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

	for (i = 0; i < ARRAY_SIZE(out_fmts); i++) {
		fmt = &out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

/***************************** stream operations ******************************/
static int es_dvp2axi_assign_new_buffer_oneframe(struct es_dvp2axi_stream *stream,
					    enum es_dvp2axi_yuvaddr_state stat)
{
	struct es_dvp2axi_device *dvp2axi_dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct es_dvp2axi_buffer *buffer = NULL;
	// unsigned long flags;
	int ret = 0;
	int hdr_id;
	u32 val = 0;
	u32 first_offset, last_offset, mask;
	if(stream->stopping || stream->status == ES_DVP2AXI_STREAM_STOPING || stream->status == ES_DVP2AXI_STREAM_DONE){
		stream->curr_buf = NULL;
		stream->next_buf = NULL;
		stream->last_buf = NULL;
		if(dummy_buf->vaddr) {
			if(stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL9_CSR+stream->id * 0xc, dummy_buf->dma_addr);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, dummy_buf->dma_addr);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, dummy_buf->dma_addr);
			}
			dev_dbg(dvp2axi_dev->dev, "stream%d use dummy buffer\n", stream->id);
		}
		return 0;
	}
	// spin_lock_irqsave(&stream->vbq_lock, flags);
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
			DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL9_CSR + stream->id * 0xc, stream->curr_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);
		} else {
			if (dummy_buf->vaddr) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL9_CSR + stream->id * 0xc, dummy_buf->dma_addr);
			}
		}

		if (dvp2axi_dev->hdr.hdr_mode == HDR_X2 || dvp2axi_dev->hdr.hdr_mode == HDR_X3) {
			const int hdr_frames = (dvp2axi_dev->hdr.hdr_mode == HDR_X2) ? 2 : 3;
			const u32 reg_offset = stream->id * 0xc;

			dev_dbg(dvp2axi_dev->dev, "HDR_MODE: %u", dvp2axi_dev->hdr.hdr_mode);

			hdr_id = DVP2AXI_HalReadReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL36_CSR);
			first_offset = VI_DVP2AXI_DVP0_FIRST_ID_OFFSET + stream->id * 2;
			last_offset = VI_DVP2AXI_DVP0_LAST_ID_OFFSET + stream->id * 2;
			mask = ~(0x3 << first_offset) & ~(0x3 << last_offset);
			val = (0x0 << first_offset) | ((hdr_frames - 1) << last_offset);

			hdr_id = (hdr_id & mask) | val;
			DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL36_CSR, hdr_id);

			if (!stream->next_buf && !list_empty(&stream->buf_head)) {
				stream->next_buf = list_first_entry(&stream->buf_head, 
					struct es_dvp2axi_buffer, queue);
				list_del(&stream->next_buf->queue);
			}

			u32 addr = (stream->next_buf) ?
				stream->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y] :
				dummy_buf->dma_addr;

			DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, 
				VI_DVP2AXI_CTRL10_CSR + reg_offset, addr);

			if (dvp2axi_dev->hdr.hdr_mode == HDR_X3) {
				if (!stream->last_buf && !list_empty(&stream->buf_head)) {
					stream->last_buf = list_first_entry(&stream->buf_head, 
						struct es_dvp2axi_buffer, queue);
					list_del(&stream->last_buf->queue);
				}

				addr = (stream->last_buf) ?
					stream->last_buf->buff_addr[ES_DVP2AXI_PLANE_Y] : 
					dummy_buf->dma_addr;

				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev,
					VI_DVP2AXI_CTRL11_CSR + reg_offset, addr);
			}
		}
	}  else if (stat == ES_DVP2AXI_YUV_ADDR_STATE_UPDATE) {
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
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL9_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			} else if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
				DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
			}
			dev_dbg(dvp2axi_dev->dev, "stream%d, es_dvp2axi_assign_new_buffer_oneframe: assign buffer 0x%x to frame0\n", stream->id,  buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
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
				if(stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
					DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL9_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				} else if(stream->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
					DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL10_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				} else if(stream->frame_phase == DVP2AXI_CSI_FRAME2_READY) {
					DVP2AXI_HalWriteReg(dvp2axi_dev->hw_dev, VI_DVP2AXI_CTRL11_CSR+stream->id * 0xc, dummy_buf->dma_addr);
				}
				dev_dbg(dvp2axi_dev->dev, "stream%d use dummy buffer\n", stream->id);
			}
			dev_dbg(dvp2axi_dev->dev, "stream%d es_dvp2axi_assign_new_buffer_oneframe: no buffer in queue, use dummy buffer, add 0x%llx\n", stream->id, dummy_buf->dma_addr);
			dvp2axi_dev->irq_stats.not_active_buf_cnt[stream->id]++;
		}
	}
	// spin_unlock_irqrestore(&stream->vbq_lock, flags);
	return ret;
}

static int es_dvp2axi_csi_stream_start(struct es_dvp2axi_stream *stream,
				  unsigned int mode)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct csi_channel_info *channel;

	if (stream->state < ES_DVP2AXI_STATE_STREAMING) {
		stream->frame_idx = 0;
		stream->frame_phase = DVP2AXI_CSI_FRAME_UNREADY;
	}

	channel = &dev->channels[stream->id];
	channel->id = stream->id;

	if (stream->state != ES_DVP2AXI_STATE_STREAMING)
		stream->state = ES_DVP2AXI_STATE_STREAMING;

	return 0;
}

static int es_dvp2axi_queue_setup(struct vb2_queue *queue, unsigned int *num_buffers,
			     unsigned int *num_planes, unsigned int sizes[],
			     struct device *alloc_ctxs[])
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	const struct v4l2_pix_format*pix = NULL;
	const struct dvp2axi_output_fmt *dvp2axi_fmt;
	const struct dvp2axi_input_fmt *in_fmt;
	u32 height;

	pix = &stream->pix;
	dvp2axi_fmt = stream->dvp2axi_fmt_out;
	in_fmt = stream->dvp2axi_fmt_in;

	*num_planes = dvp2axi_fmt->mplanes;

	if (stream->crop_enable)
		height = stream->crop[CROP_SRC_ACT].height;
	else
		height = pix->height;

	int h = round_up(height, MEMORY_ALIGN_ROUND_UP_HEIGHT);
	sizes[0] = pix->sizeimage / height * h;

	stream->total_buf_num = *num_buffers;
	v4l2_dbg(1, es_dvp2axi_debug, dev->v4l2_dev,
		 "%s count %d, size %d, \n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
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
	const struct dvp2axi_output_fmt *fmt = stream->dvp2axi_fmt_out;
	struct es_dvp2axi_hw *hw_dev = stream->dvp2axidev->hw_dev;
	unsigned long flags;
	int i;

	memset(dvp2axibuf->buff_addr, 0, sizeof(dvp2axibuf->buff_addr));
	/* If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < fmt->mplanes; i++) {
		if (hw_dev->is_dma_sg_ops) {
			struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, i);
			dvp2axibuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			dvp2axibuf->buff_addr[i] =
				vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}

	dev_dbg(stream->dvp2axidev->dev, "stream[%d] dvp2axibuf->buff_addr[0] = 0x%x \n", stream->id ,dvp2axibuf->buff_addr[0]);
	spin_lock_irqsave(&hw_dev->stream_lock, flags);
	// if(!stream->stopping) {
	if(stream->status != ES_DVP2AXI_STREAM_STOPING){
		list_add_tail(&dvp2axibuf->queue, &stream->buf_head);
	} else {
		struct es_dvp2axi_buffer *iter = NULL;
		bool found = false;
		if (list_empty(&stream->buf_head)) {
			dev_dbg(stream->dvp2axidev->dev, "stream[%d] list has deinit \n", stream->id);
		} else {
			list_for_each_entry(iter, &stream->buf_head, queue) {
				if (iter == dvp2axibuf) {
					found = true;
					break;
				}
			}
			if(!found) {
				list_add_tail(&dvp2axibuf->queue, &stream->buf_head);
			}
		}
	}
	spin_unlock_irqrestore(&hw_dev->stream_lock, flags);
	atomic_inc(&stream->buf_cnt);
}
static int es_dvp2axi_create_dummy_buf(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_hw *hw = dev->hw_dev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct es_dvp2axi_device *tmp_dev = NULL;
	struct v4l2_subdev_frame_interval_enum fie;
	struct v4l2_subdev_format fmt;
	struct v4l2_device *v4l2_dev = stream->dvp2axidev->v4l2_dev;
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
						size = (ALIGN(fie.width, 256)) * fie.height *
						       2;
					v4l2_dbg(
						1, es_dvp2axi_debug, v4l2_dev,
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
				size = (ALIGN(fmt.format.width, 256)) * fmt.format.height * 2;
			if (size > max_size)
				max_size = size;
		}
	}

	dummy_buf->size = max_size;

	dummy_buf->is_need_vaddr = true;
	dummy_buf->is_need_dbuf = true;
	ret = es_dvp2axi_alloc_buffer(dev, dummy_buf);
	if (ret) {
		v4l2_err(v4l2_dev,
			 "Failed to allocate the memory for dummy buffer\n");
		return -ENOMEM;
	}

	v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev, "Allocate dummy buffer, size: 0x%08x\n",
		  dummy_buf->size);
	return ret;
}

static void es_dvp2axi_destroy_dummy_buf(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct es_dvp2axi_dummy_buffer *dummy_buf = &stream->dummy_buf;

	if (dummy_buf->vaddr)
		es_dvp2axi_free_buffer(dev, dummy_buf);
	dummy_buf->dma_addr = 0;
	dummy_buf->vaddr = NULL;
}

void es_dvp2axi_do_stop_stream(struct es_dvp2axi_stream *stream,
			  enum es_dvp2axi_stream_mode mode)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_device *v4l2_dev = dev->v4l2_dev;
	int ret;
	struct es_dvp2axi_hw *hw_dev = dev->hw_dev;
	bool can_reset = true;
	int i;
	unsigned long flags;
	struct es_dvp2axi_buffer *buf = NULL;

	v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
		  "stream[%d] start stopping, total mode 0x%x, cur 0x%x\n",
		  stream->id, stream->cur_stream_mode, mode);

	if (mode == stream->cur_stream_mode) {
		ret = dev->pipe.close(&dev->pipe);
		if (ret < 0){
			v4l2_err(v4l2_dev, "pipeline close failed error:%d\n",
				 ret);
		}
		for (i = 0; i < hw_dev->dev_num; i++) {
			if (atomic_read(&hw_dev->dvp2axi_dev[i]->pipe.stream_cnt) !=
			    0) {
				can_reset = false;
				break;
			}
		}
	}

	if (mode == stream->cur_stream_mode) {
		stream->stopping = true;
		stream->status = ES_DVP2AXI_STREAM_STOPING;
		ret = wait_event_timeout(stream->wq_stopped,
					 stream->state != ES_DVP2AXI_STATE_STREAMING,
					 msecs_to_jiffies(1000));
		if (!ret) {
			v4l2_warn(v4l2_dev, "stream%d is not stop during 1s \n", stream->id);
		}
		ret = dev->pipe.set_stream(&dev->pipe, false);
		if (ret < 0)
			v4l2_err(v4l2_dev,
				 "pipeline stream-off failed error:%d\n", ret);
	}

	if ((mode & ES_DVP2AXI_STREAM_MODE_CAPTURE) == ES_DVP2AXI_STREAM_MODE_CAPTURE) {
		/* release buffers */
		// spin_lock_irqsave(&stream->vbq_lock, flags);
		spin_lock_irqsave(&hw_dev->stream_lock, flags);
		struct es_dvp2axi_buffer *iter = NULL;
		bool found = false;
		if (stream->curr_buf) {
			list_for_each_entry(iter, &stream->buf_head, queue) {
				if (iter == stream->curr_buf) {
					found = true;
					break;
				}
			}
			if(!found) {
				list_add_tail(&stream->curr_buf->queue, &stream->buf_head);
			}
		}
		found = false;
		if (stream->next_buf && stream->next_buf != stream->curr_buf) {
			list_for_each_entry(iter, &stream->buf_head, queue) {
				if (iter == stream->next_buf) {
					found = true;
					break;
				}
			}
			if(!found) {
				list_add_tail(&stream->next_buf->queue, &stream->buf_head);
			}
		}
		found = false;
		if (stream->last_buf && stream->last_buf != stream->curr_buf && stream->last_buf != stream->next_buf) {
			list_for_each_entry(iter, &stream->buf_head, queue) {
				if (iter == stream->last_buf) {
					found = true;
					break;
				}
			}
			if(!found) {
				list_add_tail(&stream->last_buf->queue, &stream->buf_head);
			}
		}

		stream->curr_buf = NULL;
		stream->next_buf = NULL;
		stream->last_buf = NULL;

		list_for_each_entry(buf, &stream->buf_head, queue) {
			v4l2_dbg(3, es_dvp2axi_debug, v4l2_dev,
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
		spin_unlock_irqrestore(&hw_dev->stream_lock, flags);
		stream->total_buf_num = 0;
		atomic_set(&stream->buf_cnt, 0);
	}

	if (mode == ES_DVP2AXI_STREAM_MODE_CAPTURE)
		tasklet_disable(&stream->vb_done_tasklet);

	stream->cur_stream_mode &= ~mode;

	// INIT_LIST_HEAD(&stream->vb_done_list);
	v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev, "stream[%d] stopping subdev finished\n", stream->id);
}

static void es_dvp2axi_stop_streaming(struct vb2_queue *queue)
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_hw *dvp2axi_hw = stream->dvp2axidev->hw_dev;
	uint32_t csr0;

	mutex_lock(&dvp2axi_hw->dev_multi_chn_lock);
	es_dvp2axi_do_stop_stream(stream, ES_DVP2AXI_STREAM_MODE_CAPTURE);
	msleep(30);
	csr0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
	csr0 &= ~(1 << stream->id); // disable stream channel
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, csr0);

	dvp2axi_hw_irq_mask(dvp2axi_hw, stream->id, 1);
	if((csr0 & 0x3f) == 0)
		dvp2axi_hw_irq_axi(dvp2axi_hw, 1);

	mutex_unlock(&dvp2axi_hw->dev_multi_chn_lock);
	msleep(50);

	es_dvp2axi_destroy_dummy_buf(stream);
	stream->status = ES_DVP2AXI_STREAM_DONE;
	stream->stopping = false;
	dev_dbg(dvp2axi_hw->dev, "stream[%d] stopped, lost frame %lld \n", stream->id, stream->dvp2axidev->irq_stats.not_active_buf_cnt[stream->id]++);
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
			(u8)DVP2AXI_RAW_STORED_BIT_WIDTH);
	  		cal = DVP2AXI_RAW_STORED_BIT_WIDTH;
			for (i = 1; i < 5; i++) {
				if (i * cal >= bpp) {
					bpp = i * cal;
					break;
				}
			}
			break;
		default:
			v4l2_err(stream->dvp2axidev->v4l2_dev,
				 "fourcc: %d is not supported!\n", fmt->fourcc);
			break;
		}
	}

	return bpp;
}

static void es_dvp2axi_sync_crop_info(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_subdev_selection input_sel;
	int ret;
	if (dev->terminal_sensor.sd) {
		input_sel.target = V4L2_SEL_TGT_CROP;
		input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		input_sel.pad = 0;
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, pad,
				       get_selection, NULL, &input_sel);
		if (!ret) {
			stream->crop[CROP_SRC_SENSOR] = input_sel.r;
			stream->crop_enable = false;
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
	struct v4l2_device *v4l2_dev = dev->v4l2_dev;
	struct v4l2_rect input, *crop;

	if (dev->terminal_sensor.sd) {
		stream->dvp2axi_fmt_in = es_dvp2axi_get_input_fmt(
			dev, &input, stream->id, &dev->channels[stream->id]);
		if (!stream->dvp2axi_fmt_in) {
			v4l2_err(v4l2_dev, "Input fmt is invalid\n");
			return -EINVAL;
		}
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

	return 0;
}

int es_dvp2axi_update_sensor_info(struct es_dvp2axi_stream *stream)
{
	struct es_dvp2axi_sensor_info *sensor, *terminal_sensor;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_device* v4l2_dev = stream->dvp2axidev->v4l2_dev;
	int ret = 0;

	sensor_sd = get_remote_sensor(stream, NULL);
	if (!sensor_sd) {
		v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
			 "%s: stream[%d] get remote sensor_sd failed!\n",
			 __func__, stream->id);
		return -ENODEV;
	}

	sensor = sd_to_sensor(stream->dvp2axidev, sensor_sd);
	if (!sensor) {
		v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
			 "%s: stream[%d] get remote sensor failed!\n", __func__,
			 stream->id);
		return -ENODEV;
	}
	ret = v4l2_subdev_call(sensor->sd, pad, get_mbus_config, 0,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD) {
		v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
			 "%s: get remote %s mbus failed!\n", __func__,
			 sensor->sd->name);
		return ret;
	}

	stream->dvp2axidev->active_sensor = sensor;

	terminal_sensor = &stream->dvp2axidev->terminal_sensor;
	get_remote_sensor_sd(stream, &terminal_sensor->sd);
	if (terminal_sensor->sd) {
		ret = v4l2_subdev_call(terminal_sensor->sd, pad,
				       get_mbus_config, 0,
				       &terminal_sensor->mbus);
		if (ret && ret != -ENOIOCTLCMD) {
			v4l2_err(v4l2_dev,
				 "%s: get terminal %s mbus failed!\n", __func__,
				 terminal_sensor->sd->name);
			return ret;
		}
		ret = v4l2_subdev_call(terminal_sensor->sd, video,
				       g_frame_interval, &terminal_sensor->fi);
		if (ret) {
			v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
				"%s: get terminal %s g_frame_interval is not implemented!\n",
				__func__, terminal_sensor->sd->name);
			// return ret;
			ret = 0;
		} 
		terminal_sensor->fi.interval.numerator = 1;
		terminal_sensor->fi.interval.denominator = 30;

		terminal_sensor->dsi_input_en= 1;
	} else {
		v4l2_err(v4l2_dev,
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

int es_dvp2axi_do_start_stream(struct es_dvp2axi_stream *stream,
			  enum es_dvp2axi_stream_mode mode)
{
	struct es_dvp2axi_vdev_node *node = &stream->vnode;
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	struct v4l2_device *v4l2_dev = dev->v4l2_dev;
	struct es_dvp2axi_hw *hw_dev = dev->hw_dev;
	struct es_dvp2axi_sensor_info *sensor_info = dev->active_sensor;
	struct es_dvp2axi_sensor_info *terminal_sensor = NULL;
	int esmodule_stream_seq = ESMODULE_START_STREAM_DEFAULT;
	int ret;
	u32 skip_frame = 0;
	struct v4l2_subdev_format csi_fmt;

	v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev, "sensor info: %s, mbus type: %d, lanes: %d, width: %d, height: %d, code: 0x%x\n",
		  sensor_info->sd ? sensor_info->sd->name : "NULL",
		  sensor_info->mbus.type, sensor_info->lanes, stream->pix.width, stream->pix.height,
		  stream->dvp2axi_fmt_in->mbus_code);

	csi_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	csi_fmt.pad = 0;
	csi_fmt.format.width = stream->pix.width;
	csi_fmt.format.height = stream->pix.height;
	csi_fmt.format.code = stream->dvp2axi_fmt_in->mbus_code;
	ret = v4l2_subdev_call(sensor_info->sd, pad, set_fmt, NULL, &csi_fmt);
	if (ret) {
		v4l2_err(v4l2_dev, "set csi fmt failed %d\n", ret);
		goto destroy_buf;
	}

	if ((stream->cur_stream_mode & ES_DVP2AXI_STREAM_MODE_CAPTURE) == mode) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}

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
		ret = v4l2_subdev_call(terminal_sensor->sd, video,
				       g_frame_interval, &terminal_sensor->fi);
		if (ret)
			terminal_sensor->fi.interval =
				(struct v4l2_fract){ 1, 30 };

		es_dvp2axi_sync_crop_info(stream);
	}

	ret = es_dvp2axi_sanity_check_fmt(stream, NULL);
	if (ret < 0)
		goto destroy_buf;

	mutex_lock(&hw_dev->dev_lock);
	if (dev->active_sensor && dev->is_use_dummybuf &&
	    (!stream->dummy_buf.vaddr) &&
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
		 dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)) {
		ret = es_dvp2axi_csi_stream_start(stream, mode);
	}
	if (ret < 0)
		goto destroy_buf;

	if (stream->cur_stream_mode == ES_DVP2AXI_STREAM_MODE_NONE) {
		if (sensor_info->mbus.type != V4L2_MBUS_PARALLEL &&
		    esmodule_stream_seq != ESMODULE_START_STREAM_FRONT) {
			ret = dev->pipe.set_stream(&dev->pipe, true);
			if (ret < 0)
				goto stop_stream;
		}
	}
	stream->cur_stream_mode |= mode;
	goto out;

stop_stream:
	// es_dvp2axi_stream_stop(stream);
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
	return ret;
}

void es_dvp2axi_dump_reg(struct es_dvp2axi_hw *hw, u32 id)
{
	for(int offset = 0; offset < 0xb8; offset += 4) {
		uint32_t reg_val = DVP2AXI_HalReadReg(hw, offset);
		dev_dbg(hw->dev, "stream_id%d: DVP2AXI_REG[0x%02x] = 0x%08x\n", id, offset, reg_val);
	}
}

static int es_dvp2axi_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct es_dvp2axi_stream *stream = queue->drv_priv;
	struct es_dvp2axi_hw *dvp2axi_hw = stream->dvp2axidev->hw_dev;
	uint32_t bpl = 0;
	uint32_t dvpx_bpl = 0;
	uint32_t dvp2axi_ctrl33;
	uint32_t dvp2axi_bpp;
	uint32_t csr0, csr1, csr2;
	int ret = 0;

	stream->frame_phase = DVP2AXI_CSI_FRAME_UNREADY;
	dvp2axi_bpp = stream->bpp;
	stream->frame_idx = 0;

	stream->status = ES_DVP2AXI_STREAM_STARTING;
	mutex_lock(&dvp2axi_hw->dev_multi_chn_lock);
	es_dvp2axi_assign_new_buffer_oneframe(stream , ES_DVP2AXI_YUV_ADDR_STATE_INIT);

	// stream->stopping = false;
	if (stream->dvp2axi_fmt_out->csi_fmt_val == CSI_WRDDR_TYPE_RGB888) {
		dvp2axi_bpp = dvp2axi_bpp * 3;
	}

	if(stream->crop_enable) {
		if (stream->dvp2axi_fmt_out->csi_fmt_val == CSI_WRDDR_TYPE_RGB888) {
			bpl = ALIGN(stream->crop[CROP_SRC_ACT].width * dvp2axi_bpp / 8, 16);
		} else {
			bpl = ALIGN(stream->crop[CROP_SRC_ACT].width * dvp2axi_bpp / 8, 256);
		}
		dvpx_bpl |= bpl;
	} else {
		if (stream->dvp2axi_fmt_out->csi_fmt_val == CSI_WRDDR_TYPE_RGB888) {
			bpl = ALIGN(stream->pix.width * dvp2axi_bpp / 8, 16);
		} else {
			bpl = ALIGN(stream->pix.width * dvp2axi_bpp / 8, 256);
		}
		dvpx_bpl |= bpl;
	}

	//set axi burstlen
	csr0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
	DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, (csr0 & (~VI_DVP2AXI_CTRL0_AXI_BURST_LEN_MASK)) | (es_dvp2axi_axi_burst_len << 7));

	//set qos and ots
	dev_dbg(dvp2axi_hw->dev, "outs 0x%x, wqos 0x%x \n", es_dvp2axi_ots, es_dvp2axi_wqos);
	csr2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, ((csr2 & 0xfffff) | (es_dvp2axi_ots << 24) | (es_dvp2axi_wqos << 20)));

	switch(stream->id) {
	case 0:
		csr1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR, (csr1 & (~VI_DVP2AXI_CTRL1_DVP0_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp << 20));
		break;
	case 1:
		csr1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR, (csr1 & (~VI_DVP2AXI_CTRL1_DVP1_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp << 25));
		break;
	case 2:
		csr2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, (csr2 & (~VI_DVP2AXI_CTRL2_DVP2_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp));
		break;
	case 3:
		csr2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, (csr2 & (~VI_DVP2AXI_CTRL2_DVP3_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp << 5));
		break;
	case 4:
		csr2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, (csr2 & (~VI_DVP2AXI_CTRL2_DVP4_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp << 10));
		break;
	case 5:
		csr2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, (csr2 & (~VI_DVP2AXI_CTRL2_DVP5_PIXEL_WIDTH_MASK)) | (dvp2axi_bpp << 15));
		break;
	default:
		dev_warn(dvp2axi_hw->dev, "unsupported stream id %d \n", stream->id);
		break;
	}

	//rgb888 and raw 8bit need to shift right
	if(stream->dvp2axi_fmt_out->fmt_type == CSI_WRDDR_TYPE_RGB888 || stream->dvp2axi_fmt_out->fmt_type == CSI_WRDDR_TYPE_RAW8){
		csr0 = DVP2AXI_HalReadReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR);
		DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, csr0 | (1 << (stream->id + VI_DVP2AXI_CTRL0_DVP_DATA_SHIFT_BIT)));
	}else {
		csr0 = DVP2AXI_HalReadReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR);
		DVP2AXI_HalWriteReg(stream->dvp2axidev->hw_dev, VI_DVP2AXI_CTRL0_CSR, csr0 & (~(1 << (stream->id + VI_DVP2AXI_CTRL0_DVP_DATA_SHIFT_BIT))));
	}

	if(stream->dvp2axi_fmt_out->fmt_type == CSI_WRDDR_TYPE_RGB888)
		DVP2AXI_HalWriteReg( dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR + stream->id * 0x4, (stream->pix.width * 3) | (stream->pix.height << 16));
	else {
		DVP2AXI_HalWriteReg( dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR + stream->id  * 0x4, (stream->pix.width) | (stream->pix.height << 16));
	}

	if(stream->id % 2 == 0) { // 0/2/4 stream
		uint32_t reg_offset = stream->id / 2 * 0x4;
		dvp2axi_ctrl33 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR + reg_offset);
		dvp2axi_ctrl33 =(dvp2axi_ctrl33 & 0xffff0000) | dvpx_bpl;
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR + reg_offset, dvp2axi_ctrl33);
	} else { // 1/3/5 stream
		uint32_t reg_offset = ((stream->id - 1) / 2) * 0x4;
		dvp2axi_ctrl33 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR + reg_offset);
		dvp2axi_ctrl33 |= (dvp2axi_ctrl33 & 0xffff) | (dvpx_bpl << 16);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR + reg_offset, dvp2axi_ctrl33);
	}

	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR, (0x1 << (stream->id +2)));
	if(stream->id < 3) {
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, (0x3 << stream->id) | (0x3 << (stream->id +9)));
	} else {
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, (0x3 << (stream->id - 3)) | (0x3 << (stream->id +6)));
	}

	dvp2axi_hw_irq_mask(dvp2axi_hw, stream->id, 0);
	dvp2axi_hw_irq_axi(stream->dvp2axidev->hw_dev, 0);

	csr0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR);
	if((csr0 & 0x3f) == 0)
		dvp2axi_hw_soft_reset(stream->dvp2axidev->hw_dev);
	csr0 = csr0 | (1 << stream->id);
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, csr0);

	ret = es_dvp2axi_do_start_stream(stream, ES_DVP2AXI_STREAM_MODE_CAPTURE);
	if(ret < 0) {
		dev_err(dvp2axi_hw->dev, "es_dvp2axi_do_start_stream %d failed %d\n", ret, stream->id);
		csr0 = csr0 & (~(1 << stream->id));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, csr0);
		goto unlock;
	}

	es_dvp2axi_dump_reg(dvp2axi_hw, stream->id);
unlock:
	mutex_unlock(&dvp2axi_hw->dev_multi_chn_lock);
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
	q->dev = stream->dvp2axidev->hw_dev->dev;
	q->ops = &es_dvp2axi_vb2_ops;
	q->mem_ops = hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct es_dvp2axi_buffer);
	if (stream->dvp2axidev->is_use_dummybuf)
		q->min_buffers_needed = 1;
	else
		q->min_buffers_needed = DVP2AXI_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vnode.vlock;
	q->dev = hw_dev->dev;
	q->non_coherent_mem=0;
	return vb2_queue_init(q);
}

int es_dvp2axi_set_fmt(struct es_dvp2axi_stream *stream,
		  struct v4l2_pix_format *pix, bool try)
{
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	const struct dvp2axi_output_fmt *fmt;
	const struct dvp2axi_input_fmt *dvp2axi_fmt_in = NULL;
	struct v4l2_rect input_rect;
	unsigned int imagesize = 0;
	u32 plane_index = 0;
	struct esmodule_hdr_cfg hdr_cfg;
	struct csi_channel_info *channel_info = &dev->channels[stream->id];
	int width, height, bpl, size, bpp;
	int ret;

	fmt = es_dvp2axi_find_output_fmt(stream, pix->pixelformat);
	if (!fmt)
		fmt = &out_fmts[0];

	input_rect.width = pix->width;
	input_rect.height = pix->height;
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

	if (dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(dev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_HDR_CFG, &hdr_cfg);
		if (!ret) {
			dev->hdr.hdr_mode = hdr_cfg.hdr_mode;
			dev_dbg(dev->dev, "dvp2axi hdr_mode: %d", dev->hdr.hdr_mode);
		} else
			dev_dbg(dev->dev, "sensor not implement ESMODULE_GET_HDR_CFG\n");

		dev->terminal_sensor.raw_rect = input_rect;
	}

	pix->width =
		clamp_t(u32, pix->width, DVP2AXI_MIN_WIDTH, input_rect.width);
	pix->height =
		clamp_t(u32, pix->height, DVP2AXI_MIN_HEIGHT, input_rect.height);
	pix->field = V4L2_FIELD_NONE;
	pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	es_dvp2axi_sync_crop_info(stream);

	if (dvp2axi_fmt_in && (dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_SPD_2X8 ||
			   dvp2axi_fmt_in->mbus_code == MEDIA_BUS_FMT_EBD_1X8))
		stream->crop_enable = false;

	if (stream->crop_enable) {
		width = stream->crop[CROP_SRC_ACT].width;
		height = stream->crop[CROP_SRC_ACT].height;
	} else {
		width = pix->width;
		height = pix->height;
	}

	if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW &&
	    (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
	     dev->active_sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ||
	     dev->active_sensor->mbus.type == V4L2_MBUS_CCP2) &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
	    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565) {
		if(fmt->raw_bpp >= 10) {
			bpl = ALIGN(width * ALIGN(fmt->raw_bpp, 16) / 8, 256);
			stream->bpp = 16;
			stream->bpl = bpl;
		} else {
			bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
			stream->bpp = fmt->raw_bpp;
			stream->bpl = bpl;
		}
	} else {
		if (fmt->fmt_type == DVP2AXI_FMT_TYPE_RAW &&
		    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB888 &&
		    fmt->csi_fmt_val != CSI_WRDDR_TYPE_RGB565 &&
		    dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
			bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
			stream->bpl = bpl;
		} else {
			bpp = es_dvp2axi_align_bits_per_pixel(stream, fmt, plane_index);
			if(fmt->fmt_type == DVP2AXI_FMT_TYPE_YUV){
				bpl = width * bpp / DVP2AXI_YUV_STORED_BIT_WIDTH;
				stream->bpl = bpl;
			}
			if(fmt->csi_fmt_val == CSI_WRDDR_TYPE_RGB888){
				bpl = ALIGN(width * 3 * bpp / 8, 16);
				stream->bpl = bpl;
			}
		}
	}

	size = bpl * height;
	imagesize += size;
	pix->sizeimage = imagesize;
	if (!try) {
		stream->dvp2axi_fmt_out = fmt;
		stream->pix = *pix;

		v4l2_dbg(1, es_dvp2axi_debug, stream->dvp2axidev->v4l2_dev,
			 "%s: req(%d, %d) out(%d, %d)\n", __func__, pix->width,
			 pix->height, stream->pix.width, stream->pix.height);
	}
	return 0;
}

void es_dvp2axi_stream_init(struct es_dvp2axi_device *dev, u32 id)
{
	struct es_dvp2axi_stream *stream = &dev->stream[0];
	struct v4l2_pix_format pix;
	memset(stream, 0, sizeof(*stream));
	memset(&pix, 0, sizeof(pix));
	stream->id = id;
	stream->dvp2axidev = dev;

	INIT_LIST_HEAD(&stream->buf_head);
	spin_lock_init(&stream->vbq_lock);
	spin_lock_init(&stream->fps_lock);
	stream->state = ES_DVP2AXI_STATE_READY;
	init_waitqueue_head(&stream->wq_stopped);

	/* Set default format */
	pix.pixelformat = V4L2_PIX_FMT_SRGGB10;
	pix.width = ES_DVP2AXI_DEFAULT_WIDTH;
	pix.height = ES_DVP2AXI_DEFAULT_HEIGHT;
	es_dvp2axi_set_fmt(stream, &pix, false);

	stream->crop_enable = false;
	stream->crop_dyn_en = false;
	stream->crop_mask = 0x0;
	stream->cur_stream_mode = 0;
	stream->buf_owner = 0;
	atomic_set(&stream->buf_cnt, 0);
}

static int es_dvp2axi_fh_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_stream *stream = to_es_dvp2axi_stream(vnode);
	struct es_dvp2axi_device *dvp2axidev = stream->dvp2axidev;
	int ret;

	ret = es_dvp2axi_attach_hw(dvp2axidev);
	if (ret)
		return ret;

	/* Make sure active sensor is valid before .set_fmt() */
	ret = es_dvp2axi_update_sensor_info(stream);
	if (ret < 0) {
		v4l2_dbg(1, es_dvp2axi_debug, vdev, "update sensor info failed %d\n", ret);
		return ret;
	}

	ret = pm_runtime_resume_and_get(dvp2axidev->hw_dev->dev);
	if (ret < 0) {
		v4l2_err(vdev, "Failed to get runtime pm, %d\n", ret);
		return ret;
	}

	ret = v4l2_fh_open(filp);

	if (ret < 0)
		vb2_fop_release(filp);

	return ret;
}

static int es_dvp2axi_fh_release(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_stream *stream = to_es_dvp2axi_stream(vnode);
	struct es_dvp2axi_device *dvp2axidev = stream->dvp2axidev;
	int ret = 0;

	ret = vb2_fop_release(filp);
	pm_runtime_mark_last_busy(dvp2axidev->hw_dev->dev);
	pm_runtime_put_autosuspend(dvp2axidev->hw_dev->dev);
	return ret;
}

void dvp2axi_hw_irq_mask(struct es_dvp2axi_hw *dvp2axi_hw, u32 stream_id, int mask)
{
	u32 int0;
	u32 int1;
	u32 int2;

	int0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR);
	int1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR);
	int2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR);
	if(mask) {
		if(stream_id < 3) {
			int0 |= (0x7 << stream_id*3) | (0x7 << (9 + stream_id*3));
		} else {
			int1 |= (0x7 << ((stream_id - 3)*3)) | (0x7 << (9 + (stream_id - 3)*3));
		}
		int2 |= (0x1 << (stream_id + 2));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR, int0);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR, int1);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, int2);
	} else {
		if(stream_id < 3) {
			int0 &= ~((0x7 << stream_id * 3) | (0x7 << (9 + stream_id*3)));
		} else {
			int1 &= ~((0x7 << (stream_id - 3) * 3) | (0x7 << (9 + (stream_id - 3)*3)));
		}
		int2 &= ~(0x1 << (stream_id + 2));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR, int0);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR, int1);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, int2);
	}
}

void dvp2axi_hw_irq_axi(struct es_dvp2axi_hw *dvp2axi_hw, int mask)
{
	u32 int2;
	int2 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR);
	if (mask) {
		int2 |= 0x3 | (0x1 << 8);
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, int2);
	} else {
		int2 &= ~(0x3 | (0x1 << 8));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, int2);
	}
}

static const struct v4l2_file_operations es_dvp2axi_fops = {
	.open = es_dvp2axi_fh_open,
	.release = es_dvp2axi_fh_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
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
	// int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor || !sensor->sd) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	// ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	// if (ret && ret != -ENOIOCTLCMD) {
	// 	return ret;
	// } else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	// }

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


static int es_dvp2axi_try_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	int ret = 0;
	ret = es_dvp2axi_set_fmt(stream, &f->fmt.pix, true);
	return ret;
}

static int es_dvp2axi_enum_fmt_vid_cap(struct file *file, void *priv,
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

static int es_dvp2axi_s_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *dev = stream->dvp2axidev;
	int ret = 0;

	if (vb2_is_busy(&stream->vnode.buf_queue)) {
		v4l2_err(dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}
	ret = es_dvp2axi_set_fmt(stream, &f->fmt.pix, false);
	return ret;
}

static int es_dvp2axi_g_fmt_vid_cap(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	f->fmt.pix = stream->pix;
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
	struct v4l2_device* v4l2_dev = dev->v4l2_dev;
	u16 pad = 0;
	int ret = 0;

	if (!s) {
		v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev, "sel is null\n");
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
			v4l2_dbg(1, es_dvp2axi_debug, v4l2_dev,
				 "%s: pad:%d, which:%d, target:%d\n", __func__,
				 pad, sd_sel.which, sd_sel.target);
		}
	} else if (s->target == V4L2_SEL_TGT_CROP) {
		ret = es_dvp2axi_sanity_check_fmt(stream, rect);
		if (ret) {
			v4l2_err(v4l2_dev, "set crop failed\n");
			return ret;
		}

		if (stream->crop_mask & CROP_SRC_SENSOR) {
			sensor_crop = stream->crop[CROP_SRC_SENSOR];
			if (rect->left + rect->width > sensor_crop.width ||
			    rect->top + rect->height > sensor_crop.height) {
				v4l2_err(
					v4l2_dev,
					"crop size is bigger than sensor input:left:%d, top:%d, width:%d, height:%d\n",
					sensor_crop.left, sensor_crop.top,
					sensor_crop.width, sensor_crop.height);
				return -EINVAL;
			}
		} else {
			if (rect->left + rect->width > raw_rect->width ||
			    rect->top + rect->height > raw_rect->height) {
				v4l2_err(
					v4l2_dev,
					"crop size is bigger than sensor raw input:left:%d, top:%d, width:%d, height:%d\n",
					raw_rect->left, raw_rect->top,
					raw_rect->width, raw_rect->height);
				return -EINVAL;
			}
		}

		stream->crop[CROP_SRC_USR] = *rect;
		stream->crop_enable = false;
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
				v4l2_dev,
				"enable dynamic crop, S_SELECTION(%ux%u@%u:%u) target: %d\n",
				rect->width, rect->height, rect->left,
				rect->top, s->target);
		} else {
			v4l2_info(
				v4l2_dev,
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
		v4l2_dbg(1, es_dvp2axi_debug, dev->v4l2_dev, "sel is null\n");
		goto err;
	}

	if (s->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sensor_sd = get_remote_sensor(stream, &pad);

		sd_sel.pad = pad;
		sd_sel.target = s->target;
		sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;

		v4l2_dbg(1, es_dvp2axi_debug, dev->v4l2_dev,
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
			s->r.width = stream->pix.width;
			s->r.height = stream->pix.height;
		}
	} else if (s->target == V4L2_SEL_TGT_CROP) {
		if (stream->crop_mask &
		    (CROP_SRC_USR_MASK | CROP_SRC_SENSOR_MASK)) {
			s->r = stream->crop[CROP_SRC_ACT];
		} else {
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = stream->pix.width;
			s->r.height = stream->pix.height;
		}
	} else {
		goto err;
	}

	return ret;
err:
	return -EINVAL;
}

static int __maybe_unused es_dvp2axi_get_max_common_div(int a, int b)
{
	int remainder = a % b;

	while (remainder != 0) {
		a = b;
		b = remainder;
		remainder = a % b;
	}
	return b;
}

int es_dvp2axi_g_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	int ret;
	int def_vblank, cur_vblank;
	int def_hblank, cur_hblank;
	u64 pixel_rate;
	u64 frame_pixel;
	int max_common_div;
	int fps_numerator;
	int fps_denominator;
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *es_dev = stream->dvp2axidev;
	struct v4l2_subdev *sensor_sd = es_dev->terminal_sensor.sd;

	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = 0,
	};

	def_vblank =  es_dvp2axi_get_sensor_vblank_def(es_dev);
	cur_vblank = es_dvp2axi_get_sensor_vblank(es_dev);

	def_hblank = es_dvp2axi_get_sensor_hblank_def(es_dev);
	cur_hblank = es_dvp2axi_get_sensor_hblank(es_dev);

	pixel_rate = es_dvp2axi_get_sensor_pixel_rate(es_dev);
	if(pixel_rate == 0) {
		v4l2_err(es_dev->v4l2_dev, "FPS adjustment not supported\n");
		return -EOPNOTSUPP;
	}

	ret = v4l2_subdev_call(sensor_sd, pad, get_fmt, NULL, &fmt);
	if(ret) {
		v4l2_err(es_dev->v4l2_dev, "%s sensor get_fmt error, ret %d \n", __func__, ret);
		return ret;
	}

	frame_pixel = (fmt.format.width+cur_hblank) * (fmt.format.height+cur_vblank);
	max_common_div = es_dvp2axi_get_max_common_div(pixel_rate, frame_pixel);
	if (max_common_div > 1) {
		fps_numerator = (u32)(frame_pixel / max_common_div);
		fps_denominator = (u32)(pixel_rate / max_common_div);
	} else {
		fps_numerator = (u32)frame_pixel;
		fps_denominator = (u32)pixel_rate;
	}

	if (fps_denominator == 0) {
		fps_numerator = 1;
		fps_denominator = 30;  // use default 30fps
	}

	a->parm.capture.timeperframe.denominator = fps_denominator;
	a->parm.capture.timeperframe.numerator = fps_numerator;
	// a->parm.capture.readbuffers = 6;
	v4l2_dbg(2, es_dvp2axi_debug, es_dev->v4l2_dev, "timeperframe_denominator %d, timeperframe_numerator %d \n", fps_denominator, fps_numerator);

	return 0;
}

int es_dvp2axi_s_parm(struct file *file, void *fh,
			     struct v4l2_streamparm *a)
{
	int ret;
	int def_fps;
	int target_fps;
	int vts;
	int def_vblank, cur_vblank;
	int def_hblank, cur_hblank;
	u64 def_frame_pixel;
	u64 pixel_rate;
	struct es_dvp2axi_stream *stream = video_drvdata(file);
	struct es_dvp2axi_device *es_dev = stream->dvp2axidev;
	struct v4l2_subdev *sensor_sd = es_dev->terminal_sensor.sd;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = 0,
	};

	struct v4l2_control vblank_ctrl = {
		.id = V4L2_CID_VBLANK
	};

	def_vblank =  es_dvp2axi_get_sensor_vblank_def(es_dev);
	cur_vblank = es_dvp2axi_get_sensor_vblank(es_dev);

	def_hblank = es_dvp2axi_get_sensor_hblank_def(es_dev);
	cur_hblank = es_dvp2axi_get_sensor_hblank(es_dev);

	pixel_rate = es_dvp2axi_get_sensor_pixel_rate(es_dev);
	if(pixel_rate == 0) {
		v4l2_err(es_dev->v4l2_dev, "FPS adjustment not supported\n");
		return -EOPNOTSUPP;
	}

	ret = v4l2_subdev_call(sensor_sd, pad, get_fmt, NULL, &fmt);
	if (ret) {
		v4l2_err(es_dev->v4l2_dev, "%s sensor get_fmt error, ret %d \n", __func__, ret);
		return ret;
	}
	
	def_frame_pixel = (fmt.format.width+def_hblank) * (fmt.format.height+def_vblank);
	def_fps= pixel_rate / def_frame_pixel;
	def_fps += (pixel_rate - def_fps*def_frame_pixel) > 0 ? 1:0;

	target_fps = a->parm.capture.timeperframe.denominator / a->parm.capture.timeperframe.numerator;
	v4l2_dbg(2, es_dvp2axi_debug, es_dev->v4l2_dev, "%s: target_fps %d", __func__, target_fps);

	if(target_fps > def_fps) {
		dev_warn(es_dev->dev, "set fps %d must smaller than default fps %d \n", target_fps, def_fps);
		return 0;
	}

	// start cal vblank
	vts = pixel_rate / target_fps / (fmt.format.width+def_hblank);
	vblank_ctrl.value = vts - fmt.format.height;
	ret = v4l2_s_ctrl(fh, sensor_sd->ctrl_handler, &vblank_ctrl);
	if (ret) {
		v4l2_err(es_dev->v4l2_dev, "%s: call sensor set V4L2_CID_VBLANK error, ret %d \n", __func__, ret);
		return ret;
	}

	v4l2_dbg(2, es_dvp2axi_debug, es_dev->v4l2_dev, "set target fps %d done \n", target_fps);
	return 0;
}

static long es_dvp2axi_ioctl_default(struct file *file, void *fh, bool valid_prio,
				unsigned int cmd, void *arg)
{
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
	.vidioc_try_fmt_vid_cap = es_dvp2axi_try_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = es_dvp2axi_enum_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = es_dvp2axi_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = es_dvp2axi_g_fmt_vid_cap,
	.vidioc_querycap = es_dvp2axi_querycap,
	.vidioc_s_selection = es_dvp2axi_s_selection,
	.vidioc_g_selection = es_dvp2axi_g_selection,
	.vidioc_enum_frameintervals = es_dvp2axi_enum_frameintervals,
	.vidioc_enum_framesizes = es_dvp2axi_enum_framesizes,
	.vidioc_g_parm = es_dvp2axi_g_parm,
	.vidioc_s_parm = es_dvp2axi_s_parm,
	.vidioc_default = es_dvp2axi_ioctl_default,
};

void es_dvp2axi_vb_done_oneframe(struct es_dvp2axi_stream *stream,
			    struct vb2_v4l2_buffer *vb_done)
{
	/* Dequeue a filled buffer */
	vb2_set_plane_payload(&vb_done->vb2_buf, 0,
				  stream->pix.sizeimage);
	vb2_buffer_done(&vb_done->vb2_buf, VB2_BUF_STATE_DONE);
	v4l2_dbg(2, es_dvp2axi_debug, stream->dvp2axidev->v4l2_dev,
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

	spin_lock_irqsave(&stream->dvp2axidev->hw_dev->stream_lock, flags);
	list_replace_init(&stream->vb_done_list, &local_list);
	spin_unlock_irqrestore(&stream->dvp2axidev->hw_dev->stream_lock, flags);

	while (!list_empty(&local_list)) {
		buf = list_first_entry(&local_list, struct es_dvp2axi_buffer, queue);
		list_del(&buf->queue);
		es_dvp2axi_vb_done_oneframe(stream, &buf->vb);
	}
}

void es_dvp2axi_vb_done_tasklet(struct es_dvp2axi_stream *stream,
			   struct es_dvp2axi_buffer *buf)
{
	if (!stream || !buf)
		return;

	list_add_tail(&buf->queue, &stream->vb_done_list);
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
	struct v4l2_device *v4l2_dev = dev->v4l2_dev;
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
	node = vdev_to_node(vdev);
	mutex_init(&node->vlock);
	vdev->ioctl_ops = &es_dvp2axi_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &es_dvp2axi_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &node->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
	es_dvp2axi_init_vb2_queue(&node->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE);
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
		udelay(100);
	} while ((soft_rstn & VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK) != VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_DONE_MASK && count++ < 100);
    // release reset
	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL0_CSR, soft_rstn | VI_DVP2AXI_CTRL0_AXI_SOFT_RSTN_MASK);
}

void es_irq_oneframe(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev)
{
	u32 vi_dvp2axi_int0, vi_dvp2axi_int1;
	u32 frame0_done_detect = 0, frame1_done_detect = 0, frame2_done_detect = 0;
	int ret = 0;
	struct es_dvp2axi_hw	*dvp2axi_hw =  dev_get_drvdata(dev);
	struct es_dvp2axi_stream *stream = &dvp2axi_dev->stream[ES_DVP2AXI_STREAM_DVP2AXI];
	struct es_dvp2axi_buffer *active_buf = NULL;
	int es_dvp2axi_addr_state;
	int flush_intr0 = 0, flush_intr1 = 0;
	int done_intr0 = 0, done_intr1 = 0;

	vi_dvp2axi_int0 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR);
	vi_dvp2axi_int1 = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR);
	dev_dbg(dev, "stream%d: vi_dvp2axi_int0 0x%x, vi_dvp2axi_int1 0x%x \n", stream->id, vi_dvp2axi_int0, vi_dvp2axi_int1);


	if(stream->id == 0 || stream->id == 1 || stream->id == 2) {
		frame0_done_detect = vi_dvp2axi_int0 & (1 << (9 + stream->id * 3));
		frame1_done_detect = vi_dvp2axi_int0 & (1 << (10 + stream->id * 3));
		frame2_done_detect = vi_dvp2axi_int0 & (1 << (11 + stream->id * 3));
	} else {
		frame0_done_detect = vi_dvp2axi_int1 & (1 << (9 + (stream->id-3) * 3));
		frame1_done_detect = vi_dvp2axi_int1 & (1 << (10 + (stream->id-3) * 3));
		frame2_done_detect = vi_dvp2axi_int1 & (1 << (11 + (stream->id-3)* 3));
	}

	if(stream->stopping || stream->status == ES_DVP2AXI_STREAM_STOPING ||stream->status == ES_DVP2AXI_STREAM_DONE) {
		if(stream->state == ES_DVP2AXI_STATE_STREAMING) {
			stream->state = ES_DVP2AXI_STATE_READY;
		}
		goto clr_int;
	}

	if(frame0_done_detect) {
		stream->frame_idx++;
		active_buf = stream->curr_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME0_READY;
	}
	if(frame1_done_detect) {
		if(dvp2axi_dev->hdr.hdr_mode != NO_HDR) {
			goto clr_int;
		}
		stream->frame_idx++;
		active_buf = stream->next_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME1_READY;
	}
	if (frame2_done_detect) {
		if(dvp2axi_dev->hdr.hdr_mode == NO_HDR) {
			goto clr_int;
		}
		stream->frame_idx++;
		active_buf = stream->last_buf;
		stream->frame_phase = DVP2AXI_CSI_FRAME2_READY;
	}
	es_dvp2axi_addr_state = ES_DVP2AXI_YUV_ADDR_STATE_UPDATE;

	if (stream->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
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

clr_int:
	// clear stream done interrupt
	if(stream->id == 0 || stream->id == 1 || stream->id == 2) {
		if(frame0_done_detect) {
			done_intr0 |= (1 << (9 + stream->id * 3));
		}
		if(frame1_done_detect) {
			done_intr0 |= (1 << (10 + stream->id * 3));
		}
		if(frame2_done_detect) {
			done_intr0 |= (1 << (11 + stream->id * 3));
		}
		flush_intr0 = vi_dvp2axi_int0 & (0x7 << (stream->id * 3));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT0_CSR, done_intr0 | flush_intr0);
	} else {
		if(frame0_done_detect) {
			done_intr1 |= (1 << (9 + (stream->id-3) * 3));
		}
		if(frame1_done_detect) {
			done_intr1 |= (1 << (10 + (stream->id-3) * 3));
		}
		if(frame2_done_detect) {
			done_intr1 |= (1 << (11 + (stream->id-3) * 3));
		}
		flush_intr1 = vi_dvp2axi_int1 & (0x7 << ((stream->id-3) * 3));
		DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT1_CSR, flush_intr1 | done_intr1);
	}

	stream->frame_phase = DVP2AXI_CSI_FRAME_UNREADY;
}

void es_irq_err_handle(struct device *dev)
{
	u32 vi_dvp2axi_int_err;
	struct es_dvp2axi_hw *dvp2axi_hw =  dev_get_drvdata(dev);
	vi_dvp2axi_int_err = DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR);

	dev_err_ratelimited(dev, "vi_dvp2axi_int_err 0x%x\n", vi_dvp2axi_int_err);

	if((vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_FULL) || (vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_IDBUFFER_AFULL)) {
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[0]);
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[8]);
	}

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_AXI_RESP_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[1]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP0_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[2]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP1_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[3]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP2_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[4]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP3_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[5]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP4_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[6]);

	if(vi_dvp2axi_int_err & VI_DVP2AXI_INT2_DVP5_FRAME_ERROR)
		atomic_inc(&dvp2axi_hw->dvp2axi_errirq_cnts[7]);

	DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT2_CSR, vi_dvp2axi_int_err);
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

s32 es_dvp2axi_get_sensor_hblank_def(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sd->ctrl_handler;
	struct v4l2_ctrl *ctrl = NULL;

	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_HBLANK)
				return ctrl->default_value;
		}
	}

	return 0;
}

s32 es_dvp2axi_get_sensor_hblank(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sd->ctrl_handler;
	struct v4l2_ctrl *ctrl = NULL;

	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_HBLANK)
				return ctrl->default_value;
		}
	}

	return 0;
}

u64 es_dvp2axi_get_sensor_pixel_rate(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sensor_sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sensor_sd->ctrl_handler;
	struct v4l2_ext_controls ctrls = {0};
	struct v4l2_ext_control ext_ctrl = {
		.id = V4L2_CID_PIXEL_RATE,
		.size = 0,
	};
	struct v4l2_ctrl *ctrl = NULL;
	int ret;

	if (!sensor_sd) {
		dev_err(dev->dev, "No sensor subdev\n");
		return 0;
	}

	// try 64bit ctrl
	ctrls.controls = &ext_ctrl;
	ctrls.count = 1;

	ret = v4l2_g_ext_ctrls(sensor_sd->ctrl_handler, NULL, NULL, &ctrls);
	if (ret == 0) {
		v4l2_dbg(2, es_dvp2axi_debug, dev->v4l2_dev, "Got pixel rate from sensor (64bit): %llu Hz\n", 
					ext_ctrl.value64);
		return ext_ctrl.value64;
	}

		// try 32bit ctrl
	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_PIXEL_RATE) {
				v4l2_dbg(2, es_dvp2axi_debug, dev->v4l2_dev, "Got pixel rate from sensor (32bit): %u Hz\n", ctrl->val);
				return ctrl->val;
			}
		}
	}

	v4l2_warn(dev->v4l2_dev, "Sensor doesn't expose pixel rate control\n");
	return 0;
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
	struct v4l2_pix_format pix;
	struct v4l2_subdev_format fmt;
	int stream_num = 0;
	int ret, i;

	stream_num = ES_DVP2AXI_MAX_STREAM_MIPI;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		for (i = 0; i < stream_num; i++) {
			memset(&fmt, 0, sizeof(fmt));
			fmt.pad = i;
			fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
					 get_fmt, NULL, &fmt);
			memset(&pix, 0, sizeof(pix));
			pix.pixelformat =
				es_dvp2axi_mbus_pixelcode_to_v4l2(fmt.format.code);
			pix.width = fmt.format.width;
			pix.height = fmt.format.height;
			memset(&input_sel, 0, sizeof(input_sel));
			input_sel.pad = i;
			input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
			input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
					       get_selection, NULL, &input_sel);
			if (!ret) {
				pix.width = input_sel.r.width;
				pix.height = input_sel.r.height;
			}
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[i], &pix, false);
		}
	}
}

static void es_dvp2axi_init_dummy_vb2(struct es_dvp2axi_device *dev,
				struct es_dvp2axi_dummy_buffer *buf)
{
	memset(&buf->vb2_queue, 0, sizeof(struct vb2_queue));
	memset(&buf->vb, 0, sizeof(struct vb2_buffer));
	buf->vb.vb2_queue = &buf->vb2_queue;
}

int es_dvp2axi_alloc_buffer(struct es_dvp2axi_device *dev,
		       struct es_dvp2axi_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	struct sg_table	 *sg_tbl;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}
	es_dvp2axi_init_dummy_vb2(dev, buf);

	buf->size = PAGE_ALIGN(buf->size);
	mem_priv = g_ops->alloc(&buf->vb, dev->hw_dev->dev, buf->size);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	if (dev->hw_dev->is_dma_sg_ops) {
		sg_tbl = (struct sg_table *)g_ops->cookie(&buf->vb, mem_priv);
		buf->dma_addr = sg_dma_address(sg_tbl->sgl);
		g_ops->prepare(mem_priv);
	} else {
		buf->dma_addr = *((dma_addr_t *)g_ops->cookie(&buf->vb, mem_priv));
	}

	if (buf->is_need_vaddr)
		buf->vaddr = g_ops->vaddr(&buf->vb, mem_priv);

	if (buf->is_need_dbuf) {
		buf->dbuf = g_ops->get_dmabuf(&buf->vb, mem_priv, O_RDWR);
		if (buf->is_need_dmafd) {
			buf->dma_fd = dma_buf_fd(buf->dbuf, O_CLOEXEC);
			if (buf->dma_fd < 0) {
				dma_buf_put(buf->dbuf);
				ret = buf->dma_fd;
				goto err;
			}
			get_dma_buf(buf->dbuf);
		}
	}

	v4l2_dbg(1, es_dvp2axi_debug, dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void es_dvp2axi_free_buffer(struct es_dvp2axi_device *dev,
			struct es_dvp2axi_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops =  dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv) {
		v4l2_dbg(1, es_dvp2axi_debug, dev->v4l2_dev,
			 "%s buf:0x%x~0x%x\n", __func__,
			 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size);
		if (buf->dbuf)
			dma_buf_put(buf->dbuf);
		g_ops->put(buf->mem_priv);
		buf->size = 0;
		buf->dbuf = NULL;
		buf->vaddr = NULL;
		buf->mem_priv = NULL;
		buf->is_need_dbuf = false;
		buf->is_need_vaddr = false;
		buf->is_need_dmafd = false;
		buf->is_free = true;
	}
}

int es_dvp2axi_stream_suspend(struct es_dvp2axi_device *dvp2axi_dev, int mode)
{
	return 0;
}

int es_dvp2axi_stream_resume(struct es_dvp2axi_device *dvp2axi_dev, int mode)
{
	return 0;
}