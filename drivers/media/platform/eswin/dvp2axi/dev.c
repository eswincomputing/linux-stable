// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI dev driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
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
 * Authors: Eswin VI team
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ctrls.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/kthread.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_address.h>
#include "dev.h"

int es_dvp2axi_debug = 0;
module_param_named(debug, es_dvp2axi_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-7)");

static DEFINE_MUTEX(es_dvp2axi_dev_mutex);
static LIST_HEAD(es_dvp2axi_device_list);

static ssize_t es_dvp2axi_show_dummybuf_mode(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", dvp2axi_dev->is_use_dummybuf);
	return ret;
}

static ssize_t es_dvp2axi_store_dummybuf_mode(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret) {
		if (val)
			dvp2axi_dev->is_use_dummybuf = true;
		else
			dvp2axi_dev->is_use_dummybuf = false;
	} else {
		dev_info(dvp2axi_dev->dev, "set dummy buf mode failed\n");
	}
	return len;
}

static DEVICE_ATTR(is_use_dummybuf, S_IWUSR | S_IRUSR, es_dvp2axi_show_dummybuf_mode,
		   es_dvp2axi_store_dummybuf_mode);


static struct attribute *dev_attrs[] = {
	&dev_attr_is_use_dummybuf.attr,
	NULL,
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

struct es_dvp2axi_match_data {
	int inf_id;
};

/**************************** pipeline operations *****************************/
static int __dvp2axi_pipeline_prepare(struct es_dvp2axi_pipeline *p,
				  struct media_entity *me)
{
	struct v4l2_subdev *sd;
	int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_pad_remote_pad_first(spad);
			if (pad)
				break;
		}

		if (!pad)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		p->subdevs[p->num_subdevs++] = sd;
		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}

	return 0;
}

static int __dvp2axi_pipeline_s_dvp2axi_clk(struct es_dvp2axi_pipeline *p)
{
	return 0;
}

static int es_dvp2axi_pipeline_open(struct es_dvp2axi_pipeline *p,
			       struct media_entity *me, bool prepare)
{
	int ret;

	if (WARN_ON(!p || !me))
		return -EINVAL;
	if (atomic_inc_return(&p->power_cnt) > 1)
		return 0;

	/* go through media graphic and get subdevs */
	if (prepare)
		__dvp2axi_pipeline_prepare(p, me);

	if (!p->num_subdevs)
		return -EINVAL;

	ret = __dvp2axi_pipeline_s_dvp2axi_clk(p);
	if (ret < 0)
		return ret;

	return 0;
}

static int es_dvp2axi_pipeline_close(struct es_dvp2axi_pipeline *p)
{
	atomic_dec_return(&p->power_cnt);

	return 0;
}

/*
 * stream-on order: isp_subdev, mipi dphy, sensor
 * stream-off order: mipi dphy, sensor, isp_subdev
 */
static int es_dvp2axi_pipeline_set_stream(struct es_dvp2axi_pipeline *p, bool on)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		container_of(p, struct es_dvp2axi_device, pipe);
	bool can_be_set = false;
	int i, ret = 0;

	if (dvp2axi_dev->hdr.hdr_mode == NO_HDR ||
		dvp2axi_dev->hdr.hdr_mode == HDR_X2 ||
		dvp2axi_dev->hdr.hdr_mode == HDR_X3 ||
	    dvp2axi_dev->hdr.hdr_mode == HDR_COMPR) {
		if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
		    (!on && atomic_dec_return(&p->stream_cnt) > 0))
			return 0;

		if (on) {
			dvp2axi_dev->irq_stats.frm_end_cnt[0] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[1] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[2] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[3] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[4] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[5] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[0] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[1] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[2] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[3] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[4] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[5] = 0;
			dvp2axi_dev->irq_stats.all_err_cnt = 0;
		}

		if(on) {
			/* csi -> phy -> sensor */
			for (i = 0; i < p->num_subdevs; i++) {
				ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
				if (on && ret < 0 && ret != -ENOIOCTLCMD &&
					ret != -ENODEV)
					goto err_stream_off;
			}
		} else {
			/*sensor -> phy -> csi*/
			for (i = p->num_subdevs-1; i >=0; i--) {
				ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
				if (on && ret < 0 && ret != -ENOIOCTLCMD &&
					ret != -ENODEV)
					goto err_stream_off;
				msleep(300); // ensure all data has been sent
			}
		}
	} else {
		if (!on && atomic_dec_return(&p->stream_cnt) > 0)
			return 0;

		if (on) {
			atomic_inc(&p->stream_cnt);
			if (dvp2axi_dev->hdr.hdr_mode == HDR_X2) {
				if (atomic_read(&p->stream_cnt) == 1) {
					can_be_set = false;
				} else if (atomic_read(&p->stream_cnt) == 2) {
					can_be_set = true;
				}
			} else if (dvp2axi_dev->hdr.hdr_mode == HDR_X3) {
				if (atomic_read(&p->stream_cnt) == 1) {
					can_be_set = false;
				} else if (atomic_read(&p->stream_cnt) == 3) {
					can_be_set = true;
				}
			}
		}

		if ((on && can_be_set) || !on) {
			if (on) {
				dvp2axi_dev->irq_stats.frm_end_cnt[0] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[1] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[2] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[3] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[4] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[5] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[0] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[1] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[2] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[3] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[4] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[5] = 0;
				dvp2axi_dev->irq_stats.all_err_cnt = 0;
			}

			/* phy -> sensor */
			for (i = p->num_subdevs-1; i >=0; i--) {
				ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
				if (on && ret < 0 && ret != -ENOIOCTLCMD &&
				    ret != -ENODEV)
					goto err_stream_off;
			}
		}
	}


	return 0;

err_stream_off:
	for (--i; i >= 0; --i)
		v4l2_subdev_call(p->subdevs[i], video, s_stream, false);
	return ret;
}

static int es_dvp2axi_create_link(struct es_dvp2axi_device *dev,
			     struct es_dvp2axi_sensor_info *sensor, u32 stream_num, u32 sensor_id,
			     bool *mipi_lvds_linked)
{
	struct es_dvp2axi_sensor_info linked_sensor;
	struct media_entity *source_entity, *sink_entity;
	int ret = 0;
	u32 flags, pad, id;
	int pad_offset = 0;

	if (dev->chip_id >= CHIP_EIC770X_DVP2AXI)
		pad_offset = 4;

	linked_sensor.lanes = sensor->lanes;

	if (sensor->mbus.type == V4L2_MBUS_CCP2) {
		dev_err(dev->dev, "unsupport ccp2 sensor %s\n", sensor->sd->name);
		return -EINVAL;
	} else {
		linked_sensor.sd = sensor->sd;
	}

	memcpy(&linked_sensor.mbus, &sensor->mbus,
	       sizeof(struct v4l2_mbus_config));

	for (pad = 0; pad < linked_sensor.sd->entity.num_pads; pad++) {
		if (linked_sensor.sd->entity.pads[pad].flags &
		    MEDIA_PAD_FL_SOURCE) {
			if (pad == linked_sensor.sd->entity.num_pads) {
				dev_err(dev->dev,
					"failed to find src pad for %s\n",
					linked_sensor.sd->name);

				break;
			}
			for (id = 0; id < stream_num; id++) {
				source_entity = &linked_sensor.sd->entity;
				sink_entity =
					&dev->stream[id].vnode.vdev.entity;
				if ((id == pad - 1 && !(*mipi_lvds_linked)))
					flags = MEDIA_LNK_FL_ENABLED;
				else
					flags = 0;
				ret = media_create_pad_link(source_entity, pad,
							    sink_entity, 0,
							    flags);
				if (ret) {
					dev_err(dev->dev,
						"failed to create link for %s\n",
						linked_sensor.sd->name);
					break;
				}
			}
			break;
		}
	}

	if (sensor->mbus.type == V4L2_MBUS_CCP2) {
		source_entity = &sensor->sd->entity;
		sink_entity = &linked_sensor.sd->entity;
		ret = media_create_pad_link(source_entity, 1, sink_entity, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			dev_err(dev->dev,
				"failed to create link between %s and %s\n",
				linked_sensor.sd->name, sensor->sd->name);
	}

	if (linked_sensor.mbus.type != V4L2_MBUS_BT656 &&
	    linked_sensor.mbus.type != V4L2_MBUS_PARALLEL) {
			*mipi_lvds_linked = true;
	}
	return ret;
}

/***************************** media controller *******************************/
static int es_dvp2axi_create_links(struct es_dvp2axi_device *dev)
{
	u32 s = 0;
	u32 stream_num = 0;
	bool mipi_lvds_linked= false;

	stream_num = ES_DVP2AXI_SINGLE_STREAM;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct es_dvp2axi_sensor_info *sensor = &dev->sensors[s];
		es_dvp2axi_create_link(dev, sensor, stream_num, s, &mipi_lvds_linked);
	}
	
	return 0;
}

static int _set_pipeline_default_fmt(struct es_dvp2axi_device *dev)
{
	es_dvp2axi_set_default_fmt(dev);
	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct es_dvp2axi_device *dev;
	struct es_dvp2axi_sensor_info *sensor;
	struct v4l2_subdev *sd;
	struct v4l2_device *v4l2_dev = NULL;
	int ret, index;

	dev = container_of(notifier, struct es_dvp2axi_device, notifier);
	v4l2_dev = dev->v4l2_dev;

	for (index = 0; index < dev->num_sensors; index++) {
		sensor = &dev->sensors[index];

		list_for_each_entry(sd, &v4l2_dev->subdevs, list) {
			if (sd->ops) {
				if (sd == sensor->sd) {
					ret = v4l2_subdev_call(sd, pad,
							       get_mbus_config,
							       0,
							       &sensor->mbus);
					if (ret)
						v4l2_err(
							v4l2_dev,
							"get mbus config failed for linking\n");
				}
			}
		}
		if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ||
		    sensor->mbus.type == V4L2_MBUS_CSI2_CPHY) {
			sensor->lanes =
				sensor->mbus.bus.mipi_csi2.num_data_lanes;
		} else if (sensor->mbus.type == V4L2_MBUS_CCP2) {
			sensor->lanes = sensor->mbus.bus.mipi_csi1.data_lane;
		}
	}

	ret = es_dvp2axi_create_links(dev);
	if (ret < 0)
		goto notifier_end;
	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto notifier_end;
	ret = _set_pipeline_default_fmt(dev);
	if (ret < 0)
		goto notifier_end;

	v4l2_info(v4l2_dev, "Async subdev notifier completed\n");
	return ret;

notifier_end:
	return ret;
}

struct es_dvp2axi_async_subdev {
	struct v4l2_async_connection asd;
	struct v4l2_mbus_config mbus;
	u32 port;
	int lanes;
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_connection *asd)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		container_of(notifier, struct es_dvp2axi_device, notifier);
	struct es_dvp2axi_async_subdev *s_asd =
		container_of(asd, struct es_dvp2axi_async_subdev, asd);

	if (dvp2axi_dev->num_sensors == ARRAY_SIZE(dvp2axi_dev->sensors)) {
		v4l2_err(dvp2axi_dev->v4l2_dev,
			 "%s: the num of subdev is beyond %d\n", __func__,
			 dvp2axi_dev->num_sensors);
		return -EBUSY;
	}

	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].lanes = s_asd->lanes;
	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].mbus = s_asd->mbus;
	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].sd = subdev;
	++dvp2axi_dev->num_sensors;

	return 0;
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
};

static int es_dvp2axi_fwnode_parse(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct device *dev = dvp2axi_dev->dev;

	int ret = 0, i = 0;

	for (i = 0; i < 2; i++) {
		struct v4l2_fwnode_endpoint vep = {
			// .bus_type = V4L2_MBUS_CSI2_DPHY
			.bus_type = V4L2_MBUS_UNKNOWN
		};
		struct es_dvp2axi_async_subdev *s_asd;
		struct fwnode_handle *ep;
		struct fwnode_handle *remote_ep = NULL;

		ep = fwnode_graph_get_endpoint_by_id(
			dev_fwnode(dev), i, 0, FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		remote_ep = fwnode_graph_get_remote_port_parent(ep);

		if (!fwnode_device_is_available(remote_ep)) {
			fwnode_handle_put(remote_ep);
			continue;
		}

		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret)
			goto err_parse;

		s_asd = v4l2_async_nf_add_fwnode_remote(
			&dvp2axi_dev->notifier, ep, struct es_dvp2axi_async_subdev);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}

		s_asd->port = vep.base.port;
		s_asd->lanes = vep.bus.mipi_csi2.num_data_lanes;
		s_asd->mbus.type = vep.bus_type;

		fwnode_handle_put(ep);
		
		continue;

err_parse:
		fwnode_handle_put(ep);
		return ret;
	}

	/*
	 * Proceed even without sensors connected to allow the device to
	 * suspend.
	 */
	dvp2axi_dev->notifier.ops = &subdev_notifier_ops;
	ret = v4l2_async_nf_register(&dvp2axi_dev->notifier);
	if (ret)
		dev_err(dev, "failed to register async notifier : %d\n", ret);
	return ret;
}

static int dvp2axi_subdev_notifier(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct v4l2_async_notifier *ntf = &dvp2axi_dev->notifier;
	int ret;

	v4l2_async_nf_init(ntf, dvp2axi_dev->v4l2_dev);
	ret = es_dvp2axi_fwnode_parse(dvp2axi_dev);
	if (ret < 0)
		return ret;
	return 0;
}

/***************************** platform deive *******************************/

static int es_dvp2axi_register_platform_subdevs(struct es_dvp2axi_device *dvp2axi_dev)
{
	int stream_num = 0, ret;

	stream_num = ES_DVP2AXI_SINGLE_STREAM;
	// stream_num = ESDVP2AXI_MAX_STREAM_MIPI;
	ret = es_dvp2axi_register_stream_vdevs(dvp2axi_dev, stream_num, true);
	//ret = es_dvp2axi_register_stream_vdevs(dvp2axi_dev, stream_num, false);
	if (ret < 0) {
		dev_err(dvp2axi_dev->dev, "dvp2axi register stream[%d] failed!\n",
			stream_num);
		return -EINVAL;
	}

	ret = dvp2axi_subdev_notifier(dvp2axi_dev);
	if (ret < 0) {
		dev_err(dvp2axi_dev->dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_stream_vdev;
	}
	return 0;
err_unreg_stream_vdev:
	es_dvp2axi_unregister_stream_vdevs(dvp2axi_dev, stream_num);
	return ret;
}

int es_dvp2axi_attach_hw(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct es_dvp2axi_hw *hw;

	if (dvp2axi_dev->hw_dev)
		return 0;

	dvp2axi_dev->chip_id = CHIP_EIC770X_DVP2AXI;
	np = of_parse_phandle(dvp2axi_dev->dev->of_node, "eswin,hw", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(dvp2axi_dev->dev, "failed to get dvp2axi hw node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(dvp2axi_dev->dev, "failed to get dvp2axi hw from node\n");
		return -ENODEV;
	}

	hw = platform_get_drvdata(pdev);
	if (!hw) {
		dev_err(dvp2axi_dev->dev, "failed attach dvp2axi hw\n");
		return -EINVAL;
	}

	hw->dvp2axi_dev[hw->dev_num] = dvp2axi_dev;
	hw->dev_num++;
	dvp2axi_dev->hw_dev = hw;
	dvp2axi_dev->chip_id = hw->chip_id;
	dev_info(dvp2axi_dev->dev, "attach to dvp2axi hw node\n");

	return 0;
}

static int es_dvp2axi_detach_hw(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_hw *hw = dvp2axi_dev->hw_dev;
	int i;

	for (i = 0; i < hw->dev_num; i++) {
		if (hw->dvp2axi_dev[i] == dvp2axi_dev) {
			if ((i + 1) < hw->dev_num) {
				hw->dvp2axi_dev[i] = hw->dvp2axi_dev[i + 1];
				hw->dvp2axi_dev[i + 1] = NULL;
			} else {
				hw->dvp2axi_dev[i] = NULL;
			}

			hw->dev_num--;
			dev_info(dvp2axi_dev->dev, "detach to dvp2axi hw node\n");
			break;
		}
	}

	return 0;
}

//2025-3-14 use es_vi_dev->media_dev to replace es_dvp2axi_media_dev
int es_dvp2axi_plat_init(struct es_dvp2axi_device *dvp2axi_dev, struct device_node *node,
		    int inf_id)
{
	struct eswin_vi_device* es_vi_dev;
	int ret;
	int dvp2axi_id;

	dvp2axi_dev->hdr.hdr_mode = NO_HDR;
	dvp2axi_dev->inf_id = inf_id;
	es_vi_dev =  dev_get_drvdata(dvp2axi_dev->dev->parent);

	mutex_init(&dvp2axi_dev->stream_lock);
	spin_lock_init(&dvp2axi_dev->hdr_lock);
	atomic_set(&dvp2axi_dev->pipe.power_cnt, 0);
	atomic_set(&dvp2axi_dev->pipe.stream_cnt, 0);
	atomic_set(&dvp2axi_dev->power_cnt, 0);
	atomic_set(&dvp2axi_dev->streamoff_cnt, 0);
	dvp2axi_dev->pipe.open = es_dvp2axi_pipeline_open;
	dvp2axi_dev->pipe.close = es_dvp2axi_pipeline_close;
	dvp2axi_dev->pipe.set_stream = es_dvp2axi_pipeline_set_stream;

	memset(&dvp2axi_dev->channels[0].capture_info, 0,
	       sizeof(dvp2axi_dev->channels[0].capture_info));

	if (of_property_read_u32(node, "dvp2axi_id", &dvp2axi_dev->dvp2axi_id))
		dvp2axi_id = 0;

	es_dvp2axi_stream_init(dvp2axi_dev, dvp2axi_dev->dvp2axi_id);

	dvp2axi_dev->workmode = ES_DVP2AXI_WORKMODE_ONEFRAME;

	dvp2axi_dev->is_use_dummybuf = true;

	dvp2axi_dev->media_dev = es_vi_dev->media_dev;
	dvp2axi_dev->v4l2_dev = &es_vi_dev->v4l2_dev;

	/* create & register platefom subdev (from of_node) */
	ret = es_dvp2axi_register_platform_subdevs(dvp2axi_dev);
	if (ret < 0)
		return ret;

	mutex_lock(&es_dvp2axi_dev_mutex);
	list_add_tail(&dvp2axi_dev->list, &es_dvp2axi_device_list);
	mutex_unlock(&es_dvp2axi_dev_mutex);
	return 0;
}

int es_dvp2axi_plat_uninit(struct es_dvp2axi_device *dvp2axi_dev)
{
	int stream_num = 0;

	stream_num = ES_DVP2AXI_MAX_STREAM_MIPI;
	es_dvp2axi_unregister_stream_vdevs(dvp2axi_dev, stream_num);
	return 0;
}

static const struct es_dvp2axi_match_data es_dvp2axi_dvp_match_data = {
	.inf_id = ES_DVP2AXI_DVP,
};

static const struct es_dvp2axi_match_data es_dvp2axi_mipi_lvds_match_data = {
	.inf_id = ES_DVP2AXI_MIPI_LVDS,
};

static const struct of_device_id es_dvp2axi_plat_of_match[] = {
	{
		.compatible = "eswin,dvp2axi-mipi-lvds",
		.data = &es_dvp2axi_mipi_lvds_match_data,
	},
	{},
};

static const struct of_device_id es_dvp2axi_plat_of_match_d1[] = {
	{
		.compatible = "eswin,dvp2axi-mipi-lvds_d1",
		.data = &es_dvp2axi_mipi_lvds_match_data,
	},
	{},
};

static void es_dvp2axi_parse_dts(struct es_dvp2axi_device *dvp2axi_dev)
{
	int ret = 0;
	struct device_node *node = dvp2axi_dev->dev->of_node;

	ret = of_property_read_u32(node, OF_DVP2AXI_HDR_MODE, &dvp2axi_dev->hdr.hdr_mode);
	if (ret != 0)
		dvp2axi_dev->hdr_mode = NO_HDR;
}

static int dvp2axi_of_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct es_dvp2axi_device *dvp2axi_dev = container_of(nb, struct es_dvp2axi_device, of_notifier);
	struct of_overlay_notify_data *notify_data = data;
	if (!dvp2axi_dev || !notify_data || !notify_data->target)
		return NOTIFY_DONE;

	if(dvp2axi_dev->dev->of_node != notify_data->target)
		return NOTIFY_DONE;

	if (action == OF_OVERLAY_POST_APPLY) {
		msleep(200);
		if (of_property_read_u32(dvp2axi_dev->dev->of_node, OF_DVP2AXI_HDR_MODE, &dvp2axi_dev->hdr.hdr_mode))
			dvp2axi_dev->hdr.hdr_mode = NO_HDR;
	}
	return NOTIFY_DONE;
}

static int es_dvp2axi_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct es_dvp2axi_device *dvp2axi_dev;
	const struct es_dvp2axi_match_data *data;
	int ret;

	dev_info(dev, "es_dvp2axi driver version: v0.9\n");

	match = of_match_node(es_dvp2axi_plat_of_match, node);
	if (!match) {
		match = of_match_node(es_dvp2axi_plat_of_match_d1, node);
		if (!match)
			return -ENODEV;
	}
	data = match->data;

	dvp2axi_dev = devm_kzalloc(dev, sizeof(*dvp2axi_dev), GFP_KERNEL);
	if (!dvp2axi_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, dvp2axi_dev);
	dvp2axi_dev->dev = dev;

	if (sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp))
		return -ENODEV;
	ret = es_dvp2axi_attach_hw(dvp2axi_dev);
	if (ret)
		return ret;
	es_dvp2axi_parse_dts(dvp2axi_dev);
	ret = es_dvp2axi_plat_init(dvp2axi_dev, node, data->inf_id);
	if (ret) {
		es_dvp2axi_detach_hw(dvp2axi_dev);
		return ret;
	}

	dvp2axi_dev->of_notifier.notifier_call = dvp2axi_of_notifier;
	of_overlay_notifier_register(&dvp2axi_dev->of_notifier);
	dev_info(dvp2axi_dev->dev, "dvp2axi probe succsess!\n");

	return 0;
}

static int es_dvp2axi_plat_remove(struct platform_device *pdev)
{
	struct es_dvp2axi_device *dvp2axi_dev = platform_get_drvdata(pdev);

	es_dvp2axi_plat_uninit(dvp2axi_dev);
	es_dvp2axi_detach_hw(dvp2axi_dev);
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_grp);

	return 0;
}

static int __maybe_unused __es_dvp2axi_clr_unready_dev(void)
{
	struct es_dvp2axi_device *dvp2axi_dev = NULL;

	mutex_lock(&es_dvp2axi_dev_mutex);

	list_for_each_entry(dvp2axi_dev, &es_dvp2axi_device_list, list) {
		// v4l2_async_notifier_clr_unready_dev(&dvp2axi_dev->notifier);
		v4l2_async_nf_cleanup(&dvp2axi_dev->notifier);
	}

	mutex_unlock(&es_dvp2axi_dev_mutex);

	return 0;
}

MODULE_PARM_DESC(clr_unready_dev, "clear unready devices");

#ifndef MODULE
int es_dvp2axi_clr_unready_dev(void)
{
	__es_dvp2axi_clr_unready_dev();

	return 0;
}
#ifndef CONFIG_VIDEO_REVERSE_IMAGE
late_initcall(es_dvp2axi_clr_unready_dev);
#endif
#endif

struct platform_driver es_dvp2axi_plat_drv = {
	.driver = {
		.name = DVP2AXI_DRIVER_NAME,
		.of_match_table = of_match_ptr(es_dvp2axi_plat_of_match),
	},
	.probe = es_dvp2axi_plat_probe,
	.remove = es_dvp2axi_plat_remove,
};
EXPORT_SYMBOL(es_dvp2axi_plat_drv);

#ifdef CONFIG_NUMA
struct platform_driver es_dvp2axi_plat_drv_d1 = {
	.driver = {
		.name = DVP2AXI_DRIVER_NAME_D1,
		.of_match_table = of_match_ptr(es_dvp2axi_plat_of_match_d1),
	},
	.probe = es_dvp2axi_plat_probe,
	.remove = es_dvp2axi_plat_remove,
};
EXPORT_SYMBOL(es_dvp2axi_plat_drv_d1);
#endif

MODULE_AUTHOR("Eswin VI team");
MODULE_DESCRIPTION("Eswin DVP2AXI platform driver");
MODULE_LICENSE("GPL v2");
