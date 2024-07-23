// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

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
