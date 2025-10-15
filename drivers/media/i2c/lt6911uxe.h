// SPDX-License-Identifier: GPL-2.0
/*
 * LT6911UXE Sensor Driver Header File for EIC7700 SoC
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Authors: Junfa Sun <sunjunfa@eswincomputing.com>
 *          Yulin Lu <luyulin@eswincomputing.com>
 */

#ifndef __LT6911UXE_H
#define __LT6911UXE_H

#include <linux/types.h>

#define LT6911UXE_NAME             "lt6911uxe"

struct lt6911uxe_platform_data {
	unsigned int port;
	unsigned int lanes;
	uint32_t i2c_slave_address;
	int irq_pin;
	unsigned int irq_pin_flags;
	char irq_pin_name[16];
	int reset_pin;
	int detect_pin;
	char suffix;
	int gpios[4];
};

#endif /* __LT6911UXE_H  */