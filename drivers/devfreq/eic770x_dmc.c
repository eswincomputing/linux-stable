// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * Author: Donghuawei
 */


#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <asm/cpuidle.h>
#include <asm/cacheflush.h>
#include <soc/eswin/eswin-lpcpu.h>

struct eic770x_dmcfreq {
	struct device *dev;
	struct mutex lock;
	struct devfreq *devfreq;
	struct devfreq_dev_profile profile;
	struct devfreq_simple_ondemand_data ondemand_data;
	unsigned long rate, target_rate;
	struct mbox_chan *mbox_chan;
	void __iomem *sys_crg_base;
	int numa_id;
};

struct mbox_msg {
	u32 data_l;
	u32 data_h;
};

#define DDR_CHANGE_FREQ 0xddcaef

static int eic770x_dmcfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct eic770x_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long target_rate;
	int ret = 0;
	struct mbox_msg mbox;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		return PTR_ERR(opp);
	}
	target_rate = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);
	
	if (dmcfreq->rate == target_rate) {
		return 0;
	}
	
	mutex_lock(&dmcfreq->lock);
	mbox.data_l = DDR_CHANGE_FREQ;
	mbox.data_h = target_rate;
	dev_info(dev, "current rate = %lu, target_rate=%lu\n", dmcfreq->rate, target_rate);

	ret = lpcpu_send_message(dmcfreq->mbox_chan, (u8*)&mbox);
	if (ret) {
		dev_err(dev, "eic770x dmc cannot change freq from %lu to %lu failed.\n", dmcfreq->rate, target_rate);
		goto err;
	}
	dmcfreq->rate = target_rate;
err:
	mutex_unlock(&dmcfreq->lock);
	return ret;
}

static int eic770x_dmcfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct eic770x_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;
	
	stat->current_frequency = dmcfreq->rate;
	return ret;
}

static int eic770x_dmcfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct eic770x_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	*freq = dmcfreq->rate;
	return 0;
}

static struct devfreq_dev_profile dev_profile = {
	.polling_ms = 100,
	.target = eic770x_dmcfreq_target,
	.get_dev_status = eic770x_dmcfreq_get_dev_status,
	.get_cur_freq = eic770x_dmcfreq_get_cur_freq,
};

static int get_dmc_rate(void *sys_crg_base)
{	
	u32 pll0, pll1, pll2;
	u32 rate0, rate1, rate2;

	pll0 = readl(sys_crg_base + 0x78);
	pll1 = readl(sys_crg_base + 0x7c);
	pll2 = readl(sys_crg_base + 0x80);

	rate0 = (pll0 >> 16) & 0xffff;
	rate1 = (pll1 >> 16) & 0xffff;
	rate2 = pll2 & 0xffff;
	switch (rate0) {
	case 0x02c0:
		return 1066;
	case 0x0850:
		if (rate2 == 0x104) {
			return 1600;
		} else {
			return 3200;
		}
	case 0x0b10:
		return 4266;
	case 0x0e50:
		return 5500;
	case 0xfa0:
		return 6000;
	case 0x10a0:
		return 6400;
	default:
		return -1;
	}
}

static int eic770x_dmcfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct eic770x_dmcfreq *data;
	int ret;
	int numa_id;
	void __iomem *sys_crg_base;
	struct device_node *lpcpu_node;
	struct platform_device *lpcpu_pdev;

	pr_info("ESWIN DDR Frequency Change Driver Probe\n");
	data = devm_kzalloc(dev, sizeof(struct eic770x_dmcfreq), GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}
	mutex_init(&data->lock);

	if(of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
                numa_id = 0;
        }
        dev_dbg(&pdev->dev, "numa_id=%d\n", numa_id);
        data->numa_id = numa_id;
	
	sys_crg_base = devm_ioremap(&pdev->dev, 0x51828000 + numa_id * 0x20000000, 0x1000);
	if (IS_ERR(sys_crg_base)) {
		dev_err(&pdev->dev, "ioremap sys_crg error\n");
		return PTR_ERR(sys_crg_base);
	}

	data->sys_crg_base = sys_crg_base;

	lpcpu_node = of_parse_phandle(pdev->dev.of_node, "lpcpu", 0);
	if (lpcpu_node == NULL) {
		dev_err(&pdev->dev, "ESWIN DDR Relay LPCPU, But Not Found THis Node, Error\n");
		return -EINVAL;
	}
	lpcpu_pdev = of_find_device_by_node(lpcpu_node);
	if (lpcpu_pdev == NULL) {
		dev_err(&pdev->dev, "Cannot Find LPCPU platform dev, need retry probe.\n");
		return -EPROBE_DEFER;
	}
	data->mbox_chan = lpcpu_get_mboxchan(lpcpu_pdev);
	if (!data->mbox_chan) {
		dev_err(dev, "Get LPCPU MailBox Channel failed.\n");
		return -ENODEV;
	}

	if (devm_pm_opp_of_add_table(dev)) {
		dev_err(dev, "Invalid OPP in device tree.\n");
		ret = -EINVAL;
		goto err_table;
	}
	
	data->rate = get_dmc_rate(sys_crg_base);
	data->profile  = dev_profile;
	data->profile.initial_freq = data->rate;

	data->devfreq = devm_devfreq_add_device(dev, &data->profile, DEVFREQ_GOV_SIMPLE_ONDEMAND, &data->ondemand_data);
	if (IS_ERR(data->devfreq)) {
		ret = PTR_ERR(data->devfreq);
		goto err_table;
	}

	devm_devfreq_register_opp_notifier(dev, data->devfreq);
	data->dev = dev;
	platform_set_drvdata(pdev, data);
	dev_info(dev, "ESWIN DDR Freq Change Register Successfully\n");
	return 0;

err_table:
	dev_err(dev, "ESWIN DDR Freq Change Register Failed.\n");
	return ret;
}


static int eic770x_dmcfreq_remove(struct platform_device *pdev)
{
	struct eic770x_dmcfreq *dmcfreq = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "ESWIN DDR Node%d Freq Change DRV Remove Done.\n", dmcfreq->numa_id);
	return 0;
}

static const struct of_device_id eic770xdmc_devfreq_of_match[] = {
	{ .compatible = "eswin,eic770x-dmc-freq" },
	{ },
};
MODULE_DEVICE_TABLE(of, eic770xdmc_devfreq_of_match);

static struct platform_driver eic770x_dmcfreq_driver = {
	.probe = eic770x_dmcfreq_probe,
	.remove = eic770x_dmcfreq_remove,
	.driver = {
		.name = "eswin-eic770x-dmc-freq",
		.of_match_table = eic770xdmc_devfreq_of_match,
	},
};
module_platform_driver(eic770x_dmcfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ESWIN EIC770x dmcfreq driver with devfreq framework");
