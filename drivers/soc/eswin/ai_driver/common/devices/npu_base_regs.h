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

#ifndef __NPU_BASE_REGS_H__
#define __NPU_BASE_REGS_H__

#include "mailbox_regs.h"

#define NPU_TOP_CTRL 0x0
#define NPU_TOP_INT_STATUS 0x10
#define NPU_TOP_INT_CLEAR 0x14
#define NPU_TOP_INT_ENABLE 0x18

#define NPU_QOS_0 0x20
#define NPU_QOS_SEL 0x30

/**
 * @brief NPU hardware module indices.
 *
 */
typedef enum type_NPU_MODULE_SEL_ID_E {
    NPU_MODULE_PEC0_S = 0x0,
    NPU_MODULE_PEC0_D = 0x1,

    NPU_MODULE_EDMA = 0x110,

    NPU_MODULE_RDMA0_S = 0x112,
    NPU_MODULE_RDMA0_D = 0x113,

    NPU_MODULE_RDMA_WRAPPER0_S = 0x128,
    NPU_MODULE_RDMA_WRAPPER0_D = 0x129,

    NPU_MODULE_PRE_DRP_RESHARP_S = 0x130,
    NPU_MODULE_PRE_DRP_RESHARP_D = 0x131,

    NPU_MODULE_PRE_DRP_WR0_S = 0x132,
    NPU_MODULE_PRE_DRP_WR0_D = 0x133,

    NPU_MODULE_POST_DRP0_S = 0x13a,
    NPU_MODULE_POST_DRP0_D = 0x13b,

    NPU_MODULE_POST_DRP_SDP_S = NPU_MODULE_POST_DRP0_S,
    NPU_MODULE_POST_DRP_SDP_D = NPU_MODULE_POST_DRP0_D,
    NPU_MODULE_POST_DRP_PDP_S = 0x13c,
    NPU_MODULE_POST_DRP_PDP_D = 0x13d,

    NPU_MODULE_SDP_RDMA = 0x15a,
    NPU_MODULE_SDP = 0x15b,
    NPU_MODULE_PDP_RDMA = 0x15c,
    NPU_MODULE_PDP = 0x15d,
    NPU_MODULE_RUBIK = 0x160,

    NPU_MODULE_E21_S = 0x180,
    NPU_MODULE_CDMA_S = 0x181,

    NPU_MODULE_LLC_0_S = 0x188,
    NPU_MODULE_LLC_1_S = 0x189,

    NPU_MODULE_MAC_TOP0_S = 0x190,
    NPU_MODULE_MAC_TOP0_D = 0x191,

    NPU_MODULE_TOP_S = 0x198,
    NPU_MODULE_TOP_D = 0x199,

    NPU_MODULE_MAX_SIZE = 0x200,
} NPU_MODULE_SEL_ID_E;

/* NPU system port base address */
#define NPU_SYS_BASE_ADDR 0x51C00000
/* NPU configuration base address */
#define NPU_CFG_BASE_ADDR 0x51828000

/* NPU PP port base address */
#define NPU_PP_BASE_ADDR 0x20000000

/* NPU sram base address */
#define NPU_SRAM_BASE_ADDR 0x59000000

/* SYS CON base address */
#define SYS_CON_BASE_ADDR 0x51810000

/* Power domain management control base address */
#define PMC_BASE_ADDR 0x51800000

#define PMC_REG_MAX 0x9000

/* SYS CON noc_cfg0 */
#define SYS_CON_NOC_CFG0 (SYS_CON_BASE_ADDR + CON_NOC_CFG_0)

/* SYS CON test_reg_0 */
#define SYS_CON_TEST_REG_0 (SYS_CON_BASE_ADDR + CON_TEST_REG_0)

/**
 * @brief Defines the stride between neighbor modules.
 *
 */
#define NPU_MODULE_STRIDE 0x1000

/**
 * @brief Defines the hardware instance address range.
 *
 */
#define NPU_HW_ADDR_RANGE (NPU_MODULE_STRIDE * 2)

/**
 * @brief Defines the module base of each hardware instance.
 *
 */
#define NPU_GET_MODULE_BASE(n) (NPU_SYS_BASE_ADDR + (n)*NPU_MODULE_STRIDE)

/**
 * @brief Defines the broadcast address offset of RDMAs and PECs.
 *
 */
#define NPU_BROADCAST_OFFSET 0x800

/**
 * @brief Defines the multicast address offset of PECs.
 *
 */
#define NPU_MULTICAST_OFFSET 0x400

/**
 * @brief The max sram size 4M.
 *
 */
#define SRAM_BYTE_SIZE 0x400000

/**
 * @brief The base address of control register space.
 *
 */
#define NPU_CTRL_OFFSET (0x51D80000 - NPU_CFG_BASE_ADDR)

/**
 * @brief NPU_TOP_CSR_OFFSET
 *
 */
#define NPU_MAPPING_BASE 0x51c00000
#define NPU_TOP_CSR_OFFSET (NPU_MAPPING_BASE - NPU_CFG_BASE_ADDR + (0x198 << 12))

/**
 * @brief NPU configuration space range size
 *
 */
#define NPU_CFG_ADDR_RANGE (0x5A920000 - NPU_CFG_BASE_ADDR)

/**
 * @brief The base address of E31 CPU space.
 *
 */
#define NPU_CPU_BASE_ADDR 0x5A000000

/**
 * @brief The base address of E31 CPU space.
 *
 */
#define NPU_CPU_OFFSET (NPU_CPU_BASE_ADDR - NPU_CFG_BASE_ADDR)

/**
 * @brief The size in bytes of each E31 CPU space.
 *
 */
#define NPU_CPU_SIZE 0x00100000

/**
 * @brief The ITim offset relative to NPU_CPU_BASE_ADDR.
 *
 */
#define NPU_ITIM_OFFSET 0

/**
 * @brief The DTim offset relative to NPU_CPU_BASE_ADDR.
 *
 */
#define NPU_DTIM_OFFSET 0x10000

/**
 * @brief The DTim size of each E31 core.
 *  From npu_metal_master.lds, npu_metal_aux.lds, npu_metal_major.lds
 */
#define E31_EMISSION_DTIM_SIZE 0x4000U
#define E31_PROGRAM_DTIM_SIZE 0x4000U
#define E31_MAJOR_DTIM_SIZE 0x2000U

/**
 * @brief The reset and clock gating address register space.
 *
 */
#define NPU_RESET_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_SYS_RESET)

/**
 * @brief The base address of configuration DMA register space.
 *
 */
#define NPU_CDMA_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_CDMA_S)

/**
 * @brief The base address of control register space.
 *
 */
#define NPU_CTRL_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_E21_S)

/**
 * @brief The base address of RDMA register space.
 *
 */
#define NPU_RDMA_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_RDMA0_S)
#define NPU_RDMA_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_RDMA0_D)

/**
 * @brief The base address of PEC register space.
 *
 */
#define NPU_PEC_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PEC0_S)
#define NPU_PEC_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PEC0_D)

/**
 * @brief The base address of PreDRP_WRITE register space.
 *
 */
#define NPU_DRP_WR_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PRE_DRP_WR0_S)
#define NPU_DRP_WR_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PRE_DRP_WR0_D)

/**
 * @brief The base address of DRP_RESHARP register space.
 *
 */
#define NPU_DRP_RESHARP_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PRE_DRP_RESHARP_S)
#define NPU_DRP_RESHARP_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_PRE_DRP_RESHARP_D)

/**
 * @brief The base address of NPU top register space.
 *
 */
#define NPU_TOP_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_TOP_S)
#define NPU_TOP_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_TOP_D)

/**
 * @brief The base address of NPU llc space.
 *
 */
#define NPU_LLC_0_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_LLC_0_S)
#define NPU_LLC_1_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_LLC_1_S)

/**
 * @brief The base address of NPU MAC register space.
 *
 */
#define NPU_MAC_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_MAC_TOP0_S)
#define NPU_MAC_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_MAC_TOP0_D)

/**
 * @brief The base address of NPU RDMA wrapper register space.
 *
 */
#define NPU_RMDA_WRAPPER_S_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_RDMA_WRAPPER0_S)
#define NPU_RMDA_WRAPPER_D_BASE_ADDR NPU_GET_MODULE_BASE(NPU_MODULE_RDMA_WRAPPER0_D)

/**
 * @brief The base address of NPU hardware modules register space.
 *
 */
#define NPU_HW_MODULE_BASE_ADDR NPU_GET_MODULE_BASE(0)

/**
 * @brief The size in bytes of NPU hardware modules register space.
 *
 */
#define NPU_HW_MODULE_SIZE (NPU_MODULE_STRIDE * NPU_MODULE_MAX_SIZE)

/**
 * @brief The offset address of npu nvdla.
 *
 */
#define NPU_NVDLA_ADDRESS_OFFSET 0x150000

/**
 * @brief The int status of npu nvdla.
 *
 */
#define NPU_NVDLA_INT_STATUS 0xc

#define CONV_EDMA_STATUS_ADDR (NPU_MODULE_TOP_S * NPU_MODULE_STRIDE + NPU_TOP_INT_STATUS)
#define DLA_OP_STATUS_ADDR (NPU_NVDLA_ADDRESS_OFFSET + NPU_NVDLA_INT_STATUS)

/**
 * @brief Assert the register access address is legal.
 *
 */
#define ASSERT_REG_ADDR(addr)                                                                                   \
    ASSERT(((addr)-NPU_HW_MODULE_BASE_ADDR) < NPU_HW_MODULE_SIZE || ((addr)-SYS_CON_BASE_ADDR) < CON_REG_MAX || \
           ((addr)-PMC_BASE_ADDR) < PMC_REG_MAX || ((addr)-ESWIN_MAILBOX_NPU_0_TO_U84_REG_BASE) < MBOX_NPU_MAX_SIZE)

/**
 * @brief The ssid of write and read operation.
 *
 */
#define NPU_DMA_SSID GENMASK(27, 8)

/**
 * @brief The sid of write and read operation.
 *
 */
#define NPU_DMA_SID GENMASK(7, 0)  //

/**
 * @brief NPU_DMA_MMU_RID_REG_OFFSET
 *
 */
#define NPU_DMA_MMU_RID_REG_OFFSET 0x34

/**
 * @brief NPU_DMA_MMU_WID_REG_OFFSET
 *
 */
#define NPU_DMA_MMU_WID_REG_OFFSET 0x38

/* NPU reset and clock base address */

/**
 * @brief npu aclk ctrl
 *
 */
#define NPU_ACLK_CTRL (0x51828178 - NPU_CFG_BASE_ADDR)

/**
 * @brief npu llc ctrl
 *
 */
#define NPU_LLC_CTRL (0x5182817C - NPU_CFG_BASE_ADDR)

/**
 * @brief npu core ctrl
 *
 */
#define NPU_CORE_CTRL (0x51828180 - NPU_CFG_BASE_ADDR)

/**
 * @brief npu rst ctrl
 *
 */
#define NPU_RST_CTRL (0x51828418 - NPU_CFG_BASE_ADDR)

/**
 * @brief NPU clamp control register. Refer to https://ekm.eswincomputing.com/preview.html?fileid=1179453
 *
 */
#define NPU_CLAMP_CTRL (0x518081C4 - NPU_CFG_BASE_ADDR)

/**
 * @brief Control JTAG chain behaviors. Refer to https://ekm.eswincomputing.com/preview.html?fileid=641149
 *
 */
#define JTAG_CHAIN_CTRL (0x518103BC - NPU_CFG_BASE_ADDR)

/**
 * @brief The base address of scie hardware pattern.
 *
 */
#define NPU_SCIE_CMD_OFFSET (NPU_CTRL_OFFSET + SCIE_CMD_DEC_MODE)

/********************NPU TOP**********************/
#define REG_NPU_TOP_INT_STATUS (NPU_TOP_S_BASE_ADDR + NPU_TOP_INT_STATUS)
#define REG_NPU_TOP_INT_CLEAR (NPU_TOP_S_BASE_ADDR + NPU_TOP_INT_CLEAR)

/****************NPU E31 DTIM***************/
#define E31_MASTER_DTIM_BASE (NPU_CPU_BASE_ADDR + NPU_DTIM_OFFSET)
#define HOST2DEV_IPC_BASE_ADDR E31_MASTER_DTIM_BASE
#define E31_EMISSION_DTIM_BASE (E31_MASTER_DTIM_BASE + NPU_CPU_SIZE * EMISSION_CORE_ID)
#define E31_PROGRAM_DTIM_BASE (E31_MASTER_DTIM_BASE + NPU_CPU_SIZE * PROGRAM_CORE_ID)
#define E31_MAJOR_DTIM_BASE(core_id) (E31_MASTER_DTIM_BASE + NPU_CPU_SIZE * (MAJOR_0_CORE_ID + core_id))

#define UART_MUTEX_BASE_ADDR 0x51820000
#define UART_MUTEX_ADDR_SIZE 0x1000
#define UART_MUTEX_UNIT_OFFSET 4

#endif
