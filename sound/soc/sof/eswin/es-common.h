// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN audio sof driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: DengLei <denglei@eswincomputing.com>
 */

#ifndef __ES_COMMON_H__
#define __ES_COMMON_H__

#include <linux/clk.h>
#include <linux/mailbox_client.h>
#include <linux/es_iommu_rsv.h>
#include <linux/iommu.h>
#include "eswin-dsp-subsys.h"

#define EXCEPT_MAX_HDR_SIZE	0x400
#define ESWIN_DSP_STACK_DUMP_SIZE 32

#define DSP_UART_MUTEX_IOVA       0xff5c0000
#define DSP_UART_IOVA             0xff5d0000
#define DSP_MBOX_TX_IOVA          0xff5e0000
#define DSP_MBOX_RX_IOVA          0xff5f0000
#define DSP_RING_BUFFER_IOVA      0xff600000
#define DSP_FIRMWARE_IOVA         0xff800000

#define DSP_UART_MUTEX_IOVA_SIZE  0x10000
#define DSP_MBOX_IOVA_SIZE        0x10000
#define DSP_FIRMWARE_IOVA_SIZE    0x800000

#define UART_MUTEX_BASE_ADDR      0x51820000
#define UART_MUTEX_UNIT_OFFSET    4

#define IPC_MSG_RQ_FLAG 1
#define IPC_MSG_RP_FLAG 2

struct es_dsp_chan {
	struct es_dsp_ipc *ipc;
	struct mbox_client cl;
	struct mbox_chan *ch;
	char *name;
};

struct es_dsp_ops {
	void (*handle_reply)(struct es_dsp_ipc *ipc);
	void (*handle_request)(struct es_dsp_ipc *ipc);
};

struct es_dsp_ipc {
	/* Host <-> DSP communication uses 2 txdb and 2 rxdb channels */
	struct es_dsp_chan chan;
	struct device *dev;
	struct es_dsp_ops *ops;
	void *private_data;
};

struct es_sof_priv {
	struct device *dev;
	struct snd_sof_dev *sdev;

	/* DSP IPC handler */
	struct es_dsp_ipc *dsp_ipc;
	struct es_dsp_subsys *dsp_subsys;
	struct clk *aclk;

	u32 process_id;
	u32 dsp_iram_phyaddr;
	u64 dsp_iram_size;
	u32 dsp_dram_phyaddr;
	u64 dsp_dram_size;
	u32 uart_phyaddr;
	u64 uart_reg_size;
	u32 uart_mutex_phyaddr;
	u32 mbox_tx_phyaddr;
	u32 mbox_rx_phyaddr;
	u32 ringbuffer_phyaddr;
	u32 ringbuffer_size;
	void *firmware_addr;
	dma_addr_t firmware_dev_addr;
	dma_addr_t ringbuffer_dev_addr;
	dma_addr_t uart_dev_addr;
	dma_addr_t uart_mutex_dev_addr;
	dma_addr_t mbox_tx_dev_addr;
	dma_addr_t mbox_rx_dev_addr;
	bool dsp_hw_init_done;
};

static inline void es_dsp_set_data(struct es_dsp_ipc *ipc, void *data)
{
	if (!ipc)
		return;

	ipc->private_data = data;
}

static inline void *es_dsp_get_data(struct es_dsp_ipc *ipc)
{
	if (!ipc)
		return NULL;

	return ipc->private_data;
}

void es_dsp_put_subsys(struct snd_sof_dev *sdev);

int es_dsp_get_subsys(struct snd_sof_dev *sdev);

int es_dsp_ring_doorbell(struct es_dsp_ipc *dsp, void *msg);

struct mbox_chan *es_dsp_request_channel(struct es_dsp_ipc *ipc);
void es_dsp_free_channel(struct es_dsp_ipc *ipc);

void es_dsp_dump(struct snd_sof_dev *sdev, u32 flags);

struct es_dsp_ipc *es_ipc_init(struct snd_sof_dev *sdev);

int es_get_hw_res(struct snd_sof_dev *sdev);

int es_dsp_hw_init(struct snd_sof_dev *sdev);

extern const struct dsp_arch_ops es_sof_xtensa_arch_ops;

#endif
