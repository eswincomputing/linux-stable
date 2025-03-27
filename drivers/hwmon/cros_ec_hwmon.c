// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ChromeOS EC driver for hwmon
 *
 *  Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/of.h>

#define DRV_NAME	"cros-ec-hwmon"

struct cros_ec_hwmon_priv {
	struct cros_ec_device *cros_ec;
	const char *temp_sensor_names[EC_TEMP_SENSOR_ENTRIES + EC_TEMP_SENSOR_B_ENTRIES];
	bool has_temp_threshold;
};

static int cros_ec_hwmon_read_temp(struct device *dev, struct cros_ec_device *cros_ec, u8 index, u8 *temp)
{
	unsigned int offset;
	int ret;

	if (index < EC_TEMP_SENSOR_ENTRIES)
		offset = EC_MEMMAP_TEMP_SENSOR + index;
	else
		offset = EC_MEMMAP_TEMP_SENSOR_B + index - EC_TEMP_SENSOR_ENTRIES;

	dev_dbg(dev, "Reading temperature for sensor %d at offset 0x%x\n",
		index, offset);
	ret = cros_ec_cmd_readmem(cros_ec, offset, 1, temp);
	if (ret < 0) {
		dev_dbg(dev, "Failed to read temp sensor %d: %d\n", index, ret);
		return ret;
	}
	dev_dbg(dev, "Temp sensor %d raw value: %d\n", index, *temp);
	return 0;
}

static int cros_ec_hwmon_read_temp_threshold(struct device *dev, struct cros_ec_device *cros_ec, u8 index,
					     enum ec_temp_thresholds threshold, u32 *temp)
{
	struct ec_params_thermal_get_threshold_v1 req = {};
	struct ec_thermal_config resp;
	int ret;
	req.sensor_num = index;

	dev_dbg(dev, "Reading temp threshold %d for sensor %d\n", threshold, index);
	ret = cros_ec_cmd(cros_ec, 1, EC_CMD_THERMAL_GET_THRESHOLD, &req,
			  sizeof(req), &resp, sizeof(resp));
	if (ret < 0) {
		dev_dbg(dev, "Failed to read temp threshold for sensor %d: %d\n",
			index, ret);
		return ret;
	}
	*temp = resp.temp_host[threshold];
	dev_dbg(dev, "Temp threshold %d for sensor %d: %d\n",
		threshold, index, *temp);
	return 0;
}

static int cros_ec_hwmon_write_temp_threshold(struct device *dev, struct cros_ec_device *cros_ec, u8 index,
					      enum ec_temp_thresholds threshold, u32 temp)
{
	struct ec_params_thermal_get_threshold_v1 get_req = {};
	struct ec_params_thermal_set_threshold_v1 set_req = {};
	int ret;

	get_req.sensor_num = index;
	dev_dbg(dev, "Writing temp threshold %d for sensor %d to %d\n",
		threshold, index, temp);
	ret = cros_ec_cmd(cros_ec, 1, EC_CMD_THERMAL_GET_THRESHOLD, &get_req,
			  sizeof(get_req), &set_req.cfg, sizeof(set_req.cfg));
	if (ret < 0) {
		dev_dbg(dev, "Failed to get current threshold for sensor %d: %d\n",
			index, ret);
		return ret;
	}

	set_req.sensor_num = index;
	set_req.cfg.temp_host[threshold] = temp;
	ret = cros_ec_cmd(cros_ec, 1, EC_CMD_THERMAL_SET_THRESHOLD, &set_req,
			   sizeof(set_req), NULL, 0);
	if (ret < 0)
		dev_dbg(dev, "Failed to set temp threshold for sensor %d: %d\n",
			index, ret);
	return ret;
}

static bool cros_ec_hwmon_is_error_temp(u8 temp)
{
	return temp == EC_TEMP_SENSOR_NOT_PRESENT     ||
	       temp == EC_TEMP_SENSOR_ERROR           ||
	       temp == EC_TEMP_SENSOR_NOT_POWERED     ||
	       temp == EC_TEMP_SENSOR_NOT_CALIBRATED;
}

static long cros_ec_hwmon_temp_to_millicelsius(u8 temp)
{
	return kelvin_to_millicelsius((((long)temp) + EC_TEMP_SENSOR_OFFSET));
}

static enum ec_temp_thresholds cros_ec_hwmon_attr_to_thres(u32 attr)
{
	if (attr == hwmon_temp_max)
		return EC_TEMP_THRESH_WARN;
	else if (attr == hwmon_temp_crit)
		return EC_TEMP_THRESH_HIGH;
	else if (attr == hwmon_temp_emergency)
		return EC_TEMP_THRESH_HALT;
	else
		return 0;
}

static int cros_ec_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);
	int ret = -EOPNOTSUPP;
	u32 threshold;
	u8 temp;

	dev_dbg(dev, "Reading type %d attr %d channel %d\n", type, attr, channel);

	if (type == hwmon_temp) {
		if (attr == hwmon_temp_input) {
			ret = cros_ec_hwmon_read_temp(dev, priv->cros_ec, channel, &temp);
			if (ret == 0) {
				if (cros_ec_hwmon_is_error_temp(temp))
					ret = -ENODATA;
				else
					*val = cros_ec_hwmon_temp_to_millicelsius(temp);
			}
		} else if (attr == hwmon_temp_fault) {
			ret = cros_ec_hwmon_read_temp(dev, priv->cros_ec, channel, &temp);
			if (ret == 0)
				*val = cros_ec_hwmon_is_error_temp(temp);
		} else if (attr == hwmon_temp_max || attr == hwmon_temp_crit ||
			   attr == hwmon_temp_emergency) {
			ret = cros_ec_hwmon_read_temp_threshold(dev, priv->cros_ec, channel,
													cros_ec_hwmon_attr_to_thres(attr),
													&threshold);
			if (ret == 0)
				*val = kelvin_to_millicelsius(threshold);
		}
	}

	if (ret < 0)
		dev_dbg(dev, "Read failed with error: %d\n", ret);
	else if (ret == 0)
		dev_dbg(dev, "Read successful, value: %ld\n", *val);
	return ret;
}

static int cros_ec_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				     u32 attr, int channel, const char **str)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);

	if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = priv->temp_sensor_names[channel];
		dev_dbg(dev, "Temp sensor %d label: %s\n", channel, *str);
		return 0;
	}

	return -EOPNOTSUPP;
}

static int cros_ec_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long val)
{
	struct cros_ec_hwmon_priv *priv = dev_get_drvdata(dev);
	int ret = -EOPNOTSUPP;

	dev_dbg(dev, "Writing type %d attr %d channel %d value %ld\n",
		type, attr, channel, val);

	if (type == hwmon_temp) {
		ret = cros_ec_hwmon_write_temp_threshold(dev, priv->cros_ec, channel,
												cros_ec_hwmon_attr_to_thres(attr),
												millicelsius_to_kelvin(val));
	}
	if (ret < 0)
		dev_dbg(dev, "Write failed with error: %d\n", ret);
	return ret;
}

static umode_t cros_ec_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	const struct cros_ec_hwmon_priv *priv = data;

	if (type == hwmon_temp) {
		if (priv->temp_sensor_names[channel]) {
			if (attr == hwmon_temp_max ||
			    attr == hwmon_temp_crit ||
			    attr == hwmon_temp_emergency) {
				if (priv->has_temp_threshold)
					return 0644;
			} else {
				return 0444;
			}
		}
	}

	return 0;
}

static const struct hwmon_channel_info * const cros_ec_hwmon_info[] = {
#define cros_EC_HWMON_TEMP_PARAMS (HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_LABEL | \
				   HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_EMERGENCY)
	HWMON_CHANNEL_INFO(temp,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS,
			   cros_EC_HWMON_TEMP_PARAMS),
	NULL
};

static const struct hwmon_ops cros_ec_hwmon_ops = {
	.read = cros_ec_hwmon_read,
	.read_string = cros_ec_hwmon_read_string,
	.write = cros_ec_hwmon_write,
	.is_visible = cros_ec_hwmon_is_visible,
};

static const struct hwmon_chip_info cros_ec_hwmon_chip_info = {
	.ops = &cros_ec_hwmon_ops,
	.info = cros_ec_hwmon_info,
};

static void cros_ec_hwmon_probe_temp_sensors(struct device *dev, struct cros_ec_hwmon_priv *priv,
					     u8 thermal_version)
{
	struct ec_params_temp_sensor_get_info req = {};
	struct ec_response_temp_sensor_get_info resp;
	size_t candidates, i, sensor_name_size;
	int ret;
	u32 threshold;
	u8 temp;

	dev_dbg(dev, "Probing temperature sensors, thermal version: %d\n", thermal_version);

	ret = cros_ec_hwmon_read_temp_threshold(dev, priv->cros_ec, 0, EC_TEMP_THRESH_HIGH, &threshold);
	if (ret == 0) {
		priv->has_temp_threshold = 1;
		dev_dbg(dev, "Temperature threshold support detected\n");
	} else {
		dev_dbg(dev, "No temperature threshold support: %d\n", ret);
	}

	if (thermal_version < 2)
		candidates = EC_TEMP_SENSOR_ENTRIES;
	else
		candidates = ARRAY_SIZE(priv->temp_sensor_names);

	for (i = 0; i < candidates; i++) {
		if (cros_ec_hwmon_read_temp(dev, priv->cros_ec, i, &temp) < 0)
			continue;

		if (temp == EC_TEMP_SENSOR_NOT_PRESENT)
			continue;

		req.id = i;
		ret = cros_ec_cmd(priv->cros_ec, 0, EC_CMD_TEMP_SENSOR_GET_INFO,
				  &req, sizeof(req), &resp, sizeof(resp));
		if (ret < 0) {
			dev_dbg(dev, "Failed to get sensor %zu info: %d\n", i, ret);
			continue;
		}

		sensor_name_size = strnlen(resp.sensor_name, sizeof(resp.sensor_name));
		priv->temp_sensor_names[i] = devm_kasprintf(dev, GFP_KERNEL, "%.*s",
							    (int)sensor_name_size,
							    resp.sensor_name);
		dev_dbg(dev, "Found sensor %zu: %s\n", i, priv->temp_sensor_names[i]);
	}
}

static int cros_ec_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct cros_ec_hwmon_priv *priv;
	struct device *hwmon_dev;
	u8 thermal_version;
	int ret;

#if 0
	ret = cros_ec_cmd_readmem(ec_dev, EC_MEMMAP_THERMAL_VERSION, 1, &thermal_version);
	if (ret < 0)
		return ret;

	/* Covers both fan and temp sensors */
	if (thermal_version == 0)
		return -ENODEV;
#else
	ret = 0;
	thermal_version = 3;
#endif

	dev_dbg(dev, "Starting ChromeOS EC hwmon probe\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_dbg(dev, "Failed to allocate private data\n");
		return -ENOMEM;
	}
	priv->cros_ec = cros_ec;
	dev_dbg(dev, "Private data initialized\n");

	cros_ec_hwmon_probe_temp_sensors(dev, priv, thermal_version);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "cros_ec", priv,
							 &cros_ec_hwmon_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		dev_dbg(dev, "Failed to register hwmon device: %ld\n", PTR_ERR(hwmon_dev));
	else
		dev_dbg(dev, "Hwmon device registered successfully\n");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct platform_device_id cros_ec_hwmon_id[] = {
	{ DRV_NAME, 0 },
	{}
};

static struct platform_driver cros_ec_hwmon_driver = {
	.driver.name	= DRV_NAME,
	.probe 		= cros_ec_hwmon_probe,
	.id_table	= cros_ec_hwmon_id,
};

module_platform_driver(cros_ec_hwmon_driver);

MODULE_DEVICE_TABLE(platform, cros_ec_hwmon_id);
MODULE_DESCRIPTION("ChromeOS EC Hardware Monitoring Driver");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net");
MODULE_LICENSE("GPL");
