// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
int npu_init_reset(struct nvdla_device *nvdla_dev);
void npu_dma_sid_cfg(void __iomem *npu_subsys_base, u32 sid);

int npu_e31_load_fw(struct platform_device *, void __iomem *e31_mmio_base);
int npu_pm_get(struct nvdla_device *ndev);
int npu_pm_put(struct nvdla_device *ndev);

int npu_enable_clock(struct nvdla_device *ndev);

int npu_disable_clock(struct nvdla_device *ndev);
#endif
