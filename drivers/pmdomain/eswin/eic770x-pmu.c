// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PMU Driver
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
 * Authors: gengzonglin <gengzonglin@eswincomputing.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/pm_domain.h>
#include <dt-bindings/power/eswin,eic770x-pmu.h>

#define PD_CTRL                 0x0  // override
#define PD_SW_COLLAPSE          0x4  // sw collapse en
#define PD_SW_PWR               0x8  // power switch ack & power switch en 
#define PD_SW_RESET             0xC  // reg reset
#define PD_SW_ISO               0x10 // sw clamp io
#define PD_SW_CLK_DISABLE       0x14 // sw clk disable
#define PD_PWR_DLY              0x18 // sw clk disable
#define PD_CLK_DLY              0x1c // sw clk disable
#define PD_RST_DLY              0x20 // sw clk disable
#define PD_CLAMP_DLY            0x24 // sw clk disable
#define PD_DEBUG                0x28 // status

#define PD_STATUS_MASK          0x03 // STATE 3'b011 power on, 3'b000 power off

#define eic770x_PMU_TIMEOUT_US		500

struct pmu_device_power_delay {
	unsigned int dly_en_up;			/* wait time after power-on */
	unsigned int dly_en_down;		/* delay time of the req signal */
	unsigned int en_up_div_exp;		/*  */
	unsigned int en_down_div_exp;	/*  */
};

struct pmu_device_clock_delay {
	unsigned int disable_div_exp;
	unsigned int nodisable_div_exp;
	unsigned int dly_disable_clk;
	unsigned int dly_nodisble_clk;
};

struct pmu_device_reset_delay {
	unsigned int dly_deassert_ares;
	unsigned int dly_assert_ares;
	unsigned int deassert_div_exp;
	unsigned int assert_div_exp;
};

struct pmu_device_clamp_delay {
	unsigned int dly_unclamp_io;
	unsigned int dly_clamp_io;
	unsigned int unclamp_div_exp;
	unsigned int clamp_div_exp;
};

struct eic770x_domain_info
{
	const char *name;
	void __iomem *reg_base;
	struct device_node *of_node;
	unsigned int status;
	struct pmu_device_power_delay power_dly;
	struct pmu_device_clock_delay clk_dly;
	struct pmu_device_reset_delay reset_dly;
	struct pmu_device_clamp_delay clamp_dly;
};

struct eic770x_pmu {
	struct	device		*dev;
	void __iomem 		*base;
	struct eic770x_domain_info *domain_info;
	unsigned int		 num_domains;
	struct generic_pm_domain **genpd;
	struct genpd_onecell_data genpd_data;
};

struct eic770x_pmu_dev {
	struct eic770x_domain_info *domain_info;
	struct eic770x_pmu *pmu;
	struct generic_pm_domain genpd;
};

static int eic770x_pmu_get_domain_state(struct eic770x_pmu_dev *pmd, bool *is_on)
{
	*is_on = false;

	if(PD_STATUS_MASK & ioread32(pmd->domain_info->reg_base + PD_DEBUG)) { 
		*is_on = true;
	}

	return 0;
}

static int eic770x_pmu_set_domain_state(struct eic770x_pmu_dev *pmd, bool off)
{
	iowrite32(0x0, pmd->domain_info->reg_base + PD_CTRL); // power domain cntr use hardware
	iowrite32(off, pmd->domain_info->reg_base + PD_SW_COLLAPSE); // collapse 1:power off 0:power on
	return 0;
}

static int eic770x_pmu_domain_on(struct generic_pm_domain *genpd)
{
	struct eic770x_pmu_dev *pmd = container_of(genpd,
						  struct eic770x_pmu_dev, genpd);
	struct eic770x_pmu *pmu = pmd->pmu;
	struct device_node *node = pmd->domain_info->of_node;
	bool is_on;
	int ret;
	u32 val;

	eic770x_pmu_get_domain_state(pmd, &is_on);
	if (is_on == true) {
		dev_info(pmu->dev, "pm domain [%s] was already in power on state.\n",
			pmd->genpd.name);
		return 0;
	}

	dev_info(pmu->dev, "The %s enters power-on process.\n", pmd->genpd.name);

	eic770x_pmu_set_domain_state(pmd, false); //true: power off.
	ret = readl_poll_timeout_atomic(pmd->domain_info->reg_base + PD_DEBUG,
					val, (val & PD_STATUS_MASK),
					1, eic770x_PMU_TIMEOUT_US);
	if (ret) {
		dev_err(pmu->dev, "%s: failed to power on\n",
			pmd->genpd.name);
		return -ETIMEDOUT;
	}

	if (!of_property_read_u32(node, "tbus", &val)) {
		win2030_tbu_power_by_dev_and_node(pmu->dev, node, true); // tbu power on
		dev_info(pmu->dev, "%s power on tbu.\n", pmd->genpd.name);
	}

	dev_info(pmu->dev, "The %s ends power-on process.\n",  pmd->genpd.name);
	return 0;
}

static int eic770x_pmu_domain_off(struct generic_pm_domain *genpd)
{
	struct eic770x_pmu_dev *pmd = container_of(genpd,
						  struct eic770x_pmu_dev, genpd);
	struct eic770x_pmu *pmu = pmd->pmu;
	struct device_node *node = pmd->domain_info->of_node;
	bool is_on = false;
	int ret;
	u32 val;

	eic770x_pmu_get_domain_state(pmd, &is_on);
	if (is_on == false) {
		dev_info(pmu->dev, "pm domain [%s] was already in  power off state.\n",
			pmd->genpd.name);
		return 0;
	}

	dev_info(pmu->dev, "The %s enters power off process.\n", pmd->genpd.name);

	if (!of_property_read_u32(node, "tbus", &val)) {
		win2030_tbu_power_by_dev_and_node(pmu->dev, node, false); // tbu power off
		dev_info(pmu->dev, "%s power off tbu.\n", pmd->genpd.name);
	}

	eic770x_pmu_set_domain_state(pmd, true); //true: power off.
	ret = readl_poll_timeout_atomic(pmd->domain_info->reg_base + PD_DEBUG,
				val, !(val & PD_STATUS_MASK),
				1, eic770x_PMU_TIMEOUT_US);
	if (ret) {
		dev_err(pmu->dev, "%s: failed to power off\n",
			pmd->genpd.name);
		return -ETIMEDOUT;
	}
	dev_info(pmu->dev, "The %s ends power off process.\n",  pmd->genpd.name);
	return 0;
}

static int eic770x_pmu_init_domain(struct eic770x_pmu *pmu, int index)
{
	struct eic770x_pmu_dev *pmd;
	int ret;
	bool is_on = false;

	pmd = devm_kzalloc(pmu->dev, sizeof(*pmd), GFP_KERNEL);
	if (!pmd)
		return -ENOMEM;

	pmd->domain_info = &pmu->domain_info[index];
	pmd->pmu = pmu;

	pmd->genpd.name = pmd->domain_info->name;

	if(pmd->domain_info->status == 2) {
		pmd->genpd.flags = GENPD_FLAG_ALWAYS_ON;
	}

	if(pmd->domain_info->status == 0) {
		eic770x_pmu_domain_off(&pmd->genpd);
	}
	else {
		eic770x_pmu_domain_on(&pmd->genpd);
	}

	ret = eic770x_pmu_get_domain_state(pmd, &is_on);
	if (ret)
		dev_warn(pmu->dev, "unable to get current state for %s\n",
			 pmd->genpd.name);

	pmd->genpd.power_on = eic770x_pmu_domain_on;
	pmd->genpd.power_off = eic770x_pmu_domain_off;
	pm_genpd_init(&pmd->genpd, NULL, !is_on);

	pmu->genpd_data.domains[index] = &pmd->genpd;

	return 0;
}


static int eic770x_pmu_add_domain(struct device *dev, struct eic770x_pmu *pmu)
{
	struct eic770x_domain_info *pd_info;
	struct device_node *node;
	int id, nval, num_domains;
	int ret;
	unsigned int val[32];

	num_domains = device_get_child_node_count(dev);
	if (num_domains == 0)
		return -ENODEV;

	pmu->domain_info = devm_kcalloc(dev,  num_domains,
				  sizeof(*pd_info),
				  GFP_KERNEL);
	if (!pmu->domain_info)
		return -ENOMEM;

	num_domains = 0;
	for_each_child_of_node(dev->of_node, node)
	{
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			dev_err(dev, "Failed to parse pmu id.\n");
			continue;
		}
		if(id > EIC770X_PD_DSP3) {
			dev_err(dev, "pmu id %d out of range.\n", id);
			continue;
		}

		pd_info = &pmu->domain_info[id];
		pd_info->of_node = node;
		unsigned int  val_u32;
		of_property_read_u32(node, "reg_base", &val_u32);
		pd_info->reg_base = pmu->base + val_u32;

		of_property_read_string(node, "label", &pd_info->name);
		of_property_read_u32(node, "power_status", &pd_info->status);

		nval = of_property_read_u32_array(node, "power_delay", &val[0], 4);
		if(!nval ){
			pd_info->power_dly.dly_en_up = val[0];
			pd_info->power_dly.dly_en_down = val[1];
			pd_info->power_dly.en_up_div_exp = val[2];
			pd_info->power_dly.en_down_div_exp = val[3];
			val_u32 = (pd_info->power_dly.dly_en_up & 0xff) |
					((pd_info->power_dly.dly_en_down & 0xff) << 8) |
					((pd_info->power_dly.en_up_div_exp & 0xf) << 16) |
					((pd_info->power_dly.en_down_div_exp & 0xf) << 20);
			iowrite32( val_u32, pd_info->reg_base + PD_PWR_DLY);
		}

		nval = of_property_read_u32_array(node, "clock_delay", &val[0], 4);
		if(!nval){
			pd_info->clk_dly.dly_nodisble_clk = val[0];
			pd_info->clk_dly.dly_disable_clk = val[1];
			pd_info->clk_dly.nodisable_div_exp = val[2];
			pd_info->clk_dly.disable_div_exp = val[3];
			val_u32 = (pd_info->clk_dly.dly_nodisble_clk & 0xff) |
					((pd_info->clk_dly.dly_disable_clk & 0xff) << 8) |
					((pd_info->clk_dly.nodisable_div_exp & 0xf) << 16) |
					((pd_info->clk_dly.disable_div_exp & 0xf) << 20);
			iowrite32( val_u32, pd_info->reg_base + PD_CLK_DLY);
		}

		nval = of_property_read_u32_array(node, "reset_delay", &val[0], 4);
		if(!nval){
			pd_info->reset_dly.dly_assert_ares = val[0];
			pd_info->reset_dly.dly_deassert_ares = val[1];
			pd_info->reset_dly.assert_div_exp = val[2];
			pd_info->reset_dly.deassert_div_exp = val[3];
			val_u32 = (pd_info->reset_dly.dly_assert_ares & 0xff) |
					((pd_info->reset_dly.dly_deassert_ares & 0xff) << 8) |
					((pd_info->reset_dly.assert_div_exp & 0xf) << 16) |
					((pd_info->reset_dly.deassert_div_exp & 0xf) << 20);
			iowrite32( val_u32, pd_info->reg_base + PD_RST_DLY);
		}

		nval = of_property_read_u32_array(node, "clamp_delay", &val[0], 4);
		if(!nval){
			pd_info->clamp_dly.dly_unclamp_io = val[0];
			pd_info->clamp_dly.dly_clamp_io = val[1];
			pd_info->clamp_dly.unclamp_div_exp = val[2];
			pd_info->clamp_dly.clamp_div_exp = val[3];
			val_u32 = (pd_info->clamp_dly.dly_unclamp_io & 0xff) |
					((pd_info->clamp_dly.dly_clamp_io & 0xff) << 8) |
					((pd_info->clamp_dly.unclamp_div_exp & 0xf) << 16) |
					((pd_info->clamp_dly.clamp_div_exp & 0xf) << 20);
			iowrite32( val_u32, pd_info->reg_base + PD_CLAMP_DLY);
		}

		num_domains++;
	}
	pmu->num_domains = num_domains;
	return 0;
}

static int eic770x_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct eic770x_pmu *pmu;
	unsigned int i;
	int ret;

	dev_info(dev, "start registe power domains\n");

	pmu = devm_kzalloc(dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu->base))
		return PTR_ERR(pmu->base);

	eic770x_pmu_add_domain(dev, pmu);

	pmu->genpd = devm_kcalloc(dev, pmu->num_domains,
				  sizeof(struct generic_pm_domain *),
				  GFP_KERNEL);
	if (!pmu->genpd)
		return -ENOMEM;

	pmu->dev = dev;
	pmu->genpd_data.domains = pmu->genpd;
	pmu->genpd_data.num_domains = pmu->num_domains;

	for (i = 0; i < pmu->num_domains; i++) {
		ret = eic770x_pmu_init_domain(pmu, i);
		if (ret) {
			dev_err(dev, "failed to initialize power domain\n");
			return ret;
		}
	}

	ret = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (ret) {
		dev_err(dev, "failed to register genpd driver: %d\n", ret);
		return ret;
	}

	dev_info(dev, "registered %u power domains\n", i);

	return 0;
}


static const struct of_device_id eic770x_pmu_of_match[] = {
	{
		.compatible = "eswin,win2030-pmu-controller",
	}, {
		/* sentinel */
	}
};

static struct platform_driver eic770x_pmu_driver = {
	.probe = eic770x_pmu_probe,
	.driver = {
		.name = "eswin-pmu",
		.of_match_table = eic770x_pmu_of_match,
	},
};
builtin_platform_driver(eic770x_pmu_driver);

MODULE_AUTHOR("Geng Zonglin <gengzonglin@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN eic770x PMU Driver");
MODULE_LICENSE("GPL v2");
