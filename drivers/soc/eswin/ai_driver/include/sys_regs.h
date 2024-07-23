// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __SYSTEM_REGS_H__
#define __SYSTEM_REGS_H__

/**
 * @brief SYS CON register indices.
 *
 */
typedef enum type_SYS_CON_REG_E {
    CON_DYNM_CSR_EN = 0x0000,
    CON_NOC_CFG_0 = 0x0324,
    CON_TEST_REG_0 = 0x0668,
    CON_SEC_SID = 0x4004,

    CON_REG_MAX,
} SYS_CON_REG_E;

typedef enum type_NPU_LLC_E {
    NPU_LLC_0 = 0,
    NPU_LLC_1 = 1,
    NPU_LLC_MAX,
} NPU_LLC_E;

#endif
