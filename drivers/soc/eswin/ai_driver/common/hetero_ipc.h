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

#ifndef _HETERO_IPC_H_
#define _HETERO_IPC_H_

#include "hetero_env.h"
#ifndef __KERNEL__
#include <string.h>
#include <stdint.h>
#endif

#if (NPU_DEV_SIM != NPU_ESIM_TOOL)
#include "hetero_types.h"
#include "hetero_common.h"
#include "npu_base_regs.h"
#include "conv_regs.h"
#include "cdma_regs.h"
#include "hetero_arch.h"
#include "mailbox_regs.h"
#endif
#include "es_dsp_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Define the core ID of each thread. */
#define EMISSION_CORE_ID 0
#define PROGRAM_CORE_ID 1
#define MAJOR_0_CORE_ID 2
#define MAJOR_1_CORE_ID 3
#define MAJOR_2_CORE_ID 4
#define MAJOR_3_CORE_ID 5
#define MAJOR_4_CORE_ID 6
#define MAJOR_5_CORE_ID 7
#define MAJOR_6_CORE_ID 8
#define MAJOR_7_CORE_ID 9
#define DSP_0_CORE_ID 10
#define DSP_1_CORE_ID 11
#define DSP_2_CORE_ID 12
#define DSP_3_CORE_ID 13
/* In the real env, the interrupt register of e31 is only 10bit, per e31 use one bit.
 * the u84 reuses the 0 bit of emssion core.
 */
#if (NPU_DEV_SIM == NPU_REAL_ENV)
#define HOST_CORE_ID 0
#else
#define HOST_CORE_ID 14
#endif

#define NUM_IPC_CORES 15
#define HETERO_HW_ID NUM_IPC_CORES
#define HETERO_SIM_ID 16

#define CDMA_64_BITS_ALIGN 64

#if (NPU_DEV_SIM != NPU_ESIM_TOOL)
typedef struct _msg_payload {
    /**
     *  @brief Specifies the transaction layer protocol.
     */
    u8 type;
    /**
     *  @brief Specifies the parameter passed to transaction layer handler.
     */
    u8 param;
    /**
     *  @brief Specifies the long parameter passed to transaction layer handler.
     */
    u16 lparam;
} msg_payload_t;

typedef struct _hetero_signal_channel {
    volatile u8 produce_index;
    volatile u8 consume_index;
    /**
     *  @brief Specifies the payload size. It should be aligned to 2^N, N should
     *  be less than 8.
     */
    u8 fifo_size;
    u8 reserved;
    u32 peer;
    msg_payload_t payloads[0];
} hetero_signal_channel_t;

/**
 *  @brief Defines an instance of heterogeneous channel.
 *
 *  @param channel_name:    Specify the name of the channel.
 *  @param fifo_size:       Specify the size of channel entry size.
 */
#define DECLARE_HETERO_CHANNEL(channel_name, fifo_size) \
    struct {                                            \
        hetero_signal_channel_t channel;                \
        msg_payload_t reserved[fifo_size];              \
    } channel_name;

/**
 *  @brief Defines an instance array of heterogeneous channel.
 *
 *  @param channel_name:    Specify the name of the channel.
 *  @param fifo_size:       Specify the size of channel entry size.
 *  @param inst_num:        Specify number of instance.
 */
#define DECLARE_HETERO_CHANNELS(channel_name, fifo_size, inst_num) \
    struct {                                                       \
        hetero_signal_channel_t channel;                           \
        msg_payload_t reserved[fifo_size];                         \
    } channel_name[inst_num];

static const u32 invalid_signature = 0;
static const u32 node_signature = 0xE31;

typedef struct _cpu_node {
    u32 signature;
    u32 node_id;
} cpu_node_t;

typedef struct _program_node {
    cpu_node_t cpu_node;
    /**
     * @brief Specifies the CDMA slots used for temporarily storing CDMA program
     * data.
     */
    u8 padding[CDMA_64_BITS_ALIGN - sizeof(cpu_node_t)];
    struct {
        u32 program_info[MAX_CDMA_PROGRAM_SIZE / sizeof(u32)];
    } cdma_program_slot[NUM_CDMA_SLOT] __attribute__((aligned(CDMA_SRC_BYTE_ALIGN)));

    // Message header
    // Sender
    DECLARE_HETERO_CHANNEL(chn_to_emission, 0)
    // Receiver
    DECLARE_HETERO_CHANNEL(chn_from_emission, 16)

    // Message body
    npu_io_tensor_t io_addr_list[NUM_TIKTOK];

#if NPU_DEV_SIM != NPU_REAL_ENV
    u64 conv_extra_data_addr[NUM_DATA_SLOT];
#endif
} program_node_t;

// program_node_t program_node __attribute__((section(".ipc_ram0")));

typedef struct _emission_node {
    cpu_node_t cpu_node;
    u8 padding[CDMA_64_BITS_ALIGN - sizeof(cpu_node_t)];
    /**
     * @brief Specifies the dependency information for each operation type. Note
     * for convolution operator type, conv_dep is used. CDMA is used to transfer
     * data from DDR to local DTCM.
     */
    union {
        npu_dep_info_t npu_dep[NUM_OP_TYPE - DSP_MAX_CORE_NUM - 1][NUM_DATA_SLOT];
        struct {
            // make sure the sequence is correct: edma, conv, sdp, pdp, rubik.
            npu_dep_info_t edma_dep[NUM_DATA_SLOT];
            npu_dep_info_t sdp_dep[NUM_DATA_SLOT];
            npu_dep_info_t pdp_dep[NUM_DATA_SLOT];
            npu_dep_info_t rubik_dep[NUM_DATA_SLOT];
            npu_dep_info_t event_sink_dep[NUM_DATA_SLOT];
            npu_dep_info_t event_source_dep[NUM_DATA_SLOT];
        };
    } __attribute__((aligned(CDMA_DST_BYTE_ALIGN)));
    npu_dep_info_t dsp_dep_transfer;
    conv_emission_t conv_dep[NUM_DATA_SLOT];
    npu_dep_info_t dsp_dep[DSP_MAX_CORE_NUM][NUM_DATA_SLOT];
    /**
     *  @brief Specifies the IOVA for emission core to access U84.
     */
    u32 host_base_addr;

    // Message header
    // Sender
    DECLARE_HETERO_CHANNEL(chn_to_program, 0)
    // Receiver
    DECLARE_HETERO_CHANNEL(chn_from_host, 2)
    DECLARE_HETERO_CHANNEL(chn_from_program, 16)

    // Message body
    hetero_ipc_frame_t frame_desc[NUM_TIKTOK];
} emission_node_t;

typedef struct _major_node {
    cpu_node_t cpu_node;
    u8 padding[CDMA_64_BITS_ALIGN - sizeof(cpu_node_t)];
    union {
        rdma_dev_com_inf_t rdma_com;
        u32 __buffer[MAX_MAJOR_CONFIG_SIZE / sizeof(u32)];
    } buffer[NUM_DATA_SLOT];
} major_node_t;

typedef struct _host_node {
    cpu_node_t cpu_node;

    /**
     *  @brief Specifies the mapped virtual address for U84 to access program
     *  core.
     */
    u64 program_base_addr;

    /**
     *  @brief Specifies the mapped virtual address for U84 to access emission
     *  core.
     */
    u64 emission_base_addr;

    // Message header
    // Sender
    DECLARE_HETERO_CHANNEL(chn_to_emission, 0)
    // Receiver

    // Message body
    /**
     * @brief Specifies the model statistics data.
     */
#if NPU_PERF_STATS > 1
    model_stat_t model_stat[NUM_TIKTOK];
#endif
} host_node_t;

typedef struct _dsp_node {
    cpu_node_t cpu_node;

    // Message header
    // Sender
    DECLARE_HETERO_CHANNEL(chn_to_program, 0)
    DECLARE_HETERO_CHANNEL(chn_to_emission, 0)
    // Receiver

    // Message body
} dsp_node_t;

typedef enum {
    FRAME_READY = 0,
    FRAME_DONE,

    /**
     * @brief Emission core use this message to notify host an operation is
     * done. If host listen's this message, it should activate procedure that
     * waits this message.
     */
    NOTIFY_OP_DONE,
    NOTIFY_EVENT_SINK_DONE,

    /**
     * @brief Host core use this message to notify Emission core to resume
     * evaluation completion process.
     */
    NOTIFY_OP_RESUME,

    /**
     * @brief Host core use this message to notify Emission core to decrease
     * reference of a given operator.
     */
    DEC_OP_REF,
    DATA_TRANSFER_REQ,
    DATA_TRANSFER_DONE,
    PROGRAM_REQ,
    PROGRAM_DONE,
    EVALUATE_REQ,
    EVALUATE_DONE,
    PROGRAM_DIR_REQ,
    PROGRAM_DIR_DONE,
    DSP_EVAL_DONE
} CHANNAL_EVENT_TYPE;

#ifndef __KERNEL__
#if NPU_DEV_SIM == NPU_REAL_ENV
#if CURRENT_CORE == PROGRAM_CORE_ID
extern program_node_t local_program_node;
#define node_id (((cpu_node_t *)&local_program_node)->node_id)
#elif CURRENT_CORE == EMISSION_CORE_ID
extern emission_node_t local_emission_node;
#define node_id (((cpu_node_t *)&local_emission_node)->node_id)
#else
STATIC_ASSERT(CURRENT_CORE == MAJOR_0_CORE_ID);
extern major_node_t local_major_node;
#define node_id (((cpu_node_t *)&local_major_node)->node_id)
#endif  // CURRENT_CORE
extern emission_node_t emission_node;

#else  // NPU_DEV_SIM != NPU_REAL_ENV
extern TLS u32 node_id;
extern emission_node_t &emission_node;

#endif  // NPU_DEV_SIM == NPU_REAL_ENV
static inline void notify_peer_by_intr(u32 peer)
{
    u32 peer_node_id = (peer - (u32)(u64)&emission_node) / NPU_CPU_SIZE;
    reg_write(NPU_CTRL_BASE_ADDR + INT_SET_BITS(peer_node_id), 0x1UL << node_id);
}

static inline void notify_core_by_intr(u32 peer_core)
{
    reg_write(NPU_CTRL_BASE_ADDR + INT_SET_BITS(peer_core), 0x1UL << node_id);
}

/**
 * @brief loop check cdma status util cdma done
 */
static inline void check_cdma(void)
{
    union npu_cdma_misc_ctl_t misc_ctrl;
    do {
        misc_ctrl.dw = reg_read(NPU_CDMA_BASE_ADDR + NPU_CDMA_MISC_CTL_OFFSET);
    } while (misc_ctrl.dma_en);
}

/**
 *  @brief Send a message payload to the receiver of this channel, assuming the
 *  queue is not full. This function should be used by E31 and DSP.
 *
 *  @param channel:         Specify the channel port pointer.
 *  @param payload:         Specify the message payload.
 */
static void hetero_send(void *channel, msg_payload_t payload)
{
    hetero_signal_channel_t *pchan = (hetero_signal_channel_t *)channel;
    hetero_signal_channel_t *peer = (hetero_signal_channel_t *)((uintptr_t)pchan->peer);

    while (pchan->produce_index - pchan->consume_index >= pchan->fifo_size) {
        continue;  // no fifo, wait
    }
    check_cdma();
    peer->payloads[pchan->produce_index & (pchan->fifo_size - 1)] = payload;
    pchan->produce_index++;
    peer->produce_index = pchan->produce_index;
    hmb();
    notify_peer_by_intr(pchan->peer);
}

/**
 *  @brief Receive a message payload from this channel. This function should be
 *  used by E31.
 *
 *  @param channel:         Specify the channel port pointer.
 *  @param payload:         Specify the message payload to be received.
 *  @return:                True if a message is received successfully.
 */
static bool hetero_receive(void *channel, msg_payload_t *payload)
{
    hetero_signal_channel_t *pchan = (hetero_signal_channel_t *)channel;
    hetero_signal_channel_t *peer = (hetero_signal_channel_t *)((uintptr_t)pchan->peer);

    if (pchan->produce_index == pchan->consume_index) {
        return false;  // no data in fifo, wait
    }
    check_cdma();
    *payload = pchan->payloads[pchan->consume_index & (pchan->fifo_size - 1)];
    pchan->consume_index++;
    peer->consume_index = pchan->consume_index;
    return true;
}
#endif  // __KERNEL__

/**
 *  @brief Initialize the heterogeneous channel. This function should be called
 *  inside the core that hold this channel port.
 *
 *  @param channel:         Specify the channel port.
 *  @param peer_channel:    Specify the adversary channel port.
 */

#define hetero_init(local_channel, peer_channel)                                                          \
    {                                                                                                     \
        u32 local_size = (sizeof(local_channel) - sizeof(local_channel.channel)) / sizeof(msg_payload_t); \
        u32 peer_size = (sizeof(peer_channel) - sizeof(peer_channel.channel)) / sizeof(msg_payload_t);    \
        u32 fifo_size = local_size > peer_size ? local_size : peer_size;                                  \
        memset(&local_channel, 0, sizeof(local_channel));                                                 \
        local_channel.channel.fifo_size = fifo_size;                                                      \
        local_channel.channel.peer = (u32)(u64) & peer_channel;                                           \
    }

/**
 *  @brief Send a message payload to the receiver of this channel, assuming the
 *  queue is not full. This function should be used by U84.
 *
 *  @param channel:         Specify the channel port pointer.
 *  @param base_addr:       Specify a base address for peer offset.
 *  @param intr_addr:       Specify the interrupt address to notify e31.
 *  @param payload:         Specify the message payload.
 */
static void host_hetero_send(void *channel, void *base_addr, void *npu_ctrl_addr, msg_payload_t payload)
{
    union npu_cdma_misc_ctl_t misc_ctrl;
    hetero_signal_channel_t *pchan = (hetero_signal_channel_t *)channel;
    hetero_signal_channel_t *peer = (hetero_signal_channel_t *)((u8 *)base_addr + pchan->peer);
    while (pchan->produce_index - pchan->consume_index >= pchan->fifo_size) {
        continue;  // no fifo, wait
    }
    do {
        misc_ctrl.dw =
            reg_read((size_t)npu_ctrl_addr + NPU_CDMA_BASE_ADDR - NPU_CTRL_BASE_ADDR + NPU_CDMA_MISC_CTL_OFFSET);
    } while (misc_ctrl.dma_en);

    peer->payloads[pchan->produce_index & (pchan->fifo_size - 1)] = payload;
    pchan->produce_index++;
    peer->produce_index = pchan->produce_index;

    reg_write((size_t)((size_t)npu_ctrl_addr + INT_SET_BITS(EMISSION_CORE_ID)), 0x1UL << HOST_CORE_ID);
}

/**
 *  @brief Send a mailbox message payload to U84. This function should be used
 *  by E31.
 *
 *  @param channel:         Specify the channel port pointer.
 *  @param payload:         Specify the message payload to be received.
 */
static void messagebox_send(u16 channel_id, msg_payload_t payload)
{
    u32 mbox_base, data;

    ASSERT(channel_id == MBOX_CHN_NPU_TO_U84);

    mbox_base = MBOX_REG_BASE + channel_id * MBOX_REG_OFFSET;
    while (reg_read(mbox_base + MBOX_NPU_FIFO_OFFSET) & 0x1) {
    }

    data = ((u32)payload.type | (u32)payload.param << 8 | (u32)payload.lparam << 16);
    reg_write((size_t)(mbox_base + MBOX_NPU_WR_DATA0_OFFSET), data);
    reg_write((size_t)(mbox_base + MBOX_NPU_WR_DATA1_OFFSET), MBOX_WRITE_FIFO_BIT);
}

#ifndef __KERNEL__
/**
 *  @brief Send a mailbox message payload to DSP. This function should be used
 *  by E31.
 *
 *  @param op_type:         Specify the dsp operator type.
 *  @param payload:         Specify the message payload to be received.
 */
static void messagebox_send_dsp(u32 op_type, u64 payload)
{
    u32 dsp_idx, mbox_base, mbox_int, mbox_lock, timeout;

    ASSERT((op_type == IDX_KMD_DSP0) || (op_type == IDX_KMD_DSP1) || (op_type == IDX_KMD_DSP2) ||
           (op_type == IDX_KMD_DSP3));

    dsp_idx = op_type - IDX_KMD_DSP0;
    mbox_base = MAILBOX_E31_TO_DSP_REG[dsp_idx];
    mbox_int = MAILBOX_E31_TO_DSP_INT[dsp_idx];
    mbox_lock = BIT1;  // must different with dsp driver(using BIT0)
    timeout = 0;

    // check lock bit and fifo
    while (1) {
        reg_write(mbox_base + MBOX_NPU_WR_LOCK, mbox_lock);

        if ((reg_read(mbox_base + MBOX_NPU_WR_LOCK) & mbox_lock) &&
            (reg_read(mbox_base + MBOX_NPU_FIFO_OFFSET) & 0x1) == 0) {
            break;
        }

        if (timeout++ > MBX_LOCK_BIT_TIMEOUT) {
            return;
        }
    }

    // send data
    reg_write(mbox_base + MBOX_NPU_WR_DATA0_OFFSET, (uint32_t)(payload & 0xFFFFFFFF));
    reg_write(mbox_base + MBOX_NPU_WR_DATA1_OFFSET, (uint32_t)(payload >> 32) | MBOX_WRITE_FIFO_BIT);

    // enable interrupt
    reg_write(mbox_base + MBOX_NPU_INT_OFFSET, mbox_int);

    // clear lock bit
    reg_write(mbox_base + MBOX_NPU_WR_LOCK, 0);
}
#endif

/**
 * @brief Receives data from a mailbox message.
 *
 * @param payload Pointer to the message to receive from u84 or dsp.
 * @return int Returns 0 on success, or an error code on failure.
 */
static bool messagebox_receive_data(msg_payload_t *payload)
{
    u32 data[2];
    /* must be first read rd data0. */
    data[0] = reg_read(ESWIN_MAILBOX_U84_TO_NPU_0_REG_BASE + MBOX_NPU_RD_DATA0_OFFSET);
    data[1] = reg_read(ESWIN_MAILBOX_U84_TO_NPU_0_REG_BASE + MBOX_NPU_RD_DATA1_OFFSET);
    if (data[1] == 0) {
        return false;
    }

    memcpy(payload, (msg_payload_t *)&data[0], sizeof(msg_payload_t));

    reg_write(ESWIN_MAILBOX_U84_TO_NPU_0_REG_BASE + MBOX_NPU_RD_DATA1_OFFSET, 0);

    return true;
}

#endif  // #if (NPU_DEV_SIM != NPU_ESIM_TOOL)

#ifdef __cplusplus
}
#endif

#endif
