// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __ESWIN_DSP_TYPES_H__
#define __ESWIN_DSP_TYPES_H__

#include "es_type.h"
#if defined(__KERNEL__)
#include <uapi/linux/es_vb_user.h>
#else
#include "es_vb_user.h"
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

typedef struct DSP_Capability_S {
    ES_U64 reserved;
} ES_DSP_Capability_S;

typedef enum DSP_LOG_LEVEL_E {
    ES_DSP_LOG_DEBUG = 0x0,
    ES_DSP_LOG_INFO = 0x1,
    ES_DSP_LOG_WARNING = 0x2,
    ES_DSP_LOG_ERROR = 0x3,
    ES_DSP_LOG_NONE = 0x4
} ES_DSP_LOG_LEVEL_E;

typedef enum DSP_ID_E {
    ES_DSP_ID_0 = 0x0,
    ES_DSP_ID_1 = 0x1,
    ES_DSP_ID_2 = 0x2,
    ES_DSP_ID_3 = 0x3,

    ES_DSP_ID_4 = 0x4,
    ES_DSP_ID_5 = 0x5,
    ES_DSP_ID_6 = 0x6,
    ES_DSP_ID_7 = 0x7,

    ES_DSP_ID_BUTT
} ES_DSP_ID_E;

typedef enum DSP_LOAD_POLICY_E {
    /**
     * The operator will not be unloaded.
     */
    ES_LOAD_CACHED = 0x0,
    /**
     * The operator can be unloaded.
     */
    ES_LOAD_UNCACHED = 0x1,
    ES_DSP_LOAD_BUTT
} ES_DSP_LOAD_POLICY_E;

typedef enum DSP_PRI_E {
    ES_DSP_PRI_0 = 0x0,
    ES_DSP_PRI_1 = 0x1,
    ES_DSP_PRI_2 = 0x2,
    ES_DSP_PRI_3 = 0x3,

    ES_DSP_PRI_BUTT
} ES_DSP_PRI_E;

#define BUFFER_CNT_MAXSIZE 32

typedef struct DEVICE_BUFFER_GROUP_S {
    ES_DEV_BUF_S *buffers;
    ES_U32 bufferCnt;
} ES_DEVICE_BUFFER_GROUP_S;

typedef ES_S32 ES_DSP_HANDLE;

#define WAIT_FOR_TASK_COMPLETION -1

/**
 * @brief Callback function type for task completion or exception notification.
 *
 * This callback type is used to define functions that can be registered as
 * callbacks to receive notifications when a task completes or encounters an exception.
 *
 * @param arg Pointer to user-defined data that can be passed to the callback.
 * @param state An integer representing the state of the task.
 *              - If state is 0, it indicates successful completion of the task.
 *              - If state is non-zero, it indicates an exception or error during the task.
 */
typedef void (*ES_DSP_TASK_CALLBACK)(void *arg, ES_S32 state);

typedef struct DSP_TASK_S {
    /**
     * Specify the handle of target operator.
     */
    ES_DSP_HANDLE operatorHandle;
    /**
     * Specify the device buffers that will be used by DSP device.
     */
    ES_DEV_BUF_S dspBuffers[BUFFER_CNT_MAXSIZE];
    /**
     * Specify total number of parameter buffers.
     */
    ES_U32 bufferCntCfg;
    /**
     * Specify total number of input buffers.
     */
    ES_U32 bufferCntInput;
    /**
     * Specify total number of output buffers.
     */
    ES_U32 bufferCntOutput;
    /**
     * Specify the priority of target operator.
     */
    ES_DSP_PRI_E priority;
    /**
     * Specify the driver responsible for host side cache synchronization.
     */
    ES_BOOL syncCache;
    /**
     * Specify DSP for starting evaluation and notifying `prepare` and `eval` completion events.
     */
    ES_BOOL pollMode;
    /**
     * Specify the handle of operator task.
     * Can only be used in the low-level async interface.
     */
    ES_DSP_HANDLE taskHandle;
    /**
     * Specify the callback when the asyc task is completed.
     * Can only be used in the low-level async interface.
     */
    ES_DSP_TASK_CALLBACK callback;
    /**
     * Specify the callback arguments.
     * Can only be used in the low-level async interface.
     */
    ES_VOID *cbArg;
    /**
     * Reserved field.
     */
    ES_U32 reserved;
} __attribute__((packed)) ES_DSP_TASK_S;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
