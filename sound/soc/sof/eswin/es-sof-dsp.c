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

#include <linux/bits.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <linux/reset.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/of_reserved_mem.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "../ops.h"
#include "../sof-of-dev.h"
#include "es-common.h"

#define MBOX_OFFSET             0x600000
#define MBOX_SIZE               0x1000

#define REG_OFFSET_DSP_START    0x00
#define REG_OFFSET_DSP_STAT     0x04
#define REG_OFFSET_DSP_PRID     0x08
#define REG_OFFSET_DSP_RESET    0x18
#define REG_OFFSET_DSP_DYARID   0x504
#define REG_OFFSET_DSP_DYAWID   0x508
#define REG_OFFSET_USR_CONF0    0x1c

//bit definitions for REG_OFFSET_DSP_STAT
#define DSP_STAT_REG_BIT_STAT_VECTOR_SEL      BIT_ULL(0)
#define DSP_STAT_REG_BIT_ARID_DYNM_EN         BIT_ULL(4)
#define DSP_STAT_REG_BITS_ARMMUSID            GENMASK_ULL(23, 16)

//bit definitions for REG_OFFSET_DSP_PRID
#define DSP_PRID_REG_BIT_PRID_MASK            GENMASK_ULL(15, 0)

//bit definitions for REG_OFFSET_DSP_RESET
#define DSP_RESET_REG_BIT_RUNSTALL_ON_RESET   BIT_ULL(0)
#define DSP_RESET_REG_BIT_CORE_RESET          BIT_ULL(1)
#define DSP_RESET_REG_BIT_DEBUG_RESET         BIT_ULL(2)

#define FIRMWARE_SIZE_MAX              0x800000     // 8M

//bit definitions for REG_OFFSET_DSP_DYAWID
#define DSP_DYAWID_REG_BITS_ARMMUSID_MASK     GENMASK(23, 16)
#define DSP_DYAWID_REG_BITS_AWMMUSID_MASK     GENMASK(23, 16)
#define REG_DEFAULT_SIZE               0x10000
#define REG_OFFSET_SIZE                0x20
#define REG_OFFSET_SIZE_8              0x8

#define REG_OFFSET(reg, pro_id)      (reg + (REG_OFFSET_SIZE * pro_id))
#define REG_OFFSET_8(reg, pro_id)    (reg + (REG_OFFSET_SIZE_8 * pro_id))

static int es_sof_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int es_sof_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static void es_sof_handle_reply(struct es_dsp_ipc *ipc)
{
	struct es_sof_priv *priv = es_dsp_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);
	snd_sof_ipc_process_reply(priv->sdev, 0);
	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static void es_sof_handle_request(struct es_dsp_ipc *ipc)
{
	struct es_sof_priv *priv = es_dsp_get_data(ipc);
	u32 p; /* Panic code */
	unsigned long flags;
	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4, &p, sizeof(p));

	/* Check to see if the message is a panic code (0x0dead***) */
	if ((p & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC){
		snd_sof_dsp_panic(priv->sdev, p, true);
	}
	else
		snd_sof_ipc_msgs_rx(priv->sdev);

	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static struct es_dsp_ops dsp_ops = {
	.handle_reply		= es_sof_handle_reply,
	.handle_request		= es_sof_handle_request,
};

static int es_sof_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct es_sof_priv *priv = sdev->pdata->hw_pdata;
	u64 *msg_flag = kzalloc(sizeof(u64), GFP_ATOMIC);
	if (!msg_flag)
		return -ENOMEM;

	*msg_flag = IPC_MSG_RQ_FLAG;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
					  msg->msg_size);
	es_dsp_ring_doorbell(priv->dsp_ipc, msg_flag);
	return 0;
}

/*
 * DSP control.
 */

static int es_dsp_run(struct snd_sof_dev *sdev)
{
	int ret;
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	dev_info(sdev->dev, "%s\n", __func__);

	dma_sync_single_for_device(sdev->dev, priv->firmware_dev_addr, DSP_FIRMWARE_IOVA_SIZE,
				   			   DMA_BIDIRECTIONAL);

	/*dereset dsp core*/
	ret = regmap_clear_bits(priv->dsp_subsys->map,
							REG_OFFSET(REG_OFFSET_DSP_RESET, priv->process_id),
							DSP_RESET_REG_BIT_RUNSTALL_ON_RESET);
	WARN_ON(0 != ret);

	return ret;
}

static int es_dsp_reset(struct snd_sof_dev *sdev)
{
	int ret;
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	dev_info(sdev->dev, "%s\n", __func__);

	//halt
	ret = regmap_set_bits(priv->dsp_subsys->map,
						  REG_OFFSET(REG_OFFSET_DSP_RESET, priv->process_id),
						  DSP_RESET_REG_BIT_RUNSTALL_ON_RESET);
	WARN_ON(0 != ret);
	/*reset dsp core*/
	ret = regmap_set_bits(priv->dsp_subsys->map,
						  REG_OFFSET(REG_OFFSET_DSP_RESET,
						  priv->process_id),
						  DSP_RESET_REG_BIT_DEBUG_RESET);
	WARN_ON(0 != ret);
	ret = regmap_set_bits(priv->dsp_subsys->map,
						  REG_OFFSET(REG_OFFSET_DSP_RESET, priv->process_id),
						  DSP_RESET_REG_BIT_CORE_RESET);
	WARN_ON(0 != ret);
	mdelay(20);
	/*set processer id*/
	ret = regmap_write_bits(priv->dsp_subsys->map,
							REG_OFFSET(REG_OFFSET_DSP_PRID, priv->process_id),
							DSP_PRID_REG_BIT_PRID_MASK, priv->process_id);
	WARN_ON(0 != ret);
	/*set reset vector*/
	ret = regmap_write(priv->dsp_subsys->map, REG_OFFSET(REG_OFFSET_DSP_START, priv->process_id),
					   priv->firmware_dev_addr);
	WARN_ON(0 != ret);
	ret = regmap_set_bits(priv->dsp_subsys->map, REG_OFFSET(REG_OFFSET_DSP_STAT, priv->process_id),
						  DSP_STAT_REG_BIT_STAT_VECTOR_SEL);
	WARN_ON(0 != ret);
	/*dereset dsp core*/
	ret = regmap_clear_bits(priv->dsp_subsys->map, REG_OFFSET(REG_OFFSET_DSP_RESET, priv->process_id),
							DSP_RESET_REG_BIT_CORE_RESET);

	/*set smmu id*/
	ret = regmap_write_bits(priv->dsp_subsys->map, REG_OFFSET_8(REG_OFFSET_DSP_DYAWID, priv->process_id),
							DSP_DYAWID_REG_BITS_AWMMUSID_MASK, (WIN2030_SID_DSP_0 + priv->process_id) << 16);
	WARN_ON(0 != ret);

	ret = regmap_write_bits(priv->dsp_subsys->map, REG_OFFSET_8(REG_OFFSET_DSP_DYARID, priv->process_id),
							DSP_DYAWID_REG_BITS_ARMMUSID_MASK, (WIN2030_SID_DSP_0 + priv->process_id) << 16);
	WARN_ON(0 != ret);

	ret = win2030_dynm_sid_enable(dev_to_node(sdev->dev));
	WARN_ON(0 != ret);

	/*enable jtag*/
	ret = regmap_write(priv->dsp_subsys->con_map, 0x330, 0xF0F0);
	WARN_ON(0 != ret);

	/*dereset dsp debug*/
	ret = regmap_clear_bits(priv->dsp_subsys->map, REG_OFFSET(REG_OFFSET_DSP_RESET, priv->process_id),
							DSP_RESET_REG_BIT_DEBUG_RESET);
	WARN_ON(0 != ret);

	return 0;
}

static int es_dsp_clk_enable(struct es_sof_priv *priv)
{
	int ret;
	u32 val;
	bool enabled;

	ret = clk_prepare_enable(priv->dsp_subsys->cfg_clk);
	if (ret) {
		dev_err(priv->dev, "failed to enable cfg clk, ret=%d.\n", ret);
		return ret;
	}
	enabled = __clk_is_enabled(priv->dsp_subsys->aclk);
	if (!enabled) {
		ret = clk_prepare_enable(priv->dsp_subsys->aclk);
		if (ret) {
			dev_err(priv->dev, "failed to enable aclk: %d\n", ret);
			return ret;
		}
	}

	regmap_read(priv->dsp_subsys->map, REG_OFFSET_USR_CONF0, &val);
	val |= (3 << (priv->process_id * 2));
	ret = regmap_write(priv->dsp_subsys->map, REG_OFFSET_USR_CONF0, val);
	regmap_read(priv->dsp_subsys->map, REG_OFFSET_USR_CONF0, &val);
	dev_info(priv->dev, "%s, %d, val=0x%x.\n", __func__, __LINE__, val);

	return 0;
}

static int es_dsp_probe(struct snd_sof_dev *sdev)
{
	struct es_sof_priv *priv;
	int ret = 0;

	dev_info(sdev->dev, "%s\n", __func__);

	priv = devm_kzalloc(sdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}
	sdev->num_cores = 1;
	sdev->pdata->hw_pdata = priv;
	priv->dev = sdev->dev;
	priv->sdev = sdev;

	ret = es_dsp_get_subsys(sdev);
	if (ret) {
		dev_err(sdev->dev, "get dsp subsys err, ret=%d.\n", ret);
		return -ENXIO;
	}

	ret = es_get_hw_res(sdev);
	if (ret) {
		dev_err(sdev->dev, "get hw res error.\n");
		goto err_release_subsys;
	}

	/* setting for DMA check */
	ret = dma_set_mask(sdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		WARN_ON_ONCE(sdev->dev->dma_mask);
		goto err_release_subsys;
	}

	priv->dsp_ipc = es_ipc_init(sdev);
	if (!priv->dsp_ipc) {
		/* DSP IPC driver not probed yet, try later */
		dev_err(sdev->dev, "Failed to init ipc channel\n");
		goto err_release_subsys;
	}
	es_dsp_set_data(priv->dsp_ipc, priv);
	priv->dsp_ipc->ops = &dsp_ops;

	ret = es_dsp_clk_enable(priv);
	if (0 != ret) {
		dev_err(sdev->dev, "Failed to enable subsys clk\n");
		goto err_release_ipc;
	}

	ret = win2030_tbu_power(sdev->dev, true);
	if (ret) {
		dev_err(sdev->dev, "tbu power failed\n");
		goto err;
	}

	sdev->mailbox_bar = SOF_FW_BLK_TYPE_SRAM;
	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	ret = es_dsp_hw_init(sdev);
	if (ret) {
		goto err;
	}
	priv->dsp_hw_init_done = true;

	return 0;

err:
	clk_disable_unprepare(priv->aclk);
err_release_ipc:
	es_dsp_free_channel(priv->dsp_ipc);
err_release_subsys:
	es_dsp_put_subsys(sdev);
	return ret;
}

static int es_dsp_remove(struct snd_sof_dev *sdev)
{
	dev_info(sdev->dev, "%s\n", __func__);
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	if (!priv || !priv->dsp_hw_init_done) {
		return 0;
	}

	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->ringbuffer_dev_addr,
						 priv->ringbuffer_size);
	iommu_unmap_rsv_iova(sdev->dev, priv->firmware_addr,
						 priv->firmware_dev_addr, DSP_FIRMWARE_IOVA_SIZE);
	priv->firmware_addr = NULL;
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->mbox_rx_dev_addr,
						 DSP_MBOX_IOVA_SIZE);
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->mbox_tx_dev_addr,
						 DSP_MBOX_IOVA_SIZE);
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->uart_mutex_dev_addr,
						 DSP_UART_MUTEX_IOVA_SIZE);
	iommu_unmap_rsv_iova(sdev->dev, NULL, priv->uart_dev_addr,
						 priv->uart_reg_size);
	release_mem_region(priv->dsp_iram_phyaddr,  priv->dsp_iram_size);
	release_mem_region(priv->dsp_dram_phyaddr,  priv->dsp_dram_size);

	es_dsp_free_channel(priv->dsp_ipc);
	es_dsp_put_subsys(sdev);
	return 0;
}

static int es_sof_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	/* Only IRAM and SRAM bars are valid */
	switch (type) {
	case SOF_FW_BLK_TYPE_IRAM:
	case SOF_FW_BLK_TYPE_DRAM:
	case SOF_FW_BLK_TYPE_SRAM:
		return type;
	default:
		dev_err(sdev->dev, "error type:%d\n", type);
		return -EINVAL;
	}
}

static struct snd_soc_dai_driver es_sof_dai[] = {
{
	.name = "dsp_port",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
	},
},
{
	.name = "dsp_port2",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
	},
},
{
	.name = "dsp_port3",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
	},
},
};

static int es_sof_dsp_set_power_state(struct snd_sof_dev *sdev,
				       const struct sof_dsp_power_state *target_state)
{
	sdev->dsp_power_state = *target_state;

	return 0;
}

static int es_sof_resume(struct snd_sof_dev *sdev)
{
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	es_dsp_request_channel(priv->dsp_ipc);
	return 0;
}

static void es_sof_suspend(struct snd_sof_dev *sdev)
{
	struct es_sof_priv *priv = (struct es_sof_priv *)sdev->pdata->hw_pdata;

	es_dsp_free_channel(priv->dsp_ipc);
}

static int es_sof_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	int ret;
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
	};

	ret = es_sof_resume(sdev);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int es_sof_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D3,
	};

	es_sof_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int es_sof_dsp_resume(struct snd_sof_dev *sdev)
{
	int ret;
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
	};

	ret = es_sof_resume(sdev);
	if (ret < 0)
		return ret;

	if (pm_runtime_suspended(sdev->dev)) {
		pm_runtime_disable(sdev->dev);
		pm_runtime_set_active(sdev->dev);
		pm_runtime_mark_last_busy(sdev->dev);
		pm_runtime_enable(sdev->dev);
		pm_runtime_idle(sdev->dev);
	}

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int es_sof_dsp_suspend(struct snd_sof_dev *sdev, unsigned int target_state)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = target_state,
	};

	if (!pm_runtime_suspended(sdev->dev))
		es_sof_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static struct snd_soc_acpi_mach *es_sof_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	struct snd_soc_acpi_mach *mach;

	mach = devm_kmalloc(sdev->dev, sizeof(struct snd_soc_acpi_mach), GFP_KERNEL);
	if (!mach) {
		dev_warn(sdev->dev, "warning: alloc machine info failed\n");
		return NULL;
	}

	sof_pdata->tplg_filename = "sof-esw-es8388.tplg";
	mach->sof_tplg_filename = "sof-esw-es8388.tplg";
	mach->drv_name = "asoc-simple-card";

	return mach;
}


static struct snd_sof_dsp_ops sof_es_sof_ops = {
	/* probe and remove */
	.probe = es_dsp_probe,
	.remove = es_dsp_remove,
	/* DSP core boot */
	.run = es_dsp_run,
	.reset = es_dsp_reset,

	/* Block IO */
	.block_read = sof_block_read,
	.block_write = sof_block_write,

	/* Mailbox IO */
	.mailbox_read = sof_mailbox_read,
	.mailbox_write = sof_mailbox_write,

	/* ipc */
	.send_msg = es_sof_send_msg,

	.get_mailbox_offset = es_sof_get_mailbox_offset,
	.get_window_offset = es_sof_get_window_offset,

	.ipc_msg_data = sof_ipc_msg_data,
	.set_stream_data_offset = sof_set_stream_data_offset,

	.get_bar_index = es_sof_get_bar_index,
	/* firmware loading */
	.load_firmware = snd_sof_load_firmware_memcpy,

	/* Debug information */
	.dbg_dump = es_dsp_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open = sof_stream_pcm_open,
	.pcm_close = sof_stream_pcm_close,
	/* Firmware ops */
	.dsp_arch_ops = &es_sof_xtensa_arch_ops,

	/* machine driver */
	.machine_select = es_sof_machine_select,

	/* DAI drivers */
	.drv = es_sof_dai,
	.num_drv = ARRAY_SIZE(es_sof_dai),

	.suspend = es_sof_dsp_suspend,
	.resume = es_sof_dsp_resume,

	.runtime_suspend = es_sof_dsp_runtime_suspend,
	.runtime_resume = es_sof_dsp_runtime_resume,

	.set_power_state = es_sof_dsp_set_power_state,

	.hw_info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};

static struct sof_dev_desc sof_of_es_sof_desc = {
	.ipc_supported_mask = BIT(SOF_IPC),
	.ipc_default = SOF_IPC,
	.ipc_timeout = 3000,
	.boot_timeout = 5000,
	.default_fw_path = {
		[SOF_IPC] = "eswin",
	},
	.default_tplg_path = {
		[SOF_IPC] = "eswin",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-2030.ri",
	},
	.nocodec_tplg_filename = "sof-esw-es8388.tplg",
	.ops = &sof_es_sof_ops,
};

static const struct of_device_id sof_of_es_sof_ids[] = {
	{ .compatible = "eswin,sof-dsp", .data = &sof_of_es_sof_desc},
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_es_sof_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_es_sof_driver = {
	.probe = sof_of_probe,
	.remove = sof_of_remove,
	.driver = {
		.name = "sof-audio-of-es-dsp",
		.of_match_table = sof_of_es_sof_ids,
	},
};
module_platform_driver(snd_sof_of_es_sof_driver);

MODULE_AUTHOR("DengLei <denglei@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:eswin dsp driver");
