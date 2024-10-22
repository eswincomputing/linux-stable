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
#ifndef __DTS_PARSER_H__
#define __DTS_PARSER_H__

#define VDEC_MAX_SUBSYS 4
#define	VDEC_MAX_CORE 12
#define VDEC_ADDR_OFFSET_MASK 0xffff

int vdec_device_nodes_check(void);
extern struct SubsysDesc subsys_array[VDEC_MAX_SUBSYS];
extern struct CoreDesc core_array[VDEC_MAX_CORE];
extern u8 numa_id_array[4];
extern int vdec_trans_device_nodes(struct platform_device *pdev, u8 numa_id);

#endif /* __DTS_PARSER_H__ */
