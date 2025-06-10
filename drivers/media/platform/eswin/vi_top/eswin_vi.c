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

#include <media/eswin/eswin_vi.h>
#include <media/eswin/common-def.h>

#include "vitop.h"
#include <linux/eswin-win2030-sid-cfg.h>

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



static int viscu_cfg(struct eswin_vi_device *es_vi_dev)
{
	struct regmap *regmap = es_vi_dev->syscrg_regmap;
    
	regmap_write(regmap, VI_SUBSYSTEM_SCU_ISP_RESET, 0x0);///isp reset(sys_crg)
	pr_debug("SCU: ISP Reset ...\n");
	regmap_write(regmap, VI_SUBSYSTEM_SCU_ISP_RESET, 0x1);

	// CSI controller 0 HDR mode
	pr_debug("SCU: HDR Disabled ...\n");
    // regmap_write(regmap, VI_SUBSYSTEM_SCU_SONY_HDR_MODE, 0x0);///i2c_rst_ctl(sys_crg)
	return 0;
}

static void vi_top_register_write(struct eswin_vi_device *es_vi_dev, u32 reg, u32 val)
{
    writel(val, es_vi_dev->base + reg);
}
static u32 vi_top_register_read(struct eswin_vi_device *es_vi_dev, u32 reg)
{
    return readl(es_vi_dev->base + reg);
}

UNUSED_FUNC static void syscrg_register_write(struct eswin_vi_device *es_vi_dev, u32 reg, u32 val)
{
    u32 reg_value;
    pr_debug("%s reg:0x%x, val:0x%x", __func__, reg, val);
    regmap_write(es_vi_dev->syscrg_regmap, reg, val);
    regmap_read(es_vi_dev->syscrg_regmap, reg, &reg_value);
    pr_debug("read reg:0x%x, val:0x%x", reg, reg_value);
}
UNUSED_FUNC static u32 syscrg_register_read(struct eswin_vi_device *es_vi_dev, u32 reg)
{
    u32 reg_value;
    regmap_read(es_vi_dev->syscrg_regmap, reg, &reg_value);
    pr_debug("%s reg:0x%x, val:0x%x", __func__, reg, reg_value);
    return reg_value;
}

static int vitop_intf_cfg(struct eswin_vi_device *es_vi_dev)
{
	u32 val = 0;
	unsigned int reg_value;

    pr_debug("ISP Top Setting ...\n");
    #ifdef SENSOR_OUT_2LANES
	vi_top_register_write(es_vi_dev, VI_TOP_PHY_CONNECT_MODE, 5);
    #else
	vi_top_register_write(es_vi_dev, VI_TOP_PHY_CONNECT_MODE, 3);
    #endif

    vi_top_register_write(es_vi_dev, VI_TOP_CONTROLLER_SELECT, 0);
	
	val = (CSI_CONTROLLER_ID << VI_TOP_ISP0_DVP0_SEL_OFFSET) | (CSI_CONTROLLER_ID << VI_TOP_ISP0_DVP1_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP0_DVP2_SEL_OFFSET) | (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP3_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP0_SEL_OFFSET) | (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP1_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP2_SEL_OFFSET) | (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP3_SEL_OFFSET);

    vi_top_register_write(es_vi_dev, VI_TOP_ISP_DVP_SEL, val);
    vi_top_register_write(es_vi_dev, VI_TOP_ISP0_DVP0_SIZE, (es_vi_dev->isp_dvp0_ver << 16) | es_vi_dev->isp_dvp0_hor);
    vi_top_register_write(es_vi_dev, VI_TOP_ISP0_DVP1_SIZE, (es_vi_dev->isp_dvp0_ver << 16) | es_vi_dev->isp_dvp0_hor);
    vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE, (es_vi_dev->isp_dvp0_ver << 16) | es_vi_dev->isp_dvp0_hor);
    vi_top_register_write(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE, (es_vi_dev->isp_dvp0_ver << 16) | es_vi_dev->isp_dvp0_hor);
    vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP_BLANK, (0xff << 8) | 0xff);
    vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP0, (4 << 9));
    vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP0_DVP1, (4 << 9));
    vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP0, (4 << 9));
    vi_top_register_write(es_vi_dev, VI_TOP_MULTI2ISP1_DVP1, (4 << 9));


    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_PHY_CONNECT_MODE);
    pr_debug("t2 VI_TOP_PHY_CONNECT_MODE[0x51030000] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP_DVP_SEL);
    pr_debug("ISP0_DVP_SEL[0x51030008] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP0_SIZE);
    pr_debug("ISP0_DVP0 size[0x5103000c] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP0_DVP1_SIZE);
    pr_debug("ISP0_DVP1 size[0x51030010] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP0_SIZE);
    pr_debug("ISP1_DVP0 size[0x51030014] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_ISP1_DVP1_SIZE);
    pr_debug("ISP1_DVP1 size[0x51030018] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP_BLANK);
    pr_debug("ISP_MUL2ISP_BLANK[0x5103001c] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP0);
    pr_debug("ISP0_DVP0 multi[0x51030020] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP0_DVP1);
    pr_debug("ISP0_DVP1 multi[0x51030024] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP0);
    pr_debug("ISP1_DVP0 multi[0x51030028] = %x\n", reg_value);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_MULTI2ISP1_DVP1);
    pr_debug("ISP1_DVP1 multi[0x5103002c] = %x\n", reg_value);

	return 0;
}


static int eic770x_vi_init(struct eswin_vi_device *es_vi_dev)
{
    struct eswin_vi_device *regmap = es_vi_dev;
    unsigned int reg_value;

    syscrg_register_write(regmap, 0x200, 0xffffffff);///lsp_clk_en0 enable(sys_crg)
    syscrg_register_write(regmap, 0x424, 0x3ff);///i2c_rst_ctl(sys_crg)
    
    udelay(1000);


    syscrg_register_write(regmap, 0x470, 0x7);///vi_rst_ctl
    syscrg_register_write(regmap, 0x474, 0x1);///dvp_rst_ctl
    syscrg_register_write(regmap, 0x478, 0x1);///isp0_rst_ctl
    syscrg_register_write(regmap, 0x47c, 0x1);///isp1_rst_ctl
    syscrg_register_write(regmap, 0x480, 0x1);///shutter_rst_ctl
    udelay(20000);


    syscrg_register_write(regmap, 0x184, 0x80000020);///vi_dwclk_ctl
    syscrg_register_write(regmap, 0x188, 0xc0000020);///vi_aclk_ctl
    syscrg_register_write(regmap, 0x18c, 0x80000020);///vi_dig_isp_clk_ctl
	#ifdef CONFIG_EIC7700_EVB_VI
    syscrg_register_write(regmap, 0x190, 0x80000020);///vi_dvp_clk_ctl
	#else
    syscrg_register_write(regmap, 0x190, 0x80000020);///vi_dvp_clk_ctl
	#endif
    
	#ifdef CONFIG_EIC7700_EVB_VI
    syscrg_register_write(regmap, 0x194, 0x80000100);///vi_shutter0
    syscrg_register_write(regmap, 0x198, 0x80000100);///vi_shutter1
	#else
	syscrg_register_write(regmap, 0x194, 0x80000180);///vi_shutter0
	syscrg_register_write(regmap, 0x198, 0x80000180);///vi_shutter1
	#endif

    syscrg_register_write(regmap, 0x19c, 0x80000100);///vi_shutter2
    syscrg_register_write(regmap, 0x1a0, 0x80000100);///vi_shutter3
    syscrg_register_write(regmap, 0x1a4, 0x80000100);///vi_shutter4
    syscrg_register_write(regmap, 0x1a8, 0x80000100);///vi_shutter5
    syscrg_register_write(regmap, 0x1ac, 0x3);///vi_phy_clk_ctl

    // Enable Clocks from TOP CSR
    vi_top_register_write(es_vi_dev, VI_TOP_CLOCK_ENABLE, 0xffffffff);
    reg_value = vi_top_register_read(es_vi_dev, VI_TOP_CLOCK_ENABLE);
    pr_debug("ISP_TOP_CLOCK_EN[0x51030040] = %x\n", reg_value);
    
    udelay(200000);

	viscu_cfg(es_vi_dev);///isp_rst(sys_crg)

    vitop_intf_cfg(es_vi_dev);///(vi_top_cfg)
   
    
    return 0;
}



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
	eswin_vi_CLK_GET_HANDLE(dev, vi_crg->vpll_fout1, "vpll_fout1");

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
		pr_info("DW set aclk to %ldHZ\n", rate);
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
    pr_info(DRIVER_NAME ": Device opened\n");
    struct eswin_vi_device *es_vi_dev = container_of(inode->i_cdev, struct eswin_vi_device, es_vi_cdev);
    file->private_data = es_vi_dev;
    return 0;
}

static int eswin_release(struct inode *inode, struct file *file)
{
    pr_info(DRIVER_NAME ": Device closed\n");
    return 0;
}

static ssize_t eswin_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    pr_info(DRIVER_NAME ": Read operation not implemented\n");
    return 0;
}

static ssize_t eswin_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
    pr_info(DRIVER_NAME ": Write operation not implemented\n");
    return len;
}

static int eswin_vi_reset(struct eswin_vi_device *es_vi_dev, struct soc_control_context* soc_ctrl)
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
    pr_info(DRIVER_NAME ": IOCTL In\n");

    switch (cmd) {
        case VI_IOCTL_RESET:
            pr_info(DRIVER_NAME ": VI_IOCTL_RESET\n");
            retval = copy_from_user(&soc_ctrl, (int __user *)arg, sizeof(soc_ctrl));
		    CHECK_COPY_RETVAL(retval);
            ret = eswin_vi_reset(es_vi_dev, &soc_ctrl);
            break;
        case VI_IOCTL_ISP_H_V:
            pr_info(DRIVER_NAME ": VI_IOCTL_ISP_H_V\n");
            retval = copy_from_user(&isp_control_h_v, (int __user *)arg, sizeof(isp_control_h_v));
            es_vi_dev->isp_dvp0_hor = isp_control_h_v.horizontal;
            es_vi_dev->isp_dvp0_ver = isp_control_h_v.vertical;
            break;
        default:
            pr_err(DRIVER_NAME ": Invalid IOCTL command\n");
            return -EINVAL;
    }

    pr_info(DRIVER_NAME ": IOCTL Out\n");

    return ret;
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

static int eswin_vi_probe(struct platform_device *pdev)
{
    int ret;
    struct device *dev = &pdev->dev;
    struct eswin_vi_device *es_vi_dev;
    struct media_device *media_dev;
    struct device_node *np = pdev->dev.of_node;
    struct resource* res;
    __maybe_unused struct eswin_vi_clk_rst *vi_clk_rst;

    pr_info("%s: enter\n", __func__);
    es_vi_dev = devm_kzalloc(dev, sizeof(*es_vi_dev), GFP_KERNEL);
    
    es_vi_dev->isp_dvp0_hor = SENSOR_OUT_H;
    es_vi_dev->isp_dvp0_ver = SENSOR_OUT_V;

#if 1
    //TODO: next step is to get the vi clk and rst from the device tree
    //and remove clk and rst in dewarp driver
    vi_clk_rst = &es_vi_dev->clk_rst;
	ret = eswin_vi_sys_reset_init(pdev, &es_vi_dev->clk_rst);
	if (ret) {
		pr_err("%s: DW reset init failed\n", __func__);
		return ret;
	}

	ret = eswin_vi_sys_clk_init(pdev,&es_vi_dev->clk_rst);
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
#endif
    es_vi_dev->media_dev = devm_kzalloc(dev, sizeof(*media_dev), GFP_KERNEL);

    media_dev = es_vi_dev->media_dev;
    if (!es_vi_dev) {
        pr_err(DRIVER_NAME ": Failed to allocate device\n");
        return -ENOMEM;
    }
    es_vi_dev->dev = dev;
    dev_set_drvdata(dev, es_vi_dev);

    ret = alloc_chrdev_region(&major_number, 0, 1, "eswin_vi");
    if (ret < 0) {
        pr_err("Failed to allocate major number\n");
        return ret;
    }

    // Initialize cdev
    cdev_init(&es_vi_dev->es_vi_cdev, &eswin_fops);
    ret = cdev_add(&es_vi_dev->es_vi_cdev, major_number, 1);
    if (ret < 0) {
        pr_err(DRIVER_NAME ": Failed to add cdev\n");
        return ret;
    }

    es_vi_dev->es_vi_class = class_create("eswin_vi_class");
    if (IS_ERR(es_vi_dev->es_vi_class)) {
        cdev_del(&es_vi_dev->es_vi_cdev);
        unregister_chrdev_region(major_number, 1);
        return PTR_ERR(es_vi_dev->es_vi_class);
    }

    // 创建设备节点
    device_create(es_vi_dev->es_vi_class, NULL, major_number, NULL, "eswin_vi");
    media_device_init(media_dev);
    media_dev->dev = &pdev->dev;
    media_dev->ops = &es_mdev_ops;
    strscpy(media_dev->model, "verisilicon_media", sizeof(media_dev->model));

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

    es_vi_dev->syscrg_regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
    if (IS_ERR(es_vi_dev->syscrg_regmap)) {
        dev_err(dev, "No syscrg_csr phandle specified\n");
        return PTR_ERR(es_vi_dev->syscrg_regmap);
    }


    eic770x_vi_init(es_vi_dev);
    pr_info(DRIVER_NAME ": Probe successful\n");


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
    pr_info(DRIVER_NAME ": Removed\n");
    return 0;
}

static const struct of_device_id eswin_id_table[] = {
    { 
        .compatible = "esw,vi_subsys", 
        .data = 0 
    },
    { }
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
MODULE_DESCRIPTION("ESWIN VI TOP Driver");
MODULE_VERSION("0.1");
