// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 *
 * Synopsys DesignWare MIPI CSI-2 Host controller driver.
 * Platform driver
 *
 * Author: Luis Oliveira <luis.oliveira@synopsys.com>
 */

#include "dw-csi-plat.h"
#include <linux/es-camera-module.h>


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

	pr_debug("%s entered mbus: 0x%x\n", __func__, mf->code);

	for (i = 0; i < ARRAY_SIZE(dw_mipi_csi_formats); i++)
		if (mf->code == dw_mipi_csi_formats[i].mbus_code) {
			pr_debug("Found mbus 0x%x\n", dw_mipi_csi_formats[i].mbus_code);
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
	struct media_entity *entity;
	struct v4l2_subdev *sensor_sd = NULL;

	local = &sd->entity.pads[DWC_CSI2_PAD_SINK];

	while (1) {
		remote = media_pad_remote_pad_first(local);
		if (!remote) {
			v4l2_warn(sd, "No remote pad found\n");
			break;
		}

		entity = remote->entity;

		if (entity->function == MEDIA_ENT_F_CAM_SENSOR) {
			sensor_sd = media_entity_to_v4l2_subdev(entity);
			dev_dbg(sd->dev, "Found sensor device: %s\n", sensor_sd->name);
			break;
		}

		local = &entity->pads[0];
		if (!(local->flags & MEDIA_PAD_FL_SINK)) {
			v4l2_warn(sd, "No SINK pad found in entity %s\n", entity->name);
			break;
		}
	}

	return sensor_sd;
}

static struct v4l2_mbus_framefmt *
dw_mipi_csi_get_format(struct dw_csi *dev, struct v4l2_subdev_state *cfg,
		       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return cfg ? v4l2_subdev_get_try_format(&dev->sd, cfg, 0) : NULL;
	dev_dbg(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev->format.code);
	dev_dbg(dev->dev,
		"%s got width. 0x%x\n", __func__,
		dev->format.width);
	dev_dbg(dev->dev,
		"%s got height. 0x%x\n", __func__,
		dev->format.height);
	return &dev->format;
}

static int dw_mipi_csi_get_hdr_config(struct v4l2_subdev *sd);
static void dw_mipi_csi_set_ipi_config(struct dw_csi *dev);
static int
dw_mipi_csi_set_fmt(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *cfg,
		    struct v4l2_subdev_format *fmt)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct mipi_fmt *dev_fmt = NULL;
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	dev_fmt = dw_mipi_csi_try_format(&fmt->format);
	if (!fmt)
		return -EINVAL;

	if (dev_fmt) {
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			dev->fmt = dev_fmt;
		dev->fmt->mbus_code = mf->code;
		dev->format = *mf;
		dw_mipi_csi_get_hdr_config(sd);
		dw_mipi_csi_set_ipi_config(dev);
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

static void dw_mipi_csi_set_ipi_config(struct dw_csi *dev)
{
	switch (dev->hw.hdr_mode) {
	case NO_HDR:
		dev->hw.ipi2_en = 0;
		dev->hw.ipi2_vcid = 0;
		dev->hw.ipi3_en = 0;
		dev->hw.ipi3_vcid = 0;
		break;

	case HDR_X2:
		dev->hw.ipi2_en = 1;
		dev->hw.ipi2_vcid = 1;
		dev->hw.ipi3_en = 0;
		dev->hw.ipi3_vcid = 0;
		break;

	case HDR_X3:
		dev->hw.ipi2_en = 1;
		dev->hw.ipi2_vcid = 1;
		dev->hw.ipi3_en = 1;
		dev->hw.ipi3_vcid = 2;
		break;

	default:
		dev->hw.ipi2_en = 0;
		dev->hw.ipi2_vcid = 0;
		dev->hw.ipi3_en = 0;
		dev->hw.ipi3_vcid = 0;
		break;
	}

	dev_dbg(dev->dev, "HDR mode %d: IPI2=%d(VCID=%d), IPI3=%d(VCID=%d)\n",
				dev->hw.hdr_mode, dev->hw.ipi2_en, dev->hw.ipi2_vcid,
				dev->hw.ipi3_en, dev->hw.ipi3_vcid);
}

static int dw_mipi_csi_get_hdr_config(struct v4l2_subdev *sd)
{
	int ret = 0;
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct esmodule_hdr_cfg hdr_cfg;

	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	if (sensor) {
		ret = v4l2_subdev_call(sensor, core, ioctl, ESMODULE_GET_HDR_CFG, &hdr_cfg);
		if(!ret) {
			dev->hw.hdr_mode = hdr_cfg.hdr_mode;
			dev_dbg(sd->dev, "CYY csi v4l2_subdev_call success! hdr_mode: %d\n", dev->hw.hdr_mode);
		} else
			dev_dbg(sd->dev, "sensor not implement ESMODULE_GET_HDR_CFG\n");
	}

	return 0;
}

static int
dw_mipi_csi_s_power(struct v4l2_subdev *sd, int on)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dev_dbg(dev->dev, "%s: on=%d\n", __func__, on);

	if (on) {
		dw_mipi_csi_hw_stdby(dev);
		dw_mipi_csi_start(dev);
		dw_mipi_csi_log_status(sd);
	} else {
#ifdef DWC_PHY_USING
		phy_power_off(dev->phy);
#endif
		writel(0, dev->base_address + 0x44); //phy reset
		writel(0, dev->base_address + 0x40); //phy shutdownz
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
	int ret = 0;

	if (of_property_read_u32(node, "snps,output-type", &dev->hw.output))
		dev->hw.output = 2;

	if (of_property_read_u32(node, "snps,en-ppi-width", &dev->hw.ppi_width))
		dev->hw.ppi_width = 0;

	if (dev->hw.ppi_width > 1) {
		dev_err(&pdev->dev, "Wrong ppi width(%d), valid value is '0'(ppi8) or '1'(ppi16)\n",
				dev->hw.ppi_width);
		return -EINVAL;
	}

	if(of_property_read_u32(node, "eswin,csi-hdr-mode", &dev->hw.hdr_mode))
		dev->hw.hdr_mode = NO_HDR;

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

	if (of_property_read_u32(dev->dev->of_node, "ipi_dt", &dev->hw.ipi_dt))
		dev->hw.ipi_dt = 0x2b;

	node = of_graph_get_next_endpoint(node, NULL);
	if (!node) {
		dev_err(&pdev->dev, "No port node at %pOF\n",
			pdev->dev.of_node);
		return -EINVAL;
	}

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
	unsigned int pad, ret;

	if (csi2->num_sensors == ARRAY_SIZE(csi2->sensors)) {
		v4l2_err(&csi2->sd, "%s: the num of sd is beyond:%d\n",
			 __func__, csi2->num_sensors);
		return -EBUSY;
	}
	sensor = &csi2->sensors[csi2->num_sensors++];
	sensor->sd = sd;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(csi2->dev, "failed to find src pad for %s\n", sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(
		&sensor->sd->entity, pad, &csi2->sd.entity, DWC_CSI2_PAD_SINK,
		MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2->dev, "failed to create link for %s\n", sd->name);
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
	return ret;
}

static int dw_csi_of_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct dw_csi *csi_dev = container_of(nb, struct dw_csi, of_notifier);
	struct of_overlay_notify_data *notify_data = data;
	if (!csi_dev || !notify_data || !notify_data->target)
		return NOTIFY_DONE;

	if(csi_dev->dev->of_node != notify_data->target)
		return NOTIFY_DONE;

	if (action == OF_OVERLAY_POST_APPLY) {
		if (of_property_read_u32(csi_dev->dev->of_node, "num_lanes", &csi_dev->hw.num_lanes))
			csi_dev->hw.num_lanes = 2;
	}
	return NOTIFY_DONE;
}

static int dw_csi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id = NULL;
	struct device *dev = &pdev->dev;
	struct dw_csi *csi;
	struct v4l2_subdev *sd;
	struct device *parent = pdev->dev.parent;
	struct eswin_vi_device* es_vi_dev;
	int ret;

	es_vi_dev = dev_get_drvdata(parent);

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
	} else {
		dev_err(dev, "No device tree node\n");
		return -EINVAL;
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
		dev_err(dev, "irq csi %s failed\n", of_id->name);
		goto end;
	}

	sd = &csi->sd;
	v4l2_subdev_init(sd, &dw_mipi_csi_subdev_ops);
	sd->dev = dev;
	csi->sd.owner = THIS_MODULE;

	snprintf(sd->name, sizeof(sd->name), "%s.%d",
			 "dw-csi", csi->index);

	csi->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	csi->fmt = &dw_mipi_csi_formats[0];
	csi->format.code = dw_mipi_csi_formats[0].mbus_code;

	sd->entity.function = MEDIA_ENT_F_IO_V4L;

	csi->pads[CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	csi->pads[CSI_PAD_SOURCE0].flags = MEDIA_PAD_FL_SOURCE;
	csi->pads[CSI_PAD_SOURCE1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi->sd.entity, CSI_PADS_NUM, csi->pads);
	if (ret < 0) {
		dev_err(dev, "media entity init failed\n");
		goto end;
	}

	v4l2_set_subdevdata(&csi->sd, pdev);
	platform_set_drvdata(pdev, &csi->sd);
	dev_set_drvdata(dev, sd);

	if (csi->rst)
		reset_control_deassert(csi->rst);

	dw_mipi_csi_get_version(csi);
	dw_mipi_csi_specific_mappings(csi);
	dw_mipi_csi_mask_irq_power_off(csi);
	dw_mipi_csi_fill_timings(csi);

	dev_info(dev, "DW MIPI CSI-2 Host registered successfully HW v%u.%u\n",
		 csi->hw_version_major, csi->hw_version_minor);

	ret = csi2_notifier(csi);
	if (ret) {
		dev_err(dev, "failed to register async notifier --> %d\n",
			ret);
		goto end;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(dev, "failed to register subdev\n");
		goto end;
	}

	csi->of_notifier.notifier_call = dw_csi_of_notifier;
	of_overlay_notifier_register(&csi->of_notifier);

	return 0;

end:
#if IS_ENABLED(CONFIG_OF)
	media_entity_cleanup(&csi->sd.entity);
	return ret;
#endif
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
