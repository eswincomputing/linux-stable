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
#ifdef ISP8000L_V2008
#include <linux/io.h>		//Fix thead compile error.
#endif

#include <linux/io.h> 
#include "soc_ioctl.h"
#include "vsi_core_gen6.h"

int gen6_write_reg(void *dev, unsigned int addr, unsigned int val)
{
	struct vvcam_soc_dev *soc_dev;

	soc_dev = (struct vvcam_soc_dev *)dev;

	if (soc_dev == NULL)
		return -1;

	__raw_writel(val,soc_dev->base + addr);
    
	return 0;
}

int gen6_read_reg(void *dev, unsigned int addr, unsigned int *val)
{
	struct vvcam_soc_dev *soc_dev;

	soc_dev = (struct vvcam_soc_dev *)dev;

	if (soc_dev == NULL)
		return -1;

	*val = __raw_readl(soc_dev->base + addr);

	return 0;
}

static int gen6_set_isp_power(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_get_isp_power(void *dev, unsigned int id, unsigned int *status)
{
	return 0;
}

static int gen6_set_isp_reset(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_set_isp_clk(void *dev, unsigned int id, unsigned int freq)
{
	return 0;
}

static int gen6_get_isp_clk(void *dev, unsigned int id, unsigned int *freq)
{
	return 0;
}

static int gen6_set_dwe_power(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_get_dwe_power(void *dev, unsigned int id, unsigned int *status)
{
	return 0;
}

static int gen6_set_dwe_reset(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_set_dwe_clk(void *dev, unsigned int id, unsigned int freq)
{
	return 0;
}

static int gen6_get_dwe_clk(void *dev, unsigned int id, unsigned int *freq)
{
	return 0;
}

static int gen6_set_vse_power(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_get_vse_power(void *dev, unsigned int id, unsigned int *status)
{
	return 0;
}

static int gen6_set_vse_reset(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_set_vse_clk(void *dev, unsigned int id, unsigned int freq)
{
	return 0;
}

static int gen6_get_vse_clk(void *dev, unsigned int id, unsigned int *freq)
{
	return 0;
}

static int gen6_set_csi_power(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_get_csi_power(void *dev, unsigned int id, unsigned int *status)
{
	return 0;
}

static int gen6_set_csi_reset(void *dev, unsigned int id, unsigned int status)
{
	return 0;
}

static int gen6_set_csi_clk(void *dev, unsigned int id, unsigned int freq)
{
	return 0;
}

static int gen6_get_csi_clk(void *dev, unsigned int id, unsigned int *freq)
{
	return 0;
}

static int gen6_set_sensor_power(void *dev, unsigned int id,
				 unsigned int status)
{
	return 0;
}

static int gen6_get_sensor_power(void *dev, unsigned int id,
				 unsigned int *status)
{
	return 0;
}

static int gen6_set_sensor_reset(void *dev, unsigned int id,
				 unsigned int status)
{
	return 0;
}

static int gen6_set_sensor_clk(void *dev, unsigned int id, unsigned int freq)
{
	return 0;
}

static int gen6_get_sensor_clk(void *dev, unsigned int id, unsigned int *freq)
{
	return 0;
}

struct vvcam_soc_function_s gen6_soc_function = {
	.isp_func.set_power = gen6_set_isp_power,
	.isp_func.get_power = gen6_get_isp_power,
	.isp_func.set_reset = gen6_set_isp_reset,
	.isp_func.set_clk = gen6_set_isp_clk,
	.isp_func.get_clk = gen6_get_isp_clk,

	.dwe_func.set_power = gen6_set_dwe_power,
	.dwe_func.get_power = gen6_get_dwe_power,
	.dwe_func.set_reset = gen6_set_dwe_reset,
	.dwe_func.set_clk = gen6_set_dwe_clk,
	.dwe_func.get_clk = gen6_get_dwe_clk,

	.vse_func.set_power = gen6_set_vse_power,
	.vse_func.get_power = gen6_get_vse_power,
	.vse_func.set_reset = gen6_set_vse_reset,
	.vse_func.set_clk = gen6_set_vse_clk,
	.vse_func.get_clk = gen6_get_vse_clk,

	.csi_func.set_power = gen6_set_csi_power,
	.csi_func.get_power = gen6_get_csi_power,
	.csi_func.set_reset = gen6_set_csi_reset,
	.csi_func.set_clk = gen6_set_csi_clk,
	.csi_func.get_clk = gen6_get_csi_clk,

	.sensor_func.set_power = gen6_set_sensor_power,
	.sensor_func.get_power = gen6_get_sensor_power,
	.sensor_func.set_reset = gen6_set_sensor_reset,
	.sensor_func.set_clk = gen6_set_sensor_clk,
	.sensor_func.get_clk = gen6_get_sensor_clk,
};
