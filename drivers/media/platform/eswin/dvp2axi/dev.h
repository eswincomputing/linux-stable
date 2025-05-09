// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI dev driver
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

#ifndef _ES_DVP2AXI_DEV_H
#define _ES_DVP2AXI_DEV_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/workqueue.h>
#include <media/eswin/eswin_vi.h>
#include "../../../../phy/eswin/es-camera-module.h"
#include "../../../../phy/eswin/es-dvp2axi-config.h"

#include "regs.h"
#include "version.h"
#include "dvp2axi-luma.h"
#include "hw.h"
#include "subdev-itf.h"

#define DVP2AXI_DRIVER_NAME		"es_dvp2axi"
#define DVP2AXI_VIDEODEVICE_NAME	"stream_dvp2axi"

#define OF_DVP2AXI_MONITOR_PARA	"eic770x,dvp2axi-monitor"
#define OF_DVP2AXI_WAIT_LINE	"wait-line"

#define DVP2AXI_MONITOR_PARA_NUM	(5)

#define ES_DVP2AXI_SINGLE_STREAM	1
#define ESDVP2AXI_MULTI_STREAM 6
#define ESDVP2AXI_MAX_STREAM_MIPI 6
#define ESDVP2AXI_STREAM_MIPI_ID0 0
#define ESDVP2AXI_STREAM_MIPI_ID1 1
#define ESDVP2AXI_STREAM_MIPI_ID2 2
#define ESDVP2AXI_STREAM_MIPI_ID3 3
#define ESDVP2AXI_STREAM_MIPI_ID4 4
#define ESDVP2AXI_STREAM_MIPI_ID5 5

#define ESDVP2AXI_STREAM_DVP	6

#define ES_DVP2AXI_STREAM_DVP2AXI	0
#define DVP2AXI_DVP_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME		"_dvp"
#define DVP2AXI_MIPI_ID0_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id0"
#define DVP2AXI_MIPI_ID1_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id1"
#define DVP2AXI_MIPI_ID2_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id2"
#define DVP2AXI_MIPI_ID3_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id3"
#define DVP2AXI_MIPI_ID4_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id4"
#define DVP2AXI_MIPI_ID5_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id5"

#define DVP2AXI_DVP_ID0_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_dvp_id0"
#define DVP2AXI_DVP_ID1_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_dvp_id1"
#define DVP2AXI_DVP_ID2_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_dvp_id2"
#define DVP2AXI_DVP_ID3_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_dvp_id3"

#define ES_DVP2AXI_PLANE_Y		0
#define ES_DVP2AXI_PLANE_CBCR	1

/*
 * ES1808 support 5 channel inputs simultaneously:
 * dvp + 4 mipi virtual channels;
 * RV1126/ES356X support 4 channels of BT.656/BT.1120/MIPI
 */
#define ES_DVP2AXI_MULTI_STREAMS_NUM	5
#define ES_DVP2AXI_STREAM_MIPI_ID0	0
#define ES_DVP2AXI_STREAM_MIPI_ID1	1
#define ES_DVP2AXI_STREAM_MIPI_ID2	2
#define ES_DVP2AXI_STREAM_MIPI_ID3	3
// #define ES_DVP2AXI_MAX_STREAM_MIPI	4
#define ES_DVP2AXI_MAX_STREAM_MIPI	1
#define ES_DVP2AXI_MAX_STREAM_LVDS	4
#define ES_DVP2AXI_MAX_STREAM_DVP	4
#define ES_DVP2AXI_STREAM_DVP	4

#define ES_DVP2AXI_MAX_SENSOR	6
#define ES_DVP2AXI_MAX_CSI_CHANNEL	4
#define ES_DVP2AXI_MAX_PIPELINE	4

#define ES_DVP2AXI_DEFAULT_WIDTH	3280
#define ES_DVP2AXI_DEFAULT_HEIGHT	2464
#define ES_DVP2AXI_FS_DETECTED_NUM	2

#define ES_DVP2AXI_MAX_INTERVAL_NS	5000000
/*
 * for HDR mode sync buf
 */
#define RDBK_MAX		3
#define RDBK_TOISP_MAX		2
#define RDBK_L			0
#define RDBK_M			1
#define RDBK_S			2

/*
 * for distinguishing cropping from senosr or usr
 */
#define CROP_SRC_SENSOR_MASK		(0x1 << 0)
#define CROP_SRC_USR_MASK		(0x1 << 1)

/*
 * max wait time for stream stop
 */
#define ES_DVP2AXI_STOP_MAX_WAIT_TIME_MS	(500)

#define ES_DVP2AXI_SKIP_FRAME_MAX		(16)

enum es_dvp2axi_workmode {
	ES_DVP2AXI_WORKMODE_ONEFRAME = 0x00,
	ES_DVP2AXI_WORKMODE_PINGPONG = 0x01,
	ES_DVP2AXI_WORKMODE_LINELOOP = 0x02
};

enum es_dvp2axi_stream_mode {
	ES_DVP2AXI_STREAM_MODE_NONE       = 0x0,
	ES_DVP2AXI_STREAM_MODE_CAPTURE    = 0x01,
	ES_DVP2AXI_STREAM_MODE_TOISP      = 0x02,
	ES_DVP2AXI_STREAM_MODE_TOSCALE    = 0x04,
	ES_DVP2AXI_STREAM_MODE_TOISP_RDBK = 0x08,
	ES_DVP2AXI_STREAM_MODE_ESKIT     = 0x10
};

enum es_dvp2axi_yuvaddr_state {
	ES_DVP2AXI_YUV_ADDR_STATE_UPDATE = 0x0,
	ES_DVP2AXI_YUV_ADDR_STATE_INIT = 0x1
};

enum es_dvp2axi_state {
	ES_DVP2AXI_STATE_DISABLED,
	ES_DVP2AXI_STATE_READY,
	ES_DVP2AXI_STATE_STREAMING,
	ES_DVP2AXI_STATE_RESET_IN_STREAMING,
};

enum es_dvp2axi_lvds_pad {
	ES_DVP2AXI_LVDS_PAD_SINK = 0x0,
	ES_DVP2AXI_LVDS_PAD_SRC_ID0,
	ES_DVP2AXI_LVDS_PAD_SRC_ID1,
	ES_DVP2AXI_LVDS_PAD_SRC_ID2,
	ES_DVP2AXI_LVDS_PAD_SRC_ID3,
	ES_DVP2AXI_LVDS_PAD_SCL_ID0,
	ES_DVP2AXI_LVDS_PAD_SCL_ID1,
	ES_DVP2AXI_LVDS_PAD_SCL_ID2,
	ES_DVP2AXI_LVDS_PAD_SCL_ID3,
	ES_DVP2AXI_LVDS_PAD_MAX,
};

enum es_dvp2axi_lvds_state {
	ES_DVP2AXI_LVDS_STOP = 0,
	ES_DVP2AXI_LVDS_START,
};

enum es_dvp2axi_inf_id {
	ES_DVP2AXI_DVP,
	ES_DVP2AXI_MIPI_LVDS,
};

enum es_dvp2axi_clk_edge {
	ES_DVP2AXI_CLK_RISING = 0x0,
	ES_DVP2AXI_CLK_FALLING,
};

/*
 * for distinguishing cropping from senosr or usr
 */
enum es_dvp2axi_crop_src {
	CROP_SRC_ACT	= 0x0,
	CROP_SRC_SENSOR,
	CROP_SRC_USR,
	CROP_SRC_MAX
};

/*
 * struct es_dvp2axi_pipeline - An DVP2AXI hardware pipeline
 *
 * Capture device call other devices via pipeline
 *
 * @num_subdevs: number of linked subdevs
 * @power_cnt: pipeline power count
 * @stream_cnt: stream power count
 */
struct es_dvp2axi_pipeline {
	struct media_pipeline pipe;
	int num_subdevs;
	atomic_t power_cnt;
	atomic_t stream_cnt;
	struct v4l2_subdev *subdevs[ES_DVP2AXI_MAX_PIPELINE];
	int (*open)(struct es_dvp2axi_pipeline *p,
		    struct media_entity *me, bool prepare);
	int (*close)(struct es_dvp2axi_pipeline *p);
	int (*set_stream)(struct es_dvp2axi_pipeline *p, bool on);
};

struct es_dvp2axi_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
	struct dma_buf *dbuf;
	u64 fe_timestamp;
};

struct es_dvp2axi_tools_buffer {
	struct vb2_v4l2_buffer *vb;
	struct esisp_rx_buf *dbufs;
	struct list_head list;
	u32 frame_idx;
	u64 timestamp;
	int use_cnt;
};

extern int es_dvp2axi_debug;

/*
 * struct es_dvp2axi_sensor_info - Sensor infomations
 * @sd: v4l2 subdev of sensor
 * @mbus: media bus configuration
 * @fi: v4l2 subdev frame interval
 * @lanes: lane num of sensor
 * @raw_rect: raw output rectangle of sensor, not crop or selection
 * @selection: selection info of sensor
 */
struct es_dvp2axi_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	struct v4l2_subdev_frame_interval fi;
	int lanes;
	struct v4l2_rect raw_rect;
	struct v4l2_subdev_selection selection;
	int dsi_input_en;
};

enum dvp2axi_fmt_type {
	DVP2AXI_FMT_TYPE_YUV = 0,
	DVP2AXI_FMT_TYPE_RAW,
};

/*
 * struct dvp2axi_output_fmt - The output format
 *
 * @bpp: bits per pixel for each cplanes
 * @fourcc: pixel format in fourcc
 * @fmt_val: the fmt val corresponding to DVP2AXI_FOR register
 * @csi_fmt_val: the fmt val corresponding to DVP2AXI_CSI_ID_CTRL
 * @cplanes: number of colour planes
 * @mplanes: number of planes for format
 * @raw_bpp: bits per pixel for raw format
 * @fmt_type: image format, raw or yuv
 */
struct dvp2axi_output_fmt {
	u8 bpp[VIDEO_MAX_PLANES];
	u32 fourcc;
	u32 fmt_val;
	u32 csi_fmt_val;
	u8 cplanes;
	u8 mplanes;
	u8 raw_bpp;
	enum dvp2axi_fmt_type fmt_type;
};

/*
 * struct dvp2axi_input_fmt - The input mbus format from sensor
 *
 * @mbus_code: mbus format
 * @dvp_fmt_val: the fmt val corresponding to DVP2AXI_FOR register
 * @csi_fmt_val: the fmt val corresponding to DVP2AXI_CSI_ID_CTRL
 * @fmt_type: image format, raw or yuv
 * @field: the field type of the input from sensor
 */
struct dvp2axi_input_fmt {
	u32 mbus_code;
	u32 dvp_fmt_val;
	u32 csi_fmt_val;
	u32 csi_yuv_order;
	enum dvp2axi_fmt_type fmt_type;
	enum v4l2_field field;
};

struct csi_channel_info {
	unsigned char id;
	unsigned char enable;	/* capture enable */
	unsigned char vc;
	unsigned char data_type;
	unsigned char data_bit;
	unsigned char crop_en;
	unsigned char cmd_mode_en;
	unsigned char fmt_val;
	unsigned char csi_fmt_val;
	unsigned int width;
	unsigned int height;
	unsigned int virtual_width;
	unsigned int left_virtual_width;
	unsigned int crop_st_x;
	unsigned int crop_st_y;
	unsigned int dsi_input;
	struct esmodule_lvds_cfg lvds_cfg;
	struct esmodule_capture_info capture_info;
};

struct es_dvp2axi_vdev_node {
	struct vb2_queue buf_queue;
	/* vfd lock */
	struct mutex vlock;
	struct video_device vdev;
	struct media_pad pad;
};

/*
 * the mark that csi frame0/1 interrupts enabled
 * in DVP2AXI_MIPI_INTEN
 */
enum dvp2axi_frame_ready {
	DVP2AXI_CSI_FRAME0_READY = 0,
	DVP2AXI_CSI_FRAME1_READY,
	DVP2AXI_CSI_FRAME2_READY,
	DVP2AXI_CSI_FRAME_UNREADY
};

/* struct es_dvp2axi_hdr - hdr configured
 * @op_mode: hdr optional mode
 */
struct es_dvp2axi_hdr {
	u8 mode;
};

/* struct es_dvp2axi_fps_stats - take notes on timestamp of buf
 * @frm0_timestamp: timesstamp of buf in frm0
 * @frm1_timestamp: timesstamp of buf in frm1
 */
struct es_dvp2axi_fps_stats {
	u64 frm0_timestamp;
	u64 frm1_timestamp;
};

/* struct es_dvp2axi_fps_stats - take notes on timestamp of buf
 * @fs_timestamp: timesstamp of frame start
 * @fe_timestamp: timesstamp of frame end
 * @wk_timestamp: timesstamp of buf send to user in wake up mode
 * @readout_time: one frame of readout time
 * @early_time: early time of buf send to user
 * @total_time: totaltime of readout time in hdr
 */
struct es_dvp2axi_readout_stats {
	u64 fs_timestamp;
	u64 fe_timestamp;
	u64 wk_timestamp;
	u64 readout_time;
	u64 early_time;
	u64 total_time;
};

/* struct es_dvp2axi_irq_stats - take notes on irq number
 * @csi_overflow_cnt: count of csi overflow irq
 * @csi_bwidth_lack_cnt: count of csi bandwidth lack irq
 * @dvp_bus_err_cnt: count of dvp bus err irq
 * @dvp_overflow_cnt: count dvp overflow irq
 * @dvp_line_err_cnt: count dvp line err irq
 * @dvp_pix_err_cnt: count dvp pix err irq
 * @all_frm_end_cnt: raw frame end count
 * @all_err_cnt: all err count
 * @
 */
struct es_dvp2axi_irq_stats {
	u64 csi_overflow_cnt;
	u64 csi_bwidth_lack_cnt;
	u64 csi_size_err_cnt;
	u64 dvp_bus_err_cnt;
	u64 dvp_overflow_cnt;
	u64 dvp_line_err_cnt;
	u64 dvp_pix_err_cnt;
	u64 dvp_size_err_cnt;
	u64 dvp_bwidth_lack_cnt;
	u64 frm_end_cnt[ES_DVP2AXI_MAX_STREAM_MIPI];
	u64 not_active_buf_cnt[ES_DVP2AXI_MAX_STREAM_MIPI];
	u64 trig_simult_cnt[ES_DVP2AXI_MAX_STREAM_MIPI];
	u64 all_err_cnt;
};

/*
 * the detecting mode of dvp2axi reset timer
 * related with dts property:eic770x,dvp2axi-monitor
 */
enum es_dvp2axi_monitor_mode {
	ES_DVP2AXI_MONITOR_MODE_IDLE = 0x0,
	ES_DVP2AXI_MONITOR_MODE_CONTINUE,
	ES_DVP2AXI_MONITOR_MODE_TRIGGER,
	ES_DVP2AXI_MONITOR_MODE_HOTPLUG,
};

/*
 * the parameters to resume when reset dvp2axi in running
 */
struct es_dvp2axi_resume_info {
	u32 frm_sync_seq;
};

struct es_dvp2axi_work_struct {
	struct work_struct	work;
	enum esmodule_reset_src	reset_src;
	struct es_dvp2axi_resume_info	resume_info;
};

struct es_dvp2axi_timer {
	struct timer_list	timer;
	spinlock_t		timer_lock;
	spinlock_t		csi2_err_lock;
	unsigned long		cycle;
	/* unit: us */
	unsigned long		line_end_cycle;
	unsigned int		run_cnt;
	unsigned int		max_run_cnt;
	unsigned int		stop_index_of_run_cnt;
	unsigned int		last_buf_wakeup_cnt[ES_DVP2AXI_MAX_STREAM_MIPI];
	unsigned long		csi2_err_cnt_even;
	unsigned long		csi2_err_cnt_odd;
	unsigned int		csi2_err_ref_cnt;
	unsigned int		csi2_err_fs_fe_cnt;
	unsigned int		csi2_err_fs_fe_detect_cnt;
	unsigned int		frm_num_of_monitor_cycle;
	unsigned int		triggered_frame_num;
	unsigned int		vts;
	unsigned int		raw_height;
	/* unit: ms */
	unsigned int		err_time_interval;
	unsigned int		csi2_err_triggered_cnt;
	unsigned int		notifer_called_cnt;
	unsigned long		frame_end_cycle_us;
	u64			csi2_first_err_timestamp;
	bool			is_triggered;
	bool			is_buf_stop_update;
	bool			is_running;
	bool			is_csi2_err_occurred;
	bool			has_been_init;
	bool			is_ctrl_by_user;
	enum es_dvp2axi_monitor_mode	monitor_mode;
	enum esmodule_reset_src	reset_src;
};

struct es_dvp2axi_extend_info {
	struct v4l2_pix_format_mplane	pixm;
	bool is_extended;
};

enum es_dvp2axi_capture_mode {
	ES_DVP2AXI_TO_DDR = 0,
	ES_DVP2AXI_TO_ISP_DDR,
	ES_DVP2AXI_TO_ISP_DMA,
};

/*
 * list: used for buf rotation
 * list_free: only used to release buf asynchronously
 */
struct es_dvp2axi_rx_buffer {
	int buf_idx;
	struct list_head list;
	struct list_head list_free;
	struct esisp_rx_buf dbufs;
	struct es_dvp2axi_dummy_buffer dummy;
	struct esisp_thunderboot_shmem shmem;
	u64 fe_timestamp;
};

enum es_dvp2axi_dma_en_mode {
	ES_DVP2AXI_DMAEN_BY_VICAP = 0x1,
	ES_DVP2AXI_DMAEN_BY_ISP = 0x2,
	ES_DVP2AXI_DMAEN_BY_VICAP_TO_ISP = 0x4,
	ES_DVP2AXI_DMAEN_BY_ISP_TO_VICAP = 0x8,
	ES_DVP2AXI_DMAEN_BY_ESKIT = 0x10,
};

struct es_dvp2axi_skip_info {
	u8 cap_m;
	u8 skip_n;
	bool skip_en;
	bool skip_to_en;
	bool skip_to_dis;
};

struct es_dvp2axi_sync_cfg {
	u32 type;
	u32 group;
};

enum es_dvp2axi_toisp_buf_update_state {
	ES_DVP2AXI_TOISP_BUF_ROTATE,
	ES_DVP2AXI_TOISP_BUF_THESAME,
	ES_DVP2AXI_TOISP_BUF_LOSS,
};

struct es_dvp2axi_toisp_buf_state {
	enum es_dvp2axi_toisp_buf_update_state state;
	int check_cnt;
	bool is_early_update;
};

/*
 * struct es_dvp2axi_stream - Stream states TODO
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 * @dummy_buf: dummy space to store dropped data
 * @crop_enable: crop status when stream off
 * @crop_dyn_en: crop status when streaming
 * es_dvp2axi use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 * @fps_lock: to protect parameters about calculating fps
 */
struct es_dvp2axi_stream {
	unsigned id:3;
	struct es_dvp2axi_device		*dvp2axidev;
	struct es_dvp2axi_vdev_node		vnode;
	enum es_dvp2axi_state		state;
	wait_queue_head_t		wq_stopped;
	unsigned int			frame_idx;
	int				frame_phase;
	int				frame_phase_cache;
	unsigned int			crop_mask;
	/* lock between irq and buf_queue */
	struct list_head		buf_head;
	struct es_dvp2axi_buffer		*curr_buf;
	struct es_dvp2axi_buffer		*next_buf;
	struct es_dvp2axi_buffer		*last_buf;
	struct es_dvp2axi_rx_buffer		*curr_buf_toisp;
	struct es_dvp2axi_rx_buffer		*next_buf_toisp;
	struct es_dvp2axi_rx_buffer		*last_buf_toisp;
	struct list_head		eskit_buf_head;
	struct es_dvp2axi_buffer		*curr_buf_eskit;
	struct es_dvp2axi_buffer		*next_buf_eskit;

	spinlock_t vbq_lock; /* vfd lock */
	spinlock_t fps_lock;
	/* TODO: pad for dvp and mipi separately? */
	struct media_pad		pad;

	const struct dvp2axi_output_fmt	*dvp2axi_fmt_out;
	const struct dvp2axi_input_fmt	*dvp2axi_fmt_in;
	struct v4l2_pix_format_mplane	pixm;
	struct v4l2_rect		crop[CROP_SRC_MAX];
	struct es_dvp2axi_fps_stats		fps_stats;
	struct es_dvp2axi_extend_info	extend_line;
	struct es_dvp2axi_readout_stats	readout;
	unsigned int			fs_cnt_in_single_frame;
	unsigned int			capture_mode;
	struct es_dvp2axi_scale_vdev		*scale_vdev;
	struct es_dvp2axi_tools_vdev		*tools_vdev;
	int				dma_en;
	int				to_en_dma;
	int				to_stop_dma;
	int				buf_owner;
	int				buf_replace_cnt;
	struct list_head		rx_buf_head_vicap;
	unsigned int			cur_stream_mode;
	struct es_dvp2axi_rx_buffer		rx_buf[ESISP_VICAP_BUF_CNT_MAX];
	struct list_head		rx_buf_head;
	int				total_buf_num;
	int				rx_buf_num;
	u64				line_int_cnt;
	int				lack_buf_cnt;
	unsigned int			buf_wake_up_cnt;
	struct es_dvp2axi_skip_info		skip_info;
	struct tasklet_struct		vb_done_tasklet;
	struct list_head		vb_done_list;
	int				last_rx_buf_idx;
	int				last_frame_idx;
	int				new_fource_idx;
	atomic_t			buf_cnt;
	struct completion		stop_complete;
	struct es_dvp2axi_toisp_buf_state	toisp_buf_state;
	u32				skip_frame;
	u32				cur_skip_frame;
	bool				stopping;
	bool				crop_enable;
	bool				crop_dyn_en;
	bool				is_compact;
	bool				is_dvp_yuv_addr_init;
	bool				is_fs_fe_not_paired;
	bool				is_line_wake_up;
	bool				is_line_inten;
	bool				is_can_stop;
	bool				is_buf_active;
	bool				is_high_align;
	bool				to_en_scale;
	bool				is_finish_stop_dma;
	bool				is_in_vblank;
	bool				is_change_toisp;
	bool				is_stop_capture;
	bool				is_wait_dma_stop;
	bool				is_single_cap;
	bool				is_wait_stop_complete;

	bool 				is_first_flush;
	bool				is_dummy_buffer;
};

struct es_dvp2axi_lvds_subdev {
	struct es_dvp2axi_device	*dvp2axidev;
	struct v4l2_subdev sd;
	struct v4l2_subdev *remote_sd;
	struct media_pad pads[ES_DVP2AXI_LVDS_PAD_MAX];
	struct v4l2_mbus_framefmt in_fmt;
	struct v4l2_rect crop;
	const struct dvp2axi_output_fmt	*dvp2axi_fmt_out;
	const struct dvp2axi_input_fmt	*dvp2axi_fmt_in;
	enum es_dvp2axi_lvds_state		state;
	struct es_dvp2axi_sensor_info	sensor_self;
	atomic_t			frm_sync_seq;
};

struct es_dvp2axi_dvp_sof_subdev {
	struct es_dvp2axi_device *dvp2axidev;
	struct v4l2_subdev sd;
	atomic_t			frm_sync_seq;
};

static inline struct es_dvp2axi_buffer *to_es_dvp2axi_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct es_dvp2axi_buffer, vb);
}

static inline
struct es_dvp2axi_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct es_dvp2axi_vdev_node, vdev);
}

static inline
struct es_dvp2axi_stream *to_es_dvp2axi_stream(struct es_dvp2axi_vdev_node *vnode)
{
	return container_of(vnode, struct es_dvp2axi_stream, vnode);
}

static inline struct es_dvp2axi_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct es_dvp2axi_vdev_node, buf_queue);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct es_dvp2axi_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

#define SCALE_DRIVER_NAME		"es_dvp2axi_scale"

#define ES_DVP2AXI_SCALE_CH0		0
#define ES_DVP2AXI_SCALE_CH1		1
#define ES_DVP2AXI_SCALE_CH2		2
#define ES_DVP2AXI_SCALE_CH3		3
#define ES_DVP2AXI_MAX_SCALE_CH	4

#define DVP2AXI_SCALE_CH0_VDEV_NAME DVP2AXI_DRIVER_NAME	"_scale_ch0"
#define DVP2AXI_SCALE_CH1_VDEV_NAME DVP2AXI_DRIVER_NAME	"_scale_ch1"
#define DVP2AXI_SCALE_CH2_VDEV_NAME DVP2AXI_DRIVER_NAME	"_scale_ch2"
#define DVP2AXI_SCALE_CH3_VDEV_NAME DVP2AXI_DRIVER_NAME	"_scale_ch3"

#define ES_DVP2AXI_SCALE_ENUM_SIZE_MAX	3
#define ES_DVP2AXI_MAX_SDITF			4

enum scale_ch_sw {
	SCALE_MIPI0_ID0,
	SCALE_MIPI0_ID1,
	SCALE_MIPI0_ID2,
	SCALE_MIPI0_ID3,
	SCALE_MIPI1_ID0,
	SCALE_MIPI1_ID1,
	SCALE_MIPI1_ID2,
	SCALE_MIPI1_ID3,
	SCALE_MIPI2_ID0,
	SCALE_MIPI2_ID1,
	SCALE_MIPI2_ID2,
	SCALE_MIPI2_ID3,
	SCALE_MIPI3_ID0,
	SCALE_MIPI3_ID1,
	SCALE_MIPI3_ID2,
	SCALE_MIPI3_ID3,
	SCALE_MIPI4_ID0,
	SCALE_MIPI4_ID1,
	SCALE_MIPI4_ID2,
	SCALE_MIPI4_ID3,
	SCALE_MIPI5_ID0,
	SCALE_MIPI5_ID1,
	SCALE_MIPI5_ID2,
	SCALE_MIPI5_ID3,
	SCALE_DVP,
	SCALE_CH_MAX,
};

enum scale_mode {
	SCALE_8TIMES,
	SCALE_16TIMES,
	SCALE_32TIMES,
};

struct es_dvp2axi_scale_ch_info {
	u32 width;
	u32 height;
	u32 vir_width;
};

struct es_dvp2axi_scale_src_res {
	u32 width;
	u32 height;
};

/*
 * struct es_dvp2axi_scale_vdev - DVP2AXI Capture device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct es_dvp2axi_scale_vdev {
	unsigned int ch:3;
	struct es_dvp2axi_device *dvp2axidev;
	struct es_dvp2axi_vdev_node vnode;
	struct es_dvp2axi_stream *stream;
	struct list_head buf_head;
	spinlock_t vbq_lock; /* vfd lock */
	wait_queue_head_t wq_stopped;
	struct v4l2_pix_format_mplane	pixm;
	const struct dvp2axi_output_fmt *scale_out_fmt;
	struct es_dvp2axi_scale_ch_info ch_info;
	struct es_dvp2axi_scale_src_res src_res;
	struct es_dvp2axi_buffer *curr_buf;
	struct es_dvp2axi_buffer *next_buf;
	struct bayer_blc blc;
	enum es_dvp2axi_state state;
	unsigned int ch_src;
	unsigned int scale_mode;
	int frame_phase;
	unsigned int frame_idx;
	bool stopping;
};

static inline
struct es_dvp2axi_scale_vdev *to_es_dvp2axi_scale_vdev(struct es_dvp2axi_vdev_node *vnode)
{
	return container_of(vnode, struct es_dvp2axi_scale_vdev, vnode);
}

void es_dvp2axi_init_scale_vdev(struct es_dvp2axi_device *dvp2axi_dev, u32 ch);
int es_dvp2axi_register_scale_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
				int stream_num,
				bool is_multi_input);
void es_dvp2axi_unregister_scale_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
				   int stream_num);

#define TOOLS_DRIVER_NAME		"es_dvp2axi_tools"

#define ES_DVP2AXI_TOOLS_CH0		0
#define ES_DVP2AXI_TOOLS_CH1		1
#define ES_DVP2AXI_TOOLS_CH2		2
#define ES_DVP2AXI_MAX_TOOLS_CH	3

#define DVP2AXI_TOOLS_CH0_VDEV_NAME DVP2AXI_DRIVER_NAME	"_tools_id0"
#define DVP2AXI_TOOLS_CH1_VDEV_NAME DVP2AXI_DRIVER_NAME	"_tools_id1"
#define DVP2AXI_TOOLS_CH2_VDEV_NAME DVP2AXI_DRIVER_NAME	"_tools_id2"

/*
 * struct es_dvp2axi_tools_vdev - DVP2AXI Capture device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct es_dvp2axi_tools_vdev {
	unsigned int ch:3;
	struct es_dvp2axi_device *dvp2axidev;
	struct es_dvp2axi_vdev_node vnode;
	struct es_dvp2axi_stream *stream;
	struct list_head buf_head;
	struct list_head buf_done_head;
	struct list_head src_buf_head;
	spinlock_t vbq_lock; /* vfd lock */
	wait_queue_head_t wq_stopped;
	struct v4l2_pix_format_mplane	pixm;
	const struct dvp2axi_output_fmt *tools_out_fmt;
	struct es_dvp2axi_buffer *curr_buf;
	struct work_struct work;
	enum es_dvp2axi_state state;
	int frame_phase;
	unsigned int frame_idx;
	bool stopping;
};

static inline
struct es_dvp2axi_tools_vdev *to_es_dvp2axi_tools_vdev(struct es_dvp2axi_vdev_node *vnode)
{
	return container_of(vnode, struct es_dvp2axi_tools_vdev, vnode);
}

void es_dvp2axi_init_tools_vdev(struct es_dvp2axi_device *dvp2axi_dev, u32 ch);
int es_dvp2axi_register_tools_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
				int stream_num,
				bool is_multi_input);
// void es_dvp2axi_unregister_tools_vdevs(struct es_dvp2axi_device *dvp2axi_dev,
// 				   int stream_num);

enum es_dvp2axi_err_state {
	ES_DVP2AXI_ERR_ID0_NOT_BUF = 0x1,
	ES_DVP2AXI_ERR_ID1_NOT_BUF = 0x2,
	ES_DVP2AXI_ERR_ID2_NOT_BUF = 0x4,
	ES_DVP2AXI_ERR_ID3_NOT_BUF = 0x8,
	ES_DVP2AXI_ERR_ID0_TRIG_SIMULT = 0x10,
	ES_DVP2AXI_ERR_ID1_TRIG_SIMULT = 0x20,
	ES_DVP2AXI_ERR_ID2_TRIG_SIMULT = 0x40,
	ES_DVP2AXI_ERR_ID3_TRIG_SIMULT = 0x80,
	ES_DVP2AXI_ERR_SIZE = 0x100,
	ES_DVP2AXI_ERR_OVERFLOW = 0x200,
	ES_DVP2AXI_ERR_BANDWIDTH_LACK = 0x400,
	ES_DVP2AXI_ERR_BUS = 0X800,
	ES_DVP2AXI_ERR_ID0_MULTI_FS = 0x1000,
	ES_DVP2AXI_ERR_ID1_MULTI_FS = 0x2000,
	ES_DVP2AXI_ERR_ID2_MULTI_FS = 0x4000,
	ES_DVP2AXI_ERR_ID3_MULTI_FS = 0x8000,
	ES_DVP2AXI_ERR_PIXEL = 0x10000,
	ES_DVP2AXI_ERR_LINE = 0x20000,
};

struct es_dvp2axi_err_state_work {
	struct work_struct	work;
	u64 last_timestamp;
	u32 err_state;
	u32 intstat;
	u32 lastline;
	u32 lastpixel;
	u32 size_id0;
	u32 size_id1;
	u32 size_id2;
	u32 size_id3;
};

enum es_dvp2axi_resume_user {
	ES_DVP2AXI_RESUME_DVP2AXI,
	ES_DVP2AXI_RESUME_ISP,
};

struct es_dvp2axi_sensor_work {
	struct work_struct work;
	int on;
};

/*
 * struct es_dvp2axi_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @stream: capture video device
 */
struct es_dvp2axi_device {
	struct list_head		list;
	struct device			*dev;
	struct v4l2_device		v4l2_dev;
	struct media_device		*media_dev;
	struct v4l2_async_notifier	notifier;

	struct es_dvp2axi_sensor_info	sensors[ES_DVP2AXI_MAX_SENSOR];
	u32				num_sensors;
	struct es_dvp2axi_sensor_info	*active_sensor;
	struct es_dvp2axi_sensor_info	terminal_sensor;

	struct es_dvp2axi_stream		stream[ESDVP2AXI_MULTI_STREAM];
	struct es_dvp2axi_scale_vdev		scale_vdev[ESDVP2AXI_MULTI_STREAM];
	struct es_dvp2axi_tools_vdev		tools_vdev[ES_DVP2AXI_MAX_TOOLS_CH];
	struct es_dvp2axi_pipeline		pipe;

	struct csi_channel_info		channels[ES_DVP2AXI_MAX_CSI_CHANNEL];
	int				num_channels;
	int				chip_id;
	atomic_t			stream_cnt;
	atomic_t			power_cnt;
	atomic_t			streamoff_cnt;
	struct mutex			stream_lock; /* lock between streams */
	struct mutex			scale_lock; /* lock between scale dev */
	struct mutex			tools_lock; /* lock between tools dev */
	enum es_dvp2axi_workmode		workmode;
	bool				can_be_reset;
	struct esmodule_hdr_cfg		hdr;
	struct es_dvp2axi_buffer		*rdbk_buf[RDBK_MAX];
	struct es_dvp2axi_rx_buffer		*rdbk_rx_buf[RDBK_MAX];
	struct es_dvp2axi_luma_vdev		luma_vdev;
	struct es_dvp2axi_lvds_subdev	lvds_subdev;
	struct es_dvp2axi_dvp_sof_subdev	dvp_sof_subdev;
	struct es_dvp2axi_hw *hw_dev;
	irqreturn_t (*isr_hdl)(int irq, struct es_dvp2axi_device *dvp2axi_dev);
	int inf_id;

	struct sditf_priv		*sditf[ES_DVP2AXI_MAX_SDITF];
	struct proc_dir_entry		*proc_dir;
	struct es_dvp2axi_irq_stats		irq_stats;
	spinlock_t			hdr_lock; /* lock for hdr buf sync */
	spinlock_t			buffree_lock;
	struct es_dvp2axi_timer		reset_watchdog_timer;
	struct es_dvp2axi_work_struct	reset_work;
	int				id_use_cnt;
	unsigned int			csi_host_idx;
	unsigned int			csi_host_idx_def;
	unsigned int			dvp_sof_in_oneframe;
	unsigned int			wait_line;
	unsigned int			wait_line_bak;
	unsigned int			wait_line_cache;
	struct completion		cmpl_ntf;
	struct csi2_dphy_hw		*dphy_hw;
	phys_addr_t			resmem_pa;
	dma_addr_t			resmem_addr;
	size_t				resmem_size;
	// struct es_tb_client		tb_client;
	bool				is_start_hdr;
	bool				reset_work_cancel;
	bool				iommu_en;
	bool				is_use_dummybuf;
	bool				is_notifier_isp;
	bool				is_thunderboot;
	bool				is_rdbk_to_online;
	bool				is_support_tools;
	bool				is_rtt_suspend;
	bool				is_aov_reserved;
	bool				sensor_state_change;
	bool				is_toisp_reset;
	int				rdbk_debug;
	struct es_dvp2axi_sync_cfg		sync_cfg;
	int				sditf_cnt;
	u32				early_line;
	int				isp_runtime_max;
	int				sensor_linetime;
	u32				err_state;
	struct es_dvp2axi_err_state_work	err_state_work;
	struct es_dvp2axi_sensor_work	sensor_work;
	int				resume_mode;
	u32				nr_buf_size;
	u32				share_mem_size;
	u32				thunderboot_sensor_num;
	int				sensor_state;
	u32				intr_mask;
	struct delayed_work		work_deal_err;
};


void dvp2axi_hw_soft_reset(struct es_dvp2axi_hw *es_dvp2axi_hw);

void dvp2axi_hw_int_mask(struct es_dvp2axi_hw *dvp2axi_hw, int mask);

extern struct platform_driver es_dvp2axi_plat_drv;
void es_dvp2axi_set_fps(struct es_dvp2axi_stream *stream, struct es_dvp2axi_fps *fps);
int es_dvp2axi_do_start_stream(struct es_dvp2axi_stream *stream,
				enum es_dvp2axi_stream_mode mode);
void es_dvp2axi_do_stop_stream(struct es_dvp2axi_stream *stream,
				enum es_dvp2axi_stream_mode mode);
void es_dvp2axi_irq_handle_scale(struct es_dvp2axi_device *dvp2axi_dev,
				  unsigned int intstat_glb);
void es_dvp2axi_buf_queue(struct vb2_buffer *vb);

void es_dvp2axi_vb_done_tasklet(struct es_dvp2axi_stream *stream, struct es_dvp2axi_buffer *buf);

void es_irq_oneframe(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev);

void es_irq_oneframe_new(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev);

int es_dvp2axi_scale_start(struct es_dvp2axi_scale_vdev *scale_vdev);

const struct
dvp2axi_input_fmt *es_dvp2axi_get_input_fmt(struct es_dvp2axi_device *dev,
				 struct v4l2_rect *rect,
				 u32 pad_id, struct csi_channel_info *csi_info);

void es_dvp2axi_write_register(struct es_dvp2axi_device *dev,
			  enum dvp2axi_reg_index index, u32 val);
void es_dvp2axi_write_register_or(struct es_dvp2axi_device *dev,
			     enum dvp2axi_reg_index index, u32 val);
void es_dvp2axi_write_register_and(struct es_dvp2axi_device *dev,
			      enum dvp2axi_reg_index index, u32 val);
unsigned int es_dvp2axi_read_register(struct es_dvp2axi_device *dev,
				 enum dvp2axi_reg_index index);
void es_dvp2axi_write_grf_reg(struct es_dvp2axi_device *dev,
			 enum dvp2axi_reg_index index, u32 val);
u32 es_dvp2axi_read_grf_reg(struct es_dvp2axi_device *dev,
		       enum dvp2axi_reg_index index);
void es_dvp2axi_unregister_stream_vdevs(struct es_dvp2axi_device *dev,
				   int stream_num);
int es_dvp2axi_register_stream_vdevs(struct es_dvp2axi_device *dev,
				int stream_num,
				bool is_multi_input);
void es_dvp2axi_stream_init(struct es_dvp2axi_device *dev, u32 id);
void es_dvp2axi_set_default_fmt(struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_irq_oneframe(struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_irq_pingpong(struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_irq_pingpong_v1(struct es_dvp2axi_device *dvp2axi_dev);
unsigned int es_dvp2axi_irq_global(struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_irq_handle_toisp(struct es_dvp2axi_device *dvp2axi_dev, unsigned int intstat_glb);
int es_dvp2axi_register_lvds_subdev(struct es_dvp2axi_device *dev);
void es_dvp2axi_unregister_lvds_subdev(struct es_dvp2axi_device *dev);
int es_dvp2axi_register_dvp_sof_subdev(struct es_dvp2axi_device *dev);
void es_dvp2axi_unregister_dvp_sof_subdev(struct es_dvp2axi_device *dev);
void es_dvp2axi_irq_lite_lvds(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_plat_init(struct es_dvp2axi_device *dvp2axi_dev, struct device_node *node, int inf_id);
int es_dvp2axi_plat_uninit(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_attach_hw(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_update_sensor_info(struct es_dvp2axi_stream *stream);
int es_dvp2axi_reset_notifier(struct notifier_block *nb, unsigned long action, void *data);
void es_dvp2axi_reset_watchdog_timer_handler(struct timer_list *t);
void es_dvp2axi_config_dvp_clk_sampling_edge(struct es_dvp2axi_device *dev,
					enum es_dvp2axi_clk_edge edge);
void es_dvp2axi_enable_dvp_clk_dual_edge(struct es_dvp2axi_device *dev, bool on);
void es_dvp2axi_reset_work(struct work_struct *work);

void es_dvp2axi_vb_done_oneframe(struct es_dvp2axi_stream *stream,
			    struct vb2_v4l2_buffer *vb_done);

int es_dvp2axi_init_rx_buf(struct es_dvp2axi_stream *stream, int buf_num);
void es_dvp2axi_free_rx_buf(struct es_dvp2axi_stream *stream, int buf_num);

int es_dvp2axi_set_fmt(struct es_dvp2axi_stream *stream,
		       struct v4l2_pix_format_mplane *pixm,
		       bool try);
void es_dvp2axi_enable_dma_capture(struct es_dvp2axi_stream *stream, bool is_only_enable);

void es_dvp2axi_do_soft_reset(struct es_dvp2axi_device *dev);

u32 es_dvp2axi_mbus_pixelcode_to_v4l2(u32 pixelcode);

void es_dvp2axi_config_dvp_pin(struct es_dvp2axi_device *dev, bool on);

s32 es_dvp2axi_get_sensor_vblank_def(struct es_dvp2axi_device *dev);
s32 es_dvp2axi_get_sensor_vblank(struct es_dvp2axi_device *dev);
int es_dvp2axi_get_linetime(struct es_dvp2axi_stream *stream);

void es_dvp2axi_assign_check_buffer_update_toisp(struct es_dvp2axi_stream *stream);

struct es_dvp2axi_rx_buffer *to_dvp2axi_rx_buf(struct esisp_rx_buf *dbufs);

int es_dvp2axi_clr_unready_dev(void);

const struct
dvp2axi_output_fmt *es_dvp2axi_find_output_fmt(struct es_dvp2axi_stream *stream, u32 pixelfmt);

void es_dvp2axi_err_print_work(struct work_struct *work);
int es_dvp2axi_stream_suspend(struct es_dvp2axi_device *dvp2axi_dev, int mode);
int es_dvp2axi_stream_resume(struct es_dvp2axi_device *dvp2axi_dev, int mode);

static inline u64 es_dvp2axi_time_get_ns(struct es_dvp2axi_device *dev)
{
	return ktime_get_ns();
}

bool es_dvp2axi_check_single_dev_stream_on(struct es_dvp2axi_hw *hw);

#endif
