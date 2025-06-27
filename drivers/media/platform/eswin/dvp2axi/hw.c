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
// #include <media/videobuf2-cma-sg.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-fwnode.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include "common.h"

#include <linux/bitfield.h>
#include <linux/eswin-win2030-sid-cfg.h>

/* eic770x */
#include <media/eswin/common-def.h>
#include "../vi_top/vitop.h"
#include "dvp2axi.h"

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation

u32 devm_irq_num[6] = {0};
static inline void DVP2AXI_HalWriteReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address, u32 data) {
    writel(data, dvp2axi_hw->base_addr + address);
}
static inline u32 DVP2AXI_HalReadReg(struct es_dvp2axi_hw *dvp2axi_hw, u32 address) {
    u32 val;
    val = readl(dvp2axi_hw->base_addr + address);
    return val;
}

static const char * const eic770x_dvp2axi_clks[] = {
	"aclk_dvp2axi",
	"hclk_dvp2axi",
	"dclk_dvp2axi",
	"iclk_host0",
	"iclk_host1",
};

static const char * const eic770x_dvp2axi_rsts[] = {
	"rst_dvp2axi_a",
	"rst_dvp2axi_h",
	"rst_dvp2axi_d",
	"rst_dvp2axi_host0",
	"rst_dvp2axi_host1",
	"rst_dvp2axi_host2",
	"rst_dvp2axi_host3",
	"rst_dvp2axi_host4",
	"rst_dvp2axi_host5",
};

static const struct dvp2axi_reg eic770x_dvp2axi_regs[] = {
	[DVP2AXI_REG_DVP_CTRL] = DVP2AXI_REG(DVP_CTRL),
	[DVP2AXI_REG_DVP_INTEN] = DVP2AXI_REG(DVP_INTEN),
	[DVP2AXI_REG_DVP_INTSTAT] = DVP2AXI_REG(DVP_INTSTAT),
	[DVP2AXI_REG_DVP_FOR] = DVP2AXI_REG(DVP_FOR),
	[DVP2AXI_REG_DVP_MULTI_ID] = DVP2AXI_REG(DVP_MULTI_ID),
	[DVP2AXI_REG_DVP_SAV_EAV] = DVP2AXI_REG(DVP_SAV_EAV),
	[DVP2AXI_REG_DVP_FRM0_ADDR_Y] = DVP2AXI_REG(DVP_FRM0_ADDR_Y_ID0),
	[DVP2AXI_REG_DVP_FRM0_ADDR_UV] = DVP2AXI_REG(DVP_FRM0_ADDR_UV_ID0),
	[DVP2AXI_REG_DVP_FRM1_ADDR_Y] = DVP2AXI_REG(DVP_FRM1_ADDR_Y_ID0),
	[DVP2AXI_REG_DVP_FRM1_ADDR_UV] = DVP2AXI_REG(DVP_FRM1_ADDR_UV_ID0),
	[DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID1] = DVP2AXI_REG(DVP_FRM0_ADDR_Y_ID1),
	[DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID1] = DVP2AXI_REG(DVP_FRM0_ADDR_UV_ID1),
	[DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID1] = DVP2AXI_REG(DVP_FRM1_ADDR_Y_ID1),
	[DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID1] = DVP2AXI_REG(DVP_FRM1_ADDR_UV_ID1),
	[DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID2] = DVP2AXI_REG(DVP_FRM0_ADDR_Y_ID2),
	[DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID2] = DVP2AXI_REG(DVP_FRM0_ADDR_UV_ID2),
	[DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID2] = DVP2AXI_REG(DVP_FRM1_ADDR_Y_ID2),
	[DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID2] = DVP2AXI_REG(DVP_FRM1_ADDR_UV_ID2),
	[DVP2AXI_REG_DVP_FRM0_ADDR_Y_ID3] = DVP2AXI_REG(DVP_FRM0_ADDR_Y_ID3),
	[DVP2AXI_REG_DVP_FRM0_ADDR_UV_ID3] = DVP2AXI_REG(DVP_FRM0_ADDR_UV_ID3),
	[DVP2AXI_REG_DVP_FRM1_ADDR_Y_ID3] = DVP2AXI_REG(DVP_FRM1_ADDR_Y_ID3),
	[DVP2AXI_REG_DVP_FRM1_ADDR_UV_ID3] = DVP2AXI_REG(DVP_FRM1_ADDR_UV_ID3),
	[DVP2AXI_REG_DVP_VIR_LINE_WIDTH] = DVP2AXI_REG(DVP_VIR_LINE_WIDTH),
	[DVP2AXI_REG_DVP_SET_SIZE] = DVP2AXI_REG(DVP_CROP_SIZE),
	[DVP2AXI_REG_DVP_CROP] = DVP2AXI_REG(DVP_CROP),
	[DVP2AXI_REG_DVP_LINE_INT_NUM] = DVP2AXI_REG(DVP_LINE_INT_NUM_01),
	[DVP2AXI_REG_DVP_LINE_INT_NUM1] = DVP2AXI_REG(DVP_LINE_INT_NUM_23),
	[DVP2AXI_REG_DVP_LINE_CNT] = DVP2AXI_REG(DVP_LINE_INT_NUM_01),
	[DVP2AXI_REG_DVP_LINE_CNT1] = DVP2AXI_REG(DVP_LINE_INT_NUM_23),

	[DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0] = DVP2AXI_REG(CSI_MIPI0_ID0_CTRL0),
	[DVP2AXI_REG_MIPI_LVDS_ID0_CTRL1] = DVP2AXI_REG(CSI_MIPI0_ID0_CTRL1),
	[DVP2AXI_REG_MIPI_LVDS_ID1_CTRL0] = DVP2AXI_REG(CSI_MIPI0_ID1_CTRL0),
	[DVP2AXI_REG_MIPI_LVDS_ID1_CTRL1] = DVP2AXI_REG(CSI_MIPI0_ID1_CTRL1),
	[DVP2AXI_REG_MIPI_LVDS_ID2_CTRL0] = DVP2AXI_REG(CSI_MIPI0_ID2_CTRL0),
	[DVP2AXI_REG_MIPI_LVDS_ID2_CTRL1] = DVP2AXI_REG(CSI_MIPI0_ID2_CTRL1),
	[DVP2AXI_REG_MIPI_LVDS_ID3_CTRL0] = DVP2AXI_REG(CSI_MIPI0_ID3_CTRL0),
	[DVP2AXI_REG_MIPI_LVDS_ID3_CTRL1] = DVP2AXI_REG(CSI_MIPI0_ID3_CTRL1),
	[DVP2AXI_REG_MIPI_LVDS_CTRL] = DVP2AXI_REG(CSI_MIPI0_CTRL),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID0] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_Y_ID0),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID0] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_Y_ID0),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID0] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_UV_ID0),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID0] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_UV_ID0),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID0] = DVP2AXI_REG(CSI_MIPI0_VLW_ID0),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID1] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_Y_ID1),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID1] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_Y_ID1),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID1] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_UV_ID1),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID1] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_UV_ID1),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID1] = DVP2AXI_REG(CSI_MIPI0_VLW_ID1),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID2] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_Y_ID2),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID2] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_Y_ID2),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID2] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_UV_ID2),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID2] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_UV_ID2),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID2] = DVP2AXI_REG(CSI_MIPI0_VLW_ID2),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID3] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_Y_ID3),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID3] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_Y_ID3),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID3] = DVP2AXI_REG(CSI_MIPI0_FRM0_ADDR_UV_ID3),
	[DVP2AXI_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID3] = DVP2AXI_REG(CSI_MIPI0_FRM1_ADDR_UV_ID3),
	[DVP2AXI_REG_MIPI_LVDS_FRAME0_VLW_Y_ID3] = DVP2AXI_REG(CSI_MIPI0_VLW_ID3),
	[DVP2AXI_REG_MIPI_LVDS_INTEN] = DVP2AXI_REG(CSI_MIPI0_INTEN),
	[DVP2AXI_REG_MIPI_LVDS_INTSTAT] = DVP2AXI_REG(CSI_MIPI0_INTSTAT),
	[DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1] = DVP2AXI_REG(CSI_MIPI0_LINE_INT_NUM_ID0_1),
	[DVP2AXI_REG_MIPI_LVDS_LINE_INT_NUM_ID2_3] = DVP2AXI_REG(CSI_MIPI0_LINE_INT_NUM_ID2_3),
	[DVP2AXI_REG_MIPI_LVDS_LINE_LINE_CNT_ID0_1] = DVP2AXI_REG(CSI_MIPI0_LINE_CNT_ID0_1),
	[DVP2AXI_REG_MIPI_LVDS_LINE_LINE_CNT_ID2_3] = DVP2AXI_REG(CSI_MIPI0_LINE_CNT_ID2_3),
	[DVP2AXI_REG_MIPI_LVDS_ID0_CROP_START] = DVP2AXI_REG(CSI_MIPI0_ID0_CROP_START),
	[DVP2AXI_REG_MIPI_LVDS_ID1_CROP_START] = DVP2AXI_REG(CSI_MIPI0_ID1_CROP_START),
	[DVP2AXI_REG_MIPI_LVDS_ID2_CROP_START] = DVP2AXI_REG(CSI_MIPI0_ID2_CROP_START),
	[DVP2AXI_REG_MIPI_LVDS_ID3_CROP_START] = DVP2AXI_REG(CSI_MIPI0_ID3_CROP_START),
	[DVP2AXI_REG_MIPI_FRAME_NUM_VC0] = DVP2AXI_REG(CSI_MIPI0_FRAME_NUM_VC0),
	[DVP2AXI_REG_MIPI_FRAME_NUM_VC1] = DVP2AXI_REG(CSI_MIPI0_FRAME_NUM_VC1),
	[DVP2AXI_REG_MIPI_FRAME_NUM_VC2] = DVP2AXI_REG(CSI_MIPI0_FRAME_NUM_VC2),
	[DVP2AXI_REG_MIPI_FRAME_NUM_VC3] = DVP2AXI_REG(CSI_MIPI0_FRAME_NUM_VC3),
	[DVP2AXI_REG_MIPI_EFFECT_CODE_ID0] = DVP2AXI_REG(CSI_MIPI0_EFFECT_CODE_ID0),
	[DVP2AXI_REG_MIPI_EFFECT_CODE_ID1] = DVP2AXI_REG(CSI_MIPI0_EFFECT_CODE_ID1),
	[DVP2AXI_REG_MIPI_EFFECT_CODE_ID2] = DVP2AXI_REG(CSI_MIPI0_EFFECT_CODE_ID2),
	[DVP2AXI_REG_MIPI_EFFECT_CODE_ID3] = DVP2AXI_REG(CSI_MIPI0_EFFECT_CODE_ID3),
	[DVP2AXI_REG_MIPI_FRAME_SIZE_ID0] = DVP2AXI_REG(CSI_MIPI0_FRAME_SIZE_ID0),
	[DVP2AXI_REG_MIPI_FRAME_SIZE_ID1] = DVP2AXI_REG(CSI_MIPI0_FRAME_SIZE_ID1),
	[DVP2AXI_REG_MIPI_FRAME_SIZE_ID2] = DVP2AXI_REG(CSI_MIPI0_FRAME_SIZE_ID2),
	[DVP2AXI_REG_MIPI_FRAME_SIZE_ID3] = DVP2AXI_REG(CSI_MIPI0_FRAME_SIZE_ID3),
	[DVP2AXI_REG_MIPI_ON_PAD] = DVP2AXI_REG(CSI_MIPI0_ON_PAD),

	[DVP2AXI_REG_GLB_CTRL] = DVP2AXI_REG(GLB_CTRL),
	[DVP2AXI_REG_GLB_INTEN] = DVP2AXI_REG(GLB_INTEN),
	[DVP2AXI_REG_GLB_INTST] = DVP2AXI_REG(GLB_INTST),

	[DVP2AXI_REG_SCL_CH_CTRL] = DVP2AXI_REG(SCL_CH_CTRL),
	[DVP2AXI_REG_SCL_CTRL] = DVP2AXI_REG(SCL_CTRL),
	[DVP2AXI_REG_SCL_FRM0_ADDR_CH0] = DVP2AXI_REG(SCL_FRM0_ADDR_CH0),
	[DVP2AXI_REG_SCL_FRM1_ADDR_CH0] = DVP2AXI_REG(SCL_FRM1_ADDR_CH0),
	[DVP2AXI_REG_SCL_VLW_CH0] = DVP2AXI_REG(SCL_VLW_CH0),
	[DVP2AXI_REG_SCL_FRM0_ADDR_CH1] = DVP2AXI_REG(SCL_FRM0_ADDR_CH1),
	[DVP2AXI_REG_SCL_FRM1_ADDR_CH1] = DVP2AXI_REG(SCL_FRM1_ADDR_CH1),
	[DVP2AXI_REG_SCL_VLW_CH1] = DVP2AXI_REG(SCL_VLW_CH1),
	[DVP2AXI_REG_SCL_FRM0_ADDR_CH2] = DVP2AXI_REG(SCL_FRM0_ADDR_CH2),
	[DVP2AXI_REG_SCL_FRM1_ADDR_CH2] = DVP2AXI_REG(SCL_FRM1_ADDR_CH2),
	[DVP2AXI_REG_SCL_VLW_CH2] = DVP2AXI_REG(SCL_VLW_CH2),
	[DVP2AXI_REG_SCL_FRM0_ADDR_CH3] = DVP2AXI_REG(SCL_FRM0_ADDR_CH3),
	[DVP2AXI_REG_SCL_FRM1_ADDR_CH3] = DVP2AXI_REG(SCL_FRM1_ADDR_CH3),
	[DVP2AXI_REG_SCL_VLW_CH3] = DVP2AXI_REG(SCL_VLW_CH3),
	[DVP2AXI_REG_SCL_BLC_CH0] = DVP2AXI_REG(SCL_BLC_CH0),
	[DVP2AXI_REG_SCL_BLC_CH1] = DVP2AXI_REG(SCL_BLC_CH1),
	[DVP2AXI_REG_SCL_BLC_CH2] = DVP2AXI_REG(SCL_BLC_CH2),
	[DVP2AXI_REG_SCL_BLC_CH3] = DVP2AXI_REG(SCL_BLC_CH3),
	[DVP2AXI_REG_TOISP0_CTRL] = DVP2AXI_REG(TOISP0_CH_CTRL),
	[DVP2AXI_REG_TOISP0_SIZE] = DVP2AXI_REG(TOISP0_CROP_SIZE),
	[DVP2AXI_REG_TOISP0_CROP] = DVP2AXI_REG(TOISP0_CROP),
	[DVP2AXI_REG_TOISP1_CTRL] = DVP2AXI_REG(TOISP1_CH_CTRL),
	[DVP2AXI_REG_TOISP1_SIZE] = DVP2AXI_REG(TOISP1_CROP_SIZE),
	[DVP2AXI_REG_TOISP1_CROP] = DVP2AXI_REG(TOISP1_CROP),
	[DVP2AXI_REG_GRF_DVP2AXIIO_CON] = DVP2AXI_REG(DVP2AXI_GRF_SOC_CON2),
};


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
	// unsigned int intstat_glb = 0;
	u64 irq_start, irq_stop;
	// int i;

	irq_start = ktime_get_ns();
	for(int i = 0; i < 6; i++) {
		if(irq == dvp2axi_hw->devm_irq_num[i]) {
			es_irq_oneframe(dev, dvp2axi_hw->dvp2axi_dev[i]);
		}
	}

	irq_stop = ktime_get_ns();
	dvp2axi_hw->irq_time = irq_stop - irq_start;
	return IRQ_HANDLED;
}

static irqreturn_t es_dvp2axi_err_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	u64 irq_start, irq_stop;

	irq_start = ktime_get_ns();
	for(int i = 0; i < ES_DVP2AXI_IRQ_NUM; i++) {
		if(irq == dvp2axi_hw->devm_irq_num[i]) {
			es_irq_err_handle(dev, dvp2axi_hw->dvp2axi_dev[i]);
		}
	}

	irq_stop = ktime_get_ns();
	dvp2axi_hw->irq_time = irq_stop - irq_start;
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

	write_dvp2axi_reg_and(dvp2axi_hw->base_addr, DVP2AXI_CSI_INTEN, 0x0);
	return 0;

err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(dvp2axi_hw->clks[i]);

	return ret;
}

void es_dvp2axi_hw_soft_reset(struct es_dvp2axi_hw *dvp2axi_hw, bool is_rst_iommu)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dvp2axi_hw->dvp2axi_rst); i++)
		if (dvp2axi_hw->dvp2axi_rst[i])
			reset_control_assert(dvp2axi_hw->dvp2axi_rst[i]);
	udelay(5);
	for (i = 0; i < ARRAY_SIZE(dvp2axi_hw->dvp2axi_rst); i++)
		if (dvp2axi_hw->dvp2axi_rst[i])
			reset_control_deassert(dvp2axi_hw->dvp2axi_rst[i]);
}

/* CTRL1 */
#define DVP2AXI_DVP0_PWIDTH 16
#define DVP2AXI_DVP1_PWIDTH 16

#define DVP2AXI_IO_DVP_DIS  0
#define DVP2AXI_IO_DVP_ENA  1
#define DVP2AXI_PIXEL_MODE_1WORD 0
#define DVP2AXI_PIXEL_MODE_2WORD 1
#define DVP2AXI_PIXEL_MODE_3WORD 2

/* CTRL2 */
#define DVP2AXI_DVP2_PWIDTH 16
#define DVP2AXI_DVP3_PWIDTH 16
#define DVP2AXI_DVP4_PWIDTH 16
#define DVP2AXI_DVP5_PWIDTH 16
#define DVP2AXI_OUTSTANDING_SIZE 16
#define DVP2AXI_WQOS_CFG 0


static int dvp2axi_smmu_sid_cfg(struct device* dev)
{
    int ret = 0;
    struct regmap* regmap = NULL;
    int mmu_tbu0_vi_dvp2axi_reg = 0;
    u32 rdwr_sid_ssid = 0;
    u32 sid = 0;
	u32 val;

    struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

    if (fwspec == NULL) {
        pr_info("Device is not behind SMMU, using default streamID(0)\n");
        return 0;
    }

	if (fwspec->num_ids == 0) {
		dev_err(dev, "No Stream IDs configured!\n");
		return -EINVAL;
	}

    sid = fwspec->ids[0];

    regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(regmap)) {
        pr_err("No vi_top_csr phandle specified, regmap=%ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
    }

    ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
                                    &mmu_tbu0_vi_dvp2axi_reg);
    if (ret) {
        pr_err("Failed to get sid cfg reg offset, ret=%d\n", ret);
        return ret;
    }

    rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, sid);
    rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
    rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
    rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);

    regmap_write(regmap, mmu_tbu0_vi_dvp2axi_reg, rdwr_sid_ssid);


	regmap_read(regmap, mmu_tbu0_vi_dvp2axi_reg, &val);

    ret = win2030_dynm_sid_enable(dev_to_node(dev));
    if (ret < 0)
        pr_err("Failed to enable dynamic SID for sid=%u, ret=%d\n", sid, ret);

    return ret;
}

static int es_dvp2axi_plat_hw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct es_dvp2axi_hw *dvp2axi_hw;
	struct resource *res;
	int ret, irq;
#ifdef CONFIG_NUMA
	u32 numa_id = 0;
#endif

	dvp2axi_hw = devm_kzalloc(dev, sizeof(*dvp2axi_hw), GFP_KERNEL);
	if (!dvp2axi_hw)
		return -ENOMEM;

	dev_set_drvdata(dev, dvp2axi_hw);
	dvp2axi_hw->dev = dev;

	ret = dvp2axi_smmu_sid_cfg(dev);
	if (ret) {
		dev_err(dev, "SMMU SID config failed: %d\n", ret);
		return ret;
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

	for(int i = 0; i < ES_DVP2AXI_ERRIRQ_NUM; i++)
		atomic_set(&dvp2axi_hw->dvp2axi_errirq_cnts[i], 0);

	dvp2axi_hw->irq = irq;

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

	// dvp2axi_hw->is_dma_sg_ops = true;
	dvp2axi_hw->is_dma_sg_ops = false;
	dvp2axi_hw->is_dma_contig = true;
	mutex_init(&dvp2axi_hw->dev_lock);
	mutex_init(&dvp2axi_hw->dev_multi_chn_lock);

	spin_lock_init(&dvp2axi_hw->group_lock);
	spin_lock_init(&dvp2axi_hw->intr_spinlock);
	atomic_set(&dvp2axi_hw->power_cnt, 0);

	pm_runtime_enable(&pdev->dev);

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
	return 0;
}

static int es_dvp2axi_plat_remove(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	mutex_destroy(&dvp2axi_hw->dev_lock);
	mutex_destroy(&dvp2axi_hw->dev_multi_chn_lock);

	return 0;
}

static void es_dvp2axi_hw_shutdown(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);

	if (pm_runtime_get_if_in_use(&pdev->dev) <= 0)
		return;

	write_dvp2axi_reg(dvp2axi_hw->base_addr, 0, 0);
	if (dvp2axi_hw->irq > 0)
		disable_irq(dvp2axi_hw->irq);
	pm_runtime_put(&pdev->dev);
}

static int __maybe_unused es_dvp2axi_runtime_suspend(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);

	if (atomic_dec_return(&dvp2axi_hw->power_cnt))
		return 0;
	es_dvp2axi_disable_sys_clk(dvp2axi_hw);

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused es_dvp2axi_runtime_resume(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	int ret;

	if (atomic_inc_return(&dvp2axi_hw->power_cnt) > 1)
		return 0;
	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	es_dvp2axi_enable_sys_clk(dvp2axi_hw);
	es_dvp2axi_hw_soft_reset(dvp2axi_hw, true);

	return 0;
}

static int __maybe_unused es_dvp2axi_sleep_suspend(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);

	if (atomic_read(&dvp2axi_hw->power_cnt) == 0)
		return 0;

	es_dvp2axi_disable_sys_clk(dvp2axi_hw);

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused es_dvp2axi_sleep_resume(struct device *dev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev_get_drvdata(dev);
	int ret;

	if (atomic_read(&dvp2axi_hw->power_cnt) == 0)
		return 0;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	es_dvp2axi_enable_sys_clk(dvp2axi_hw);
	es_dvp2axi_hw_soft_reset(dvp2axi_hw, true);

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
		.pm = &es_dvp2axi_plat_pm_ops,
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
