#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <linux/phy/phy.h>
#include <linux/init.h>

#include "phy-eswin-csi2-dphy-common.h"

struct sensor_async_subdev {
	struct v4l2_async_connection asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

static LIST_HEAD(csi2dphy_device_list);

static inline struct csi2_dphy *to_csi2_dphy(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct csi2_dphy, sd);
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct csi2_dphy *dphy = to_csi2_dphy(sd);

	if (dphy->num_sensors == 0)
		return NULL;
	local = &sd->entity.pads[CSI2_DPHY_RX_PAD_SINK];
	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		pr_info("%s:%d yfx !!!!! \n", __func__, __LINE__);

		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_pad_remote_pad_first(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dphy *dphy,
					struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dphy->num_sensors; ++i)
		if (dphy->sensors[i].sd == sd)
			return &dphy->sensors[i];

	return NULL;
}

static int csi2_dphy_get_sensor_data_rate(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	int ret = 0;

	if (!sensor_sd)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(sensor_sd->dev), NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	if(ret) {
		dev_err(dphy->dev, "could not parse endpoint\n");
		fwnode_handle_put(ep);
		return ret;
	}
	fwnode_handle_put(ep);

	dphy->data_rate_mbps = bus_cfg.link_frequencies[0] * 2;
	do_div(dphy->data_rate_mbps, 1000 * 1000);
	dev_dbg(dphy->dev, "dphy%d, data_rate_mbps %lld\n", dphy->phy_index,
		  dphy->data_rate_mbps);
	return 0;
}

static int eswin_csi2_dphy_attach_hw(struct csi2_dphy *dphy, int csi_idx,
				     int index)
{
	struct csi2_dphy_hw *dphy_hw;
	struct v4l2_subdev *sensor_sd = get_remote_sensor(&dphy->sd);
	struct csi2_sensor *sensor = NULL;
	int lanes = 2;

	if (sensor_sd) {
		sensor = sd_to_sensor(dphy, sensor_sd);
		lanes = sensor->lanes;
	}

	if (dphy->drv_data->chip_id == CHIP_ID_EIC7700) {
		dphy_hw = dphy->dphy_hw_group[0];
		mutex_lock(&dphy_hw->mutex);
		dphy_hw->dphy_dev[dphy_hw->dphy_dev_num] = dphy;
		dphy_hw->dphy_dev_num++;
		dphy->dphy_hw = dphy_hw;
		dphy->phy_hw[index] = (void *)dphy_hw;
		mutex_unlock(&dphy_hw->mutex);
	}
	else {
		dev_err(dphy->dev, "Not support %s\n", dphy->drv_data->dev_name);
		return -EINVAL;
	}

	return 0;
}

static void eswin_csi2_inno_phy_remove_dphy_dev(struct csi2_dphy *dphy,
						struct csi2_dphy_hw *dphy_hw)
{
	int i = 0;
	bool is_find_dev = false;
	struct csi2_dphy *csi2_dphy = NULL;

	for (i = 0; i < dphy_hw->dphy_dev_num; i++) {
		csi2_dphy = dphy_hw->dphy_dev[i];
		if (csi2_dphy && csi2_dphy->phy_index == dphy->phy_index)
			is_find_dev = true;
		if (is_find_dev) {
			if (i < dphy_hw->dphy_dev_num - 1)
				dphy_hw->dphy_dev[i] = dphy_hw->dphy_dev[i + 1];
			else
				dphy_hw->dphy_dev[i] = NULL;
		}
	}
	if (is_find_dev)
		dphy_hw->dphy_dev_num--;
}

static int eswin_csi2_dphy_detach_hw(struct csi2_dphy *dphy, int csi_idx,
				     int index)
{
	struct csi2_dphy_hw *dphy_hw = NULL;

	if (dphy->drv_data->chip_id == CHIP_ID_EIC7700) {
		dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[index];
		if (!dphy_hw) {
			dev_err(dphy->dev, "%s csi_idx %d detach hw failed\n",
				__func__, csi_idx);
			return -EINVAL;
		}
		mutex_lock(&dphy_hw->mutex);
		eswin_csi2_inno_phy_remove_dphy_dev(dphy, dphy_hw);
		mutex_unlock(&dphy_hw->mutex);
	}
	else {
		dev_err(dphy->dev, "Not support %s\n", dphy->drv_data->dev_name);
		return -EINVAL;
	}

	return 0;
}

static int csi2_dphy_s_stream_start(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;
	int ret = 0;

	ret = csi2_dphy_get_sensor_data_rate(sd);
	if (ret < 0)
		return ret;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		dphy->dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
		if (dphy->dphy_hw && dphy->dphy_hw->stream_on)
			dphy->dphy_hw->stream_on(dphy, sd);
	}

	dphy->is_streaming = true;

	return 0;
}

static int csi2_dphy_s_stream_stop(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		dphy->dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
		if (dphy->dphy_hw && dphy->dphy_hw->stream_off)
			dphy->dphy_hw->stream_off(dphy, sd);

		if (dphy->drv_data->chip_id != CHIP_ID_EIC7700)
			eswin_csi2_dphy_detach_hw(dphy,
						  dphy->csi_info.csi_idx[i], i);
	}

	dphy->is_streaming = false;

	dev_dbg(dphy->dev, "%s stream stop, dphy%d\n", __func__,
		 dphy->phy_index);

	return 0;
}

static int csi2_dphy_enable_clk(struct csi2_dphy *dphy)
{
	struct csi2_dphy_hw *hw = NULL;
	int ret;
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
		if (hw) {
			ret = clk_bulk_prepare_enable(hw->num_clks,
						      hw->clks_bulk);
			if (ret) {
				dev_err(hw->dev, "failed to enable clks\n");
				return ret;
			}
		}
	}
	return 0;
}

static void csi2_dphy_disable_clk(struct csi2_dphy *dphy)
{
	struct csi2_dphy_hw *hw = NULL;
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
		if (hw)
			clk_bulk_disable_unprepare(hw->num_clks, hw->clks_bulk);
	}
}

static int csi2_dphy_s_stream(struct v4l2_subdev *sd, int on)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int ret = 0;

	mutex_lock(&dphy->mutex);

	if (on) {
		if (dphy->is_streaming) {
			mutex_unlock(&dphy->mutex);
			return 0;
		}

		ret = csi2_dphy_get_sensor_data_rate(sd);
		if (ret < 0) {
			mutex_unlock(&dphy->mutex);
			return ret;
		}

		ret = csi2_dphy_enable_clk(dphy);
		if (ret) {
			mutex_unlock(&dphy->mutex);
			return ret;
		}
		ret = csi2_dphy_s_stream_start(sd);

	} else {
		if (!dphy->is_streaming) {
			mutex_unlock(&dphy->mutex);
			return 0;
		}

		ret = csi2_dphy_s_stream_stop(sd);
		csi2_dphy_disable_clk(dphy);
	}
	mutex_unlock(&dphy->mutex);

	dev_dbg(dphy->dev, "%s stream on:%d, dphy%d, ret %d\n", __func__, on,
		 dphy->phy_index, ret);

	return ret;
}

static int csi2_dphy_g_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	if (sensor)
		return v4l2_subdev_call(sensor, video, g_frame_interval, fi);

	return -EINVAL;
}

static int csi2_dphy_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				   struct v4l2_mbus_config *config)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	int ret = 0;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	// ret = csi2_dphy_update_sensor_mbus(sd);
	*config = sensor->mbus;

	return ret;
}

static int csi2_dphy_s_power(struct v4l2_subdev *sd, int on)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);

	if (on)
		return pm_runtime_get_sync(dphy->dev);
	else
		return pm_runtime_put(dphy->dev);
}

/* dphy accepts all fmt/size from sensor */
static int csi2_dphy_get_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	int ret;
	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	pr_info("sensor_sd->name %s:%d \n", __func__, __LINE__);
	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;
	ret = v4l2_subdev_call(sensor_sd, pad, get_fmt, NULL, fmt);
	if (!ret && fmt->pad == 0 && fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		sensor->format = fmt->format;
	return ret;
}

static int csi2_dphy_get_selection(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_selection *sel)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	return v4l2_subdev_call(sensor, pad, get_selection, NULL, sel);
}

static long es_csi2_dphy_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
				  void *arg)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	long ret = 0;
	int i = 0;
	int on = 0;

	switch (cmd) {
	case ES_DVP2AXI_CMD_SET_CSI_IDX:
		if (dphy->drv_data->chip_id != CHIP_ID_EIC7700)
			dphy->csi_info = *((struct es_dvp2axi_csi_info *)arg);
		break;
	case ESMODULE_SET_QUICK_STREAM:
		for (i = 0; i < dphy->csi_info.csi_num; i++) {
			if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_INNO) {
				dphy->dphy_hw =
					(struct csi2_dphy_hw *)dphy->phy_hw[i];
				if (!dphy->dphy_hw ||
				    !dphy->dphy_hw->quick_stream_off ||
				    !dphy->dphy_hw->quick_stream_on) {
					ret = -EINVAL;
					break;
				}
				on = *(int *)arg;
				if (on)
					dphy->dphy_hw->quick_stream_on(dphy,
								       sd);
				else
					dphy->dphy_hw->quick_stream_off(dphy,
									sd);
			}
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long es_csi2_dphy_compat_ioctl32(struct v4l2_subdev *sd,
					   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct es_dvp2axi_csi_info csi_info = { 0 };
	long ret;

	switch (cmd) {
	case ES_DVP2AXI_CMD_SET_CSI_IDX:
		if (copy_from_user(&csi_info, up,
				   sizeof(struct es_dvp2axi_csi_info)))
			return -EFAULT;

		ret = es_csi2_dphy_ioctl(sd, cmd, &csi_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops csi2_dphy_core_ops = {
	.s_power = csi2_dphy_s_power,
	.ioctl = es_csi2_dphy_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = es_csi2_dphy_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops csi2_dphy_video_ops = {
	.g_frame_interval = csi2_dphy_g_frame_interval,
	.s_stream = csi2_dphy_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_dphy_subdev_pad_ops = {
	.set_fmt = csi2_dphy_get_set_fmt,
	.get_fmt = csi2_dphy_get_set_fmt,
	.get_selection = csi2_dphy_get_selection,
	.get_mbus_config = csi2_dphy_g_mbus_config,
};

static const struct v4l2_subdev_ops csi2_dphy_subdev_ops = {
	.core = &csi2_dphy_core_ops,
	.video = &csi2_dphy_video_ops,
	.pad = &csi2_dphy_subdev_pad_ops,
};

/* The .bound() notifier callback when a match is found */
static int eswin_csi2_dphy_notifier_bound(struct v4l2_async_notifier *notifier,
					  struct v4l2_subdev *sd,
					  struct v4l2_async_connection *asd)
{
	struct csi2_dphy *dphy =
		container_of(notifier, struct csi2_dphy, notifier);
	struct sensor_async_subdev *s_asd =
		container_of(asd, struct sensor_async_subdev, asd);
	struct csi2_sensor *sensor;
	unsigned int pad, ret;

	if (dphy->num_sensors == ARRAY_SIZE(dphy->sensors))
		return -EBUSY;

	sensor = &dphy->sensors[dphy->num_sensors++];
	sensor->lanes = s_asd->lanes;
	sensor->mbus = s_asd->mbus;
	sensor->sd = sd;

	dev_dbg(dphy->dev, "dphy%d matches %s:bus type %d\n", dphy->phy_index,
		 sd->name, s_asd->mbus.type);

	for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(dphy->dev, "failed to find src pad for %s\n",
			sensor->sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(
		&sensor->sd->entity, pad, &dphy->sd.entity,
		CSI2_DPHY_RX_PAD_SINK,
		dphy->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(dphy->dev, "failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}
	return 0;
}

/* The .unbind callback */
static void
eswin_csi2_dphy_notifier_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *sd,
				struct v4l2_async_connection *asd)
{
	struct csi2_dphy *dphy =
		container_of(notifier, struct csi2_dphy, notifier);
	struct csi2_sensor *sensor = sd_to_sensor(dphy, sd);

	if (sensor)
		sensor->sd = NULL;
}

static const struct v4l2_async_notifier_operations eswin_csi2_dphy_async_ops = {
	.bound = eswin_csi2_dphy_notifier_bound,
	.unbind = eswin_csi2_dphy_notifier_unbind,
};

/* Parse fwnode with port0, if an empty function is used, each node will parse
 * all ports, causing the device to repeatedly join the link and unable to
 * complete the link
 */
static int eswin_csi2_dphy_fwnode_parse(struct csi2_dphy *dphy)
{
	struct device *dev = dphy->dev;
	struct fwnode_handle *ep = NULL;
	struct sensor_async_subdev *s_asd = NULL;
	struct v4l2_mbus_config *config = NULL;
	struct fwnode_handle *remote_ep = NULL;
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	// struct device *remote_dev = NULL;
	int ret;

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

		v4l2_async_subdev_nf_init(&dphy->notifier, &dphy->sd);
		s_asd = v4l2_async_nf_add_fwnode_remote(
			&dphy->notifier, ep, struct sensor_async_subdev);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}

		config = &s_asd->mbus;
		if (vep.bus_type == V4L2_MBUS_CSI2_DPHY ||
		    vep.bus_type == V4L2_MBUS_CSI2_CPHY) {
			config->type = vep.bus_type;
			config->bus.mipi_csi2.flags = vep.bus.mipi_csi2.flags;
			s_asd->lanes = vep.bus.mipi_csi2.num_data_lanes;
		} else if (vep.bus_type == V4L2_MBUS_CCP2) {
			/* V4L2_MBUS_CCP2 for lvds */
			config->type = V4L2_MBUS_CCP2;
			s_asd->lanes = vep.bus.mipi_csi1.data_lane;
		} else {
			dev_err(dev,
				"Only CSI2 and CCP2 bus type is currently supported\n");
			ret = -EINVAL;
			goto err_parse;
		}
	}
	return 0;

err_parse:
	fwnode_handle_put(ep);
	return ret;
}

static int eswin_csi2dphy_media_init(struct csi2_dphy *dphy)
{
	int ret;
	
	dphy->pads[CSI2_DPHY_RX_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE |
						    MEDIA_PAD_FL_MUST_CONNECT;
	dphy->pads[CSI2_DPHY_RX_PAD_SINK].flags = MEDIA_PAD_FL_SINK |
						  MEDIA_PAD_FL_MUST_CONNECT;
	dphy->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	ret = media_entity_pads_init(&dphy->sd.entity, CSI2_DPHY_RX_PADS_NUM,
				     dphy->pads);
	if (ret < 0)
		return ret;

	ret = eswin_csi2_dphy_fwnode_parse(dphy);
	if (ret)
		return ret;
	dphy->notifier.ops = &eswin_csi2_dphy_async_ops;
	ret = v4l2_async_nf_register(&dphy->notifier);
	if (ret) {
		dev_err(dphy->dev, "fail to register async notifier: %d\n",
			ret);
		v4l2_async_nf_cleanup(&dphy->notifier);
	}

	return v4l2_async_register_subdev(&dphy->sd);
}

static struct dphy_drv_data eic770x_dphy_drv_data = {
	.dev_name = "csi2dphy",
	.chip_id = CHIP_ID_EIC7700,
	.num_inno_phy = 1,
};

static const struct of_device_id eswin_csi2_dphy_match_id[] = {
	{
		.compatible = "eswin,eic770x-csi2-dphy",
		.data = &eic770x_dphy_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, eswin_csi2_dphy_match_id);

static int eswin_csi2_dphy_get_inno_phy_hw(struct csi2_dphy *dphy)
{
	struct platform_device *plat_dev;
	struct device *dev = dphy->dev;
	struct csi2_dphy_hw *dphy_hw;
	struct device_node *np;
	int i = 0;

	for (i = 0; i < dphy->drv_data->num_inno_phy; i++) {
		np = of_parse_phandle(dev->of_node, "eswin,hw", i);
		if (!np || !of_device_is_available(np)) {
			dev_err(dphy->dev, "failed to get dphy%d hw node\n",
				dphy->phy_index);
			return -ENODEV;
		}
		plat_dev = of_find_device_by_node(np);
		of_node_put(np);
		if (!plat_dev) {
			dev_err(dphy->dev,
				"failed to get dphy%d hw from node\n",
				dphy->phy_index);
			return -ENODEV;
		}
		dphy_hw = platform_get_drvdata(plat_dev);
		if (!dphy_hw) {
			dev_err(dphy->dev, "failed attach dphy%d hw\n",
				dphy->phy_index);
			return -EINVAL;
		}
		dphy->dphy_hw_group[i] = dphy_hw;
	}
	return 0;
}

static int eswin_csi2_dphy_get_hw(struct csi2_dphy *dphy)
{
	int ret = 0;

	if (dphy->drv_data->chip_id == CHIP_ID_EIC7700) {
		ret = eswin_csi2_dphy_get_inno_phy_hw(dphy);
	} else {
		ret = eswin_csi2_dphy_get_inno_phy_hw(dphy);
	}
	return ret;
}

static int eswin_csi2dphy_of_notifier(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct csi2_dphy *dphy =
		container_of(nb, struct csi2_dphy, of_notifier);
	struct of_overlay_notify_data *notify_data = data;
	struct v4l2_async_notifier *parent_notifier = NULL;
	int ret = 0;
	if (!dphy || !notify_data || !notify_data->target)
		return NOTIFY_DONE;

	if(dphy->dev->of_node != notify_data->target)
		return NOTIFY_DONE;

	if (action == OF_OVERLAY_POST_APPLY) {
		msleep(200);
		parent_notifier = dphy->notifier.parent;
		v4l2_async_nf_unregister(&dphy->notifier);
		v4l2_async_nf_cleanup(&dphy->notifier);

		dphy->notifier.parent = parent_notifier;
		ret = eswin_csi2_dphy_fwnode_parse(dphy);
		if (ret < 0) {
			dev_err(dphy->dev, "failed to parse fwnode: %d\n", ret);
			return NOTIFY_DONE;
		}

		dphy->notifier.ops = &eswin_csi2_dphy_async_ops;
		ret = v4l2_async_nf_register(&dphy->notifier);
		if (ret) {
			dev_err(dphy->dev, "Failed to register notifier: %d\n",
				ret);
			v4l2_async_nf_cleanup(&dphy->notifier);
			return NOTIFY_DONE;
		}
	}

	return NOTIFY_OK;
}

static int eswin_csi2_dphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct csi2_dphy *csi2dphy;
	struct v4l2_subdev *sd;
	const struct dphy_drv_data *drv_data;
	struct device *parent = pdev->dev.parent;
	struct eswin_vi_device* es_vi_dev;
	int ret;

	es_vi_dev = dev_get_drvdata(parent);
	csi2dphy = devm_kzalloc(dev, sizeof(*csi2dphy), GFP_KERNEL);
	if (!csi2dphy)
		return -ENOMEM;
	csi2dphy->dev = dev;

	of_id = of_match_device(eswin_csi2_dphy_match_id, dev);
	if (!of_id)
		return -EINVAL;
	drv_data = of_id->data;
	csi2dphy->drv_data = drv_data;

	of_property_read_u32(dev->of_node, "index", &csi2dphy->phy_index);

	if (csi2dphy->phy_index >= PHY_MAX)
		csi2dphy->phy_index = 0;

	ret = eswin_csi2_dphy_get_hw(csi2dphy);
	if (ret)
		return -EINVAL;
	if (csi2dphy->drv_data->chip_id == CHIP_ID_EIC7700) {
		csi2dphy->csi_info.csi_num = 1;
		csi2dphy->csi_info.dphy_vendor[0] = PHY_VENDOR_INNO;
		for (size_t i = 0; i < csi2dphy->csi_info.csi_num; i++)
		{
			eswin_csi2_dphy_attach_hw(csi2dphy, i, i);
		}
	} else {
		csi2dphy->csi_info.csi_num = 0;
	}
	csi2dphy->v4l2_dev.mdev = es_vi_dev->media_dev;
	ret = v4l2_device_register(dev, &csi2dphy->v4l2_dev);
	sd = &csi2dphy->sd;
	mutex_init(&csi2dphy->mutex);
	v4l2_subdev_init(sd, &csi2_dphy_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "eswin-csi2-dphy%d",
		 csi2dphy->phy_index);
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = eswin_csi2dphy_media_init(csi2dphy);
	if (ret < 0)
		goto detach_hw;

	csi2dphy->of_notifier.notifier_call = eswin_csi2dphy_of_notifier;
	of_overlay_notifier_register(&csi2dphy->of_notifier);
	pm_runtime_enable(&pdev->dev);
	dev_info(dev, "csi2 dphy%d probe successfully!\n", csi2dphy->phy_index);
	return 0;

detach_hw:
	mutex_destroy(&csi2dphy->mutex);
	return -EINVAL;
}

static int eswin_csi2_dphy_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++)
		eswin_csi2_dphy_detach_hw(dphy, dphy->csi_info.csi_idx[i], i);
	media_entity_cleanup(&sd->entity);
	v4l2_async_nf_cleanup(&dphy->notifier);
	v4l2_async_nf_unregister(&dphy->notifier);
	v4l2_device_unregister(&dphy->v4l2_dev);
	v4l2_device_unregister_subdev(sd);
	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&dphy->mutex);
	return 0;
}

struct platform_driver eswin_csi2_dphy_driver = {
	.probe = eswin_csi2_dphy_probe,
	.remove = eswin_csi2_dphy_remove,
	.driver = {
		.name = "eswin-csi2-dphy",
		.owner = THIS_MODULE,
		.of_match_table = eswin_csi2_dphy_match_id,
	},
};

int eswin_csi2_dphy_init(void)
{
	return platform_driver_register(&eswin_csi2_dphy_driver);
}

void eswin_csi2_dphy_uinit(void)
{
	pr_err("%s:%d yfx !!!!!! \n", __func__, __LINE__);
	platform_driver_unregister(&eswin_csi2_dphy_driver);
}

late_initcall(eswin_csi2_dphy_init);
module_exit(eswin_csi2_dphy_uinit);

MODULE_AUTHOR("luyulin@eswincomputing.com");
MODULE_DESCRIPTION("Eswin dphy platform driver");
MODULE_LICENSE("GPL v2");
