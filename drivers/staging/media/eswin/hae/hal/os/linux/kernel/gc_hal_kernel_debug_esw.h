// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN HAE Debug Proc Header File 
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Zhilin Lei <leizhilin@eswincomputing.com>
 */

#ifndef __gc_hal_kernel_debug_esw_h__
#define __gc_hal_kernel_debug_esw_h__

#include "gc_hal_kernel_linux.h"

int gc_hal_kernel_dbg_esw_create_procfs(gckGALDEVICE g_dev);
void gc_hal_kernel_dbg_esw_remove_procfs(void);

#endif