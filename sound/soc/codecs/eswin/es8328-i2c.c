// SPDX-License-Identifier: GPL-2.0-only
/*
 * es8328-i2c.c  --  ES8328 ALSA SoC I2C Audio driver
 *
 * Copyright 2014 Sutajio Ko-Usagi PTE LTD
 *
 * Author: Sean Cross <xobs@kosagi.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>

#include "es8328.h"

extern const struct dev_pm_ops es8328_pm_ops;

static const struct i2c_device_id es8328_id[] = {
	{ "es8328", 0 },
	{ "es8388", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8328_id);

static const struct of_device_id es8328_of_match[] = {
	{ .compatible = "eswin,es8388", },
	{ }
};
MODULE_DEVICE_TABLE(of, es8328_of_match);

static int es8328_i2c_probe(struct i2c_client *i2c)
{
	int ret;

	ret = es8328_probe(&i2c->dev,
			   devm_regmap_init_i2c(i2c, &es8328_regmap_config));
	if (ret) {
		dev_err(&i2c->dev, "es8328_probe failed, ret=%d\n", ret);
		return ret;
	}

	pm_runtime_set_autosuspend_delay(&i2c->dev, 2000);
	pm_runtime_use_autosuspend(&i2c->dev);
	pm_runtime_set_active(&i2c->dev);
	pm_runtime_enable(&i2c->dev);
	pm_runtime_mark_last_busy(&i2c->dev);

	return 0;
}

static void es8328_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
}


static struct i2c_driver es8328_i2c_driver = {
	.driver = {
		.name		= "es8328",
		.of_match_table = es8328_of_match,
		.pm	= pm_sleep_ptr(&es8328_pm_ops),
	},
	.probe    = es8328_i2c_probe,
	.remove   = es8328_i2c_remove,
	.id_table = es8328_id,
};

module_i2c_driver(es8328_i2c_driver);

MODULE_DESCRIPTION("ASoC ES8328 audio CODEC I2C driver");
MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_LICENSE("GPL");
