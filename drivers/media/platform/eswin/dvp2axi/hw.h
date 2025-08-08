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
#include "../../../../phy/eswin/es-camera-module.h"
#include "regs.h"
#include "dev.h"

#define ES_DVP2AXI_DEV_MAX		7
#define ES_DVP2AXI_HW_DRIVER_NAME	"es_dvp2axihw"
#define ES_DVP2AXI_MAX_BUS_CLK	15
#define ES_DVP2AXI_MAX_RESET		15

#define ES_DVP2AXI_MAX_GROUP		4

#define ES_DVP2AXI_ERR_IRQ		6
#define ES_DVP2AXI_AFULL_IRQ	7		
#define ES_DVP2AXI_IRQ_NUM      8
#define ES_DVP2AXI_ERRIRQ_NUM	9

#define write_dvp2axi_reg(base, addr, val) \
	writel(val, (addr) + (base))
#define read_dvp2axi_reg(base, addr) \
	readl((addr) + (base))
#define write_dvp2axi_reg_or(base, addr, val) \
	writel(readl((addr) + (base)) | (val), (addr) + (base))
#define write_dvp2axi_reg_and(base, addr, val) \
	writel(readl((addr) + (base)) & (val), (addr) + (base))

/*
 * multi sensor sync mode
 * ES_DVP2AXI_NOSYNC_MODE: not used sync mode
 * ES_DVP2AXI_MASTER_MASTER:	internal master->external master
 * ES_DVP2AXI_MASTER_SLAVE:	internal master->slave
 * ES_DVP2AXI_MASTER_MASTER: pwm/gpio->external master
 * ES_DVP2AXI_MASTER_MASTER: pwm/gpio->slave
 */
enum es_dvp2axi_sync_mode {
	ES_DVP2AXI_NOSYNC_MODE,
	ES_DVP2AXI_MASTER_MASTER,
	ES_DVP2AXI_MASTER_SLAVE,
	ES_DVP2AXI_EXT_MASTER,
	ES_DVP2AXI_EXT_SLAVE,
};

struct es_dvp2axi_sync_dev {
	struct es_dvp2axi_device *dvp2axi_dev[ES_DVP2AXI_DEV_MAX];
	int count;
	bool is_streaming[ES_DVP2AXI_DEV_MAX];
};

struct es_dvp2axi_multi_sync_config {
	struct es_dvp2axi_sync_dev int_master;
	struct es_dvp2axi_sync_dev ext_master;
	struct es_dvp2axi_sync_dev slave;
	enum es_dvp2axi_sync_mode mode;
	int dev_cnt;
	int streaming_cnt;
	u32 sync_code;
	u32 sync_mask;
	u32 update_code;
	u32 update_cache;
	u32 frame_idx;
	bool is_attach;
};

struct es_dvp2axi_dummy_buffer {
	struct vb2_buffer vb;
	struct vb2_queue vb2_queue;
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	struct page **pages;
	void *mem_priv;
	void *vaddr;
	u32 size;
	int dma_fd;
	bool is_need_vaddr;
	bool is_need_dbuf;
	bool is_need_dmafd;
	bool is_free;
};

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
	const struct dvp2axi_reg *dvp2axi_regs;
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
	void __iomem			*csi_base;
	struct regmap			*grf;
	struct clk			*clks[ES_DVP2AXI_MAX_BUS_CLK];
	int				clk_size;
	struct iommu_domain		*domain;
	struct reset_control		*dvp2axi_rst[ES_DVP2AXI_MAX_RESET];
	int				chip_id;
	const struct dvp2axi_reg		*dvp2axi_regs;
	const struct vb2_mem_ops	*mem_ops;
	struct es_dvp2axi_device		*dvp2axi_dev[ES_DVP2AXI_DEV_MAX];
	int				dev_num;
	atomic_t			power_cnt;
	const struct es_dvp2axi_hw_match_data *match_data;
	struct mutex			dev_lock;
	struct es_dvp2axi_multi_sync_config	sync_config[ES_DVP2AXI_MAX_GROUP];
	spinlock_t			group_lock;
	struct notifier_block		reset_notifier; /* reset for mipi csi crc err */
	struct es_dvp2axi_dummy_buffer	dummy_buf;
	bool				iommu_en;
	bool				can_be_reset;
	bool				is_dma_sg_ops;
	bool				is_dma_contig;
	bool				adapt_to_usbcamerahal;
	u64				irq_time;
	bool				is_eic770xs2;
	atomic_t 			dvp2axi_errirq_cnts[ES_DVP2AXI_ERRIRQ_NUM]; 
	struct mutex		dev_multi_chn_lock;
	spinlock_t			intr_spinlock;

};

void es_dvp2axi_disable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw);
int es_dvp2axi_enable_sys_clk(struct es_dvp2axi_hw *dvp2axi_hw);
int es_dvp2axi_plat_drv_init(void);

#endif
