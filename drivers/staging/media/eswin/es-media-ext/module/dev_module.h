#ifndef __DEV_MODULE__H
#define __DEV_MODULE__H

#include "dev_common.h"
#include <linux/fs.h>
#include <linux/types.h>
#include "es_media_ext_drv.h"

int dev_module_init(dev_minor_t minor);
void dev_module_deinit(void);

void dev_module_init_cdec(void *dev);

#endif
