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

#include <linux/of_graph.h>
#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/version.h>
#include <linux/of_address.h>
#include <linux/dma-map-ops.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_vblank.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fbdev_generic.h>

#include "es_drv.h"
#include "es_fb.h"
#include "es_gem.h"
#include "es_plane.h"
#include "es_crtc.h"
#include "es_simple_enc.h"
#include "es_dc.h"
#include "es_virtual.h"
#ifdef CONFIG_ESWIN_DW_HDMI
#include "dw-hdmi.h"
#endif

#include "es_mipi_dsi.h"

#define DRV_NAME "es_drm"
#define DRV_DESC "Eswin DRM driver"
#define DRV_DATE "20191101"
#define DRV_MAJOR 1
#define DRV_MINOR 0

static bool has_iommu = true;
static struct platform_driver es_drm_platform_driver;

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.mmap = es_gem_mmap,
};

#ifdef CONFIG_DEBUG_FS
static int es_debugfs_planes_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_plane *plane = NULL;

	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		struct drm_plane_state *state = plane->state;
		struct es_plane_state *plane_state = to_es_plane_state(state);

		seq_printf(s, "plane[%u]: %s\n", plane->base.id, plane->name);
		seq_printf(s, "\tcrtc = %s\n",
			   state->crtc ? state->crtc->name : "(null)");
		seq_printf(s, "\tcrtc id = %u\n",
			   state->crtc ? state->crtc->base.id : 0);
		seq_printf(s, "\tcrtc-pos = " DRM_RECT_FMT "\n",
			   DRM_RECT_ARG(&plane_state->status.dest));
		seq_printf(s, "\tsrc-pos = " DRM_RECT_FP_FMT "\n",
			   DRM_RECT_FP_ARG(&plane_state->status.src));
		seq_printf(s, "\tformat = %p4cc\n",
			   state->fb ? &state->fb->format->format : NULL);
		seq_printf(s, "\trotation = 0x%x\n", state->rotation);
		seq_printf(s, "\ttiling = %u\n", plane_state->status.tile_mode);

		seq_puts(s, "\n");
	}

	return 0;
}

static struct drm_info_list es_debugfs_list[] = {
	{ "planes", es_debugfs_planes_show, 0, NULL },
};

static void es_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(es_debugfs_list, ARRAY_SIZE(es_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif

static struct drm_driver es_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM |
			   DRIVER_SYNCOBJ,
	.lastclose = drm_fb_helper_lastclose,
	.gem_prime_import = es_gem_prime_import,
	.gem_prime_import_sg_table = es_gem_prime_import_sg_table,
	.dumb_create = es_gem_dumb_create,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = es_debugfs_init,
#endif
	.fops = &fops,
	.name = DRV_NAME,
	.desc = DRV_DESC,
	.date = DRV_DATE,
	.major = DRV_MAJOR,
	.minor = DRV_MINOR,
};

int es_drm_iommu_attach_device(struct drm_device *drm_dev, struct device *dev)
{
	struct es_drm_private *priv = drm_dev->dev_private;
	int ret;

	if (!has_iommu)
		return 0;

	if (!priv->domain) {
		priv->domain = iommu_get_domain_for_dev(dev);
		if (IS_ERR(priv->domain))
			return PTR_ERR(priv->domain);
		priv->dma_dev = dev;
	}

	ret = iommu_attach_device(priv->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void es_drm_iommu_detach_device(struct drm_device *drm_dev, struct device *dev)
{
	struct es_drm_private *priv = drm_dev->dev_private;

	if (!has_iommu)
		return;

	iommu_detach_device(priv->domain, dev);

	if (priv->dma_dev == dev)
		priv->dma_dev = drm_dev->dev;
}

void es_drm_update_pitch_alignment(struct drm_device *drm_dev,
				   unsigned int alignment)
{
	struct es_drm_private *priv = drm_dev->dev_private;

	if (alignment > priv->pitch_alignment)
		priv->pitch_alignment = alignment;
}

#ifdef CONFIG_ESWIN_DW_HDMI
static int es_drm_create_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	struct es_drm_private *private = dev->dev_private;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC, "EOTF", 0,
					 5);
	if (!prop)
		return -ENOMEM;
	private->eotf_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "COLOR_SPACE", 0, 12);
	if (!prop)
		return -ENOMEM;
	private->color_space_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "GLOBAL_ALPHA", 0, 255);
	if (!prop)
		return -ENOMEM;
	private->global_alpha_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "BLEND_MODE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->blend_mode_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ALPHA_SCALE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->alpha_scale_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ASYNC_COMMIT", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->async_commit_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC, "SHARE_ID",
					 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	private->share_id_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "CONNECTOR_ID", 0, 0xf);
	if (!prop)
		return -ENOMEM;
	private->connector_id_prop = prop;

	return drm_mode_create_tv_properties(dev, 0);
}
#endif

static int es_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct es_drm_private *priv;
	int ret, id;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	static u64 dma_mask = DMA_BIT_MASK(40);
#else
	static u64 dma_mask = DMA_40BIT_MASK;
#endif
	drm_dev = drm_dev_alloc(&es_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	priv = devm_kzalloc(drm_dev->dev, sizeof(struct es_drm_private),
			    GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	priv->pitch_alignment = 64;
	priv->dma_dev = drm_dev->dev;
	priv->dma_dev->coherent_dma_mask = dma_mask;
	dma_set_mask_and_coherent(priv->dma_dev, DMA_BIT_MASK(40));

	drm_dev->dev_private = priv;

	drm_mode_config_init(drm_dev);

#ifdef CONFIG_ESWIN_DW_HDMI
	es_drm_create_properties(drm_dev);
#endif

	/* Now try and bind all our sub-components */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode;

	es_mode_config_init(drm_dev);

	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_bind;

	drm_mode_config_reset(drm_dev);

	drm_kms_helper_poll_init(drm_dev);

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_helper;

	drm_fbdev_generic_setup(drm_dev, 32);

	ret = of_property_read_u32(dev->of_node, "numa-node-id", &id);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to read index property, ret = %d\n",
			      ret);
		return ret;
	}
	DRM_INFO("drm dev is on die%d\n", id);
	priv->die_id = id;
	priv->mmu_constructed = false;

	if (drm_dev->unique) {
		sprintf(drm_dev->unique, "%d", id);
	}
	DRM_INFO("drm_dev name:%s\n", drm_dev->unique);
	return 0;

err_helper:
	drm_kms_helper_poll_fini(drm_dev);
err_bind:
	component_unbind_all(drm_dev->dev, drm_dev);
err_mode:
	drm_mode_config_cleanup(drm_dev);
	if (priv->domain)
		iommu_domain_free(priv->domain);
err_put_dev:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
	return ret;
}

static void es_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct es_drm_private *priv = drm_dev->dev_private;

	drm_dev_unregister(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(drm_dev->dev, drm_dev);
	drm_mode_config_cleanup(drm_dev);

	if (priv->domain) {
		iommu_domain_free(priv->domain);
		priv->domain = NULL;
	}

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
}

static const struct component_master_ops es_drm_ops = {
	.bind = es_drm_bind,
	.unbind = es_drm_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	// DRM_INFO("Comparing of node %pOF with %pOF\n", dev->of_node, data);
	return dev->of_node == data;
}

static int es_drm_of_component_probe(struct device *dev,
				     int (*compare_of)(struct device *, void *),
				     const struct component_master_ops *m_ops)
{
	struct device_node *ep, *port, *remote;
	struct component_match *match = NULL;
	int i;
	bool found = false;
	bool matched = false;

	if (!dev->of_node)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0;; i++) {
		struct device_node *iommu;
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent)) {
			drm_of_component_match_add(dev, &match, compare_of,
						   port->parent);
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);

		/*
         * if there is a crtc not support iommu, force set all
         * crtc use non-iommu buffer.
         */
		if (!iommu || !of_device_is_available(iommu->parent))
			has_iommu = false;

		found = true;

		of_node_put(iommu);
		of_node_put(port);
	}

	if (!found) {
		DRM_DEV_ERROR(dev, "No available DC found.\n");
		return -ENODEV;
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "no available port\n");
		return -ENODEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0;; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		for_each_child_of_node(port, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote)) {
				of_node_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(
					dev,
					"parent device of %pOF is not available\n",
					remote);
				of_node_put(remote);
				continue;
			}

#ifdef CONFIG_ESWIN_DW_HDMI
			if (!strcmp(remote->name, "hdmi")) {
				matched = true;
			}
#endif

#ifdef CONFIG_ESWIN_VIRTUAL_DISPLAY
			if (!strcmp(remote->name, "es_wb")) {
				matched = true;
			}
#endif

#ifdef CONFIG_ESWIN_MIPI_DSI
			if (!strcmp(remote->name, "mipi_dsi")) {
				matched = true;
			}
#endif
			if (matched == true) {
				drm_of_component_match_add(dev, &match,
							   compare_of, remote);
				matched = false;
				dev_dbg(dev, "matched: %pOF, remote->name:%s\n",
					 remote, remote->name);
			}

			of_node_put(remote);
		}
		of_node_put(port);
	}
	return component_master_add_with_match(dev, m_ops, match);
}

static int es_drm_platform_probe(struct platform_device *pdev)
{
	DRM_INFO("drm platform probe enter\n");

	return es_drm_of_component_probe(&pdev->dev, compare_of, &es_drm_ops);
}

static int es_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &es_drm_ops);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int es_drm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int es_drm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static SIMPLE_DEV_PM_OPS(es_drm_pm_ops, es_drm_suspend, es_drm_resume);

static const struct of_device_id es_drm_dt_ids[] = {

	{
		.compatible = "eswin,display-subsystem",
	},

	{ /* sentinel */ },

};

MODULE_DEVICE_TABLE(of, es_drm_dt_ids);

static struct platform_driver es_drm_platform_driver = {
    .probe = es_drm_platform_probe,
    .remove = es_drm_platform_remove,

    .driver = {
        .name = DRV_NAME,
        .of_match_table = es_drm_dt_ids,
        .pm = &es_drm_pm_ops,
    },
};

static struct platform_driver *const drivers[] = {
	&es_drm_platform_driver,
	/* put display control driver at start */
	&dc_platform_driver,

/* connector */

/* bridge */
#if 1
#ifdef CONFIG_ESWIN_DW_HDMI
	&dw_hdmi_eswin_pltfm_driver,
#endif
#ifdef CONFIG_DW_HDMI_I2S_AUDIO
	&snd_dw_hdmi_driver,
#endif

#ifdef CONFIG_DW_HDMI_CEC
	&dw_hdmi_cec_driver,
#endif

#ifdef CONFIG_DW_HDMI_HDCP
	&dw_hdmi_hdcp_driver,
#endif
#endif

#ifdef CONFIG_ESWIN_VIRTUAL_DISPLAY
	&virtual_display_platform_driver,
#endif

#ifdef CONFIG_ESWIN_MIPI_DSI
	&es_mipi_dsi_driver,
#endif
};

static int __init es_drm_init(void)
{
	DRM_INFO("drm init enter\n");

	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

static void __exit es_drm_fini(void)
{
	DRM_INFO("drm exit enter\n");

	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}

module_init(es_drm_init);
module_exit(es_drm_fini);

MODULE_DESCRIPTION("Eswin DRM Driver");
MODULE_LICENSE("GPL v2");
