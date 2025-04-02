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
#ifndef _VIVSOC_HUB_H_
#define _VIVSOC_HUB_H_

#include "soc_ioctl.h"

int vivsoc_hub_isp_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl);
int vivsoc_hub_isp_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_isp_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_isp_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_isp_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);

int vivsoc_hub_dwe_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl);
int vivsoc_hub_dwe_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_dwe_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_dwe_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_dwe_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);

int vivsoc_hub_vse_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl);
int vivsoc_hub_vse_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_vse_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_vse_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_vse_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);

int vivsoc_hub_csi_reset(struct vvcam_soc_dev *dev,
			 struct soc_control_context *soc_ctrl);
int vivsoc_hub_csi_s_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_csi_g_power_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_csi_s_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);
int vivsoc_hub_csi_g_clock_ctrl(struct vvcam_soc_dev *dev,
				struct soc_control_context *soc_ctrl);

int vivsoc_hub_sensor_reset(struct vvcam_soc_dev *dev,
			    struct soc_control_context *soc_ctrl);
int vivsoc_hub_sensor_s_power_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl);
int vivsoc_hub_sensor_g_power_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl);
int vivsoc_hub_sensor_s_clock_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl);
int vivsoc_hub_sensor_g_clock_ctrl(struct vvcam_soc_dev *dev,
				   struct soc_control_context *soc_ctrl);


int vivsoc_hub_s_multi2isp_imgsize(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl);
int vivsoc_hub_g_multi2isp_imgsize(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl);

int vivsoc_hub_s_imx327_hdr(struct vvcam_soc_dev *dev,
					struct soc_control_context *soc_ctrl);

int vivsoc_hub_write_part(struct vvcam_soc_dev *dev,
		    uint32_t address, uint32_t data, uint8_t shift, uint8_t width);
int vivsoc_hub_s_reset_dvp2axi(struct vvcam_soc_driver_dev *pdriver_dev, struct vvcam_soc_dev *dev);
int vivsoc_hub_s_dvp2axi_iodvp_sel(struct vvcam_soc_dev *dev,
		    struct soc_control_context *soc_ctrl);
int vivsoc_hub_s_dvp2axi_chn_attr(struct vvcam_soc_dev *dev,
		    struct dvp2axi_chn_attr *chn_attr);
int vivsoc_hub_s_dvp2axi_emd_chn_attr(struct vvcam_soc_dev *dev,
		    struct dvp2axi_emd_chn_attr *chn_attr);
int vivsoc_hub_s_dvp2axi_iova_get(struct visoc_pdata *pdata,
		    struct dvp2axi_dmabuf_attr *dmabuf_attr);
int vivsoc_hub_s_dvp2axi_iova_put(struct visoc_pdata *pdata, int dmabuf_fd);
int vivsoc_hub_s_phy_mode(struct vvcam_soc_dev *dev,int phy_mode);
int vivsoc_hub_s_controller_sel(struct vvcam_soc_dev *dev,
			struct soc_control_context *soc_ctrl);
int vivsoc_hub_s_isp_dvp_sel(struct vvcam_soc_dev *dev,
			struct isp_dvp_attr *dvp_attr);


unsigned int vivsoc_register_hardware(struct vvcam_soc_dev *dev,
				      struct vvcam_soc_function_s *func);

#endif /* _VIVSOC_HUB_H_ */
