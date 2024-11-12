// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN audio proc driver
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
 * Authors: Zack Yang <yangqiang1@eswincomputing.com>
 */

#ifndef _ES_AUDIO_PROC_H
#define _ES_AUDIO_PROC_H

int audio_proc_module_init(void);
void audio_proc_module_exit(void);

#endif
