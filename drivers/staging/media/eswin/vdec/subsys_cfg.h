// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN video decoder driver
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
 */
struct SubsysDesc subsys_array[] ={
    {0, 0, 0x50100000},
    {0, 1, 0x50120000},
//  {0, 2, 0x60100000},
//  {0, 3, 0x60120000},
};

struct CoreDesc core_array[] ={
/* slice_id, subsystem_id,  core_type,   offset,               iosize,         irq, has_apb */
//    {0,             0,      HW_VCMD,        0x0,               27 * 4,          232,     0},
    {0,             0,      HW_AXIFE,    0x0200,                 64 * 4,          -1,     0},
    {0,             0,      HW_VC8000D, 0x0800,      MAX_REG_COUNT * 4,          -1,     0},
//    {0,             1,      HW_VCMD,        0x0,               27 * 4,          233,     0},
    {0,             1,      HW_AXIFE,    0x0200,                 64 * 4,          -1,     0},
    {0,             1,      HW_VC8000D,  0x0800,      MAX_REG_COUNT * 4,          -1,     0},
};
