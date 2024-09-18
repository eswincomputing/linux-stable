/*
 * ESWIN eic770x cpu_nid and hart_id convert
 * Copyright 2023, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
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
 */

#ifndef __ESWIN_HARDID_CPUID_CONVERT_H
#define __ESWIN_HARDID_CPUID_CONVERT_H
#include <asm/smp.h>

#ifdef CONFIG_SMP
/*
 * Mapping between linux logical cpu index and hartid.
 */
extern unsigned long __eswin_cpuid_to_hartid_map[NR_CPUS];
#define eswin_cpuid_to_hartid_map(cpu)    __eswin_cpuid_to_hartid_map[cpu]

#endif

#endif
