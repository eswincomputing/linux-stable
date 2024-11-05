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

#ifndef __ES_DRM_H__
#define __ES_DRM_H__

#include <drm/drm.h>

enum drm_es_degamma_mode {
	ES_DEGAMMA_DISABLE = 0,
	ES_DEGAMMA_BT709 = 1,
	ES_DEGAMMA_BT2020 = 2,
};

enum drm_es_sync_dc_mode {
	ES_SINGLE_DC = 0,
	ES_MULTI_DC_PRIMARY = 1,
	ES_MULTI_DC_SECONDARY = 2,
};

enum drm_es_mmu_prefetch_mode {
	ES_MMU_PREFETCH_DISABLE = 0,
	ES_MMU_PREFETCH_ENABLE = 1,
};

#endif /* __ES_DRM_H__ */