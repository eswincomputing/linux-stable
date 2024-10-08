// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __CONV_REGS_H__
#define __CONV_REGS_H__

/**
 * @brief Declear miscellaneous interrupts in NPU module.
 *
 */
#define IRQ_NPU_PEC_PING 5
#define IRQ_NPU_PEC_PONG 6
#define IRQ_NPU_PEC_ERROR 7
#define IRQ_NPU_PREDRP_PING 8
#define IRQ_NPU_PREDRP_PONG 9
#define IRQ_NPU_PREDRP_ERROR 10
#define IRQ_NPU_CONV_MASK ((1U << IRQ_NPU_PREDRP_PING) | (1U << IRQ_NPU_PREDRP_PONG))

/**
 * @brief PEC registers.
 *
 */
#define PEC_D_OP_EN_TRIG 0x0
#define PEC_D_OP_STATUS 0x4

#define PEC_D_SPAD_PARAM0 0x8
#define PEC_D_SPAD_PARAM1 0xC
#define PEC_D_GLB_PARAM0 0x10
#define PEC_D_CALC_MODE 0x14
#define PEC_D_ROUNT_CASE_MODE_PADDING 0x18
#define PEC_D_SPACE_OFFSET0 0x1C
#define PEC_D_SPACE_OFFSET1 0x20
#define PEC_D_PADDING_VALUE 0x24
#define PEC_D_GLB_WT_IFM_REUSE 0x28
#define PEC_WT_NAN_CNT 0x2C
#define PEC_WT_IFN_CNT 0x30
#define PEC_IFM_NAN_CNT 0x34
#define PEC_IFM_IFN_CNT 0x38
#define PEC_PSUM_OV_CNT 0x3C

#define PEC_S_SOFT_RESET 0x0
#define PEC_S_POINTER_FLAG 0x4
#define PEC_S_PP_STATUS 0x8
#define PEC_PERF_CNT_CLR_OV 0xC
#define PEC_PERF_CNT_L 0x10
#define PEC_PERF_CNT_H 0x14

#define NPU_PEC_UCAST_ADDR_STRIDE (NPU_MODULE_STRIDE * 2)

/**
 * @brief MAC registers.
 *
 */
#define MAC_D_OP_EN_TRIG 0x0
#define MAC_D_OP_STATUS 0x4

#define MAC_S_SOFT_RESET 0x0
#define MAC_S_POINTER_FLAG 0x4
#define MAC_S_PP_STATUS 0x8
#define MAC_S_MAC_PERF_CNT_CLR_OV 0xc
#define MAC_S_OP_EN_ACTIVE_CNT_L 0x10
#define MAC_S_OP_EN_ACTIVE_CNT_H 0x14
#define MAC_S_OP_EN_NOT_CNT_L 0x18
#define MAC_S_OP_EN_NOT_CNT_H 0x1c
#define MAC_S_INT_MASK_PING_BASE1 0x20
#define MAC_S_INT_MASK_PONG_BASE1 0x2C

#define MAC_CSC_MODE 0x2C

/**
 * @brief RDMA registers.
 *
 */
#define RDMA_D_OP_EN_TRIG 0x0
#define RDMA_D_OP_STATUS 0x4

/**
 * @brief The Ifmap and weight control register offset relative to RDMA base address.
 *
 */
#define RDMA_D_IFM_CR 0x8
#define RDMA_D_IFM_SA_H 0xc
#define RDMA_D_IFM_SA_L 0x10
#define RDMA_D_WT_SA_H 0x14
#define RDMA_D_WT_SA_L 0x18
#define RDMA_D_DEST_CHAN(n) (0x20 + (n * 4))

/**
 * @brief The WIG base address register offset relative to RDMA base address.
 *
 */
#define RDMA_D_WIG_BASE_ADDR0 0x30
#define RDMA_D_WIG_BASE_ADDR1 0x34
#define RDMA_D_WIG_BASE_ADDR2 0x38
#define RDMA_D_WIG_BASE_ADDR3 0x3c
#define RDMA_D_LOOP_NUM2_CH 0x40
#define RDMA_D_LOOP_NUM1_CH 0x44
#define RDMA_D_LOOP_NUM0_CH 0x48
#define RDMA_D_OFFSET9_CH 0x50
#define RDMA_D_OFFSET8_CH 0x54
#define RDMA_D_OFFSET7_CH 0x58
#define RDMA_D_OFFSET6_CH 0x5c
#define RDMA_D_OFFSET5_CH 0x60
#define RDMA_D_OFFSET4_CH 0x64
#define RDMA_D_OFFSET3_CH 0x68
#define RDMA_D_OFFSET2_CH 0x6c
#define RDMA_D_OFFSET1_CH 0x70
#define RDMA_D_OFFSET0_CH 0x74

#define RDMA_S_RST 0x0
#define RDMA_S_POINTER_FLAG 0x4
#define RDMA_S_PP_STAUTS 0x8
#define RDMA_S_INTS_ENABLE 0xC

#define RDMA_WRAPPER_D_OP_EN_TRIG 0x0
#define RDMA_WRAPPER_D_OP_STATUS 0x4

#define RDMA_WRAPPER_S_RST 0x0
#define RDMA_WRAPPER_S_POINTER_FLAG 0x4
#define RDMA_WRAPPER_S_PP_STATUS 0x8
#define RDMA_WRAPPER_S_PING_INT_TASK 0x10
#define RDMA_WRAPPER_S_PONG_INT_TASK 0x14

/**
 * @brief E31 GPR reset register.
 *
 */
#define NPU_E31_GPR_REST 0x0

/**
 * @brief E31 debug reset register.
 *
 */
#define NPU_E31_DEBUG_REST 0x4

/**
 * @brief E31 bus reset register.
 *
 */
#define NPU_E31_BUS_REST 0x8

/**
 * @brief E31 core reset register.
 *
 */
#define NPU_E31_CORE_REST 0xC

/**
 * @brief E31 core clock gate register.
 *
 */
#define NPU_E31_CORE_CLOCK_GATE 0x10

/**
 * @brief E31 clock gate register.
 *
 */
#define NPU_E31_CLOCK_GATE 0x14

/**
 * @brief The start address of node specific registers.
 *
 */
#define NODE_CTRL_OFFSET 0x18

/**
 * @brief The stride between the beginning of one node control and its neighbor's.
 *
 */
#define NODE_CTRL_STRIDE 0x18

/**
 * @brief This is the JTAG register.
 * Bit[3:0] jtag version.
 * Bit[19:4] part number.
 * Bit[30:20]mfr id.
 *
 */
#define JTAG_ID_CODE_OFFSET(node) (0x18 + (node)*NODE_CTRL_STRIDE)

/**
 * @brief This is the reset vector.
 *
 */
#define RESET_VECTOR_OFFSET(node) (0x1C + (node)*NODE_CTRL_STRIDE)

/**
 * @brief This is a bitmap. Write to this register will set the corresponding bits in the interrupt status register if
 * the written value in these bits are 1. Otherwise the bits remain unchanged.
 *
 */
#define INT_SET_BITS(node) (0x20 + (node)*NODE_CTRL_STRIDE)

/**
 * @brief This is a bitmap. Write to this register will clear the corresponding bits in the interrupt status register if
 * the written value in these bits are 1. Otherwise the bits remain unchanged.
 *
 */
#define INT_CLR_BITS(node) (0x24 + (node)*NODE_CTRL_STRIDE)

/**
 * @brief This is a interrupt status register. If the value is non-zero, it will trigger an interrupt to the specific
 * node. This register is read-only.
 *
 */
#define INT_STATUS(node) (0x28 + (node)*NODE_CTRL_STRIDE)

/**
 * @brief CPU status register.
 * Bit0: wfi status.
 * Bit1: halt status.
 * Bit2: debug status.
 * Bit3: cease status.
 *
 */
#define CPU_STATUS(node) (0x2C + (node)*NODE_CTRL_STRIDE)

/**
 * @brief The following definitions are E31 IRQ numbers.
 *
 */
#define IRQ_E31_INTER_CORE_COMM 0

/**
 * @brief The register of scie hardware pattern.
 *
 */
#define SCIE_CMD_DEC_MODE 0x10C

#endif