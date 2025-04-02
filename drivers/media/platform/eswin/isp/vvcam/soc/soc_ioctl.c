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

#ifndef __KERNEL__
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define pr_info printf
#define pr_err printf
#define copy_from_user(a, b, c) soc_copy_data(a, b, c)
#define copy_to_user(a, b, c) soc_copy_data(a, b, c)
#define __user
#define __iomem

void soc_copy_data(void *dst, void *src, int size)
{
	if (dst != src)
		memcpy(dst, src, size);
}

#else // __KERNEL__
#include <linux/module.h>	/* Module support */
#include <linux/uaccess.h>

#endif

#include "soc_ioctl.h"
#include "vivsoc_hub.h"

#define CHECK_COPY_RETVAL(retval) do { \
	if (retval < 0) { \
		pr_err("%s %d: copy from or to user failed\n", __func__, __LINE__); \
		return -EFAULT; \
	} \
} while(0);

long soc_priv_ioctl(struct visoc_pdata *pdata, unsigned int cmd,
		    void __user *args)
{
	int ret = -1, retval;
	struct soc_control_context soc_ctrl;
	struct vvcam_soc_dev *dev;
	struct vvcam_soc_driver_dev *pdriver_dev;

	pdriver_dev = pdata->pdrv_dev;
	dev = pdriver_dev->private;

	if (!dev) {
		return ret;
	}

	switch (cmd) {
		/* ISP part */
	case VVSOC_IOC_S_RESET_ISP:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_reset(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_POWER_ISP:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_s_power_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_POWER_ISP:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_g_power_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_CLOCK_ISP:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_s_clock_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_CLOCK_ISP:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_g_clock_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

		/* DWE part */
	case VVSOC_IOC_S_RESET_DWE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_dwe_reset(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_POWER_DWE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_dwe_s_power_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_POWER_DWE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_dwe_g_power_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_CLOCK_DWE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_dwe_s_clock_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_CLOCK_DWE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_isp_g_clock_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

		/* VSE part */
	case VVSOC_IOC_S_RESET_VSE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_vse_reset(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_POWER_VSE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_vse_s_power_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_POWER_VSE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_vse_g_power_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_CLOCK_VSE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_vse_s_clock_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_CLOCK_VSE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_vse_g_clock_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

		/* CSI part */
	case VVSOC_IOC_S_RESET_CSI:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_csi_reset(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_POWER_CSI:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_csi_s_power_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_POWER_CSI:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_csi_g_power_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_CLOCK_CSI:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_csi_s_clock_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_CLOCK_CSI:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_csi_g_clock_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

		/* sensor part */
	case VVSOC_IOC_S_RESET_SENSOR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_sensor_reset(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_POWER_SENSOR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_sensor_s_power_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_POWER_SENSOR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_sensor_g_power_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_CLOCK_SENSOR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_sensor_s_clock_ctrl(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_G_CLOCK_SENSOR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_sensor_g_clock_ctrl(dev, &soc_ctrl);
		retval = copy_to_user(args, &soc_ctrl, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		break;

	/* dvp part*/
	case VVSOC_IOC_S_RESET_DVP:
	case VVSOC_IOC_S_POWER_DVP:
	case VVSOC_IOC_G_POWER_DVP:
	case VVSOC_IOC_S_CLOCK_DVP:
	case VVSOC_IOC_G_CLOCK_DVP:
		ret = 0;
		break;

	/* vi top multi2isp */
	case VVSOC_IOC_S_MULTI2ISP_SIZE:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_multi2isp_imgsize(dev, &soc_ctrl);
		break;

	/* vi top multi2isp */
	case VVSOC_IOC_S_IMX327_HDR:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_imx327_hdr(dev, &soc_ctrl);
		break;

	/* dvp2axi */
	case VVSOC_IOC_S_DVP2AXI_RESET:
		ret = vivsoc_hub_s_reset_dvp2axi(pdriver_dev, dev);
		break;

	case VVSOC_IOC_S_DVP2AXI_IODVP_SEL:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		vivsoc_hub_s_dvp2axi_iodvp_sel(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_DVP2AXI_CHN_ATTR:
		struct dvp2axi_chn_attr chn_attr;
		retval = copy_from_user(&chn_attr, args, sizeof(chn_attr));
		CHECK_COPY_RETVAL(retval);
		pdata->dvp_idx = chn_attr.device_idx;
		ret = vivsoc_hub_s_dvp2axi_chn_attr(dev, &chn_attr);
		break;

	case VVSOC_IOC_S_DVP2AXI_EMD_ATTR:
		struct dvp2axi_emd_chn_attr emd_chn_attr;
		retval = copy_from_user(&emd_chn_attr, args, sizeof(emd_chn_attr));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_dvp2axi_emd_chn_attr(dev, &emd_chn_attr);
		break;

	case VVSOC_IOC_S_DVP2AXI_IOVA_GET:
		struct dvp2axi_dmabuf_attr dmabuf_attr;
		retval = copy_from_user(&dmabuf_attr, args, sizeof(dmabuf_attr));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_dvp2axi_iova_get(pdata, &dmabuf_attr);
		retval = copy_to_user(args, &dmabuf_attr, sizeof(dmabuf_attr));
		CHECK_COPY_RETVAL(retval);
		break;

	case VVSOC_IOC_S_DVP2AXI_IOVA_PUT:
		int dmabuf_fd;
		retval = copy_from_user(&dmabuf_fd, args, sizeof(dmabuf_fd));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_dvp2axi_iova_put(pdata, dmabuf_fd);
		break;

	case VVSOC_IOC_S_PHY_MODE:
		int phy_mode = 0;
		retval = copy_from_user(&phy_mode, args, sizeof(phy_mode));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_phy_mode(dev, phy_mode);
		break;

	case VVSOC_IOC_S_CONTROLLER_SEL:
		retval = copy_from_user(&soc_ctrl, args, sizeof(soc_ctrl));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_controller_sel(dev, &soc_ctrl);
		break;

	case VVSOC_IOC_S_ISP_DVP_SEL:
		struct isp_dvp_attr dvp_attr;
		retval = copy_from_user(&dvp_attr, args, sizeof(dvp_attr));
		CHECK_COPY_RETVAL(retval);
		ret = vivsoc_hub_s_isp_dvp_sel(dev, &dvp_attr);
		break;

	default:
		pr_err("unsupported command %d", cmd);
		break;
	}

	return ret;
}

extern struct vvcam_soc_function_s gen6_soc_function;

int vvnative_soc_init(struct vvcam_soc_dev *dev)
{
	if (dev == NULL) {
		pr_err("[%s] dev is NULL\n", __func__);
		return -1;
	}

	vivsoc_register_hardware(dev, &gen6_soc_function);
	return 0;
}

int vvnative_soc_deinit(struct vvcam_soc_dev *dev)
{
	return 0;
}
