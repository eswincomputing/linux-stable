// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef NVDLA_LINUX_H
#define NVDLA_LINUX_H

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include "dla_driver.h"
#include "hetero_ioctl.h"

/**
 * @brief			Task information submitted from user space
 *
 * ref				Reference count for task
 * num_addresses		Number of addresses in address list
 * nvdla_dev			Pointer to NVDLA device
 * address_list			Address list
 * file				DRM file instance
 */
struct nvdla_task {
	addrListDesc_t *addrlist;
	struct dla_buffer_object *bobjs;
	void *executor;
};

/**
 * @brief			Submit task
 *
 * This function submits task to NVDLA engine.
 *
 * @param nvdla_dev		Pointer to NVDLA device
 * @param task			Pointer to task
 * @return			0 on success and negative on error
 *
 */
int32_t nvdla_task_submit(struct nvdla_device *nvdla_dev,
			  struct nvdla_task *task);

#endif
