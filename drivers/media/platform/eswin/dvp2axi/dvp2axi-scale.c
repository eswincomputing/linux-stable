// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI scale driver
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

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <media/v4l2-event.h>
#include "dev.h"
#include "regs.h"
#include "mipi-csi2.h"
#include <media/v4l2-fwnode.h>
#include <linux/pm_runtime.h>

#define MEMORY_ALIGN_ROUND_UP_HEIGHT		16

#define SCALE_MIN_WIDTH		4
#define SCALE_MIN_HEIGHT	4
#define SCALE_OUTPUT_STEP_WISE	1
#define DVP2AXI_SCALE_REQ_BUFS_MIN	3

static const struct dvp2axi_output_fmt scale_out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = DVP2AXI_FMT_TYPE_RAW,
	}
};

static int es_dvp2axi_scale_enum_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	const struct dvp2axi_output_fmt *fmt = NULL;

	if (f->index >= ARRAY_SIZE(scale_out_fmts))
		return -EINVAL;
	fmt = &scale_out_fmts[f->index];
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int es_dvp2axi_scale_g_fmt_vid_cap_mplane(struct file *file, void *priv,
					    struct v4l2_format *f)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);

	f->fmt.pix_mp = scale_vdev->pixm;
	return 0;
}

static u32 es_dvp2axi_scale_align_bits_per_pixel(struct es_dvp2axi_device *dvp2axi_dev,
					    const struct dvp2axi_output_fmt *fmt,
					    int plane_index)
{
	u32 bpp = 0, i;

	if (fmt) {
		switch (fmt->fourcc) {
		case V4L2_PIX_FMT_SBGGR16:
		case V4L2_PIX_FMT_SGBRG16:
		case V4L2_PIX_FMT_SGRBG16:
		case V4L2_PIX_FMT_SRGGB16:
			bpp = max(fmt->bpp[plane_index], (u8)DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126);
			for (i = 1; i < 5; i++) {
				if (i * DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126 >= bpp) {
					bpp = i * DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126;
					break;
				}
			}
			break;
		default:
			v4l2_err(&dvp2axi_dev->v4l2_dev, "fourcc: %d is not supported!\n",
				 fmt->fourcc);
			break;
		}
	}

	return bpp;
}


static const struct
dvp2axi_output_fmt *es_dvp2axi_scale_find_output_fmt(u32 pixelfmt)
{
	const struct dvp2axi_output_fmt *fmt;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(scale_out_fmts); i++) {
		fmt = &scale_out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

static int es_dvp2axi_scale_set_fmt(struct es_dvp2axi_scale_vdev *scale_vdev,
			       struct v4l2_pix_format_mplane *pixm,
			       bool try)
{
	struct es_dvp2axi_stream *stream = scale_vdev->stream;
	struct es_dvp2axi_device *dvp2axi_dev = scale_vdev->dvp2axidev;
	struct v4l2_subdev_selection input_sel;
	struct v4l2_subdev_format fmt_src;
	const struct dvp2axi_output_fmt *fmt;
	unsigned int imagesize = 0;
	int bpl, size, bpp;
	int scale_times = 0;
	u32 scale_ratio = 0;
	u32 width = 3280;
	u32 height = 2464;
	int ret = 0;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		fmt_src.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt_src.pad = 0;
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad, get_fmt, NULL, &fmt_src);
		if (ret) {
			v4l2_err(&scale_vdev->dvp2axidev->v4l2_dev,
				 "%s: get sensor format failed\n", __func__);
			return ret;
		}

		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		input_sel.pad = 0;
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			fmt_src.format.width = input_sel.r.width;
			fmt_src.format.height = input_sel.r.height;
		}
		scale_vdev->src_res.width = fmt_src.format.width;
		scale_vdev->src_res.height = fmt_src.format.height;
	}
	fmt = es_dvp2axi_scale_find_output_fmt(pixm->pixelformat);
	if (fmt == NULL) {
		v4l2_err(&scale_vdev->dvp2axidev->v4l2_dev,
			"format of source channel are not bayer raw, not support scale\n");
		return -1;
	}
	if (scale_vdev->src_res.width && scale_vdev->src_res.height) {
		width = scale_vdev->src_res.width;
		height = scale_vdev->src_res.height;
	}
	scale_ratio = width / pixm->width;
	if (scale_ratio <= 8) {
		scale_vdev->scale_mode = SCALE_8TIMES;
		scale_times = 8;
	} else if (scale_ratio <= 16) {
		scale_vdev->scale_mode = SCALE_16TIMES;
		scale_times = 16;
	} else {
		scale_vdev->scale_mode = SCALE_32TIMES;
		scale_times = 32;
	}
	//source resolution align (scale_times * 2)
	width = ALIGN(width, scale_times * 2);
	pixm->width = width  / (scale_times * 2) * 2;
	pixm->height = height / (scale_times * 2) * 2;
	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	pixm->quantization = V4L2_QUANTIZATION_DEFAULT;

	bpp = es_dvp2axi_scale_align_bits_per_pixel(dvp2axi_dev, fmt, 0);
	bpl = pixm->width * bpp / DVP2AXI_RAW_STORED_BIT_WIDTH_RV1126;
	bpl = ALIGN(bpl, 8);
	size = bpl * pixm->height;
	imagesize += size;

	v4l2_dbg(1, es_dvp2axi_debug, &stream->dvp2axidev->v4l2_dev,
		 "%s C-Plane %i size: %d, Total imagesize: %d\n",
		 __func__, 0, size, imagesize);

	if (fmt->mplanes == 1) {
		pixm->plane_fmt[0].bytesperline = bpl;
		pixm->plane_fmt[0].sizeimage = imagesize;
	}

	if (!try) {
		scale_vdev->scale_out_fmt = fmt;
		scale_vdev->pixm = *pixm;

		v4l2_info(&stream->dvp2axidev->v4l2_dev,
			  "%s: req(%d, %d) src out(%d, %d)\n", __func__,
			  pixm->width, pixm->height,
			  scale_vdev->src_res.width, scale_vdev->src_res.height);
	}
	return 0;
}

static int es_dvp2axi_scale_s_fmt_vid_cap_mplane(struct file *file,
					    void *priv, struct v4l2_format *f)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	int ret = 0;

	if (vb2_is_busy(&scale_vdev->vnode.buf_queue)) {
		v4l2_err(&scale_vdev->dvp2axidev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	ret = es_dvp2axi_scale_set_fmt(scale_vdev, &f->fmt.pix_mp, false);

	return ret;
}

static int es_dvp2axi_scale_querycap(struct file *file,
				void *priv, struct v4l2_capability *cap)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	struct device *dev = scale_vdev->dvp2axidev->dev;

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));
	return 0;
}

static long es_dvp2axi_scale_ioctl_default(struct file *file, void *fh,
				    bool valid_prio, unsigned int cmd, void *arg)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct bayer_blc *pblc;

	switch (cmd) {
	case ES_DVP2AXI_CMD_GET_SCALE_BLC:
		pblc = (struct bayer_blc *)arg;
		*pblc = scale_vdev->blc;
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev, "get scale blc %d %d %d %d\n",
			 pblc->pattern00, pblc->pattern01, pblc->pattern02, pblc->pattern03);
		break;
	case ES_DVP2AXI_CMD_SET_SCALE_BLC:
		pblc = (struct bayer_blc *)arg;
		scale_vdev->blc = *pblc;
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev, "set scale blc %d %d %d %d\n",
			 pblc->pattern00, pblc->pattern01, pblc->pattern02, pblc->pattern03);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es_dvp2axi_scale_enum_input(struct file *file, void *priv,
				  struct v4l2_input *input)
{

	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int es_dvp2axi_scale_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					      struct v4l2_format *f)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	int ret = 0;

	ret = es_dvp2axi_scale_set_fmt(scale_vdev, &f->fmt.pix_mp, true);

	return ret;
}

static int es_dvp2axi_scale_enum_frameintervals(struct file *file, void *fh,
					   struct v4l2_frmivalenum *fival)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct es_dvp2axi_sensor_info *sensor = &dev->terminal_sensor;
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

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.min.numerator = fi.interval.numerator;
	fival->stepwise.min.denominator = fi.interval.denominator;

	return 0;
}

static int es_dvp2axi_scale_enum_framesizes(struct file *file, void *prov,
				       struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_frmsize_discrete *s = &fsize->discrete;
	struct es_dvp2axi_scale_vdev *scale_vdev = video_drvdata(file);
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct v4l2_rect input_rect;
	struct es_dvp2axi_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct csi_channel_info csi_info;
	int scale_times = 0;

	if (fsize->index >= ES_DVP2AXI_SCALE_ENUM_SIZE_MAX)
		return -EINVAL;

	if (!es_dvp2axi_scale_find_output_fmt(fsize->pixel_format))
		return -EINVAL;

	input_rect.width = ES_DVP2AXI_DEFAULT_WIDTH;
	input_rect.height = ES_DVP2AXI_DEFAULT_HEIGHT;

	if (terminal_sensor && terminal_sensor->sd)
		es_dvp2axi_get_input_fmt(dev,
				    &input_rect, 0, &csi_info);

	switch (fsize->index) {
	case SCALE_8TIMES:
		scale_times = 8;
		break;
	case SCALE_16TIMES:
		scale_times = 16;
		break;
	case SCALE_32TIMES:
		scale_times = 32;
		break;
	default:
		scale_times = 32;
		break;
	}
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	s->width = input_rect.width  / (scale_times * 2) * 2;
	s->height = input_rect.height / (scale_times * 2) * 2;

	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops es_dvp2axi_scale_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = es_dvp2axi_scale_enum_input,
	.vidioc_enum_fmt_vid_cap = es_dvp2axi_scale_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane = es_dvp2axi_scale_g_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = es_dvp2axi_scale_s_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_cap_mplane = es_dvp2axi_scale_try_fmt_vid_cap_mplane,
	.vidioc_querycap = es_dvp2axi_scale_querycap,
	.vidioc_enum_frameintervals = es_dvp2axi_scale_enum_frameintervals,
	.vidioc_enum_framesizes = es_dvp2axi_scale_enum_framesizes,
	.vidioc_default = es_dvp2axi_scale_ioctl_default,
};

static int es_dvp2axi_scale_fh_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_scale_vdev *scale_vdev = to_es_dvp2axi_scale_vdev(vnode);
	struct es_dvp2axi_device *dvp2axidev = scale_vdev->dvp2axidev;
	int ret;

	ret = es_dvp2axi_update_sensor_info(scale_vdev->stream);
	if (ret < 0) {
		v4l2_err(vdev,
			 "update sensor info failed %d\n",
			 ret);

		return ret;
	}

	ret = pm_runtime_resume_and_get(dvp2axidev->dev);
	if (ret < 0)
		v4l2_err(&dvp2axidev->v4l2_dev, "Failed to get runtime pm, %d\n",
			 ret);

	ret = v4l2_fh_open(file);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&vnode->vdev.entity);
		if (ret < 0)
			vb2_fop_release(file);
	}

	return ret;
}

static int es_dvp2axi_scale_fop_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct es_dvp2axi_vdev_node *vnode = vdev_to_node(vdev);
	struct es_dvp2axi_scale_vdev *scale_vdev = to_es_dvp2axi_scale_vdev(vnode);
	struct es_dvp2axi_device *dvp2axidev = scale_vdev->dvp2axidev;
	int ret;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&vnode->vdev.entity);

	pm_runtime_put_sync(dvp2axidev->dev);
	return ret;
}

struct v4l2_file_operations es_dvp2axi_scale_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = es_dvp2axi_scale_fh_open,
	.release = es_dvp2axi_scale_fop_release
};

static int es_dvp2axi_scale_vb2_queue_setup(struct vb2_queue *queue,
				       unsigned int *num_buffers,
				       unsigned int *num_planes,
				       unsigned int sizes[],
				       struct device *alloc_ctxs[])
{
	struct es_dvp2axi_scale_vdev *scale_vdev = queue->drv_priv;
	struct es_dvp2axi_device *dvp2axi_dev = scale_vdev->dvp2axidev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct dvp2axi_output_fmt *dvp2axi_fmt;
	u32 i;
	const struct v4l2_plane_pix_format *plane_fmt;

	pixm = &scale_vdev->pixm;
	dvp2axi_fmt = scale_vdev->scale_out_fmt;
	*num_planes = dvp2axi_fmt->mplanes;

	for (i = 0; i < dvp2axi_fmt->mplanes; i++) {
		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage;
	}

	v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);
	return 0;

}

static void es_dvp2axi_scale_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct es_dvp2axi_buffer *dvp2axibuf = to_es_dvp2axi_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct es_dvp2axi_scale_vdev *scale_vdev = queue->drv_priv;
	struct v4l2_pix_format_mplane *pixm = &scale_vdev->pixm;
	const struct dvp2axi_output_fmt *fmt = scale_vdev->scale_out_fmt;
	struct es_dvp2axi_hw *hw_dev = scale_vdev->dvp2axidev->hw_dev;
	unsigned long lock_flags = 0;
	int i;

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
			dvp2axibuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
		if (es_dvp2axi_debug && addr && !hw_dev->iommu_en) {
			memset(addr, 0, pixm->plane_fmt[i].sizeimage);
			v4l2_dbg(3, es_dvp2axi_debug, &scale_vdev->dvp2axidev->v4l2_dev,
				 "Clear buffer, size: 0x%08x\n",
				 pixm->plane_fmt[i].sizeimage);
		}
	}

	if (fmt->mplanes == 1) {
		for (i = 0; i < fmt->cplanes - 1; i++)
			dvp2axibuf->buff_addr[i + 1] = dvp2axibuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline * pixm->height;
	}
	spin_lock_irqsave(&scale_vdev->vbq_lock, lock_flags);
	list_add_tail(&dvp2axibuf->queue, &scale_vdev->buf_head);
	spin_unlock_irqrestore(&scale_vdev->vbq_lock, lock_flags);
}

static int es_dvp2axi_scale_stop(struct es_dvp2axi_scale_vdev *scale_vdev)
{
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	int ch = scale_vdev->ch;

	es_dvp2axi_write_register_and(dev, DVP2AXI_REG_SCL_CH_CTRL,
				 ~(DVP2AXI_SCALE_EN(ch) |
				 DVP2AXI_SCALE_SW_SRC_CH(0x1f, ch) |
				 DVP2AXI_SCALE_SW_MODE(0x03, ch)));
	scale_vdev->state = ES_DVP2AXI_STATE_READY;
	scale_vdev->frame_idx = 0;
	return 0;
}

static void es_dvp2axi_scale_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = vq->drv_priv;
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct es_dvp2axi_buffer *buf = NULL;
	int ret = 0;

	mutex_lock(&dev->scale_lock);
	/* Make sure no new work queued in isr before draining wq */
	scale_vdev->stopping = true;
	ret = wait_event_timeout(scale_vdev->wq_stopped,
				 scale_vdev->state != ES_DVP2AXI_STATE_STREAMING,
				 msecs_to_jiffies(1000));
	if (!ret) {
		es_dvp2axi_scale_stop(scale_vdev);
		scale_vdev->stopping = false;
	}
	/* release buffers */
	if (scale_vdev->curr_buf)
		list_add_tail(&scale_vdev->curr_buf->queue, &scale_vdev->buf_head);

	if (scale_vdev->next_buf &&
	    scale_vdev->next_buf != scale_vdev->curr_buf)
		list_add_tail(&scale_vdev->next_buf->queue, &scale_vdev->buf_head);
	scale_vdev->curr_buf = NULL;
	scale_vdev->next_buf = NULL;
	while (!list_empty(&scale_vdev->buf_head)) {
		buf = list_first_entry(&scale_vdev->buf_head,
				       struct es_dvp2axi_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&dev->scale_lock);
}

static int es_dvp2axi_scale_channel_init(struct es_dvp2axi_scale_vdev *scale_vdev)
{
	struct es_dvp2axi_device *dvp2axi_dev = scale_vdev->dvp2axidev;
	struct es_dvp2axi_scale_ch_info *ch_info = &scale_vdev->ch_info;
	struct v4l2_pix_format_mplane pixm = scale_vdev->pixm;
	const struct dvp2axi_output_fmt *fmt = scale_vdev->scale_out_fmt;

	if (dvp2axi_dev->inf_id == ES_DVP2AXI_DVP)
		scale_vdev->ch_src = SCALE_DVP;
	else
		scale_vdev->ch_src = 4 * dvp2axi_dev->csi_host_idx + scale_vdev->ch;
	ch_info->width = pixm.width;
	ch_info->height = pixm.height;
	ch_info->vir_width = ALIGN(ch_info->width  * fmt->bpp[0] / 8, 8);
	return 0;
}

static enum dvp2axi_reg_index get_reg_index_of_scale_vlw(int ch)
{
	enum dvp2axi_reg_index index;

	switch (ch) {
	case 0:
		index = DVP2AXI_REG_SCL_VLW_CH0;
		break;
	case 1:
		index = DVP2AXI_REG_SCL_VLW_CH1;
		break;
	case 2:
		index = DVP2AXI_REG_SCL_VLW_CH2;
		break;
	case 3:
		index = DVP2AXI_REG_SCL_VLW_CH3;
		break;
	default:
		index = DVP2AXI_REG_SCL_VLW_CH0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_scale_frm0_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_SCL_FRM0_ADDR_CH0;
		break;
	case 1:
		index = DVP2AXI_REG_SCL_FRM0_ADDR_CH1;
		break;
	case 2:
		index = DVP2AXI_REG_SCL_FRM0_ADDR_CH2;
		break;
	case 3:
		index = DVP2AXI_REG_SCL_FRM0_ADDR_CH3;
		break;
	default:
		index = DVP2AXI_REG_SCL_FRM0_ADDR_CH0;
		break;
	}

	return index;
}

static enum dvp2axi_reg_index get_reg_index_of_scale_frm1_addr(int channel_id)
{
	enum dvp2axi_reg_index index;

	switch (channel_id) {
	case 0:
		index = DVP2AXI_REG_SCL_FRM1_ADDR_CH0;
		break;
	case 1:
		index = DVP2AXI_REG_SCL_FRM1_ADDR_CH1;
		break;
	case 2:
		index = DVP2AXI_REG_SCL_FRM1_ADDR_CH2;
		break;
	case 3:
		index = DVP2AXI_REG_SCL_FRM1_ADDR_CH3;
		break;
	default:
		index = DVP2AXI_REG_SCL_FRM1_ADDR_CH0;
		break;
	}

	return index;
}

static void es_dvp2axi_assign_scale_buffer_init(struct es_dvp2axi_scale_vdev *scale_vdev,
					   int ch)
{
	struct es_dvp2axi_device *dev = scale_vdev->stream->dvp2axidev;
	u32 frm0_addr;
	u32 frm1_addr;
	unsigned long flags;

	frm0_addr = get_reg_index_of_scale_frm0_addr(ch);
	frm1_addr = get_reg_index_of_scale_frm1_addr(ch);

	spin_lock_irqsave(&scale_vdev->vbq_lock, flags);

	if (!scale_vdev->curr_buf) {
		if (!list_empty(&scale_vdev->buf_head)) {
			scale_vdev->curr_buf = list_first_entry(&scale_vdev->buf_head,
							    struct es_dvp2axi_buffer,
							    queue);
			list_del(&scale_vdev->curr_buf->queue);
		}
	}

	if (scale_vdev->curr_buf)
		es_dvp2axi_write_register(dev, frm0_addr,
				     scale_vdev->curr_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);

	if (!scale_vdev->next_buf) {
		if (!list_empty(&scale_vdev->buf_head)) {
			scale_vdev->next_buf = list_first_entry(&scale_vdev->buf_head,
							    struct es_dvp2axi_buffer, queue);
			list_del(&scale_vdev->next_buf->queue);
		}
	}

	if (scale_vdev->next_buf)
		es_dvp2axi_write_register(dev, frm1_addr,
				     scale_vdev->next_buf->buff_addr[ES_DVP2AXI_PLANE_Y]);

	spin_unlock_irqrestore(&scale_vdev->vbq_lock, flags);
}

static int es_dvp2axi_assign_scale_buffer_update(struct es_dvp2axi_scale_vdev *scale_vdev,
					    int channel_id)
{
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct es_dvp2axi_buffer *buffer = NULL;
	u32 frm_addr;
	int ret = 0;
	unsigned long flags;

	frm_addr = scale_vdev->frame_phase & DVP2AXI_CSI_FRAME0_READY ?
		   get_reg_index_of_scale_frm0_addr(channel_id) :
		   get_reg_index_of_scale_frm1_addr(channel_id);

	spin_lock_irqsave(&scale_vdev->vbq_lock, flags);
	if (!list_empty(&scale_vdev->buf_head)) {
		if (scale_vdev->frame_phase == DVP2AXI_CSI_FRAME0_READY) {
			scale_vdev->curr_buf = list_first_entry(&scale_vdev->buf_head,
							    struct es_dvp2axi_buffer, queue);
			if (scale_vdev->curr_buf) {
				list_del(&scale_vdev->curr_buf->queue);
				buffer = scale_vdev->curr_buf;
			}
		} else if (scale_vdev->frame_phase == DVP2AXI_CSI_FRAME1_READY) {
			scale_vdev->next_buf = list_first_entry(&scale_vdev->buf_head,
							    struct es_dvp2axi_buffer, queue);
			if (scale_vdev->next_buf) {
				list_del(&scale_vdev->next_buf->queue);
				buffer = scale_vdev->next_buf;
			}
		}
	} else {
		buffer = NULL;
	}
	spin_unlock_irqrestore(&scale_vdev->vbq_lock, flags);

	if (buffer) {
		es_dvp2axi_write_register(dev, frm_addr,
				     buffer->buff_addr[ES_DVP2AXI_PLANE_Y]);
	} else {
		ret = -EINVAL;
		v4l2_info(&dev->v4l2_dev,
			 "not active buffer,skip frame, scale ch[%d]\n",
			  scale_vdev->ch);
	}
	return ret;
}

static int es_dvp2axi_assign_scale_buffer_pingpong(struct es_dvp2axi_scale_vdev *scale_vdev,
					      int init, int channel_id)
{
	int ret = 0;

	if (init)
		es_dvp2axi_assign_scale_buffer_init(scale_vdev, channel_id);
	else
		ret = es_dvp2axi_assign_scale_buffer_update(scale_vdev, channel_id);
	return ret;
}

static int es_dvp2axi_scale_channel_set(struct es_dvp2axi_scale_vdev *scale_vdev)
{
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	u32 val = 0;
	u32 ch  = scale_vdev->ch;

	val = es_dvp2axi_read_register(dev, DVP2AXI_REG_SCL_CH_CTRL);
	if (val & DVP2AXI_SCALE_EN(ch)) {
		v4l2_err(&dev->v4l2_dev, "scale_vdev[%d] has been used by other device\n", ch);
		return -EINVAL;
	}

	es_dvp2axi_assign_scale_buffer_pingpong(scale_vdev,
					   ES_DVP2AXI_YUV_ADDR_STATE_INIT,
					   ch);
	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_SCL_CTRL, SCALE_SOFT_RESET(scale_vdev->ch));

	es_dvp2axi_write_register_and(dev, DVP2AXI_REG_GLB_INTST,
				 ~(SCALE_END_INTSTAT(ch) |
				 SCALE_FIFO_OVERFLOW(ch)));
	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_GLB_INTEN,
				(SCALE_END_INTSTAT(ch) |
				SCALE_FIFO_OVERFLOW(ch) |
				SCALE_TOISP_AXI0_ERR |
				SCALE_TOISP_AXI1_ERR));
	val = DVP2AXI_SCALE_SW_PRESS_ENABLE |
	      DVP2AXI_SCALE_SW_PRESS_VALUE(7) |
	      DVP2AXI_SCALE_SW_HURRY_ENABLE |
	      DVP2AXI_SCALE_SW_HURRY_VALUE(7) |
	      DVP2AXI_SCALE_SW_WATER_LINE(1);

	es_dvp2axi_write_register(dev, DVP2AXI_REG_SCL_CTRL, val);
	val = scale_vdev->blc.pattern00 |
	      (scale_vdev->blc.pattern01 << 8) |
	      (scale_vdev->blc.pattern02 << 16) |
	      (scale_vdev->blc.pattern03 << 24);
	es_dvp2axi_write_register(dev, DVP2AXI_REG_SCL_BLC_CH0 + ch,
			     val);
	es_dvp2axi_write_register(dev, get_reg_index_of_scale_vlw(ch),
			     scale_vdev->ch_info.vir_width);
	val = DVP2AXI_SCALE_SW_SRC_CH(scale_vdev->ch_src, ch) |
	      DVP2AXI_SCALE_SW_MODE(scale_vdev->scale_mode, ch) |
	      DVP2AXI_SCALE_EN(ch);
	es_dvp2axi_write_register_or(dev, DVP2AXI_REG_SCL_CH_CTRL,
				val);
	return 0;
}


int es_dvp2axi_scale_start(struct es_dvp2axi_scale_vdev *scale_vdev)
{
	int ret = 0;
	struct es_dvp2axi_device *dev = scale_vdev->dvp2axidev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;

	mutex_lock(&dev->scale_lock);
	if (scale_vdev->state == ES_DVP2AXI_STATE_STREAMING) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}

	es_dvp2axi_scale_channel_init(scale_vdev);
	ret = es_dvp2axi_scale_channel_set(scale_vdev);
	if (ret)
		goto destroy_buf;
	scale_vdev->frame_idx = 0;
	scale_vdev->state = ES_DVP2AXI_STATE_STREAMING;
	mutex_unlock(&dev->scale_lock);
	return 0;

destroy_buf:
	if (scale_vdev->next_buf)
		vb2_buffer_done(&scale_vdev->next_buf->vb.vb2_buf,
				VB2_BUF_STATE_QUEUED);
	if (scale_vdev->curr_buf)
		vb2_buffer_done(&scale_vdev->curr_buf->vb.vb2_buf,
				VB2_BUF_STATE_QUEUED);
	while (!list_empty(&scale_vdev->buf_head)) {
		struct es_dvp2axi_buffer *buf;

		buf = list_first_entry(&scale_vdev->buf_head,
				       struct es_dvp2axi_buffer, queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}
	mutex_unlock(&dev->scale_lock);
	return ret;
}

static int
es_dvp2axi_scale_vb2_start_streaming(struct vb2_queue *queue,
				unsigned int count)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = queue->drv_priv;
	struct es_dvp2axi_stream *stream = scale_vdev->stream;
	int ret = 0;

	if (stream->state == ES_DVP2AXI_STATE_STREAMING) {
		stream->to_en_scale = true;
	} else {
		ret = es_dvp2axi_scale_start(scale_vdev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct vb2_ops es_dvp2axi_scale_vb2_ops = {
	.queue_setup = es_dvp2axi_scale_vb2_queue_setup,
	.buf_queue = es_dvp2axi_scale_vb2_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = es_dvp2axi_scale_vb2_stop_streaming,
	.start_streaming = es_dvp2axi_scale_vb2_start_streaming,
};

static int es_dvp2axi_scale_init_vb2_queue(struct vb2_queue *q,
				      struct es_dvp2axi_scale_vdev *scale_vdev,
				      enum v4l2_buf_type buf_type)
{
	struct es_dvp2axi_hw *hw_dev = scale_vdev->dvp2axidev->hw_dev;

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = scale_vdev;
	q->ops = &es_dvp2axi_scale_vb2_ops;
	q->mem_ops = hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct es_dvp2axi_buffer);
	q->min_buffers_needed = DVP2AXI_SCALE_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &scale_vdev->vnode.vlock;
	q->dev = hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	q->gfp_flags = GFP_DMA32;
	if (hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	return vb2_queue_init(q);
}


static int es_dvp2axi_scale_g_ch(struct v4l2_device *v4l2_dev,
			    unsigned int intstat)
{
	if (intstat & SCALE_END_INTSTAT(0)) {
		if ((intstat & SCALE_END_INTSTAT(0)) ==
		    SCALE_END_INTSTAT(0))
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in CH0\n");
		return ES_DVP2AXI_SCALE_CH0;
	}

	if (intstat & SCALE_END_INTSTAT(1)) {
		if ((intstat & SCALE_END_INTSTAT(1)) ==
		    SCALE_END_INTSTAT(1))
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in CH1\n");
		return ES_DVP2AXI_SCALE_CH1;
	}

	if (intstat & SCALE_END_INTSTAT(2)) {
		if ((intstat & SCALE_END_INTSTAT(2)) ==
		    SCALE_END_INTSTAT(2))
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in CH2\n");
		return ES_DVP2AXI_SCALE_CH2;
	}

	if (intstat & SCALE_END_INTSTAT(3)) {
		if ((intstat & SCALE_END_INTSTAT(3)) ==
		    SCALE_END_INTSTAT(3))
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in CH3\n");
		return ES_DVP2AXI_SCALE_CH3;
	}

	return -EINVAL;
}

static void es_dvp2axi_scale_vb_done_oneframe(struct es_dvp2axi_scale_vdev *scale_vdev,
					 struct vb2_v4l2_buffer *vb_done)
{
	const struct dvp2axi_output_fmt *fmt = scale_vdev->scale_out_fmt;
	u32 i;

	/* Dequeue a filled buffer */
	for (i = 0; i < fmt->mplanes; i++) {
		vb2_set_plane_payload(&vb_done->vb2_buf, i,
				      scale_vdev->pixm.plane_fmt[i].sizeimage);
	}

	vb_done->vb2_buf.timestamp = es_dvp2axi_time_get_ns(scale_vdev->dvp2axidev);

	vb2_buffer_done(&vb_done->vb2_buf, VB2_BUF_STATE_DONE);
}

static void es_dvp2axi_scale_update_stream(struct es_dvp2axi_scale_vdev *scale_vdev, int ch)
{
	struct es_dvp2axi_buffer *active_buf = NULL;
	struct vb2_v4l2_buffer *vb_done = NULL;
	int ret = 0;

	if (scale_vdev->frame_phase & DVP2AXI_CSI_FRAME0_READY) {
		if (scale_vdev->curr_buf)
			active_buf = scale_vdev->curr_buf;
	} else if (scale_vdev->frame_phase & DVP2AXI_CSI_FRAME1_READY) {
		if (scale_vdev->next_buf)
			active_buf = scale_vdev->next_buf;
	}

	ret = es_dvp2axi_assign_scale_buffer_pingpong(scale_vdev,
					 ES_DVP2AXI_YUV_ADDR_STATE_UPDATE,
					 ch);

	scale_vdev->frame_idx = scale_vdev->stream->frame_idx;
	if (active_buf && (!ret)) {
		vb_done = &active_buf->vb;
		vb_done->vb2_buf.timestamp = ktime_get_ns();
		vb_done->sequence = scale_vdev->frame_idx;
		es_dvp2axi_scale_vb_done_oneframe(scale_vdev, vb_done);
	}
}

void es_dvp2axi_irq_handle_scale(struct es_dvp2axi_device *dvp2axi_dev, unsigned int intstat_glb)
{
	struct es_dvp2axi_scale_vdev *scale_vdev;
	struct es_dvp2axi_stream *stream;
	int ch;
	int i = 0;
	u32 val = 0;

	val = SCALE_FIFO_OVERFLOW(0) |
	      SCALE_FIFO_OVERFLOW(1) |
	      SCALE_FIFO_OVERFLOW(2) |
	      SCALE_FIFO_OVERFLOW(3);
	if (intstat_glb & val) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			"ERROR: scale channel, overflow intstat_glb:0x%x !!\n",
			intstat_glb);
		return;
	}

	ch = es_dvp2axi_scale_g_ch(&dvp2axi_dev->v4l2_dev,
				      intstat_glb);
	if (ch < 0)
		return;

	for (i = 0; i < ES_DVP2AXI_MAX_STREAM_MIPI; i++) {
		ch = es_dvp2axi_scale_g_ch(&dvp2axi_dev->v4l2_dev,
				      intstat_glb);
		if (ch < 0)
			continue;

		scale_vdev = &dvp2axi_dev->scale_vdev[ch];

		if (scale_vdev->state != ES_DVP2AXI_STATE_STREAMING)
			continue;

		if (scale_vdev->stopping) {
			es_dvp2axi_scale_stop(scale_vdev);
			scale_vdev->stopping = false;
			wake_up(&scale_vdev->wq_stopped);
			continue;
		}

		scale_vdev->frame_phase = SW_SCALE_END(intstat_glb, ch);
		intstat_glb &= ~(SCALE_END_INTSTAT(ch));
		es_dvp2axi_scale_update_stream(scale_vdev, ch);
		stream = scale_vdev->stream;
		if (stream->to_en_dma)
			es_dvp2axi_enable_dma_capture(stream, false);
	}
}

void es_dvp2axi_init_scale_vdev(struct es_dvp2axi_device *dvp2axi_dev, u32 ch)
{
	struct es_dvp2axi_scale_vdev *scale_vdev = &dvp2axi_dev->scale_vdev[ch];
	struct es_dvp2axi_stream *stream = &dvp2axi_dev->stream[ch];
	struct v4l2_pix_format_mplane pixm;

	memset(scale_vdev, 0, sizeof(*scale_vdev));
	memset(&pixm, 0, sizeof(pixm));
	scale_vdev->dvp2axidev = dvp2axi_dev;
	scale_vdev->stream = stream;
	stream->scale_vdev = scale_vdev;
	scale_vdev->ch = ch;
	scale_vdev->ch_src = 0;
	scale_vdev->frame_idx = 0;
	pixm.pixelformat = V4L2_PIX_FMT_SBGGR16;
	pixm.width = ES_DVP2AXI_DEFAULT_WIDTH;
	pixm.height = ES_DVP2AXI_DEFAULT_HEIGHT;
	scale_vdev->state = ES_DVP2AXI_STATE_READY;
	scale_vdev->stopping = false;
	scale_vdev->blc.pattern00 = 0;
	scale_vdev->blc.pattern01 = 0;
	scale_vdev->blc.pattern02 = 0;
	scale_vdev->blc.pattern03 = 0;
	INIT_LIST_HEAD(&scale_vdev->buf_head);
	spin_lock_init(&scale_vdev->vbq_lock);
	init_waitqueue_head(&scale_vdev->wq_stopped);
	es_dvp2axi_scale_set_fmt(scale_vdev, &pixm, false);
}

static int es_dvp2axi_register_scale_vdev(struct es_dvp2axi_scale_vdev *scale_vdev, bool is_multi_input)
{
	int ret = 0;
	struct video_device *vdev = &scale_vdev->vnode.vdev;
	struct es_dvp2axi_vdev_node *node;
	char *vdev_name;

	switch (scale_vdev->ch) {
	case ES_DVP2AXI_SCALE_CH0:
		vdev_name = DVP2AXI_SCALE_CH0_VDEV_NAME;
		break;
	case ES_DVP2AXI_SCALE_CH1:
		vdev_name = DVP2AXI_SCALE_CH1_VDEV_NAME;
		break;
	case ES_DVP2AXI_SCALE_CH2:
		vdev_name = DVP2AXI_SCALE_CH2_VDEV_NAME;
		break;
	case ES_DVP2AXI_SCALE_CH3:
		vdev_name = DVP2AXI_SCALE_CH3_VDEV_NAME;
		break;
	default:
		ret = -EINVAL;
		v4l2_err(&scale_vdev->dvp2axidev->v4l2_dev, "Invalid stream\n");
		goto err_cleanup_media_entity;
	}

	strscpy(vdev->name, vdev_name, sizeof(vdev->name));
	node = container_of(vdev, struct es_dvp2axi_vdev_node, vdev);
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &es_dvp2axi_scale_ioctl;
	vdev->fops = &es_dvp2axi_scale_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = &scale_vdev->dvp2axidev->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
	video_set_drvdata(vdev, scale_vdev);

	es_dvp2axi_scale_init_vb2_queue(&node->buf_queue,
				   scale_vdev,
				   V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &node->buf_queue;

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto err_release_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

static void es_dvp2axi_unregister_scale_vdev(struct es_dvp2axi_scale_vdev *scale_vdev)
{
	struct es_dvp2axi_vdev_node *node = &scale_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

int es_dvp2axi_register_scale_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
			       int stream_num,
			       bool is_multi_input)
{
	struct es_dvp2axi_scale_vdev *scale_vdev;
	int i, j, ret;

	for (i = 0; i < stream_num; i++) {
		scale_vdev = &dvp2axi_dev->scale_vdev[i];
		ret = es_dvp2axi_register_scale_vdev(scale_vdev, is_multi_input);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	for (j = 0; j < i; j++) {
		scale_vdev = &dvp2axi_dev->scale_vdev[j];
		es_dvp2axi_unregister_scale_vdev(scale_vdev);
	}

	return ret;
}

void es_dvp2axi_unregister_scale_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
				  int stream_num)
{
	struct es_dvp2axi_scale_vdev *scale_vdev;
	int i;

	for (i = 0; i < stream_num; i++) {
		scale_vdev = &dvp2axi_dev->scale_vdev[i];
		es_dvp2axi_unregister_scale_vdev(scale_vdev);
	}
}

