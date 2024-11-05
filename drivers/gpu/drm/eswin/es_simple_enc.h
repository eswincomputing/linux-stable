// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN drm driver
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
 * Authors: Eswin Driver team
 */

#ifndef __ES_SIMPLE_ENC_H_
#define __ES_SIMPLE_ENC_H_

struct simple_encoder_priv {
	unsigned char encoder_type;
};

struct dss_data {
	u32 mask;
	u32 value;
};

struct simple_encoder {
	struct drm_encoder encoder;
	struct device *dev;
	const struct simple_encoder_priv *priv;
	struct regmap *dss_regmap;
	struct dss_data *dss_regdatas;
};

extern struct platform_driver simple_encoder_driver;
#endif /* __ES_SIMPLE_ENC_H_ */
