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

#ifndef ESWIN_DSP_COMMON_MAILBOX_H_
#define ESWIN_DSP_COMMON_MAILBOX_H_

#define ESWIN_MAILBOX_DSP_TO_E31_REG_BASE 0xfffb9000 /*mailbox 4*/
#define ESWIN_MAILBOX_U84_TO_DSP_REG_BASE 0xfffd0000 /*mailbox 8*/
#define ESWIN_MAILBOX_DSP_TO_U84_REG_BASE 0xfffe0000 /*mailbox 9*/

#define ESWIN_MAILBOX_DSP_TO_E31_BIT_BASE 4
#define ESWIN_MAILBOX_DSP_TO_U84_BIT_BASE 5

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

#define ESWIN_MBOX_WR_DATA0 0x00
#define ESWIN_MBOX_WR_DATA1 0x04
#define ESWIN_MBOX_RD_DATA0 0x08
#define ESWIN_MBOX_RD_DATA1 0x0C
#define ESWIN_MBOX_FIFO_STATUS 0x10
#define ESWIN_MBOX_MB_ERR 0x14
#define ESWIN_MBOX_INT_CTRL 0x18
#define ESWIN_MBOX_WR_LOCK 0x1C

#endif  // ESWIN_DSP_COMMON_MAILBOX_H_
