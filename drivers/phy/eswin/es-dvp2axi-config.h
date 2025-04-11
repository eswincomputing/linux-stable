// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI config driver
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
 #ifndef _UAPI_ES_DVP2AXI_CONFIG_H
 #define _UAPI_ES_DVP2AXI_CONFIG_H
 
 #include <linux/types.h>
 #include <linux/v4l2-controls.h>
 
 #define ES_DVP2AXI_MAX_CSI_NUM		4
 
 #define ES_DVP2AXI_API_VERSION		KERNEL_VERSION(0, 2, 0)
 
 #define V4L2_EVENT_RESET_DEV		0X1001
 
 #define ES_DVP2AXI_CMD_GET_CSI_MEMORY_MODE \
     _IOR('V', BASE_VIDIOC_PRIVATE + 0, int)
 
 #define ES_DVP2AXI_CMD_SET_CSI_MEMORY_MODE \
     _IOW('V', BASE_VIDIOC_PRIVATE + 1, int)
 
 #define ES_DVP2AXI_CMD_GET_SCALE_BLC \
     _IOR('V', BASE_VIDIOC_PRIVATE + 2, struct bayer_blc)
 
 #define ES_DVP2AXI_CMD_SET_SCALE_BLC \
     _IOW('V', BASE_VIDIOC_PRIVATE + 3, struct bayer_blc)
 
 #define ES_DVP2AXI_CMD_SET_FPS \
     _IOW('V', BASE_VIDIOC_PRIVATE + 4, struct es_dvp2axi_fps)
 
 #define ES_DVP2AXI_CMD_SET_RESET \
     _IOW('V', BASE_VIDIOC_PRIVATE + 6, int)
 
 #define ES_DVP2AXI_CMD_SET_CSI_IDX \
     _IOW('V', BASE_VIDIOC_PRIVATE + 7, struct es_dvp2axi_csi_info)
 
 #define ES_DVP2AXI_CMD_SET_QUICK_STREAM \
     _IOWR('V', BASE_VIDIOC_PRIVATE + 8, struct es_dvp2axi_quick_stream_param)
 
 /* cif memory mode
  * 0: raw12/raw10/raw8 8bit memory compact
  * 1: raw12/raw10 16bit memory one pixel
  *    low align for rv1126/rv1109/rk356x
  *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
  *    | -| -| -| -|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
  * 2: raw12/raw10 16bit memory one pixel
  *    high align for rv1126/rv1109/rk356x
  *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
  *    |11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0| -| -| -| -|
  *
  * note: rv1109/rv1126/rk356x dvp only support uncompact mode,
  *       and can be set low align or high align
  */
 
 enum cif_csi_lvds_memory {
     CSI_LVDS_MEM_COMPACT = 0,
     CSI_LVDS_MEM_WORD_LOW_ALIGN = 1,
     CSI_LVDS_MEM_WORD_HIGH_ALIGN = 2,
 };
 
 /* black level for scale image
  * The sequence of pattern00~03 is the same as the output of sensor bayer
  */
 
 struct bayer_blc {
     __u8 pattern00;
     __u8 pattern01;
     __u8 pattern02;
     __u8 pattern03;
 };
 
 struct es_dvp2axi_fps {
     int ch_num;
     int fps;
 };
 
 struct es_dvp2axi_csi_info {
     int csi_num;
     int csi_idx[ES_DVP2AXI_MAX_CSI_NUM];
     int dphy_vendor[ES_DVP2AXI_MAX_CSI_NUM];
 };
 
 struct es_dvp2axi_quick_stream_param {
     int on;
     __u32 frame_num;
     int resume_mode;
 };
 
 #endif
 