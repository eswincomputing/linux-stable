// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN vi ircut driver
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

#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <linux/version.h>
#include <linux/es-camera-module.h>
#include "eswin_vi.h"

#define ES_IRCUT_NAME "ircut"
#define NUM_PADS 1

enum IRCUT_STATE_ENUM {
	IRCUT_STATE_CLOSED,
	IRCUT_STATE_CLOSING,
	IRCUT_STATE_OPENING,
	IRCUT_STATE_OPENED,
};

struct ircut_op_work {
	struct work_struct work;
	int op_cmd;
	struct ircut_dev *dev;
};

struct ircut_drv_data {
	int	(*parse_dt)(struct ircut_dev *ircut, struct device_node *node);
	void (*ctrl)(struct ircut_dev *ircut, int cmd);
};

struct ircut_dev {
	struct v4l2_subdev sd;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct device *dev;
	struct completion complete;
	struct mutex mut_state;
	enum IRCUT_STATE_ENUM state;
	struct workqueue_struct *wq;
	int pulse_width;
	int val;
	struct gpio_desc *ircut_gpio;
	u32 module_index;
	const char *module_facing;
	const struct ircut_drv_data *drv_data;
	struct media_pad pad[NUM_PADS];

};

static inline struct ircut_dev *to_ircut(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct ircut_dev, sd);
}

#define IRCUT_STATE_EQ(expected) \
	((ircut->state == (expected)) ? true : false)

static int rpi_parse_dt(struct ircut_dev *ircut, struct device_node *node)
{
	int ret;

	ret = of_property_read_u32(node, "eswin,pulse-width", &ircut->pulse_width);
	if (ret != 0) {
		ircut->pulse_width = 100;
		dev_warn(ircut->dev, "failed get pulse-width,use dafult value 100\n");
	}
	if (ircut->pulse_width > 2000) {
		ircut->pulse_width = 300;
		dev_warn(ircut->dev, "pulse width to long,use default dafult 300");
	}
	dev_dbg(ircut->dev, "pulse-width value from dts %d\n", ircut->pulse_width);
	/* get ircut open gpio */
	ircut->ircut_gpio = devm_gpiod_get(ircut->dev, "ircut", GPIOD_OUT_LOW);
	if (IS_ERR(ircut->ircut_gpio)) {
		dev_err(ircut->dev, "Failed to get ircut-gpios\n");
		return PTR_ERR(ircut->ircut_gpio);
	}

	dev_dbg(ircut->dev, "parse ircut-dts ok\n");

	return 0;
}

static void rpi_ctrl(struct ircut_dev *ircut, int cmd)
{
	if (cmd > 0) {
		gpiod_set_value_cansleep(ircut->ircut_gpio, 1);
		msleep(ircut->pulse_width);
	} else {
		gpiod_set_value_cansleep(ircut->ircut_gpio, 0);
		msleep(ircut->pulse_width);
	}
	dev_dbg(ircut->dev, "set ircut ctrl done \n");
}

static void ircut_op_work(struct work_struct *work)
{
	struct ircut_op_work *wk =
		container_of(work, struct ircut_op_work, work);
	struct ircut_dev *ircut = wk->dev;
	enum IRCUT_STATE_ENUM state;

	if (ircut->drv_data->ctrl)
		ircut->drv_data->ctrl(ircut, wk->op_cmd);

	state = (wk->op_cmd > 0) ? IRCUT_STATE_OPENED : IRCUT_STATE_CLOSED;
	mutex_lock(&ircut->mut_state);
	complete(&ircut->complete);
	ircut->state = state;
	mutex_unlock(&ircut->mut_state);
	kfree(wk);
	wk = NULL;
}

static int ircut_operation(struct ircut_dev *ircut, int op)
{
	struct ircut_op_work *wk = NULL;
	bool should_wait = false;
	bool do_nothing = false;
	enum IRCUT_STATE_ENUM old_state;

	dev_dbg(ircut->dev, "%s, status %d\n", __func__, op);
	mutex_lock(&ircut->mut_state);
	old_state = ircut->state;
	if (op > 0) {
		if (IRCUT_STATE_EQ(IRCUT_STATE_OPENING) ||
			IRCUT_STATE_EQ(IRCUT_STATE_OPENED)) {
			/* already in opening or opened state, do nothing */
			do_nothing = true;
		} else if (IRCUT_STATE_EQ(IRCUT_STATE_CLOSING)) {
			/* in closing state,should wait */
			should_wait = true;
		}
	} else {
		/* check state */
		if (IRCUT_STATE_EQ(IRCUT_STATE_CLOSING) ||
			IRCUT_STATE_EQ(IRCUT_STATE_CLOSED)) {
			/* already in closing or closed state, do nothing */
			do_nothing = true;
		} else if (IRCUT_STATE_EQ(IRCUT_STATE_OPENING)) {
			/* in opening state,should wait */
			should_wait = true;
		}
	}
	mutex_unlock(&ircut->mut_state);
	if (do_nothing)
		goto op_done;
	if (should_wait)
		wait_for_completion(&ircut->complete);
	wk = kmalloc(sizeof(*wk), GFP_KERNEL);
	if (!wk) {
		dev_err(ircut->dev, "failed to alloc ircut work struct\n");
		goto err_ircut_operation;
	}
	wk->op_cmd = op;
	wk->dev = ircut;
	mutex_lock(&ircut->mut_state);
	if (op > 0)
		ircut->state = IRCUT_STATE_OPENING;
	else
		ircut->state = IRCUT_STATE_CLOSING;
	reinit_completion(&ircut->complete);
	mutex_unlock(&ircut->mut_state);
	/* queue work */
	INIT_WORK(&wk->work, ircut_op_work);
	if (!queue_work(ircut->wq, &wk->work)) {
		dev_err(ircut->dev, "queue work failed\n");
		kfree(wk);
		ircut->state = old_state;
		goto err_ircut_operation;
	}
op_done:
	return 0;
err_ircut_operation:
	return -1;
}
static int ircut_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = -EINVAL;
	struct ircut_dev *ircut = container_of(ctrl->handler,
					     struct ircut_dev, ctrl_handler);

	if (ctrl->id == V4L2_CID_BAND_STOP_FILTER) {
		ret = ircut_operation(ircut, ctrl->val);
		if (ret == 0)
			ircut->val = ctrl->val;
	}
	return ret;
}

static long ircut_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	return -EINVAL;
}

static const struct v4l2_subdev_core_ops ircut_core_ops = {
	.ioctl = ircut_ioctl,
};

static const struct v4l2_subdev_ops ircut_subdev_ops = {
	.core	= &ircut_core_ops,
};

static const struct v4l2_ctrl_ops ircut_ctrl_ops = {
	.s_ctrl = ircut_s_ctrl,
};

static int ircut_ctrl_init(struct ircut_dev *ircut)
{
	int ret;
	struct v4l2_ctrl_handler *ctrl_handler;
	
	ctrl_handler = &ircut->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_handler, 1);
	if (ret)
		return ret;

	v4l2_ctrl_new_std(ctrl_handler, &ircut_ctrl_ops,
		V4L2_CID_BAND_STOP_FILTER, 0, 1, 1, IRCUT_STATE_CLOSED);

	if (ctrl_handler->error) {
		ret = ctrl_handler->error;
		dev_err(ircut->dev, "Failed to init controls(%d)\n", ret);
		goto err;
	}

	ircut->sd.ctrl_handler = ctrl_handler;

	/* set default state to close */
	ret = ircut_operation(ircut, IRCUT_STATE_CLOSED);
	if (ret)
		goto err;
	
	ircut->val = IRCUT_STATE_CLOSED;
	return 0;
err:
	v4l2_ctrl_handler_free(ctrl_handler);
	return ret;
}

static const struct ircut_drv_data rpi_drv_data = {
	.parse_dt	= rpi_parse_dt,
	.ctrl		= rpi_ctrl,
};

#if defined(CONFIG_OF)
static const struct of_device_id ircut_of_match[] = {
	{
		.compatible = "eswin,ircut",
		.data = &rpi_drv_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ircut_of_match);
#endif

static int ircut_probe(struct platform_device *pdev)
{
	struct ircut_dev *ircut = NULL;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device *parent = pdev->dev.parent;
	struct eswin_vi_device* es_vi_dev;
	struct v4l2_subdev *sd;
	int ret = 0;

	es_vi_dev = dev_get_drvdata(parent);

	ircut = devm_kzalloc(&pdev->dev, sizeof(*ircut), GFP_KERNEL);
	if (!ircut) {
		dev_err(&pdev->dev, "alloc ircut failed\n");
		return -ENOMEM;
	}

	match = of_match_node(ircut_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	ircut->drv_data = match->data;

	ret = of_property_read_u32(node, ESMODULE_CAMERA_MODULE_INDEX,
				   &ircut->module_index);

	if (ret) {
		dev_err(&pdev->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	ircut->dev = &pdev->dev;

	mutex_init(&ircut->mut_state);
	init_completion(&ircut->complete);

	ircut->wq = alloc_workqueue("ircut wq", WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_FREEZABLE, 1);
	if(!ircut->wq)
		return -ENOMEM;

	if (ircut->drv_data->parse_dt) {
		ret = ircut->drv_data->parse_dt(ircut, node);
		if (ret)
			goto error_free_wq;
	}

	ircut->v4l2_dev.mdev = es_vi_dev->media_dev;
	snprintf(ircut->v4l2_dev.name, sizeof(ircut->sd.name), "es_ircut");
	ret = v4l2_device_register(&pdev->dev, &ircut->v4l2_dev);
	if(ret) {
		dev_err(ircut->dev, "Fail to register v4l2 device, ret %d \n", ret);
		goto error_free_wq;
	}

	v4l2_subdev_init(&ircut->sd, &ircut_subdev_ops);
	snprintf(ircut->sd.name, sizeof(ircut->sd.name), "ircut_camera-%d",
		 ircut->module_index);
	ircut->sd.owner = pdev->dev.driver->owner;
	ircut->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	ircut->sd.dev = &pdev->dev;
	v4l2_set_subdevdata(&ircut->sd, pdev);
	platform_set_drvdata(pdev, &ircut->sd);

	ret = ircut_ctrl_init(ircut);
	if(ret)
		goto error_v4l2_device_unregister;

	ircut->sd.entity.function = MEDIA_ENT_F_LENS;
	ircut->sd.entity.flags = 1;
	ircut->pad[0].flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&ircut->sd.entity, 1, ircut->pad);
	if (ret < 0)
		goto error_handler_free;

	ret = v4l2_device_register_subdev(&ircut->v4l2_dev, &ircut->sd);
	if (ret) {
		dev_err(ircut->dev, "Failed to register subdev to v4l2 dev, ret = %d\n", ret);
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev(&ircut->sd);
	if (ret) {
		dev_err(ircut->dev, "v4l2 async register subdev failed, ret = %d\n", ret);
		goto error_v4l2_subdev_unregister;
	}

	v4l2_device_register_subdev_nodes(&ircut->v4l2_dev);
	if(ret) {
		dev_err(ircut->dev, "Failed to register subdev nodes, ret = %d \n", ret);
		goto error_subdev_cleanup;
	}

	dev_info(ircut->dev, "probe successful!");
	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&ircut->sd);
error_v4l2_subdev_unregister:
	v4l2_device_unregister_subdev(&ircut->sd);
error_media_entity:
	media_entity_cleanup(&ircut->sd.entity);
error_handler_free:
	v4l2_ctrl_handler_free(&ircut->ctrl_handler);
error_v4l2_device_unregister:
	v4l2_device_unregister(&ircut->v4l2_dev);
error_free_wq:
	destroy_workqueue(ircut->wq);
	return ret;
}

static int ircut_drv_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct ircut_dev *ircut = to_ircut(sd);

	drain_workqueue(ircut->wq);
	destroy_workqueue(ircut->wq);

	v4l2_device_unregister_subdev(&ircut->sd);
	v4l2_ctrl_handler_free(&ircut->ctrl_handler);
	media_entity_cleanup(&ircut->sd.entity);
	v4l2_device_unregister(&ircut->v4l2_dev);
	v4l2_subdev_cleanup(&ircut->sd);

	return 0;
}

static int __maybe_unused ircut_suspend(struct device *dev)
{
	//do nothing
	return 0;
}

static int __maybe_unused ircut_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ircut_dev *ircut = to_ircut(sd);
	//restore ircut
	if(IRCUT_STATE_EQ(IRCUT_STATE_OPENED)) {
		gpiod_set_value_cansleep(ircut->ircut_gpio, 1);
		msleep(ircut->pulse_width);
	} else {
		gpiod_set_value_cansleep(ircut->ircut_gpio, 0);
		msleep(ircut->pulse_width);
	}

	return 0;
}

static const struct dev_pm_ops ircut_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ircut_suspend, ircut_resume)
};

static struct platform_driver ircut_driver = {
	.driver = {
		.name = ES_IRCUT_NAME,
		.of_match_table = of_match_ptr(ircut_of_match),
		.pm = pm_sleep_ptr(&ircut_pm_ops),
	},
	.probe = ircut_probe,
	.remove = ircut_drv_remove,
};

module_platform_driver(ircut_driver);

MODULE_AUTHOR("ESWIN VI TEAM");
MODULE_DESCRIPTION("ESWIN ir-cut driver");
MODULE_LICENSE("GPL v2");
