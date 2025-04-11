// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI luma driver
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
#ifndef _ES_DVP2AXI_LUMA_H
#define _ES_DVP2AXI_LUMA_H

#include "es-isp1-config.h"
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "dev.h"

#define ES_DVP2AXI_LUMA_READOUT_WORK_SIZE	\
	(9 * sizeof(struct es_dvp2axi_luma_readout_work))
#define ES_DVP2AXI_LUMA_YSTAT_ISR_NUM	1
#define ES_DVP2AXI_RAW_MAX			3

struct es_dvp2axi_luma_vdev;
struct dvp2axi_input_fmt;

enum es_dvp2axi_luma_readout_cmd {
	ES_DVP2AXI_READOUT_LUMA,
};

enum es_dvp2axi_luma_frm_mode {
	ES_DVP2AXI_LUMA_ONEFRM,
	ES_DVP2AXI_LUMA_TWOFRM,
	ES_DVP2AXI_LUMA_THREEFRM,
};

struct es_dvp2axi_luma_readout_work {
	enum es_dvp2axi_luma_readout_cmd readout;
	unsigned long long timestamp;
	unsigned int meas_type;
	unsigned int frame_id;
	struct esisp_mipi_luma luma[ES_DVP2AXI_RAW_MAX];
};

struct es_dvp2axi_luma_node {
	struct vb2_queue buf_queue;
	/* vfd lock */
	struct mutex vlock;
	struct video_device vdev;
	struct media_pad pad;
};

/*
 * struct es_dvp2axi_luma_vdev - DVP2AXI Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct es_dvp2axi_luma_vdev {
	struct es_dvp2axi_luma_node vnode;
	struct es_dvp2axi_device *dvp2axidev;
	bool enable;

	spinlock_t irq_lock;	/* tasklet queue lock */
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool streamon;

	spinlock_t rd_lock;	/* buffer queue lock */
	struct kfifo rd_kfifo;
	struct tasklet_struct rd_tasklet;

	bool ystat_rdflg[ISP2X_MIPI_RAW_MAX];
	struct es_dvp2axi_luma_readout_work work;
};

void es_dvp2axi_start_luma(struct es_dvp2axi_luma_vdev *luma_vdev, const struct dvp2axi_input_fmt *dvp2axi_fmt_in);

void es_dvp2axi_stop_luma(struct es_dvp2axi_luma_vdev *luma_vdev);

void es_dvp2axi_luma_isr(struct es_dvp2axi_luma_vdev *luma_vdev, int isp_stat, u32 frame_id);

int es_dvp2axi_register_luma_vdev(struct es_dvp2axi_luma_vdev *luma_vdev,
			     struct v4l2_device *v4l2_dev,
			     struct es_dvp2axi_device *dev);

void es_dvp2axi_unregister_luma_vdev(struct es_dvp2axi_luma_vdev *luma_vdev);

#endif /* _ES_DVP2AXI_LUMA_H */
