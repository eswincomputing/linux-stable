// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
