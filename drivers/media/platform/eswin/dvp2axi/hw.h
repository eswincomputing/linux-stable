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

#ifndef _ES_DVP2AXI_HW_H
#define _ES_DVP2AXI_HW_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/es-camera-module.h>
#include "dvp2axi.h"
#include "dev.h"
#include "dvp2axi_vb2.h"

#define ES_DVP2AXI_DEV_MAX		7
#define ES_DVP2AXI_HW_DRIVER_NAME	"es_dvp2axihw"
#define ES_DVP2AXI_MAX_BUS_CLK	15
#define ES_DVP2AXI_MAX_RESET	15

#define ES_DVP2AXI_MAX_GROUP	4

#define ES_DVP2AXI_ERR_IRQ		6
#define ES_DVP2AXI_AFULL_IRQ	7
#define ES_DVP2AXI_IRQ_NUM		8
#define ES_DVP2AXI_ERRIRQ_NUM	9

#define DVP2AXI_DVP_CLK_EN		BIT(17)
#define CTRL_DVP_CLK_EN			0x1f80

/*
 * add new chip id in tail in time order
 * by increasing to distinguish dvp2axi version
 */
enum es_dvp2axi_chip_id {
	CHIP_EIC770X_DVP2AXI,
};

struct es_dvp2axi_hw_match_data {
	int chip_id;
	const char * const *clks;
	const char * const *rsts;
	int clks_num;
	int rsts_num;
};

/*
 * struct es_dvp2axi_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @stream: capture video device
 */
struct es_dvp2axi_hw {
	struct device			*dev;
	int				irq;
	int devm_irq_num[ES_DVP2AXI_IRQ_NUM];
	void __iomem			*base_addr;
	struct clk			*clks[ES_DVP2AXI_MAX_BUS_CLK];
	int				clk_size;
	struct reset_control		*dvp2axi_rst[ES_DVP2AXI_MAX_RESET];
	int				chip_id;
	const struct vb2_mem_ops	*mem_ops;
	struct es_dvp2axi_device		*dvp2axi_dev[ES_DVP2AXI_DEV_MAX];
	int				dev_num;
	atomic_t			power_cnt;
	const struct es_dvp2axi_hw_match_data *match_data;
	struct mutex			dev_lock;
	struct notifier_block		reset_notifier; /* reset for mipi csi crc err */
	bool				iommu_en;
	bool				is_dma_sg_ops;
	bool				is_dma_contig;
	bool				adapt_to_usbcamerahal;
	u64				irq_time;
	bool				is_eic770xs2;
	atomic_t 			dvp2axi_errirq_cnts[ES_DVP2AXI_ERRIRQ_NUM]; 
	struct mutex		dev_multi_chn_lock;
	struct tasklet_struct		dvp2axi_err_tasklet;
	struct clk_bulk_data	*clks_bulk;
	struct reset_control *rstc;
	int num_clks;
	struct regmap *vi_topcsr_regmap;
	u32 vi_topcsr_reg;

	struct dvp2axi_mem_pool* mem_pool;
	bool is_use_dvp2axi_mem_ops;
	spinlock_t stream_lock;

	struct clk *dvp_clk;
	struct clk *phy_cfg;
	struct clk *phy_txclkesc;
	struct clk *spll0_fout1;
	struct clk *vpll_fout1;
	struct clk *dvp_mux;

};

void es_dvp2axi_disable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw);
int es_dvp2axi_enable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw);
int es_dvp2axi_plat_drv_init(void);

#endif
