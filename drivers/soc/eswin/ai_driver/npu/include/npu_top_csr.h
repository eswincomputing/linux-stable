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

/*
 * npu_top_csr
 */

#ifndef __NPU_TOP_CSR_H_
#define __NPU_TOP_CSR_H_

// define npu top csr base addr
#define NPU_TOP_CSR_BASE 0x198000

// define offset of module
#define E31_TASK0_PING_OFFSET 0
#define E31_TASK1_PONG_OFFSET 1
#define RDMA_TASK0_PING_OFFSET 2
#define RDMA_TASK0_PONG_OFFSET 3
#define RDMA_ERROR_OFFSET 4
#define PEC_TASK0_PING_OFFSET 5
#define PEC_TASK0_PONG_OFFSET 6
#define PEC_ERROR_OFFSET 7
#define PRE_DRP_PING_OFFSET 8
#define PRE_DRP_PONG_OFFSET 9
#define PRE_DRP_ERROR_OFFSET 10
#define EDMA_INTR0_OFFSET 11
#define EDMA_INTR1_OFFSET 12
#define EDMA_ERROR_OFFSET 13
#define EDMA_IRQ_MASK                                          \
	((1 << EDMA_INTR0_OFFSET) + (1 << EDMA_INTR1_OFFSET) + \
	 (1 << EDMA_ERROR_OFFSET))

typedef enum {
	REG_NPU_CTRL = 0x0,
	REG_NPU_INT_STATUS = 0x10,
	REG_NPU_INT_CLEAR = 0x14,
	REG_NPU_INT_ENABLE = 0x18,
	REG_NPU_QQS_0 = 0x20,
	REG_NPU_QQS_1 = 0x24,
	REG_NPU_QQS_2 = 0x28,
	REG_NPU_QQS_3 = 0x2c,
	REG_NPU_QQS_SEL = 0x30,
	REG_NPU_MMU_RID = 0x34,
	REG_NPU_MMU_WID = 0x38,
} REG_NPU_TOP;

#endif /* __NPU_TOP_CSR_H_ */
