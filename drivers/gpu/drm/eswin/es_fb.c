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

#include <linux/module.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "es_fb.h"
#include "es_gem.h"

static struct drm_framebuffer_funcs es_fb_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = drm_gem_fb_destroy,
	.dirty = drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *
es_fb_alloc(struct drm_device *dev, const struct drm_mode_fb_cmd2 *mode_cmd,
	    struct es_gem_object **obj, unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	int ret, i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = &obj[i]->base;

	ret = drm_framebuffer_init(dev, fb, &es_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n",
			ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

static struct drm_framebuffer *
es_fb_create(struct drm_device *dev, struct drm_file *file_priv,
	     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	struct es_gem_object *objs[MAX_NUM_PLANES];
	struct drm_gem_object *obj;
	unsigned int height, size;
	unsigned char i, num_planes;
	int ret = 0;

	info = drm_format_info(mode_cmd->pixel_format);
	if (!info)
		return ERR_PTR(-EINVAL);

	num_planes = info->num_planes;
	if (num_planes > MAX_NUM_PLANES)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < num_planes; i++) {
		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object.\n");
			ret = -ENXIO;
			goto err;
		}

		height =
			drm_format_info_plane_height(info, mode_cmd->height, i);

		size = height * mode_cmd->pitches[i] + mode_cmd->offsets[i];

		if (obj->size < size) {
			drm_gem_object_put(obj);
			ret = -EINVAL;
			goto err;
		}

		objs[i] = to_es_gem_object(obj);
	}

	fb = es_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	for (; i > 0; i--) {
		drm_gem_object_put(&objs[i - 1]->base);
	}

	return ERR_PTR(ret);
}

struct es_gem_object *es_fb_get_gem_obj(struct drm_framebuffer *fb,
					unsigned char plane)
{
	if (plane > MAX_NUM_PLANES)
		return NULL;

	return to_es_gem_object(fb->obj[plane]);
}

static const struct drm_mode_config_funcs es_mode_config_funcs = {
	.fb_create = es_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs es_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

void es_mode_config_init(struct drm_device *dev)
{
	if (dev->mode_config.max_width == 0 ||
	    dev->mode_config.max_height == 0) {
		dev->mode_config.min_width = 0;
		dev->mode_config.min_height = 0;
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	}
	dev->mode_config.funcs = &es_mode_config_funcs;
	dev->mode_config.helper_private = &es_mode_config_helpers;
}
