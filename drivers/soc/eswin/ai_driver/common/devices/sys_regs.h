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
