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
#if defined(CONFIG_PM_DEVFREQ)
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#endif

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation


#define DVP2AXI_CLK_GET_HANDLE(dev, clk_handle, clk_name)                     \
	{                                                                   \
		clk_handle = devm_clk_get(dev, clk_name);                   \
		if (IS_ERR(clk_handle)) {                                   \
			ret = PTR_ERR(clk_handle);                          \
			dev_err(dev, "failed to get dvp2axi %s: %d\n", clk_name, \
				ret);                                       \
			return ret;                                         \
		}                                                           \
	}

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
	unsigned long flags;

	spin_lock_irqsave(&dvp2axi_hw->stream_lock, flags);
	for(int i = 0; i < 6; i++) {
		if(irq == dvp2axi_hw->devm_irq_num[i]) {
			es_irq_oneframe(dev, dvp2axi_hw->dvp2axi_dev[i]);
		}
	}
	spin_unlock_irqrestore(&dvp2axi_hw->stream_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t es_dvp2axi_err_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&dvp2axi_hw->stream_lock, flags);
	es_irq_err_handle(dev);
	spin_unlock_irqrestore(&dvp2axi_hw->stream_lock, flags);

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

#if defined(CONFIG_PM_DEVFREQ)
/* devfreq target function to set frequency */
static int dvp_devfreq_target(struct device *dev, unsigned long *freq,
				 u32 flags)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	unsigned long spll0_rate = clk_get_rate(dvp2axi_hw->spll0_fout1);
	unsigned long vpll_rate = clk_get_rate(dvp2axi_hw->vpll_fout1);
	unsigned long target = *freq;
	int ret = 0;

	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	if (!dvp2axi_hw) {
		dev_err(dev, "dvp2axi_hw is NULL\n");
		return -EINVAL;
	}

	if (!dvp2axi_hw->dvp_mux) {
		dev_err(dev, "dvp_mux clock is NULL\n");
		return -EINVAL;
	}

	if (!dvp2axi_hw->spll0_fout1 || !dvp2axi_hw->vpll_fout1) {
		dev_err(dev, "Parent clocks are NULL\n");
		return -EINVAL;
	}

	if (spll0_rate % target < vpll_rate % target) {
		ret = clk_set_parent(dvp2axi_hw->dvp_mux, dvp2axi_hw->spll0_fout1);
	} else {
		ret = clk_set_parent(dvp2axi_hw->dvp_mux, dvp2axi_hw->vpll_fout1);
	}

	if (ret) {
		dev_err(dev, "Failed to set clock parent\n");
		return ret;
	}

	ret = clk_set_rate(dvp2axi_hw->dvp_clk, target);
	if (ret) {
		dev_warn(dev, "dvp_clk set rate failed");
		return ret;
	}

	return 0;
}

static int dvp_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	unsigned long rate;

	rate = clk_get_rate(dvp2axi_hw->dvp_clk);
	if (rate <= 0) {
		dev_warn(dev, "failed to get dvp_clk rate");
		return rate;
	}
	*freq = rate;

	return 0;
}

/* devfreq profile */
static struct devfreq_dev_profile dvp_devfreq_profile = {
	.initial_freq = 800000000,
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 1000, /* Poll every 1000ms to monitor load */
	.target = dvp_devfreq_target,
	.get_cur_freq = dvp_devfreq_get_cur_freq,
};
#endif

static int es_dvp2axi_sys_clk_init(struct platform_device *pdev,
			      struct es_dvp2axi_hw *dvp2axi_hw)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->dvp_clk, "dvp");
	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->dvp_mux, "dvp_mux");
	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->phy_cfg, "phy_cfg");
	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->phy_txclkesc, "phy_txclkesc");
	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->spll0_fout1, "spll0_fout1");
	DVP2AXI_CLK_GET_HANDLE(dev, dvp2axi_hw->vpll_fout1, "vpll_fout1");

	return ret;
}

static int es_dvp2axi_sys_clk_enable(struct es_dvp2axi_hw *dvp2axi_hw)
{
	clk_prepare_enable(dvp2axi_hw->dvp_clk);
	clk_prepare_enable(dvp2axi_hw->phy_cfg);
	clk_prepare_enable(dvp2axi_hw->phy_txclkesc);
	return 0;
}

int es_dvp2axi_ots = 0xff;
int es_dvp2axi_wqos = 0;
int es_dvp2axi_axi_burst_len = 1;

static ssize_t es_dvp2axi_show_outstanding(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", es_dvp2axi_ots);
	return ret;
}

static ssize_t es_dvp2axi_store_outstanding(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret) {
		if (val >= 1 && val <= 255)
			es_dvp2axi_ots = val;
		else
			dev_warn(dev, "invalid outstanding value, range (1-255)\n");
	} else {
		dev_err(dev, "set outstanding failed, ret %d\n", ret);
	}
	return len;
}

static ssize_t es_dvp2axi_show_wqos(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", es_dvp2axi_wqos);
	return ret;
}

static ssize_t es_dvp2axi_store_wqos(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret) {
		if (val >= 0 && val <= 15)
			es_dvp2axi_wqos = val;
		else
			dev_warn(dev, "invalid wqos value, range (0-15)\n");
	} else {
		dev_err(dev, "set wqos failed, ret %d\n", ret);
	}
	return len;
}

static ssize_t es_dvp2axi_show_burstlen(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", es_dvp2axi_axi_burst_len);
	return ret;
}

static ssize_t es_dvp2axi_store_burstlen(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret) {
		if (val >= 0 && val <= 1)
			es_dvp2axi_axi_burst_len = val;
		else
			dev_warn(dev, "invalid burstlen value, range (0-1)\n");
	} else {
		dev_err(dev, "set burstlen failed, ret %d\n", ret);
	}
	return len;
}

static DEVICE_ATTR(outstanding, S_IWUSR | S_IRUSR, es_dvp2axi_show_outstanding,
		   es_dvp2axi_store_outstanding);
static DEVICE_ATTR(wqos, S_IWUSR | S_IRUSR, es_dvp2axi_show_wqos,
		   es_dvp2axi_store_wqos);
static DEVICE_ATTR(burstlen, S_IWUSR | S_IRUSR, es_dvp2axi_show_burstlen,
		   es_dvp2axi_store_burstlen);

static struct attribute *dev_attrs[] = {
	&dev_attr_outstanding.attr,
	&dev_attr_wqos.attr,
	&dev_attr_burstlen.attr,
	NULL,
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

static int es_dvp2axi_sys_clk_disable(struct es_dvp2axi_hw *dvp2axi_hw)
{
	clk_disable_unprepare(dvp2axi_hw->dvp_clk);
	clk_disable_unprepare(dvp2axi_hw->phy_cfg);
	clk_disable_unprepare(dvp2axi_hw->phy_txclkesc);
	return 0;
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
#if defined(CONFIG_PM_DEVFREQ)
	struct devfreq *df;
#endif

	dvp2axi_hw = devm_kzalloc(dev, sizeof(*dvp2axi_hw), GFP_KERNEL);
	if (!dvp2axi_hw)
		return -ENOMEM;

	dev_set_drvdata(dev, dvp2axi_hw);
	dvp2axi_hw->dev = dev;

	if (sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp))
		return -ENODEV;

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

	#ifndef CONFIG_ARCH_SUSPEND_POSSIBLE
	regmap_read(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, &reg_val);
	reg_val |= (DVP2AXI_DVP_CLK_EN | CTRL_DVP_CLK_EN);
	regmap_write(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, reg_val);
	#endif

	es_dvp2axi_sys_clk_init(pdev, dvp2axi_hw);
	es_dvp2axi_sys_clk_enable(dvp2axi_hw);

	dvp2axi_hw->rstc = devm_reset_control_array_get_shared(&pdev->dev);
	if (IS_ERR_OR_NULL(dvp2axi_hw->rstc)) {
		dev_err_probe(dev, PTR_ERR(dvp2axi_hw->rstc), "unable to get dvp2axi rst_cfg\n");
	}

	reset_control_deassert(dvp2axi_hw->rstc);

	#if defined(CONFIG_PM_DEVFREQ)

	/* Add OPP table from device tree */
	ret = dev_pm_opp_of_add_table(&pdev->dev);
	if (ret) {
		pr_err("%s, %d, failed to add OPP table\n", __func__, __LINE__);
		return ret;
	}

	df = devm_devfreq_add_device(&pdev->dev, &dvp_devfreq_profile,
				     "userspace", NULL);
	if (IS_ERR(df)) {
		pr_err("%s, %d, add devfreq failed\n", __func__, __LINE__);
		return ret;
	}
#endif

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

	if (!of_property_read_bool(dev->of_node, "vb2-mem-ops")) {
		dvp2axi_hw->mem_pool = dvp2axi_mem_pool_create(&pdev->dev,
									"dvp2axi-pool",
									DVP2AXI_DEFAULT_BLOCK_SIZE, DVP2AXI_DEFAULT_BLOCK_NUM);
		if (IS_ERR(dvp2axi_hw->mem_pool)) {
			ret = PTR_ERR(dvp2axi_hw->mem_pool);
			dev_err(&pdev->dev, "Failed to create memory pool: %d\n", ret);
			return ret;
		}
		dvp2axi_hw->is_use_dvp2axi_mem_ops = true;
		dev_info(&pdev->dev, "DVP2AXI use dvp2axi_vb2_mem_ops\n");
		dvp2axi_hw->mem_ops = &dvp2axi_vb2_mem_ops;
	} else {
		dvp2axi_hw->is_use_dvp2axi_mem_ops = false;
		dvp2axi_hw->mem_ops = &vb2_dma_contig_memops;
		dev_info(&pdev->dev, "DVP2AXI use vb2_dma_contig_memops\n");
	}
	dvp2axi_mem_pool_sysfs_init(dvp2axi_hw->mem_pool, &dev->kobj);
	dvp2axi_hw->is_dma_sg_ops = false;
	dvp2axi_hw->is_dma_contig = true;
	mutex_init(&dvp2axi_hw->dev_lock);
	mutex_init(&dvp2axi_hw->dev_multi_chn_lock);
	spin_lock_init(&dvp2axi_hw->stream_lock);
	atomic_set(&dvp2axi_hw->power_cnt, 0);

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

	dvp2axi_mem_pool_sysfs_cleanup(dvp2axi_hw->mem_pool);
	dvp2axi_mem_pool_destroy(dvp2axi_hw->mem_pool);

	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_grp);

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
	struct device *parent = dev->parent;
	u32 reg_val = 0;
	int parent_count;

	win2030_tbu_power(dev, false);

	reset_control_assert(dvp2axi_hw->rstc);

	regmap_read(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, &reg_val);
	reg_val &= (~DVP2AXI_DVP_CLK_EN);
	regmap_write(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, reg_val);

	es_dvp2axi_sys_clk_disable(dvp2axi_hw);

	parent_count = atomic_read(&parent->power.usage_count);
	if ((parent && pm_runtime_enabled(parent)) && (parent_count)) {
		pm_runtime_mark_last_busy(parent);
		pm_runtime_put_autosuspend(parent);
    }

	return 0;
}

static int __maybe_unused es_dvp2axi_runtime_resume(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	struct eswin_vi_device* es_vi_dev;
	u32 reg_val = 0;
	int ret;

	es_vi_dev = dev_get_drvdata(parent);
	if (!es_vi_dev) {
		return -ENODEV;
	}

	regmap_read(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, &reg_val);
	reg_val |= (DVP2AXI_DVP_CLK_EN | CTRL_DVP_CLK_EN);
	regmap_write(dvp2axi_hw->vi_topcsr_regmap, dvp2axi_hw->vi_topcsr_reg, reg_val);

	if (parent && pm_runtime_enabled(parent)) {
        ret = pm_runtime_resume_and_get(parent);
        if (ret < 0) {
            dev_err(dev, "Failed to resume parent VI: %d\n", ret);
            return ret;
        }
    }

	es_dvp2axi_sys_clk_enable(dvp2axi_hw);

	reset_control_deassert(dvp2axi_hw->rstc);

	win2030_tbu_power(dev, true);

	eic770x_vi_init(es_vi_dev);

	eic770x_top_clk_init(es_vi_dev);

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