// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Noc Driver
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
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */

#include <linux/debugfs.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include "noc.h"
#include "noc_regs.h"

static const char *win2030_noc_busy_idle_enum[] = {
	"busy",
	"idle",
	NULL
};

int win2030_noc_sideband_mgr_query(u32 sbm_id)
{
	struct win2030_bitfield *bf = NULL;
	struct win2030_register *reg;
	struct win2030_noc_sideband_magr *sbm = NULL;
	int ret;
	unsigned value;

	if (IS_ERR_OR_NULL(win2030_noc_ctrl.win2030_noc_root_debug_dir) ||
		list_empty(&win2030_noc_ctrl.sbm_link)) {
			pr_info("Win2030 NOC SideBand driver not initialisation, will return idle directly!");
			return 1;
	}
	list_for_each_entry(sbm, &win2030_noc_ctrl.sbm_link, link) {
		reg = sbm->SenseIn0;
		list_for_each_entry(bf, &reg->bitfields, link) {
			if (sbm_id == bf->sbm_id) {
				ret = win2030_bitfield_read(bf, &value);
				if (ret) {
					return -EIO;
				} else {
					return value;
				}
			}
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(win2030_noc_sideband_mgr_query);

int win2030_noc_sideband_mgr_debug_init(struct device *_dev)
{
	int ret;
	struct dentry *sbm_dir;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_sideband_magr *sbm = noc_device->sbm;

	if (NULL == sbm) {
		return 0;
	}

	sbm_dir = debugfs_create_dir("sideband_manager", noc_device->dir);
	if (IS_ERR(sbm_dir))
		return PTR_ERR(sbm_dir);

	ret = win2030_noc_debug_reg_init(sbm->SenseIn0, sbm_dir);
	if (ret < 0) {
		return ret;
	}
	return 0;
}

static int win2030_noc_register_sideband_mgr(struct device_node *np,
		struct device *_dev)
{
	int ret, i;
	struct win2030_noc_sideband_magr *sbm;
	struct win2030_register *reg;
	struct win2030_bitfield *bf;
	int size;
	u32 *values_tab;
	int offset;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	const char **bf_name;

	/* Initialise mgr */
	sbm = devm_kcalloc(_dev, 1, sizeof(struct win2030_noc_sideband_magr), GFP_KERNEL);
	sbm->hw_base = of_iomap(np, 0);
	if (IS_ERR_OR_NULL(sbm->hw_base)) {
		return -EINVAL;
	}
	sbm->dev = _dev;

	/**
	 * Register: SenseIn0
	 */
	reg = sbm->SenseIn0 = win2030_new_register(_dev, "SenseIn0",
			sbm->hw_base, SBM_SENSE_IN0, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	size = of_property_count_elems_of_size(np, "SenseIn0", sizeof(u32) * 2);
	if (size < 0) {
		return -ENOENT;
	}
	values_tab = devm_kcalloc(_dev, size * 2, sizeof(u32), GFP_KERNEL);
	if (!values_tab) {
		return -ENOMEM;
	}
	ret = of_property_read_u32_array(np, "SenseIn0", values_tab, size * 2);
	if (ret) {
		dev_err(_dev, "Error while parsing SenseIn0 bitfield, ret %d!\n", ret);
		return -ENOENT;
	}
	bf_name = win2030_get_strings_from_dts(_dev, np, "bf-name");
	if (IS_ERR(bf_name)) {
		ret = PTR_ERR(bf_name);
		dev_err(_dev, "Error %d while get sbm bf name!\n", ret);
		return ret;
	}
	for (i = 0; i < size; i++) {
		offset = values_tab[2 * i + 1];
		bf = win2030_new_bitfield(reg, bf_name[i], offset, SBM_SENSE_IN0_SENSE_IN0_WIDTH,
				win2030_noc_busy_idle_enum);
		if (IS_ERR_OR_NULL(bf))
			return -EIO;

		bf->sbm_id = values_tab[2 * i];
	}
	kfree(bf_name);
	noc_device->sbm = sbm;
	list_add_tail(&sbm->link, &win2030_noc_ctrl.sbm_link);
	return 0;
}


/**
 * Create sideband manager
 * @param _dev
 * @return
 */
int win2030_noc_init_sideband_mgr(struct device *_dev)
{
	int ret;
	struct device_node *child = NULL;

	for_each_child_of_node(_dev->of_node, child) {
		if (of_device_is_compatible(child, "eswin,win2xxx-noc-sideband-manager")) {
			ret = win2030_noc_register_sideband_mgr(child, _dev);
			if (0 != ret) {
				dev_err(_dev, "Error while register sideband manager\n");
				return ret;
			}
		}
	}
	return 0;
}
