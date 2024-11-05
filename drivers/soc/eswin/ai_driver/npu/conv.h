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

#ifndef __CONV_H__
#define __CONV_H__
#include "hetero_common.h"

typedef enum ipc_message_t {
	IPC_NONE = 0,
	IPC_CONFIG0_TRG,  //1
	IPC_CONFIG1_TRG,  //2
	IPC_CONFIG0_CPL,  //3
	IPC_CPNFIG1_CPL,  //4
	IPC_EVAL0_TRG,	//5
	IPC_EVAL1_TRG,
	IPC_MAX_MSG,
} ipc_message_t;

/**
 * @brief Defines message queue between a pair of E31 cores.
 *
 */
#define QUEUE_SIZE 8
#define QUEUE_LIMIT 16
#define IPC_MESSAGE_BITS 3

#define NPU_CPU_SIZE 0x00100000
#define NPU_DTIM_OFFSET 0x10000

#endif
