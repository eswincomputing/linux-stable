// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN vi top driver
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/media.h>
#include <linux/platform_device.h>
#include <linux/mdev.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/eswin-win2030-sid-cfg.h>

#include "eswin_vi.h"
#include "es_vi_cmn_register.h"

#include <linux/pm_runtime.h>
#if defined(CONFIG_PM_DEVFREQ)
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#endif

#define DRIVER_NAME "eswin-vi"
#define DEVICE_NAME "eswin_vi"
#define CLASS_NAME "eswin_vi_class"

static int major_number;

#define ES_VITOP_REG_OFFSET 0x30000
#define ES_VITOP_WRITE_REG(base, offset, val) writel(val, base + offset+ES_VITOP_REG_OFFSET)
#define ES_VITOP_READ_REG(base, offset) readl(base + offset+ES_VITOP_REG_OFFSET)

#define eswin_vi_DW_CLK_HIGHEST 594000000
#define eswin_vi_AXI_CLK_HIGHEST 800000000

#if defined(__GNUC__) || defined(__clang__)
    #define UNUSED_FUNC __attribute__((unused))
#else
    #define UNUSED_FUNC
#endif

#define CHECK_COPY_RETVAL(retval) do { \
	if (retval < 0) { \
		pr_err("%s %d: copy from or to user failed\n", __func__, __LINE__); \
		return -EFAULT; \
	} \
} while(0);

static void vi_top_register_write(struct eswin_vi_device *es_vi_dev, u32 reg,
				  u32 val)
{
	writel(val, es_vi_dev->base + reg);
}
static u32 vi_top_register_read(struct eswin_vi_device *es_vi_dev, u32 reg)
{
	return readl(es_vi_dev->base + reg);
}

UNUSED_FUNC static void syscrg_register_write(struct eswin_vi_device *es_vi_dev,
					      u32 reg, u32 val)
{
	u32 reg_value;
	regmap_write(es_vi_dev->syscrg_regmap, reg, val);
	regmap_read(es_vi_dev->syscrg_regmap, reg, &reg_value);
}
UNUSED_FUNC static u32 syscrg_register_read(struct eswin_vi_device *es_vi_dev,
					    u32 reg)
{
	u32 reg_value;
	regmap_read(es_vi_dev->syscrg_regmap, reg, &reg_value);
	return reg_value;
}

int vitop_intf_cfg(struct eswin_vi_device *es_vi_dev)
{
	u32 val = 0;
	u32 phy_val = 0;
	unsigned int reg_value;

	phy_val = es_vi_dev->phy_mode;
	/* Enable IMX327 WDR MODE */
	if (es_vi_dev->wdr_en_2) {
		phy_val |= VI_TOP_IMX327_WDR_EN_2_BIT;
	}

	vi_top_register_write(es_vi_dev, VI_TOP_PHY_CONNECT_MODE, phy_val);
	vi_top_register_write(es_vi_dev, VI_TOP_CONTROLLER_SELECT, 0);

	switch(es_vi_dev->isp_connect_mode) {
		case ISP_CONNECT_MODE_0_2:
			val = (CSI_CONTROLLER_ID0 << VI_TOP_ISP0_DVP0_SEL_OFFSET) |
				(CSI_CONTROLLER_ID2 << VI_TOP_ISP1_DVP0_SEL_OFFSET);
			break;
		case ISP_CONNECT_MODE_0_2_4:
			val = (CSI_CONTROLLER_ID0 << VI_TOP_ISP0_DVP0_SEL_OFFSET) |
				(CSI_CONTROLLER_ID2 << VI_TOP_ISP0_DVP1_SEL_OFFSET);
			val |= (CSI_CONTROLLER_ID4 << VI_TOP_ISP1_DVP0_SEL_OFFSET);
			break;
		case ISP_CONNECT_MODE_0_1_2_3:
			val = (CSI_CONTROLLER_ID0 << VI_TOP_ISP0_DVP0_SEL_OFFSET) |
				(CSI_CONTROLLER_ID1 << VI_TOP_ISP0_DVP1_SEL_OFFSET);
			val |= (CSI_CONTROLLER_ID2 << VI_TOP_ISP1_DVP0_SEL_OFFSET) |
				(CSI_CONTROLLER_ID3 << VI_TOP_ISP1_DVP1_SEL_OFFSET);
			break;
		default:
			pr_warn(DRIVER_NAME ": isp_connect_mode matching failed use default 0\n");
			val = (CSI_CONTROLLER_ID0 << VI_TOP_ISP0_DVP0_SEL_OFFSET) |
				(CSI_CONTROLLER_ID2 << VI_TOP_ISP0_DVP1_SEL_OFFSET);
			val |= (CSI_CONTROLLER_ID4 << VI_TOP_ISP1_DVP0_SEL_OFFSET);
			break;
	}

	vi_top_register_write(es_vi_dev, VI_TOP_ISP_DVP_SEL, val);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP0_DVP0_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP0_DVP1_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);
	vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE,
		(es_vi_dev->isp_dvp0_ver << 16) |
		es_vi_dev->isp_dvp0_hor);

	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP_BLANK,
			      (0xff << 8) | 0xff);
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP0, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP1, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP2, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP3, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP0, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP1, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP2, (4 << 9));
	vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP3, (4 << 9));

	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_PHY_CONNECT_MODE);
	pr_debug("t2 VI_TOP_PHY_CONNECT_MODE[0x51030000] = %x\n", reg_value);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP_DVP_SEL);
	pr_debug("ISP0_DVP_SEL[0x51030008] = %x\n", reg_value);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP0_SIZE);
	pr_debug("ISP0_DVP0 size[0x510300%x] = %x\n", reg_value,VI_TOP_ISP0_DVP0_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP1_SIZE);
	pr_debug("ISP0_DVP0 size[0x510300%x] = %x\n", reg_value,VI_TOP_ISP0_DVP1_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP2_SIZE);
	pr_debug("ISP0_DVP0 size[0x510300%x] = %x\n", reg_value,VI_TOP_ISP0_DVP2_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP3_SIZE);
	pr_debug("ISP0_DVP1 size[0x510300%x] = %x\n", reg_value,VI_TOP_ISP0_DVP3_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE);
	pr_debug("ISP1_DVP0 size[0x510300%x] = %x\n", reg_value, VI_TOP_ISP1_DVP0_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE);
	pr_debug("ISP1_DVP1 size[0x510300%x] = %x\n", reg_value, VI_TOP_ISP1_DVP1_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP2_SIZE);
	pr_debug("ISP1_DVP2 size[0x510300%x] = %x\n", reg_value, VI_TOP_ISP1_DVP2_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP3_SIZE);
	pr_debug("ISP1_DVP3 size[0x510300%x] = %x\n", reg_value, VI_TOP_ISP1_DVP3_SIZE);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP_BLANK);
	pr_debug("ISP_MUL2ISP_BLANK[0x5103001c] = %x\n", reg_value);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP0);
	pr_debug("ISP0_DVP0 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP0_DVP0);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP1);
	pr_debug("ISP0_DVP1 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP0_DVP1);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP2);
	pr_debug("ISP0_DVP0 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP0_DVP2);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP3);
	pr_debug("ISP0_DVP1 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP0_DVP3);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP0);
	pr_debug("ISP1_DVP0 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP1_DVP0);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP1);
	pr_debug("ISP1_DVP1 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP1_DVP1);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP2);
	pr_debug("ISP1_DVP0 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP1_DVP2);
	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP3);
	pr_debug("ISP1_DVP1 multi[0x510300%x] = %x\n", reg_value, VI_TOP_MULTI2ISP1_DVP3);

	return 0;
}
EXPORT_SYMBOL(vitop_intf_cfg);

int eic770x_top_clk_init(struct eswin_vi_device *es_vi_dev)
{
	unsigned int reg_value;

	reg_value = vi_top_register_read(es_vi_dev, VI_TOP_CLOCK_ENABLE);
	vi_top_register_write(es_vi_dev, VI_TOP_CLOCK_ENABLE, reg_value | 0x1fff8);
	udelay(1000);

	return 0;
}
EXPORT_SYMBOL(eic770x_top_clk_init);

int eic770x_vi_init(struct eswin_vi_device *es_vi_dev)
{
	struct eswin_vi_device *regmap = es_vi_dev;
	unsigned int reg_value;

	syscrg_register_write(regmap, 0x470, 0x7);
	udelay(1000);

	syscrg_register_write(regmap, 0x188, 0xc0000020);

	#ifndef CONFIG_ARCH_SUSPEND_POSSIBLE
	eic770x_top_clk_init(es_vi_dev);
	#endif

	return 0;
}
EXPORT_SYMBOL(eic770x_vi_init);

UNUSED_FUNC static int eswin_vi_sys_reset_init(struct platform_device *pdev,
				struct eswin_vi_clk_rst *vi_crg)
{
	vi_crg->rstc_axi = devm_reset_control_get_shared(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(vi_crg->rstc_axi)) {
		dev_err(&pdev->dev, "Failed to get vi axi reset handle\n");
		return -EFAULT;
	}

	vi_crg->rstc_cfg = devm_reset_control_get_shared(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(vi_crg->rstc_cfg)) {
		dev_err(&pdev->dev, "Failed to get vi cfg reset handle\n");
		return -EFAULT;
	}

	return 0;
}
#define eswin_vi_CLK_GET_HANDLE(dev, clk_handle, clk_name)                     \
	{                                                                   \
		clk_handle = devm_clk_get(dev, clk_name);                   \
		if (IS_ERR(clk_handle)) {                                   \
			ret = PTR_ERR(clk_handle);                          \
			dev_err(dev, "failed to get dw %s: %d\n", clk_name, \
				ret);                                       \
			return ret;                                         \
		}                                                           \
	}

UNUSED_FUNC static int eswin_vi_sys_clk_init(struct platform_device *pdev,
			      struct eswin_vi_clk_rst *vi_crg)
{
	int ret;
	struct device *dev = &pdev->dev;

	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->aclk, "aclk");
	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->cfg_clk, "cfg_clk");
	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->aclk_mux, "aclk_mux");
	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->spll0_fout1, "spll0_fout1");
	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->spll2_fout1, "spll2_fout1");

	return 0;
}


#define eswin_vi_SYS_CLK_PREPARE(clk)                                 \
	do {                                                       \
		if (clk_prepare_enable(clk)) {                     \
			pr_err("Failed to enable clk %px\n", clk); \
		}                                                  \
	} while (0)


UNUSED_FUNC static int eswin_vi_sys_clk_config(struct eswin_vi_clk_rst *vi_crg)
{
	int ret = 0;
	long rate;

	ret = clk_set_parent(vi_crg->aclk_mux, vi_crg->spll0_fout1);
	if (ret < 0) {
		pr_err("DW: failed to set aclk_mux parent: %d\n", ret);
		return ret;
	}

	rate = clk_round_rate(vi_crg->aclk, eswin_vi_AXI_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vi_crg->aclk, rate);
		if (ret) {
			pr_err("DW: failed to set aclk: %d\n", ret);
			return ret;
		}
		pr_debug("DW set aclk to %ldHZ\n", rate);
	}

	return 0;
}

UNUSED_FUNC static int eswin_vi_sys_clk_prepare(struct eswin_vi_clk_rst *vi_crg)
{
	int ret = 0;
	eswin_vi_SYS_CLK_PREPARE(vi_crg->aclk);
	eswin_vi_SYS_CLK_PREPARE(vi_crg->cfg_clk);
	ret = win2030_tbu_power(vi_crg->dev, true);
	if (ret) {
		pr_err("%s: DW tbu power up failed\n", __func__);
		return ret;
	}
	return 0;
}

UNUSED_FUNC static int eswin_vi_sys_clk_unprepare(struct eswin_vi_clk_rst *vi_crg)
{
	int ret = 0;
	//  tbu power down need enanle clk
	ret = win2030_tbu_power(vi_crg->dev, false);
	if (ret) {
		pr_err("dw tbu power down failed\n");
		return ret;
	}
	clk_disable_unprepare(vi_crg->cfg_clk);
	clk_disable_unprepare(vi_crg->aclk);

	return 0;
}

UNUSED_FUNC static int eswin_vi_reset_fini(struct eswin_vi_clk_rst *vi_crg)
{
	reset_control_assert(vi_crg->rstc_cfg);
	reset_control_assert(vi_crg->rstc_axi);
	return 0;
}

UNUSED_FUNC static int eswin_vi_sys_reset_release(struct eswin_vi_clk_rst *vi_crg)
{
	int ret;

	ret = reset_control_deassert(vi_crg->rstc_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(vi_crg->rstc_axi);
	WARN_ON(0 != ret);

	return 0;
}

static int eswin_open(struct inode *inode, struct file *file)
{
	struct eswin_vi_device *es_vi_dev =
		container_of(inode->i_cdev, struct eswin_vi_device, es_vi_cdev);
	file->private_data = es_vi_dev;
	return 0;
}

static int eswin_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t eswin_read(struct file *file, char __user *buffer, size_t len,
			  loff_t *offset)
{
	return 0;
}

static ssize_t eswin_write(struct file *file, const char __user *buffer,
			   size_t len, loff_t *offset)
{
	return len;
}

static int eswin_vi_reset(struct eswin_vi_device *es_vi_dev,
			  struct soc_control_context *soc_ctrl)
{
	//TODO template for reset
	eic770x_vi_init(es_vi_dev);
	return 0;
}

long eswin_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int retval, ret;
	struct eswin_vi_device *es_vi_dev = file->private_data;
	struct soc_control_context soc_ctrl;
	struct isp_control_HxV isp_control_h_v;

	switch (cmd) {
	case VI_IOCTL_RESET:
		dev_dbg(es_vi_dev->dev, "VI_IOCTL_RESET\n");
		retval = copy_from_user(&soc_ctrl, (int __user *)arg,
					sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = eswin_vi_reset(es_vi_dev, &soc_ctrl);
		break;
	case VI_IOCTL_ISP_H_V:
		dev_dbg(es_vi_dev->dev, "VI_IOCTL_ISP_H_V\n");
		retval = copy_from_user(&isp_control_h_v, (int __user *)arg,
					sizeof(isp_control_h_v));
		CHECK_COPY_RETVAL(retval);
		es_vi_dev->isp_dvp0_hor = isp_control_h_v.horizontal;
		es_vi_dev->isp_dvp0_ver = isp_control_h_v.vertical;
		break;
	default:
		dev_err(es_vi_dev->dev,  "Invalid IOCTL command\n");
		return -EINVAL;
	}

	return ret;
}

static int eswin_vi_of_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct eswin_vi_device *es_vi_dev = container_of(nb, struct eswin_vi_device, of_notifier);
	struct of_overlay_notify_data *notify_data = data;
	int ret = 0;
	if (!es_vi_dev || !notify_data || !notify_data->target)
		return NOTIFY_DONE;

	if(es_vi_dev->dev->of_node != notify_data->target)
		return NOTIFY_DONE;

	if (action == OF_OVERLAY_POST_APPLY) {
		ret = of_property_read_u32(es_vi_dev->dev->of_node, "phy_mode",
			&es_vi_dev->phy_mode);
		if (ret) {
			dev_warn(es_vi_dev->dev,
				"Failed to read phy_mode property! Use Default 5!\n");
			es_vi_dev->phy_mode = 5;
		}
		dev_dbg(es_vi_dev->dev, "phy_mode=%d\n", es_vi_dev->phy_mode);
	}
	return NOTIFY_DONE;
}

static const struct media_device_ops es_mdev_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

static const struct file_operations eswin_fops = {
	.owner = THIS_MODULE,
	.open = eswin_open,
	.release = eswin_release,
	.read = eswin_read,
	.write = eswin_write,
	.unlocked_ioctl = eswin_ioctl,
};

#if defined(CONFIG_PM_DEVFREQ)
/* devfreq target function to set frequency */
static int vi_devfreq_target(struct device *dev, unsigned long *freq,
				 u32 flags)
{
	struct eswin_vi_device *es_vi_dev = dev_get_drvdata(dev);
	unsigned long spll0_rate = clk_get_rate(es_vi_dev->clk_rst.spll0_fout1);
	unsigned long spll2_rate = clk_get_rate(es_vi_dev->clk_rst.spll2_fout1);
	unsigned long target = *freq;
	int ret = 0;

	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	if (!es_vi_dev) {
		dev_err(dev, "es_vi_dev is NULL\n");
		return -EINVAL;
	}

	if (!es_vi_dev->clk_rst.aclk_mux) {
		dev_err(dev, "aclk_mux clock is NULL\n");
		return -EINVAL;
	}

	if (!es_vi_dev->clk_rst.spll0_fout1 || !es_vi_dev->clk_rst.spll2_fout1) {
		dev_err(dev, "Parent clocks are NULL\n");
		return -EINVAL;
	}

	if (spll0_rate % target < spll2_rate % target) {
		ret = clk_set_parent(es_vi_dev->clk_rst.aclk_mux, es_vi_dev->clk_rst.spll0_fout1);
	} else {
		ret = clk_set_parent(es_vi_dev->clk_rst.aclk_mux, es_vi_dev->clk_rst.spll2_fout1);
	}

	if (ret) {
		dev_err(dev, "Failed to set clock parent\n");
		return ret;
	}

	ret = clk_set_rate(es_vi_dev->clk_rst.aclk, target);
	if (ret) {
		dev_warn(dev, "aclk set rate failed");
		return ret;
	}

	return 0;
}

static int vi_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct eswin_vi_device *es_vi_dev = dev_get_drvdata(dev);
	unsigned long rate;

	rate = clk_get_rate(es_vi_dev->clk_rst.aclk);
	if (rate <= 0) {
		dev_warn(dev, "failed to get aclk rate");
		return rate;
	}
	*freq = rate;

	return 0;
}

/* devfreq profile */
static struct devfreq_dev_profile vi_devfreq_profile = {
	.initial_freq = 800000000,
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 1000, /* Poll every 1000ms to monitor load */
	.target = vi_devfreq_target,
	.get_cur_freq = vi_devfreq_get_cur_freq,
};
#endif

static int eswin_vi_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct eswin_vi_device *es_vi_dev;
	struct media_device *media_dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int numa_id = 0;
	char class_name[32];
	__maybe_unused struct eswin_vi_clk_rst *vi_clk_rst;
#if defined(CONFIG_PM_DEVFREQ)
	struct devfreq *df;
#endif

	es_vi_dev = devm_kzalloc(dev, sizeof(*es_vi_dev), GFP_KERNEL);
	if (!es_vi_dev)
		return -ENOMEM;

	//TODO: next step is to get the vi clk and rst from the device tree
	//and remove clk and rst in dewarp driver
	vi_clk_rst = &es_vi_dev->clk_rst;
	ret = eswin_vi_sys_reset_init(pdev, &es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW reset init failed\n", __func__);
		return ret;
	}

	ret = eswin_vi_sys_clk_init(pdev, &es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW clk init failed\n", __func__);
		return ret;
	}

	ret = eswin_vi_sys_clk_config(&es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW clk prepare failed\n", __func__);
		return ret;
	}

	es_vi_dev->clk_rst.dev = &pdev->dev;

	ret = eswin_vi_sys_clk_prepare(&es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW clk prepare failed\n", __func__);
		return ret;
	}

	ret = eswin_vi_sys_reset_release(&es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW reset release failed\n", __func__);
		return ret;
	}

	es_vi_dev->media_dev =
		devm_kzalloc(dev, sizeof(*media_dev), GFP_KERNEL);

	if(!es_vi_dev->media_dev)
		return -ENOMEM;

	media_dev = es_vi_dev->media_dev;
	es_vi_dev->dev = dev;
	dev_set_drvdata(dev, es_vi_dev);

#if defined(CONFIG_PM_DEVFREQ)
	/* Add OPP table from device tree */
	ret = dev_pm_opp_of_add_table(&pdev->dev);
	if (ret) {
		pr_err("%s, %d, failed to add OPP table\n", __func__, __LINE__);
		return ret;
	}

	df = devm_devfreq_add_device(&pdev->dev, &vi_devfreq_profile,
				     "userspace", NULL);
	if (IS_ERR(df)) {
		pr_err("%s, %d, add devfreq failed\n", __func__, __LINE__);
		return ret;
	}
#endif

	ret = alloc_chrdev_region(&major_number, 0, 1, "eswin_vi");
	if (ret < 0) {
		pr_err("Failed to allocate major number\n");
		return ret;
	}

	cdev_init(&es_vi_dev->es_vi_cdev, &eswin_fops);
	ret = cdev_add(&es_vi_dev->es_vi_cdev, major_number, 1);
	if (ret < 0) {
		pr_err(DRIVER_NAME ": Failed to add cdev\n");
		return ret;
	}

	ret = of_property_read_u32(np, "numa-node-id", &numa_id);
	if (ret) {
		pr_warn(DRIVER_NAME
			": Failed to read numa-node-id property! Use Default 0!\n");
		numa_id = 0;
	}
	es_vi_dev->numa_id = numa_id;

	snprintf(class_name, sizeof(class_name), "eswin_vi_class%d", numa_id);
	es_vi_dev->es_vi_class = class_create(class_name);
	if (IS_ERR(es_vi_dev->es_vi_class)) {
		cdev_del(&es_vi_dev->es_vi_cdev);
		unregister_chrdev_region(major_number, 1);
		return PTR_ERR(es_vi_dev->es_vi_class);
	}

	// 创建设备节点
	device_create(es_vi_dev->es_vi_class, NULL, major_number, NULL,
		      "es_vi%d", numa_id);
	media_device_init(media_dev);
	media_dev->dev = &pdev->dev;
	media_dev->ops = &es_mdev_ops;
	strscpy(media_dev->model, "verisilicon_media",
		sizeof(media_dev->model));

	ret = media_device_register(media_dev);
	if (ret < 0) {
		media_device_cleanup(media_dev);
		kfree(media_dev);
		cdev_del(&es_vi_dev->es_vi_cdev);
		device_destroy(es_vi_dev->es_vi_class, MKDEV(major_number, 0));
		class_destroy(es_vi_dev->es_vi_class);
		unregister_chrdev(major_number, DEVICE_NAME);
		pr_err(DRIVER_NAME ": Failed to register media device\n");
		return ret;
	}

	es_vi_dev->v4l2_dev.mdev = media_dev;
	strlcpy(es_vi_dev->v4l2_dev.name, dev_name(dev), sizeof(es_vi_dev->v4l2_dev.name));
	ret = v4l2_device_register(es_vi_dev->dev, &es_vi_dev->v4l2_dev);
	if(ret < 0) {
		media_device_unregister(media_dev);
		media_device_cleanup(media_dev);
		kfree(media_dev);
		cdev_del(&es_vi_dev->es_vi_cdev);
		device_destroy(es_vi_dev->es_vi_class, MKDEV(major_number, 0));
		class_destroy(es_vi_dev->es_vi_class);
		unregister_chrdev(major_number, DEVICE_NAME);
		pr_err(DRIVER_NAME ": Failed to register v4l2_dev\n");
		return ret;
	}
	of_platform_populate(np, NULL, NULL, &pdev->dev);

	//TODO add parser es_vi_top dts

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	es_vi_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(es_vi_dev->base)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);
		dev_warn(&pdev->dev, "avoid secondary mipi resource check!\n");
		es_vi_dev->base = devm_ioremap(&pdev->dev, offset, size);
		if (IS_ERR(es_vi_dev->base)) {
			dev_err(&pdev->dev, "Failed to ioremap resource\n");
			return PTR_ERR(es_vi_dev->base);
		}
	}

	mutex_init(&es_vi_dev->vi_topcsr_lock);

	ret = of_property_read_u32(np, "eswin,isp_dvp0_hor",
				   &es_vi_dev->isp_dvp0_hor);
	if (ret)
		es_vi_dev->isp_dvp0_hor = 3280;

	ret = of_property_read_u32(np, "eswin,isp_dvp0_ver",
				   &es_vi_dev->isp_dvp0_ver);
	if (ret)
		es_vi_dev->isp_dvp0_ver = 2464;

	ret = of_property_read_u32(np, "phy_mode", &es_vi_dev->phy_mode);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to read phy_mode property! Use Default 5!\n");
		es_vi_dev->phy_mode = 5;
	}

	ret = of_property_read_u32(np, "isp_connect_mode", &es_vi_dev->isp_connect_mode);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to read isp_connect_mode property! Use Default ISP_CONNECT_MODE_0_2_4!\n");
		es_vi_dev->isp_connect_mode = ISP_CONNECT_MODE_0_2_4;
	}

	/* imx327 wdr enabled or not */
	es_vi_dev->wdr_en_2 = of_property_read_bool(np, "imx327,wdr-en-2");

	es_vi_dev->syscrg_regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
	if (IS_ERR(es_vi_dev->syscrg_regmap)) {
		dev_err(dev, "No syscrg_csr phandle specified\n");
		return PTR_ERR(es_vi_dev->syscrg_regmap);
	}

	eic770x_vi_init(es_vi_dev);

	es_vi_dev->of_notifier.notifier_call = eswin_vi_of_notifier;
	of_overlay_notifier_register(&es_vi_dev->of_notifier);

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int eswin_remove(struct platform_device *pdev)
{
	struct eswin_vi_device *es_vi_dev = platform_get_drvdata(pdev);
	media_device_unregister(es_vi_dev->media_dev);
	media_device_cleanup(es_vi_dev->media_dev);
	kfree(es_vi_dev->media_dev);
	cdev_del(&es_vi_dev->es_vi_cdev);
	device_destroy(es_vi_dev->es_vi_class, MKDEV(major_number, 0));
	class_destroy(es_vi_dev->es_vi_class);
	unregister_chrdev(major_number, DEVICE_NAME);
	of_platform_depopulate(&pdev->dev);
	pr_debug(DRIVER_NAME ": Removed\n");
	return 0;
}

static const struct of_device_id eswin_id_table[] = {
	{ .compatible = "esw,vi_subsys", .data = 0 },
	{}
};

static struct platform_driver eswin_vi_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(eswin_id_table),
    },
    .probe = eswin_vi_probe,
    .remove = eswin_remove,
};

int es_vi_drv_init(void)
{
	platform_driver_register(&eswin_vi_driver);
	return 0;
}

static void __exit es_vi_drv_exit(void)
{
	platform_driver_unregister(&eswin_vi_driver);
}

module_init(es_vi_drv_init);
module_exit(es_vi_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yufangxian@eswincomputing.com");
MODULE_DESCRIPTION("ESWIN VI Driver");
MODULE_VERSION("0.1");