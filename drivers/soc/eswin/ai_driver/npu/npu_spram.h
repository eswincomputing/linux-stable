// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef NPU_SPRAM_H
#define NPU_SPRAM_H

#include "dla_driver.h"

int npu_spram_init(struct nvdla_device *nvdla_dev);
void npu_spram_read(uint32_t addr, uint32_t len, uint8_t *buffer);
void npu_spram_write(uint32_t addr, uint32_t len, uint8_t *buffer);

#endif
