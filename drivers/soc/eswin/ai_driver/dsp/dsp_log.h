// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
