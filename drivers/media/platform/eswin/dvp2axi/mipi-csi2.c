// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI CSI2 Driver
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include "../../../../phy/eswin/rk-camera-module.h"
#include <media/v4l2-ioctl.h>
#include "mipi-csi2.h"
#include <linux/regulator/consumer.h>

/* eic770x */
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include "common-def.h"
#include "dw-mipi-csi-hal.h"
#include "bmtest_vitop.h"

static int csi2_debug;
module_param_named(debug_csi2, csi2_debug, int, 0644);
MODULE_PARM_DESC(debug_csi2, "Debug level (0-1)");

// #define write_csihost_reg(base, addr, val) writel(val, (addr) + (base))
// #define read_csihost_reg(base, addr) readl((addr) + (base))

#define write_csihost_reg(base, addr, val) printk("t1 writel\n")
#define read_csihost_reg(base, addr) printk("t1 readel\n")

static ATOMIC_NOTIFIER_HEAD(g_csi_host_chain);

int rkcif_csi2_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&g_csi_host_chain, nb);
}

int rkcif_csi2_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&g_csi_host_chain, nb);
}

static inline struct csi2_dev *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi2_dev, sd);
}

static struct csi2_sensor_info *sd_to_sensor(struct csi2_dev *csi2,
					     struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < csi2->num_sensors; ++i)
		if (csi2->sensors[i].sd == sd)
			return &csi2->sensors[i];

	return NULL;
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RK_CSI2_PAD_SINK];
	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor, flag = %d, entity name = %s\n", local->flags, sd->entity.name);
		return NULL;
	}

	sensor_me = media_pad_remote_pad_first(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static void get_remote_terminal_sensor(struct v4l2_subdev *sd,
				       struct v4l2_subdev **sensor_sd)
{
	struct media_graph graph;
	struct media_entity *entity = &sd->entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	int ret;

	/* Walk the graph to locate sensor nodes. */
	mutex_lock(&mdev->graph_mutex);
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		*sensor_sd = NULL;
		return;
	}

	media_graph_walk_start(&graph, entity);
	while ((entity = media_graph_walk_next(&graph))) {
		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			break;
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);

	if (entity)
		*sensor_sd = media_entity_to_v4l2_subdev(entity);
	else
		*sensor_sd = NULL;
}

static void csi2_update_sensor_info(struct csi2_dev *csi2)
{
	struct v4l2_subdev *terminal_sensor_sd = NULL;
	struct csi2_sensor_info *sensor = &csi2->sensors[0];
	struct v4l2_mbus_config mbus;
	int ret = 0;

	ret = v4l2_subdev_call(sensor->sd, pad, get_mbus_config, 0, &mbus);
	if (ret) {
		v4l2_err(&csi2->sd, "update sensor info failed!\n");
		return;
	}

	get_remote_terminal_sensor(&csi2->sd, &terminal_sensor_sd);
	ret = v4l2_subdev_call(terminal_sensor_sd, core, ioctl,
			       RKMODULE_GET_CSI_DSI_INFO, &csi2->dsi_input_en);
	if (ret) {
		v4l2_dbg(1, csi2_debug, &csi2->sd,
			 "get CSI/DSI sel failed, default csi!\n");
		printk("get CSI/DSI sel failed, default csi!\n");
		csi2->dsi_input_en = 0;
	}

	csi2->bus = mbus.bus.mipi_csi2;
}

static void csi2_hw_do_reset(struct csi2_hw *csi2_hw)
{
	if (!csi2_hw->rsts_bulk)
		return;

	reset_control_assert(csi2_hw->rsts_bulk);

	udelay(5);

	reset_control_deassert(csi2_hw->rsts_bulk);
}

static int csi2_enable_clks(struct csi2_hw *csi2_hw)
{
	int ret = 0;

	if (!csi2_hw->clks_bulk)
		return -EINVAL;

	ret = clk_bulk_prepare_enable(csi2_hw->clks_num, csi2_hw->clks_bulk);
	if (ret)
		dev_err(csi2_hw->dev, "failed to enable clks\n");

	return ret;
}

static void csi2_disable_clks(struct csi2_hw *csi2_hw)
{
	if (!csi2_hw->clks_bulk)
		return;
	clk_bulk_disable_unprepare(csi2_hw->clks_num, csi2_hw->clks_bulk);
}

static void csi2_disable(struct csi2_hw *csi2_hw)
{
	write_csihost_reg(csi2_hw->base, CSIHOST_RESETN, 0);
	write_csihost_reg(csi2_hw->base, CSIHOST_MSK1, 0xffffffff);
	write_csihost_reg(csi2_hw->base, CSIHOST_MSK2, 0xffffffff);
}

static int csi2_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			      struct v4l2_mbus_config *mbus);

static void csi2_enable(struct csi2_hw *csi2_hw, enum host_type_t host_type)
{
	void __iomem *base = csi2_hw->base;
	struct csi2_dev *csi2 = csi2_hw->csi2;
	int lanes = csi2->bus.num_data_lanes;
	struct v4l2_mbus_config mbus;
	u32 val = 0;

	csi2_g_mbus_config(&csi2->sd, 0, &mbus);
	if (mbus.type == V4L2_MBUS_CSI2_DPHY)
		val = SW_CPHY_EN(0);
	else if (mbus.type == V4L2_MBUS_CSI2_CPHY)
		val = SW_CPHY_EN(1);

	write_csihost_reg(base, CSIHOST_N_LANES, lanes - 1);

	if (host_type == RK_DSI_RXHOST) {
		val |= SW_DSI_EN(1) | SW_DATATYPE_FS(0x01) |
		       SW_DATATYPE_FE(0x11) | SW_DATATYPE_LS(0x21) |
		       SW_DATATYPE_LE(0x31);
		write_csihost_reg(base, CSIHOST_CONTROL, val);
		/* Disable some error interrupt when HOST work on DSI RX mode */
		write_csihost_reg(base, CSIHOST_MSK1, 0xe00000f0);
		write_csihost_reg(base, CSIHOST_MSK2, 0xff00);
	} else {
		val |= SW_DSI_EN(0) | SW_DATATYPE_FS(0x0) |
		       SW_DATATYPE_FE(0x01) | SW_DATATYPE_LS(0x02) |
		       SW_DATATYPE_LE(0x03);
		write_csihost_reg(base, CSIHOST_CONTROL, val);
		write_csihost_reg(base, CSIHOST_MSK1, 0x0);
		write_csihost_reg(base, CSIHOST_MSK2, 0xf000);
		csi2->is_check_sot_sync = true;
	}

	write_csihost_reg(base, CSIHOST_RESETN, 1);
}

static int csi2_start(struct csi2_dev *csi2)
{
	enum host_type_t host_type;
	int ret, i;
	int csi_idx = 0;

	atomic_set(&csi2->frm_sync_seq, 0);

	csi2_update_sensor_info(csi2);

	if (csi2->dsi_input_en == RKMODULE_DSI_INPUT)
		host_type = RK_DSI_RXHOST;
	else
		host_type = RK_CSI_RXHOST;

	for (i = 0; i < csi2->csi_info.csi_num; i++) {
		csi_idx = csi2->csi_info.csi_idx[i];
		// csi2_hw_do_reset(csi2->csi2_hw[csi_idx]);
		// ret = csi2_enable_clks(csi2->csi2_hw[csi_idx]);
		// if (ret) {
		// 	v4l2_err(&csi2->sd, "%s: enable clks failed\n",
		// 		 __func__);
		// 	return ret;
		// }
		// enable_irq(csi2->csi2_hw[csi_idx]->irq1);
		// enable_irq(csi2->csi2_hw[csi_idx]->irq2);
		// csi2_enable(csi2->csi2_hw[csi_idx], host_type);
	}

	printk("stream sd: %s\n", csi2->src_sd->name);
	ret = v4l2_subdev_call(csi2->src_sd, video, s_stream, 1);
	ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
	if (ret)
		goto err_assert_reset;

	for (i = 0; i < RK_CSI2_ERR_MAX; i++)
		csi2->err_list[i].cnt = 0;

	return 0;

err_assert_reset:
	for (i = 0; i < csi2->csi_info.csi_num; i++) {
		csi_idx = csi2->csi_info.csi_idx[i];
		disable_irq(csi2->csi2_hw[csi_idx]->irq1);
		disable_irq(csi2->csi2_hw[csi_idx]->irq2);
		csi2_disable(csi2->csi2_hw[csi_idx]);
		csi2_disable_clks(csi2->csi2_hw[csi_idx]);
	}

	return ret;
}

static void csi2_stop(struct csi2_dev *csi2)
{
	int i = 0;
	int csi_idx = 0;

	/* stop upstream */
	v4l2_subdev_call(csi2->src_sd, video, s_stream, 0);

	for (i = 0; i < csi2->csi_info.csi_num; i++) {
		csi_idx = csi2->csi_info.csi_idx[i];
		disable_irq(csi2->csi2_hw[csi_idx]->irq1);
		disable_irq(csi2->csi2_hw[csi_idx]->irq2);
		csi2_disable(csi2->csi2_hw[csi_idx]);
		csi2_hw_do_reset(csi2->csi2_hw[csi_idx]);
		csi2_disable_clks(csi2->csi2_hw[csi_idx]);
	}
}

/*
 * V4L2 subdev operations.
 */

static int csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int ret = 0;

	mutex_lock(&csi2->lock);

	dev_err(csi2->dev, "stream %s, src_sd: %p, sd_name:%s\n",
		enable ? "on" : "off", csi2->src_sd, csi2->src_sd->name);

	/*
	 * enable/disable streaming only if stream_count is
	 * going from 0 to 1 / 1 to 0.
	 */
	if (csi2->stream_count != !enable)
		goto update_count;

	dev_err(csi2->dev, "stream %s\n", enable ? "ON" : "OFF");

	if (enable)
		ret = csi2_start(csi2);
	else
		csi2_stop(csi2);
	if (ret)
		goto out;

update_count:
	csi2->stream_count += enable ? 1 : -1;
	if (csi2->stream_count < 0)
		csi2->stream_count = 0;
out:
	mutex_unlock(&csi2->lock);

	printk("csi strat_stream ret = %d\n", ret);
	return ret;
}

static int csi2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	remote_sd = media_entity_to_v4l2_subdev(remote->entity);
	mutex_lock(&csi2->lock);

	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->sink_linked[local->index - 1]) {
				ret = -EBUSY;
				goto out;
			}
			csi2->sink_linked[local->index - 1] = true;
		} else {
			csi2->sink_linked[local->index - 1] = false;
		}
	} else {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->src_sd) {
				ret = -EBUSY;
				goto out;
			}
			csi2->src_sd = remote_sd;
		} else {
			csi2->src_sd = NULL;
		}
	}

out:
	mutex_unlock(&csi2->lock);
	return ret;
}
/* Media Entity */

static const struct media_entity_operations sun6i_mipi_csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int csi2_media_init(struct v4l2_subdev *sd)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int i = 0, num_pads = 0;
	int ret;

	num_pads = csi2->match_data->num_pads;

	for (i = 0; i < num_pads; i++) {
		csi2->pad[i].flags = (i == CSI2_SINK_PAD) ? MEDIA_PAD_FL_SINK :
							    MEDIA_PAD_FL_SOURCE;
	}
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	// sd->entity.ops = &sun6i_mipi_csi2_entity_ops;

	csi2->pad[RK_CSI2X_PAD_SOURCE0].flags = MEDIA_PAD_FL_SOURCE |
						MEDIA_PAD_FL_MUST_CONNECT;
	csi2->pad[RK_CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK |
					    MEDIA_PAD_FL_MUST_CONNECT;

	/* set a default mbus format  */
	csi2->format_mbus.code = MEDIA_BUS_FMT_UYVY8_2X8;
	csi2->format_mbus.field = V4L2_FIELD_NONE;
	csi2->format_mbus.width = RKCIF_DEFAULT_WIDTH;
	csi2->format_mbus.height = RKCIF_DEFAULT_HEIGHT;
	csi2->crop.top = 0;
	csi2->crop.left = 0;
	csi2->crop.width = RKCIF_DEFAULT_WIDTH;
	csi2->crop.height = RKCIF_DEFAULT_HEIGHT;
	csi2->bus.num_data_lanes = 2;

	DPRINTK("t1 csi2_media_init num_pads:%d, %s, %d, %d, %d, %d\n", 
	num_pads, sd->entity.name, csi2->pad[RK_CSI2X_PAD_SOURCE0].flags, 
	csi2->pad[RK_CSI2_PAD_SINK].flags, MEDIA_PAD_FL_SOURCE, MEDIA_PAD_FL_MUST_CONNECT);

	ret =  media_entity_pads_init(&sd->entity, num_pads, csi2->pad);

	printk("media_entity_pads_init ret = %d, csi entity major = %d, %d\n", 
	ret, sd->entity.info.dev.major, sd->entity.info.dev.minor);

	return ret;
}

/* csi2 accepts all fmt/size from sensor */
static int csi2_get_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	int ret;
	struct csi2_dev *csi2 = sd_to_dev(sd);
		printk("%s %d\n",__func__, __LINE__);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
	if (!ret)
		csi2->format_mbus = fmt->format;

	return ret;
}

static struct v4l2_rect *
mipi_csi2_get_crop(struct csi2_dev *csi2, struct v4l2_subdev_state *sd_state,
		   enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&csi2->sd, sd_state,
						RK_CSI2_PAD_SINK);
	else
		return &csi2->crop;
}

static int csi2_get_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_selection *sel)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	struct v4l2_subdev_format fmt;
	int ret = 0;

	if (!sel) {
		v4l2_dbg(1, csi2_debug, &csi2->sd, "sel is null\n");
		goto err;
	}

	if (sel->pad > RK_CSI2X_PAD_SOURCE3) {
		v4l2_dbg(1, csi2_debug, &csi2->sd, "pad[%d] isn't matched\n",
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
					csi2->format_mbus = fmt.format;
					sel->r.top = 0;
					sel->r.left = 0;
					sel->r.width = csi2->format_mbus.width;
					sel->r.height =
						csi2->format_mbus.height;
					csi2->crop = sel->r;
				} else {
					sel->r = csi2->crop;
				}
			} else {
				csi2->crop = sel->r;
			}
		} else {
			sel->r = *v4l2_subdev_get_try_crop(&csi2->sd, sd_state,
							   sel->pad);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *mipi_csi2_get_crop(csi2, sd_state, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
err:
	return -EINVAL;
}

static int csi2_set_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_selection *sel)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	printk("%s %d\n",__func__, __LINE__);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	int ret = 0;

	ret = v4l2_subdev_call(sensor, pad, set_selection, sd_state, sel);
	if (!ret)
		csi2->crop = sel->r;

	return ret;
}

static int csi2_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			      struct v4l2_mbus_config *mbus)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
		printk("%s %d\n",__func__, __LINE__);
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

static const struct media_entity_operations csi2_entity_ops = {
	.link_setup = csi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

void rkcif_csi2_event_reset_pipe(struct csi2_dev *csi2_dev, int reset_src)
{
	if (csi2_dev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_RESET_DEV,
			.reserved[0] = reset_src,
		};
		v4l2_event_queue(csi2_dev->sd.devnode, &event);
	}
}

void rkcif_csi2_event_inc_sof(struct csi2_dev *csi2_dev)
{
	if (csi2_dev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&csi2_dev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(csi2_dev->sd.devnode, &event);
	}
}

u32 rkcif_csi2_get_sof(struct csi2_dev *csi2_dev)
{
	if (csi2_dev)
		return atomic_read(&csi2_dev->frm_sync_seq) - 1;

	return 0;
}

void rkcif_csi2_set_sof(struct csi2_dev *csi2_dev, u32 seq)
{
	if (csi2_dev)
		atomic_set(&csi2_dev->frm_sync_seq, seq);
}

static int rkcif_csi2_subscribe_event(struct v4l2_subdev *sd,
				      struct v4l2_fh *fh,
				      struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_FRAME_SYNC ||
	    sub->type == V4L2_EVENT_RESET_DEV)
		return v4l2_event_subscribe(fh, sub, RKCIF_V4L2_EVENT_ELEMS,
					    NULL);
	else
		return -EINVAL;
}

static int rkcif_csi2_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static long rkcif_csi2_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
			     void *arg)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
		printk("%s %d, CMD = %d\n",__func__, __LINE__, cmd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	long ret = 0;
	int i = 0;

	switch (cmd) {
	case RKCIF_CMD_SET_CSI_IDX:
		csi2->csi_info = *((struct rkcif_csi_info *)arg);
		for (i = 0; i < csi2->csi_info.csi_num; i++)
			csi2->csi2_hw[csi2->csi_info.csi_idx[i]]->csi2 = csi2;
		if (csi2->match_data->chip_id > CHIP_RV1126_CSI2)
			ret = v4l2_subdev_call(sensor, core, ioctl,
					       RKCIF_CMD_SET_CSI_IDX, arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long rkcif_csi2_compat_ioctl32(struct v4l2_subdev *sd, unsigned int cmd,
				      unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkcif_csi_info csi_info;
	long ret;

	switch (cmd) {
	case RKCIF_CMD_SET_CSI_IDX:
		if (copy_from_user(&csi_info, up,
				   sizeof(struct rkcif_csi_info)))
			return -EFAULT;

		ret = rkcif_csi2_ioctl(sd, cmd, &csi_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops csi2_core_ops = {
	.subscribe_event = rkcif_csi2_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.s_power = rkcif_csi2_s_power,
	.ioctl = rkcif_csi2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rkcif_csi2_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops csi2_video_ops = {
	.s_stream = csi2_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_pad_ops = {
	.get_fmt = csi2_get_set_fmt,
	.set_fmt = csi2_get_set_fmt,
	.get_selection = csi2_get_selection,
	.set_selection = csi2_set_selection,
	.get_mbus_config = csi2_g_mbus_config,
};

static const struct v4l2_subdev_ops csi2_subdev_ops = {
	.core = &csi2_core_ops,
	.video = &csi2_video_ops,
	.pad = &csi2_pad_ops,
};

/* The .bound() notifier callback when a match is found */
static int csi2_notifier_bound(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd,
			       struct v4l2_async_connection *asd)
{
	struct csi2_dev *csi2 =
		container_of(notifier, struct csi2_dev, notifier);
	struct csi2_sensor_info *sensor;
	struct media_link *link;
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
		&sensor->sd->entity, pad, &csi2->sd.entity, RK_CSI2_PAD_SINK,
		0 /* csi2->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED */);
	if (ret) {
		dev_err(csi2->dev, "failed to create link for %s\n", sd->name);
		return ret;
	}

	// link = list_first_entry(&csi2->sd.entity.links, struct media_link,
	// 			list);
		link = list_first_entry(&sensor->sd->entity.links, struct media_link,
				list);
	ret = media_entity_setup_link(link, MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2->dev, "failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	printk("success create pad link %s, %d -> %s, %d, ret = %d\n", sensor->sd->entity.name, pad, csi2->sd.entity.name, RK_CSI2_PAD_SINK, ret);
	return 0;
}

/* The .unbind callback */
static void csi2_notifier_unbind(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_connection *asd)
{
	struct csi2_dev *csi2 =
		container_of(notifier, struct csi2_dev, notifier);
	struct csi2_sensor_info *sensor = sd_to_sensor(csi2, sd);

	if (sensor)
		sensor->sd = NULL;
	DPRINTK("t1 %s, %d succsess\n", __func__, __LINE__);
}

static const struct v4l2_async_notifier_operations csi2_async_ops = {
	.bound = csi2_notifier_bound,
	.unbind = csi2_notifier_unbind,
};

static void csi2_find_err_vc(int val, char *vc_info)
{
	int i;
	char cur_str[CSI_VCINFO_LEN] = { 0 };

	memset(vc_info, 0, sizeof(*vc_info));
	for (i = 0; i < 4; i++) {
		if ((val >> i) & 0x1) {
			snprintf(cur_str, CSI_VCINFO_LEN, " %d", i);
			if (strlen(vc_info) + strlen(cur_str) < CSI_VCINFO_LEN)
				strncat(vc_info, cur_str, strlen(cur_str));
		}
	}
}

#define csi2_err_strncat(dst_str, src_str)                              \
	{                                                               \
		if (strlen(dst_str) + strlen(src_str) < CSI_ERRSTR_LEN) \
			strncat(dst_str, src_str, strlen(src_str));     \
	}

static irqreturn_t rk_csirx_irq1_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_hw *csi2_hw = dev_get_drvdata(dev);
	struct csi2_dev *csi2 = NULL;
	struct csi2_err_stats *err_list = NULL;
	unsigned long err_stat = 0;
	u32 val;
	char err_str[CSI_ERRSTR_LEN] = { 0 };
	char cur_str[CSI_ERRSTR_LEN] = { 0 };
	char vc_info[CSI_VCINFO_LEN] = { 0 };
	bool is_add_cnt = false;

	if (!csi2_hw) {
		disable_irq_nosync(irq);
		return IRQ_HANDLED;
	}

	csi2 = csi2_hw->csi2;
	if (!csi2) {
		disable_irq_nosync(irq);
		return IRQ_HANDLED;
	}
	val = read_csihost_reg(csi2_hw->base, CSIHOST_ERR1);
	if (val) {
		if (val & CSIHOST_ERR1_PHYERR_SPTSYNCHS) {
			err_list = &csi2->err_list[RK_CSI2_ERR_SOTSYN];
			err_list->cnt++;
			if (csi2->match_data->chip_id == CHIP_RK3588_CSI2) {
				if (err_list->cnt > 3 &&
				    csi2->err_list[RK_CSI2_ERR_ALL].cnt <=
					    err_list->cnt) {
					csi2->is_check_sot_sync = false;
					write_csihost_reg(csi2_hw->base,
							  CSIHOST_MSK1, 0xf);
				}
				if (csi2->is_check_sot_sync) {
					csi2_find_err_vc(val & 0xf, vc_info);
					snprintf(cur_str, CSI_ERRSTR_LEN,
						 "(sot sync,lane:%s) ",
						 vc_info);
					csi2_err_strncat(err_str, cur_str);
				}
			} else {
				csi2_find_err_vc(val & 0xf, vc_info);
				snprintf(cur_str, CSI_ERRSTR_LEN,
					 "(sot sync,lane:%s) ", vc_info);
				csi2_err_strncat(err_str, cur_str);
				is_add_cnt = true;
			}
		}

		if (val & CSIHOST_ERR1_ERR_BNDRY_MATCH) {
			err_list = &csi2->err_list[RK_CSI2_ERR_FS_FE_MIS];
			err_list->cnt++;
			csi2_find_err_vc((val >> 4) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(fs/fe mis,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
			if (csi2->match_data->chip_id < CHIP_RK3588_CSI2)
				is_add_cnt = true;
		}

		if (val & CSIHOST_ERR1_ERR_SEQ) {
			err_list = &csi2->err_list[RK_CSI2_ERR_FRM_SEQ_ERR];
			err_list->cnt++;
			csi2_find_err_vc((val >> 8) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(f_seq,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}

		if (val & CSIHOST_ERR1_ERR_FRM_DATA) {
			err_list = &csi2->err_list[RK_CSI2_ERR_CRC_ONCE];
			is_add_cnt = true;
			err_list->cnt++;
			csi2_find_err_vc((val >> 12) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(err_data,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}

		if (val & CSIHOST_ERR1_ERR_CRC) {
			err_list = &csi2->err_list[RK_CSI2_ERR_CRC];
			err_list->cnt++;
			is_add_cnt = true;
			csi2_find_err_vc((val >> 24) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(crc,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}

		if (val & CSIHOST_ERR1_ERR_ECC2) {
			err_list = &csi2->err_list[RK_CSI2_ERR_CRC];
			err_list->cnt++;
			is_add_cnt = true;
			snprintf(cur_str, CSI_ERRSTR_LEN, "(ecc2) ");
			csi2_err_strncat(err_str, cur_str);
		}

		if (val & CSIHOST_ERR1_ERR_CTRL) {
			csi2_find_err_vc((val >> 16) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(ctrl,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}

		pr_err("%s ERR1:0x%x %s\n", csi2_hw->dev_name, val, err_str);

		if (is_add_cnt) {
			csi2->err_list[RK_CSI2_ERR_ALL].cnt++;
			err_stat =
				((csi2->err_list[RK_CSI2_ERR_FS_FE_MIS].cnt &
				  0xff)
				 << 8) |
				((csi2->err_list[RK_CSI2_ERR_ALL].cnt) & 0xff);

			atomic_notifier_call_chain(
				&g_csi_host_chain, err_stat,
				&csi2->csi_info
					 .csi_idx[csi2->csi_info.csi_num - 1]);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t rk_csirx_irq2_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_hw *csi2_hw = dev_get_drvdata(dev);
	u32 val;
	char cur_str[CSI_ERRSTR_LEN] = { 0 };
	char err_str[CSI_ERRSTR_LEN] = { 0 };
	char vc_info[CSI_VCINFO_LEN] = { 0 };

	if (!csi2_hw) {
		disable_irq_nosync(irq);
		return IRQ_HANDLED;
	}

	val = read_csihost_reg(csi2_hw->base, CSIHOST_ERR2);
	if (val) {
		if (val & CSIHOST_ERR2_PHYERR_ESC) {
			csi2_find_err_vc(val & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(ULPM,lane:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}
		if (val & CSIHOST_ERR2_PHYERR_SOTHS) {
			csi2_find_err_vc((val >> 4) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(sot,lane:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}
		if (val & CSIHOST_ERR2_ECC_CORRECTED) {
			csi2_find_err_vc((val >> 8) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(ecc,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}
		if (val & CSIHOST_ERR2_ERR_ID) {
			csi2_find_err_vc((val >> 12) & 0xf, vc_info);
			snprintf(cur_str, CSI_ERRSTR_LEN, "(err id,vc:%s) ",
				 vc_info);
			csi2_err_strncat(err_str, cur_str);
		}
		if (val & CSIHOST_ERR2_PHYERR_CODEHS) {
			snprintf(cur_str, CSI_ERRSTR_LEN, "(err code) ");
			csi2_err_strncat(err_str, cur_str);
		}

		pr_err("%s ERR2:0x%x %s\n", csi2_hw->dev_name, val, err_str);
	}

	return IRQ_HANDLED;
}

/* Parse fwnode with port0, if an empty function is used, each node will parse
 * all ports, causing the device to repeatedly join the link and unable to
 * complete the link
 */
static int csi2_fwnode_parse(struct csi2_dev *csi2)
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

		DPRINTK("t1 %s, %d, port:%d\n", __func__, __LINE__,
		       vep.base.port);

		/* only add fwnode form port 0 to notifier list */
		if (vep.base.port != 0)
			continue;

		remote_ep = fwnode_graph_get_remote_port_parent(ep);

		DPRINTK("t1 %s, %d remote_ep:%s\n", __func__, __LINE__,
		       fwnode_get_name(remote_ep));

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
	DPRINTK("t1 %s, %d \n", __func__, __LINE__);
	return 0;

err_parse:
	DPRINTK("t1 %s, %d \n", __func__, __LINE__);
	fwnode_handle_put(ep);
	return ret;
}

static int csi2_notifier(struct csi2_dev *csi2)
{
	struct v4l2_async_notifier *ntf = &csi2->notifier;
	int ret;

	ret = csi2_fwnode_parse(csi2);
	if (ret < 0)
		return ret;
	DPRINTK("t1 %s, %d \n", __func__, __LINE__);
	csi2->sd.subdev_notifier = &csi2->notifier;
	csi2->notifier.ops = &csi2_async_ops;
	DPRINTK("t1 %s, %d \n", __func__, __LINE__);
	ret = v4l2_async_nf_register(&csi2->notifier);
	if (ret) {
		dev_err(csi2->dev, "fail to register async notifier: %d\n",
			ret);
		v4l2_async_nf_cleanup(&csi2->notifier);
	}
	DPRINTK("t1 %s, %d \n", __func__, __LINE__);
	ret = v4l2_async_register_subdev(&csi2->sd);
	DPRINTK("t1 %s %d. sd->name:%s\n", __func__, __LINE__, csi2->sd.name);
	DPRINTK("t1 %s, %d done\n", __func__, __LINE__);
	return ret;
}

static const struct csi2_match_data rk1808_csi2_match_data = {
	.chip_id = CHIP_RK3588_CSI2,
	.num_pads = CSI2_NUM_PADS,
	.num_hw = 1,
};
static const struct of_device_id csi2_dt_ids[] = {
	{
		.compatible = "eswin,eic770x-mipi-csi2",
		.data = &rk1808_csi2_match_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, csi2_dt_ids);

static int csi2_attach_hw(struct csi2_dev *csi2)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct csi2_hw *hw;
	int i = 0;

	for (i = 0; i < csi2->match_data->num_hw; i++) {
		np = of_parse_phandle(csi2->dev->of_node, "eswin,hw", i);
		if (!np || !of_device_is_available(np)) {
			dev_err(csi2->dev, "failed to get csi2 hw node\n");
			return -ENODEV;
		}

		pdev = of_find_device_by_node(np);
		of_node_put(np);
		if (!pdev) {
			dev_err(csi2->dev, "failed to get csi2 hw from node\n");
			return -ENODEV;
		}

		hw = platform_get_drvdata(pdev);
		if (!hw) {
			dev_err(csi2->dev, "failed attach csi2 hw\n");
			return -EINVAL;
		}

		hw->csi2 = csi2;
		csi2->csi2_hw[i] = hw;
	}
	dev_info(csi2->dev, "attach to csi2 hw node\n");

	return 0;
}

static int csi2_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct csi2_dev *csi2 = NULL;
	const struct csi2_match_data *data;
	int ret;

	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	match = of_match_node(csi2_dt_ids, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	DPRINTK("t1 chip_id:%d, num_pads:%d, num_hw:%d \n", data->chip_id,
	       data->num_pads, data->num_hw);

	csi2 = devm_kzalloc(&pdev->dev, sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return -ENOMEM;

	csi2->dev = &pdev->dev;
	csi2->match_data = data;

	csi2->dev_name = node->name;
	DPRINTK("t1 dev_name:%s\n", csi2->dev_name);

	/* Initialize V4L2 subdevice and media entity */
	v4l2_subdev_init(&csi2->sd, &csi2_subdev_ops);
	v4l2_set_subdevdata(&csi2->sd, &pdev->dev);
	csi2->sd.entity.ops = &csi2_entity_ops;
	csi2->sd.dev = &pdev->dev;
	csi2->sd.owner = THIS_MODULE;
	csi2->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			  V4L2_SUBDEV_FL_HAS_EVENTS;
	ret = strscpy(csi2->sd.name, DEVICE_NAME, sizeof(csi2->sd.name));
	if (ret < 0)
		v4l2_err(&csi2->sd, "failed to copy name\n");
	platform_set_drvdata(pdev, &csi2->sd);

	ret = csi2_attach_hw(csi2);
	if (ret) {
		v4l2_err(&csi2->sd, "must enable all mipi csi2 hw node\n");
		return -EINVAL;
	}
	mutex_init(&csi2->lock);
	DPRINTK("t1 %s %d. sd->name:%s\n", __func__, __LINE__, csi2->sd.name);
	ret = csi2_media_init(&csi2->sd);
	if (ret < 0)
		goto rmmutex;
	ret = csi2_notifier(csi2);
	if (ret)
		goto rmmutex;

	v4l2_info(&csi2->sd, "probe success, v4l2_dev:%s!\n",
		  csi2->sd.v4l2_dev->name);

	return 0;

rmmutex:
	mutex_destroy(&csi2->lock);
	return ret;
}

static int csi2_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	v4l2_async_unregister_subdev(sd);
	mutex_destroy(&csi2->lock);
	media_entity_cleanup(&sd->entity);

	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);

	return 0;
}

static struct platform_driver csi2_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = csi2_dt_ids,
	},
	.probe = csi2_probe,
	.remove = csi2_remove,
};

int rkcif_csi2_plat_drv_init(void)
{
	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	return platform_driver_register(&csi2_driver);
}

void rkcif_csi2_plat_drv_exit(void)
{
	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	platform_driver_unregister(&csi2_driver);
}

static const struct csi2_hw_match_data rk1808_csi2_hw_match_data = {
	.chip_id = CHIP_RK3588_CSI2,
};

static const struct of_device_id csi2_hw_ids[] = {
	{
		.compatible = "eswin,eic770x-mipi-csi2-hw",
		.data = &rk1808_csi2_hw_match_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, csi2_hw_ids);

static int viscu_cfg(struct device *dev)
{
	struct regmap *regmap;
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
    if (IS_ERR(regmap)) {
        dev_err(dev, "No syscrg_csr phandle specified\n");
        return -ENODEV;
    }

	regmap_write(regmap, VI_SUBSYSTEM_SCU_ISP_RESET, 0x0);///isp reset(sys_crg)
	DPRINTK("SCU: ISP Reset ...\n");
	regmap_write(regmap, VI_SUBSYSTEM_SCU_ISP_RESET, 0x1);

	// CSI controller 0 HDR mode
	DPRINTK("SCU: HDR Disabled ...\n");
    // regmap_write(regmap, VI_SUBSYSTEM_SCU_SONY_HDR_MODE, 0x0);///i2c_rst_ctl(sys_crg)
	return 0;
}

static int vitop_intf_cfg(struct device *dev)
{
	struct regmap *regmap;
	u32 val = 0;
	unsigned int reg_value;

	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);

    regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(regmap)) {
        dev_err(dev, "No vi_top_csr phandle specified\n");
        return -ENODEV;
    }

	// Enable Clocks from TOP CSR
	DPRINTK("ISP Top Setting ...\n");
	val = VI_TOP_ISP0_CLOCK_ENABLED | VI_TOP_ISP1_CLOCK_ENABLED; //ISP0 ISP1 Core Clock
    val |= VI_TOP_CTRL0_DVP_CLOCK_ENABLED | VI_TOP_CTRL1_DVP_CLOCK_ENABLED; //DVP Input Interface Clock: MIPI Controller 0,1
    val |= VI_TOP_DVP2AXI_CLOCK_ENABLED; // DVP2AXI clock
	regmap_write(regmap, VI_TOP_CLOCK_ENABLE, val);
	regmap_read(regmap, VI_TOP_CLOCK_ENABLE, &reg_value);
	DPRINTK("ISP_TOP_CLOCK_EN[0x51030040] = %x\n", reg_value);

	// ISP CSI0-DVP0 Input
    #ifdef SENSOR_OUT_2LANES
	regmap_write(regmap, VI_TOP_PHY_CONNECT_MODE, 5);
    #else
	regmap_write(regmap, VI_TOP_PHY_CONNECT_MODE, 3);
    #endif
	regmap_write(regmap, VI_TOP_CONTROLLER_SELECT, 0);//all from csi

	// isp0 and isp1: all dvp port from csi0
    val = (CSI_CONTROLLER_ID << VI_TOP_ISP0_DVP0_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP0_DVP1_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP0_SEL_OFFSET);
    val |= (CSI_CONTROLLER_ID << VI_TOP_ISP1_DVP1_SEL_OFFSET);
	regmap_write(regmap, VI_TOP_ISP_DVP_SEL, val);
	regmap_write(regmap, VI_TOP_ISP0_DVP0_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	regmap_write(regmap, VI_TOP_ISP0_DVP1_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	// regmap_write(regmap, VI_TOP_ISP0_DVP2_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	// regmap_write(regmap, VI_TOP_ISP0_DVP3_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	regmap_write(regmap, VI_TOP_ISP1_DVP0_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	regmap_write(regmap, VI_TOP_ISP1_DVP1_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	// regmap_write(regmap, VI_TOP_ISP1_DVP2_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	// regmap_write(regmap, VI_TOP_ISP1_DVP3_SIZE, (SENSOR_OUT_V << 16) | SENSOR_OUT_H);
	regmap_write(regmap, VI_TOP_MULTI2ISP_BLANK, (0xe0 << 8) | 0xe0);
	regmap_write(regmap, VI_TOP_MULTI2ISP0_DVP0, (4 << 9));
	regmap_write(regmap, VI_TOP_MULTI2ISP0_DVP1, (4 << 9));
	regmap_write(regmap, VI_TOP_MULTI2ISP1_DVP0, (4 << 9));
	regmap_write(regmap, VI_TOP_MULTI2ISP1_DVP1, (4 << 9));
    // regmap_write(regmap, VI_TOP_MULTI2ISP0_DVP2, (4 << 9));
	// regmap_write(regmap, VI_TOP_MULTI2ISP0_DVP3, (4 << 9));
	// regmap_write(regmap, VI_TOP_MULTI2ISP1_DVP2, (4 << 9));
	// regmap_write(regmap, VI_TOP_MULTI2ISP1_DVP3, (4 << 9));
	regmap_read(regmap, VI_TOP_PHY_CONNECT_MODE, &reg_value);
    DPRINTK("t2 VI_TOP_PHY_CONNECT_MODE[0x51030000] = %x\n", reg_value);

	regmap_read(regmap, VI_TOP_ISP_DVP_SEL, &reg_value);
	DPRINTK("ISP0_DVP_SEL[0x51030008] = %x\n", reg_value);

	// regmap_write(regmap, VI_TOP_ISP_DVP_SEL, 1);;
	// regmap_read(regmap, VI_TOP_ISP_DVP_SEL, &reg_value);
	// DPRINTK("ISP0_DVP_SEL[0x51030008] = %x\n", reg_value);

	regmap_read(regmap, VI_TOP_ISP0_DVP0_SIZE, &reg_value);
    DPRINTK("ISP0_DVP0 size[0x5103000c] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_ISP0_DVP1_SIZE, &reg_value);
    DPRINTK("ISP0_DVP1 size[0x51030010] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_ISP1_DVP0_SIZE, &reg_value);
    DPRINTK("ISP1_DVP0 size[0x51030014] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_ISP1_DVP1_SIZE, &reg_value);
    DPRINTK("ISP1_DVP1 size[0x51030018] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_MULTI2ISP_BLANK, &reg_value);
    DPRINTK("ISP_MUL2ISP_BLANK[0x5103001c] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_MULTI2ISP0_DVP0, &reg_value);
    DPRINTK("ISP0_DVP0 multi[0x51030020] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_MULTI2ISP0_DVP1, &reg_value);
    DPRINTK("ISP0_DVP1 multi[0x51030024] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_MULTI2ISP1_DVP0, &reg_value);
    DPRINTK("ISP1_DVP0 multi[0x51030028] = %x\n", reg_value);
	regmap_read(regmap, VI_TOP_MULTI2ISP1_DVP1, &reg_value);
    DPRINTK("ISP1_DVP1 multi[0x5103002c] = %x\n", reg_value);

	return 0;
}

static int eic770x_vi_init(struct device *dev)
{
    struct regmap *regmap;
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);

    regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
    if (IS_ERR(regmap)) {
        dev_err(dev, "No syscrg_csr phandle specified\n");
        return -ENODEV;
    }

	regmap_write(regmap, 0x200, 0xffffffff);///lsp_clk_en0 enable(sys_crg)
    regmap_write(regmap, 0x424, 0x3ff);///i2c_rst_ctl(sys_crg)
    udelay(1000);

    ///(sys_crg)
    regmap_write(regmap, 0x470, 0x7);///vi_rst_ctl
    regmap_write(regmap, 0x474, 0x1);///dvp_rst_ctl
    regmap_write(regmap, 0x478, 0x1);///isp0_rst_ctl
    regmap_write(regmap, 0x47c, 0x1);///isp1_rst_ctl
    regmap_write(regmap, 0x480, 0x1);///shutter_rst_ctl

    udelay(20000);

    regmap_write(regmap, 0x184, 0x80000020);///vi_dwclk_ctl
    regmap_write(regmap, 0x188, 0xc0000020);///vi_aclk_ctl
    regmap_write(regmap, 0x18c, 0x80000021);///vi_dig_isp_clk_ctl
    regmap_write(regmap, 0x190, 0x80000021);///vi_dvp_clk_ctl
    regmap_write(regmap, 0x194, 0x80000180);///vi_shutter0
    regmap_write(regmap, 0x198, 0x80000180);///vi_shutter1
    regmap_write(regmap, 0x19c, 0x80000100);///vi_shutter2
    regmap_write(regmap, 0x1a0, 0x80000100);///vi_shutter3
    regmap_write(regmap, 0x1a4, 0x80000100);///vi_shutter4
    regmap_write(regmap, 0x1a8, 0x80000100);///vi_shutter5
    regmap_write(regmap, 0x1ac, 0x3);///vi_phy_clk_ctl

    regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(regmap)) {
        dev_err(dev, "No vi_top_csr phandle specified\n");
        return -ENODEV;
    }

	regmap_write(regmap, 0x40, 0xffffffff);///vi_clk_en(vi_common_top_2.4.xlsx)
    udelay(20000);

	viscu_cfg(dev);///isp_rst(sys_crg)

    vitop_intf_cfg(dev);///(vi_top_cfg)

    return 0;
}

static inline void eic770x_dw_mipi_csi_write(struct csi2_hw *csi2_hw, u32 address, u32 data) {
    DPRINTK("csi [%08x]: %08x\n", csi2_hw->base + address, data);
    writel(data, csi2_hw->base + address);
    // writel(data, csi2_hw->base + address);
    // writel(data, csi2_hw->base + address);
}

static inline u32 eic770x_dw_mipi_csi_read(struct csi2_hw *csi2_hw, u32 address) {
    u32 val;

#ifdef REG_DUMMY_READ
    readl(csi2_hw->base + address);
    // readl(csi2_hw->base + address);
    // readl(csi2_hw->base + address);
#endif

    val = readl(csi2_hw->base + address);
    // DBG_PRINT("csi RD[%08x]: %08x\n", dev->base_address+address, val);
    return val;
}

void eic770x_dw_mipi_csi_write_part(struct csi2_hw *csi2_hw, u32 address, u32 data, u8 shift, u8 width) {
    u32 mask = (1 << width) - 1;
    u32 temp = eic770x_dw_mipi_csi_read(csi2_hw, address);

    temp &= ~(mask << shift);
    temp |= (data & mask) << shift;
    eic770x_dw_mipi_csi_write(csi2_hw, address, temp);
}

void eic770x_mipi_csi_regs_dump(struct csi2_hw *csi2_hw) {
    int i;
    DPRINTK("base=%08x\n", csi2_hw->base);

    for (i = 0; i < 20; i++) {
        DPRINTK("[%03x] = %08x\n", i * 4, eic770x_dw_mipi_csi_read(csi2_hw, i * 4));
    }

    /* IPI1 */
    for (i = 20; i < 50; i++) {
        DPRINTK("[%03x] = %08x\n", i * 4, eic770x_dw_mipi_csi_read(csi2_hw, i * 4));
    }

    /* IPI2 */
    for (i = 128; i < 135; i++) {
        DPRINTK("[%03x] = %08x\n", i * 4, eic770x_dw_mipi_csi_read(csi2_hw, i * 4));
    }
}

static void eic770x_mipi_csi_enable_irq(struct csi2_hw *csi2_hw) {
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    eic770x_dw_mipi_csi_write(csi2_hw, 0xe4, 0x1FF);
    eic770x_dw_mipi_csi_write(csi2_hw, 0xf4, 0x3);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x114, 0xff00ff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x134, 0xff00ff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x144, 0x7f);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x184, 0x3ffffff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x194, 0x3f);
#ifdef SENSOR_HDR_STAGGER2
    eic770x_dw_mipi_csi_write(csi2_hw, 0x154, 0x3f);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x1a4, 0x3f);
#endif
    eic770x_dw_mipi_csi_write(csi2_hw, 0x2a4, 0xffffffff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x2b4, 0xffffffff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x2c4, 0xffffffff);
    eic770x_dw_mipi_csi_write(csi2_hw, 0x2d4, 0xffffffff);
}

void eic770x_dw_mipi_csi_reset(struct csi2_hw *csi2_hw) {
    DPRINTK("**** reset controller ****\n");
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    eic770x_dw_mipi_csi_write(csi2_hw, CSI2_RESETN, 0);
    udelay(1000);
    eic770x_dw_mipi_csi_write(csi2_hw, CSI2_RESETN, 1);
    udelay(50000);
}

void eic770x_dw_mipi_ppi_pg_pattern_enable(struct csi2_hw *csi2_hw, int enable) {
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    if (enable)
        eic770x_dw_mipi_csi_write(csi2_hw, PPI_PG_ENABLE, 1);
    else
        eic770x_dw_mipi_csi_write(csi2_hw, PPI_PG_ENABLE, 0);
}

void eic770x_dw_mipi_csi_hw_cfg(struct csi2_hw *csi2_hw) {
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    /* Configure PHY mode  */
    eic770x_dw_mipi_csi_write(csi2_hw, PHY_MODE, csi2_hw->phy_mode);
    /* Configure number of lanes */
    eic770x_dw_mipi_csi_write(csi2_hw, N_LANES, csi2_hw->num_lanes - 1);
    /* Configure PPI width */
    eic770x_dw_mipi_csi_write(csi2_hw, PHY_CFG, csi2_hw->ppi_width);

#ifdef SENSOR_HDR_STAGGER2
    /* select IPI virtual channal */
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VCID, csi2_hw->vc);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI2_VCID, csi2_hw->vc + 1);
    /* select IPI data type */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_DATA_TYPE, csi2_hw->dt, 0, 6);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_DATA_TYPE, csi2_hw->dt, 0, 6);
    /* configure embedded data */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_DATA_TYPE, csi2_hw->emb, 8, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_DATA_TYPE, csi2_hw->emb, 8, 1);

    /* enable IPI mode */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, 1, 24, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_MODE, 1, 24, 1);
    /* Configure IPI MODE */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_mode, 0, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_MODE, csi2_hw->ipi_mode, 0, 1);
    /* Configure IPI data interface */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_color_com, 8, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_MODE, csi2_hw->ipi_color_com, 8, 1);
    /* Configure IPI cut through */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_cut_through, 16, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_MODE, csi2_hw->ipi_cut_through, 16, 1);
    /* Configure IPI MEM flush */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MEM_FLUSH, csi2_hw->ipi_auto_flush, 8, 1);
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_MEM_FLUSH, csi2_hw->ipi_auto_flush, 8, 1);

    if (csi2_hw->ipi_mode == CAMERA_TIMING) {
        /* TODO: Configure line event selection */
        eic770x_dw_mipi_csi_write(csi2_hw, IPI_ADV_FEATURES, csi2_hw->ipi_line_event);
        eic770x_dw_mipi_csi_write(csi2_hw, IPI2_ADV_FEATURES, csi2_hw->ipi_line_event);
        /* Configure ipi sync event mode */
        eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_ADV_FEATURES, csi2_hw->frame_det, 24, 1);
        eic770x_dw_mipi_csi_write_part(csi2_hw, IPI2_ADV_FEATURES, csi2_hw->frame_det, 24, 1);
    }

    // dw_mipi_csi_write_part(csi_dev, IPI_SOFTRSTN, 1, 0, 1);
    // dw_mipi_csi_write_part(csi_dev, IPI_SOFTRSTN, 1, 4, 1);

    /* Configure the IPI horizontal frame information*/
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HSA_TIME, csi2_hw->hsa);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI2_HSA_TIME, csi2_hw->hsa);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HBP_TIME, csi2_hw->hbp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI2_HBP_TIME, csi2_hw->hbp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HSD_TIME, csi2_hw->hsd);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI2_HSD_TIME, csi2_hw->hsd);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HLINE_TIME, csi2_hw->htotal);

    /*Configure the IPI vertical frame information */
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VSA_LINES, csi2_hw->vsa);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VBP_LINES, csi2_hw->vbp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VFP_LINES, csi2_hw->vfp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VACTIVE_LINES, csi2_hw->vactive);

    eic770x_dw_mipi_csi_write(csi2_hw, IPI_SOFTRSTN, 0x11);
#else
    /* select IPI virtual channal */
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VCID, csi2_hw->vc);
    /* select IPI data type */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_DATA_TYPE, csi2_hw->dt, 0, 6);
    /* configure embedded data */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_DATA_TYPE, csi2_hw->emb, 8, 1);

    /* enable IPI mode */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, 1, 24, 1);
    /* Configure IPI MODE */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_mode, 0, 1);
    /* Configure IPI data interface */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_color_com, 8, 1);
    /* Configure IPI cut through */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MODE, csi2_hw->ipi_cut_through, 16, 1);
    /* Configure IPI MEM flush */
    eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_MEM_FLUSH, csi2_hw->ipi_auto_flush, 8, 1);

    if (csi2_hw->ipi_mode == CAMERA_TIMING) {
        /* TODO: Configure line event selection */
        eic770x_dw_mipi_csi_write(csi2_hw, IPI_ADV_FEATURES, csi2_hw->ipi_line_event);
        /* Configure ipi sync event mode */
        eic770x_dw_mipi_csi_write_part(csi2_hw, IPI_ADV_FEATURES, csi2_hw->frame_det, 24, 1);
    }

    eic770x_dw_mipi_csi_write(csi2_hw, IPI_ADV_FEATURES, 0xb0000);

    // dw_mipi_csi_write_part(csi_dev, IPI_SOFTRSTN, 1, 0, 1);

    /* Configure the IPI horizontal frame information*/
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HSA_TIME, csi2_hw->hsa);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HBP_TIME, csi2_hw->hbp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HSD_TIME, csi2_hw->hsd);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_HLINE_TIME, csi2_hw->htotal);

    /*Configure the IPI vertical frame information */
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VSA_LINES, csi2_hw->vsa);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VBP_LINES, csi2_hw->vbp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VFP_LINES, csi2_hw->vfp);
    eic770x_dw_mipi_csi_write(csi2_hw, IPI_VACTIVE_LINES, csi2_hw->vactive);

    eic770x_dw_mipi_csi_write(csi2_hw, IPI_SOFTRSTN, 1);
    udelay(1000);
#endif

#define DUMP_CSI_REGS
#ifdef DUMP_CSI_REGS
    eic770x_mipi_csi_regs_dump(csi2_hw);
#endif
}

int eic770x_mipi_csi2_init(struct csi2_hw *csi2_hw, uint32_t controller_id)
{
	int irq_num;
    // struct csi_data *hw = &csi_dev.hw;

    DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
    // csi_dev.base_address = (volatile void *)(MIPI_CSI_REG_ADDR_0 + MIPI_CSI_REG_SIZE_0 * controller_id);
    // printf("--%s : csi base addr is 0x%x\n", __func__, csi_dev.base_address);

#ifdef SENSOR_OUT_2LANES
    csi2_hw->num_lanes = 2;
#else
    hw->num_lanes = 4;
#endif
    csi2_hw->ppi_width = D_PHY_PPI_8;
    csi2_hw->phy_mode = D_PHY_MODE;

    csi2_hw->vc = 0;
#ifdef SENSOR_OUT_12BIT
    csi2_hw->dt = CSI_2_RAW12;
#else
    csi2_hw->dt = CSI_2_RAW10;
#endif

    csi2_hw->emb = 1;
    csi2_hw->ipi_mode = CAMERA_TIMING;
    csi2_hw->ipi_color_com = IPI_DATA_16_BIT;
    csi2_hw->ipi_auto_flush = 0;  // TODO
    csi2_hw->ipi_cut_through = IPI_CTACTIVE;

#if 0  // legacy mode with manual selection
    csi2_hw->frame_det = 1;
    hw->ipi_line_event = EN_NULL_BIT | EN_EMBEDDED_BIT | EN_VIDEO_BIT;
#else  // default mode with manual selection (video only)
    csi2_hw->frame_det = 0;
    csi2_hw->ipi_line_event = EN_NULL_BIT | EN_VIDEO_BIT;
#endif

    csi2_hw->ipi_line_event |= LINE_EVENT_SELECTION_BIT;

    // hw->ipi_line_event = EN_EMBEDDED_BIT | EN_VIDEO_BIT;

    csi2_hw->hsa = 1;
    csi2_hw->hbp = 1;
    csi2_hw->hsd = 1;
    csi2_hw->htotal = 0;

    csi2_hw->vsa = 0;
    csi2_hw->vbp = 0;
    csi2_hw->vfp = 0;
    csi2_hw->vactive = 0;

    // mipi_csi_print_cfg_info(&csi_dev);

    // enable interrupt
    switch (controller_id) {
        case CSI_CONTROLLER_ID0:
            /* code */
            irq_num = MIPI_CSI_IRQ_NUM_0;
            break;
        case CSI_CONTROLLER_ID1:
            irq_num = MIPI_CSI_IRQ_NUM_1;
            /* code */
            break;
        case CSI_CONTROLLER_ID2:
            irq_num = MIPI_CSI_IRQ_NUM_2;
            /* code */
            break;
        case CSI_CONTROLLER_ID3:
            irq_num = MIPI_CSI_IRQ_NUM_3;
            /* code */
            break;
        case CSI_CONTROLLER_ID4:
            irq_num = MIPI_CSI_IRQ_NUM_4;
            /* code */
            break;
        case CSI_CONTROLLER_ID5:
            irq_num = MIPI_CSI_IRQ_NUM_5;
            /* code */
            break;
        default:
            break;
    }

	eic770x_mipi_csi_enable_irq(csi2_hw);
    // eic770x_metal_external_interrupt_register(irq_num, mipi_csi_irq_handler, INTERRUPT_ISP_PRIORITY, &csi_dev);///TODO
    // eic770x_metal_interrupt_ext_irq_enable(irq_num);///TODO

    eic770x_dw_mipi_csi_reset(csi2_hw);

    // /* disabel pattern */
    eic770x_dw_mipi_ppi_pg_pattern_enable(csi2_hw, 0);

    eic770x_dw_mipi_csi_hw_cfg(csi2_hw);
	return 0;
}

int eic770x_mipi_csi2_cfg(struct device* dev, struct csi2_hw *csi2_hw, uint32_t controller_id)
{
	DPRINTK("t2 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	eic770x_mipi_csi2_init(csi2_hw, controller_id);

	struct regmap *regmap;
	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
    if (IS_ERR(regmap)) {
        dev_err(dev, "No syscrg_csr phandle specified\n");
        return -ENODEV;
    }
	regmap_write(regmap, 0x40, 0xffffffff);///lsp_clk_en0 enable(sys_crg)
	return 0;
}

static int csi2_hw_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct csi2_hw *csi2_hw = NULL;
	struct resource *res;
	const struct csi2_hw_match_data *data;
	int ret, irq;

	dev_info(&pdev->dev, "enter mipi csi2 hw probe!\n");
	match = of_match_node(csi2_hw_ids, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	csi2_hw = devm_kzalloc(&pdev->dev, sizeof(*csi2_hw), GFP_KERNEL);
	if (!csi2_hw)
		return -ENOMEM;

	csi2_hw->dev = &pdev->dev;
	csi2_hw->match_data = data;

	csi2_hw->dev_name = node->name;

	DPRINTK("t1 %s, %s, %d csi2_hw->dev_name:%s\n", __FILE__, __func__,
	       __LINE__, csi2_hw->dev_name);

	// csi2_hw->clks_num = devm_clk_bulk_get_all(dev, &csi2_hw->clks_bulk);
	// if (csi2_hw->clks_num < 0) {
	// 	csi2_hw->clks_num = 0;
	// 	dev_err(dev, "failed to get csi2 clks\n");
	// }

	// csi2_hw->rsts_bulk = devm_reset_control_array_get_optional_exclusive(dev);
	// if (IS_ERR(csi2_hw->rsts_bulk)) {
	// 	if (PTR_ERR(csi2_hw->rsts_bulk) != -EPROBE_DEFER)
	// 		dev_err(dev, "failed to get csi2 reset\n");
	// 	csi2_hw->rsts_bulk = NULL;
	// }

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	pr_info("Resource name: %s\n", res->name ? res->name : "(null)");
	csi2_hw->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(csi2_hw->base)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		dev_warn(&pdev->dev, "avoid secondary mipi resource check!\n");

		csi2_hw->base = devm_ioremap(&pdev->dev, offset, size);
		if (IS_ERR(csi2_hw->base)) {
			dev_err(&pdev->dev, "Failed to ioremap resource\n");

			return PTR_ERR(csi2_hw->base);
		}
		printk("ioremap csi2_hw base failed\n");
		return -1;
	}

	eic770x_vi_init(dev);

	eic770x_mipi_csi2_cfg(dev, csi2_hw, CSI_CONTROLLER_ID);

	// irq = platform_get_irq_byname(pdev, "csi-intr1");
	// if (irq > 0) {
	// 	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	// 	ret = devm_request_irq(&pdev->dev, irq, rk_csirx_irq1_handler,
	// 			       0, dev_driver_string(&pdev->dev),
	// 			       &pdev->dev);
	// 	if (ret < 0)
	// 		dev_err(&pdev->dev,
	// 			"request csi-intr1 irq failed: %d\n", ret);
	// 	csi2_hw->irq1 = irq;
	// } else {
	// 	dev_err(&pdev->dev, "No found irq csi-intr1\n");
	// }

	// irq = platform_get_irq_byname(pdev, "csi-intr2");
	// if (irq > 0) {
	// 	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	// 	ret = devm_request_irq(&pdev->dev, irq, rk_csirx_irq2_handler,
	// 			       0, dev_driver_string(&pdev->dev),
	// 			       &pdev->dev);
	// 	if (ret < 0)
	// 		dev_err(&pdev->dev, "request csi-intr2 failed: %d\n",
	// 			ret);
	// 	csi2_hw->irq2 = irq;
	// } else {
	// 	dev_err(&pdev->dev, "No found irq csi-intr2\n");
	// }

	platform_set_drvdata(pdev, csi2_hw);
	dev_info(&pdev->dev, "probe success, v4l2_dev:%s!\n",
		 csi2_hw->dev_name);

	return 0;
}

static int csi2_hw_remove(struct platform_device *pdev)
{
	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	return 0;
}

static struct platform_driver csi2_hw_driver = {
	.driver = {
		.name = DEVICE_NAME_HW,
		.of_match_table = csi2_hw_ids,
	},
	.probe = csi2_hw_probe,
	.remove = csi2_hw_remove,
};

int rkcif_csi2_hw_plat_drv_init(void)
{
	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	return platform_driver_register(&csi2_hw_driver);
}

void rkcif_csi2_hw_plat_drv_exit(void)
{
	DPRINTK("t1 %s, %s, %d \n", __FILE__, __func__, __LINE__);
	platform_driver_unregister(&csi2_hw_driver);
}

MODULE_DESCRIPTION("Eswin MIPI CSI2 driver");
MODULE_AUTHOR("lilijun@eswincomputing.com");
MODULE_LICENSE("GPL v2");
