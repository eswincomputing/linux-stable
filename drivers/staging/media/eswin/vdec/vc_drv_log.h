// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN video decoder driver
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
 */
#ifndef __VC_DRV_LOG__H
#define __VC_DRV_LOG__H

#include <linux/kernel.h>

#define VC_LOG_LEVEL_TRACE 0x01
#define VC_LOG_LEVEL_DBG 0x02
#define VC_LOG_LEVEL_INFO 0x04
#define VC_LOG_LEVEL_NOTICE 0x08
#define VC_LOG_LEVEL_WARN 0x10
#define VC_LOG_LEVEL_ERR 0x20

// define in makefile
#define OUTPUT_LOG_LEVEL 0xFC

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_ERR)
#define LOG_ERR(fmt, args...) do { pr_err("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_ERR(fmt, args...)
#endif

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_WARN)
#define LOG_WARN(fmt, args...) do { pr_warn("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_WARN(fmt, args...)
#endif

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_NOTICE)
#define LOG_NOTICE(fmt, args...) do { pr_notice("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_NOTICE(fmt, args...)
#endif

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_INFO)
#define LOG_INFO(fmt, args...) do { pr_info("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_INFO(fmt, args...)
#endif

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_DBG)
#define LOG_DBG(fmt, args...) do { pr_debug("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_DBG(fmt, args...)
#endif

#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_TRACE)
#define LOG_TRACE(fmt, args...) do { pr_info("[" LOG_TAG "]" fmt, ##args); } while (0)
#else
#define LOG_TRACE(fmt, args...)
#endif

#define DEC_DEV_NAME "es_vdec"

#endif
