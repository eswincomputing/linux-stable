// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

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
