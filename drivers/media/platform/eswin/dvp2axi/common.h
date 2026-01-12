// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI common driver
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

#ifndef _ES_DVP2AXI_COMMON_H
#define _ES_DVP2AXI_COMMON_H
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/media.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include "dev.h"

int es_dvp2axi_alloc_buffer(struct es_dvp2axi_device *dev,
		       struct es_dvp2axi_dummy_buffer *buf);
void es_dvp2axi_free_buffer(struct es_dvp2axi_device *dev,
			struct es_dvp2axi_dummy_buffer *buf);

#endif /* _ES_DVP2AXI_COMMON_H */

