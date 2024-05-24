/*
 * Program's name, and a brief idea of what it does（One line）.
 * Copyright 20XX, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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

#ifndef __ESWIN_DSP_SUBSYS_H__
#define __ESWIN_DSP_SUBSYS_H__
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

typedef int (*dsp_subsys_status_pfunc)(void);

struct es_dsp_subsys {
	void __iomem *reg_base;
	void __iomem *con_reg_base;
	struct regmap *map;
	struct regmap *con_map;
	struct platform_device *pdev;

	struct reset_control *rstc_axi;
	struct reset_control *rstc_cfg;
	struct reset_control *rstc_div4;
	struct reset_control *rstc_div_0;
	struct reset_control *rstc_div_1;
	struct reset_control *rstc_div_2;
	struct reset_control *rstc_div_3;
	struct clk *cfg_clk;
	dsp_subsys_status_pfunc dsp_subsys_status;
};
#endif