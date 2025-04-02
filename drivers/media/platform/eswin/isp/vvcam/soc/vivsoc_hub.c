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
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include "soc_ioctl.h"
#include "vivsoc_hub.h"
#include "vsi_core_gen6.h"

int vivsoc_hub_isp_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.isp_func.set_reset == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.isp_func.set_reset(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_isp_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.isp_func.set_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.isp_func.set_power(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_isp_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.isp_func.get_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.isp_func.get_power(dev, soc_ctrl->device_idx,
					     &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_isp_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.isp_func.set_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.isp_func.set_clk(dev, soc_ctrl->device_idx,
					   soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_isp_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.isp_func.get_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.isp_func.get_clk(dev, soc_ctrl->device_idx,
					   &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_dwe_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.dwe_func.set_reset == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.dwe_func.set_reset(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_dwe_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.dwe_func.set_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.dwe_func.set_power(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_dwe_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.dwe_func.get_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.dwe_func.get_power(dev, soc_ctrl->device_idx,
					     &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_dwe_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.dwe_func.set_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.dwe_func.set_clk(dev, soc_ctrl->device_idx,
					   soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_dwe_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.dwe_func.get_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.dwe_func.get_clk(dev, soc_ctrl->device_idx,
					   &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_vse_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.vse_func.set_reset == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.vse_func.set_reset(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_vse_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.vse_func.set_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.vse_func.set_power(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_vse_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.vse_func.get_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.vse_func.get_power(dev, soc_ctrl->device_idx,
					     &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_vse_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.vse_func.set_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.vse_func.set_clk(dev, soc_ctrl->device_idx,
					   soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_vse_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.vse_func.get_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.vse_func.get_clk(dev, soc_ctrl->device_idx,
					   &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_csi_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.csi_func.set_reset == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.csi_func.set_reset(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_csi_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.csi_func.set_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.csi_func.set_power(dev, soc_ctrl->device_idx,
					     soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_csi_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.csi_func.get_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.csi_func.get_power(dev, soc_ctrl->device_idx,
					     &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_csi_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.csi_func.set_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.csi_func.set_clk(dev, soc_ctrl->device_idx,
					   soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_csi_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.csi_func.get_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.csi_func.get_clk(dev, soc_ctrl->device_idx,
					   &soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_sensor_reset(struct vvcam_soc_dev *dev,
			    struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.sensor_func.set_reset == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.sensor_func.set_reset(dev, soc_ctrl->device_idx,
						soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_sensor_s_power_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.sensor_func.set_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.sensor_func.set_power(dev, soc_ctrl->device_idx,
						soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_sensor_g_power_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.sensor_func.get_power == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.sensor_func.get_power(dev, soc_ctrl->device_idx,
						&soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_sensor_s_clock_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.sensor_func.set_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.sensor_func.set_clk(dev, soc_ctrl->device_idx,
					      soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_sensor_g_clock_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	if (dev->soc_func.sensor_func.get_clk == NULL) {
		pr_err("[%s] function is null\n", __func__);
		return -1;
	}

	ret =
	    dev->soc_func.sensor_func.get_clk(dev, soc_ctrl->device_idx,
					      &soc_ctrl->control_value);

	return ret;
}

// OYX_TODO: choose reg base on the devId
int vivsoc_hub_s_multi2isp_imgsize(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	dev->soc_access.write(dev, VI_REG_ISP0_DVP0_SIZE, soc_ctrl->control_value);
	dev->soc_access.write(dev, VI_REG_ISP0_DVP1_SIZE, soc_ctrl->control_value);
	dev->soc_access.write(dev, VI_REG_ISP1_DVP0_SIZE, soc_ctrl->control_value);
	dev->soc_access.write(dev, VI_REG_ISP1_DVP1_SIZE, soc_ctrl->control_value);

	return ret;
}

int vivsoc_hub_write_part(struct vvcam_soc_dev *dev, uint32_t address, uint32_t data, uint8_t shift, uint8_t width)
{
	uint32_t temp = 0;
	uint32_t mask = (1 << width) - 1;

	dev->soc_access.read(dev, address, &temp);
//	pr_info("[%s]read 0x%x:0x%x\n", __func__, address, temp);

	temp &= ~(mask << shift);
	temp |= (data & mask) << shift;

	dev->soc_access.write(dev, address, temp);
//	pr_info("[%s]write 0x%x:0x%x\n", __func__, address, temp);

	return 0;
}

int vivsoc_hub_s_imx327_hdr(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	int sensor_index = 0;
	char hdr_en = 0;
	uint32_t width = 0;
	uint32_t shift = 0;

	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	sensor_index = soc_ctrl->device_idx;
	hdr_en = soc_ctrl->control_value &0x01;
	width = 1;

	shift = IMX327_HDR_EN_0 + sensor_index*2;
	vivsoc_hub_write_part(dev, VI_REG_PHY_CONNECT_MODE, hdr_en, shift, width);

	shift = IMX327_HDR_RAW12_0 + sensor_index*2;
	vivsoc_hub_write_part(dev, VI_REG_PHY_CONNECT_MODE, hdr_en, shift, width);

	return ret;
}

int vivsoc_hub_g_multi2isp_imgsize(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl)
{
	int ret = 0;
	if ((dev == NULL) || (soc_ctrl == NULL)) {
		pr_err("[%s]:dev is null\n", __func__);
		return -1;
	}

	dev->soc_access.read(dev, 0x0c, &(soc_ctrl->control_value));

	return ret;
}

int vivsoc_hub_s_reset_dvp2axi(struct vvcam_soc_driver_dev *pdriver_dev, struct vvcam_soc_dev *dev)
{
	int val, ret, rst_count = 0;

	/*
	 * Ensure no data from sensor before reseting dvp2axi
	 */

	dev->soc_access.read(dev, VI_DVP2AXI_CTRL0_CSR, &val);
	val &= ~VI_DVP2AXI_SOFT_RST_MASK;
	dev->soc_access.write(dev, VI_DVP2AXI_CTRL0_CSR, val);

	do {
		fsleep(10);
		rst_count++;
		dev->soc_access.read(dev, VI_DVP2AXI_CTRL0_CSR, &val);
		pr_info("... dvp2axi reseting ...\n");
		if (rst_count > 10) {
			pr_info("... dvp2axi crg reseting ...\n");
			ret = reset_control_reset(pdriver_dev->isp_crg.rstc_dvp);
			WARN_ON(0 != ret);

			if (rst_count > 20) {
				pr_err("dvp2axi reset failed, stop sensor first\n");
				return -1;
			}
		}
	} while((val & VI_DVP2AXI_RST_DONE_MASK) != VI_DVP2AXI_RST_DONE_MASK);

	dev->soc_access.read(dev, VI_DVP2AXI_CTRL0_CSR, &val);
	val |= VI_DVP2AXI_SOFT_RST_MASK;
	dev->soc_access.write(dev, VI_DVP2AXI_CTRL0_CSR, val);

	return 0;
}

int vivsoc_hub_s_dvp2axi_iodvp_sel(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl)
{
	if (soc_ctrl->control_value) {
		dev->dvp2axi_iodvp_en = 1;
	} else {
		dev->dvp2axi_iodvp_en = 0;
	}

	vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL1_CSR,
		    dev->dvp2axi_iodvp_en, IODVP_MUX_LSB, IODVP_MUX_BITS);

	return 0;
}

int vivsoc_hub_s_dvp2axi_chn_attr(struct vvcam_soc_dev *dev,
					struct dvp2axi_chn_attr *chn_attr)
{
	int i;

	dev->dvp2axi_framesize[chn_attr->device_idx] =
		    chn_attr->hs_num * chn_attr->vs_num * (chn_attr->pixel_width / 8);
	for (i = 0; i < VI_DVP2AXI_VIRTUAL_CHNS; i++) {
		atomic_set(&dev->dvp2axi_vchn_fidx[chn_attr->device_idx][i], 0);
		atomic_set(&dev->dvp2axi_vchn_didx[chn_attr->device_idx][i], 0);
		atomic_set(&dev->irqc[chn_attr->device_idx][i], 0);
	}

	// error irq mask disable
	atomic_set(&dev->dvp2axi_errirq[chn_attr->device_idx + 2], 0);
	atomic_set(&dev->dvp2axi_errirq[0], 0); // axi full
	atomic_set(&dev->dvp2axi_errirq[1], 0); // axi error
	atomic_set(&dev->dvp2axi_errirq[8], 0); // axi afull
	vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK2, 0, (chn_attr->device_idx + 2), 1);
	vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK2, 0, 0, 1); // axi full
	vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK2, 0, 1, 1); // axi error
	vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK2, 0, 8, 1); // axi afull

	/*
	 * The way of blocking data coming from sensor is to
	 * turn off the sensor, clearing dvp_chn_en is not working.
	 *
	 * Add flush idx and done idx to mask the related irq,
	 * otherwise a lot of irq generated even if dvp_chn_en cleared.
	 */
	switch(chn_attr->device_idx) {
	case 0:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL1_CSR,
			    chn_attr->pixel_width, DVP0_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL3_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL9_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL10_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL11_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL33_CSR,
			    chn_attr->hs_stride, DVP0_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP0_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP0_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP0_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP0_FLUSH_MASK_LSB, 3);
		break;
	case 1:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL1_CSR,
			    chn_attr->pixel_width, DVP1_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL4_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL12_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL13_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL14_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL33_CSR,
			    chn_attr->hs_stride, DVP1_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP1_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP1_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP1_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP1_FLUSH_MASK_LSB, 3);
		break;
	case 2:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL2_CSR,
			    chn_attr->pixel_width, DVP2_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL5_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL15_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL16_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL17_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL34_CSR,
			    chn_attr->hs_stride, DVP2_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP2_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP2_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP2_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK0, 0, DVP2_FLUSH_MASK_LSB, 3);
		break;
	case 3:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL2_CSR,
			    chn_attr->pixel_width, DVP3_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL6_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL18_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL19_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL20_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL34_CSR,
			    chn_attr->hs_stride, DVP3_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP3_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP3_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP3_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP3_FLUSH_MASK_LSB, 3);
		break;
	case 4:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL2_CSR,
			    chn_attr->pixel_width, DVP4_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL7_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL21_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL22_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL23_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL35_CSR,
			    chn_attr->hs_stride, DVP4_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP4_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP4_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP4_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP4_FLUSH_MASK_LSB, 3);
		break;
	case 5:
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL2_CSR,
			    chn_attr->pixel_width, DVP5_PIX_WIDTH_LSB, PIX_WIDTH_BITS);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL8_CSR, chn_attr->hs_num | (chn_attr->vs_num << 16));
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL24_CSR, chn_attr->chn0_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL25_CSR, chn_attr->chn1_baddr);
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL26_CSR, chn_attr->chn2_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL35_CSR,
			    chn_attr->hs_stride, DVP5_HS_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->first_id, DVP5_FIRST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL36_CSR,
			    chn_attr->last_id, DVP5_LAST_ID_LSB, HDR_ID_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP5_DONE_MASK_LSB, 3);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_INT_MASK1, 0, DVP5_FLUSH_MASK_LSB, 3);
		break;
	default:
		pr_err("Invalid dvp port\n");
		return -1;
	}

	if (chn_attr->enable)
		chn_attr->enable = 1;

	vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL0_CSR, chn_attr->enable, chn_attr->device_idx, 1);

	return 0;
}

int vivsoc_hub_s_dvp2axi_emd_chn_attr(struct vvcam_soc_dev *dev,
					struct dvp2axi_emd_chn_attr *chn_attr)
{
	switch(chn_attr->device_idx) {
	case 0:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL27_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL37_CSR,
			    chn_attr->emd_stride, DVP0_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL40_CSR,
			    chn_attr->emd_hsnum, DVP0_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	case 1:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL28_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL37_CSR,
			    chn_attr->emd_stride, DVP1_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL40_CSR,
			    chn_attr->emd_hsnum, DVP1_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	case 2:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL29_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL38_CSR,
			    chn_attr->emd_stride, DVP2_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL41_CSR,
			    chn_attr->emd_hsnum, DVP2_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	case 3:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL30_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL38_CSR,
			    chn_attr->emd_stride, DVP3_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL41_CSR,
			    chn_attr->emd_hsnum, DVP3_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	case 4:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL31_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL39_CSR,
			    chn_attr->emd_stride, DVP4_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL42_CSR,
			    chn_attr->emd_hsnum, DVP4_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	case 5:
		dev->soc_access.write(dev, VI_DVP2AXI_CTRL32_CSR, chn_attr->emd_baddr);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL39_CSR,
			    chn_attr->emd_stride, DVP5_EMD_STRIDE_LSB, STRIDE_BITS);
		vivsoc_hub_write_part(dev, VI_DVP2AXI_CTRL42_CSR,
			    chn_attr->emd_hsnum, DVP5_EMD_HSNUM_LSB, EMD_HSNUM_BITS);
		break;
	default:
		pr_err("Invalid dvp port\n");
		return -1;
	}

	return 0;
}

int vivsoc_hub_s_dvp2axi_iova_get(struct visoc_pdata *pdata,
					struct dvp2axi_dmabuf_attr *dmabuf_attr)
{

	size_t buf_size = 0;
	struct heap_mem *hmem = NULL;

	pr_info("import dmabuf_fd = %d\n", dmabuf_attr->dmabuf_fd);

	//hmem = common_dmabuf_heap_import_from_user(&pdata->root, dmabuf_attr->dmabuf_fd, false);
	if(IS_ERR(hmem)) {
		printk(KERN_ERR "dmabuf-heap alloc from userspace failed\n");
		return -ENOMEM;
	}

	buf_size = common_dmabuf_heap_get_size(hmem);
	pr_info("dmabuf info: CPU VA:0x%lx, PA:0x%lx, DMA addr(iova):0x%lx, size=0x%lx\n",
			(unsigned long)hmem->vaddr, (unsigned long)sg_phys(hmem->sgt->sgl), (unsigned long)sg_dma_address(hmem->sgt->sgl), (unsigned long)buf_size);

	dmabuf_attr->iova = (unsigned long)sg_dma_address(hmem->sgt->sgl);

	return 0;
}

int vivsoc_hub_s_dvp2axi_iova_put(struct visoc_pdata *pdata, int dmabuf_fd)
{
	struct heap_mem *hmem;

	pr_info("release dmabuf_fd = %d\n", dmabuf_fd);

	hmem = common_dmabuf_lookup_heapobj_by_fd(&pdata->root, dmabuf_fd);
	if (IS_ERR(hmem)) {
		pr_err("cannot find dmabuf-heap for dmabuf_fd %d\n", dmabuf_fd);
		return -ENOMEM;
	}

	common_dmabuf_heap_release(hmem);

	return 0;
}

int vivsoc_hub_s_phy_mode(struct vvcam_soc_dev *dev,int phy_mode)
{
	int shift = 0, width = 3;

	if (phy_mode < 0 || phy_mode > PHY_MODE_LANE_2_2_2_2_2_2) {
		pr_err("%s: Invalid phy mode\n", __func__);
		return -EINVAL;
	}

	vivsoc_hub_write_part(dev, VI_REG_PHY_CONNECT_MODE, phy_mode, shift, width);

	return 0;
}


int vivsoc_hub_s_controller_sel(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl)
{
	int width = 1;

	if (soc_ctrl->device_idx > 5) {
		pr_err("%s: Invalid device index\n", __func__);
		return -EINVAL;
	}

	if (soc_ctrl->control_value) {
		soc_ctrl->control_value = 1;
	}

	vivsoc_hub_write_part(dev, VI_REG_CONTROLLER_SEL,
		    soc_ctrl->control_value, soc_ctrl->device_idx, width);

	return 0;
}

int vivsoc_hub_s_isp_dvp_sel(struct vvcam_soc_dev *dev,
					struct isp_dvp_attr *dvp_attr)
{
	int shift = 0;
	if (dvp_attr->dvp_sel >= CONTROLLER_BUTT) {
		pr_err("%s: Invalid dvp controller type\n", __func__);
		return -EINVAL;
	}

	switch (dvp_attr->devId) {
		case 0:
			shift = ISP0_DVP0_SEL_LSB;
			break;
		case 1:
			shift = ISP0_DVP1_SEL_LSB;
			break;
		case 2:
			shift = ISP0_DVP2_SEL_LSB;
			break;
		case 3:
			shift = ISP0_DVP3_SEL_LSB;
			break;
		case 4:
			shift = ISP1_DVP0_SEL_LSB;
			break;
		case 5:
			shift = ISP1_DVP1_SEL_LSB;
			break;
		case 6:
			shift = ISP1_DVP2_SEL_LSB;
			break;
		case 7:
			shift = ISP1_DVP3_SEL_LSB;
			break;
		default:
			shift = 0;
			return -EINVAL;
	}
	vivsoc_hub_write_part(dev, VI_REG_ISP_DVP_SEL,
			dvp_attr->dvp_sel, shift, ISP_DVP_SEL_BITS);

	return 0;
}

unsigned int vivsoc_register_hardware(struct vvcam_soc_dev *dev,
				      struct vvcam_soc_function_s *func)
{
	if (func == NULL) {
		return -1;
	}

	memcpy(&dev->soc_func, func, sizeof(struct vvcam_soc_function_s));

	dev->soc_access.write = gen6_write_reg;
	dev->soc_access.read = gen6_read_reg;

	return 0;
}
