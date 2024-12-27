// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN hdmi driver
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

#ifndef DW_HDMI_CEC_H
#define DW_HDMI_CEC_H

struct dw_hdmi;

struct dw_hdmi_cec_ops {
	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
	void (*enable)(struct dw_hdmi *hdmi);
	void (*disable)(struct dw_hdmi *hdmi);
};

struct dw_hdmi_cec_data {
	struct dw_hdmi *hdmi;
	const struct dw_hdmi_cec_ops *ops;
	int irq;
};
#endif
