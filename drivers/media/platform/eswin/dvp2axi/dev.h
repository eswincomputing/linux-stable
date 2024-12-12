// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI dev driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.yy
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
#include <linux/es-camera-module.h>
// #include <linux/es-dvp2axi-config.h>
#include "../eswin_vi.h"
#include "hw.h"

#define DVP2AXI_DRIVER_NAME		"es_dvp2axi"
#define DVP2AXI_DRIVER_NAME_D1		"es_dvp2axi_d1"
#define DVP2AXI_VIDEODEVICE_NAME	"stream_dvp2axi"

#define OF_DVP2AXI_HDR_MODE     "eswin,dvp2axi-hdr-mode"

#define ES_DVP2AXI_SINGLE_STREAM	1
#define ES_DVP2AXI_MULTI_STREAM 6
#define ESDVP2AXI_MAX_STREAM_MIPI 6
#define ESDVP2AXI_STREAM_MIPI_ID0 0
#define ESDVP2AXI_STREAM_MIPI_ID1 1
#define ESDVP2AXI_STREAM_MIPI_ID2 2
#define ESDVP2AXI_STREAM_MIPI_ID3 3
#define ESDVP2AXI_STREAM_MIPI_ID4 4
#define ESDVP2AXI_STREAM_MIPI_ID5 5

#define ES_DVP2AXI_STREAM_DVP2AXI	0
#define DVP2AXI_MIPI_ID0_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id0"
#define DVP2AXI_MIPI_ID1_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id1"
#define DVP2AXI_MIPI_ID2_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id2"
#define DVP2AXI_MIPI_ID3_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id3"
#define DVP2AXI_MIPI_ID4_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id4"
#define DVP2AXI_MIPI_ID5_VDEV_NAME DVP2AXI_VIDEODEVICE_NAME	"_mipi_id5"

#define ES_DVP2AXI_PLANE_Y		0
#define ES_DVP2AXI_PLANE_CBCR	1

// #define ES_DVP2AXI_MAX_STREAM_MIPI	4
#define ES_DVP2AXI_MAX_STREAM_MIPI	1


#define ES_DVP2AXI_MAX_SENSOR	6
#define ES_DVP2AXI_MAX_CSI_CHANNEL	4
#define ES_DVP2AXI_MAX_PIPELINE	4

#define ES_DVP2AXI_DEFAULT_WIDTH	3280
#define ES_DVP2AXI_DEFAULT_HEIGHT	2464

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
};

enum es_dvp2axi_stream_mode {
	ES_DVP2AXI_STREAM_MODE_NONE       = 0x0,
	ES_DVP2AXI_STREAM_MODE_CAPTURE    = 0x01,
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

enum es_dvp2axi_inf_id {
	ES_DVP2AXI_DVP,
	ES_DVP2AXI_MIPI_LVDS,
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

struct es_dvp2axi_fps {
	int ch_num;
	int fps;
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
 * @frm_end_cnt: frame end count
 * @not_active_buf_cnt: not active buf count
 * @all_err_cnt: all err count
 * @
 */
struct es_dvp2axi_irq_stats {
	u64 frm_end_cnt[ES_DVP2AXI_MULTI_STREAM];
	u64 not_active_buf_cnt[ES_DVP2AXI_MULTI_STREAM];
	u64 all_err_cnt;
};


struct es_dvp2axi_skip_info {
	u8 cap_m;
	u8 skip_n;
	bool skip_en;
	bool skip_to_en;
	bool skip_to_dis;
};

struct es_dvp2axi_dummy_buffer {
	struct vb2_buffer vb;
	struct vb2_queue vb2_queue;
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	struct page **pages;
	void *mem_priv;
	void *vaddr;
	u32 size;
	int dma_fd;
	bool is_need_vaddr;
	bool is_need_dbuf;
	bool is_need_dmafd;
	bool is_free;
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
	enum dvp2axi_frame_ready	frame_phase;
	unsigned int			crop_mask;
	/* lock between irq and buf_queue */
	struct list_head		buf_head;
	struct es_dvp2axi_buffer		*curr_buf;
	struct es_dvp2axi_buffer		*next_buf;
	struct es_dvp2axi_buffer		*last_buf;

	spinlock_t vbq_lock; /* vfd lock */
	spinlock_t fps_lock;
	/* TODO: pad for dvp and mipi separately? */
	struct media_pad		pad;

	const struct dvp2axi_output_fmt	*dvp2axi_fmt_out;
	const struct dvp2axi_input_fmt	*dvp2axi_fmt_in;
	struct v4l2_pix_format_mplane	pixm;
	struct v4l2_rect		crop[CROP_SRC_MAX];
	struct es_dvp2axi_fps_stats		fps_stats;
	struct es_dvp2axi_readout_stats	readout;
	int				buf_owner;
	unsigned int			cur_stream_mode;
	int				total_buf_num;
	struct es_dvp2axi_skip_info		skip_info;
	struct tasklet_struct		vb_done_tasklet;
	struct list_head		vb_done_list;
	int				new_fource_idx;
	atomic_t			buf_cnt;
	u32				skip_frame;
	u32				cur_skip_frame;
	bool				stopping;
	bool				crop_enable;
	bool				crop_dyn_en;
	bool				is_compact;
	struct es_dvp2axi_dummy_buffer dummy_buf;
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

enum es_dvp2axi_resume_user {
	ES_DVP2AXI_RESUME_DVP2AXI,
	ES_DVP2AXI_RESUME_ISP,
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
	struct v4l2_device		*v4l2_dev;
	struct media_device		*media_dev;
	struct v4l2_async_notifier	notifier;

	struct v4l2_subdev       sd;
    struct v4l2_ctrl_handler ctrl_handler;

	int hdr_mode;

	struct es_dvp2axi_sensor_info	sensors[ES_DVP2AXI_MAX_SENSOR];
	u32				num_sensors;
	struct es_dvp2axi_sensor_info	*active_sensor;
	struct es_dvp2axi_sensor_info	terminal_sensor;

	struct es_dvp2axi_stream		stream[ES_DVP2AXI_MULTI_STREAM];
	struct es_dvp2axi_pipeline		pipe;

	struct csi_channel_info		channels[ES_DVP2AXI_MAX_CSI_CHANNEL];
	int				num_channels;
	int				chip_id;
	atomic_t			stream_cnt;
	atomic_t			power_cnt;
	atomic_t			streamoff_cnt;
	struct mutex			stream_lock; /* lock between streams */
	enum es_dvp2axi_workmode		workmode;
	struct esmodule_hdr_cfg		hdr;
	struct es_dvp2axi_hw *hw_dev;
	int inf_id;

	struct proc_dir_entry		*proc_dir;
	struct es_dvp2axi_irq_stats		irq_stats;
	spinlock_t			hdr_lock; /* lock for hdr buf sync */
	unsigned int			csi_host_idx;
	unsigned int			csi_host_idx_def;
	struct completion		cmpl_ntf;

	bool				iommu_en;
	bool				is_use_dummybuf;
	u32						dvp2axi_id;
	struct notifier_block of_notifier;
};

void dvp2axi_hw_soft_reset(struct es_dvp2axi_hw *es_dvp2axi_hw);
void dvp2axi_hw_irq_mask(struct es_dvp2axi_hw *dvp2axi_hw, u32 stream_id, int mask);
void dvp2axi_hw_irq_axi(struct es_dvp2axi_hw *dvp2axi_hw, int mask);

extern struct platform_driver es_dvp2axi_plat_drv;
#ifdef CONFIG_NUMA
extern struct platform_driver es_dvp2axi_plat_drv_d1;
#endif
void es_dvp2axi_set_fps(struct es_dvp2axi_stream *stream, struct es_dvp2axi_fps *fps);
int es_dvp2axi_do_start_stream(struct es_dvp2axi_stream *stream,
				enum es_dvp2axi_stream_mode mode);
void es_dvp2axi_do_stop_stream(struct es_dvp2axi_stream *stream,
				enum es_dvp2axi_stream_mode mode);
void es_dvp2axi_irq_handle_scale(struct es_dvp2axi_device *dvp2axi_dev,
				  unsigned int intstat_glb);
void es_dvp2axi_buf_queue(struct vb2_buffer *vb);

void es_dvp2axi_vb_done_tasklet(struct es_dvp2axi_stream *stream, struct es_dvp2axi_buffer *buf);

void dvp2axi_interrupt_handler(struct device *dev);

void es_irq_oneframe(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev);
void es_irq_err_handle(struct device *dev, struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_tasklet_err_handle(unsigned long data);

const struct
dvp2axi_input_fmt *es_dvp2axi_get_input_fmt(struct es_dvp2axi_device *dev,
				 struct v4l2_rect *rect,
				 u32 pad_id, struct csi_channel_info *csi_info);

void es_dvp2axi_unregister_stream_vdevs(struct es_dvp2axi_device *dev,
				   int stream_num);
int es_dvp2axi_register_stream_vdevs(struct es_dvp2axi_device *dev,
				int stream_num,
				bool is_multi_input);
void es_dvp2axi_stream_init(struct es_dvp2axi_device *dev, u32 id);
void es_dvp2axi_set_default_fmt(struct es_dvp2axi_device *dvp2axi_dev);
void es_dvp2axi_irq_oneframe(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_plat_init(struct es_dvp2axi_device *dvp2axi_dev, struct device_node *node, int inf_id);
int es_dvp2axi_plat_uninit(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_attach_hw(struct es_dvp2axi_device *dvp2axi_dev);
int es_dvp2axi_update_sensor_info(struct es_dvp2axi_stream *stream);
void es_dvp2axi_vb_done_oneframe(struct es_dvp2axi_stream *stream,
			    struct vb2_v4l2_buffer *vb_done);


int es_dvp2axi_set_fmt(struct es_dvp2axi_stream *stream,
		       struct v4l2_pix_format_mplane *pixm,
		       bool try);

u32 es_dvp2axi_mbus_pixelcode_to_v4l2(u32 pixelcode);

s32 es_dvp2axi_get_sensor_vblank_def(struct es_dvp2axi_device *dev);
s32 es_dvp2axi_get_sensor_vblank(struct es_dvp2axi_device *dev);

int es_dvp2axi_clr_unready_dev(void);

const struct
dvp2axi_output_fmt *es_dvp2axi_find_output_fmt(struct es_dvp2axi_stream *stream, u32 pixelfmt);

int es_dvp2axi_alloc_buffer(struct es_dvp2axi_device *dev,
		       struct es_dvp2axi_dummy_buffer *buf);
void es_dvp2axi_free_buffer(struct es_dvp2axi_device *dev,
			struct es_dvp2axi_dummy_buffer *buf);

int es_dvp2axi_stream_suspend(struct es_dvp2axi_device *dvp2axi_dev, int mode);
int es_dvp2axi_stream_resume(struct es_dvp2axi_device *dvp2axi_dev, int mode);

static inline u64 es_dvp2axi_time_get_ns(struct es_dvp2axi_device *dev)
{
	return ktime_get_ns();
}

#endif
