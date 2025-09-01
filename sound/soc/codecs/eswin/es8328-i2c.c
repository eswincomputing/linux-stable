// SPDX-License-Identifier: GPL-2.0-only
/*
 * es8328-i2c.c  --  ES8328 ALSA SoC I2C Audio driver
 *
 * Copyright 2014 Sutajio Ko-Usagi PTE LTD
 *
 * Author: Sean Cross <xobs@kosagi.com>
 */

/*
 * Copyright (C) 2021 ESWIN, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "es8328.h"

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
	unsigned int val = 0;
	struct regmap *map;

	map = devm_regmap_init_i2c(i2c, &es8328_regmap_config);
	if (IS_ERR(map)) {
		dev_err(&i2c->dev, "failed to init regmap %ld\n", PTR_ERR(map));
		return PTR_ERR(map);
	}

	ret = regmap_read(map, ES8328_CONTROL1, &val);
	if (ret != 0) {
		dev_info(&i2c->dev, "read control1 register failed\n");
		return -EIO;
	}
	if (val != 0x06) {
		dev_info(&i2c->dev, "control1 val mismatching %d\n", val);
		return -EINVAL;
	}

	ret = regmap_read(map, ES8328_CONTROL2, &val);
	if (ret != 0) {
		dev_warn(&i2c->dev, "read control2 register failed\n");
		return -EIO;
	}
	if ((val&0x0f) != 0x0C) {
		dev_warn(&i2c->dev, "control2 val mismatching %d\n", val);
		return -EINVAL;
	}

	return es8328_probe(&i2c->dev, map);
}

static struct i2c_driver es8328_i2c_driver = {
	.driver = {
		.name		= "es8328",
		.of_match_table = es8328_of_match,
	},
	.probe    = es8328_i2c_probe,
	.id_table = es8328_id,
};

module_i2c_driver(es8328_i2c_driver);

MODULE_DESCRIPTION("ASoC ES8328 audio CODEC I2C driver");
MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_LICENSE("GPL");
