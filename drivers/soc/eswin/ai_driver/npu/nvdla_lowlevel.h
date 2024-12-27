// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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

#ifndef __NVDLA_LOWLEVEL_H__
#define __NVDLA_LOWLEVEL_H__
#include <linux/platform_device.h>
#include "dla_driver.h"

irqreturn_t npu_mbox_irq(int irq, void *dev_id);
void npu_tbu_power(struct device *dev, bool flag);
void npu_hw_init(struct nvdla_device *ndev);
int npu_platform_init(void);
int npu_platform_uninit(void);
int npu_dt_node_resources(struct nvdla_device *nvdla);
int npu_put_dt_resources(struct nvdla_device *ndev);

int npu_init_mbox(struct nvdla_device *nvdla_dev);

int npu_uninit_mbox(struct nvdla_device *nvdla_dev);
int npu_remove_sysfs(struct platform_device *pdev);
int npu_create_sysfs(struct platform_device *pdev);

int npu_hardware_reset(struct nvdla_device *nvdla_dev);

int npu_dev_reset(struct nvdla_device *nvdla_dev);
int npu_dev_assert(struct nvdla_device *nvdla_dev);
int npu_init_reset(struct nvdla_device *nvdla_dev);
void npu_dma_sid_cfg(void __iomem *npu_subsys_base, u32 sid);

int npu_e31_load_fw(struct nvdla_device *ndev);

int npu_pm_get(struct nvdla_device *ndev);
int npu_pm_put(struct nvdla_device *ndev);

int npu_enable_clock(struct nvdla_device *ndev);

int npu_disable_clock(struct nvdla_device *ndev);
#endif
