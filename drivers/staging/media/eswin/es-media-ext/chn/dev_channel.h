#ifndef __DEV_CHANNEL__H
#define __DEV_CHANNEL__H
#include "dev_common.h"
#include <linux/fs.h>
#include <linux/types.h>

int dev_channel_init(dev_minor_t minor);
void dev_channel_deinit(void);

void dev_channel_init_cdec(void *dev);

#endif
