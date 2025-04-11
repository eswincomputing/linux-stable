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

#ifndef _ES_DVP2AXI_SDITF_H
#define _ES_DVP2AXI_SDITF_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include "../../../../phy/eswin/es-camera-module.h"
#include "hw.h"
#include "isp_external.h"

#define ESISP0_DEVNAME "esisp0"
#define ESISP1_DEVNAME "esisp1"
#define ESISP_UNITE_DEVNAME "esisp-unite"

#define ES_DVP2AXI_TOISP_CH0	0
#define ES_DVP2AXI_TOISP_CH1	1
#define ES_DVP2AXI_TOISP_CH2	2
#define TOISP_CH_MAX 3

#define SDITF_PIXEL_RATE_MAX (1000000000)

struct capture_info {
	unsigned int offset_x;
	unsigned int offset_y;
	unsigned int width;
	unsigned int height;
};

enum toisp_link_mode {
	TOISP_NONE,
	TOISP0,
	TOISP1,
	TOISP_UNITE,
};

struct toisp_ch_info {
	bool is_valid;
	int id;
};

struct toisp_info {
	struct toisp_ch_info ch_info[TOISP_CH_MAX];
	enum toisp_link_mode link_mode;
};

struct sditf_work_struct {
	struct work_struct	work;
	struct esisp_rx_buffer *buf;
};

struct sditf_priv {
	struct device *dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev sd;
	struct media_pad pads[2];
	struct es_dvp2axi_device *dvp2axi_dev;
	struct esmodule_hdr_cfg	hdr_cfg;
	struct capture_info cap_info;
	struct esisp_vicap_mode mode;
	struct toisp_info toisp_inf;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_subdev *sensor_sd;
	struct sditf_work_struct buffree_work;
	struct list_head buf_free_list;
	int buf_num;
	int num_sensors;
	int combine_index;
	bool is_combine_mode;
	atomic_t power_cnt;
	atomic_t stream_cnt;
};

extern struct platform_driver es_dvp2axi_subdev_driver;
void sditf_change_to_online(struct sditf_priv *priv);
void sditf_disable_immediately(struct sditf_priv *priv);

#endif
