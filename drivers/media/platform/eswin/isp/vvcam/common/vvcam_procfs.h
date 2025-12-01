#ifndef __VVCAM_PROCFS_H__
#define __VVCAM_PROCFS_H__
#include <linux/kernel.h>
#include <linux/device.h>

extern int vvcam_procfs_register(struct device *dev);

#endif