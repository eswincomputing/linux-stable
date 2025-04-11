// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI subdev driver
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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include "dev.h"
#include <linux/regulator/consumer.h>
#include "../../../../phy/eswin/es-camera-module.h"
#include "common.h"

static inline struct sditf_priv *to_sditf_priv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct sditf_priv, sd);
}

static void sditf_buffree_work(struct work_struct *work)
{
	struct sditf_work_struct *buffree_work =
		container_of(work, struct sditf_work_struct, work);
	struct sditf_priv *priv =
		container_of(buffree_work, struct sditf_priv, buffree_work);
	struct es_dvp2axi_rx_buffer *rx_buf = NULL;
	unsigned long flags;
	LIST_HEAD(local_list);

	spin_lock_irqsave(&priv->dvp2axi_dev->buffree_lock, flags);
	list_replace_init(&priv->buf_free_list, &local_list);
	while (!list_empty(&local_list)) {
		rx_buf = list_first_entry(&local_list, struct es_dvp2axi_rx_buffer,
					  list_free);
		if (rx_buf) {
			list_del(&rx_buf->list_free);
			es_dvp2axi_free_reserved_mem_buf(priv->dvp2axi_dev, rx_buf);
		}
	}
	spin_unlock_irqrestore(&priv->dvp2axi_dev->buffree_lock, flags);
}

static void sditf_get_hdr_mode(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct esmodule_hdr_cfg hdr_cfg;
	int ret = 0;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, core, ioctl,
				       ESMODULE_GET_HDR_CFG, &hdr_cfg);
		if (!ret)
			priv->hdr_cfg = hdr_cfg;
		else
			priv->hdr_cfg.hdr_mode = NO_HDR;
	} else {
		priv->hdr_cfg.hdr_mode = NO_HDR;
	}
}

static int sditf_g_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_frame_interval *fi)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev *sensor_sd;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		sensor_sd = dvp2axi_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, video, g_frame_interval, fi);
	}

	return -EINVAL;
}

static int sditf_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			       struct v4l2_mbus_config *config)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev *sensor_sd;

	if (!dvp2axi_dev->active_sensor)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->active_sensor) {
		sensor_sd = dvp2axi_dev->active_sensor->sd;
		return v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0,
					config);
	}

	return -EINVAL;
}

static int sditf_get_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev_selection input_sel;
	struct v4l2_pix_format_mplane pixm;
	const struct dvp2axi_output_fmt *out_fmt;
	int ret = -EINVAL;
	bool is_uncompact = false;

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		sditf_get_hdr_mode(priv);
		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = 0;
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
				       get_fmt, NULL, fmt);
		if (ret) {
			v4l2_err(&priv->sd, "%s: get sensor format failed\n",
				 __func__);
			return ret;
		}

		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		input_sel.pad = 0;
		ret = v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, pad,
				       get_selection, NULL, &input_sel);
		if (!ret) {
			fmt->format.width = input_sel.r.width;
			fmt->format.height = input_sel.r.height;
		}
		priv->cap_info.width = fmt->format.width;
		priv->cap_info.height = fmt->format.height;
		pixm.pixelformat =
			es_dvp2axi_mbus_pixelcode_to_v4l2(fmt->format.code);
		pixm.width = priv->cap_info.width;
		pixm.height = priv->cap_info.height;

		out_fmt = es_dvp2axi_find_output_fmt(NULL, pixm.pixelformat);
		if (priv->toisp_inf.link_mode == TOISP_UNITE &&
		    ((pixm.width / 2 - ESMOUDLE_UNITE_EXTEND_PIXEL) *
		     out_fmt->raw_bpp / 8) &
			    0xf)
			is_uncompact = true;

		v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "%s, width %d, height %d, hdr mode %d\n", __func__,
			 fmt->format.width, fmt->format.height,
			 priv->hdr_cfg.hdr_mode);
		if (priv->hdr_cfg.hdr_mode == NO_HDR ||
		    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[0], &pixm, false);
		} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
			if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE &&
			    priv->toisp_inf.link_mode == TOISP_UNITE) {
				if (is_uncompact) {
					dvp2axi_dev->stream[0].is_compact = false;
					dvp2axi_dev->stream[0].is_high_align = true;
				} else {
					dvp2axi_dev->stream[0].is_compact = true;
				}
			}
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[0], &pixm, false);
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[1], &pixm, false);
		} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
			if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE &&
			    priv->toisp_inf.link_mode == TOISP_UNITE) {
				if (is_uncompact) {
					dvp2axi_dev->stream[0].is_compact = false;
					dvp2axi_dev->stream[0].is_high_align = true;
					dvp2axi_dev->stream[1].is_compact = false;
					dvp2axi_dev->stream[1].is_high_align = true;
				} else {
					dvp2axi_dev->stream[0].is_compact = true;
					dvp2axi_dev->stream[1].is_compact = true;
				}
			}
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[0], &pixm, false);
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[1], &pixm, false);
			es_dvp2axi_set_fmt(&dvp2axi_dev->stream[2], &pixm, false);
		}
	} else {
		if (priv->sensor_sd) {
			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			fmt->pad = 0;
			ret = v4l2_subdev_call(priv->sensor_sd, pad, get_fmt,
					       NULL, fmt);
			if (ret) {
				v4l2_err(&priv->sd,
					 "%s: get sensor format failed\n",
					 __func__);
				return ret;
			}

			input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
			input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			input_sel.pad = 0;
			ret = v4l2_subdev_call(priv->sensor_sd, pad,
					       get_selection, NULL, &input_sel);
			if (!ret) {
				fmt->format.width = input_sel.r.width;
				fmt->format.height = input_sel.r.height;
			}
			priv->cap_info.width = fmt->format.width;
			priv->cap_info.height = fmt->format.height;
			pixm.pixelformat =
				es_dvp2axi_mbus_pixelcode_to_v4l2(fmt->format.code);
			pixm.width = priv->cap_info.width;
			pixm.height = priv->cap_info.height;
		} else {
			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			fmt->pad = 0;
			fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
			fmt->format.width = 3280;
			fmt->format.height = 2464;
		}
	}

	return 0;
}

static int sditf_init_buf(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	int ret = 0;

	if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
			if (dvp2axi_dev->is_thunderboot)
				dvp2axi_dev->resmem_size /= 2;
			ret = es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[0],
						priv->buf_num);
			if (dvp2axi_dev->is_thunderboot)
				dvp2axi_dev->resmem_pa += dvp2axi_dev->resmem_size;
			ret |= es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[1],
						 priv->buf_num);
		} else {
			ret = es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[0],
						priv->buf_num);
		}
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
			if (dvp2axi_dev->is_thunderboot)
				dvp2axi_dev->resmem_size /= 3;
			ret = es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[0],
						priv->buf_num);
			if (dvp2axi_dev->is_thunderboot)
				dvp2axi_dev->resmem_pa += dvp2axi_dev->resmem_size;
			ret |= es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[1],
						 priv->buf_num);
			if (dvp2axi_dev->is_thunderboot)
				dvp2axi_dev->resmem_pa += dvp2axi_dev->resmem_size;
			ret |= es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[2],
						 priv->buf_num);
		} else {
			ret = es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[0],
						priv->buf_num);
			ret |= es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[1],
						 priv->buf_num);
		}
	} else {
		if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO)
			ret = es_dvp2axi_init_rx_buf(&dvp2axi_dev->stream[0],
						priv->buf_num);
		else
			ret = -EINVAL;
	}
	return ret;
}

static void sditf_free_buf(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;

	if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[0],
				  dvp2axi_dev->stream[0].rx_buf_num);
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[1],
				  dvp2axi_dev->stream[1].rx_buf_num);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[0],
				  dvp2axi_dev->stream[0].rx_buf_num);
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[1],
				  dvp2axi_dev->stream[1].rx_buf_num);
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[2],
				  dvp2axi_dev->stream[2].rx_buf_num);
	} else {
		es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[0],
				  dvp2axi_dev->stream[0].rx_buf_num);
	}
	if (dvp2axi_dev->is_thunderboot) {
		dvp2axi_dev->wait_line_cache = 0;
		dvp2axi_dev->wait_line = 0;
		dvp2axi_dev->wait_line_bak = 0;
		dvp2axi_dev->is_thunderboot = false;
	}
}

static int sditf_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_selection *sel)
{
	return -EINVAL;
}

static void sditf_reinit_mode(struct sditf_priv *priv,
			      struct esisp_vicap_mode *mode)
{
	if (mode->rdbk_mode == ESISP_VICAP_RDBK_AIQ) {
		priv->toisp_inf.link_mode = TOISP_NONE;
	} else {
		if (strstr(mode->name, ESISP0_DEVNAME))
			priv->toisp_inf.link_mode = TOISP0;
		else if (strstr(mode->name, ESISP1_DEVNAME))
			priv->toisp_inf.link_mode = TOISP1;
		else if (strstr(mode->name, ESISP_UNITE_DEVNAME))
			priv->toisp_inf.link_mode = TOISP_UNITE;
		else
			priv->toisp_inf.link_mode = TOISP0;
	}

	v4l2_dbg(1, es_dvp2axi_debug, &priv->dvp2axi_dev->v4l2_dev,
		 "%s, mode->rdbk_mode %d, mode->name %s, link_mode %d\n",
		 __func__, mode->rdbk_mode, mode->name,
		 priv->toisp_inf.link_mode);
}

static void sditf_channel_disable(struct sditf_priv *priv, int user);
static long sditf_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct esisp_vicap_mode *mode;
	struct v4l2_subdev_format fmt;
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev *sensor_sd;
	int *pbuf_num = NULL;
	int ret = 0;
	int *on = NULL;

	switch (cmd) {
	case ESISP_VICAP_CMD_MODE:
		mode = (struct esisp_vicap_mode *)arg;
		mutex_lock(&dvp2axi_dev->stream_lock);
		memcpy(&priv->mode, mode, sizeof(*mode));
		mutex_unlock(&dvp2axi_dev->stream_lock);
		sditf_reinit_mode(priv, &priv->mode);
		if (priv->is_combine_mode)
			mode->input.merge_num = dvp2axi_dev->sditf_cnt;
		else
			mode->input.merge_num = 1;
		mode->input.index = priv->combine_index;
		return 0;
	case ESISP_VICAP_CMD_INIT_BUF:
		pbuf_num = (int *)arg;
		priv->buf_num = *pbuf_num;
		sditf_get_set_fmt(&priv->sd, NULL, &fmt);
		ret = sditf_init_buf(priv);
		return ret;
	case ESMODULE_GET_HDR_CFG:
		if (!dvp2axi_dev->terminal_sensor.sd)
			es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

		if (dvp2axi_dev->terminal_sensor.sd) {
			sensor_sd = dvp2axi_dev->terminal_sensor.sd;
			return v4l2_subdev_call(sensor_sd, core, ioctl, cmd,
						arg);
		}
		break;
	case ESISP_VICAP_CMD_QUICK_STREAM:
		on = (int *)arg;
		if (*on) {
			es_dvp2axi_stream_resume(dvp2axi_dev, ES_DVP2AXI_RESUME_ISP);
		} else {
			if (priv->toisp_inf.link_mode == TOISP0) {
				sditf_channel_disable(priv, 0);
			} else if (priv->toisp_inf.link_mode == TOISP1) {
				sditf_channel_disable(priv, 1);
			} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
				sditf_channel_disable(priv, 0);
				sditf_channel_disable(priv, 1);
			}
			es_dvp2axi_stream_suspend(dvp2axi_dev, ES_DVP2AXI_RESUME_ISP);
		}
		break;
	case ESISP_VICAP_CMD_SET_RESET:
		if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
			dvp2axi_dev->is_toisp_reset = true;
			return 0;
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long sditf_compat_ioctl32(struct v4l2_subdev *sd, unsigned int cmd,
				 unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev *sensor_sd;
	struct esisp_vicap_mode *mode;
	struct esmodule_hdr_cfg *hdr_cfg;
	int buf_num;
	int ret = 0;
	int on;

	switch (cmd) {
	case ESISP_VICAP_CMD_MODE:
		mode = kzalloc(sizeof(*mode), GFP_KERNEL);
		if (!mode) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(mode, up, sizeof(*mode))) {
			kfree(mode);
			return -EFAULT;
		}
		ret = sditf_ioctl(sd, cmd, mode);
		kfree(mode);
		return ret;
	case ESISP_VICAP_CMD_INIT_BUF:
		if (copy_from_user(&buf_num, up, sizeof(int)))
			return -EFAULT;
		ret = sditf_ioctl(sd, cmd, &buf_num);
		return ret;
	case ESMODULE_GET_HDR_CFG:
		hdr_cfg = kzalloc(sizeof(*hdr_cfg), GFP_KERNEL);
		if (!hdr_cfg) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(hdr_cfg, up, sizeof(*hdr_cfg))) {
			kfree(hdr_cfg);
			return -EFAULT;
		}
		ret = sditf_ioctl(sd, cmd, hdr_cfg);
		return ret;
	case ESISP_VICAP_CMD_QUICK_STREAM:
		if (copy_from_user(&on, up, sizeof(int)))
			return -EFAULT;
		ret = sditf_ioctl(sd, cmd, &on);
		return ret;
	case ESISP_VICAP_CMD_SET_RESET:
		ret = sditf_ioctl(sd, cmd, NULL);
		return ret;
	default:
		break;
	}

	if (!dvp2axi_dev->terminal_sensor.sd)
		es_dvp2axi_update_sensor_info(&dvp2axi_dev->stream[0]);

	if (dvp2axi_dev->terminal_sensor.sd) {
		sensor_sd = dvp2axi_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, core, compat_ioctl32, cmd,
					arg);
	}

	return -EINVAL;
}
#endif

static int sditf_channel_enable(struct sditf_priv *priv, int user)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct esmodule_capture_info *capture_info =
		&dvp2axi_dev->channels[0].capture_info;
	unsigned int ch0 = 0, ch1 = 0, ch2 = 0;
	unsigned int ctrl_val = 0;
	unsigned int int_en = 0;
	unsigned int offset_x = 0;
	unsigned int offset_y = 0;
	unsigned int width = priv->cap_info.width;
	unsigned int height = priv->cap_info.height;
	int csi_idx = dvp2axi_dev->csi_host_idx;

	if (capture_info->mode == ESMODULE_MULTI_DEV_COMBINE_ONE &&
	    priv->toisp_inf.link_mode == TOISP_UNITE) {
		if (capture_info->multi_dev.dev_num != 2 ||
		    capture_info->multi_dev.pixel_offset !=
			    ESMOUDLE_UNITE_EXTEND_PIXEL) {
			v4l2_err(
				&dvp2axi_dev->v4l2_dev,
				"param error of online mode, combine dev num %d, offset %d\n",
				capture_info->multi_dev.dev_num,
				capture_info->multi_dev.pixel_offset);
			return -EINVAL;
		}
		csi_idx = capture_info->multi_dev.dev_idx[user];
	}

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		if (dvp2axi_dev->inf_id == ES_DVP2AXI_MIPI_LVDS)
			ch0 = csi_idx * 4;
		else
			ch0 = 24; //dvp
		ctrl_val = (ch0 << 3) | 0x1;
		if (user == 0)
			int_en = DVP2AXI_TOISP0_FS(0) | DVP2AXI_TOISP0_FE(0);
		else
			int_en = DVP2AXI_TOISP1_FS(0) | DVP2AXI_TOISP1_FE(0);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		ch0 = dvp2axi_dev->csi_host_idx * 4 + 1;
		ch1 = dvp2axi_dev->csi_host_idx * 4;
		ctrl_val = (ch0 << 3) | 0x1;
		ctrl_val |= (ch1 << 11) | 0x100;
		if (user == 0)
			int_en = DVP2AXI_TOISP0_FS(0) | DVP2AXI_TOISP0_FS(1) |
				 DVP2AXI_TOISP0_FE(0) | DVP2AXI_TOISP0_FE(1);
		else
			int_en = DVP2AXI_TOISP1_FS(0) | DVP2AXI_TOISP1_FS(1) |
				 DVP2AXI_TOISP1_FE(0) | DVP2AXI_TOISP1_FE(1);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
		priv->toisp_inf.ch_info[1].is_valid = true;
		priv->toisp_inf.ch_info[1].id = ch1;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		ch0 = dvp2axi_dev->csi_host_idx * 4 + 2;
		ch1 = dvp2axi_dev->csi_host_idx * 4 + 1;
		ch2 = dvp2axi_dev->csi_host_idx * 4;
		ctrl_val = (ch0 << 3) | 0x1;
		ctrl_val |= (ch1 << 11) | 0x100;
		ctrl_val |= (ch2 << 19) | 0x10000;
		if (user == 0)
			int_en = DVP2AXI_TOISP0_FS(0) | DVP2AXI_TOISP0_FS(1) |
				 DVP2AXI_TOISP0_FS(2) | DVP2AXI_TOISP0_FE(0) |
				 DVP2AXI_TOISP0_FE(1) | DVP2AXI_TOISP0_FE(2);
		else
			int_en = DVP2AXI_TOISP1_FS(0) | DVP2AXI_TOISP1_FS(1) |
				 DVP2AXI_TOISP1_FS(2) | DVP2AXI_TOISP1_FE(0) |
				 DVP2AXI_TOISP1_FE(1) | DVP2AXI_TOISP1_FE(2);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
		priv->toisp_inf.ch_info[1].is_valid = true;
		priv->toisp_inf.ch_info[1].id = ch1;
		priv->toisp_inf.ch_info[2].is_valid = true;
		priv->toisp_inf.ch_info[2].id = ch2;
	}
	if (user == 0) {
		if (priv->toisp_inf.link_mode == TOISP_UNITE)
			width = priv->cap_info.width / 2 +
				ESMOUDLE_UNITE_EXTEND_PIXEL;
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP0_CTRL, ctrl_val);
		if (width && height) {
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP0_CROP,
					     offset_x | (offset_y << 16));
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP0_SIZE,
					     width | (height << 16));
		} else {
			return -EINVAL;
		}
	} else {
		if (priv->toisp_inf.link_mode == TOISP_UNITE) {
			if (capture_info->mode ==
			    ESMODULE_MULTI_DEV_COMBINE_ONE)
				offset_x = 0;
			else
				offset_x = priv->cap_info.width / 2 -
					   ESMOUDLE_UNITE_EXTEND_PIXEL;
			width = priv->cap_info.width / 2 +
				ESMOUDLE_UNITE_EXTEND_PIXEL;
		}
		es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP1_CTRL, ctrl_val);
		if (width && height) {
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP1_CROP,
					     offset_x | (offset_y << 16));
			es_dvp2axi_write_register(dvp2axi_dev, DVP2AXI_REG_TOISP1_SIZE,
					     width | (height << 16));
		} else {
			return -EINVAL;
		}
	}

	es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_GLB_INTEN, int_en);
	return 0;
}

static void sditf_channel_disable(struct sditf_priv *priv, int user)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	unsigned int ctrl_val = 0;

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		if (user == 0)
			ctrl_val = DVP2AXI_TOISP0_FE(0);
		else
			ctrl_val = DVP2AXI_TOISP1_FE(0);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		if (user == 0)
			ctrl_val = DVP2AXI_TOISP0_FE(0) | DVP2AXI_TOISP0_FE(1);
		else
			ctrl_val = DVP2AXI_TOISP1_FE(0) | DVP2AXI_TOISP1_FE(1);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		if (user == 0)
			ctrl_val = DVP2AXI_TOISP0_FE(0) | DVP2AXI_TOISP0_FE(1) |
				   DVP2AXI_TOISP0_FE(2);
		else
			ctrl_val = DVP2AXI_TOISP1_FE(0) | DVP2AXI_TOISP1_FE(1) |
				   DVP2AXI_TOISP1_FE(2);
	}

	es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_GLB_INTEN, ctrl_val);

	priv->toisp_inf.ch_info[0].is_valid = false;
	priv->toisp_inf.ch_info[1].is_valid = false;
	priv->toisp_inf.ch_info[2].is_valid = false;
}

void sditf_change_to_online(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;

	priv->mode.rdbk_mode = ESISP_VICAP_ONLINE;
	if (priv->toisp_inf.link_mode == TOISP0) {
		sditf_channel_enable(priv, 0);
	} else if (priv->toisp_inf.link_mode == TOISP1) {
		sditf_channel_enable(priv, 1);
	} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
		sditf_channel_enable(priv, 0);
		sditf_channel_enable(priv, 1);
	}

	if (dvp2axi_dev->is_thunderboot) {
		if (priv->hdr_cfg.hdr_mode == NO_HDR) {
			es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[0],
					  dvp2axi_dev->stream[0].rx_buf_num);
			dvp2axi_dev->stream[0].is_line_wake_up = false;
		} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
			es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[1],
					  dvp2axi_dev->stream[1].rx_buf_num);
			dvp2axi_dev->stream[0].is_line_wake_up = false;
			dvp2axi_dev->stream[1].is_line_wake_up = false;
		} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
			es_dvp2axi_free_rx_buf(&dvp2axi_dev->stream[2],
					  dvp2axi_dev->stream[2].rx_buf_num);
			dvp2axi_dev->stream[0].is_line_wake_up = false;
			dvp2axi_dev->stream[1].is_line_wake_up = false;
			dvp2axi_dev->stream[2].is_line_wake_up = false;
		}
		dvp2axi_dev->wait_line_cache = 0;
		dvp2axi_dev->wait_line = 0;
		dvp2axi_dev->wait_line_bak = 0;
		dvp2axi_dev->is_thunderboot = false;
	}
}

void sditf_disable_immediately(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	u32 ctrl_val = 0x10101;

	if (priv->toisp_inf.link_mode == TOISP0) {
		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_TOISP0_CTRL,
					 ~ctrl_val);
	} else if (priv->toisp_inf.link_mode == TOISP1) {
		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_TOISP1_CTRL,
					 ~ctrl_val);
	} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_TOISP0_CTRL,
					 ~ctrl_val);
		es_dvp2axi_write_register_and(dvp2axi_dev, DVP2AXI_REG_TOISP1_CTRL,
					 ~ctrl_val);
	}
}

static void sditf_check_capture_mode(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_device *dev = NULL;
	int i = 0;
	int toisp_cnt = 0;

	for (i = 0; i < dvp2axi_dev->hw_dev->dev_num; i++) {
		dev = dvp2axi_dev->hw_dev->dvp2axi_dev[i];
		if (dev && dev->sditf_cnt)
			toisp_cnt++;
	}
	if (dvp2axi_dev->is_thunderboot && toisp_cnt == 1)
		dvp2axi_dev->is_rdbk_to_online = true;
	else
		dvp2axi_dev->is_rdbk_to_online = false;
}

static int sditf_start_stream(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_subdev_format fmt;
	unsigned int mode = ES_DVP2AXI_STREAM_MODE_TOISP;

	sditf_check_capture_mode(dvp2axi_dev);
	sditf_get_set_fmt(&priv->sd, NULL, &fmt);
	if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
		if (priv->toisp_inf.link_mode == TOISP0) {
			sditf_channel_enable(priv, 0);
		} else if (priv->toisp_inf.link_mode == TOISP1) {
			sditf_channel_enable(priv, 1);
		} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
			sditf_channel_enable(priv, 0);
			sditf_channel_enable(priv, 1);
		}
		mode = ES_DVP2AXI_STREAM_MODE_TOISP;
	} else if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO) {
		mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;
	}

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[0], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[0], mode);
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[1], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[0], mode);
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[1], mode);
		es_dvp2axi_do_start_stream(&dvp2axi_dev->stream[2], mode);
	}
	INIT_LIST_HEAD(&priv->buf_free_list);
	return 0;
}

static int sditf_stop_stream(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	unsigned int mode = ES_DVP2AXI_STREAM_MODE_TOISP;

	if (priv->toisp_inf.link_mode == TOISP0) {
		sditf_channel_disable(priv, 0);
	} else if (priv->toisp_inf.link_mode == TOISP1) {
		sditf_channel_disable(priv, 1);
	} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
		sditf_channel_disable(priv, 0);
		sditf_channel_disable(priv, 1);
	}

	if (priv->mode.rdbk_mode == ESISP_VICAP_ONLINE)
		mode = ES_DVP2AXI_STREAM_MODE_TOISP;
	else if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AUTO)
		mode = ES_DVP2AXI_STREAM_MODE_TOISP_RDBK;

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[0], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[0], mode);
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[1], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[0], mode);
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[1], mode);
		es_dvp2axi_do_stop_stream(&dvp2axi_dev->stream[2], mode);
	}
	return 0;
}

static int sditf_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	int ret = 0;

	if (!on && atomic_dec_return(&priv->stream_cnt))
		return 0;

	if (on && atomic_inc_return(&priv->stream_cnt) > 1)
		return 0;

	if (dvp2axi_dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
		if (priv->mode.rdbk_mode == ESISP_VICAP_RDBK_AIQ)
			return 0;
		v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "%s, toisp mode %d, hdr %d, stream on %d\n", __func__,
			 priv->toisp_inf.link_mode, priv->hdr_cfg.hdr_mode, on);
		if (on) {
			ret = sditf_start_stream(priv);
		} else {
			ret = sditf_stop_stream(priv);
			sditf_free_buf(priv);
			priv->mode.rdbk_mode = ESISP_VICAP_RDBK_AIQ;
		}
	}
	if (on && ret)
		atomic_dec(&priv->stream_cnt);
	return ret;
}

static int sditf_s_power(struct v4l2_subdev *sd, int on)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct es_dvp2axi_vdev_node *node = &dvp2axi_dev->stream[0].vnode;
	int ret = 0;

	if (!on && atomic_dec_return(&priv->power_cnt))
		return 0;

	if (on && atomic_inc_return(&priv->power_cnt) > 1)
		return 0;

	if (dvp2axi_dev->chip_id >= CHIP_EIC770X_DVP2AXI) {
		v4l2_dbg(1, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "%s, toisp mode %d, hdr %d, set power %d\n", __func__,
			 priv->toisp_inf.link_mode, priv->hdr_cfg.hdr_mode, on);
		mutex_lock(&dvp2axi_dev->stream_lock);
		if (on) {
			ret = pm_runtime_resume_and_get(dvp2axi_dev->dev);
			ret |= v4l2_pipeline_pm_get(&node->vdev.entity);
		} else {
			v4l2_pipeline_pm_put(&node->vdev.entity);
			pm_runtime_put_sync(dvp2axi_dev->dev);
			priv->mode.rdbk_mode = ESISP_VICAP_RDBK_AIQ;
		}
		v4l2_info(&node->vdev, "s_power %d, entity use_count %d\n", on,
			  node->vdev.entity.use_count);
		mutex_unlock(&dvp2axi_dev->stream_lock);
	}
	return ret;
}

static int sditf_s_rx_buffer(struct v4l2_subdev *sd, void *buf,
			     unsigned int *size)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct es_dvp2axi_sensor_info *sensor = &dvp2axi_dev->terminal_sensor;
	struct es_dvp2axi_stream *stream = NULL;
	struct esisp_rx_buf *dbufs;
	struct es_dvp2axi_rx_buffer *rx_buf = NULL;
	unsigned long flags, buffree_flags;
	u32 diff_time = 1000000;
	u32 early_time = 0;
	bool is_free = false;
	bool is_single_dev = false;

	if (!buf) {
		v4l2_err(&dvp2axi_dev->v4l2_dev, "buf is NULL\n");
		return -EINVAL;
	}

	dbufs = buf;
	if (dvp2axi_dev->hdr.hdr_mode == NO_HDR) {
		if (dbufs->type == BUF_SHORT)
			stream = &dvp2axi_dev->stream[0];
		else
			return -EINVAL;
	} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X2) {
		if (dbufs->type == BUF_SHORT)
			stream = &dvp2axi_dev->stream[1];
		else if (dbufs->type == BUF_MIDDLE)
			stream = &dvp2axi_dev->stream[0];
		else
			return -EINVAL;
	} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X3) {
		if (dbufs->type == BUF_SHORT)
			stream = &dvp2axi_dev->stream[2];
		else if (dbufs->type == BUF_MIDDLE)
			stream = &dvp2axi_dev->stream[1];
		else if (dbufs->type == BUF_LONG)
			stream = &dvp2axi_dev->stream[0];
		else
			return -EINVAL;
	}

	if (!stream)
		return -EINVAL;

	rx_buf = to_dvp2axi_rx_buf(dbufs);
	v4l2_dbg(3, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev, "buf back to vicap 0x%x\n",
		 (u32)rx_buf->dummy.dma_addr);
	spin_lock_irqsave(&stream->vbq_lock, flags);
	stream->last_rx_buf_idx = dbufs->sequence + 1;
	atomic_inc(&stream->buf_cnt);

	is_single_dev = es_dvp2axi_check_single_dev_stream_on(dvp2axi_dev->hw_dev);
	if (!list_empty(&stream->rx_buf_head) && dvp2axi_dev->is_thunderboot &&
	    ((!dvp2axi_dev->is_rtt_suspend && !dvp2axi_dev->is_aov_reserved) ||
	     !is_single_dev) &&
	    (dbufs->type == BUF_SHORT ||
	     (dbufs->type != BUF_SHORT && (!dbufs->is_switch)))) {
		spin_lock_irqsave(&dvp2axi_dev->buffree_lock, buffree_flags);
		list_add_tail(&rx_buf->list_free, &priv->buf_free_list);
		spin_unlock_irqrestore(&dvp2axi_dev->buffree_lock, buffree_flags);
		atomic_dec(&stream->buf_cnt);
		stream->total_buf_num--;
		schedule_work(&priv->buffree_work.work);
		is_free = true;
	}

	if (!is_free && (!dbufs->is_switch)) {
		list_add_tail(&rx_buf->list, &stream->rx_buf_head);
		if (dvp2axi_dev->resume_mode != ESISP_RTT_MODE_ONE_FRAME) {
			es_dvp2axi_assign_check_buffer_update_toisp(stream);
			if (!stream->dma_en) {
				stream->to_en_dma = ES_DVP2AXI_DMAEN_BY_ISP;
				es_dvp2axi_enable_dma_capture(stream, true);
				dvp2axi_dev->sensor_work.on = 1;
				schedule_work(&dvp2axi_dev->sensor_work.work);
			}
		}
		if (dvp2axi_dev->rdbk_debug) {
			u32 offset = 0;

			offset = rx_buf->dummy.size -
				 stream->pixm.plane_fmt[0].bytesperline * 3;
			memset(rx_buf->dummy.vaddr + offset, 0x00,
			       stream->pixm.plane_fmt[0].bytesperline * 3);
			if (dvp2axi_dev->is_thunderboot ||
			    dvp2axi_dev->is_rtt_suspend || dvp2axi_dev->is_aov_reserved)
				dma_sync_single_for_device(
					dvp2axi_dev->dev,
					rx_buf->dummy.dma_addr +
						rx_buf->dummy.size -
						stream->pixm.plane_fmt[0]
								.bytesperline *
							3,
					stream->pixm.plane_fmt[0].bytesperline *
						3,
					DMA_FROM_DEVICE);
			else
				dvp2axi_dev->hw_dev->mem_ops->prepare(
					rx_buf->dummy.mem_priv);
		}
	}

	if (dbufs->is_switch && dbufs->type == BUF_SHORT) {
		if (stream->is_in_vblank)
			sditf_change_to_online(priv);
		else
			stream->is_change_toisp = true;
		v4l2_dbg(3, es_dvp2axi_debug, &dvp2axi_dev->v4l2_dev,
			 "switch to online mode\n");
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	if (!dvp2axi_dev->is_thunderboot || dvp2axi_dev->is_rdbk_to_online == false)
		return 0;

	if (dbufs->runtime_us && dvp2axi_dev->early_line == 0) {
		if (!dvp2axi_dev->sensor_linetime)
			dvp2axi_dev->sensor_linetime = es_dvp2axi_get_linetime(stream);
		dvp2axi_dev->isp_runtime_max = dbufs->runtime_us;
		if (dvp2axi_dev->is_thunderboot)
			diff_time = 200000;
		else
			diff_time = 1000000;
		if (dbufs->runtime_us * 1000 + dvp2axi_dev->sensor_linetime >
		    diff_time)
			early_time = dbufs->runtime_us * 1000 - diff_time;
		else
			early_time = diff_time;
		dvp2axi_dev->early_line =
			div_u64(early_time, dvp2axi_dev->sensor_linetime);
		dvp2axi_dev->wait_line_cache =
			sensor->raw_rect.height - dvp2axi_dev->early_line;
		if (dvp2axi_dev->rdbk_debug && dbufs->sequence < 15)
			v4l2_info(
				&dvp2axi_dev->v4l2_dev,
				"%s, isp runtime %d, line time %d, early_line %d, line_intr_cnt %d, seq %d, type %d, dma_addr %x\n",
				__func__, dbufs->runtime_us,
				dvp2axi_dev->sensor_linetime, dvp2axi_dev->early_line,
				dvp2axi_dev->wait_line_cache, dbufs->sequence,
				dbufs->type, (u32)rx_buf->dummy.dma_addr);
	} else {
		if (dbufs->runtime_us < dvp2axi_dev->isp_runtime_max) {
			dvp2axi_dev->isp_runtime_max = dbufs->runtime_us;
			if (dvp2axi_dev->is_thunderboot)
				diff_time = 200000;
			else
				diff_time = 1000000;
			if (dbufs->runtime_us * 1000 +
				    dvp2axi_dev->sensor_linetime >
			    diff_time)
				early_time =
					dbufs->runtime_us * 1000 - diff_time;
			else
				early_time = diff_time;
			dvp2axi_dev->early_line =
				div_u64(early_time, dvp2axi_dev->sensor_linetime);
			dvp2axi_dev->wait_line_cache =
				sensor->raw_rect.height - dvp2axi_dev->early_line;
		}
		if (dvp2axi_dev->rdbk_debug && dbufs->sequence < 15)
			v4l2_info(
				&dvp2axi_dev->v4l2_dev,
				"isp runtime %d, seq %d, type %d, early_line %d, dma addr %x\n",
				dbufs->runtime_us, dbufs->sequence, dbufs->type,
				dvp2axi_dev->early_line,
				(u32)rx_buf->dummy.dma_addr);
	}
	return 0;
}

static const struct v4l2_subdev_pad_ops sditf_subdev_pad_ops = {
	.set_fmt = sditf_get_set_fmt,
	.get_fmt = sditf_get_set_fmt,
	.get_selection = sditf_get_selection,
	.get_mbus_config = sditf_g_mbus_config,
};

static const struct v4l2_subdev_video_ops sditf_video_ops = {
	.g_frame_interval = sditf_g_frame_interval,
	.s_stream = sditf_s_stream,
	.s_rx_buffer = sditf_s_rx_buffer,
};

static const struct v4l2_subdev_core_ops sditf_core_ops = {
	.ioctl = sditf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sditf_compat_ioctl32,
#endif
	.s_power = sditf_s_power,
};

static const struct v4l2_subdev_ops sditf_subdev_ops = {
	.core = &sditf_core_ops,
	.video = &sditf_video_ops,
	.pad = &sditf_subdev_pad_ops,
};

static int es_dvp2axi_sditf_attach_dvp2axidev(struct sditf_priv *sditf)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct es_dvp2axi_device *dvp2axi_dev;

	np = of_parse_phandle(sditf->dev->of_node, "eci770x,dvp2axi", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(sditf->dev, "failed to get dvp2axi dev node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(sditf->dev, "failed to get dvp2axi dev from node\n");
		return -ENODEV;
	}

	dvp2axi_dev = platform_get_drvdata(pdev);
	if (!dvp2axi_dev) {
		dev_err(sditf->dev, "failed attach dvp2axi dev\n");
		return -EINVAL;
	}

	dvp2axi_dev->sditf[dvp2axi_dev->sditf_cnt] = sditf;
	sditf->dvp2axi_dev = dvp2axi_dev;
	dvp2axi_dev->sditf_cnt++;

	return 0;
}

struct sensor_async_subdev {
	struct v4l2_async_connection asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

// static int sditf_fwnode_parse(struct device *dev,
// 					  struct v4l2_fwnode_endpoint *vep,
// 					  struct v4l2_async_subdev *asd)
// {
// 	struct sensor_async_subdev *s_asd =
// 			container_of(asd, struct sensor_async_subdev, asd);
// 	struct v4l2_mbus_config *config = &s_asd->mbus;

// 	if (vep->base.port != 0) {
// 		dev_err(dev, "sditf has only port 0\n");
// 		return -EINVAL;
// 	}

// 	if (vep->bus_type == V4L2_MBUS_CSI2_DPHY ||
// 	    vep->bus_type == V4L2_MBUS_CSI2_CPHY) {
// 		config->type = vep->bus_type;
// 		config->bus.mipi_csi2.flags = vep->bus.mipi_csi2.flags;
// 		s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
// 	} else if (vep->bus_type == V4L2_MBUS_CCP2) {
// 		config->type = vep->bus_type;
// 		s_asd->lanes = vep->bus.mipi_csi1.data_lane;
// 	} else {
// 		dev_err(dev, "type is not supported\n");
// 		return -EINVAL;
// 	}

// 	return 0;
// }

static int sditf_fwnode_parse(struct sditf_priv *sditf)
{
	struct device *dev = sditf->dev;
	struct fwnode_handle *ep = NULL;
	struct v4l2_async_connection *s_asd = NULL;
	struct fwnode_handle *remote_ep = NULL;
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	int ret = 0;

	fwnode_graph_for_each_endpoint(dev_fwnode(dev), ep) {
		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret)
			goto err_parse;

		/* only add fwnode form port 0 to notifier list */
		if (vep.base.port != 0)
			continue;

		remote_ep = fwnode_graph_get_remote_port_parent(ep);

		/* skip device dts status is disabled */
		if (!fwnode_device_is_available(remote_ep)) {
			fwnode_handle_put(remote_ep);
			continue;
		}

		fwnode_handle_put(remote_ep);

		v4l2_async_subdev_nf_init(&sditf->notifier, &sditf->sd);

		s_asd = v4l2_async_nf_add_fwnode_remote(
			&sditf->notifier, ep, struct v4l2_async_connection);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}

		// if (vep->bus_type == V4L2_MBUS_CSI2_DPHY ||
		//     vep->bus_type == V4L2_MBUS_CSI2_CPHY) {
		// 	config->type = vep->bus_type;
		// 	config->bus.mipi_csi2.flags = vep->bus.mipi_csi2.flags;
		// 	s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
		// } else if (vep->bus_type == V4L2_MBUS_CCP2) {
		// 	config->type = vep->bus_type;
		// 	s_asd->lanes = vep->bus.mipi_csi1.data_lane;
		// } else {
		// 	dev_err(dev, "type is not supported\n");
		// 	return -EINVAL;
		// }
	}
	return 0;

err_parse:
	fwnode_handle_put(ep);
	return ret;
}

static int es_dvp2axi_sditf_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sditf_priv *priv =
		container_of(ctrl->handler, struct sditf_priv, ctrl_handler);
	struct v4l2_ctrl *sensor_ctrl = NULL;

	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		if (priv->dvp2axi_dev->terminal_sensor.sd) {
			sensor_ctrl = v4l2_ctrl_find(
				priv->dvp2axi_dev->terminal_sensor.sd->ctrl_handler,
				V4L2_CID_PIXEL_RATE);
			if (sensor_ctrl) {
				ctrl->val = v4l2_ctrl_g_ctrl_int64(sensor_ctrl);
				__v4l2_ctrl_s_ctrl_int64(priv->pixel_rate,
							 ctrl->val);
				v4l2_dbg(
					1, es_dvp2axi_debug,
					&priv->dvp2axi_dev->v4l2_dev,
					"%s, %s pixel rate %d\n", __func__,
					priv->dvp2axi_dev->terminal_sensor.sd->name,
					ctrl->val);
				return 0;
			} else {
				return -EINVAL;
			}
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops es_dvp2axi_sditf_ctrl_ops = {
	.g_volatile_ctrl = es_dvp2axi_sditf_get_ctrl,
};

static int sditf_notifier_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_connection *asd)
{
	struct sditf_priv *sditf =
		container_of(notifier, struct sditf_priv, notifier);
	struct media_entity *source_entity, *sink_entity;
	int ret = 0;

	sditf->sensor_sd = subdev;

	if (sditf->num_sensors == 1) {
		v4l2_err(subdev, "%s: the num of subdev is beyond %d\n",
			 __func__, sditf->num_sensors);
		return -EBUSY;
	}

	if (sditf->sd.entity.pads[0].flags & MEDIA_PAD_FL_SINK) {
		source_entity = &subdev->entity;
		sink_entity = &sditf->sd.entity;

		ret = media_create_pad_link(source_entity, 0, sink_entity, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			v4l2_err(&sditf->sd, "failed to create link for %s\n",
				 sditf->sensor_sd->name);
	}
	sditf->sensor_sd = subdev;
	++sditf->num_sensors;

	v4l2_err(subdev, "Async registered subdev\n");

	return 0;
}

static void sditf_notifier_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_connection *asd)
{
	struct sditf_priv *sditf =
		container_of(notifier, struct sditf_priv, notifier);

	sditf->sensor_sd = NULL;
}

static const struct v4l2_async_notifier_operations sditf_notifier_ops = {
	.bound = sditf_notifier_bound,
	.unbind = sditf_notifier_unbind,
};

static int sditf_subdev_notifier(struct sditf_priv *sditf)
{
	struct v4l2_async_notifier *ntf = &sditf->notifier;
	int ret;

	ret = sditf_fwnode_parse(sditf);
	if (ret < 0)
		return ret;

	sditf->sd.subdev_notifier = &sditf->notifier;
	sditf->notifier.ops = &sditf_notifier_ops;

	ret = v4l2_async_nf_register(ntf);
	if (ret) {
		dev_err(sditf->dev, "fail to register async notifier: %d\n",
			ret);
		v4l2_async_nf_cleanup(ntf);
	}

	return v4l2_async_register_subdev(&sditf->sd);
}

static int es_dvp2axi_subdev_media_init(struct sditf_priv *priv)
{
	struct es_dvp2axi_device *dvp2axi_dev = priv->dvp2axi_dev;
	struct v4l2_ctrl_handler *handler = &priv->ctrl_handler;
	unsigned long flags = V4L2_CTRL_FLAG_VOLATILE;
	int ret;
	int pad_num = 0;

	if (priv->is_combine_mode) {
		priv->pads[0].flags = MEDIA_PAD_FL_SINK;
		priv->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		pad_num = 2;
	} else {
		priv->pads[0].flags = MEDIA_PAD_FL_SOURCE;
		pad_num = 1;
	}
	priv->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_COMPOSER;
	ret = media_entity_pads_init(&priv->sd.entity, pad_num, priv->pads);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	priv->pixel_rate = v4l2_ctrl_new_std(handler, &es_dvp2axi_sditf_ctrl_ops,
					     V4L2_CID_PIXEL_RATE, 0,
					     SDITF_PIXEL_RATE_MAX, 1,
					     SDITF_PIXEL_RATE_MAX);
	if (priv->pixel_rate)
		priv->pixel_rate->flags |= flags;
	priv->sd.ctrl_handler = handler;
	if (handler->error) {
		v4l2_ctrl_handler_free(handler);
		return handler->error;
	}

	strncpy(priv->sd.name, dev_name(dvp2axi_dev->dev), sizeof(priv->sd.name));
	priv->cap_info.width = 0;
	priv->cap_info.height = 0;
	priv->mode.rdbk_mode = ESISP_VICAP_RDBK_AIQ;
	priv->toisp_inf.link_mode = TOISP_NONE;
	priv->toisp_inf.ch_info[0].is_valid = false;
	priv->toisp_inf.ch_info[1].is_valid = false;
	priv->toisp_inf.ch_info[2].is_valid = false;
	if (priv->is_combine_mode)
		sditf_subdev_notifier(priv);
	atomic_set(&priv->power_cnt, 0);
	atomic_set(&priv->stream_cnt, 0);
	INIT_WORK(&priv->buffree_work.work, sditf_buffree_work);
	INIT_LIST_HEAD(&priv->buf_free_list);
	return 0;
}

static int es_dvp2axi_subdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd;
	struct sditf_priv *priv;
	struct device_node *node = dev->of_node;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	sd = &priv->sd;
	v4l2_subdev_init(sd, &sditf_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "eic770x-dvp2axi-sditf");
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = es_dvp2axi_sditf_attach_dvp2axidev(priv);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "eic770x,combine-index",
				   &priv->combine_index);
	if (ret) {
		priv->is_combine_mode = false;
		priv->combine_index = 0;
	} else {
		priv->is_combine_mode = true;
	}
	ret = es_dvp2axi_subdev_media_init(priv);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&pdev->dev);
	return 0;
}

static int es_dvp2axi_subdev_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);

	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id es_dvp2axi_subdev_match_id[] = {
	{
		.compatible = "eic770x,es_dvp2axi-sditf",
	},
	{}
};
MODULE_DEVICE_TABLE(of, es_dvp2axi_subdev_match_id);

struct platform_driver es_dvp2axi_subdev_driver = {
	.probe = es_dvp2axi_subdev_probe,
	.remove = es_dvp2axi_subdev_remove,
	.driver = {
		.name = "es_dvp2axi_sditf",
		.of_match_table = es_dvp2axi_subdev_match_id,
	},
};
EXPORT_SYMBOL(es_dvp2axi_subdev_driver);

MODULE_AUTHOR("ESWIN VI team");
MODULE_DESCRIPTION("ESWIN DVP2AXI platform driver");
MODULE_LICENSE("GPL v2");
