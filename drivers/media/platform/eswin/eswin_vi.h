/* SPDX-License-Identifier: GPL-2.0 */
/*
 * eswin_vi.h - Header file for ESWIN Video Input Driver
 *
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 */

#ifndef _ESWIN_VI_H_
#define _ESWIN_VI_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>

/* Driver-specific macros */
#define ESWIN_VI_DRIVER_NAME "eswin_vi"
#define ESWIN_VI_MAX_INPUTS 4

enum ISP_CONNECT_MODE {
	ISP_CONNECT_MODE_0_2_4 = 0,
	ISP_CONNECT_MODE_0_2,
	ISP_CONNECT_MODE_0_1_2_3,
};

struct eswin_vi_clk_rst {
	struct device *dev; // used for tbu enanle/disable
	struct clk *aclk;
	struct clk *cfg_clk;
	struct clk *dw_aclk;
	struct clk *aclk_mux;
	struct clk *dw_mux;
	struct clk *spll0_fout1;
	struct clk *spll2_fout1;
	struct reset_control *rstc_axi;
	struct reset_control *rstc_cfg;
	struct reset_control *rstc_dwe;
};

/* Structure to represent a video input device */
struct eswin_vi_device {
	struct device *dev;               /* Pointer to device structure */
	struct video_device *vdev;        /* Video device structure */
	struct v4l2_device v4l2_dev;      /* V4L2 device structure */
	struct mutex lock;                /* Mutex for device operations */
	struct media_device *media_dev;   /* Media device structure */
	struct cdev es_vi_cdev;                 /* Character device structure */
	struct class  *es_vi_class;         /* Character device class */
	int input_count;                  /* Number of inputs supported */
	void __iomem		*base;      /* Base address of the device */
	struct regmap *syscrg_regmap;   /* System control register map */
	struct regmap *vitop_regmap;      /* vitop register map */
	struct eswin_vi_clk_rst clk_rst; /* Clock and reset control structure */
	u32 isp_dvp0_hor;               /* ISP DVP0 horizontal resolution */
	u32 isp_dvp0_ver;               /* ISP DVP0 vertical resolution */
	u32 phy_mode;                   /* phy connect mode */
	u32 isp_connect_mode;                   /* phy connect mode */
	bool wdr_en_2;                  /* hdr enable x2 mode*/
	int numa_id;
	struct notifier_block of_notifier;
	struct mutex vi_topcsr_lock;
};

extern int eic770x_vi_init(struct eswin_vi_device *es_vi_dev);
extern int eic770x_top_clk_init(struct eswin_vi_device *es_vi_dev);
extern int vitop_intf_cfg(struct eswin_vi_device *es_vi_dev);
#endif /* _ESWIN_VI_H_ */