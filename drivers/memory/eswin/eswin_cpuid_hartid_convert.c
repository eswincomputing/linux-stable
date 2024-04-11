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
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include "eswin_cpuid_hartid_convert.h"

unsigned long __eswin_cpuid_to_hartid_map[NR_CPUS] = {
	[0 ... NR_CPUS-1] = INVALID_HARTID
};
EXPORT_SYMBOL_GPL(__eswin_cpuid_to_hartid_map);

static int __init eswin_cpuid_hartid_convert_init(void)
{
	int i;
	
	for (i = 0; i < NR_CPUS; i++)
		eswin_cpuid_to_hartid_map(i) = cpuid_to_hartid_map(i);
	
	return 0;
}
arch_initcall(eswin_cpuid_hartid_convert_init);