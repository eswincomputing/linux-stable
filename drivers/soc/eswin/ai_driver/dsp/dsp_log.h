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

#include <linux/module.h>
#include <linux/kernel.h>

#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_INFO 3
#define LOG_DEBUG 4

extern int dsp_log_level;

#define dsp_log(level, fmt, ...)                                          \
	do {                                                              \
		if (level <= dsp_log_level)                               \
			printk(KERN_INFO "[DSP][Driver][%s](%s:%d) " fmt, \
			       level == LOG_ERROR   ? "ERROR" :           \
			       level == LOG_WARNING ? "WARNING" :         \
			       level == LOG_INFO    ? "INFO" :            \
							    "DEBUG",               \
			       __func__, __LINE__, ##__VA_ARGS__);        \
	} while (0)

#define dsp_err(fmt, ...) dsp_log(LOG_ERROR, fmt, ##__VA_ARGS__)
#define dsp_warning(fmt, ...) dsp_log(LOG_WARNING, fmt, ##__VA_ARGS__)
#define dsp_info(fmt, ...) dsp_log(LOG_INFO, fmt, ##__VA_ARGS__)
#define dsp_debug(fmt, ...) dsp_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
