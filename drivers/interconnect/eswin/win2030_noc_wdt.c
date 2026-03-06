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
#include <linux/delay.h>

#define 	SYSCON_NOC_CFG0_OFFSET			0x324
#define		NOC_CFG0_REG_BIT_WDT_MASK		GENMASK(16, 0)
#define 	SYSCON_NOC_CFG1_OFFSET			0x328
#define		NOC_CFG1_REG_BIT_WDT_MASK		GENMASK(20, 0)

/* NOC Reset Register Offsets (from provided header) */
#define SNOC_RST_CTRL_OFFSET                    0x400
#define MNOC_RST_CTRL_OFFSET                    0x4D4
#define RNOC_RST_CTRL_OFFSET                    0x4D8
#define CNOC_RST_CTRL_OFFSET                     0x4DC
#define LNOCC_RST_CTRL_OFFSET                    0x4E0

/* SNOC Reset Control Register Bits */
#define SW_SNOCC_AON_ARSTN_BIT                  BIT(16)
#define SW_SNOCC_D2D_ARSTN_BIT                   BIT(15)
#define SW_SNOCC_DDRC0_P1_ARSTN_BIT              BIT(14)
#define SW_SNOCC_DDRC0_P2_ARSTN_BIT              BIT(13)
#define SW_SNOCC_DDRC1_P1_ARSTN_BIT              BIT(12)
#define SW_SNOCC_DDRC1_P2_ARSTN_BIT              BIT(11)
#define SW_SNOCC_DSPT_ARSTN_BIT                   BIT(10)
#define SW_SNOCC_JTAG_PRSTN_BIT                   BIT(9)
#define SW_SNOCC_NPU_ARSTN_BIT                    BIT(8)
#define SW_SNOCC_PCIET_PRSTN_BIT                   BIT(7)
#define SW_SNOCC_PCIET_XMRSTN_BIT                  BIT(6)
#define SW_SNOCC_PCIET_XRSTN_BIT                   BIT(5)
#define SW_SNOCC_U84_ARSTN_BIT                     BIT(4)
#define SW_SNOCC_TCU_ARSTN_BIT                     BIT(3)
#define SW_RNOCC_NSP_RSTN_BIT                      BIT(2)
#define SW_NOCC_CFG_RSTN_BIT                       BIT(1)
#define SW_NOCC_NSP_RSTN_BIT                       BIT(0)

/* MNOC Reset Control Register Bits */
#define SW_MNOCC_DDRC0_P3_ARSTN_BIT              BIT(6)
#define SW_MNOCC_DDRC1_P3_ARSTN_BIT              BIT(5)
#define SW_MNOCC_GPU_ARSTN_BIT                    BIT(4)
#define SW_MNOCC_HSP_ARSTN_BIT                    BIT(3)
#define SW_MNOCC_CFG_RSTN_BIT                     BIT(2)
#define SW_MNOCC_VC_ARSTN_BIT                     BIT(1)
#define SW_MNOCC_SNOCC_NSP_RSTN_BIT               BIT(0)

/* RNOC Reset Control Register Bits */
#define SW_RNOCC_DDRC0_P4_ARSTN_BIT              BIT(5)
#define SW_RNOCC_DDRC1_P4_ARSTN_BIT              BIT(4)
#define SW_RNOCC_CFG_RSTN_BIT                     BIT(3)
#define SW_RNOCC_SNOCC_NSP_RSTN_BIT               BIT(2)
#define SW_RNOCC_VI_ARSTN_BIT                     BIT(1)
#define SW_RNOCC_VO_ARSTN_BIT                     BIT(0)

/* CNOC Reset Control Register Bits */
#define SW_CNOCC_AON_CFG_RSTN_BIT                BIT(15)
#define SW_CNOCC_CLMM_CFG_RSTN_BIT                BIT(14)
#define SW_CNOCC_CFG_RSTN_BIT                     BIT(13)
#define SW_CNOCC_D2D_CFG_RSTN_BIT                 BIT(12)
#define SW_CNOCC_DDRT0_CFG_RSTN_BIT               BIT(11)
#define SW_CNOCC_DDRT1_CFG_RSTN_BIT               BIT(10)
#define SW_CNOCC_DSPT_CFG_RSTN_BIT                BIT(9)
#define SW_CNOCC_GPU_CFG_RSTN_BIT                 BIT(8)
#define SW_CNOCC_HSP_CFG_RSTN_BIT                 BIT(7)
#define SW_CNOCC_LSP_CFG_RSTN_BIT                 BIT(6)
#define SW_CNOCC_NPU_CFG_RSTN_BIT                  BIT(5)
#define SW_CNOCC_PCIET_CFG_RSTN_BIT                BIT(4)
#define SW_CNOCC_TCU_CFG_RSTN_BIT                  BIT(3)
#define SW_CNOCC_VC_CFG_RSTN_BIT                   BIT(2)
#define SW_CNOCC_VI_CFG_RSTN_BIT                   BIT(1)
#define SW_CNOCC_VO_CFG_RSTN_BIT                   BIT(0)

/* LNOCC Reset Control Register Bits */
#define SW_LNOCC_DDRC0_P0_ARSTN_BIT              BIT(3)
#define SW_LNOCC_DDRC1_P0_ARSTN_BIT              BIT(2)
#define SW_LNOCC_NPU_LLCC_ARSTN_BIT               BIT(1)
#define SW_LNOCC_CFG_RSTN_BIT                     BIT(0)

/* Original timeout IRQ source names */
static const char * const noc_wdt_irq_src[] = {
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

#define NOC_WDT_IRQ_NUMBER	ARRAY_SIZE(noc_wdt_irq_src)

/* Mapping from IRQ source name to reset register offset and bit mask */
struct noc_wdt_reset_map {
	const char *name;
	unsigned int offset;
	unsigned int mask;
};

static const struct noc_wdt_reset_map reset_map[] = {
	{ "cnoc_vo_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_VO_CFG_RSTN_BIT },
	{ "cnoc_vi_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_VI_CFG_RSTN_BIT },
	{ "cnoc_vc_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_VC_CFG_RSTN_BIT },
	{ "cnoc_tcu_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_TCU_CFG_RSTN_BIT },
	{ "cnoc_pciet_x_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_PCIET_CFG_RSTN_BIT },
	{ "cnoc_pciet_p_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_PCIET_CFG_RSTN_BIT },
	{ "cnoc_npu_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_NPU_CFG_RSTN_BIT },
	{ "cnoc_mcput_d2d_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_D2D_CFG_RSTN_BIT },
	{ "cnoc_lsp_apb6_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_LSP_CFG_RSTN_BIT },
	{ "cnoc_lsp_apb4_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_LSP_CFG_RSTN_BIT },
	{ "cnoc_lsp_apb3_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_LSP_CFG_RSTN_BIT },
	{ "cnoc_lsp_apb2_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_LSP_CFG_RSTN_BIT },
	{ "cnoc_hsp_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_HSP_CFG_RSTN_BIT },
	{ "cnoc_gpu_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_GPU_CFG_RSTN_BIT },
	{ "cnoc_dspt_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_DSPT_CFG_RSTN_BIT },
	{ "cnoc_ddrt1_phy_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_DDRT1_CFG_RSTN_BIT },
	{ "cnoc_ddrt1_ctrl_timeout",	CNOC_RST_CTRL_OFFSET,	SW_CNOCC_DDRT1_CFG_RSTN_BIT },
	{ "cnoc_ddr0_phy_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_DDRT0_CFG_RSTN_BIT },
	{ "cnoc_ddr0_ctrl_timeout",		CNOC_RST_CTRL_OFFSET, SW_CNOCC_DDRT0_CFG_RSTN_BIT },
	{ "cnoc_aon_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_AON_CFG_RSTN_BIT },
	{ "clmm_timeout",		CNOC_RST_CTRL_OFFSET,	SW_CNOCC_CLMM_CFG_RSTN_BIT },
	{ "rnoc_ddrt1_p4_timeout",	RNOC_RST_CTRL_OFFSET,	SW_RNOCC_DDRC1_P4_ARSTN_BIT },
	{ "rnoc_ddrt0_p4_timeout",	RNOC_RST_CTRL_OFFSET,	SW_RNOCC_DDRC0_P4_ARSTN_BIT },
	{ "mnoc_ddr1_p3_timeout",	MNOC_RST_CTRL_OFFSET,	SW_MNOCC_DDRC1_P3_ARSTN_BIT },
	{ "mnoc_ddr0_p3_timeout",	MNOC_RST_CTRL_OFFSET,	SW_MNOCC_DDRC0_P3_ARSTN_BIT },
	{ "snoc_pciet_timeout",		SNOC_RST_CTRL_OFFSET,	SW_SNOCC_PCIET_XRSTN_BIT },
	{ "snoc_npu_timeout",		SNOC_RST_CTRL_OFFSET,	SW_SNOCC_NPU_ARSTN_BIT },
	{ "snoc_dspt_timeout",		SNOC_RST_CTRL_OFFSET,	SW_SNOCC_DSPT_ARSTN_BIT },
	{ "snoc_ddrt1_p2_timeout",	SNOC_RST_CTRL_OFFSET,	SW_SNOCC_DDRC1_P2_ARSTN_BIT },
	{ "snoc_ddrt1_p1_timeout",	SNOC_RST_CTRL_OFFSET,	SW_SNOCC_DDRC1_P1_ARSTN_BIT },
	{ "snoc_ddrt0_p2_timeout",	SNOC_RST_CTRL_OFFSET,	SW_SNOCC_DDRC0_P2_ARSTN_BIT },
	{ "snoc_ddrt0_p1_timeout",	SNOC_RST_CTRL_OFFSET,	SW_SNOCC_DDRC0_P1_ARSTN_BIT },
	{ "snoc_aon_timeout",		SNOC_RST_CTRL_OFFSET,	SW_SNOCC_AON_ARSTN_BIT },
	{ "lnoc_ddrt1_p0_timeout",	LNOCC_RST_CTRL_OFFSET,	SW_LNOCC_DDRC1_P0_ARSTN_BIT },
	{ "lnoc_ddrt0_p0_timeout",	LNOCC_RST_CTRL_OFFSET,	SW_LNOCC_DDRC0_P0_ARSTN_BIT },
};

/* Private data for the driver */
struct eswin_noc_wdt_priv {
	struct regmap *crg_regmap;	/* regmap for d0_sys_crg (reset control) */
};

/* Data passed to interrupt handler */
struct noc_wdt_irq_data {
	struct device *dev;
	const char *name;
};

static irqreturn_t noc_wdt_interrupt(int irq, void *dev_id)
{
	struct noc_wdt_irq_data *irq_data = dev_id;
	struct device *dev = irq_data->dev;
	struct eswin_noc_wdt_priv *priv = dev_get_drvdata(dev);
	const char *irq_src = irq_data->name;
	const struct noc_wdt_reset_map *map;
	int i;
	struct irq_data *data = NULL;

	data = irq_get_irq_data(irq);
	if (NULL == data) {
		pr_err("noc-wdt: invalid irq data\n");
	}

	/* Find the reset mapping for this IRQ source */
	for (i = 0; i < ARRAY_SIZE(reset_map); i++) {
		if (strcmp(reset_map[i].name, irq_src) == 0) {
			map = &reset_map[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(reset_map)) {
		dev_warn(dev, "timeout on %s, irq %d, hw irq %ld!\n", irq_src,
			irq, data->hwirq);
		return IRQ_HANDLED;
	}

	dev_warn(dev, "timeout on %s, irq %d, hw irq %ld, resetting port (offset 0x%x mask 0x%x)!\n",
		      irq_src, irq, data->hwirq, map->offset, map->mask);

	/* Perform a reset pulse: assert reset (write 0) then deassert (write 1) */
	regmap_update_bits(priv->crg_regmap, map->offset, map->mask, 0);
	udelay(10);	/* Short delay to ensure reset takes effect */
	regmap_update_bits(priv->crg_regmap, map->offset, map->mask, map->mask);

	return IRQ_HANDLED;
}

static int eswin_noc_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent;
	struct regmap *regmap;
	struct eswin_noc_wdt_priv *priv;
	int noc_ctl_reg, wd_ref_divsor;
	int i, ret;

	/* Allocate private data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	/* Get CRG regmap (d0_sys_crg) from phandle for reset control */
	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get syscrg regmap\n");
		return PTR_ERR(regmap);
	}
	priv->crg_regmap = regmap;

	/* Read and configure timeout parameters from the same phandle */
	ret = of_property_read_u32_index(dev->of_node, "eswin,syscrg_csr", 1, &noc_ctl_reg);
	if (ret) {
		dev_err(dev, "can't get noc_ctl_reg offset from dts (err %d)\n", ret);
		return ret;
	}
	ret = of_property_read_u32_index(dev->of_node, "eswin,syscrg_csr", 2, &wd_ref_divsor);
	if (ret) {
		dev_err(dev, "can't get wd_ref_divsor value from dts (err %d)\n", ret);
		return ret;
	}
	ret = regmap_update_bits(regmap, noc_ctl_reg, GENMASK(19, 4),
				 FIELD_PREP(GENMASK(19, 4), wd_ref_divsor));
	if (ret) {
		dev_err(dev, "failed to set wd_ref_divsor in sys_crg (err %d)\n", ret);
		return ret;
	}

	/* Request all NOC watchdog interrupts */
	for (i = 0; i < NOC_WDT_IRQ_NUMBER; i++) {
		int irq = platform_get_irq(pdev, i);
		struct noc_wdt_irq_data *irq_data;

		if (irq < 0)
			return irq;

		/* Allocate IRQ-specific data */
		irq_data = devm_kzalloc(dev, sizeof(*irq_data), GFP_KERNEL);
		if (!irq_data)
			return -ENOMEM;

		irq_data->dev = dev;
		irq_data->name = noc_wdt_irq_src[i];

		ret = devm_request_irq(dev, irq, noc_wdt_interrupt,
				       IRQF_SHARED | IRQF_ONESHOT,
				       noc_wdt_irq_src[i], irq_data);
		if (ret) {
			dev_err(dev, "cannot register irq %d (%s), ret %d\n",
				irq, noc_wdt_irq_src[i], ret);
			return ret;
		}
		dev_dbg(dev, "registered irq %s, hwirq %d\n",
			noc_wdt_irq_src[i], irq);
	}

	/* Get parent syscon regmap (d0_sys_con) for global timeout enable */
	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent device\n");
		return -ENODEV;
	}
	regmap = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get parent regmap\n");
		return PTR_ERR(regmap);
	}

	/* Enable global NOC timeouts in CFG0/CFG1 registers */
	ret = regmap_write_bits(regmap, SYSCON_NOC_CFG0_OFFSET,
				NOC_CFG0_REG_BIT_WDT_MASK, 0x1FFFF);
	if (ret)
		dev_err(dev, "failed to enable timeout wdt in CFG0\n");

	ret = regmap_write_bits(regmap, SYSCON_NOC_CFG1_OFFSET,
				NOC_CFG1_REG_BIT_WDT_MASK, 0x1FFFFF);
	if (ret)
		dev_err(dev, "failed to enable timeout wdt in CFG1\n");

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

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ESWIN NOC Watchdog Driver");
MODULE_AUTHOR("HuangYiFeng <huangyifeng@eswincomputing.com>");
