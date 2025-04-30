// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI procfs driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Eswin VI team
 */

#ifndef _ES_DVP2AXI_PROCFS_H
#define _ES_DVP2AXI_PROCFS_H

#ifdef CONFIG_PROC_FS

int es_dvp2axi_proc_init(struct es_dvp2axi_device *dev);
void es_dvp2axi_proc_cleanup(struct es_dvp2axi_device *dev);

#else

static inline int es_dvp2axi_proc_init(struct esisp_device *dev)
{
	return 0;
}
static inline void es_dvp2axi_proc_cleanup(struct esisp_device *dev)
{

}

#endif

#endif
