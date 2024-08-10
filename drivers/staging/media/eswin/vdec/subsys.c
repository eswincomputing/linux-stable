// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 *    The MIT License (MIT)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a
 *    copy of this software and associated documentation files (the "Software"),
 *    to deal in the Software without restriction, including without limitation
 *    the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *    and/or sell copies of the Software, and to permit persons to whom the
 *    Software is furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 *    The GPL License (GPL)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version 2
 *    of the License, or (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *****************************************************************************
 *
 *    Note: This software is released under dual MIT and GPL licenses. A
 *    recipient may use this file under the terms of either the MIT license or
 *    GPL License. If you wish to use only one license not the other, you can
 *    indicate your decision by deleting one of the above license notices
 *    in your version of this file.
 *
 *****************************************************************************
 */

#include "subsys.h"
#include "dts_parser.h"
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#define LOG_TAG DEC_DEV_NAME ":subs"
#include "vc_drv_log.h"
/* subsystem configuration */
//#include "subsys_cfg.h"
//extern struct vcmd_config vcmd_core_array[MAX_SUBSYS_NUM];
//extern u32 total_vcmd_core_num;
//extern unsigned long multicorebase[];
//extern int irq[];
//extern unsigned int iosize[];
//extern int reg_count[];

/*
 * If VCMD is used, convert core_array to vcmd_core_array,
 * which are used in hantor_vcmd.c. Otherwise, covnert
 * core_array to multicore_base/irq/iosize,
 * which are used in hantro_dec.c

 * VCMD:
 *  - struct vcmd_config vcmd_core_array[MAX_SUBSYS_NUM]
 *  - total_vcmd_core_num

 * NON-VCMD:
 *  - multicorebase[HXDEC_MAX_CORES]
 *	- irq[HXDEC_MAX_CORES]
 *	- iosize[HXDEC_MAX_CORES]
 */
void CheckSubsysCoreArray(struct subsys_config *subsys, int *vcmd)
{
	int subsys_size = 0, core_size = 0;
	int i, j;

	for (i = 0; i < VDEC_MAX_CORE; i++) {
		if (core_array[i].iosize) {
			core_size++;
		}
	}

	for (i = 0; i < VDEC_MAX_SUBSYS; i++) {
		if (subsys_array[i].base) {
			subsys_size++;
		}
	}

	LOG_DBG("+++++++++++++++++ %s %s %d ++++++++++++++ subsys %d, core_size %d\n", "subsys.c", __func__, __LINE__, subsys_size, core_size);

	memset(subsys, 0, sizeof(subsys[0]) * MAX_SUBSYS_NUM);
	for (i = 0; i < subsys_size; i++) {
		subsys[i].base_addr = subsys_array[i].base;
		subsys[i].irq = -1;
		for (j = 0; j < HW_CORE_MAX; j++) {
			subsys[i].submodule_offset[j] = 0xffff;
			subsys[i].submodule_iosize[j] = 0;
			subsys[i].submodule_hwregs[j] = NULL;
		}
	}

	total_vcmd_core_num = 0;

	for (i = 0; i < core_size; i++) {
		if (!subsys[core_array[i].subsys].base_addr) {
			/* undefined subsystem */
			continue;
		}

        /* identifier for each subsys vc8000e=0,
         * IM=1,vc8000d=2,jpege=3,jpegd=4
         */
        if (core_array[i].core_type == HW_VC8000D)
            subsys[core_array[i].subsys].subsys_type = 2;
        else if (core_array[i].core_type == HW_VC8000DJ)
            subsys[core_array[i].subsys].subsys_type = 4;

		subsys[core_array[i].subsys].submodule_offset[core_array[i].core_type] =
			core_array[i].offset;
		subsys[core_array[i].subsys].submodule_iosize[core_array[i].core_type] =
			core_array[i].iosize;
		if (subsys[core_array[i].subsys].irq != -1 &&
		    core_array[i].irq != -1) {
			if (subsys[core_array[i].subsys].irq !=
			    core_array[i].irq) {
				LOG_INFO("hw core type %d irq %d != subsystem irq %d\n",
					core_array[i].core_type,
				       core_array[i].irq,
				       subsys[core_array[i].subsys].irq);
				LOG_INFO("hw cores ofa subsystem should have same irq\n");
			}
		} else if (core_array[i].irq != -1) {
			subsys[core_array[i].subsys].irq = core_array[i].irq;
		}
		subsys[core_array[i].subsys].has_apbfilter[core_array[i].core_type] =
			core_array[i].has_apb;
		/* vcmd found */
		if (core_array[i].core_type == HW_VCMD) {
			*vcmd = 1;
			total_vcmd_core_num++;
		}
	}

	LOG_INFO("decoding mode = %d\n", *vcmd);

	/* To plug into hantro_vcmd.c */
	if (*vcmd) {
		for (i = 0; i < total_vcmd_core_num; i++) {
			enum CoreType core_type = HW_CORE_MAX;

			if (subsys[i].subsys_type == 2)
				core_type = HW_VC8000D;
			else if (subsys[i].subsys_type == 4)
				core_type = HW_VC8000DJ;

			vcmd_core_array[i].vcmd_base_addr = subsys[i].base_addr;
			vcmd_core_array[i].vcmd_iosize =
				subsys[i].submodule_iosize[HW_VCMD];
			vcmd_core_array[i].vcmd_irq = subsys[i].irq;
			/* TODO(min): to be fixed */
			vcmd_core_array[i].sub_module_type = subsys[i].subsys_type;
			vcmd_core_array[i].submodule_main_addr =
				subsys[i].submodule_offset[core_type];
			vcmd_core_array[i].submodule_dec400_addr =
				subsys[i].submodule_offset[HW_DEC400];
			vcmd_core_array[i].submodule_L2Cache_addr =
				subsys[i].submodule_offset[HW_L2CACHE];
			vcmd_core_array[i].submodule_MMU_addr =
				subsys[i].submodule_offset[HW_MMU];
			vcmd_core_array[i].submodule_MMUWrite_addr =
				subsys[i].submodule_offset[HW_MMU_WR];
			vcmd_core_array[i].submodule_axife_addr =
				subsys[i].submodule_offset[HW_AXIFE];
		}
	}
	memset(multicorebase, 0, sizeof(multicorebase[0]) * HXDEC_MAX_CORES);
	for (i = 0; i < subsys_size; i++) {
		enum CoreType core_type = HW_CORE_MAX;

		if (subsys[i].subsys_type == 2)
			core_type = HW_VC8000D;
		else if (subsys[i].subsys_type == 4)
			core_type = HW_VC8000DJ;

		if (core_type == HW_CORE_MAX) {
			LOG_ERR("%s,%d: Unkown core type for subsys %d\n", __func__, __LINE__, i);
		}

		LOG_INFO("core type %d\n", core_type);

		multicorebase[i] = subsys[i].base_addr +
			subsys[i].submodule_offset[core_type];
		irq[i] = subsys[i].irq;
		iosize[i] = subsys[i].submodule_iosize[core_type];
		LOG_INFO("[%d] multicorebase 0x%08lx, iosize %d\n",
				i, multicorebase[i], iosize[i]);
	}
}

struct platform_device *vdec_get_platform_device(u32 core_id)
{
	struct platform_device *pdev = NULL;
	u32 core_num = 4;
	u8 numa_id;

	if (core_id >= core_num) {
		LOG_ERR("invalid core_id = %u, core_num = %u\n", core_id, core_num);
		return NULL;
	}
	numa_id = numa_id_array[core_id];

	if (0 == numa_id)
		pdev = platformdev;
	else if (1 == numa_id)
		pdev = platformdev_d1;

	return pdev;
}

int vdec_pm_runtime_sync(u32 core_id) {
	struct platform_device *pdev = vdec_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm sync, core_id = %u\n", core_id);
	}

	return pm_runtime_get_sync(&pdev->dev);
}

int vdec_pm_runtime_put(u32 core_id) {
	struct platform_device *pdev = vdec_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm put, numa_id = %u\n", core_id);
	}

	return pm_runtime_put(&pdev->dev);
}
