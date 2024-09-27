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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/win2030_noc.h>
#include <dt-bindings/interconnect/eswin,win2030.h>
#include "eswin-dsp-subsys.h"

#define DRIVER_NAME "eswin-dsp-subsys"

/**
 * dsp_subsys_status - query the dsp subsys transaction status
 *
 * @void
 *
 * return: module transaction status on success , 1 if idle, 0 if busy.
 *	negative for error
 *	if can't get idle status in 3 seconds, return current status.
 */
static int dsp_subsys_status(void)
{
	unsigned long deadline = jiffies + 3 * HZ;
	int status = 0;

	do {
		status = win2030_noc_sideband_mgr_query(SBM_DSPT_SNOC);
		status |= win2030_noc_sideband_mgr_query(SBM_CNOC_DSPT);
		if (0 != status) {
			break;
		}
		schedule();
	} while (time_before(jiffies, deadline));

	return status;
}

static inline int dsp_subsys_clk_init(struct platform_device *pdev,
									  struct es_dsp_subsys *subsys)
{
	int ret;

	subsys->cfg_clk = devm_clk_get(&pdev->dev, "cfg_clk");
	if (IS_ERR(subsys->cfg_clk)) {
		ret = PTR_ERR(subsys->cfg_clk);
		dev_err(&pdev->dev, "failed to get cfg_clk: %d\n", ret);
		return ret;
	}

	subsys->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(subsys->aclk)) {
		ret = PTR_ERR(subsys->aclk);
		dev_err(&pdev->dev, "failed to get aclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int dsp_subsys_reset(struct es_dsp_subsys *subsys)
{
	int ret;

	/*reset dsp bus*/
	ret = reset_control_reset(subsys->rstc_axi);
	WARN_ON(0 != ret);

	ret = reset_control_reset(subsys->rstc_div4);
	WARN_ON(0 != ret);

	/*reset dsp cfg*/
	ret = reset_control_reset(subsys->rstc_cfg);
	WARN_ON(0 != ret);

	/*reset dsp core clk div*/
	ret = reset_control_reset(subsys->rstc_div_0);
	WARN_ON(0 != ret);

	ret = reset_control_reset(subsys->rstc_div_1);
	WARN_ON(0 != ret);

	ret = reset_control_reset(subsys->rstc_div_2);
	WARN_ON(0 != ret);

	ret = reset_control_reset(subsys->rstc_div_3);
	WARN_ON(0 != ret);

	return 0;
}

static int dsp_subsys_aclk_enable(struct es_dsp_subsys *subsys)
{
	int ret;

	ret = clk_prepare_enable(subsys->aclk);
	if (ret) {
		dev_err(&subsys->pdev->dev, "failed to enable aclk: %d\n", ret);
		return ret;
	}
	return 0;
}

static int dsp_subsys_reset_init(struct platform_device *pdev,
								 struct es_dsp_subsys *subsys)
{
	subsys->rstc_axi = devm_reset_control_get_optional(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(subsys->rstc_axi)) {
		dev_err(&subsys->pdev->dev, "Failed to get axi reset handle\n");
		return -EFAULT;
	}

	subsys->rstc_div4 = devm_reset_control_get_optional(&pdev->dev, "div4");
	if (IS_ERR_OR_NULL(subsys->rstc_div4)) {
		dev_err(&subsys->pdev->dev, "Failed to div4 reset handle\n");
		return -EFAULT;
	}

	subsys->rstc_cfg = devm_reset_control_get_optional(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(subsys->rstc_cfg)) {
		dev_err(&subsys->pdev->dev, "Failed to get cfg reset handle\n");
		return -EFAULT;
	}

	subsys->rstc_div_0 = devm_reset_control_get_optional(&pdev->dev, "div_0");
	if (IS_ERR_OR_NULL(subsys->rstc_div_0)) {
		dev_err(&subsys->pdev->dev, "Failed to div_0 reset handle\n");
		return -EFAULT;
	}
	subsys->rstc_div_1 = devm_reset_control_get_optional(&pdev->dev, "div_1");
	if (IS_ERR_OR_NULL(subsys->rstc_div_1)) {
		dev_err(&subsys->pdev->dev, "Failed to div_1 reset handle\n");
		return -EFAULT;
	}
	subsys->rstc_div_2 = devm_reset_control_get_optional(&pdev->dev, "div_2");
	if (IS_ERR_OR_NULL(subsys->rstc_div_2)) {
		dev_err(&subsys->pdev->dev, "Failed to div_2 reset handle\n");
		return -EFAULT;
	}
	subsys->rstc_div_3 = devm_reset_control_get_optional(&pdev->dev, "div_3");
	if (IS_ERR_OR_NULL(subsys->rstc_div_3)) {
		dev_err(&subsys->pdev->dev, "Failed to div_3 reset handle\n");
		return -EFAULT;
	}
	return 0;
}

static int dsp_subsys_reg_read(void *context, unsigned int reg,
							   unsigned int *val)
{
	struct es_dsp_subsys *subsys = context;

	*val = readl_relaxed(subsys->reg_base + reg);
	return 0;
}

static int dsp_subsys_reg_write(void *context, unsigned int reg,
								unsigned int val)
{
	struct es_dsp_subsys *subsys = context;

	writel_relaxed(val, subsys->reg_base + reg);
	return 0;
}

static int dsp_subsys_con_reg_read(void *context, unsigned int reg,
								   unsigned int *val)
{
	struct es_dsp_subsys *subsys = context;

	*val = readl_relaxed(subsys->con_reg_base + reg);
	return 0;
}

static int dsp_subsys_con_reg_write(void *context, unsigned int reg,
									unsigned int val)
{
	struct es_dsp_subsys *subsys = context;

	writel_relaxed(val, subsys->con_reg_base + reg);
	return 0;
}

/**
 * dsp_subsys_init_regmap() - Initialize registers map
 *
 * Autodetects needed register access mode and creates the regmap with
 * corresponding read/write callbacks. This must be called before doing any
 * other register access.
 */
static int dsp_subsys_init_regmap(struct es_dsp_subsys *subsys)
{
	struct regmap_config map_cfg = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.use_hwlock = true,
		.cache_type = REGCACHE_NONE,
		.can_sleep = false,
		.reg_read = dsp_subsys_reg_read,
		.reg_write = dsp_subsys_reg_write,
	};
	struct regmap_config con_map_cfg = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.use_hwlock = true,
		.cache_type = REGCACHE_NONE,
		.can_sleep = false,
		.reg_read = dsp_subsys_con_reg_read,
		.reg_write = dsp_subsys_con_reg_write,
	};

	/*
	 * Note we'll check the return value of the regmap IO accessors only
	 * at the probe stage. The rest of the code won't do this because
	 * basically we have MMIO-based regmap so non of the read/write methods
	 * can fail.
	 */
	subsys->map = devm_regmap_init(&subsys->pdev->dev, NULL, subsys, &map_cfg);
	if (IS_ERR(subsys->map)) {
		dev_err(&subsys->pdev->dev, "Failed to init the registers map\n");
		return PTR_ERR(subsys->map);
	}

	subsys->con_map = devm_regmap_init(&subsys->pdev->dev, NULL, subsys, &con_map_cfg);
	if (IS_ERR(subsys->con_map)) {
		dev_err(&subsys->pdev->dev, "Failed to init the con registers map\n");
		return PTR_ERR(subsys->con_map);
	}

	return 0;
}

static int es_dsp_subsys_probe(struct platform_device *pdev)
{
	struct es_dsp_subsys *subsys;
	int ret;
	struct resource *res;

	dev_info(&pdev->dev, "%s\n", __func__);
	subsys = devm_kzalloc(&pdev->dev, sizeof(*subsys), GFP_KERNEL);
	if (!subsys) {
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, subsys);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	subsys->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(subsys->reg_base)) {
		return PTR_ERR(subsys->reg_base);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	subsys->con_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(subsys->con_reg_base)) {
		return PTR_ERR(subsys->con_reg_base);
	}

	subsys->pdev = pdev;

	ret = dsp_subsys_init_regmap(subsys);
	if (0 != ret) {
		return -ENODEV;
	}

	ret = dsp_subsys_reset_init(pdev, subsys);
	if (0 != ret) {
		return ret;
	}

	ret = dsp_subsys_clk_init(pdev, subsys);
	if (0 != ret) {
		return ret;
	}

	ret = dsp_subsys_aclk_enable(subsys);
	if (0 != ret) {
		return ret;
	}

	ret = dsp_subsys_reset(subsys);
	if (0 != ret) {
		return ret;
	}

	subsys->dsp_subsys_status = dsp_subsys_status;

	/* enable qos */
	// win2030_noc_qos_set("DSPT");

	return 0;
}

static int es_dsp_subsys_remove(struct platform_device *pdev)
{
	struct es_dsp_subsys *subsys = dev_get_drvdata(&pdev->dev);
	if (subsys) {
		clk_disable_unprepare(subsys->cfg_clk);
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id es_dsp_subsys_match[] = {
	{
		.compatible = "es-dsp-subsys",
	},
	{},
};
MODULE_DEVICE_TABLE(of, es_dsp_subsys_match);
#endif

static struct platform_driver es_dsp_subsys_driver = {
	.probe   = es_dsp_subsys_probe,
	.remove  = es_dsp_subsys_remove,
	.driver  = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(es_dsp_subsys_match),
	},
};

module_platform_driver(es_dsp_subsys_driver);

MODULE_AUTHOR("Eswin");
MODULE_DESCRIPTION("DSP: Low Level Device Driver For Eswin DSP");
MODULE_LICENSE("Dual MIT/GPL");
