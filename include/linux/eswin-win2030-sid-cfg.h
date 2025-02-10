// SPDX-License-Identifier: GPL-2.0
/*
 * Header file of eswin-win2030-sid.c
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
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#ifndef ESWIN_WIN2030_SID_CFG_H
#define ESWIN_WIN2030_SID_CFG_H

int win2030_dynm_sid_enable(int nid);
int win2030_aon_sid_cfg(struct device *dev);
int win2030_dma_sid_cfg(struct device *dev);
int win2030_tbu_power(struct device *dev, bool is_powerUp);
int win2030_tbu_power_by_dev_and_node(struct device *dev, struct device_node *node, bool is_powerUp);

void trigger_waveform_start(void);
void trigger_waveform_stop(void);
void print_tcu_node_status(const char *call_name, int call_line, int nid);
#endif
