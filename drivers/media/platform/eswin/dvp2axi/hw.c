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

/* eic770x */
#include <media/eswin/common-def.h>
// #include "dw-mipi-csi-hal.h"
#include "../vi_top/vitop.h"
#include "dw-mipi-csi-hal.h"

#include "dvp2axi.h"
// #include "common-def.h"

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

static const struct es_dvp2axi_hw_match_data eic770x_dvp2axi_match_data = {
	.chip_id = CHIP_EIC770X_DVP2AXI,
	.clks = eic770x_dvp2axi_clks,
	.clks_num = ARRAY_SIZE(eic770x_dvp2axi_clks),
	.rsts = eic770x_dvp2axi_rsts,
	.rsts_num = ARRAY_SIZE(eic770x_dvp2axi_rsts),
	.dvp2axi_regs = eic770x_dvp2axi_regs,
};

static const struct of_device_id es_dvp2axi_plat_of_match[] = {
	{
		.compatible = "eswin,dvp2axi",
		.data = &eic770x_dvp2axi_match_data,
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
	es_irq_oneframe(dev, dvp2axi_hw->dvp2axi_dev[0]);

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

static uint32_t dvp2axi_int0_done_mask = 0;
static uint32_t dvp2axi_int0_flush_mask = 0;
static uint32_t dvp2axi_int1_done_mask = 0;
static uint32_t dvp2axi_int1_flush_mask = 0;
static uint32_t dvp2axi_int2_err_global_mask = 0;
static uint32_t dvp2axi_int2_err_dvp_mask = 0;

void bmtest_dvp2axi_hw_program(struct es_dvp2axi_hw *dvp2axi_hw)
{   
    uint32_t val1, val2, val3;
    uint32_t stride, shnum;

    // /* initalize global control */
    // memset(dvp2axi_out_dvp_vch_fid, 0, VI_DVP2AXI_DVP_CHANNELS * VI_DVP2AXI_VIRTUAL_CHANNELS * sizeof(uint32_t));
    // memset(dvp2axi_out_dvp_vch_enable, 0, VI_DVP2AXI_DVP_CHANNELS * VI_DVP2AXI_VIRTUAL_CHANNELS * sizeof(uint8_t));
    // memset(dvp2axi_out_dvp_vch_done, 1, VI_DVP2AXI_DVP_CHANNELS * VI_DVP2AXI_VIRTUAL_CHANNELS * sizeof(uint8_t));
    // memset(dvp2axi_out_dvp_ch_done, 1, VI_DVP2AXI_DVP_CHANNELS * sizeof(uint8_t));

    // memset(dvp2axi_out_dvp_ch_flush_mask, 0, VI_DVP2AXI_DVP_CHANNELS * sizeof(uint32_t));
    // memset(dvp2axi_out_dvp_ch_done_mask, 0, VI_DVP2AXI_DVP_CHANNELS * sizeof(uint32_t));

    /* no bit shift, burst length 16 */

    /* DVP0-1 16bit, IO_DVP disable */
    val1 = DVP2AXI_IO_DVP_DIS;
#ifdef DVP2AXI_DVP0_ENABLE
    val1 |= (DVP2AXI_DVP0_PWIDTH << 20);
#endif
#ifdef DVP2AXI_DVP1_ENABLE
    val1 |= (DVP2AXI_DVP1_PWIDTH << 25);
#endif
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL1_CSR, val1);

    /* outstanding 16 */
    val1 = (DVP2AXI_OUTSTANDING_SIZE-1) << 24;
    // #ifdef DVP2AXI_DVP2_ENABLE
    // val1 |= (DVP2AXI_DVP2_PWIDTH);
    // #endif
    // #ifdef DVP2AXI_DVP3_ENABLE
    // val1 |= (DVP2AXI_DVP3_PWIDTH << 5);
    // #endif
    // #ifdef DVP2AXI_DVP4_ENABLE
    // val1 |= (DVP2AXI_DVP4_PWIDTH << 10);
    // #endif
    // #ifdef DVP2AXI_DVP5_ENABLE
    // val1 |= (DVP2AXI_DVP5_PWIDTH << 15);
    // #endif
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL2_CSR, val1);

    /* 240p or 480p */
#ifdef DVP2AXI_DVP0_ENABLE
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR, SENSOR_OUT_H | (SENSOR_OUT_V << 16));//DVP0
    DPRINTK("DVP0 Size 0x%x\n", DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR));
#endif
    
#ifdef DVP2AXI_DVP0_ENABLE
    /* DVP0 Address */
    // DVP2AXI_HalWriteReg(dvp2axi_hw, dvp2axi_out_addr_csr[0][0], dvp2axi_out_addr[0][0]);
    // DVP2AXI_HalWriteReg(dvp2axi_hw, dvp2axi_out_addr_csr[0][1], dvp2axi_out_addr[0][1]);
    // DVP2AXI_HalWriteReg(dvp2axi_hw, dvp2axi_out_addr_csr[0][2], dvp2axi_out_addr[0][2]);
    // DVP2AXI_HalWriteReg(dvp2axi_hw, dvp2axi_out_addr_csr[0][3], dvp2axi_out_addr[0][3]);
    /*
	dvp2axi_out_dvp_vch_enable[0][0] = 1;
    dvp2axi_out_dvp_vch_done[0][0] = 0;
    dvp2axi_out_dvp_ch_done[0] = 0;
	*/
#endif

    /* DVP0,1 stride: 16bytes aligned */
    // #ifdef SENSOR_OUT_12BIT
    // val1 = SENSOR_OUT_H * 3 / 2;
    // #else
    // val1 = SENSOR_OUT_H * 5 / 4;
    // #endif
    val1 = 0;
    val2 = 0;
#ifdef DVP2AXI_DVP0_ENABLE
    val1 = SENSOR_OUT_H * (DVP2AXI_DVP0_PWIDTH / 8);
    val1 = (val1 + 15) & 0xfff0;
    val2 |= val1;
#endif

#if defined(DVP2AXI_DVP0_ENABLE) || defined(DVP2AXI_DVP1_ENABLE)
        DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR, val2);
#endif

// #if defined(DVP2AXI_DVP4_ENABLE) || defined(DVP2AXI_DVP5_ENABLE)
//     DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL35_CSR, val2);
// #endif

    stride = (SENSOR_OUT_H + SENSOR_OUT_H_PAD)*2 + 15;
    stride = stride & 0xfff0;
    shnum = SENSOR_OUT_H + SENSOR_OUT_H_PAD;
    val2 = 0;
    val3 = 0;
#if defined(DVP2AXI_DVP0_ENABLE) || defined(DVP2AXI_DVP1_ENABLE)
    val2 = 0;
#ifdef DVP2AXI_DVP0_ENABLE
    val2 |= stride;
    val3 |= shnum;
#endif
#ifdef DVP2AXI_DVP1_ENABLE
    val2 |= (stride << 16);
    val3 |= (shnum << 16);
#endif
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL37, val2);  //for embedded
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL40, val3);  //for embedded
    DPRINTK("###########0x84:0x%x, 0x88:0x%x, 0x94:0x%x, 0xe0:0x%x\n",DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR),DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL34_CSR), DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL37), DVP2AXI_HalReadReg(dvp2axi_hw, VI_DVP2AXI_CTRL40));
#endif	//#if defined(DVP2AXI_DVP0_ENABLE) || defined(DVP2AXI_DVP1_ENABLE)
#if defined(DVP2AXI_DVP2_ENABLE) || defined(DVP2AXI_DVP3_ENABLE)
    val2 = 0;
    val3 = 0;
#ifdef DVP2AXI_DVP2_ENABLE
    val2 |= stride;
    val3 |= shnum;
#endif
#ifdef DVP2AXI_DVP3_ENABLE
    val2 |= (stride << 16);
    val3 |= (shnum << 16);
#endif
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL38, val2);  //for embedded
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL41, val3);  //for embedded
#endif
#if defined(DVP2AXI_DVP4_ENABLE) || defined(DVP2AXI_DVP5_ENABLE)
    val2 = 0;
    val3 = 0;
#ifdef DVP2AXI_DVP4_ENABLE
    val2 |= stride;
    val3 |= shnum;
#endif
#ifdef DVP2AXI_DVP5_ENABLE
    val2 |= (stride << 16);
    val3 |= (shnum << 16);
#endif
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL39, val2);  //for embedded
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL42, val3);  //for embedded
#endif 	//#if defined(DVP2AXI_DVP4_ENABLE) || defined(DVP2AXI_DVP5_ENABLE)

	// //dvp0_hs_stride_cfg
	// DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR, 0x1a00);
	// DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL3_CSR, 0x09a00cd8);
	// DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL33_CSR, 0x1a00);

    /* virtual channel 0 only */
    // DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_CTRL36_CSR, 0);
}

void bmtest_dvp2axi_hw_int_setup(struct es_dvp2axi_hw *dvp2axi_hw)
{
    uint32_t val1 = 0, val2 = 0, val3 = 0, val4 = 0;

    dvp2axi_int2_err_global_mask = 0x103;
    /* DVP0 interrupt */
    #ifdef DVP2AXI_DVP0_ENABLE
    dvp2axi_int2_err_dvp_mask |= (1 << 2);
    val1 |= 7;
    val2 |= (1 << 9);
	
    // dvp2axi_out_dvp_ch_flush_mask[0] = 1;
    // dvp2axi_out_dvp_ch_done_mask[0] = (1 << 9);
    // DPRINTK("dvp0 %x %x\n", dvp2axi_out_dvp_ch_flush_mask[0], dvp2axi_out_dvp_ch_done_mask[0]);
    
	// #ifdef VI_DVP2AXI_IRQ_ENABLE
    // metal_external_interrupt_register(VI_DVP2AXI_DVP0_IRQ_NUM, bmtest_dvp2axi_irq_handler, VI_DVP2AXI_IRQ_PRIORITY, NULL);
    // #endif
    #endif

    dvp2axi_int0_flush_mask = val1;
    dvp2axi_int0_done_mask = val2;
    dvp2axi_int1_flush_mask = val3;
    dvp2axi_int1_done_mask = val4;
    DPRINTK("int0_mask=0x%x\n", val1 | val2);
    DPRINTK("int1_mask=0x%x\n", val3 | val4);
    DPRINTK("int2_mask=0x%x\n", dvp2axi_int2_err_dvp_mask | dvp2axi_int2_err_global_mask);
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK0_CSR, val1 | val2);
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK1_CSR, val3 | val4);
    DVP2AXI_HalWriteReg(dvp2axi_hw, VI_DVP2AXI_INT_MASK2_CSR, dvp2axi_int2_err_global_mask | dvp2axi_int2_err_dvp_mask);
}

static int es_dvp2axi_plat_hw_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	// struct device_node *np = dev->of_no8de;
	struct es_dvp2axi_hw *dvp2axi_hw;
	// struct es_dvp2axi_device *dvp2axi_dev;
	const struct es_dvp2axi_hw_match_data *data;
	struct resource *res;
	int ret, irq;
	// bool is_mem_reserved = false;
	struct notifier_block *notifier;
	// int package = 0;

	match = of_match_node(es_dvp2axi_plat_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	dvp2axi_hw = devm_kzalloc(dev, sizeof(*dvp2axi_hw), GFP_KERNEL);
	if (!dvp2axi_hw)
		return -ENOMEM;

	dev_set_drvdata(dev, dvp2axi_hw);
	dvp2axi_hw->dev = dev;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, es_dvp2axi_irq_handler,
			       IRQF_SHARED,
			       dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	dvp2axi_hw->irq = irq;
	dvp2axi_hw->match_data = data;
	dvp2axi_hw->chip_id = data->chip_id;
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

	ret = of_reserved_mem_device_init(dev);
	if(ret) {
		dev_err(dev, "Could not get reserved memory\n");
		return ret;
	}

	bmtest_dvp2axi_hw_program(dvp2axi_hw);
	bmtest_dvp2axi_hw_int_setup(dvp2axi_hw);
	// bmtest_dvp2axi_hw_enable(dvp2axi_hw);

	// if (of_property_read_bool(np, "eic770x,android-usb-camerahal-enable")) {
	// 	dev_info(dev, "config dvp2axi adapt to android usb camera hal!\n");
	// 	dvp2axi_hw->adapt_to_usbcamerahal = true;
	// }

	// dvp2axi_hw->grf = syscon_regmap_lookup_by_phandle(np, "eic770x,grf");
	// if (IS_ERR(dvp2axi_hw->grf))
	// 	dev_warn(dev, "unable to get eic770x,grf\n");

	// if (data->clks_num > ES_DVP2AXI_MAX_BUS_CLK ||
	//     data->rsts_num > ES_DVP2AXI_MAX_RESET) {
	// 	dev_err(dev, "out of range: clks(%d %d) rsts(%d %d)\n",
	// 		data->clks_num, ES_DVP2AXI_MAX_BUS_CLK,
	// 		data->rsts_num, ES_DVP2AXI_MAX_RESET);
	// 	return -EINVAL;
	// }

	// for (i = 0; i < data->clks_num; i++) {
	// 	struct clk *clk = devm_clk_get(dev, data->clks[i]);

	// 	if (IS_ERR(clk)) {
	// 		dev_err(dev, "failed to get %s\n", data->clks[i]);
	// 		return PTR_ERR(clk);
	// 	}
	// 	dvp2axi_hw->clks[i] = clk;
	// }
	// dvp2axi_hw->clk_size = data->clks_num;

	// for (i = 0; i < data->rsts_num; i++) {
	// 	struct reset_control *rst = NULL;

	// 	if (data->rsts[i])
	// 		rst = devm_reset_control_get(dev, data->rsts[i]);
	// 	if (IS_ERR(rst)) {
	// 		dvp2axi_hw->dvp2axi_rst[i] = NULL;
	// 		dev_err(dev, "failed to get %s\n", data->rsts[i]);
	// 	} else {
	// 		dvp2axi_hw->dvp2axi_rst[i] = rst;
	// 	}
	// }

	dvp2axi_hw->dvp2axi_regs = data->dvp2axi_regs;

	// dvp2axi_hw->is_dma_sg_ops = true;
	dvp2axi_hw->is_dma_sg_ops = false;
	dvp2axi_hw->is_dma_contig = true;
	mutex_init(&dvp2axi_hw->dev_lock);
	spin_lock_init(&dvp2axi_hw->group_lock);
	atomic_set(&dvp2axi_hw->power_cnt, 0);

	mutex_init(&dvp2axi_hw->dev_lock);

	pm_runtime_enable(&pdev->dev);

	platform_driver_register(&es_dvp2axi_plat_drv);
	platform_driver_register(&es_dvp2axi_subdev_driver);

	notifier = &dvp2axi_hw->reset_notifier;
	notifier->priority = 1;
	notifier->notifier_call = es_dvp2axi_reset_notifier;
	// es_dvp2axi_csi2_register_notifier(notifier); 
	pr_info("%s success! \n", __func__);
	return 0;
}

static int es_dvp2axi_plat_remove(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	// if (dvp2axi_hw->iommu_en)
	// 	es_dvp2axi_iommu_cleanup(dvp2axi_hw);

	mutex_destroy(&dvp2axi_hw->dev_lock);

	return 0;
}

static void es_dvp2axi_hw_shutdown(struct platform_device *pdev)
{
	struct es_dvp2axi_hw *dvp2axi_hw = platform_get_drvdata(pdev);
	// struct es_dvp2axi_device *dvp2axi_dev = NULL;
	// int i = 0;

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
