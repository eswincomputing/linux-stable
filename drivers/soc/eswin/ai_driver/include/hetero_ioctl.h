// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __HETERO_IOCTL_H_
#define __HETERO_IOCTL_H_

#include "hetero_types.h"
#include "es_type.h"
#include <uapi/linux/es_vb_user.h>

enum {
    mem_flag_swap,
    mem_flag_input,
    mem_flag_output,
};

/**
 * struct nvdla_mem_handle structure for memory handles
 *
 * @handle		handle to DMA buffer allocated in userspace
 * @reserved		Reserved for padding
 * @offset		offset in bytes from start address of buffer
 *
 */
struct nvdla_mem_handle {
    u32 handle;
    u32 flag;
    u32 bind_id;
    u32 memory_type;
    u64 offset;
};

/**
 * struct nvdla_ioctl_submit_task structure for single task information
 *
 * @num_addresses		total number of entries in address_list
 * @reserved			Reserved for padding
 * @address_list		pointer to array of struct nvdla_mem_handle
 *
 */
struct nvdla_ioctl_submit_task {
    u32 num_addresses;
    u32 timeout;
    u64 ret;
    u64 address_list;
};

/**
 * struct nvdla_submit_args structure for task submit
 *
 * @tasks		pointer to array of struct nvdla_ioctl_submit_task
 * @num_tasks		number of entries in tasks
 * @flags		flags for task submit, no flags defined yet
 * @version		version of task structure
 *
 */
struct nvdla_submit_args {
    u64 tasks;
    u16 num_tasks;
    u16 flags;
    u32 version;
};

/** struct win_ioctl_args->mode bit definition
 * -------------------------------------
 * | bit [7:4]         | bit [3:0]     |
 * -------------------------------------
 * | WIN_EMISSION_MODE | WIN_EXEC_MODE |
 * -------------------------------------
 */
#define emission_mode_bit(m) (m << 4)
#define get_emission_mode(m) ((m & 0xf0) >> 4)
#define get_exec_mode(m) (m & 0xf)
enum WIN_EMISSION_MODE {
    EMISSION_MODE_STREAM,
    EMISSION_MODE_STREAM_STEP,
    EMISSION_MODE_DEP_GRAPH,
};

enum WIN_IOCTL_CMD {
    CMD_SUBMIT_MODEL,
    CMD_RELEASE_MODEL,
    CMD_COMMIT_NEW_IO_TENSOR,
    CMD_OUTPUT_GET,
    CMD_CREATE_STREAM,
    CMD_SYNC_STREAM,
    CMD_ABORT_STREAM,
    CMD_DESTROY_STREAM,
    CMD_GET_CMA_INFO,
    WIN_IOCTL_CMD_NUM,
};

#define TASK_RESULT_ARRAY_NUM 16

enum task_result_status {
    task_result_success = 1,
    task_result_failed,
    task_result_timeout,
};

struct task_result_t {
    u32 task_id;
    enum task_result_status result;
};

struct tasks_result_t {
    u32 preserve;
    u32 task_cnt;
    struct task_result_t task_result[TASK_RESULT_ARRAY_NUM];
};

struct npu_cma_info {
    u64 base;
    u32 size;
} __attribute__((aligned(sizeof(u32))));

struct npu_op_dump_list {
    u16 size;
    u16 *op_idx_list;
    char path[64];
    u32 prcess_id;
    u16 model_id;
} __attribute__((aligned(sizeof(u32))));

#define DUMP_PATH_LEN 128
typedef struct _kmd_dump_info {
    char path[DUMP_PATH_LEN];
    u32 process_id;
    u32 model_id;
    u16 is_dump_enable;
    u16 list_size;
    u16 *op_idx_list;
} kmd_dump_info_t __attribute__((aligned(sizeof(u32))));

enum kmd_dump_status {
    kmd_dump_disable = 0,
    kmd_dump_enable,
};

union event_union {
    s16 event_sinks[4];
    u64 event_data;
};

struct win_ioctl_args {
    union {
        /* For ioctl submit model */
        u64 shm_fd;
        /* For ioctl commit new io_tensor and
         * ioctl Dequeue Frames
         */
        struct {
            union {
                u32 frame_idx;
                u32 frame_idx_buff_size;
            };
            u16 tensor_size;
            u8 dump_enable;
            u64 dump_info;
        };
        u16 event_source_val;
    };
    u64 data;
    u64 pret;
    u32 stream_id;
    u16 model_idx;
    u32 version;
    u32 hetero_cmd;
};

typedef struct _sram_info {
    s32 fd;
    u32 size;
} sram_info_t;

enum WIN_EXEC_MODE {
    WIN_MODE_BLOCK,
    WIN_MODE_NON_BLOCK,
};
/* Must 64 bit */
union win_ioctl_exec_args {
    struct {
        u32 rsv0;
        u16 rsv1;
        /* block or not*/
        u8 mode;
        u8 model_id;
    };
    u64 value;
};

/**
 * struct nvdla_gem_create_args for allocating DMA buffer through GEM
 *
 * @handle		handle updated by kernel after allocation
 * @flags		implementation specific flags
 * @size		size of buffer to allocate
 */
struct nvdla_gem_create_args {
    u32 fd;
    u32 flags;
    u64 size;
    void *buffer;
};

/**
 * struct nvdla_gem_map_offset_args for mapping DMA buffer
 *
 * @handle		handle of the buffer
 * @reserved		reserved for padding
 * @offset		offset updated by kernel after mapping
 */
struct nvdla_gem_map_offset_args {
    u32 fd;
    u32 reserved;
    u64 offset;
};

/**
 * struct nvdla_gem_destroy_args for destroying DMA buffer
 *
 * @handle		handle of the buffer
 */
struct nvdla_gem_destroy_args {
    int32_t fd;
};

struct nvdla_spram_args {
    u32 addr;
    u32 len;
    u8 *buffer;
};

struct nvdla_reg_args {
    u32 addr;
    u32 *buffer;
    u32 num;
};

typedef struct _addrDesc {
    ES_DEV_BUF_S devBuf;
    int flag;
    int bindId;
    void *virtAddr;
    uint32_t memoryType;
} addrDesc_t;

typedef struct _addrListDesc {
    uint32_t numAddress;
    addrDesc_t addrDesc[0];
} addrListDesc_t;

typedef struct _modelShmDesc {
    uint16_t kmdSubModelId;     // kmd submodel id
    uint32_t kmdNetworkAddrId;  // kmd network address index in model
    int32_t dspFd[DSP_MAX_CORE_NUM];
    addrListDesc_t addrList;  // model address list
} modelShmDesc_t;

typedef struct _modelRec {
    int32_t dmabufFd;              // dmabuf fd for share memory
    uint64_t bufLen;               // malloc len for dambufFd
    modelShmDesc_t *modelShmDesc;  // virt address of dmabuf
} modelRec_t;

#define ES_NPU_IOCTL_BASE 'n'
#define ES_NPU_IO(nr) _IO(ES_NPU_IOCTL_BASE, nr)
#define ES_NPU_IOR(nr, type) _IOR(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOW(nr, type) _IOW(ES_NPU_IOCTL_BASE, nr, type)
#define ES_NPU_IOWR(nr, type) _IOWR(ES_NPU_IOCTL_BASE, nr, type)

#define ES_NPU_IOCTL_GET_VERSION ES_NPU_IOR(0X00, int)
#define ES_NPU_IOCTL_GET_PROPERTY ES_NPU_IOR(0X01, int)
#define ES_NPU_IOCTL_GET_NUM_DEV ES_NPU_IOR(0X02, int)

#define ES_NPU_IOCTL_MODEL_LOAD ES_NPU_IOWR(0X03, int)
#define ES_NPU_IOCTL_MODEL_UNLOAD ES_NPU_IOWR(0X04, int)

#define ES_NPU_IOCTL_TASK_SUBMIT ES_NPU_IOWR(0X05, int)
#define ES_NPU_IOCTL_TASKS_GET_RESULT ES_NPU_IOWR(0X06, int)
#define ES_NPU_IOCTL_HETERO_CMD ES_NPU_IOWR(0x7, int)

#define ES_NPU_IOCTL_GET_EVENT ES_NPU_IOR(0X08, int)
#define ES_NPU_IOCTL_SET_EVENT ES_NPU_IOWR(0X09, int)

#define ES_NPU_IOCTL_GET_SRAM_FD ES_NPU_IOR(0X0a, int)
#define ES_NPU_IOCTL_HANDLE_PERF ES_NPU_IOR(0X0b, int)
#define ES_NPU_IOCTL_GET_PERF_DATA ES_NPU_IOR(0X0c, int)

#define ES_NPU_IOCTL_MUTEX_LOCK ES_NPU_IOR(0X0d, int)
#define ES_NPU_IOCTL_MUTEX_UNLOCK ES_NPU_IOWR(0X0e, int)
#define ES_NPU_IOCTL_PREPARE_DMA_BUF ES_NPU_IOWR(0xf, int)
#define ES_NPU_IOCTL_UNPREPARE_DMA_BUF ES_NPU_IOWR(0x10, int)

#define ES_NPU_IOCTL_MUTEX_TRYLOCK ES_NPU_IOWR(0x11, int)

#define NPU_HETERO_CMD_BASE 'h'
#define NPU_HETERO_IOWR(nr, type) _IOWR(NPU_HETERO_CMD_BASE, nr, type)

#define NPU_HETERO_GET_CMA_INFO NPU_HETERO_IOWR(0x00, int)
#define NPU_HETERO_QUERY_FRAME_READY NPU_HETERO_IOWR(0x01, int)
#define NPU_HETERO_SEND_FRAME_DONE NPU_HETERO_IOWR(0x02, int)
#define NPU_HETERO_QUERY_OP_RESUME NPU_HETERO_IOWR(0x03, int)
#define NPU_HETERO_SEND_OP_DONE NPU_HETERO_IOWR(0x04, int)
#define NPU_HETERO_QUERY_EVENT_SOURCE NPU_HETERO_IOWR(0x05, int)
#define NPU_HETERO_SEND_EVENT_SINK_DONE NPU_HETERO_IOWR(0x06, int)
#endif
