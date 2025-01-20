/* SPDX-License-Identifier: GPL-2.0 */
/*
 * eswin_vi.h - Header file for ESWIN Video Input Driver
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
};

/* Function prototypes */
int eswin_vi_register_device(struct eswin_vi_device *vi_dev);
void eswin_vi_unregister_device(struct eswin_vi_device *vi_dev);

#endif /* _ESWIN_VI_H_ */