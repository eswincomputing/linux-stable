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

#ifndef __ES_VIRTUAL_H_
#define __ES_VIRTUAL_H_

struct es_virtual_display {
	u32 enable;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device *dc;
	u32 bus_format;

	struct dentry *dump_debugfs;
	struct debugfs_blob_wrapper dump_blob;
	struct es_gem_object *dump_obj;
	unsigned int pitch;
};

static inline struct es_virtual_display *
to_virtual_display_with_connector(struct drm_connector *connector)
{
	return container_of(connector, struct es_virtual_display, connector);
}

static inline struct es_virtual_display *
to_virtual_display_with_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct es_virtual_display, encoder);
}

extern struct platform_driver virtual_display_platform_driver;
#endif /* __ES_VIRTUAL_H_ */
