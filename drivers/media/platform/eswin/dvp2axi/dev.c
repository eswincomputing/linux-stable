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
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include "dev.h"
#include "procfs.h"
#include <linux/kthread.h>
#include "phy-es-csi2-dphy-common.h"
#include <linux/of_reserved_mem.h>
#include <linux/of_address.h>

#include <media/eswin/common-def.h>


#define ES_DVP2AXI_VERNO_LEN 10

int es_dvp2axi_debug = 0;
module_param_named(debug, es_dvp2axi_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static char es_dvp2axi_version[ES_DVP2AXI_VERNO_LEN];
module_param_string(version, es_dvp2axi_version, ES_DVP2AXI_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

static DEFINE_MUTEX(es_dvp2axi_dev_mutex);
static LIST_HEAD(es_dvp2axi_device_list);

/* show the compact mode of each stream in stream index order,
 * 1 for compact, 0 for 16bit
 */
static ssize_t es_dvp2axi_show_compact_mode(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d %d %d %d\n",
		       dvp2axi_dev->stream[0].is_compact ? 1 : 0,
		       dvp2axi_dev->stream[1].is_compact ? 1 : 0,
		       dvp2axi_dev->stream[2].is_compact ? 1 : 0,
		       dvp2axi_dev->stream[3].is_compact ? 1 : 0);
	return ret;
}

static ssize_t es_dvp2axi_store_compact_mode(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i, index;
	char val[4];

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (buf[i] == ' ') {
				continue;
			} else if (buf[i] == '\0') {
				break;
			} else {
				val[index] = buf[i];
				index++;
				if (index == 4)
					break;
			}
		}

		for (i = 0; i < index; i++) {
			if (val[i] - '0' == 0)
				dvp2axi_dev->stream[i].is_compact = false;
			else
				dvp2axi_dev->stream[i].is_compact = true;
		}
	}

	return len;
}

static DEVICE_ATTR(compact_test, S_IWUSR | S_IRUSR, es_dvp2axi_show_compact_mode,
		   es_dvp2axi_store_compact_mode);

static ssize_t es_dvp2axi_show_line_int_num(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", dvp2axi_dev->wait_line_cache);
	return ret;
}

static ssize_t es_dvp2axi_store_line_int_num(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	struct sditf_priv *priv = dvp2axi_dev->sditf[0];
	int val = 0;
	int ret = 0;

	if (priv && priv->mode.rdbk_mode == ESISP_VICAP_ONLINE) {
		dev_info(
			dvp2axi_dev->dev,
			"current mode is on the fly, wake up mode wouldn't used\n");
		return len;
	}
	ret = kstrtoint(buf, 0, &val);
	if (!ret && val >= 0 && val <= 0x3fff)
		dvp2axi_dev->wait_line_cache = val;
	else
		dev_info(dvp2axi_dev->dev, "set line int num failed\n");
	return len;
}

static DEVICE_ATTR(wait_line, S_IWUSR | S_IRUSR, es_dvp2axi_show_line_int_num,
		   es_dvp2axi_store_line_int_num);

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

/* show the memory mode of each stream in stream index order,
 * 1 for high align, 0 for low align
 */
static ssize_t es_dvp2axi_show_memory_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(
		buf, PAGE_SIZE,
		"stream[0~3] %d %d %d %d, 0(low align) 1(high align) 2(compact)\n",
		dvp2axi_dev->stream[0].is_compact ?
			2 :
			(dvp2axi_dev->stream[0].is_high_align ? 1 : 0),
		dvp2axi_dev->stream[1].is_compact ?
			2 :
			(dvp2axi_dev->stream[1].is_high_align ? 1 : 0),
		dvp2axi_dev->stream[2].is_compact ?
			2 :
			(dvp2axi_dev->stream[2].is_high_align ? 1 : 0),
		dvp2axi_dev->stream[3].is_compact ?
			2 :
			(dvp2axi_dev->stream[3].is_high_align ? 1 : 0));
	return ret;
}

static ssize_t es_dvp2axi_store_memory_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i, index;
	char val[4];

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (buf[i] == ' ') {
				continue;
			} else if (buf[i] == '\0') {
				break;
			} else {
				val[index] = buf[i];
				index++;
				if (index == 4)
					break;
			}
		}

		for (i = 0; i < index; i++) {
			if (dvp2axi_dev->stream[i].is_compact) {
				dev_info(
					dvp2axi_dev->dev,
					"stream[%d] set memory align fail, is compact mode\n",
					i);
				continue;
			}
			if (val[i] - '0' == 0)
				dvp2axi_dev->stream[i].is_high_align = false;
			else
				dvp2axi_dev->stream[i].is_high_align = true;
		}
	}

	return len;
}

static DEVICE_ATTR(is_high_align, S_IWUSR | S_IRUSR, es_dvp2axi_show_memory_mode,
		   es_dvp2axi_store_memory_mode);

static ssize_t es_dvp2axi_show_scale_ch0_blc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(
		buf, PAGE_SIZE,
		"ch0 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
		dvp2axi_dev->scale_vdev[0].blc.pattern00,
		dvp2axi_dev->scale_vdev[0].blc.pattern01,
		dvp2axi_dev->scale_vdev[0].blc.pattern02,
		dvp2axi_dev->scale_vdev[0].blc.pattern03);
	return ret;
}

static ssize_t es_dvp2axi_store_scale_ch0_blc(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i = 0, index = 0;
	unsigned int val[4] = { 0 };
	unsigned int temp = 0;
	int ret = 0;
	int j = 0;
	char cha[2] = { 0 };

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (((buf[i] == ' ') || (buf[i] == '\n')) && j) {
				index++;
				j = 0;
				if (index == 4)
					break;
				continue;
			} else {
				if (buf[i] < '0' || buf[i] > '9')
					continue;
				cha[0] = buf[i];
				cha[1] = '\0';
				ret = kstrtoint(cha, 0, &temp);
				if (!ret) {
					if (j)
						val[index] *= 10;
					val[index] += temp;
					j++;
				}
			}
		}
		if (val[0] > 255 || val[1] > 255 || val[2] > 255 ||
		    val[3] > 255)
			return -EINVAL;
		dvp2axi_dev->scale_vdev[0].blc.pattern00 = val[0];
		dvp2axi_dev->scale_vdev[0].blc.pattern01 = val[1];
		dvp2axi_dev->scale_vdev[0].blc.pattern02 = val[2];
		dvp2axi_dev->scale_vdev[0].blc.pattern03 = val[3];
		dev_info(
			dvp2axi_dev->dev,
			"set ch0 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
			dvp2axi_dev->scale_vdev[0].blc.pattern00,
			dvp2axi_dev->scale_vdev[0].blc.pattern01,
			dvp2axi_dev->scale_vdev[0].blc.pattern02,
			dvp2axi_dev->scale_vdev[0].blc.pattern03);
	}

	return len;
}

static DEVICE_ATTR(scale_ch0_blc, S_IWUSR | S_IRUSR, es_dvp2axi_show_scale_ch0_blc,
		   es_dvp2axi_store_scale_ch0_blc);

static ssize_t es_dvp2axi_show_scale_ch1_blc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(
		buf, PAGE_SIZE,
		"ch1 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
		dvp2axi_dev->scale_vdev[1].blc.pattern00,
		dvp2axi_dev->scale_vdev[1].blc.pattern01,
		dvp2axi_dev->scale_vdev[1].blc.pattern02,
		dvp2axi_dev->scale_vdev[1].blc.pattern03);
	return ret;
}

static ssize_t es_dvp2axi_store_scale_ch1_blc(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i = 0, index = 0;
	unsigned int val[4] = { 0 };
	unsigned int temp = 0;
	int ret = 0;
	int j = 0;
	char cha[2] = { 0 };

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (((buf[i] == ' ') || (buf[i] == '\n')) && j) {
				index++;
				j = 0;
				if (index == 4)
					break;
				continue;
			} else {
				if (buf[i] < '0' || buf[i] > '9')
					continue;
				cha[0] = buf[i];
				cha[1] = '\0';
				ret = kstrtoint(cha, 0, &temp);
				if (!ret) {
					if (j)
						val[index] *= 10;
					val[index] += temp;
					j++;
				}
			}
		}
		if (val[0] > 255 || val[1] > 255 || val[2] > 255 ||
		    val[3] > 255)
			return -EINVAL;

		dvp2axi_dev->scale_vdev[1].blc.pattern00 = val[0];
		dvp2axi_dev->scale_vdev[1].blc.pattern01 = val[1];
		dvp2axi_dev->scale_vdev[1].blc.pattern02 = val[2];
		dvp2axi_dev->scale_vdev[1].blc.pattern03 = val[3];

		dev_info(
			dvp2axi_dev->dev,
			"set ch1 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
			dvp2axi_dev->scale_vdev[1].blc.pattern00,
			dvp2axi_dev->scale_vdev[1].blc.pattern01,
			dvp2axi_dev->scale_vdev[1].blc.pattern02,
			dvp2axi_dev->scale_vdev[1].blc.pattern03);
	}

	return len;
}

static DEVICE_ATTR(scale_ch1_blc, S_IWUSR | S_IRUSR, es_dvp2axi_show_scale_ch1_blc,
		   es_dvp2axi_store_scale_ch1_blc);

static ssize_t es_dvp2axi_show_scale_ch2_blc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(
		buf, PAGE_SIZE,
		"ch2 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
		dvp2axi_dev->scale_vdev[2].blc.pattern00,
		dvp2axi_dev->scale_vdev[2].blc.pattern01,
		dvp2axi_dev->scale_vdev[2].blc.pattern02,
		dvp2axi_dev->scale_vdev[2].blc.pattern03);
	return ret;
}

static ssize_t es_dvp2axi_store_scale_ch2_blc(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i = 0, index = 0;
	unsigned int val[4] = { 0 };
	unsigned int temp = 0;
	int ret = 0;
	int j = 0;
	char cha[2] = { 0 };

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (((buf[i] == ' ') || (buf[i] == '\n')) && j) {
				index++;
				j = 0;
				if (index == 4)
					break;
				continue;
			} else {
				if (buf[i] < '0' || buf[i] > '9')
					continue;
				cha[0] = buf[i];
				cha[1] = '\0';
				ret = kstrtoint(cha, 0, &temp);
				if (!ret) {
					if (j)
						val[index] *= 10;
					val[index] += temp;
					j++;
				}
			}
		}
		if (val[0] > 255 || val[1] > 255 || val[2] > 255 ||
		    val[3] > 255)
			return -EINVAL;

		dvp2axi_dev->scale_vdev[2].blc.pattern00 = val[0];
		dvp2axi_dev->scale_vdev[2].blc.pattern01 = val[1];
		dvp2axi_dev->scale_vdev[2].blc.pattern02 = val[2];
		dvp2axi_dev->scale_vdev[2].blc.pattern03 = val[3];

		dev_info(
			dvp2axi_dev->dev,
			"set ch2 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
			dvp2axi_dev->scale_vdev[2].blc.pattern00,
			dvp2axi_dev->scale_vdev[2].blc.pattern01,
			dvp2axi_dev->scale_vdev[2].blc.pattern02,
			dvp2axi_dev->scale_vdev[2].blc.pattern03);
	}

	return len;
}
static DEVICE_ATTR(scale_ch2_blc, S_IWUSR | S_IRUSR, es_dvp2axi_show_scale_ch2_blc,
		   es_dvp2axi_store_scale_ch2_blc);

static ssize_t es_dvp2axi_show_scale_ch3_blc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(
		buf, PAGE_SIZE,
		"ch3 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
		dvp2axi_dev->scale_vdev[3].blc.pattern00,
		dvp2axi_dev->scale_vdev[3].blc.pattern01,
		dvp2axi_dev->scale_vdev[3].blc.pattern02,
		dvp2axi_dev->scale_vdev[3].blc.pattern03);
	return ret;
}

static ssize_t es_dvp2axi_store_scale_ch3_blc(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i = 0, index = 0;
	unsigned int val[4] = { 0 };
	unsigned int temp = 0;
	int ret = 0;
	int j = 0;
	char cha[2] = { 0 };

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (((buf[i] == ' ') || (buf[i] == '\n')) && j) {
				index++;
				j = 0;
				if (index == 4)
					break;
				continue;
			} else {
				if (buf[i] < '0' || buf[i] > '9')
					continue;
				cha[0] = buf[i];
				cha[1] = '\0';
				ret = kstrtoint(cha, 0, &temp);
				if (!ret) {
					if (j)
						val[index] *= 10;
					val[index] += temp;
					j++;
				}
			}
		}
		if (val[0] > 255 || val[1] > 255 || val[2] > 255 ||
		    val[3] > 255)
			return -EINVAL;

		dvp2axi_dev->scale_vdev[3].blc.pattern00 = val[0];
		dvp2axi_dev->scale_vdev[3].blc.pattern01 = val[1];
		dvp2axi_dev->scale_vdev[3].blc.pattern02 = val[2];
		dvp2axi_dev->scale_vdev[3].blc.pattern03 = val[3];

		dev_info(
			dvp2axi_dev->dev,
			"set ch3 pattern00: %d, pattern01: %d, pattern02: %d, pattern03: %d\n",
			dvp2axi_dev->scale_vdev[3].blc.pattern00,
			dvp2axi_dev->scale_vdev[3].blc.pattern01,
			dvp2axi_dev->scale_vdev[3].blc.pattern02,
			dvp2axi_dev->scale_vdev[3].blc.pattern03);
	}

	return len;
}

static DEVICE_ATTR(scale_ch3_blc, S_IWUSR | S_IRUSR, es_dvp2axi_show_scale_ch3_blc,
		   es_dvp2axi_store_scale_ch3_blc);

static ssize_t es_dvp2axi_store_capture_fps(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int i = 0, index = 0;
	unsigned int val[4] = { 0 };
	unsigned int temp = 0;
	int ret = 0;
	int j = 0;
	char cha[2] = { 0 };

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (((buf[i] == ' ') || (buf[i] == '\n')) && j) {
				index++;
				j = 0;
				if (index == 4)
					break;
				continue;
			} else {
				if (buf[i] < '0' || buf[i] > '9')
					continue;
				cha[0] = buf[i];
				cha[1] = '\0';
				ret = kstrtoint(cha, 0, &temp);
				if (!ret) {
					if (j)
						val[index] *= 10;
					val[index] += temp;
					j++;
				}
			}
		}
		dev_info(dvp2axi_dev->dev,
			 "set fps id0: %d, id1: %d, id2: %d, id3: %d\n", val[0],
			 val[1], val[2], val[3]);
	}

	return len;
}
static DEVICE_ATTR(fps, 0200, NULL, es_dvp2axi_store_capture_fps);

static ssize_t es_dvp2axi_show_rdbk_debug(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", dvp2axi_dev->rdbk_debug);
	return ret;
}

static ssize_t es_dvp2axi_store_rdbk_debug(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct es_dvp2axi_device *dvp2axi_dev =
		(struct es_dvp2axi_device *)dev_get_drvdata(dev);
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret)
		dvp2axi_dev->rdbk_debug = val;
	else
		dev_info(dvp2axi_dev->dev, "set rdbk debug failed\n");
	return len;
}
static DEVICE_ATTR(rdbk_debug, 0200, es_dvp2axi_show_rdbk_debug,
		   es_dvp2axi_store_rdbk_debug);

static struct attribute *dev_attrs[] = {
	&dev_attr_compact_test.attr,
	&dev_attr_wait_line.attr,
	&dev_attr_is_use_dummybuf.attr,
	&dev_attr_is_high_align.attr,
	&dev_attr_scale_ch0_blc.attr,
	&dev_attr_scale_ch1_blc.attr,
	&dev_attr_scale_ch2_blc.attr,
	&dev_attr_scale_ch3_blc.attr,
	&dev_attr_fps.attr,
	&dev_attr_rdbk_debug.attr,
	NULL,
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

struct es_dvp2axi_match_data {
	int inf_id;
};

void es_dvp2axi_write_register(struct es_dvp2axi_device *dev, enum dvp2axi_reg_index index,
			  u32 val)
{
	return ;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct dvp2axi_reg *reg = &dev->hw_dev->dvp2axi_regs[index];
	int csi_offset = 0;

	if (dev->inf_id == ES_DVP2AXI_MIPI_LVDS &&
	    index >= DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0 &&
	    index <= DVP2AXI_REG_MIPI_ON_PAD) {
		csi_offset = dev->csi_host_idx * 0x100;
	}
	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index == DVP2AXI_REG_DVP_CTRL || reg->offset != 0x0) {
			write_dvp2axi_reg(base, reg->offset + csi_offset, val);
			v4l2_dbg(4, es_dvp2axi_debug, &dev->v4l2_dev,
				 "write reg[0x%x]:0x%x!!!\n",
				 reg->offset + csi_offset, val);
		} else {
			v4l2_dbg(
				1, es_dvp2axi_debug, &dev->v4l2_dev,
				"write reg[%d]:0x%x failed, maybe useless!!!\n",
				index, val);
		}
	}
}

void es_dvp2axi_write_register_or(struct es_dvp2axi_device *dev, enum dvp2axi_reg_index index,
			     u32 val)
{
	return ;
	unsigned int reg_val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct dvp2axi_reg *reg = &dev->hw_dev->dvp2axi_regs[index];
	int csi_offset = 0;

	if (dev->inf_id == ES_DVP2AXI_MIPI_LVDS &&
	    index >= DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0 &&
	    index <= DVP2AXI_REG_MIPI_ON_PAD) {
			csi_offset = dev->csi_host_idx * 0x100;
	}

	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index == DVP2AXI_REG_DVP_CTRL || reg->offset != 0x0) {
			reg_val = read_dvp2axi_reg(base, reg->offset + csi_offset);
			reg_val |= val;
			write_dvp2axi_reg(base, reg->offset + csi_offset, reg_val);
			v4l2_dbg(4, es_dvp2axi_debug, &dev->v4l2_dev,
				 "write or reg[0x%x]:0x%x!!!\n",
				 reg->offset + csi_offset, val);
		} else {
			v4l2_dbg(
				1, es_dvp2axi_debug, &dev->v4l2_dev,
				"write reg[%d]:0x%x with OR failed, maybe useless!!!\n",
				index, val);
		}
	}
}

void es_dvp2axi_write_register_and(struct es_dvp2axi_device *dev,
			      enum dvp2axi_reg_index index, u32 val)
{
	return ;
	unsigned int reg_val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct dvp2axi_reg *reg = &dev->hw_dev->dvp2axi_regs[index];
	int csi_offset = 0;

	if (dev->inf_id == ES_DVP2AXI_MIPI_LVDS &&
	    index >= DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0 &&
	    index <= DVP2AXI_REG_MIPI_ON_PAD) {
		csi_offset = dev->csi_host_idx * 0x100;
	}

	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index == DVP2AXI_REG_DVP_CTRL || reg->offset != 0x0) {
			reg_val = read_dvp2axi_reg(base, reg->offset + csi_offset);
			reg_val &= val;
			write_dvp2axi_reg(base, reg->offset + csi_offset, reg_val);
			v4l2_dbg(4, es_dvp2axi_debug, &dev->v4l2_dev,
				 "write and reg[0x%x]:0x%x!!!\n",
				 reg->offset + csi_offset, val);
		} else {
			v4l2_dbg(
				1, es_dvp2axi_debug, &dev->v4l2_dev,
				"write reg[%d]:0x%x with OR failed, maybe useless!!!\n",
				index, val);
		}
	}
}

unsigned int es_dvp2axi_read_register(struct es_dvp2axi_device *dev,
				 enum dvp2axi_reg_index index)
{
	unsigned int val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct dvp2axi_reg *reg = &dev->hw_dev->dvp2axi_regs[index];
	int csi_offset = 0;

	if (dev->inf_id == ES_DVP2AXI_MIPI_LVDS &&
	    index >= DVP2AXI_REG_MIPI_LVDS_ID0_CTRL0 &&
	    index <= DVP2AXI_REG_MIPI_ON_PAD) {
		csi_offset = dev->csi_host_idx * 0x100;
	}

	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index == DVP2AXI_REG_DVP_CTRL || reg->offset != 0x0)
			val = read_dvp2axi_reg(base, reg->offset + csi_offset);
		else
			v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
				 "read reg[%d] failed, maybe useless!!!\n",
				 index);
	}

	return val;
}

void es_dvp2axi_write_grf_reg(struct es_dvp2axi_device *dev, enum dvp2axi_reg_index index,
			 u32 val)
{
	return ;
	struct es_dvp2axi_hw *dvp2axi_hw = dev->hw_dev;
	const struct dvp2axi_reg *reg = &dvp2axi_hw->dvp2axi_regs[index];

	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index > DVP2AXI_REG_DVP_CTRL) {
			if (!IS_ERR(dvp2axi_hw->grf))
				regmap_write(dvp2axi_hw->grf, reg->offset, val);
		} else {
			v4l2_dbg(
				1, es_dvp2axi_debug, &dev->v4l2_dev,
				"write reg[%d]:0x%x failed, maybe useless!!!\n",
				index, val);
		}
	}
}

u32 es_dvp2axi_read_grf_reg(struct es_dvp2axi_device *dev, enum dvp2axi_reg_index index)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev->hw_dev;
	const struct dvp2axi_reg *reg = &dvp2axi_hw->dvp2axi_regs[index];
	u32 val = 0xffff;

	if (index < DVP2AXI_REG_INDEX_MAX) {
		if (index > DVP2AXI_REG_DVP_CTRL) {
			if (!IS_ERR(dvp2axi_hw->grf))
				regmap_read(dvp2axi_hw->grf, reg->offset, &val);
		} else {
			v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
				 "read reg[%d] failed, maybe useless!!!\n",
				 index);
		}
	}

	return val;
}

void es_dvp2axi_enable_dvp_clk_dual_edge(struct es_dvp2axi_device *dev, bool on)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev->hw_dev;
	u32 val = 0x0;

	if (!IS_ERR(dvp2axi_hw->grf)) {
		if (dev->chip_id == CHIP_EIC770X_DVP2AXI) {
			if (on)
				val = EIC770X_DVP2AXI_PCLK_DUAL_EDGE;
			else
				val = EIC770X_DVP2AXI_PCLK_SINGLE_EDGE;
			es_dvp2axi_write_grf_reg(dev, DVP2AXI_REG_GRF_DVP2AXIIO_CON, val);
		}
	}

	v4l2_info(&dev->v4l2_dev, "set dual edge mode(%s,0x%x)!!!\n",
		  on ? "on" : "off", val);
}

void es_dvp2axi_config_dvp_clk_sampling_edge(struct es_dvp2axi_device *dev,
					enum es_dvp2axi_clk_edge edge)
{
	struct es_dvp2axi_hw *dvp2axi_hw = dev->hw_dev;
	u32 val = 0x0;

	if (!IS_ERR(dvp2axi_hw->grf)) {
		if (edge == ES_DVP2AXI_CLK_RISING)
			val = EIC770X_DVP2AXI_PCLK_SAMPLING_EDGE_RISING;
		else
			val = EIC770X_DVP2AXI_PCLK_SAMPLING_EDGE_FALLING;
		es_dvp2axi_write_grf_reg(dev, DVP2AXI_REG_GRF_DVP2AXIIO_CON, val);
	}
}

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

static void es_dvp2axi_set_sensor_streamon_in_sync_mode(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct es_dvp2axi_hw *hw = dvp2axi_dev->hw_dev;
	struct es_dvp2axi_device *dev = NULL;
	int i = 0, j = 0;
	int on = 1;
	int ret = 0;
	bool is_streaming = false;
	struct es_dvp2axi_multi_sync_config *sync_config;

	if (!dvp2axi_dev->sync_cfg.type)
		return;

	mutex_lock(&hw->dev_lock);
	sync_config = &hw->sync_config[dvp2axi_dev->sync_cfg.group];
	sync_config->streaming_cnt++;
	if (sync_config->streaming_cnt < sync_config->dev_cnt) {
		mutex_unlock(&hw->dev_lock);
		return;
	}

	if (sync_config->mode == ES_DVP2AXI_MASTER_MASTER ||
	    sync_config->mode == ES_DVP2AXI_MASTER_SLAVE) {
		for (i = 0; i < sync_config->slave.count; i++) {
			dev = sync_config->slave.dvp2axi_dev[i];
			is_streaming = sync_config->slave.is_streaming[i];
			if (!is_streaming) {
				if (dev->sditf_cnt == 1) {
					ret = v4l2_subdev_call(
						dev->terminal_sensor.sd, core,
						ioctl,
						ESMODULE_SET_QUICK_STREAM, &on);
					if (ret)
						dev_info(
							dev->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				} else {
					for (j = 0; j < dev->sditf_cnt; j++)
						ret |= v4l2_subdev_call(
							dev->sditf[j]->sensor_sd,
							core, ioctl,
							ESMODULE_SET_QUICK_STREAM,
							&on);
					if (ret)
						dev_info(
							dev->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				}
				sync_config->slave.is_streaming[i] = true;
			}
			v4l2_dbg(3, es_dvp2axi_debug, &dev->v4l2_dev,
				 "quick stream in sync mode, slave_dev[%d]\n",
				 i);
		}
		for (i = 0; i < sync_config->ext_master.count; i++) {
			dev = sync_config->ext_master.dvp2axi_dev[i];
			is_streaming = sync_config->ext_master.is_streaming[i];
			if (!is_streaming) {
				if (dev->sditf_cnt == 1) {
					ret = v4l2_subdev_call(
						dev->terminal_sensor.sd, core,
						ioctl,
						ESMODULE_SET_QUICK_STREAM, &on);
					if (ret)
						dev_info(
							dev->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				} else {
					for (j = 0; j < dev->sditf_cnt; j++)
						ret |= v4l2_subdev_call(
							dev->sditf[j]->sensor_sd,
							core, ioctl,
							ESMODULE_SET_QUICK_STREAM,
							&on);
					if (ret)
						dev_info(
							dev->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				}
				sync_config->ext_master.is_streaming[i] = true;
			}
			v4l2_dbg(
				3, es_dvp2axi_debug, &dev->v4l2_dev,
				"quick stream in sync mode, ext_master_dev[%d]\n",
				i);
		}
		for (i = 0; i < sync_config->int_master.count; i++) {
			dev = sync_config->int_master.dvp2axi_dev[i];
			is_streaming = sync_config->int_master.is_streaming[i];
			if (!is_streaming) {
				if (dev->sditf_cnt == 1) {
					ret = v4l2_subdev_call(
						dev->terminal_sensor.sd, core,
						ioctl,
						ESMODULE_SET_QUICK_STREAM, &on);
					if (ret)
						dev_info(
							hw->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				} else {
					for (j = 0; j < dev->sditf_cnt; j++)
						ret |= v4l2_subdev_call(
							dev->sditf[j]->sensor_sd,
							core, ioctl,
							ESMODULE_SET_QUICK_STREAM,
							&on);
					if (ret)
						dev_info(
							dev->dev,
							"set ESMODULE_SET_QUICK_STREAM failed\n");
				}
				sync_config->int_master.is_streaming[i] = true;
			}
			v4l2_dbg(
				3, es_dvp2axi_debug, &dev->v4l2_dev,
				"quick stream in sync mode, int_master_dev[%d]\n",
				i);
		}
	}
	mutex_unlock(&hw->dev_lock);
}

#if 0
static void es_dvp2axi_sensor_streaming_cb(void *data)
{
	struct v4l2_subdev *subdevs = (struct v4l2_subdev *)data;

	v4l2_subdev_call(subdevs, video, s_stream, 1);
}
#endif

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
	    dvp2axi_dev->hdr.hdr_mode == HDR_COMPR) {
		if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
		    (!on && atomic_dec_return(&p->stream_cnt) > 0))
			return 0;

		if (on) {
			dvp2axi_dev->irq_stats.csi_overflow_cnt = 0;
			dvp2axi_dev->irq_stats.csi_bwidth_lack_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_bus_err_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_line_err_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_overflow_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_pix_err_cnt = 0;
			dvp2axi_dev->irq_stats.all_err_cnt = 0;
			dvp2axi_dev->irq_stats.csi_size_err_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_size_err_cnt = 0;
			dvp2axi_dev->irq_stats.dvp_bwidth_lack_cnt = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[0] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[1] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[2] = 0;
			dvp2axi_dev->irq_stats.frm_end_cnt[3] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[0] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[1] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[2] = 0;
			dvp2axi_dev->irq_stats.not_active_buf_cnt[3] = 0;
			dvp2axi_dev->irq_stats.trig_simult_cnt[0] = 0;
			dvp2axi_dev->irq_stats.trig_simult_cnt[1] = 0;
			dvp2axi_dev->irq_stats.trig_simult_cnt[2] = 0;
			dvp2axi_dev->irq_stats.trig_simult_cnt[3] = 0;
			dvp2axi_dev->reset_watchdog_timer.is_triggered = false;
			dvp2axi_dev->reset_watchdog_timer.is_running = false;
			dvp2axi_dev->err_state_work.last_timestamp = 0;
			dvp2axi_dev->is_toisp_reset = false;
			for (i = 0; i < dvp2axi_dev->num_channels; i++)
				dvp2axi_dev->reset_watchdog_timer
					.last_buf_wakeup_cnt[i] = 0;
			dvp2axi_dev->reset_watchdog_timer.run_cnt = 0;
		}

		/* phy -> sensor */
		for (i = 0; i < p->num_subdevs; i++) {
			if (p->subdevs[i] == dvp2axi_dev->terminal_sensor.sd &&
			    on && dvp2axi_dev->is_thunderboot) {
				// dvp2axi_dev->tb_client.data = p->subdevs[i];
				// dvp2axi_dev->tb_client.cb = es_dvp2axi_sensor_streaming_cb;
				// es_tb_client_register_cb(&dvp2axi_dev->tb_client);
			} else {
				ret = v4l2_subdev_call(p->subdevs[i], video,
						       s_stream, on);
			}
			if (on && ret < 0 && ret != -ENOIOCTLCMD &&
			    ret != -ENODEV)
				goto err_stream_off;
		}

		if (dvp2axi_dev->sditf_cnt > 1) {
			for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
				ret = v4l2_subdev_call(
					dvp2axi_dev->sditf[i]->sensor_sd, video,
					s_stream, on);
				if (on && ret < 0 && ret != -ENOIOCTLCMD &&
				    ret != -ENODEV)
					goto err_stream_off;
			}
		}

		if (on)
			es_dvp2axi_set_sensor_streamon_in_sync_mode(dvp2axi_dev);
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
				dvp2axi_dev->irq_stats.csi_overflow_cnt = 0;
				dvp2axi_dev->irq_stats.csi_bwidth_lack_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_bus_err_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_line_err_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_overflow_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_pix_err_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_bwidth_lack_cnt = 0;
				dvp2axi_dev->irq_stats.all_err_cnt = 0;
				dvp2axi_dev->irq_stats.csi_size_err_cnt = 0;
				dvp2axi_dev->irq_stats.dvp_size_err_cnt = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[0] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[1] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[2] = 0;
				dvp2axi_dev->irq_stats.frm_end_cnt[3] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[0] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[1] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[2] = 0;
				dvp2axi_dev->irq_stats.not_active_buf_cnt[3] = 0;
				dvp2axi_dev->irq_stats.trig_simult_cnt[0] = 0;
				dvp2axi_dev->irq_stats.trig_simult_cnt[1] = 0;
				dvp2axi_dev->irq_stats.trig_simult_cnt[2] = 0;
				dvp2axi_dev->irq_stats.trig_simult_cnt[3] = 0;
				dvp2axi_dev->is_start_hdr = true;
				dvp2axi_dev->reset_watchdog_timer.is_triggered =
					false;
				dvp2axi_dev->reset_watchdog_timer.is_running =
					false;
				dvp2axi_dev->is_toisp_reset = false;
				for (i = 0; i < dvp2axi_dev->num_channels; i++)
					dvp2axi_dev->reset_watchdog_timer
						.last_buf_wakeup_cnt[i] = 0;
				dvp2axi_dev->reset_watchdog_timer.run_cnt = 0;
			}

			/* phy -> sensor */
			for (i = 0; i < p->num_subdevs; i++) {
				if (p->subdevs[i] ==
					    dvp2axi_dev->terminal_sensor.sd &&
				    on && dvp2axi_dev->is_thunderboot) {
					// dvp2axi_dev->tb_client.data = p->subdevs[i];
					// dvp2axi_dev->tb_client.cb = es_dvp2axi_sensor_streaming_cb;
					// es_tb_client_register_cb(&dvp2axi_dev->tb_client);
				} else {
					ret = v4l2_subdev_call(p->subdevs[i],
							       video, s_stream,
							       on);
				}
				if (on && ret < 0 && ret != -ENOIOCTLCMD &&
				    ret != -ENODEV)
					goto err_stream_off;
			}
			if (dvp2axi_dev->sditf_cnt > 1) {
				for (i = 0; i < dvp2axi_dev->sditf_cnt; i++) {
					ret = v4l2_subdev_call(
						dvp2axi_dev->sditf[i]->sensor_sd,
						video, s_stream, on);
					if (on && ret < 0 &&
					    ret != -ENOIOCTLCMD &&
					    ret != -ENODEV)
						goto err_stream_off;
				}
			}

			if (on)
				es_dvp2axi_set_sensor_streamon_in_sync_mode(dvp2axi_dev);
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
		linked_sensor.sd = &dev->lvds_subdev.sd;
		dev->lvds_subdev.sensor_self.sd = &dev->lvds_subdev.sd;
		dev->lvds_subdev.sensor_self.lanes = sensor->lanes;
		memcpy(&dev->lvds_subdev.sensor_self.mbus, &sensor->mbus,
		       sizeof(struct v4l2_mbus_config));
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
			// pr_info("%s:%d yfx is linked \n", __func__, __LINE__);
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

	stream_num = ESDVP2AXI_MULTI_STREAM;

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

static int subdev_asyn_register_itf(struct es_dvp2axi_device *dev)
{
	struct sditf_priv *sditf = NULL;
	int ret = 0;

	if (IS_ENABLED(CONFIG_NO_GKI)) {
		ret = es_dvp2axi_update_sensor_info(&dev->stream[0]);
		if (ret) {
			v4l2_err(
				&dev->v4l2_dev,
				"There is not terminal subdev, not synchronized with ISP\n");
			return 0;
		}
	}
	sditf = dev->sditf[0];
	if (sditf && (!sditf->is_combine_mode) && (!dev->is_notifier_isp)) {
		ret = v4l2_async_register_subdev_sensor(&sditf->sd);
		dev->is_notifier_isp = true;
	}

	return ret;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct es_dvp2axi_device *dev;
	struct es_dvp2axi_sensor_info *sensor;
	struct v4l2_subdev *sd;
	struct v4l2_device *v4l2_dev = NULL;
	int ret, index;

	dev = container_of(notifier, struct es_dvp2axi_device, notifier);

	v4l2_dev = &dev->v4l2_dev;
	
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
		
		if (sensor->mbus.type == V4L2_MBUS_CCP2) {
			ret = es_dvp2axi_register_lvds_subdev(dev);
			if (ret < 0) {
				v4l2_err(
					&dev->v4l2_dev,
					"Err: register lvds subdev failed!!!\n");
				goto notifier_end;
			}
			break;
		}

		if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		    sensor->mbus.type == V4L2_MBUS_BT656) {
			ret = es_dvp2axi_register_dvp_sof_subdev(dev);
			if (ret < 0) {
				v4l2_err(
					&dev->v4l2_dev,
					"Err: register dvp sof subdev failed!!!\n");
				goto notifier_end;
			}
			break;
		}
	}

	ret = es_dvp2axi_create_links(dev);
	if (ret < 0)
		goto unregister_lvds;
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unregister_lvds;
	ret = _set_pipeline_default_fmt(dev);
	if (ret < 0)
		goto unregister_lvds;
	if (!completion_done(&dev->cmpl_ntf))
		complete(&dev->cmpl_ntf);
	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");
	return ret;

unregister_lvds:
	es_dvp2axi_unregister_lvds_subdev(dev);
	es_dvp2axi_unregister_dvp_sof_subdev(dev);
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

	// pr_info("%s:%d yfx! subdev %p, asd %p \n", __func__, __LINE__,subdev, asd);

	if (dvp2axi_dev->num_sensors == ARRAY_SIZE(dvp2axi_dev->sensors)) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			 "%s: the num of subdev is beyond %d\n", __func__,
			 dvp2axi_dev->num_sensors);
		return -EBUSY;
	}

	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].lanes = s_asd->lanes;
	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].mbus = s_asd->mbus;
	dvp2axi_dev->sensors[dvp2axi_dev->num_sensors].sd = subdev;
	++dvp2axi_dev->num_sensors;


	v4l2_err(subdev, "Async registered subdev\n");

	return 0;
}

// static int es_dvp2axi_fwnode_parse(struct device *dev,
// 			      struct v4l2_fwnode_endpoint *vep,
// 			      struct v4l2_async_connection *asd)
// {
// 	struct es_dvp2axi_async_subdev *es_asd =
// 			container_of(asd, struct es_dvp2axi_async_subdev, asd);

// 	if (vep->bus_type != V4L2_MBUS_BT656 &&
// 	    vep->bus_type != V4L2_MBUS_PARALLEL &&
// 	    vep->bus_type != V4L2_MBUS_CSI2_DPHY &&
// 	    vep->bus_type != V4L2_MBUS_CSI2_CPHY &&
// 	    vep->bus_type != V4L2_MBUS_CCP2)
// 		return 0;

// 	es_asd->mbus.type = vep->bus_type;

// 	return 0;
// }

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
};

static int es_dvp2axi_fwnode_parse(struct es_dvp2axi_device *sditf)
{
	struct device *dev = sditf->dev;

	int ret = 0, i = 0;

	for (i = 0; i < 2; i++) {
		struct v4l2_fwnode_endpoint vep = {
			// .bus_type = V4L2_MBUS_CSI2_DPHY
			.bus_type = V4L2_MBUS_UNKNOWN
		};
		// pr_info("%s:%d yfx! i %d \n", __func__, __LINE__, i);
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
		// pr_info("%s:%d yfx!!!\n", __func__, __LINE__);
		if (ret)
			goto err_parse;
		
		s_asd = v4l2_async_nf_add_fwnode_remote(
			&sditf->notifier, ep, struct es_dvp2axi_async_subdev);
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
	sditf->notifier.ops = &subdev_notifier_ops;
	ret = v4l2_async_nf_register(&sditf->notifier);
	if (ret)
		dev_err(dev, "failed to register async notifier : %d\n", ret);
	return ret;
}

static int dvp2axi_subdev_notifier(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct v4l2_async_notifier *ntf = &dvp2axi_dev->notifier;
	int ret;

	v4l2_async_nf_init(ntf, &dvp2axi_dev->v4l2_dev);
	ret = es_dvp2axi_fwnode_parse(dvp2axi_dev);
	if (ret < 0)
		return ret;
	return 0;
}

static int notifier_isp_thread(void *data)
{
	struct es_dvp2axi_device *dev = data;
	int ret = 0;

	ret = wait_for_completion_timeout(&dev->cmpl_ntf,
					  msecs_to_jiffies(5000));
	if (ret) {
		mutex_lock(&es_dvp2axi_dev_mutex);
		subdev_asyn_register_itf(dev);
		mutex_unlock(&es_dvp2axi_dev_mutex);
	}
	return 0;
}

/***************************** platform deive *******************************/

static int es_dvp2axi_register_platform_subdevs(struct es_dvp2axi_device *dvp2axi_dev)
{
	int stream_num = 0, ret;

	//stream_num = ES_DVP2AXI_SINGLE_STREAM;
	stream_num = ESDVP2AXI_MAX_STREAM_MIPI;
	ret = es_dvp2axi_register_stream_vdevs(dvp2axi_dev, stream_num, true);
	//ret = es_dvp2axi_register_stream_vdevs(dvp2axi_dev, stream_num, false);
	if (ret < 0) {
		dev_err(dvp2axi_dev->dev, "dvp2axi register stream[%d] failed!\n",
			stream_num);
		return -EINVAL;
	}
	dvp2axi_dev->is_support_tools = false;

	init_completion(&dvp2axi_dev->cmpl_ntf);
	kthread_run(notifier_isp_thread, dvp2axi_dev, "notifier isp");
	ret = dvp2axi_subdev_notifier(dvp2axi_dev);
	if (ret < 0) {
		v4l2_err(&dvp2axi_dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_stream_vdev;
	}
	return 0;
err_unreg_stream_vdev:
	es_dvp2axi_unregister_stream_vdevs(dvp2axi_dev, stream_num);
	es_dvp2axi_unregister_scale_vdevs(dvp2axi_dev, ES_DVP2AXI_MAX_SCALE_CH);

	return ret;
}

static irqreturn_t es_dvp2axi_irq_handler(int irq, struct es_dvp2axi_device *dvp2axi_dev)
{
	if (dvp2axi_dev->workmode == ES_DVP2AXI_WORKMODE_PINGPONG) {
		es_dvp2axi_irq_pingpong_v1(dvp2axi_dev);
	} else {
		es_dvp2axi_irq_oneframe(dvp2axi_dev);
	}
	return IRQ_HANDLED;
}

#if 0
static irqreturn_t es_dvp2axi_irq_lite_handler(int irq, struct es_dvp2axi_device *dvp2axi_dev)
{
	es_dvp2axi_irq_lite_lvds(dvp2axi_dev);

	return IRQ_HANDLED;
}
#endif

#if 0
static void es_dvp2axi_attach_dphy_hw(struct es_dvp2axi_device *dvp2axi_dev)
{
	struct platform_device *plat_dev;
	struct device *dev = dvp2axi_dev->dev;
	struct device_node *np;
	struct csi2_dphy_hw *dphy_hw;

	np = of_parse_phandle(dev->of_node, "eic770x,dphy_hw", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(dev, "failed to get dphy hw node\n");
		return;
	}

	plat_dev = of_find_device_by_node(np);
	of_node_put(np);
	if (!plat_dev) {
		dev_err(dev, "failed to get dphy hw from node\n");
		return;
	}

	dphy_hw = platform_get_drvdata(plat_dev);
	if (!dphy_hw) {
		dev_err(dev, "failed attach dphy hw\n");
		return;
	}
	dvp2axi_dev->dphy_hw = dphy_hw;
}
#endif

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

static void es_dvp2axi_init_reset_monitor(struct es_dvp2axi_device *dev)
{
	struct es_dvp2axi_timer *timer = &dev->reset_watchdog_timer;

	timer->monitor_mode = ES_DVP2AXI_MONITOR_MODE_IDLE;
	timer->err_time_interval = 0xffffffff;
	timer->frm_num_of_monitor_cycle = 0xffffffff;
	timer->triggered_frame_num = 0xffffffff;
	timer->csi2_err_ref_cnt = 0xffffffff;
	timer->is_running = false;
	timer->is_triggered = false;
	timer->is_buf_stop_update = false;
	timer->csi2_err_cnt_even = 0;
	timer->csi2_err_cnt_odd = 0;
	timer->csi2_err_fs_fe_cnt = 0;
	timer->csi2_err_fs_fe_detect_cnt = 0;
	timer->csi2_err_triggered_cnt = 0;
	timer->csi2_first_err_timestamp = 0;

	timer_setup(&timer->timer, es_dvp2axi_reset_watchdog_timer_handler, 0);

	INIT_WORK(&dev->reset_work.work, es_dvp2axi_reset_work);
}

void es_dvp2axi_set_sensor_stream(struct work_struct *work)
{
	struct es_dvp2axi_sensor_work *sensor_work =
		container_of(work, struct es_dvp2axi_sensor_work, work);
	struct es_dvp2axi_device *dvp2axi_dev =
		container_of(sensor_work, struct es_dvp2axi_device, sensor_work);

	v4l2_subdev_call(dvp2axi_dev->terminal_sensor.sd, core, ioctl,
			 ESMODULE_SET_QUICK_STREAM, &sensor_work->on);
}

static void es_dvp2axi_deal_err_intr(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct es_dvp2axi_device *dvp2axi_dev =
		container_of(dwork, struct es_dvp2axi_device, work_deal_err);

	dvp2axi_dev->intr_mask |= CSI_BANDWIDTH_LACK_V1;
	es_dvp2axi_write_register_or(dvp2axi_dev, DVP2AXI_REG_MIPI_LVDS_INTEN,
				CSI_BANDWIDTH_LACK_V1);
}

//2025-3-14 use es_vi_dev->media_dev to replace es_dvp2axi_media_dev
int es_dvp2axi_plat_init(struct es_dvp2axi_device *dvp2axi_dev, struct device_node *node,
		    int inf_id)
{
	struct device *dev = dvp2axi_dev->dev;
	struct v4l2_device *v4l2_dev;
	struct eswin_vi_device* es_vi_dev;
	int ret;

	dvp2axi_dev->hdr.hdr_mode = NO_HDR;
	dvp2axi_dev->inf_id = inf_id;

	es_vi_dev =  dev_get_drvdata(dvp2axi_dev->dev->parent);

	mutex_init(&dvp2axi_dev->stream_lock);
	mutex_init(&dvp2axi_dev->scale_lock);
	mutex_init(&dvp2axi_dev->tools_lock);
	spin_lock_init(&dvp2axi_dev->hdr_lock);
	spin_lock_init(&dvp2axi_dev->buffree_lock);
	spin_lock_init(&dvp2axi_dev->reset_watchdog_timer.timer_lock);
	spin_lock_init(&dvp2axi_dev->reset_watchdog_timer.csi2_err_lock);
	atomic_set(&dvp2axi_dev->pipe.power_cnt, 0);
	atomic_set(&dvp2axi_dev->pipe.stream_cnt, 0);
	atomic_set(&dvp2axi_dev->power_cnt, 0);
	atomic_set(&dvp2axi_dev->streamoff_cnt, 0);
	dvp2axi_dev->is_start_hdr = false;
	dvp2axi_dev->pipe.open = es_dvp2axi_pipeline_open;
	dvp2axi_dev->pipe.close = es_dvp2axi_pipeline_close;
	dvp2axi_dev->pipe.set_stream = es_dvp2axi_pipeline_set_stream;
	dvp2axi_dev->isr_hdl = es_dvp2axi_irq_handler;
	dvp2axi_dev->id_use_cnt = 0;
	memset(&dvp2axi_dev->sync_cfg, 0, sizeof(dvp2axi_dev->sync_cfg));
	dvp2axi_dev->sditf_cnt = 0;
	dvp2axi_dev->is_notifier_isp = false;
	dvp2axi_dev->sensor_linetime = 0;
	dvp2axi_dev->early_line = 0;
	dvp2axi_dev->is_thunderboot = false;
	dvp2axi_dev->rdbk_debug = 0;

	dvp2axi_dev->resume_mode = 0;
	memset(&dvp2axi_dev->channels[0].capture_info, 0,
	       sizeof(dvp2axi_dev->channels[0].capture_info));

	INIT_WORK(&dvp2axi_dev->err_state_work.work, es_dvp2axi_err_print_work);
	INIT_WORK(&dvp2axi_dev->sensor_work.work, es_dvp2axi_set_sensor_stream);
	INIT_DELAYED_WORK(&dvp2axi_dev->work_deal_err, es_dvp2axi_deal_err_intr);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID0);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID1);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID2);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID3);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID4);
	es_dvp2axi_stream_init(dvp2axi_dev, ESDVP2AXI_STREAM_MIPI_ID5);

	
	dvp2axi_dev->workmode = ES_DVP2AXI_WORKMODE_PINGPONG;

	dvp2axi_dev->is_use_dummybuf = false;
	// strlcpy(dvp2axi_dev->media_dev.model, dev_name(dev),
		// sizeof(dvp2axi_dev->media_dev.model));
	dvp2axi_dev->csi_host_idx = of_alias_get_id(node, "es_dvp2axi_mipi_lvds");
	if (dvp2axi_dev->csi_host_idx < 0 || dvp2axi_dev->csi_host_idx > 5)
		dvp2axi_dev->csi_host_idx = 0;
	if (dvp2axi_dev->hw_dev->is_eic770xs2) {
		if (dvp2axi_dev->csi_host_idx == 0)
			dvp2axi_dev->csi_host_idx = 2;
		else if (dvp2axi_dev->csi_host_idx == 2)
			dvp2axi_dev->csi_host_idx = 4;
		else if (dvp2axi_dev->csi_host_idx == 3)
			dvp2axi_dev->csi_host_idx = 5;
		v4l2_info(&dvp2axi_dev->v4l2_dev, "eic770xs2 attach to mipi%d\n",
			  dvp2axi_dev->csi_host_idx);
	}
	dvp2axi_dev->csi_host_idx_def = dvp2axi_dev->csi_host_idx;
	
	dvp2axi_dev->media_dev = es_vi_dev->media_dev;
	v4l2_dev = &dvp2axi_dev->v4l2_dev;
	v4l2_dev->mdev = dvp2axi_dev->media_dev;
	strlcpy(v4l2_dev->name, dev_name(dev), sizeof(v4l2_dev->name));
	ret = v4l2_device_register(dvp2axi_dev->dev, &dvp2axi_dev->v4l2_dev);
	if (ret < 0)
		return ret;


	// media_device_init(&dvp2axi_dev->media_dev);
	// ret = media_device_register(&dvp2axi_dev->media_dev);
	// if (ret < 0) {
	// 	v4l2_err(v4l2_dev, "Failed to register media device: %d\n",
	// 		 ret);
	// 	goto err_unreg_v4l2_dev;
	// }
	/* create & register platefom subdev (from of_node) */
	ret = es_dvp2axi_register_platform_subdevs(dvp2axi_dev);
	// if (ret < 0)
	// 	goto err_unreg_media_dev;

	mutex_lock(&es_dvp2axi_dev_mutex);
	list_add_tail(&dvp2axi_dev->list, &es_dvp2axi_device_list);
	mutex_unlock(&es_dvp2axi_dev_mutex);
	return 0;
}

int es_dvp2axi_plat_uninit(struct es_dvp2axi_device *dvp2axi_dev)
{
	int stream_num = 0;

	if (dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)
		es_dvp2axi_unregister_lvds_subdev(dvp2axi_dev);

	if (dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_BT656 ||
	    dvp2axi_dev->active_sensor->mbus.type == V4L2_MBUS_PARALLEL)
		es_dvp2axi_unregister_dvp_sof_subdev(dvp2axi_dev);

	// media_device_unregister(&dvp2axi_dev->media_dev);
	v4l2_device_unregister(&dvp2axi_dev->v4l2_dev);

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

static void es_dvp2axi_parse_dts(struct es_dvp2axi_device *dvp2axi_dev)
{
	int ret = 0;
	struct device_node *node = dvp2axi_dev->dev->of_node;

	ret = of_property_read_u32(node, OF_DVP2AXI_WAIT_LINE, &dvp2axi_dev->wait_line);
	if (ret != 0)
		dvp2axi_dev->wait_line = 0;
	dev_info(dvp2axi_dev->dev, "es_dvp2axi wait line %d\n", dvp2axi_dev->wait_line);
}

static int es_dvp2axi_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct es_dvp2axi_device *dvp2axi_dev;
	const struct es_dvp2axi_match_data *data;
	int ret;

	sprintf(es_dvp2axi_version, "v%02x.%02x.%02x", ES_DVP2AXI_DRIVER_VERSION >> 16,
		(ES_DVP2AXI_DRIVER_VERSION & 0xff00) >> 8,
		ES_DVP2AXI_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "es_dvp2axi driver version: %s\n", es_dvp2axi_version);

	match = of_match_node(es_dvp2axi_plat_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
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

	if (es_dvp2axi_proc_init(dvp2axi_dev))
		dev_warn(dev, "dev:%s create proc failed\n", dev_name(dev));
	es_dvp2axi_init_reset_monitor(dvp2axi_dev);
	pm_runtime_enable(&pdev->dev);
	pr_info("%s succsess\n", __func__);
	return 0;
}

static int es_dvp2axi_plat_remove(struct platform_device *pdev)
{
	struct es_dvp2axi_device *dvp2axi_dev = platform_get_drvdata(pdev);

	es_dvp2axi_plat_uninit(dvp2axi_dev);
	es_dvp2axi_detach_hw(dvp2axi_dev);
	es_dvp2axi_proc_cleanup(dvp2axi_dev);
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_grp);
	del_timer_sync(&dvp2axi_dev->reset_watchdog_timer.timer);

	return 0;
}

static int __maybe_unused es_dvp2axi_sleep_suspend(struct device *dev)
{
	struct es_dvp2axi_device *dvp2axi_dev = dev_get_drvdata(dev);

	es_dvp2axi_stream_suspend(dvp2axi_dev, ES_DVP2AXI_RESUME_DVP2AXI);
	return 0;
}

static int __maybe_unused es_dvp2axi_sleep_resume(struct device *dev)
{
	struct es_dvp2axi_device *dvp2axi_dev = dev_get_drvdata(dev);

	es_dvp2axi_stream_resume(dvp2axi_dev, ES_DVP2AXI_RESUME_DVP2AXI);
	return 0;
}

static int __maybe_unused es_dvp2axi_runtime_suspend(struct device *dev)
{
	struct es_dvp2axi_device *dvp2axi_dev = dev_get_drvdata(dev);
	int ret = 0;

	if (atomic_dec_return(&dvp2axi_dev->power_cnt))
		return 0;

	mutex_lock(&dvp2axi_dev->hw_dev->dev_lock);
	ret = pm_runtime_put_sync(dvp2axi_dev->hw_dev->dev);
	mutex_unlock(&dvp2axi_dev->hw_dev->dev_lock);
	return (ret > 0) ? 0 : ret;
}

static int __maybe_unused es_dvp2axi_runtime_resume(struct device *dev)
{
	struct es_dvp2axi_device *dvp2axi_dev = dev_get_drvdata(dev);
	int ret = 0;

	if (atomic_inc_return(&dvp2axi_dev->power_cnt) > 1)
		return 0;

	mutex_lock(&dvp2axi_dev->hw_dev->dev_lock);
	ret = pm_runtime_resume_and_get(dvp2axi_dev->hw_dev->dev);
	mutex_unlock(&dvp2axi_dev->hw_dev->dev_lock);
	es_dvp2axi_do_soft_reset(dvp2axi_dev);
	return (ret > 0) ? 0 : ret;
}

static int __maybe_unused __es_dvp2axi_clr_unready_dev(void)
{
	struct es_dvp2axi_device *dvp2axi_dev;

	mutex_lock(&es_dvp2axi_dev_mutex);

	list_for_each_entry(dvp2axi_dev, &es_dvp2axi_device_list, list) {
		// v4l2_async_notifier_clr_unready_dev(&dvp2axi_dev->notifier);
		v4l2_async_nf_cleanup(&dvp2axi_dev->notifier);
		subdev_asyn_register_itf(dvp2axi_dev);
	}

	mutex_unlock(&es_dvp2axi_dev_mutex);

	return 0;
}

static int es_dvp2axi_clr_unready_dev_param_set(const char *val,
					   const struct kernel_param *kp)
{
#ifdef MODULE
	__es_dvp2axi_clr_unready_dev();
#endif

	return 0;
}

module_param_call(clr_unready_dev, es_dvp2axi_clr_unready_dev_param_set, NULL, NULL,
		  0200);
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

static const struct dev_pm_ops es_dvp2axi_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(es_dvp2axi_sleep_suspend, es_dvp2axi_sleep_resume)
		SET_RUNTIME_PM_OPS(es_dvp2axi_runtime_suspend, es_dvp2axi_runtime_resume,
				   NULL)
};

struct platform_driver es_dvp2axi_plat_drv = {
	.driver = {
		.name = DVP2AXI_DRIVER_NAME,
		.of_match_table = of_match_ptr(es_dvp2axi_plat_of_match),
		.pm = &es_dvp2axi_plat_pm_ops,
	},
	.probe = es_dvp2axi_plat_probe,
	.remove = es_dvp2axi_plat_remove,
};
EXPORT_SYMBOL(es_dvp2axi_plat_drv);

MODULE_AUTHOR("Eswin VI team");
MODULE_DESCRIPTION("Eswin DVP2AXI platform driver");
MODULE_LICENSE("GPL v2");
