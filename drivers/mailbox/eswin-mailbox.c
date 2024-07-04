// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Mailbox Driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>


#define ESWIN_MBOX_FIFO_DEPTH 8

struct eswin_mbox_data {
	int num_chans;
};

struct eswin_mbox_chan {
	int idx;
	int irq;
	struct eswin_mbox_msg msg[ESWIN_MBOX_FIFO_DEPTH];
	int msg_cnt;
	struct eswin_mbox *mb;
};

/*
 * Registers offset
 */
#define ESWIN_MBOX_WR_DATA0 0x00
#define ESWIN_MBOX_WR_DATA1 0x04
#define ESWIN_MBOX_RD_DATA0 0x08
#define ESWIN_MBOX_RD_DATA1 0x0C
#define ESWIN_MBOX_FIFO_STATUS 0x10
#define ESWIN_MBOX_MB_ERR 0x14
#define ESWIN_MBOX_INT_CTRL 0x18
#define ESWIN_MBOX_WR_LOCK 0x1C

struct eswin_mbox {
	struct mbox_controller mbox;
	struct clk *pclk;
	struct clk *pclk_device;
	struct reset_control *rst;
	struct reset_control *rst_device;
	void __iomem *mbox_base;
	void __iomem *mbox_rx_base;
	struct regmap *map;
	struct regmap *rx_map;
	struct device *dev;
	u32 lock_bit;
	u32 irq_bit;
	struct eswin_mbox_chan *chans;
	spinlock_t rx_lock;
};

static int eswin_mbox_send_data(struct mbox_chan *chan, void *data)
{
	u32 tmp_data;

	struct eswin_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	struct eswin_mbox_msg *msg = (struct eswin_mbox_msg *)data;

	dev_dbg(mb->mbox.dev, "send_data\n");
	if (!msg)
		return -EINVAL;

	// TX FIFO FULL?
	if (regmap_test_bits(mb->map, ESWIN_MBOX_FIFO_STATUS, BIT_ULL(0))) {
		return -EBUSY;
	}

	tmp_data = (u32)msg->data;
	regmap_write(mb->map, ESWIN_MBOX_WR_DATA0, tmp_data);

	tmp_data = (u32)(msg->data >> 32) | BIT(31);
	regmap_write(mb->map, ESWIN_MBOX_WR_DATA1, tmp_data);
	// Write interrupt enable bit.
	regmap_set_bits(mb->map, ESWIN_MBOX_INT_CTRL, mb->irq_bit);
	return 0;
}

static int eswin_mbox_startup(struct mbox_chan *chan)
{
	struct eswin_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	int ret;

	pm_runtime_get_sync(mb->dev);

	if (regmap_test_bits(mb->map, ESWIN_MBOX_WR_LOCK, mb->lock_bit)) {
		pm_runtime_put(mb->dev);
		return -1;
	}
	ret = regmap_set_bits(mb->map, ESWIN_MBOX_WR_LOCK, mb->lock_bit);

	/* Successfully write the occupancy flag bit, indicating successful occupancy */
	dev_dbg(mb->mbox.dev, "start, ret %d, lock_bit 0x%x\n", ret,
		mb->lock_bit);
	return ret;
}

static void eswin_mbox_shutdown(struct mbox_chan *chan)
{
	struct eswin_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	int ret;

	ret = regmap_clear_bits(mb->map, ESWIN_MBOX_WR_LOCK, mb->lock_bit);
	if (0 != ret)
		dev_err(mb->mbox.dev, "failed to shutdown mailbox\n");

	ret = regmap_clear_bits(mb->map, ESWIN_MBOX_INT_CTRL, mb->irq_bit);
	if (0 != ret)
		dev_err(mb->mbox.dev, "failed to disable  mailbox int\n");

	pm_runtime_put(mb->dev);
	return;
}

static int eswin_mbox_receive_data(struct eswin_mbox *mb,
				   struct eswin_mbox_msg *msg)
{
	u32 tmp_data;
	u32 tmp_data0;
	u64 tmp;

	regmap_read(mb->rx_map, ESWIN_MBOX_RD_DATA0, &tmp_data0);
	regmap_read(mb->rx_map, ESWIN_MBOX_RD_DATA1, &tmp_data);

	// RX FIFO empty ?
	if (tmp_data == 0) {
		return -1;
	}
	tmp = (u64)tmp_data << 32 | tmp_data0;

	msg->data = tmp;

	/*trigger FIFO dequeue*/
	regmap_write(mb->rx_map, ESWIN_MBOX_RD_DATA1, 0x0);
	return 0;
}

static bool eswin_mbox_peek_data(struct mbox_chan *chan)
{
	int idx;
	struct mbox_controller *mbox = chan->mbox;
	struct eswin_mbox_msg *msg = NULL;
	struct eswin_mbox *mb = container_of(mbox, struct eswin_mbox, mbox);
	bool IsData = false;
	unsigned long flags;

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		spin_lock_irqsave(&mb->rx_lock, flags);
		msg = &mb->chans[idx].msg[0];
		if (0 != eswin_mbox_receive_data(mb, msg)) {
			spin_unlock_irqrestore(&mb->rx_lock, flags);
			continue;
		}
		IsData = true;
		mb->chans[idx].msg_cnt--;
		if (NULL != mb->mbox.chans[idx].cl) {
			mbox_chan_received_data(&mb->mbox.chans[idx], msg);
		}
		spin_unlock_irqrestore(&mb->rx_lock, flags);
	}
	return IsData;
}

/*
    once the data has been enqueued to mailbox hw FIFO in send_data function,
    we believe that tx is done
*/
static bool eswin_mbox_last_tx_done(struct mbox_chan *chan)
{
	return true;
}

static const struct mbox_chan_ops eswin_mbox_chan_ops = {
	.send_data = eswin_mbox_send_data,
	.peek_data = eswin_mbox_peek_data,
	.startup = eswin_mbox_startup,
	.shutdown = eswin_mbox_shutdown,
	.last_tx_done = eswin_mbox_last_tx_done,
};

static irqreturn_t eswin_mbox_irq(int irq, void *dev_id)
{
	int idx;
	struct eswin_mbox *mb = (struct eswin_mbox *)dev_id;
	unsigned long flags;

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if (irq != mb->chans[idx].irq)
			continue;

		spin_lock_irqsave(&mb->rx_lock, flags);
		WARN_ON(0 != mb->chans[idx].msg_cnt);
		while (0 ==
		       eswin_mbox_receive_data(
			       mb,
			       &mb->chans[idx].msg[mb->chans[idx].msg_cnt])) {
			mb->chans[idx].msg_cnt++;
			/*
				 MCU may continue enqeuing fifo when we are dequeuing fifo,
				 So we receive up to ESWIN_MBOX_FIFO_DEPTH cnt msgs one time.
				 The left msgs will be handled after eswin_mbox_isr finished.
				*/
			if (ESWIN_MBOX_FIFO_DEPTH == mb->chans[idx].msg_cnt) {
				break;
			}
		};
		spin_unlock_irqrestore(&mb->rx_lock, flags);
	}
	return IRQ_WAKE_THREAD;
}

static irqreturn_t eswin_mbox_isr(int irq, void *dev_id)
{
	int idx;
	struct eswin_mbox_msg *msg = NULL;
	struct eswin_mbox *mb = (struct eswin_mbox *)dev_id;
	unsigned long flags;
	int i;

	for (idx = 0; idx < mb->mbox.num_chans; idx++) {
		if (irq != mb->chans[idx].irq)
			continue;

		i = 0;
		spin_lock_irqsave(&mb->rx_lock, flags);
		while (mb->chans[idx].msg_cnt) {
			msg = &mb->chans[idx].msg[i++];
			if (NULL != mb->mbox.chans[idx].cl) {
				dev_dbg(mb->mbox.dev,
					"Chan[%d]: receive MCU message, msg %p\n",
					idx, msg);
				mbox_chan_received_data(&mb->mbox.chans[idx],
							msg);
			}
			mb->chans[idx].msg_cnt--;
		}
		spin_unlock_irqrestore(&mb->rx_lock, flags);
	}
	return IRQ_HANDLED;
}

static const struct eswin_mbox_data win2030_drv_data = {
	.num_chans = 1,
};

static const struct of_device_id eswin_mbox_of_match[] = {
	{ .compatible = "eswin,win2030-mailbox", .data = &win2030_drv_data },
	{},
};
MODULE_DEVICE_TABLE(of, eswin_mbox_of_match);

static int eswin_mbox_reg_read(void *context, unsigned int reg,
		unsigned int *val)
{
	struct eswin_mbox *mb = context;

	*val = readl_relaxed(mb->mbox_base + reg);
	return 0;
}

static int eswin_mbox_reg_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct eswin_mbox *mb = context;

	writel_relaxed(val, mb->mbox_base + reg);
	return 0;
}

static int eswin_mbox_rx_reg_read(void *context, unsigned int reg,
				  unsigned int *val)
{
	struct eswin_mbox *mb = context;

	*val = readl_relaxed(mb->mbox_rx_base + reg);
	return 0;
}

static int eswin_mbox_rx_reg_write(void *context, unsigned int reg,
				    unsigned int val)
{
	struct eswin_mbox *mb = context;

	writel_relaxed(val, mb->mbox_rx_base + reg);
	return 0;
}

/**
 * eswin_mbox_init_regmap() - Initialize registers map
 * @dev: device private data
 *
 * Autodetects needed register access mode and creates the regmap with
 * corresponding read/write callbacks. This must be called before doing any
 * other register access.
 */
int eswin_mbox_init_regmap(struct eswin_mbox *mb)
{
	struct regmap_config map_cfg = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.use_hwlock = true,
		.cache_type = REGCACHE_NONE,
		.can_sleep = false,
		.reg_read = eswin_mbox_reg_read,
		.reg_write = eswin_mbox_reg_write,
	};
	struct regmap_config rx_map_cfg = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.use_hwlock = true,
		.cache_type = REGCACHE_NONE,
		.can_sleep = false,
		.reg_read = eswin_mbox_rx_reg_read,
		.reg_write = eswin_mbox_rx_reg_write,
	};

	/*
	 * Note we'll check the return value of the regmap IO accessors only
	 * at the probe stage. The rest of the code won't do this because
	 * basically we have MMIO-based regmap so non of the read/write methods
	 * can fail.
	 */
	mb->map = devm_regmap_init(mb->dev, NULL, mb, &map_cfg);
	if (IS_ERR(mb->map)) {
		dev_err(mb->dev, "Failed to init the registers map\n");
		return PTR_ERR(mb->map);
	}
	mb->rx_map = devm_regmap_init(mb->dev, NULL, mb, &rx_map_cfg);
	if (IS_ERR(mb->rx_map)) {
		dev_err(mb->dev, "Failed to init the registers rx map\n");
		regmap_exit(mb->map);
		return PTR_ERR(mb->rx_map);
	}

	return 0;
}

static int eswin_mbox_prepare_clk(struct device *dev, bool enable)
{
	int ret = 0;
	struct eswin_mbox *mb = dev_get_drvdata(dev);

	if (enable) {
		ret = clk_prepare_enable(mb->pclk);
		if (ret) {
			dev_err(dev, "failed to enable host mailbox pclk: %d\n", ret);
			return ret;
		}
		ret = clk_prepare_enable(mb->pclk_device);
		if (ret) {
			dev_err(dev, "failed to enable device mailbox pclk: %d\n", ret);
			return ret;
		}
	} else {
		clk_disable_unprepare(mb->pclk);
		clk_disable_unprepare(mb->pclk_device);
	}
	return ret;
}

static int eswin_mbox_probe(struct platform_device *pdev)
{
	struct eswin_mbox *mb;
	const struct of_device_id *match;
	const struct eswin_mbox_data *drv_data;
	struct resource *res;
	int ret, irq, i;

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(eswin_mbox_of_match, pdev->dev.of_node);
	drv_data = (const struct eswin_mbox_data *)match->data;

	mb = devm_kzalloc(&pdev->dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	if (of_property_read_u32(pdev->dev.of_node, "lock-bit",
				 &mb->lock_bit)) {
		dev_err(&pdev->dev, "failed to get lock_bit: %d\n", ret);
	}

	if (of_property_read_u32(pdev->dev.of_node, "irq-bit", &mb->irq_bit)) {
		dev_err(&pdev->dev, "failed to get irq_bit: %d\n", ret);
	}
	mb->chans = devm_kcalloc(&pdev->dev, drv_data->num_chans,
				 sizeof(*mb->chans), GFP_KERNEL);
	if (!mb->chans)
		return -ENOMEM;

	mb->mbox.chans = devm_kcalloc(&pdev->dev, drv_data->num_chans,
				      sizeof(*mb->mbox.chans), GFP_KERNEL);
	if (!mb->mbox.chans)
		return -ENOMEM;

	platform_set_drvdata(pdev, mb);

	mb->mbox.dev = &pdev->dev;
	mb->mbox.num_chans = drv_data->num_chans;
	mb->mbox.ops = &eswin_mbox_chan_ops;
	mb->mbox.txdone_irq = false;
	mb->mbox.txdone_poll = true;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	mb->mbox_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mb->mbox_base))
		return PTR_ERR(mb->mbox_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	mb->mbox_rx_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mb->mbox_rx_base))
		return PTR_ERR(mb->mbox_rx_base);

	mb->pclk = devm_clk_get(&pdev->dev, "pclk_mailbox_host");
	if (IS_ERR(mb->pclk)) {
		ret = PTR_ERR(mb->pclk);
		dev_err(&pdev->dev, "failed to get host mailbox clock: %d\n",
			ret);
		return ret;
	}

	mb->pclk_device = devm_clk_get(&pdev->dev, "pclk_mailbox_device");
	if (IS_ERR(mb->pclk_device)) {
		ret = PTR_ERR(mb->pclk_device);
		dev_err(&pdev->dev, "failed to get device mailbox clock: %d\n",
			ret);
		return ret;
	}
	eswin_mbox_prepare_clk(&pdev->dev, true);

	mb->rst = devm_reset_control_get_optional_exclusive(&pdev->dev, "rst");
	if (IS_ERR(mb->rst))
		return PTR_ERR(mb->rst);
	reset_control_reset(mb->rst);

	mb->rst_device = devm_reset_control_get_optional_exclusive(
		&pdev->dev, "rst_device");
	if (IS_ERR(mb->rst_device))
		return PTR_ERR(mb->rst_device);
	reset_control_reset(mb->rst_device);

	for (i = 0; i < mb->mbox.num_chans; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq, eswin_mbox_irq,
				eswin_mbox_isr, IRQF_ONESHOT, dev_name(&pdev->dev), mb);
		if (ret < 0)
			return ret;

		mb->chans[i].idx = i;
		mb->chans[i].irq = irq;
		mb->chans[i].mb = mb;
	}
	mb->dev = &pdev->dev;
	ret = eswin_mbox_init_regmap(mb);
	if (ret)
		return ret;

	spin_lock_init(&mb->rx_lock);

	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));

	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = devm_mbox_controller_register(&pdev->dev, &mb->mbox);
	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		dev_err(&pdev->dev, "failed to register mailbox: %d\n", ret);
	}
	dev_info(&pdev->dev, "register sucessfully\n");
	return ret;
}

static int eswin_mbox_remove(struct platform_device *pdev)
{
	int ret;
	struct eswin_mbox *mb = platform_get_drvdata(pdev);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	ret = reset_control_assert(mb->rst);
	WARN_ON(ret != 0);
	ret = reset_control_assert(mb->rst_device);
	WARN_ON(ret != 0);
	return 0;
}

__maybe_unused static int eswin_mbox_suspend(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev)) {
		return eswin_mbox_prepare_clk(dev, false);
	}
	return 0;
}

__maybe_unused static int eswin_mbox_resume(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev)) {
		eswin_mbox_prepare_clk(dev, true);
		pm_runtime_mark_last_busy(dev);
		pm_request_autosuspend(dev);
	}
	return 0;
}

__maybe_unused static int eswin_mbox_runtime_suspend(struct device *dev)
{
	return eswin_mbox_prepare_clk(dev, false);
}

__maybe_unused static int eswin_mbox_runtime_resume(struct device *dev)
{
	return eswin_mbox_prepare_clk(dev, true);
}

static const struct dev_pm_ops eswin_mbox_dev_pm_ops = {
	LATE_SYSTEM_SLEEP_PM_OPS(eswin_mbox_suspend, eswin_mbox_resume)
	RUNTIME_PM_OPS(eswin_mbox_runtime_suspend, eswin_mbox_runtime_resume, NULL)
};

static struct platform_driver eswin_mbox_driver = {
	.probe	= eswin_mbox_probe,
	.remove = eswin_mbox_remove,
	.driver = {
		.name = "eswin-mailbox",
		.of_match_table = of_match_ptr(eswin_mbox_of_match),
		.pm	= pm_ptr(&eswin_mbox_dev_pm_ops),
	},
};

module_platform_driver(eswin_mbox_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Eswin mailbox: communicate between CPU cores and MCUs");
MODULE_AUTHOR("Huang Yifeng <huangyifeng@eswincomputing.com>");
