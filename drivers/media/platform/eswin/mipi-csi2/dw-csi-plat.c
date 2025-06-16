// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 *
 * Synopsys DesignWare MIPI CSI-2 Host controller driver.
 * Platform driver
 *
 * Author: Luis Oliveira <luis.oliveira@synopsys.com>
 */

#include <media/eswin/dw-csi-data.h>
#include <media/eswin/dw-dphy-data.h>

#include "dw-csi-plat.h"


void __iomem *csc;
void __iomem *demo;

static inline struct dw_csi *sd_to_dwc_mipi_csi2h(
						struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct dw_csi, sd);
}

static inline struct dw_csi *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct dw_csi, sd);
}

static struct csi2_sensor_info *sd_to_sensor(struct dw_csi *csi2,
					     struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < csi2->num_sensors; ++i)
		if (csi2->sensors[i].sd == sd)
			return &csi2->sensors[i];

	return NULL;
}

static struct mipi_fmt *
find_dw_mipi_csi_format(struct v4l2_mbus_framefmt *mf)
{
	unsigned int i;

	pr_info("%s entered mbus: 0x%x\n", __func__, mf->code);

	for (i = 0; i < ARRAY_SIZE(dw_mipi_csi_formats); i++)
		if (mf->code == dw_mipi_csi_formats[i].mbus_code) {
			pr_info("Found mbus 0x%x\n", dw_mipi_csi_formats[i].mbus_code);
			return &dw_mipi_csi_formats[i];
		}
	return NULL;
}

static int dw_mipi_csi_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *cfg,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = dw_mipi_csi_formats[code->index].mbus_code;
	return 0;
}

static struct mipi_fmt *
dw_mipi_csi_try_format(struct v4l2_mbus_framefmt *mf)
{
	struct mipi_fmt *fmt;

	fmt = find_dw_mipi_csi_format(mf);
	if (!fmt)
		fmt = &dw_mipi_csi_formats[0];

	mf->code = fmt->mbus_code;

	return fmt;
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[DWC_CSI2_PAD_SINK];
	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_pad_remote_pad_first(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct v4l2_mbus_framefmt *
dw_mipi_csi_get_format(struct dw_csi *dev, struct v4l2_subdev_state *cfg,
		       enum v4l2_subdev_format_whence which)
{
	pr_info("%s entered\n", __func__);
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return cfg ? v4l2_subdev_get_try_format(&dev->sd, cfg, 0) : NULL;
	dev_info(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev->format.code);
	dev_info(dev->dev,
		"%s got width. 0x%x\n", __func__,
		dev->format.width);
	dev_info(dev->dev,
		"%s got height. 0x%x\n", __func__,
		dev->format.height);
	return &dev->format;
}

static int
dw_mipi_csi_set_fmt(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *cfg,
		    struct v4l2_subdev_format *fmt)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct mipi_fmt *dev_fmt = NULL;
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	pr_info("%s entered\n", __func__);

	dev_info(dev->dev,
		"%s got mbus_pixelcode. 0x%x\n", __func__,
		fmt->format.code);

	dev_fmt = dw_mipi_csi_try_format(&fmt->format);
	dev_info(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev_fmt->mbus_code);
	if (!fmt)
		return -EINVAL;

	if (dev_fmt) {
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			dev->fmt = dev_fmt;
		dev->fmt->mbus_code = mf->code;
		dw_mipi_csi_set_ipi_fmt(dev);
	}

	return 0;
}

static int
dw_mipi_csi_get_fmt(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *cfg,
		    struct v4l2_subdev_format *fmt)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = dw_mipi_csi_get_format(dev, cfg, fmt->which);
	if (!mf)
		return -EINVAL;

	mutex_lock(&dev->lock);
	fmt->format = *mf;
	mutex_unlock(&dev->lock);

	return 0;
}

static int dwc_mipi_csi2_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			      struct v4l2_mbus_config *mbus)
{
	struct dw_csi *csi2 = sd_to_dwc_mipi_csi2h(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	int ret;

	ret = v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0, mbus);
	if (ret) {
		mbus->type = V4L2_MBUS_CSI2_DPHY;
		mbus->bus.mipi_csi2.flags = csi2->bus.flags;
		mbus->bus.mipi_csi2.flags |= BIT(csi2->bus.num_data_lanes - 1);
	}

	return 0;
}

static struct v4l2_rect *
mipi_csi2_get_crop(struct dw_csi *csi2h, struct v4l2_subdev_state *sd_state,
		   enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&csi2h->sd, sd_state,
						DWC_CSI2_PAD_SINK);
	else
		return &csi2h->crop;
}
static int dwc_mipi_csi2_get_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_selection *sel)
{
	struct dw_csi *csi2h = sd_to_dwc_mipi_csi2h(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	struct v4l2_subdev_format fmt;
	int ret = 0;

	if (!sel) {
		pr_err("sel is null\n");
		goto err;
	}

	if (sel->pad > DWC_CSI2X_PAD_SOURCE3) {
		pr_err("pad[%d] isn't matched\n",
			 sel->pad);
		goto err;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			sel->pad = 0;
			ret = v4l2_subdev_call(sensor, pad, get_selection,
					       sd_state, sel);
			if (ret) {
				fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
				fmt.pad = 0;
				ret = v4l2_subdev_call(sensor, pad, get_fmt,
						       NULL, &fmt);
				if (!ret) {
					csi2h->format_mbus = fmt.format;
					sel->r.top = 0;
					sel->r.left = 0;
					sel->r.width = csi2h->format_mbus.width;
					sel->r.height =
						csi2h->format_mbus.height;
					csi2h->crop = sel->r;
				} else {
					sel->r = csi2h->crop;
				}
			} else {
				csi2h->crop = sel->r;
			}
		} else {
			sel->r = *v4l2_subdev_get_try_crop(&csi2h->sd, sd_state,
							   sel->pad);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *mipi_csi2_get_crop(csi2h, sd_state, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
err:
	return -EINVAL;
}

static int dwc_mipi_csi2_set_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_selection *sel)
{
	struct dw_csi *csi2h = sd_to_dwc_mipi_csi2h(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	int ret = 0;

	ret = v4l2_subdev_call(sensor, pad, set_selection, sd_state, sel);
	if (!ret)
		csi2h->crop = sel->r;

	return ret;
}

static int
dw_mipi_csi_log_status(struct v4l2_subdev *sd)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dw_mipi_csi_dump(dev);

	return 0;
}

#if 0
static void dw_mipi_csi_start_phy(struct dw_csi *csi_dev)
{
	int ret = 0;
	pr_info("%s:%d sensor name %s\n", __func__, __LINE__, csi_dev->sensors[0].sd->name);
	ret = v4l2_subdev_call(csi_dev->sensors[0].sd, video, s_stream, 1);
	if (ret) {
		dev_err(csi_dev->dev, "Failed to start dphy: %d\n", ret);
	}
}
#endif

static int
dw_mipi_csi_s_power(struct v4l2_subdev *sd, int on)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dev_info(dev->dev, "%s: on=%d\n", __func__, on);

	if (on) {
		dw_mipi_csi_hw_stdby(dev);
		dw_mipi_csi_start(dev);
		dw_mipi_csi_log_status(sd);
	} else {
#ifdef DWC_PHY_USING
		phy_power_off(dev->phy);
#endif
		dw_mipi_csi_mask_irq_power_off(dev);
	}
	return 0;
}



#if IS_ENABLED(CONFIG_VIDEO_ADV_DEBUG)
static int
dw_mipi_csi_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dev_vdbg(dev->dev, "%s: reg=%llu\n", __func__, reg->reg);
	reg->val = dw_mipi_csi_read(dev, reg->reg);

	return 0;
}
#endif
static int dw_mipi_csi_init_cfg(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *cfg)
{
	struct v4l2_mbus_framefmt *format =
	    v4l2_subdev_get_try_format(sd, cfg, 0);

	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = MEDIA_BUS_FMT_RGB888_1X24;
	format->field = V4L2_FIELD_NONE;

	return 0;
}

static int dw_mipi_set_stream(struct v4l2_subdev *sd, int enable)
{
	return dw_mipi_csi_s_power(sd, enable);
}

#define DW_MIPI_STREAM_CMD _IOWR('u', 0x20, int)

long dw_mipi_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case DW_MIPI_STREAM_CMD: {
		int *enable = arg;
		dw_mipi_csi_s_power(sd, *enable);
		break;
	}
	default:
		break;
	}

	return 0;
}

static struct v4l2_subdev_core_ops dw_mipi_csi_core_ops = {
	.s_power = dw_mipi_csi_s_power,
	.log_status = dw_mipi_csi_log_status,
	.ioctl = dw_mipi_ioctl,
#if IS_ENABLED(CONFIG_VIDEO_ADV_DEBUG)
	.g_register = dw_mipi_csi_g_register,
#endif
};

static struct v4l2_subdev_pad_ops dw_mipi_csi_pad_ops = {
	.init_cfg = dw_mipi_csi_init_cfg,
	.enum_mbus_code = dw_mipi_csi_enum_mbus_code,
	.get_fmt = dw_mipi_csi_get_fmt,
	.set_fmt = dw_mipi_csi_set_fmt,
	.get_mbus_config 	 = dwc_mipi_csi2_g_mbus_config,
	.get_selection 		 = dwc_mipi_csi2_get_selection,
	.set_selection 		 = dwc_mipi_csi2_set_selection,
};

static const struct v4l2_subdev_video_ops dw_mipi_video_ops = {
	.s_stream = dw_mipi_set_stream,
};

static struct v4l2_subdev_ops dw_mipi_csi_subdev_ops = {
	.core = &dw_mipi_csi_core_ops,
	.pad = &dw_mipi_csi_pad_ops,
	.video = &dw_mipi_video_ops,
};

static irqreturn_t dw_mipi_csi_irq1(int irq, void *dev_id)
{
	struct dw_csi *csi_dev = dev_id;

	dw_mipi_csi_irq_handler(csi_dev);

	return IRQ_HANDLED;
}

static int
dw_mipi_csi_parse_dt(struct platform_device *pdev, struct dw_csi *dev)
{
	struct device_node *node = pdev->dev.of_node;
	// struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	int ret = 0;

	// struct fwnode_handle *ep = NULL;
	// struct v4l2_async_connection *s_asd = NULL;
	// struct fwnode_handle *remote_ep = NULL;
	// struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };

	if (of_property_read_u32(node, "snps,output-type", &dev->hw.output))
		dev->hw.output = 2;

	if (of_property_read_u32(node, "snps,en-ppi-width", &dev->hw.ppi_width))
		dev->hw.ppi_width = 0;

	if (dev->hw.ppi_width > 1) {
		dev_err(&pdev->dev, "Wrong ppi width(%d), valid value is '0'(ppi8) or '1'(ppi16)\n",
				dev->hw.ppi_width);
		return -EINVAL;
	}

	if (of_property_read_u32(node, "snps,en-phy-mode", &dev->hw.phy_mode))
		dev->hw.phy_mode = 0;

	if (of_property_read_u32(node, "ipi2_en", &dev->hw.ipi2_en))
		dev->hw.ipi2_en = 0;

	if (of_property_read_u32(node, "ipi2_vcid", &dev->hw.ipi2_vcid))
		dev->hw.ipi2_vcid = 0;

	if (of_property_read_u32(node, "ipi3_en", &dev->hw.ipi3_en))
		dev->hw.ipi3_en = 0;

	if (of_property_read_u32(node, "ipi3_vcid", &dev->hw.ipi3_vcid))
		dev->hw.ipi3_vcid = 0;

	if (of_property_read_u32(node, "num_lanes", &dev->hw.num_lanes))
		dev->hw.num_lanes = 2;

	if (of_property_read_u32(node, "index", &dev->index))
		dev->index = 0;

	node = of_graph_get_next_endpoint(node, NULL);
	if (!node) {
		dev_err(&pdev->dev, "No port node at %pOF\n",
			pdev->dev.of_node);
		return -EINVAL;
	}

	/* Get port node and validate MIPI-CSI channel id. */
	// ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &ep);
	// pr_info("%s:%d (node)->name %s \n", __func__, __LINE__, node->name);


	// if (ret)
	// 	goto err;
	// dev->index = vep.base.port - 1;
	// // dev->index = ep.base.port - 1;
	// pr_info("%s:%d dev->index %d \n", __func__, __LINE__, dev->index);
	// if (dev->index >= CSI_MAX_ENTITIES) {
	// 	ret = -ENXIO;
	// 	goto err;
	// }
	// dev->hw.num_lanes = 2;//vep.bus.mipi_csi2.num_data_lanes;

// err:
// 	pr_info("%s:%d \n", __func__, __LINE__);


	of_node_put(node);
	return ret;
}

static const struct of_device_id dw_mipi_csi_of_match[];

static int csi2_notifier_bound(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd,
			       struct v4l2_async_connection *asd)
{
	struct dw_csi *csi2 =
		container_of(notifier, struct dw_csi, notifier);
	struct csi2_sensor_info *sensor;
	struct media_link *link;
	unsigned int pad, ret;

	if (csi2->num_sensors == ARRAY_SIZE(csi2->sensors)) {
		v4l2_err(&csi2->sd, "%s: the num of sd is beyond:%d\n",
			 __func__, csi2->num_sensors);
		return -EBUSY;
	}
	pr_debug("%s:%d csi2->num_sensors %d\n", __func__, __LINE__, csi2->num_sensors);
	sensor = &csi2->sensors[csi2->num_sensors++];
	sensor->sd = sd;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;
	pr_debug("%s:%d sensor->sd->name %s\n", __func__, __LINE__, sensor->sd->name);
	pr_debug("%s:%d sensor->sd->entity->name %s\n", __func__, __LINE__, sensor->sd->entity.name);
	
	if (pad == sensor->sd->entity.num_pads) {
		dev_err(csi2->dev, "failed to find src pad for %s\n", sd->name);

		return -ENXIO;
	}

//TODO 为什么这里创建了link，但是没有enable
	ret = media_create_pad_link(
		&sensor->sd->entity, pad, &csi2->sd.entity, DWC_CSI2_PAD_SINK,
		0 /* csi2->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED */);
	if (ret) {
		dev_err(csi2->dev, "failed to create link for %s\n", sd->name);
		return ret;
	}

	link = list_first_entry(&csi2->sd.entity.links, struct media_link,
				list);
	ret = media_entity_setup_link(link, MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2->dev, "failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void csi2_notifier_unbind(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_connection *asd)
{
	struct dw_csi *csi2 =
		container_of(notifier, struct dw_csi, notifier);
	struct csi2_sensor_info *sensor = sd_to_sensor(csi2, sd);

	if (sensor)
		sensor->sd = NULL;
	pr_info("t1 %s, %d succsess\n", __func__, __LINE__);
}

static const struct v4l2_async_notifier_operations csi2_async_ops = {
	.bound = csi2_notifier_bound,
	.unbind = csi2_notifier_unbind,
};

// /* Parse fwnode with port0, if an empty function is used, each node will parse
//  * all ports, causing the device to repeatedly join the link and unable to
//  * complete the link
//  */
static int csi2_fwnode_parse(struct dw_csi *csi2)
{
	struct device *dev = csi2->dev;
	struct fwnode_handle *ep = NULL;
	struct v4l2_async_connection *s_asd = NULL;
	struct fwnode_handle *remote_ep = NULL;
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	int ret = 0;

	fwnode_graph_for_each_endpoint(dev_fwnode(dev), ep) {
		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret)
			goto err_parse;

		/* only add fwnode form port 0 to notifier list */
		if (vep.base.port != 0)
			continue;

		remote_ep = fwnode_graph_get_remote_port_parent(ep);

		/* skip device dts status is disabled */
		if (!fwnode_device_is_available(remote_ep)) {
			fwnode_handle_put(remote_ep);
			continue;
		}

		fwnode_handle_put(remote_ep);

		v4l2_async_subdev_nf_init(&csi2->notifier, &csi2->sd);

		s_asd = v4l2_async_nf_add_fwnode_remote(
			&csi2->notifier, ep, struct v4l2_async_connection);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}
	}
	return 0;

err_parse:
	fwnode_handle_put(ep);
	return ret;
}

static int csi2_notifier(struct dw_csi *csi2)
{
	// struct v4l2_async_notifier *ntf = &csi2->notifier;
	int ret;

	ret = csi2_fwnode_parse(csi2);
	if (ret < 0)
		return ret;
	csi2->sd.subdev_notifier = &csi2->notifier;
	csi2->notifier.ops = &csi2_async_ops;
	ret = v4l2_async_nf_register(&csi2->notifier);
	if (ret) {
		dev_err(csi2->dev, "fail to register async notifier: %d\n",
			ret);
		v4l2_async_nf_cleanup(&csi2->notifier);
	}
	ret = v4l2_async_register_subdev(&csi2->sd);
	return ret;
}


static int dw_csi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id = NULL;
	struct dw_csih_pdata *pdata = NULL;
	struct device *dev = &pdev->dev;
	struct dw_csi *csi;
	struct v4l2_subdev *sd;
	struct device *parent = pdev->dev.parent;
	struct eswin_vi_device* es_vi_dev;

	int ret;
	
	es_vi_dev = dev_get_drvdata(parent);

	dev_info(dev, "DW MIPI CSI-2 Host Probe in. \n");

	if (!IS_ENABLED(CONFIG_OF))
		pdata = pdev->dev.platform_data;

	dev_vdbg(dev, "Probing started\n");

	/* Resource allocation */
	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	mutex_init(&csi->lock);
	spin_lock_init(&csi->slock);
	csi->dev = dev;

	if (dev->of_node) {
		of_id = of_match_node(dw_mipi_csi_of_match, dev->of_node);
		if (!of_id)
			return -EINVAL;

		ret = dw_mipi_csi_parse_dt(pdev, csi);
		if (ret < 0)
			return ret;

#ifdef DWC_PHY_USING
		csi->phy = devm_of_phy_get(dev, dev->of_node, NULL);
		if (IS_ERR(csi->phy)) {
			dev_err(dev, "No DPHY available\n");
			return PTR_ERR(csi->phy);
		}
#endif
	} else {
#ifdef DWC_PHY_USING
		csi->phy = devm_phy_get(dev, phys[pdata->id].name);
		if (IS_ERR(csi->phy)) {
			dev_err(dev, "No '%s' DPHY available\n",
				phys[pdata->id].name);
			return PTR_ERR(csi->phy);
		}
		dev_info(dev, "got D-PHY %s with id %d\n", phys[pdata->id].name,
			 csi->phy->id);
#endif
	}


	/* Registers mapping */
	csi->base_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->base_address)) {
		dev_err(dev, "base address not set.\n");
		return PTR_ERR(csi->base_address);
	}

	csi->ctrl_irq_number = platform_get_irq(pdev, 0);
	if (csi->ctrl_irq_number < 0) {
		dev_err(dev, "irq number %d not set.\n", csi->ctrl_irq_number);
		ret = csi->ctrl_irq_number;
		goto end;
	}

	csi->rst = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(csi->rst)) {
		dev_err(dev, "error getting reset control %d\n", ret);
		return PTR_ERR(csi->rst);
	}

	ret = devm_request_irq(dev, csi->ctrl_irq_number, dw_mipi_csi_irq1,
			       IRQF_SHARED, dev_name(dev), csi);
	if (ret) {
		if (dev->of_node)
			dev_err(dev, "irq csi %s failed\n", of_id->name);
		else
			dev_err(dev, "irq csi %d failed\n", pdata->id);

		goto end;
	}

	csi->v4l2_dev.mdev = es_vi_dev->media_dev;
	ret = v4l2_device_register(dev, &csi->v4l2_dev);

	sd = &csi->sd;
	v4l2_subdev_init(sd, &dw_mipi_csi_subdev_ops);
	sd->dev = dev;
	csi->sd.owner = THIS_MODULE;

	if (dev->of_node) {
		snprintf(sd->name, sizeof(sd->name), "%s.%d",
			 "dw-csi", csi->index);
	} else {
		strlcpy(sd->name, dev_name(dev), sizeof(sd->name));
	}
	csi->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	csi->fmt = &dw_mipi_csi_formats[0];
	csi->format.code = dw_mipi_csi_formats[0].mbus_code;

	sd->entity.function = MEDIA_ENT_F_IO_V4L;

	if (dev->of_node) {
		csi->pads[CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
		csi->pads[CSI_PAD_SOURCE0].flags = MEDIA_PAD_FL_SOURCE;
		csi->pads[CSI_PAD_SOURCE1].flags = MEDIA_PAD_FL_SOURCE;

		ret = media_entity_pads_init(&csi->sd.entity, CSI_PADS_NUM, csi->pads);
		if (ret < 0) {
			dev_err(dev, "media entity init failed\n");
			goto end;
		}
	} else {
		csi->hw.num_lanes = pdata->lanes;
		csi->hw.pclk = pdata->pclk;
		csi->hw.fps = pdata->fps;
		csi->hw.dphy_freq = pdata->hs_freq;
	}

	v4l2_set_subdevdata(&csi->sd, pdev);
	platform_set_drvdata(pdev, &csi->sd);
	dev_set_drvdata(dev, sd);

	if (csi->rst)
		reset_control_deassert(csi->rst);

#if IS_ENABLED(CONFIG_DWC_MIPI_TC_DPHY_GEN3)
	dw_csi_create_capabilities_sysfs(pdev);
#endif

	dw_mipi_csi_get_version(csi);
	dw_mipi_csi_specific_mappings(csi);
	dw_mipi_csi_mask_irq_power_off(csi);
	dw_mipi_csi_fill_timings(csi);

	dev_info(dev, "DW MIPI CSI-2 Host registered successfully HW v%u.%u\n",
		 csi->hw_version_major, csi->hw_version_minor);

#ifdef DWC_PHY_USING
	ret = phy_init(csi->phy);
	if (ret) {
		dev_err(&csi->phy->dev, "phy init failed --> %d\n", ret);
		goto end;
	}
#endif

	ret = csi2_notifier(csi);

	// ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(dev, "failed to register subdev\n");
		goto end;
	}

	return 0;

end:
#if IS_ENABLED(CONFIG_OF)
	media_entity_cleanup(&csi->sd.entity);
	return ret;
#endif
	//v4l2_device_unregister(csi->vdev.v4l2_dev);
	return ret;
}

static int dw_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct dw_csi *mipi_csi = sd_to_mipi_csi_dev(sd);

	if (mipi_csi->rst)
		reset_control_assert(mipi_csi->rst);

    v4l2_async_unregister_subdev(sd);

#if IS_ENABLED(CONFIG_OF)
	media_entity_cleanup(&mipi_csi->sd.entity);
#else
	v4l2_device_unregister(mipi_csi->vdev.v4l2_dev);
#endif
	dev_info(&pdev->dev, "DW MIPI CSI-2 Host module removed\n");

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id dw_mipi_csi_of_match[] = {
	{ .compatible = "eswin,eic770x-mipi-csi2" },
	{},
};

MODULE_DEVICE_TABLE(of, dw_mipi_csi_of_match);
#endif

static struct platform_driver dw_mipi_csi_driver = {
	.remove = dw_csi_remove,
	.probe = dw_csi_probe,
	.driver = {
		.name = "dw-csi",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = of_match_ptr(dw_mipi_csi_of_match),
#endif
	},
};

int dw_mipi_csi_driver_init(void)
{
	
	return platform_driver_register(&dw_mipi_csi_driver);


}

static void __exit dw_mipi_csi_driver_exit(void)
{
	platform_driver_unregister(&dw_mipi_csi_driver);

}


late_initcall(dw_mipi_csi_driver_init);
module_exit(dw_mipi_csi_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luis Oliveira <luis.oliveira@synopsys.com>");
MODULE_DESCRIPTION("Synopsys DesignWare MIPI CSI-2 Host Platform driver");
