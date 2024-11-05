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

#include <linux/module.h>
#include "dsp_log.h"

#if BUILD_RELEASE == 1
int dsp_log_level = LOG_ERROR;
#else
#if DEBUG_LEVEL == 1
int dsp_log_level = LOG_ERROR;
#elif DEBUG_LEVEL == 2
int dsp_log_level = LOG_INFO;
#else
int dsp_log_level = LOG_DEBUG;
#endif
#endif

module_param(dsp_log_level, int, 0644);
MODULE_PARM_DESC(dsp_log_level,
		 "Log level (0:DISABLE 1: ERROR, 2: INFO, 3: DEBUG)");
