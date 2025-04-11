/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/

#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/poll.h>
#include <linux/dmabuf-heap-import-helper.h>
#include <linux/eswin-win2030-sid-cfg.h>

#include "soc_ioctl.h"

#define VIVCAM_SOC_NAME "vivsoc"
#define VIVCAM_SOC_MAXCNT 2

#define VVCAM_AXI_CLK_HIGHEST 800000000
#define VVCAM_DVP_CLK_HIGHEST 594000000
#define VVCAM_ISP_CLK_HIGHEST 594000000

static unsigned int imx_hdr = 0;
static unsigned int phy_mode = PHY_MODE_LANE_2_2_2_2_2_2;

static unsigned int vvcam_soc_major = 0;
static unsigned int vvcam_soc_minor = 0;
static struct class *vvcam_soc_class;
static unsigned int devise_register_index;

module_param(imx_hdr, int ,S_IRUGO);
module_param(phy_mode, int ,S_IRUGO);

// #define VI_SMMU_ENABLE

static int vvcam_soc_open(struct inode *inode, struct file *file)
{
	struct visoc_pdata *pdata;
	struct platform_device *pdev;
	struct vvcam_soc_driver_dev *pdriver_dev;

	pdata = kzalloc(sizeof(struct visoc_pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: kzalloc failed for private data\n", __func__);
		return -ENOMEM;
	}

	pdriver_dev =
	    container_of(inode->i_cdev, struct vvcam_soc_driver_dev, cdev);

	pdev = pdriver_dev->pdev;
	pdata->pdrv_dev = pdriver_dev;
	pdata->dvp_idx = -1;
	common_dmabuf_heap_import_init(&pdata->root, &pdev->dev);
	file->private_data = pdata;

	return 0;
};

static long vvcam_soc_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	long ret = 0;
	struct visoc_pdata *pdata;
	struct vvcam_soc_driver_dev *pdriver_dev;

	pdata = file->private_data;
	pdriver_dev = pdata->pdrv_dev;
	if (pdriver_dev == NULL) {
		pr_err("%s:file private is null point error\n", __func__);
		return -ENODEV;
	}
	ret = soc_priv_ioctl(pdata, cmd, (void __user *)arg);

	return ret;
};

static unsigned int vvcam_soc_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0, i;
	struct visoc_pdata *pdata;
	struct vvcam_soc_dev *psoc_dev;
	struct vvcam_soc_driver_dev *pdriver_dev;

	pdata = file->private_data;
	pdriver_dev = pdata->pdrv_dev;
	psoc_dev = pdriver_dev->private;

	poll_wait(file, &psoc_dev->irq_wait, wait);

	if (atomic_read(&psoc_dev->irqc_err)) {
		pr_info("%s: err int recved\n", __func__);
		mask |= POLLERR;
		atomic_dec(&psoc_dev->irqc_err);
		goto out;
	}

	for (i = 0; i < VI_DVP2AXI_VIRTUAL_CHNS; i++) {
		if (atomic_read(&psoc_dev->dvp2axi_vchn_didx[pdata->dvp_idx][i]) &&
				    atomic_read(&psoc_dev->irqc[pdata->dvp_idx][i]) < SENSOR_FRAME_NUM) {
			pr_info("%s: frame recved, irqc %d\n", __func__, atomic_read(&psoc_dev->irqc[pdata->dvp_idx][i]));
			atomic_inc(&psoc_dev->irqc[pdata->dvp_idx][i]);
			mask |= POLLIN | POLLRDNORM;
			goto out;
		}
	}

out:
	return mask;
}

static int vvcam_soc_release(struct inode *inode, struct file *file)
{
	struct visoc_pdata *pdata = file->private_data;

	kfree(pdata);

	return 0;
};

static int soc_debug_open(struct inode* inode, struct file* filp)
{
    filp->private_data = inode->i_private;

    return 0;
}

static ssize_t soc_debug_read(struct file* filp, char __user *buffer, size_t count, loff_t* ppos)
{
    uint32_t reg_dvp0_io = 0; 
    uint32_t reg_dvp1_io = 0; 
    uint32_t reg_isp0_dvp0 = 0; 
    uint32_t reg_isp0_dvp1 = 0; 
    uint32_t reg_isp1_dvp0 = 0; 
    uint32_t reg_isp1_dvp1 = 0; 
	uint32_t reg_dvp0_cnt = 0;
	uint32_t reg_dvp1_cnt = 0;
	uint32_t reg_dvp2_cnt = 0;
	uint32_t reg_dvp3_cnt = 0;
	uint32_t reg_dvp4_cnt = 0;
	uint32_t reg_dvp5_cnt = 0;
	uint32_t reg_mmu_isp0 = 0;
	uint32_t reg_mmu_isp1 = 0;
	uint32_t reg_mmu_dw200 = 0;
	uint32_t reg_mmu_dvp = 0;

    char ret_buff[256];
    struct vvcam_soc_dev * psoc_dev = NULL;
    psoc_dev = filp->private_data;

    psoc_dev->soc_access.read(psoc_dev, 0x6c, &reg_dvp0_cnt);
    psoc_dev->soc_access.read(psoc_dev, 0x70, &reg_dvp1_cnt);
    psoc_dev->soc_access.read(psoc_dev, 0x74, &reg_dvp2_cnt);
    psoc_dev->soc_access.read(psoc_dev, 0x78, &reg_dvp3_cnt);
    psoc_dev->soc_access.read(psoc_dev, 0x7c, &reg_dvp4_cnt);
    psoc_dev->soc_access.read(psoc_dev, 0x80, &reg_dvp5_cnt);

    psoc_dev->soc_access.read(psoc_dev, 0x84, &reg_dvp0_io);
    psoc_dev->soc_access.read(psoc_dev, 0x88, &reg_dvp1_io);
    psoc_dev->soc_access.read(psoc_dev, 0x8c, &reg_isp0_dvp0);
    psoc_dev->soc_access.read(psoc_dev, 0x90, &reg_isp0_dvp1);
    psoc_dev->soc_access.read(psoc_dev, 0x94, &reg_isp1_dvp0);
    psoc_dev->soc_access.read(psoc_dev, 0x98, &reg_isp1_dvp1);

    psoc_dev->soc_access.read(psoc_dev, 0x1000, &reg_mmu_isp0);
    psoc_dev->soc_access.read(psoc_dev, 0x1004, &reg_mmu_isp1);
    psoc_dev->soc_access.read(psoc_dev, 0x1008, &reg_mmu_dw200);
    psoc_dev->soc_access.read(psoc_dev, 0x100c, &reg_mmu_dvp);

    count = sprintf(ret_buff, "dvp0-5 cnt:%x %x %x %x %x %x, dvp0_io %d dvp1_io %d isp0_dvp0 %d isp0_dvp1 %d isp1_dvp0 %d isp1_dvp1 %d, smmu 0x%x 0x%x 0x%x 0x%x\n", 
            reg_dvp0_cnt, reg_dvp1_cnt, reg_dvp2_cnt, reg_dvp3_cnt, reg_dvp4_cnt,reg_dvp5_cnt,
			reg_dvp0_io, reg_dvp1_io, reg_isp0_dvp0, reg_isp0_dvp1, reg_isp1_dvp0, reg_isp1_dvp1,
            reg_mmu_isp0, reg_mmu_isp1, reg_mmu_dw200, reg_mmu_dvp);

    return simple_read_from_buffer(buffer, count, ppos, ret_buff, count);
}

static int soc_reset_open(struct inode* inode, struct file* filp)
{
    filp->private_data = inode->i_private;

    return 0;
}
/*
Writing the registers of smmu tbl may fail, 
so we read back those value to ensure the writing was succeefully.
*/

#ifdef VI_SMMU_ENABLE
static int soc_write_dynm_reg(struct vvcam_soc_dev  *psoc_dev, unsigned int addr, unsigned int val)
{
	int try_times = 100;
	int read_val = 0;
	int i = 0;
	int ret = -1;

	for(i = 0; i < try_times; i++)
	{
		psoc_dev->soc_access.write(psoc_dev, addr, val);
		udelay(1);

		psoc_dev->soc_access.read(psoc_dev, addr, &read_val);
		if(read_val == val)
		{
			ret = 0;
			break;
		}
	}
	if(i >= try_times)
	{
		pr_err("%s failed!\n",__func__);
	}

    return ret;
}
#endif

static ssize_t soc_reset_write(struct file* filp, const char __user *buffer, size_t count, loff_t* ppos)
{
    struct vvcam_soc_dev * psoc_dev = NULL;
	char str_buff[64] = {0};

    #ifdef VI_SMMU_ENABLE
	uint32_t reg_mmu_isp0 = 0;
	uint32_t reg_mmu_isp1 = 0;
	uint32_t reg_mmu_dw200 = 0;
	uint32_t reg_mmu_dvp = 0;
    #endif

    psoc_dev = filp->private_data;

    if (*ppos >= 64) {
			pr_err("%s: ppos out of range\n", __func__);
        return 0;
    }

    if (*ppos + count > 64) {
        count = 64 - *ppos;
    }

    if (copy_from_user(str_buff + *ppos, buffer, count) != 0) {
		pr_err("%s: copy_from_user error\n", __func__);
        return -EFAULT;
    }
    *ppos += count;

    /*the val is reset register offset*/
	//val = (uint32_t)simple_strtoul(str_buff, NULL, 16);
	//pr_info("%s: Get isp_reg_addr: 0x%x\n", __func__, val);

    #ifdef M2_REGISTER_MAP
		/*带SMMU场景，复位前需要记录smmu stream id，复位后需要恢复smmu stream id*/
		#ifdef VI_SMMU_ENABLE
		psoc_dev->soc_access.read(psoc_dev, 0x1000, &reg_mmu_isp0);
		psoc_dev->soc_access.read(psoc_dev, 0x1004, &reg_mmu_isp1);
		psoc_dev->soc_access.read(psoc_dev, 0x1008, &reg_mmu_dw200);
		psoc_dev->soc_access.read(psoc_dev, 0x100c, &reg_mmu_dvp);
		#endif

		__raw_writel(0x0, psoc_dev->scu_base + 0x470);
		__raw_writel(0x7, psoc_dev->scu_base + 0x470);/*vi_rst_ctrl*/
		__raw_writel(0x0, psoc_dev->scu_base + 0x474);
		__raw_writel(0x1, psoc_dev->scu_base + 0x474);/*dvp_rst_ctrl*/
		__raw_writel(0x0, psoc_dev->scu_base + 0x478);
		__raw_writel(0x1, psoc_dev->scu_base + 0x478);/*isp0_rst_ctrl*/
		__raw_writel(0x0, psoc_dev->scu_base + 0x47c);
		__raw_writel(0x1, psoc_dev->scu_base + 0x47c);/*isp1_rst_ctrl*/

		#ifdef VI_SMMU_ENABLE
		soc_write_dynm_reg(psoc_dev, 0x1000, reg_mmu_isp0);
		soc_write_dynm_reg(psoc_dev, 0x1004, reg_mmu_isp1);
		soc_write_dynm_reg(psoc_dev, 0x1008, reg_mmu_dw200);
		soc_write_dynm_reg(psoc_dev, 0x100c, reg_mmu_dvp);
		#endif

    #else
	/*hardware reset the isp*/
	__raw_writel(0x0, psoc_dev->scu_base + 0x0c);
	__raw_writel(0x1, psoc_dev->scu_base + 0x0c);
    #endif

    return count;
}

static struct file_operations vvcam_soc_fops = {
	.owner = THIS_MODULE,
	.open = vvcam_soc_open,
	.release = vvcam_soc_release,
	.unlocked_ioctl = vvcam_soc_ioctl,
	.poll           = vvcam_soc_poll,
};

struct file_operations soc_debug_fops = {
    .owner = THIS_MODULE,
    .open = soc_debug_open,
    .read = soc_debug_read,
};

struct file_operations soc_reset_fops = {
    .owner = THIS_MODULE,
    .open = soc_reset_open,
    .write = soc_reset_write,
};

#define VVCAM_RST_GET_SHARED(dev, rst_handle, rst_name) do { \
	rst_handle = devm_reset_control_get_shared(dev, rst_name); \
	if (IS_ERR_OR_NULL(rst_handle)) { \
		dev_err(dev, "failed to get isp reset handle %s\n", rst_name); \
		return -EFAULT; \
	}} while(0)

#define VVCAM_RST_GET_OPTIONAL(dev, rst_handle, rst_name) do { \
	rst_handle = devm_reset_control_get_optional(dev, rst_name); \
	if (IS_ERR_OR_NULL(rst_handle)) { \
		dev_err(dev, "failed to get isp reset handle %s\n", rst_name); \
		return -EFAULT; \
	}} while(0)

#define VVCAM_CLK_GET_HANDLE(dev, clk_handle, clk_name) do { \
	clk_handle = devm_clk_get(dev, clk_name); \
	if (IS_ERR(clk_handle)) { \
		ret = PTR_ERR(clk_handle); \
		dev_err(dev, "failed to get dw %s: %d\n", clk_name, ret); \
		return ret; \
	}} while(0)

#define VVCAM_SYS_CLK_PREPARE(clk) do { \
	ret = clk_prepare_enable(clk); \
	if (ret) { \
		pr_err("failed to enable clk %px\n", clk); \
		return ret; \
	}} while(0)

static int vvcam_sys_reset_init(struct platform_device *pdev, isp_clk_rst_t *isp_crg)
{
	VVCAM_RST_GET_SHARED(&pdev->dev, isp_crg->rstc_axi, "axi");
	VVCAM_RST_GET_SHARED(&pdev->dev, isp_crg->rstc_cfg, "cfg");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_dvp, "dvp");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_dvp, "isp0");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_dvp, "isp1");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht0, "sht0");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht1, "sht1");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht2, "sht2");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht3, "sht3");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht4, "sht4");
	VVCAM_RST_GET_OPTIONAL(&pdev->dev, isp_crg->rstc_sht5, "sht5");

	return 0;
}

static int vvcam_sys_clk_init(struct platform_device *pdev, isp_clk_rst_t *isp_crg)
{
	int ret;
	struct device *dev = &pdev->dev;

	VVCAM_CLK_GET_HANDLE(dev, isp_crg->aclk, "aclk");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->cfg_clk, "cfg_clk");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->isp_aclk, "isp_aclk");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->dvp_clk, "dvp_clk");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->phy_cfg, "phy_cfg");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->phy_escclk, "phy_escclk");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht0, "sht0");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht1, "sht1");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht2, "sht2");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht3, "sht3");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht4, "sht4");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->sht4, "sht5");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->aclk_mux, "aclk_mux");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->dvp_mux, "dvp_mux");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->isp_mux, "isp_mux");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->spll0_fout1, "spll0_fout1");
	VVCAM_CLK_GET_HANDLE(dev, isp_crg->vpll_fout1, "vpll_fout1");

	return 0;
}

static int vvcam_sys_reset_release(isp_clk_rst_t *isp_crg)
{
	int ret;

	ret = reset_control_deassert(isp_crg->rstc_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(isp_crg->rstc_axi);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_isp);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_dvp);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht0);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht1);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht2);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht3);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht4);
	WARN_ON(0 != ret);

	ret = reset_control_reset(isp_crg->rstc_sht5);
	WARN_ON(0 != ret);

	return 0;
}

static int vvcam_sys_clk_prepare(isp_clk_rst_t *isp_crg)
{
	int ret;
	long rate;

	ret = clk_set_parent(isp_crg->aclk_mux, isp_crg->spll0_fout1);
	if (ret < 0) {
		pr_err("ISP: failed to set aclk_mux parent: %d\n", ret);
		return ret;
	}

	ret = clk_set_parent(isp_crg->dvp_mux, isp_crg->vpll_fout1);
	if (ret < 0) {
		pr_err("ISP: failed to set dvp_mux parent: %d\n", ret);
		return ret;
	}

	ret = clk_set_parent(isp_crg->isp_mux, isp_crg->vpll_fout1);
	if (ret < 0) {
		pr_err("ISP: failed to set isp_mux parent: %d\n", ret);
		return ret;
	}

	rate = clk_round_rate(isp_crg->aclk, VVCAM_AXI_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(isp_crg->aclk, rate);
		if (ret) {
			pr_err("ISP: failed to set aclk: %d\n", ret);
			return ret;
		}
		pr_info("ISP set aclk to %ldHZ\n", rate);
	}

	rate = clk_round_rate(isp_crg->dvp_clk, VVCAM_DVP_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(isp_crg->dvp_clk, rate);
		if (ret) {
			pr_err("ISP: failed to set dvp_clk: %d\n", ret);
			return ret;
		}
		pr_info("ISP set dvp_clk to %ldHZ\n", rate);
	}

	rate = clk_round_rate(isp_crg->isp_aclk, VVCAM_ISP_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(isp_crg->isp_aclk, rate);
		if (ret) {
			pr_err("ISP: failed to set isp_aclk: %d\n", ret);
			return ret;
		}
		pr_info("ISP set vi_dig_isp clk to %ldHZ\n", rate);
	}

	VVCAM_SYS_CLK_PREPARE(isp_crg->aclk);
	VVCAM_SYS_CLK_PREPARE(isp_crg->dvp_clk);
	VVCAM_SYS_CLK_PREPARE(isp_crg->isp_aclk);
	VVCAM_SYS_CLK_PREPARE(isp_crg->cfg_clk);
	VVCAM_SYS_CLK_PREPARE(isp_crg->phy_cfg);
	VVCAM_SYS_CLK_PREPARE(isp_crg->phy_escclk);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht0);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht1);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht2);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht3);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht4);
	VVCAM_SYS_CLK_PREPARE(isp_crg->sht5);

	return 0;
}

static int vvcam_clk_reset_fini(isp_clk_rst_t *isp_crg)
{
	reset_control_assert(isp_crg->rstc_sht0);
	reset_control_assert(isp_crg->rstc_sht1);
	reset_control_assert(isp_crg->rstc_sht2);
	reset_control_assert(isp_crg->rstc_sht3);
	reset_control_assert(isp_crg->rstc_sht4);
	reset_control_assert(isp_crg->rstc_sht5);
	reset_control_assert(isp_crg->rstc_isp);
	reset_control_assert(isp_crg->rstc_cfg);
	reset_control_assert(isp_crg->rstc_axi);

	clk_disable_unprepare(isp_crg->sht0);
	clk_disable_unprepare(isp_crg->sht1);
	clk_disable_unprepare(isp_crg->sht2);
	clk_disable_unprepare(isp_crg->sht3);
	clk_disable_unprepare(isp_crg->sht4);
	clk_disable_unprepare(isp_crg->sht5);
	clk_disable_unprepare(isp_crg->phy_escclk);
	clk_disable_unprepare(isp_crg->phy_cfg);
	clk_disable_unprepare(isp_crg->dvp_clk);
	clk_disable_unprepare(isp_crg->isp_aclk);
	clk_disable_unprepare(isp_crg->cfg_clk);
	clk_disable_unprepare(isp_crg->aclk);

	return 0;
}

static int vvcam_soc_probe(struct platform_device *pdev)
{
	int ret = 0, i, j;
	uint32_t vi_top_phy_mode;
	struct vvcam_soc_driver_dev *pdriver_dev;
	struct vvcam_soc_dev *psoc_dev;

    #ifdef VI_SMMU_ENABLE
	uint32_t reg_mmu_isp0 = 0;
	uint32_t reg_mmu_isp1 = 0;
	uint32_t reg_mmu_dw200 = 0;
	uint32_t reg_mmu_dvp = 0;
    #endif
	struct resource         *mem;
	char debug_filename[64] = "soc_reg_dump";
	char debug_soc_reset[64] = "soc_reset";

	pr_info("enter %s\n", __func__);

	if (device_property_read_u32(&pdev->dev, "id", &pdev->id)) {
		dev_err(&pdev->dev, "Can not read device id!\n");
		return -EINVAL;
	}

	if (pdev->id >= VIVCAM_SOC_MAXCNT) {
		pr_err("%s:pdev id is %d error\n", __func__, pdev->id);
		return -EINVAL;
	}

	pdriver_dev =
	    devm_kzalloc(&pdev->dev, sizeof(struct vvcam_soc_driver_dev),
			 GFP_KERNEL);
	if (pdriver_dev == NULL) {
		pr_err("%s:alloc struct vvcam_soc_driver_dev error\n",
		       __func__);
		return -ENOMEM;
	}

	psoc_dev =
	    devm_kzalloc(&pdev->dev, sizeof(struct vvcam_soc_dev), GFP_KERNEL);
	if (psoc_dev == NULL) {
		pr_err("%s:alloc struct vvcam_soc_dev error\n", __func__);
		return -ENOMEM;
	}

	ret = vvcam_sys_reset_init(pdev, &pdriver_dev->isp_crg);
	if (ret) {
		pr_err("%s: isp reset init failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_clk_init(pdev, &pdriver_dev->isp_crg);
	if (ret) {
		pr_err("%s: isp clk init failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_clk_prepare(&pdriver_dev->isp_crg);
	if (ret) {
		pr_err("%s: isp clk prepare failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_reset_release(&pdriver_dev->isp_crg);
	if (ret) {
		pr_err("%s: isp reset release failed\n", __func__);
		return ret;
	}

	ret = win2030_tbu_power(&pdev->dev, true);
	if (ret) {
		pr_err("%s: isp tbu power up failed\n", __func__);
		return ret;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	psoc_dev->base = devm_ioremap_resource(&pdev->dev, mem);
	/*
	psoc_dev->base = ioremap(VVCTRL_BASE, VVCTRL_SIZE);
	if (IS_ERR(psoc_dev->base))
		return PTR_ERR(psoc_dev->base);
*/
	psoc_dev->scu_base = ioremap(SCU_BASE, SCU_SIZE);
	if (IS_ERR(psoc_dev->scu_base))
		return PTR_ERR(psoc_dev->scu_base);

	init_waitqueue_head(&psoc_dev->irq_wait);
	for (i = 0; i < VI_DVP2AXI_DVP_CHNS; i++) {
		for (j = 0; j < VI_DVP2AXI_VIRTUAL_CHNS; j++)
			atomic_set(&psoc_dev->irqc[i][j], 0);
	}

	for (i = 0; i < VI_DVP2AXI_ERRIRQ_NUM; i++)
		atomic_set(&psoc_dev->dvp2axi_errirq[i], 0);

	atomic_set(&psoc_dev->irqc_err, 0);
	pdriver_dev->private = psoc_dev;
	pdriver_dev->pdev = pdev;
	mutex_init(&pdriver_dev->vvmutex);
	platform_set_drvdata(pdev, pdriver_dev);

	vvnative_soc_init(psoc_dev);

	if (devise_register_index == 0) {
		if (vvcam_soc_major == 0) {
			ret =
			    alloc_chrdev_region(&pdriver_dev->devt, 0,
						VIVCAM_SOC_MAXCNT,
						VIVCAM_SOC_NAME);
			if (ret != 0) {
				pr_err("%s:alloc_chrdev_region error\n",
				       __func__);
				return ret;
			}
			vvcam_soc_major = MAJOR(pdriver_dev->devt);
			vvcam_soc_minor = MINOR(pdriver_dev->devt);
		} else {
			pdriver_dev->devt =
			    MKDEV(vvcam_soc_major, vvcam_soc_minor);
			ret =
			    register_chrdev_region(pdriver_dev->devt,
						   VIVCAM_SOC_MAXCNT,
						   VIVCAM_SOC_NAME);
			if (ret) {
				pr_err("%s:register_chrdev_region error\n",
				       __func__);
				return ret;
			}
		}
// #if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
		vvcam_soc_class = class_create(VIVCAM_SOC_NAME);
// #else
// 		vvcam_soc_class = class_create(THIS_MODULE, VIVCAM_SOC_NAME);
// #endif

		if (IS_ERR(vvcam_soc_class)) {
			pr_err("%s[%d]:class_create error!\n", __func__,
			       __LINE__);
			return -EINVAL;
		}
	}
	pdriver_dev->devt = MKDEV(vvcam_soc_major, vvcam_soc_minor + pdev->id); //将主设备号，次设备号转换为dev_t类型

	cdev_init(&pdriver_dev->cdev, &vvcam_soc_fops);
	ret = cdev_add(&pdriver_dev->cdev, pdriver_dev->devt, 1);
	if (ret) {
		pr_err("%s[%d]:cdev_add error!\n", __func__, __LINE__);
		return ret;
	}
	pdriver_dev->class = vvcam_soc_class;
	device_create(pdriver_dev->class, NULL, pdriver_dev->devt,
		      pdriver_dev, "%s%d", VIVCAM_SOC_NAME, pdev->id);

	devise_register_index++;

    #ifndef M2_REGISTER_MAP //M1_REGISTER_MAP
		/*hardware reset the isp*/
		__raw_writel(0x0, psoc_dev->scu_base + 0x0c);
		__raw_writel(0x1, psoc_dev->scu_base + 0x0c);

		/*
		* IMX327摄像头在hdr模式下需要配置此开关, 可配置的值包括 0x03(sensor 12bit width) 0x01(sensor 10bit width)
		* 在M1平台offset为0x14，M2平台为vi_common_top phy_connect_mode寄存器的bit4～5
		*/
		//__raw_writel(0x03, psoc_dev->scu_base + 0x14);

    #endif

	ret = vvcam_soc_irq_init(pdev, psoc_dev);
	if (ret < 0)
		return ret;

	vi_top_phy_mode = phy_mode << PHY_MODE;/* 2: 4lanes,  5: 2lanes*/
	if(0 != imx_hdr){
		for (i = 0; i <= 5; i++){
			if(imx_hdr & (0x01<<i) ){
				vi_top_phy_mode |= 1 << (IMX327_HDR_EN_0 + i*2);
				vi_top_phy_mode |= 1 << (IMX327_HDR_RAW12_0 + i*2);
			}
		}
	}
	pr_err(" %s, vi_top_phy_mode:%x \n", __func__,vi_top_phy_mode);
	psoc_dev->soc_access.write(psoc_dev, VI_REG_PHY_CONNECT_MODE,vi_top_phy_mode);

	psoc_dev->soc_access.write(psoc_dev, 0x04, 0x0);  /*csix_vicapx_sel: all from MIPI-CSI*/
	//psoc_dev->soc_access.write(psoc_dev, 0x04, 0x3f);  /*csix_vicapx_sel: all from vicap*/
#ifdef MCM_DEBUG
	/* isp0_dvp0_sel -> csi0, isp0_dvp1_sel -> csi1, isp1_dvp0_sel -> csi0, isp1_dvp1_sel -> csi1 */
	psoc_dev->soc_access.write(psoc_dev, 0x08, (0 << 0) | (1 << 3) | (0 << 6) | (1 << 9));
#else
	/* isp0_dvp0_sel -> csi0, isp0_dvp1_sel -> csi0, isp1_dvp0_sel -> csi1, isp1_dvp1_sel -> csi1 */
 	psoc_dev->soc_access.write(psoc_dev, 0x08, (1 << 0) | (0 << 3) | (1 << 6) | (1 << 9));
#endif
	psoc_dev->soc_access.write(psoc_dev, 0x40, 0xffff);      /*ctrl0,isp0,isp1,isp0_dvp0,isp0_dvp1,isp1_dvp0,isp1_dvp1 clk en*/
	psoc_dev->soc_access.write(psoc_dev, 0x1c, (0xe0 << 8) | 0xe0);
	psoc_dev->soc_access.write(psoc_dev, 0x20, (4 << 9));
	psoc_dev->soc_access.write(psoc_dev, 0x24, (4 << 9));
	psoc_dev->soc_access.write(psoc_dev, 0x28, (4 << 9));
	psoc_dev->soc_access.write(psoc_dev, 0x2c, (4 << 9));

	psoc_dev->soc_debug_dc = debugfs_create_file(debug_filename, 0644, NULL, psoc_dev, &soc_debug_fops);
	psoc_dev->soc_reset = debugfs_create_file(debug_soc_reset, 0644, NULL, psoc_dev, &soc_reset_fops);

	pr_info("exit %s\n", __func__);
	return ret;
}

static int vvcam_soc_remove(struct platform_device *pdev)
{
	int ret;
	struct vvcam_soc_driver_dev *pdriver_dev;
	struct vvcam_soc_dev *psoc_dev;

	pr_info("enter %s\n", __func__);
	devise_register_index--;
	pdriver_dev = platform_get_drvdata(pdev);

	psoc_dev = pdriver_dev->private;
	//iounmap(psoc_dev->base);
	iounmap(psoc_dev->scu_base);
    debugfs_remove(psoc_dev->soc_debug_dc);
    debugfs_remove(psoc_dev->soc_reset);

	cdev_del(&pdriver_dev->cdev);
	device_destroy(pdriver_dev->class, pdriver_dev->devt);
	unregister_chrdev_region(pdriver_dev->devt, VIVCAM_SOC_MAXCNT);
	if (devise_register_index == 0) {
		class_destroy(pdriver_dev->class);
	}

	ret = win2030_tbu_power(&pdev->dev, false);
	if (ret) {
		pr_err("isp tbu power down failed\n");
		return -1;
	}
	vvcam_clk_reset_fini(&pdriver_dev->isp_crg);

	return 0;
}

static const struct of_device_id visoc_of_match[] = {
	{ .compatible = "esw,vi-common-csr" },
	{},
};

MODULE_DEVICE_TABLE(of, visoc_of_match);

static struct platform_driver vvcam_soc_driver = {
	.probe = vvcam_soc_probe,
	.remove = vvcam_soc_remove,
	.driver = {
		   .name = VIVCAM_SOC_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = visoc_of_match,
	}
};

static int __init vvcam_soc_init_module(void)
{
	int ret = 0;

	pr_info("enter %s\n", __func__);

	ret = platform_driver_register(&vvcam_soc_driver);
	if (ret) {
		pr_err("register platform driver failed.\n");
		return ret;
	}

	return ret;
}

static void __exit vvcam_soc_exit_module(void)
{
	pr_info("enter %s\n", __func__);

	platform_driver_unregister(&vvcam_soc_driver);
}

late_initcall(vvcam_soc_init_module);
module_exit(vvcam_soc_exit_module);

MODULE_DESCRIPTION("ISP");
MODULE_LICENSE("GPL");
