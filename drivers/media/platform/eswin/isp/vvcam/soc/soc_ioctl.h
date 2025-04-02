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
#ifndef _SOC_IOC_H_
#define _SOC_IOC_H_

#ifdef __KERNEL__
#include <linux/cdev.h>
#include <linux/dmabuf-heap-import-helper.h>
#include "vivsoc_dvp2axi.h"
#else
#include <stdint.h>
#endif

#include <linux/ioctl.h>

//vi top register,base address 0x5103_0000
#define VI_REG_PHY_CONNECT_MODE		0x0000
#define VI_REG_CONTROLLER_SEL		0x0004
#define VI_REG_ISP_DVP_SEL			0x0008
#define VI_REG_ISP0_DVP0_SIZE	 	0x000c
#define VI_REG_ISP0_DVP1_SIZE	 	0x0010
#define VI_REG_ISP1_DVP0_SIZE	 	0x0014
#define VI_REG_ISP1_DVP1_SIZE	 	0x0018
#define VI_REG_MULTI2ISP_BLANK	 	0x001c
#define VI_REG_MULTI2ISP0_DVP0	 	0x0020
#define VI_REG_MULTI2ISP0_DVP1	 	0x0024
#define VI_REG_MULTI2ISP1_DVP0	 	0x0028
#define VI_REG_MULTI2ISP1_DVP1	 	0x002c

//VI_REG_PHY_CONNECT_MODE shift
#define IMX327_HDR_RAW12_5			15
#define IMX327_HDR_EN_5				14
#define IMX327_HDR_RAW12_4			13
#define IMX327_HDR_EN_4				12
#define IMX327_HDR_RAW12_3			11
#define IMX327_HDR_EN_3				10
#define IMX327_HDR_RAW12_2			9
#define IMX327_HDR_EN_2				8
#define IMX327_HDR_RAW12_1			7
#define IMX327_HDR_EN_1				6
#define IMX327_HDR_RAW12_0			5
#define IMX327_HDR_EN_0				4
#define MIPI_CSI_TPG_CLOCK_MUX_EN 	3
#define PHY_MODE					0
/*  PHY_MODE bit0-2:
phy connection mode settings , one phy for 2lanes:
0: phy0,1,2,3 for 8lanes one sensor, phy4,5 for 4lanes one sensor
1: phy0,1,2,3 for 8lanes sensor, phy4 for 2lanes sensor, phy5 for 2lanes sensor
2: phy0,1 for 4lanes sensor, phy2,3 for 4lanes sensor, phy4,5 for 4lanes sensor
3: phy0,1 for 4lanes senor, phy2,3 for 4lanes sensor, phy4 for 2lanes sensor, phy5 for 2lanes sensor
4: phy0,1 for 4lanes sensor, phy2 for 2lanes sensor, phy3 for 2lanes sensor, phy4 for 2lanes sensor, phy5 for 2lanes sensor
5. phy0 for 2lanes sensor, phy1 for 2lanes sensor, phy2 for 2lanes sensor phy3 for 2lanes sensor, phy4 for 2lanes sensor phy5 for 2lanes sensor
[note] 8lanes just for sub lvds/slvs/hispi, mipi csi can't support 8lanes
*/
#define PHY_MODE_LANE_8_4					0
#define PHY_MODE_LANE_8_2_2					1
#define PHY_MODE_LANE_4_4_4					2
#define PHY_MODE_LANE_4_4_2_2				3
#define PHY_MODE_LANE_4_2_2_2_2				4
#define PHY_MODE_LANE_2_2_2_2_2_2			5

//VI_REG_CONTROLLER_SEL 0:mipi 1:others
#define CSI5_VICAP5_SEL		5
#define CSI4_VICAP4_SEL		4
#define CSI3_VICAP3_SEL		3
#define CSI2_VICAP2_SEL		2
#define CSI1_VICAP1_SEL		1
#define CSI0_VICAP0_SEL		0

#define MIPI				0
#define MULTI_PROTOCOL 		1

//VI_REG_ISP_DVP_SEL
#define ISP_DVP_SEL_BITS    3
#define ISP0_DVP0_SEL_LSB   0
#define ISP0_DVP1_SEL_LSB   3
#define ISP0_DVP2_SEL_LSB   12
#define ISP0_DVP3_SEL_LSB   15
#define ISP1_DVP0_SEL_LSB   6
#define ISP1_DVP1_SEL_LSB   9
#define ISP1_DVP2_SEL_LSB   18
#define ISP1_DVP3_SEL_LSB   21

enum vi_cntl_type {
	CONTROLLER0 = 0,
	CONTROLLER1,
	CONTROLLER2,
	CONTROLLER3,
	CONTROLLER4,
	CONTROLLER5,
	IO_DVP0,
	IO_DVP1,
	CONTROLLER_BUTT,
};

#define POL_HIGH_ACTVIE 	0
#define POL_LOW_ACTIVE		1

#define IO_DVP1_VALID_POL	29
#define IO_DVP1_HSYNC_POL	28
#define IO_DVP1_VSYNC_POL	27
#define IO_DVP0_VALID_POL	26
#define IO_DVP0_HSYNC_POL	25
#define IO_DVP0_VSYNC_POL	24
#define ISP1_DVP3_SEL  		21
#define ISP1_DVP2_SEL  		18
#define ISP0_DVP3_SEL  		15
#define ISP0_DVP2_SEL  		12
#define ISP1_DVP1_SEL  		9
#define ISP1_DVP0_SEL  		6
#define ISP0_DVP1_SEL  		3
#define ISP0_DVP0_SEL  		0

enum {
	VVSOC_IOC_S_RESET_ISP = _IO('r', 0),
	VVSOC_IOC_S_POWER_ISP,
	VVSOC_IOC_G_POWER_ISP,
	VVSOC_IOC_S_CLOCK_ISP,
	VVSOC_IOC_G_CLOCK_ISP,

	VVSOC_IOC_S_RESET_DWE,
	VVSOC_IOC_S_POWER_DWE,
	VVSOC_IOC_G_POWER_DWE,
	VVSOC_IOC_S_CLOCK_DWE,
	VVSOC_IOC_G_CLOCK_DWE,

	VVSOC_IOC_S_RESET_VSE,
	VVSOC_IOC_S_POWER_VSE,
	VVSOC_IOC_G_POWER_VSE,
	VVSOC_IOC_S_CLOCK_VSE,
	VVSOC_IOC_G_CLOCK_VSE,

	VVSOC_IOC_S_RESET_CSI,
	VVSOC_IOC_S_POWER_CSI,
	VVSOC_IOC_G_POWER_CSI,
	VVSOC_IOC_S_CLOCK_CSI,
	VVSOC_IOC_G_CLOCK_CSI,

	VVSOC_IOC_S_RESET_SENSOR,
	VVSOC_IOC_S_POWER_SENSOR,
	VVSOC_IOC_G_POWER_SENSOR,
	VVSOC_IOC_S_CLOCK_SENSOR,
	VVSOC_IOC_G_CLOCK_SENSOR,

	VVSOC_IOC_S_RESET_DVP,
	VVSOC_IOC_S_POWER_DVP,
	VVSOC_IOC_G_POWER_DVP,
	VVSOC_IOC_S_CLOCK_DVP,
	VVSOC_IOC_G_CLOCK_DVP,

	VVSOC_IOC_S_MULTI2ISP_SIZE,
	VVSOC_IOC_S_IMX327_HDR,

	VVSOC_IOC_S_DVP2AXI_RESET,
	VVSOC_IOC_S_DVP2AXI_IODVP_SEL,
	VVSOC_IOC_S_DVP2AXI_CHN_ATTR,
	VVSOC_IOC_S_DVP2AXI_EMD_ATTR,
	VVSOC_IOC_S_DVP2AXI_IOVA_GET,
	VVSOC_IOC_S_DVP2AXI_IOVA_PUT,

	VVSOC_IOC_S_PHY_MODE,
	VVSOC_IOC_S_CONTROLLER_SEL,
	VVSOC_IOC_S_ISP_DVP_SEL,

	VVSOC_IOC_MAX,
};

struct soc_control_context {
	uint32_t device_idx;
	uint32_t control_value;
};

struct dvp2axi_chn_attr {
	uint32_t device_idx;
	uint8_t enable;
	uint8_t pixel_width;
	uint32_t hs_num;
	uint32_t vs_num;
	uint32_t chn0_baddr;
	uint32_t chn1_baddr;
	uint32_t chn2_baddr;
	uint16_t hs_stride;
	uint8_t first_id;
	uint8_t last_id;
};

struct dvp2axi_emd_chn_attr {
	uint32_t device_idx;
	uint32_t emd_baddr;
	uint16_t emd_stride;
	uint16_t emd_hsnum;
};

struct dvp2axi_dmabuf_attr {
	int dmabuf_fd;
	unsigned long iova;
};

struct isp_dvp_attr {
	uint32_t devId; // 0 or 1
	enum vi_cntl_type dvp_sel; // 0 ~ 7
};

struct vvcam_soc_func_s {
	int (*set_power) (void *, unsigned int, unsigned int);
	int (*get_power) (void *, unsigned int, unsigned int *);
	int (*set_reset) (void *, unsigned int, unsigned int);
	int (*set_clk) (void *, unsigned int, unsigned int);
	int (*get_clk) (void *, unsigned int, unsigned int *);
};

struct vvcam_soc_function_s {
	struct vvcam_soc_func_s isp_func;
	struct vvcam_soc_func_s dwe_func;
	struct vvcam_soc_func_s vse_func;
	struct vvcam_soc_func_s csi_func;
	struct vvcam_soc_func_s sensor_func;
};

struct vvcam_soc_access_s {
	int (*write) (void *ctx, uint32_t address, uint32_t data);
	int (*read) (void *ctx, uint32_t address, uint32_t *data);
};

#ifdef __KERNEL__

struct vvcam_soc_dev {
	void __iomem *base;
	void __iomem *scu_base;
	struct soc_control_context isp0;
	struct soc_control_context isp1;
	struct soc_control_context dwe;
	struct soc_control_context vse;
	struct vvcam_soc_function_s soc_func;
	struct vvcam_soc_access_s soc_access;
	void * csi_private;
	struct dentry *soc_debug_dc;
	struct dentry *soc_reset;
	uint32_t dvp2axi_iodvp_en;
	uint32_t multi2isp_iodvp_en;
	wait_queue_head_t irq_wait;
	atomic_t irqc_err;
	atomic_t irqc[VI_DVP2AXI_DVP_CHNS][VI_DVP2AXI_VIRTUAL_CHNS]; // Record the irq times informed to userspace
	uint32_t dvp2axi_framesize[VI_DVP2AXI_DVP_CHNS];
	atomic_t dvp2axi_vchn_fidx[VI_DVP2AXI_DVP_CHNS][VI_DVP2AXI_VIRTUAL_CHNS]; // flush idx, to mask irq
	atomic_t dvp2axi_vchn_didx[VI_DVP2AXI_DVP_CHNS][VI_DVP2AXI_VIRTUAL_CHNS]; // done idx, to mask irq
	atomic_t dvp2axi_errirq[VI_DVP2AXI_ERRIRQ_NUM]; // to mask 9 types of err irq, otherwise the flooding irqs will come
	uint32_t dvp2axi_irqnum[8];
	uint32_t dvp2axi_intrid[8];
};
// internal functions

typedef struct _isp_clk_rst {
	struct clk            *aclk;
	struct clk            *cfg_clk;
	struct clk            *isp_aclk;
	struct clk            *dvp_clk;
	struct clk            *phy_cfg;
	struct clk            *phy_escclk;
	struct clk            *sht0;
	struct clk            *sht1;
	struct clk            *sht2;
	struct clk            *sht3;
	struct clk            *sht4;
	struct clk            *sht5;
	struct clk            *aclk_mux;
	struct clk            *dvp_mux;
	struct clk            *isp_mux;
	struct clk            *spll0_fout1;
	struct clk            *vpll_fout1;
	struct reset_control  *rstc_axi;
	struct reset_control  *rstc_cfg;
	struct reset_control  *rstc_isp;
	struct reset_control  *rstc_dvp;
	struct reset_control  *rstc_sht0;
	struct reset_control  *rstc_sht1;
	struct reset_control  *rstc_sht2;
	struct reset_control  *rstc_sht3;
	struct reset_control  *rstc_sht4;
	struct reset_control  *rstc_sht5;
} isp_clk_rst_t;

struct vvcam_soc_driver_dev {
	struct cdev cdev;
	dev_t devt;
	struct class *class;
	struct mutex vvmutex;
	struct platform_device *pdev;
	void *private;
	isp_clk_rst_t isp_crg;
};

struct visoc_pdata {
	struct vvcam_soc_driver_dev *pdrv_dev;
	struct heap_root root;
	int dvp_idx;
};

long soc_priv_ioctl(struct visoc_pdata *pdata, unsigned int cmd,
		    void __user *args);
int vvnative_soc_init(struct vvcam_soc_dev *dev);
int vvnative_soc_deinit(struct vvcam_soc_dev *dev);
int vvcam_soc_irq_init(struct platform_device *pdev, struct vvcam_soc_dev *psoc_dev);

#else
//User space connections

#endif

#endif // _SOC_IOC_H_
