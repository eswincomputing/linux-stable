// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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

#ifndef __MAILBOX_REGS_H__
#define __MAILBOX_REGS_H__

#define BIT0 (1 << 0)
#define BIT1 (1 << 1)
#define BIT2 (1 << 2)
#define BIT3 (1 << 3)
#define BIT4 (1 << 4)
#define BIT5 (1 << 5)
#define BIT6 (1 << 6)
#define BIT7 (1 << 7)
#define BIT8 (1 << 8)
#define BIT9 (1 << 9)
#define BIT10 (1 << 10)
#define BIT11 (1 << 11)
#define BIT12 (1 << 12)
#define BIT13 (1 << 13)
#define BIT14 (1 << 14)
#define BIT31 ((uint32_t)1 << 31)

/**
 * @brief ESWIN_MAILBOX_REG_BASE 0x50a00000
 *
 */
#define MBOX_REG_BASE 0x50a00000

/**
 * @brief MBOX_REG_OFFSET 0x10000
 *
 */
#define MBOX_REG_OFFSET 0x10000

/**
 * @brief ESWIN_MAILBOX_U84_TO_NPU_0_REG_BASE (mailbox 4)
 *
 */
#define ESWIN_MAILBOX_U84_TO_NPU_0_REG_BASE 0x50a40000

/**
 * @brief ESWIN_MAILBOX_NPU_0_TO_U84_REG_BASE (mailbox 5)
 *
 */
#define ESWIN_MAILBOX_NPU_0_TO_U84_REG_BASE 0x50a50000

/**
 * @brief ESWIN_MAILBOX_U84_TO_DSP_0_REG_BASE (mailbox 8)
 *
 */
#define ESWIN_MAILBOX_U84_TO_DSP_0_REG_BASE 0x50a80000UL

/**
 * @brief ESWIN_MAILBOX_U84_TO_DSP_1_REG_BASE (mailbox 10)
 *
 */
#define ESWIN_MAILBOX_U84_TO_DSP_1_REG_BASE 0x50aa0000UL

/**
 * @brief ESWIN_MAILBOX_U84_TO_DSP_2_REG_BASE (mailbox 12)
 *
 */
#define ESWIN_MAILBOX_U84_TO_DSP_2_REG_BASE 0x50ac0000UL

/**
 * @brief ESWIN_MAILBOX_U84_TO_DSP_3_REG_BASE (mailbox 14)
 *
 */
#define ESWIN_MAILBOX_U84_TO_DSP_3_REG_BASE 0x50ae0000UL

/**
 * @brief MBOX_NPU_WR_DATA0_OFFSET
 *
 */
#define MBOX_NPU_WR_DATA0_OFFSET 0

/**
 * @brief MBOX_NPU_WR_DATA1_OFFSET
 *
 */
#define MBOX_NPU_WR_DATA1_OFFSET 0x4

/**
 * @brief MBOX_NPU_RD_DATA0_OFFSET
 *
 */
#define MBOX_NPU_RD_DATA0_OFFSET 0x8

/**
 * @brief MBOX_NPU_RD_DATA1_OFFSET
 *
 */
#define MBOX_NPU_RD_DATA1_OFFSET 0xC

/**
 * @brief MBOX_NPU_FIFO_OFFSET
 *
 */
#define MBOX_NPU_FIFO_OFFSET 0x10

/**
 * @brief MBOX_NPU_INT_OFFSET
 *
 */
#define MBOX_NPU_INT_OFFSET 0x18

/**
 * @brief MBOX_NPU_WR_LOCK
 *
 */
#define MBOX_NPU_WR_LOCK 0x1C

/**
 * @brief MBOX_NPU_MAX_SIZE 0x80
 *
 */
#define MBOX_NPU_MAX_SIZE 0x80

/**
 * @brief MBOX_WRITE_FIFO_BIT
 *
 */
#define MBOX_WRITE_FIFO_BIT BIT31

/**
 * @brief indicate FIFO is empty
 *
 */
#define MBOX_FIFO_STATUS_EMPTY 0x2

/**
 * @brief Mailbox interrupt to u84
 *
 */
#define MBOX_INT_TO_U84 BIT0

/**
 * @brief Mailbox interrupt to npu0
 *
 */
#define MBOX_INT_TO_NPU0 BIT3

/**
 * @brief Mailbox interrupt to npu1
 *
 */
#define MBOX_INT_TO_NPU1 BIT4

/**
 * @brief Mailbox interrupt to dsp0
 *
 */
#define MBOX_INT_TO_DSP0 BIT5

/**
 * @brief Mailbox interrupt to dsp1
 *
 */
#define MBOX_INT_TO_DSP1 BIT6

/**
 * @brief Mailbox interrupt to dsp2
 *
 */
#define MBOX_INT_TO_DSP2 BIT7

/**
 * @brief Mailbox interrupt to dsp3
 *
 */
#define MBOX_INT_TO_DSP3 BIT8

/**
 * @brief Mailbox channel
 *
 */
#define MBOX_CHN_NPU_TO_U84 5
#define MBOX_CHN_NPU_TO_DSP0 8
#define MBOX_CHN_NPU_TO_DSP1 10
#define MBOX_CHN_NPU_TO_DSP2 12
#define MBOX_CHN_NPU_TO_DSP3 14

#define ESWIN_MAILBOX_NPU_LOCK_BIT BIT3

static u32 MAILBOX_E31_TO_DSP_REG[] = {ESWIN_MAILBOX_U84_TO_DSP_0_REG_BASE, ESWIN_MAILBOX_U84_TO_DSP_1_REG_BASE,
                                       ESWIN_MAILBOX_U84_TO_DSP_2_REG_BASE, ESWIN_MAILBOX_U84_TO_DSP_3_REG_BASE};

static u32 MAILBOX_E31_TO_DSP_INT[] = {MBOX_INT_TO_DSP0, MBOX_INT_TO_DSP1, MBOX_INT_TO_DSP2, MBOX_INT_TO_DSP3};

#define MBOX_INT_BIT_BASE 5

#define MBX_LOCK_BIT_TIMEOUT 60000

#endif
