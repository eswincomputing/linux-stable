
// SPDX-License-Identifier: GPL-2.0
/*
 * Regulator driver for TI TPS549D22
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
 * Authors: Yang Wei <yangwei1@eswincomputing.com>
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>

#define TPS549D22_CMD_OPERATION 0x1
#define TPS549D22_CMD_ON_OFF_CONFIG 0x2
#define TPS549D22_CMD_CLEAR_FAULT 0x3
#define TPS549D22_CMD_WRITE_PROTECTION 0x10
#define TPS549D22_CMD_CAPABILITY 0x19
#define TPS549D22_CMD_PMBUS_PS_NUM 0x1C
#define TPS549D22_CMD_VOUT_MODE 0x20
#define TPS549D22_CMD_VOUT_COMMAND 0x21
#define TPS549D22_CMD_VOUT_MARGIN_HIGH 0x25
#define TPS549D22_CMD_VOUT_MARGIN_LOW 0x26
#define TPS549D22_CMD_STATUS_BYTE 0x78
#define TPS549D22_CMD_STATUS_WORD 0x79
#define TPS549D22_CMD_STATUS_VOUT 0x7A
#define TPS549D22_CMD_STATUS_IOUT 0x7B
#define TPS549D22_CMD_STATUS_CML 0x7E

#define TPS549D22_VOLT_STEP 2170 // uV
#define TPS549D22_MASK_VOUT_VALUE 0x3FF
#define TPS549D22_MASK_OPERATION_ENABLE 0X80

struct TPS549D22_DRIVER_DATA
{
	struct regulator_dev *rdev;
	struct i2c_client *client;
	struct mutex config_lock;
};

static struct of_regulator_match tps549d22_matches[] = {
	{
		.name = "npu_svcc",
	},
};

static inline u32 tps549d22_volt2reg(u32 vlot_uv)
{
	u32 value = 0;

	value = DIV_ROUND_CLOSEST(vlot_uv, TPS549D22_VOLT_STEP);
	return value;
}

static inline s32 tps549d22_str2ul(const char *buf, u32 *value)
{
	unsigned long cache = 0;
	int ret = 0;

	if (NULL == strstr(buf, "0x"))
	{
		ret = kstrtoul(buf, 10, &cache);
	}
	else
	{
		ret = kstrtoul(buf, 16, &cache);
	}
	*value = cache;

	return ret;
}

static u8 tps549d22_read_byte(struct TPS549D22_DRIVER_DATA *data, u8 command)
{
	int ret = 0;
	mutex_lock(&data->config_lock);
	ret = i2c_smbus_read_byte_data(data->client, command);
	mutex_unlock(&data->config_lock);
	if (ret < 0)
	{
		dev_err(&data->client->dev, "get command:0x%x value error:%d\n", command,
				ret);
		return 0xff;
	}
	return (u8)ret;
}

static s32 tps549d22_write_byte(struct TPS549D22_DRIVER_DATA *data, u8 command,
								u8 val)
{
	int ret = 0;
	mutex_lock(&data->config_lock);
	ret = i2c_smbus_write_byte_data(data->client, command, val);
	mutex_unlock(&data->config_lock);
	if (ret < 0)
	{
		dev_err(&data->client->dev, "set command:0x%x value:0x%x error:%d\n",
				command, val, ret);
	}
	return ret;
}

static s32 tps549d22_update_byte(struct TPS549D22_DRIVER_DATA *data, u8 command,
								 u8 mask, u8 val)
{
	u8 old_value = 0;
	u8 new_value = 0;
	if (0 != (~mask & val))
	{
		dev_err(&data->client->dev, "command:0x%x,input:0x%x outrange mask:0x%x\n",
				command, val, mask);
		return -EINVAL;
	}
	old_value = tps549d22_read_byte(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return tps549d22_write_byte(data, command, new_value);
}

static u16 tps549d22_read_word(struct TPS549D22_DRIVER_DATA *data, u8 command)
{
	int ret = 0;
	mutex_lock(&data->config_lock);
	ret = i2c_smbus_read_word_data(data->client, command);
	mutex_unlock(&data->config_lock);
	if (ret < 0)
	{
		dev_err(&data->client->dev, "get command:0x%x value error:%d\n", command,
				ret);
		return 0xffff;
	}
	return (u16)ret;
}

static u16 tps549d22_read_mask_word(struct TPS549D22_DRIVER_DATA *data, u8 command,
									u16 mask)
{
	u16 ret = tps549d22_read_word(data, command);
	return (ret & mask);
}

static s32 tps549d22_write_word(struct TPS549D22_DRIVER_DATA *data, u8 command,
								u16 val)
{
	int ret = 0;
	mutex_lock(&data->config_lock);
	ret = i2c_smbus_write_word_data(data->client, command, val);
	mutex_unlock(&data->config_lock);
	if (ret < 0)
	{
		dev_err(&data->client->dev, "set command:0x%x value:0x%x error:%d\n",
				command, val, ret);
	}
	return ret;
}

static s32 tps549d22_update_word(struct TPS549D22_DRIVER_DATA *data, u8 command,
								 u16 mask, u16 val)
{
	u16 old_value = 0;
	u16 new_value = 0;
	if (0 != (~mask & val))
	{
		dev_err(&data->client->dev, "command:0x%x,input:0x%x outrange mask:0x%x\n",
				command, val, mask);
		return -EINVAL;
	}
	old_value = tps549d22_read_word(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return tps549d22_write_word(data, command, new_value);
}

static int tps549d22_get_enable(struct TPS549D22_DRIVER_DATA *data)
{
	u8 cache = 0;

	cache = tps549d22_read_byte(data, TPS549D22_CMD_OPERATION);

	return ((cache >> 7) & 0x1);
}

static u32 tps549d22_get_vout(struct TPS549D22_DRIVER_DATA *data)
{
	u32 get_value = tps549d22_read_mask_word(data, TPS549D22_CMD_VOUT_COMMAND,
											 TPS549D22_MASK_VOUT_VALUE);
	return (get_value * TPS549D22_VOLT_STEP / 1000);
}

static s32 tps549d22_set_vout(struct TPS549D22_DRIVER_DATA *data, u32 volt_uv)
{
	u16 new_value = tps549d22_volt2reg(volt_uv);

	const struct regulation_constraints *constraints = &tps549d22_matches[0].init_data->constraints;

	if ((volt_uv > (constraints->max_uV)) || (volt_uv < (constraints->min_uV)))
	{
		dev_err(&data->rdev->dev, "max:%duV,min:%duV,now:%duV\n",
				(constraints->max_uV), constraints->min_uV, volt_uv);
		return -EINVAL;
	}

	return tps549d22_update_word(data, TPS549D22_CMD_VOUT_COMMAND,
								 TPS549D22_MASK_VOUT_VALUE, (new_value));
}

int tps549d22_regulator_enable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return tps549d22_update_byte(data, TPS549D22_CMD_OPERATION,
								 TPS549D22_MASK_OPERATION_ENABLE,
								 TPS549D22_MASK_OPERATION_ENABLE);
}

int tps549d22_regulator_disable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return tps549d22_update_byte(data, TPS549D22_CMD_OPERATION,
								 TPS549D22_MASK_OPERATION_ENABLE, 0);
}
int tps549d22_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return tps549d22_get_enable(data);
}

static struct linear_range tps549d22_ext_ranges[] = {
	REGULATOR_LINEAR_RANGE(666000, 307, 614, TPS549D22_VOLT_STEP),
};

/**
 * tps549d22_set_voltage_sel -  set_voltage_sel for users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 */
static s32 tps549d22_set_voltage_sel(struct regulator_dev *rdev,
									 unsigned selector)
{
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 new_value = 0;

	if (selector > tps549d22_ext_ranges->max_sel)
	{
		dev_err(dev, "selector:%u out of rang 0~%u\n", selector,
				tps549d22_ext_ranges->max_sel);
		return -EINVAL;
	}

	new_value = tps549d22_ext_ranges->step * selector + tps549d22_ext_ranges->min;

	dev_dbg(dev, "%s_volt:%duV,selector:%u,step:%u,min:%u\n", __FUNCTION__,
			new_value, selector, tps549d22_ext_ranges->step,
			tps549d22_ext_ranges->min);

	tps549d22_set_vout(data, new_value);

	return 0;
}

/**
 * tps549d22_get_voltage_sel -  get_voltage_sel for users
 *
 * @rdev: regulator to operate on
 */
static s32 tps549d22_get_voltage_sel(struct regulator_dev *rdev)
{
	s32 index = 0;
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);

	index = tps549d22_read_mask_word(data, TPS549D22_CMD_VOUT_COMMAND, TPS549D22_MASK_VOUT_VALUE);
	index = index - DIV_ROUND_CLOSEST(tps549d22_ext_ranges->min, TPS549D22_VOLT_STEP);
	dev_dbg(dev, "%s index:%d\n", __FUNCTION__, index);
	return index;
}

static s32 tps549d22_get_error_flags(struct regulator_dev *rdev, u32 *flags)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);

	*flags = tps549d22_read_byte(data, TPS549D22_CMD_STATUS_WORD);

	dev_dbg(&rdev->dev, "tps549d22_get_error_flags_%u\n", *flags);
	return 0;
}



int tps549d22_list_voltage_linear_range(struct regulator_dev *rdev,
					unsigned int selector)
{
	int value = 0;
	value = regulator_desc_list_voltage_linear_range(rdev->desc, selector);
	value = (value/2000)*2000;
	return value;
}

static struct regulator_ops tps549d22_core_ops = {

	.list_voltage = tps549d22_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = tps549d22_set_voltage_sel,
	.get_voltage_sel = tps549d22_get_voltage_sel,
	/* enable/disable regulator */
	.enable = tps549d22_regulator_enable,
	.disable = tps549d22_regulator_disable,
	.is_enabled = tps549d22_regulator_is_enabled,
	.get_error_flags = tps549d22_get_error_flags,

};
static struct regulator_desc tps549d22_regulator_desc = {
	.name = "NPUVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 307,
	.ops = &tps549d22_core_ops,
	.linear_ranges = tps549d22_ext_ranges,
	.n_linear_ranges = ARRAY_SIZE(tps549d22_ext_ranges),
	.owner = THIS_MODULE,
};

static ssize_t tps549d22_vout_show(struct device *d,
								   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%u", tps549d22_get_vout(data));
}
static ssize_t tps549d22_vout_store(struct device *d,
									struct device_attribute *attr,
									const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 volt_value = 0;
	int ret = 0;
	ret = tps549d22_str2ul(buf, &volt_value);

	if (ret)
	{
		return ret;
	}
	ret = tps549d22_set_vout(data, volt_value * 1000);
	if (0 != ret)
	{
		return ret;
	}
	return count;
}
DEVICE_ATTR(tps549d22_vout, 0600, tps549d22_vout_show, tps549d22_vout_store);

static ssize_t tps549d22_enable_show(struct device *d,
									 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%u", data->rdev->desc->ops->is_enabled(data->rdev));
}
static ssize_t tps549d22_enable_store(struct device *d,
									  struct device_attribute *attr,
									  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 value = 0;
	int ret = 0;
	ret = tps549d22_str2ul(buf, &value);

	if (ret)
	{
		return ret;
	}
	if (0 == value)
	{
		data->rdev->desc->ops->disable(data->rdev);
	}
	else
	{
		data->rdev->desc->ops->enable(data->rdev);
	}
	return count;
}
DEVICE_ATTR(tps549d22_enable, 0600, tps549d22_enable_show, tps549d22_enable_store);

static ssize_t tps549d22_status_show(struct device *d,
									 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct TPS549D22_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "STATUS_BYTE:0x%x\nSTATUS_WORD:0x%x\n"
						   "STATUS_VOUT:0x%x\nSTATUS_IOUT:0x%x\nSTATUS_CML:0x%x\n",
					  tps549d22_read_byte(data, TPS549D22_CMD_STATUS_BYTE),
					  tps549d22_read_byte(data, TPS549D22_CMD_STATUS_WORD),
					  tps549d22_read_byte(data, TPS549D22_CMD_STATUS_VOUT),
					  tps549d22_read_byte(data, TPS549D22_CMD_STATUS_IOUT),
					  tps549d22_read_byte(data, TPS549D22_CMD_STATUS_CML));
}
DEVICE_ATTR_RO(tps549d22_status);

static struct attribute *tps549d22_attrs[] = {
	&dev_attr_tps549d22_vout.attr, &dev_attr_tps549d22_enable.attr,
	&dev_attr_tps549d22_status.attr, NULL};

ATTRIBUTE_GROUPS(tps549d22);

static s32 tps549d22_init_data(struct TPS549D22_DRIVER_DATA *data,
							   const struct regulation_constraints *constraints, u32 default_voltage)
{
	s32 ret = 0;
	struct device *dev = &data->client->dev;

	tps549d22_ext_ranges->min = constraints->min_uV;
	tps549d22_ext_ranges->min_sel = 0;
	tps549d22_ext_ranges->max_sel = DIV_ROUND_CLOSEST(constraints->max_uV - constraints->min_uV, TPS549D22_VOLT_STEP) + 1;
	tps549d22_regulator_desc.n_voltages = tps549d22_ext_ranges->max_sel - tps549d22_ext_ranges->min_sel;
	dev_dbg(dev,
			"min_uV:%d,max_uV:%d,min_sel:%d,max_sel:%d,n_voltages:%d\n",
			tps549d22_ext_ranges->min, constraints->max_uV,
			tps549d22_ext_ranges->min_sel, tps549d22_ext_ranges->max_sel,
			tps549d22_regulator_desc.n_voltages);

	tps549d22_set_vout(data, default_voltage);
	tps549d22_write_byte(data, TPS549D22_CMD_ON_OFF_CONFIG, 0xf);
	ret =  tps549d22_update_byte(data, TPS549D22_CMD_OPERATION,
								 TPS549D22_MASK_OPERATION_ENABLE,
								 TPS549D22_MASK_OPERATION_ENABLE);
	mdelay(20);
	return ret;
}

static s32 tps549d22_probe(struct i2c_client *client)
{
	struct TPS549D22_DRIVER_DATA *data = NULL;
	s32 ret = 0;
	s32 regulator_cnt = 0;
	u32 default_voltage = 0;
	struct device *hwmon_dev;
	struct regulator_config config = {};
	struct device *dev = &client->dev;
	struct device_node *np, *parent;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
	{
		dev_err(dev, "not support smbus\n");
		return -EIO;
	}
	data = devm_kzalloc(dev, sizeof(struct TPS549D22_DRIVER_DATA), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->config_lock);
	data->client = client;
	i2c_set_clientdata(client, data);
	/* Get the device (PMIC) node */
	np = of_node_get(dev->of_node);
	if (!np)
		return -EINVAL;

	/* Get 'regulators' subnode */
	parent = of_get_child_by_name(np, "regulators");
	if (!parent)
	{
		dev_err(dev, "regulators node not found\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(np, "eswin,regulator_default-microvolt", &default_voltage);
	if (ret)
	{
		default_voltage = 900000;
	}

	/* fill isl6271a_matches array */
	regulator_cnt = of_regulator_match(dev, parent, tps549d22_matches, ARRAY_SIZE(tps549d22_matches));
	of_node_put(parent);
	if (regulator_cnt != 1)
	{
		dev_err(dev, "Error parsing regulator init data: %d\n", regulator_cnt);
		return regulator_cnt;
	}

	/* Fetched from device tree */
	config.init_data = tps549d22_matches[0].init_data;
	config.dev = dev;
	config.of_node = tps549d22_matches[0].of_node;
	/* config.ena_gpio = -EINVAL; */
	ret = tps549d22_init_data(data, &config.init_data->constraints, default_voltage);
	if (0 != ret)
	{
		dev_err(dev, "init tps549d22 error\n");
		return -EIO;
	}
	data->rdev = devm_regulator_register(dev, &tps549d22_regulator_desc, &config);
	if (IS_ERR(data->rdev))
	{
		dev_err(dev, "failed to register %s\n", tps549d22_regulator_desc.name);
	}
	hwmon_dev = devm_hwmon_device_register_with_groups(
		dev, client->name, data, tps549d22_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "tps549d22_probe\n");

	return 0;
}

static void tps549d22_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "tps549d22_remove\n");
}

static s32 tps549d22_detect(struct i2c_client *client,
							struct i2c_board_info *info)
{
	dev_info(&client->dev, "tps549d22_detect\n");
	return 0;
}

static const struct i2c_device_id tps549d22_id[] = {{"tps549d22", 0}, {}};
MODULE_DEVICE_TABLE(i2c, tps549d22_id);

/* Addresses to scan */
static const unsigned short normal_i2c[] = {0x2c, 0x2d, 0x2e, 0x60,
											I2C_CLIENT_END};

static struct i2c_driver tps549d22_driver = {
	.class = I2C_CLASS_HWMON,
	.driver =
		{
			.name = "tps549d22",
		},
	.probe = tps549d22_probe,
	.remove = tps549d22_remove,
	.id_table = tps549d22_id,
	.detect = tps549d22_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(tps549d22_driver);

MODULE_AUTHOR("Yang Wei <yangwei1@eswincomputing.com>");
MODULE_DESCRIPTION("tps549d22 driver");
MODULE_LICENSE("GPL");
