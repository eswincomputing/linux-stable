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

#ifndef __ES_DRV_H__
#define __ES_DRV_H__

#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_gem.h>
#include <drm/drm_device.h>

#include "es_plane.h"
#ifdef CONFIG_ESWIN_MMU
#include "es_dc_mmu.h"
#endif

/*
 *
 * @dma_dev: device for DMA API.
 *  - use the first attached device if support iommu
    else use drm device (only contiguous buffer support)
 * @domain: iommu domain for DRM.
 *  - all DC IOMMU share same domain to reduce mapping
 * @pitch_alignment: buffer pitch alignment required by sub-devices.
 *
 */
struct es_drm_private {
	struct device *dma_dev;
	struct iommu_domain *domain;
#ifdef CONFIG_ESWIN_DW_HDMI
	struct drm_property *connector_id_prop;
	struct drm_property *eotf_prop;
	struct drm_property *color_space_prop;
	struct drm_property *global_alpha_prop;
	struct drm_property *blend_mode_prop;
	struct drm_property *alpha_scale_prop;
	struct drm_property *async_commit_prop;
	struct drm_property *share_id_prop;
#endif

#ifdef CONFIG_ESWIN_MMU
	dc_mmu *mmu;
	bool mmu_constructed;
#endif
	unsigned int die_id;

	unsigned int pitch_alignment;
};

int es_drm_iommu_attach_device(struct drm_device *drm_dev, struct device *dev);

void es_drm_iommu_detach_device(struct drm_device *drm_dev, struct device *dev);

void es_drm_update_pitch_alignment(struct drm_device *drm_dev,
				   unsigned int alignment);

static inline struct device *to_dma_dev(struct drm_device *dev)
{
	struct es_drm_private *priv = dev->dev_private;

	return priv->dma_dev;
}

static inline bool is_iommu_enabled(struct drm_device *dev)
{
	struct es_drm_private *priv = dev->dev_private;

	return priv->domain != NULL ? true : false;
}
#endif /* __ES_DRV_H__ */
