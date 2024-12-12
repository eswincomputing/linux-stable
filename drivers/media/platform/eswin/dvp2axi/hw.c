// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI hw driver
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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-fwnode.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include "../eswin_vi.h"
#include "dvp2axi.h"
#include "hw.h"

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation

static const struct of_device_id es_dvp2axi_plat_of_match[] = {
	{
		.compatible = "eswin,dvp2axi",
	},
	{},
};

static irqreturn_t es_dvp2axi_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	// unsigned int intstat_glb = 0;
	u64 irq_start, irq_stop;
	// int i;

	irq_start = ktime_get_ns();
	for(int i = 0; i < 6; i++) {
		if(irq == dvp2axi_hw->devm_irq_num[i]) {
			es_irq_oneframe(dev, dvp2axi_hw->dvp2axi_dev[i]);
		}
	}

	irq_stop = ktime_get_ns();
	dvp2axi_hw->irq_time = irq_stop - irq_start;
	return IRQ_HANDLED;
}

static irqreturn_t es_dvp2axi_err_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	u64 irq_start, irq_stop;

	irq_start = ktime_get_ns();
	for(int i = 0; i < ES_DVP2AXI_IRQ_NUM; i++) {
		if(irq == dvp2axi_hw->devm_irq_num[i]) {
			es_irq_err_handle(dev, dvp2axi_hw->dvp2axi_dev[i]);
		}
	}

	irq_stop = ktime_get_ns();
	dvp2axi_hw->irq_time = irq_stop - irq_start;
	return IRQ_HANDLED;
}

void es_dvp2axi_disable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw)
{
	int i;

	for (i = dvp2axi_hw->clk_size - 1; i >= 0; i--)
		clk_disable_unprepare(dvp2axi_hw->clks[i]);
}

int es_dvp2axi_enable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw)
{
	int i, ret = -EINVAL;

	for (i = 0; i < dvp2axi_hw->clk_size; i++) {
		ret = clk_prepare_enable(dvp2axi_hw->clks[i]);

		if (ret < 0)
			goto err;
	}

	// write_dvp2axi_reg_and(dvp2axi_hw->base_addr, DVP2AXI_CSI_INTEN, 0x0);
	return 0;

err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(dvp2axi_hw->clks[i]);

	return ret;
}

void es_dvp2axi_hw_soft_reset(struct es_dvp2axi_hw *dvp2axi_hw, bool is_rst_iommu)
{
	return ;
}

static int dvp2axi_smmu_sid_cfg(struct device *dev)
{
	int ret = 0;
	struct regmap *regmap = NULL;
	int mmu_tbu0_vi_dvp2axi_reg = 0;
	u32 rdwr_sid_ssid = 0;
	u32 sid = 0;

	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec == NULL) {
		dev_info(dev, "Device is not behind SMMU, using default streamID(0)\n");
		return 0;
	}

	if (fwspec->num_ids == 0) {
		dev_err(dev, "No Stream IDs configured!\n");
		return -EINVAL;
	}

	sid = fwspec->ids[0];

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "eswin,vi_top_csr");
	if (IS_ERR(regmap)) {
		pr_err("No vi_top_csr phandle specified, regmap=%ld\n",
		       PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
					 &mmu_tbu0_vi_dvp2axi_reg);
	if (ret) {
		pr_err("Failed to get sid cfg reg offset, ret=%d\n", ret);
		return ret;
	}

	rdwr_sid_ssid = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);

	regmap_write(regmap, mmu_tbu0_vi_dvp2axi_reg, rdwr_sid_ssid);

	ret = win2030_dynm_sid_enable(dev_to_node(dev));
	if (ret < 0)
		pr_err("Failed to enable dynamic SID for sid=%u, ret=%d\n", sid,
		       ret);

	return ret;
}

static int es_dvp2axi_plat_hw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct es_dvp2axi_hw *dvp2axi_hw;
	struct resource *res;
	int ret, irq;
	u32 reg_val;
#ifdef CONFIG_NUMA
	u32 numa_id = 0;
#endif

	dvp2axi_hw = devm_kzalloc(dev, sizeof(*dvp2axi_hw), GFP_KERNEL);
	if (!dvp2axi_hw)
		return -ENOMEM;

	dev_set_drvdata(dev, dvp2axi_hw);
	dvp2axi_hw->dev = dev;

	dvp2axi_hw->vi_topcsr_regmap = syscon_regmap_lookup_by_phandle(dvp2axi_hw->dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(dvp2axi_hw->vi_topcsr_regmap)) {
        pr_err("No vi_top_csr phandle specified, regmap=%ld\n", PTR_ERR(dvp2axi_hw->vi_topcsr_regmap));
		return PTR_ERR(dvp2axi_hw->vi_topcsr_regmap);
    }

	ret = of_property_read_u32_index(dvp2axi_hw->dev->of_node, "eswin,vi_top_csr", 2, &dvp2axi_hw->vi_topcsr_reg);
	if (ret) {
		pr_err("Failed to get dvp2axi vi top clk reg offset, ret=%d\n", ret);
		return ret;
	}

	regmap_read(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, &reg_val);
	reg_val |= (DVP2AXI_DVP_CLK_EN | CTRL_DVP_CLK_EN);
	regmap_write(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, reg_val);

	dvp2axi_hw->num_clks = devm_clk_bulk_get_all(dvp2axi_hw->dev, &dvp2axi_hw->clks_bulk);

	if (dvp2axi_hw->num_clks < 0)
		return dev_err_probe(dvp2axi_hw->dev, -ENODEV,
				     "Failed to get dvp2axi clocks\n");

	ret = clk_bulk_prepare_enable(dvp2axi_hw->num_clks, dvp2axi_hw->clks_bulk);
	if (ret)
		return dev_err_probe(dvp2axi_hw->dev, ret,
				     "Failed to enable dvp2axi clocks\n");

	dvp2axi_hw->rstc = devm_reset_control_array_get_shared(&pdev->dev);
	if (IS_ERR_OR_NULL(dvp2axi_hw->rstc)) {
		dev_err_probe(dev, PTR_ERR(dvp2axi_hw->rstc), "unable to get dvp2axi rst_cfg\n");
	}

	reset_control_deassert(dvp2axi_hw->rstc);

	ret = dvp2axi_smmu_sid_cfg(dev);
	if (ret) {
		dev_err(dev, "SMMU SID config failed: %d\n", ret);
		return ret;
	}

	win2030_tbu_power(dev, true);

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM,
		"dvp2axi_regs");
	dvp2axi_hw->base_addr = devm_ioremap_resource(dev, res);
	if (PTR_ERR(dvp2axi_hw->base_addr) == -EBUSY) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		dvp2axi_hw->base_addr = devm_ioremap(dev, offset, size);
		if (IS_ERR(dvp2axi_hw->base_addr)) {
			dev_err(dev, "ioremap failed\n");
			return PTR_ERR(dvp2axi_hw->base_addr);
		}
	}

	for(int i = 0; i < ES_DVP2AXI_IRQ_NUM; i++) {
		irq = platform_get_irq(pdev, i);
		dvp2axi_hw->devm_irq_num[i] = irq;
		if (irq < 0)
			return irq;
		if(i == ES_DVP2AXI_ERR_IRQ || i == ES_DVP2AXI_AFULL_IRQ) 
			ret = devm_request_irq(dev, irq, es_dvp2axi_err_irq_handler,
			       IRQF_SHARED,
			       dev_driver_string(dev), dev);
		else
			ret = devm_request_irq(dev, irq, es_dvp2axi_irq_handler,
			       IRQF_SHARED,
			       dev_driver_string(dev), dev);

		if (ret < 0) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	for(int i=0; i < 6; i++)
		dvp2axi_hw_irq_mask(dvp2axi_hw, i, 1);

	dvp2axi_hw_irq_axi(dvp2axi_hw, 1);


	for(int i = 0; i < ES_DVP2AXI_ERRIRQ_NUM; i++)
		atomic_set(&dvp2axi_hw->dvp2axi_errirq_cnts[i], 0);

	dvp2axi_hw->irq = irq;

	// dvp2axi_hw->is_dma_sg_ops = true;
	dvp2axi_hw->is_dma_sg_ops = false;
	dvp2axi_hw->is_dma_contig = true;
	dvp2axi_hw->mem_ops = &vb2_dma_contig_memops;
	mutex_init(&dvp2axi_hw->dev_lock);
	mutex_init(&dvp2axi_hw->dev_multi_chn_lock);

	atomic_set(&dvp2axi_hw->power_cnt, 0);

	tasklet_init(&dvp2axi_hw->dvp2axi_err_tasklet, es_dvp2axi_tasklet_err_handle,
		(unsigned long)dvp2axi_hw);

	tasklet_enable(&dvp2axi_hw->dvp2axi_err_tasklet);

#ifdef CONFIG_NUMA
	ret = of_property_read_u32(dev->of_node, "numa-node-id", &numa_id);
	if(ret) {
		dev_warn(dev, "Could not get numa-node-id, use default 0\n");
		numa_id = 0;
	}
#endif

#ifdef CONFIG_NUMA
	if(numa_id == 0) {
		platform_driver_register(&es_dvp2axi_plat_drv);
	} else {
		platform_driver_register(&es_dvp2axi_plat_drv_d1);
	}
#else
	platform_driver_register(&es_dvp2axi_plat_drv);
#endif

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	dev_info(dev, "probe success! \n");
	return 0;
}

static int es_dvp2axi_plat_remove(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	mutex_destroy(&dvp2axi_hw->dev_lock);
	mutex_destroy(&dvp2axi_hw->dev_multi_chn_lock);
	tasklet_disable(&dvp2axi_hw->dvp2axi_err_tasklet);
	tasklet_kill(&dvp2axi_hw->dvp2axi_err_tasklet);
	return 0;
}

static void es_dvp2axi_hw_shutdown(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);

	if (pm_runtime_get_if_in_use(&pdev->dev) <= 0)
		return;

	if (dvp2axi_hw->irq > 0)
		disable_irq(dvp2axi_hw->irq);

	pm_runtime_put(&pdev->dev);
}

static int __maybe_unused es_dvp2axi_runtime_suspend(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);

	win2030_tbu_power(dev, false);

	reset_control_assert(dvp2axi_hw->rstc);

	clk_bulk_disable_unprepare(dvp2axi_hw->num_clks, dvp2axi_hw->clks_bulk);

	return 0;
}

static int __maybe_unused es_dvp2axi_runtime_resume(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	struct eswin_vi_device* es_vi_dev;
	u32 reg_val = 0;
	int ret;

	ret = clk_bulk_prepare_enable(dvp2axi_hw->num_clks, dvp2axi_hw->clks_bulk);
	if (ret)
		return dev_err_probe(dvp2axi_hw->dev, ret,
				     "Failed to enable dvp2axi clocks\n");

	reset_control_deassert(dvp2axi_hw->rstc);

	win2030_tbu_power(dev, true);

	regmap_read(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, &reg_val);
	reg_val |= (DVP2AXI_DVP_CLK_EN | CTRL_DVP_CLK_EN);
	regmap_write(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, reg_val);

	es_vi_dev = dev_get_drvdata(parent);
	if (!es_vi_dev) {
		return -ENODEV;
	}

	vitop_intf_cfg(es_vi_dev);

	dvp2axi_smmu_sid_cfg(dev);

	for(int i=0; i < 6; i++)
		dvp2axi_hw_irq_mask(dvp2axi_hw, i, 1);

	return 0;
}

static int __maybe_unused es_dvp2axi_sleep_suspend(struct device *dev)
{
	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	es_dvp2axi_runtime_suspend(dev);

	return 0;
}

static int __maybe_unused es_dvp2axi_sleep_resume(struct device *dev)
{
	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	es_dvp2axi_runtime_resume(dev);

	return 0;
}

static const struct dev_pm_ops es_dvp2axi_plat_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(es_dvp2axi_sleep_suspend,
				es_dvp2axi_sleep_resume)
	SET_RUNTIME_PM_OPS(es_dvp2axi_runtime_suspend, es_dvp2axi_runtime_resume, NULL)
};

static struct platform_driver es_dvp2axi_hw_plat_drv = {
	.driver = {
		.name = ES_DVP2AXI_HW_DRIVER_NAME,
		.of_match_table = of_match_ptr(es_dvp2axi_plat_of_match),
		.pm = pm_sleep_ptr(&es_dvp2axi_plat_pm_ops),
	},
	.probe = es_dvp2axi_plat_hw_probe,
	.remove = es_dvp2axi_plat_remove,
	.shutdown = es_dvp2axi_hw_shutdown,
};

int es_dvp2axi_plat_drv_init(void)
{
	platform_driver_register(&es_dvp2axi_hw_plat_drv);
	return 0;
}

static void __exit es_dvp2axi_plat_drv_exit(void)
{
	platform_driver_unregister(&es_dvp2axi_hw_plat_drv);
}


late_initcall(es_dvp2axi_plat_drv_init);
module_exit(es_dvp2axi_plat_drv_exit);

MODULE_AUTHOR("ES VI team");
MODULE_DESCRIPTION("ES DVP2AXI platform driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);