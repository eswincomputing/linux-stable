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

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/printk.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>

#define 	SYSCON_NOC_CFG0_OFFSET			0x324
#define		NOC_CFG0_REG_BIT_WDT_MASK		GENMASK(16, 0)
#define 	SYSCON_NOC_CFG1_OFFSET			0x328
#define		NOC_CFG1_REG_BIT_WDT_MASK		GENMASK(20, 0)

char *noc_wdt_irq_src[] = {
	"cnoc_vo_timeout",
	"cnoc_vi_timeout",
	"cnoc_vc_timeout",
	"cnoc_tcu_timeout",
	"cnoc_pciet_x_timeout",
	"cnoc_pciet_p_timeout",
	"cnoc_npu_timeout",
	"cnoc_mcput_d2d_timeout",
	"cnoc_lsp_apb6_timeout",
	"cnoc_lsp_apb4_timeout",
	"cnoc_lsp_apb3_timeout",
	"cnoc_lsp_apb2_timeout",
	"cnoc_hsp_timeout",
	"cnoc_gpu_timeout",
	"cnoc_dspt_timeout",
	"cnoc_ddrt1_phy_timeout",
	"cnoc_ddrt1_ctrl_timeout",
	"cnoc_ddr0_phy_timeout",
	"cnoc_ddr0_ctrl_timeout",
	"cnoc_aon_timeout",
	"clmm_timeout",
	"rnoc_ddrt1_p4_timeout",
	"rnoc_ddrt0_p4_timeout",
	"mnoc_ddr1_p3_timeout",
	"mnoc_ddr0_p3_timeout",
	"snoc_pciet_timeout",
	"snoc_npu_timeout",
	"snoc_dspt_timeout",
	"snoc_ddrt1_p2_timeout",
	"snoc_ddrt1_p1_timeout",
	"snoc_ddrt0_p2_timeout",
	"snoc_ddrt0_p1_timeout",
	"snoc_aon_timeout",
	"lnoc_ddrt1_p0_timeout",
	"lnoc_ddrt0_p0_timeout",
};

#define 	NOC_WDT_IRQ_NUMBER			(ARRAY_SIZE(noc_wdt_irq_src))

static irqreturn_t noc_wdt_interrupt(int irq, void *dev_id)
{
	struct irq_data *data = NULL;
	char *irq_src = NULL;

	data = irq_get_irq_data(irq);
	if (NULL == data) {
		pr_err("noc-wdt: invalid irq data\n");
	}
	irq_src = (char *)dev_id;
	pr_warn_once("noc-wdt: interrupt %s occurred on irq %d, hw irq %ld!\n", irq_src,
		irq, data->hwirq);
	return IRQ_HANDLED;
}

static int eswin_noc_wdt_probe(struct platform_device *pdev)
{
	int i;
	int req_irq;
	struct device	*dev;
	int ret;
	struct device *parent;
	struct regmap *regmap;
	int noc_ctl_reg;
	int wd_ref_divsor;

	for (i = 0; i < NOC_WDT_IRQ_NUMBER; i++) {
		req_irq = platform_get_irq(pdev, i);
		if (req_irq < 0)
			return req_irq;

		ret = devm_request_irq(&pdev->dev, req_irq, &noc_wdt_interrupt,
				IRQF_SHARED |IRQF_ONESHOT ,
				noc_wdt_irq_src[i], (void *)noc_wdt_irq_src[i]);
		if (ret) {
			dev_err(&pdev->dev, "cannot register irq %d, ret %d\n", req_irq, ret);
			return ret;
		}
		dev_dbg(&pdev->dev,"registered irq %s, base %d, num %ld\n", noc_wdt_irq_src[i],
			platform_get_irq(pdev, 0), NOC_WDT_IRQ_NUMBER);
	}
	dev = &pdev->dev;
	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
	if (!IS_ERR(regmap)) {
		ret = of_property_read_u32_index(dev->of_node, "eswin,syscrg_csr", 1,
						&noc_ctl_reg);
		if (ret) {
			dev_err(dev, "can't get noc_ctl_reg offset from dts node(errno:%d)\n", ret);
			return ret;
		}
		ret = of_property_read_u32_index(dev->of_node, "eswin,syscrg_csr", 2,
						&wd_ref_divsor);
		if (ret) {
			dev_err(dev, "can't get wd_ref_divsor val from dts node(errno:%d)\n", ret);
			return ret;
		}
		ret = regmap_update_bits(regmap, noc_ctl_reg, GENMASK(19, 4),
			FIELD_PREP(GENMASK(19, 4), wd_ref_divsor));
		if (ret) {
			dev_err(dev, "can't set wd_ref_divsor val in sys_crg(errno:%d)\n", ret);
			return ret;
		}
	}
	parent = pdev->dev.parent;
	if (!parent) {
		dev_err(&pdev->dev, "no parent\n");
		return -ENODEV;
	}

	regmap = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return PTR_ERR(regmap);
	}
	ret = regmap_write_bits(regmap, SYSCON_NOC_CFG0_OFFSET,
		NOC_CFG0_REG_BIT_WDT_MASK, 0x1FFFF);
	if (0 != ret) {
		dev_err(&pdev->dev,"failed to enable timeout wdt\n");
	}
	ret = regmap_write_bits(regmap, SYSCON_NOC_CFG1_OFFSET,
		NOC_CFG1_REG_BIT_WDT_MASK, 0x1FFFFF);
	if (0 != ret) {
		dev_err(&pdev->dev,"failed to enable timeout wdt\n");
	}
	return ret;
}

static int eswin_noc_wdt_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id eswin_noc_wdt_dt_ids[] = {
	 { .compatible = "eswin,win2030-noc-wdt", },
	 { /* sentinel */ },
};

static struct platform_driver eswin_noc_wdt_driver = {
	.probe  = eswin_noc_wdt_probe,
	.remove = eswin_noc_wdt_remove,
	.driver = {
		.name		= "eswin-noc-wdt",
		.of_match_table	= eswin_noc_wdt_dt_ids,
	},
};

static int __init eswin_noc_wdt_init(void)
{
	return platform_driver_register(&eswin_noc_wdt_driver);
}

subsys_initcall(eswin_noc_wdt_init);
