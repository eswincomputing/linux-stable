
// SPDX-License-Identifier: GPL-2.0
/*
 * eswin Specific Glue layer
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

#define ES5340_VOLT_DENOMINATOR 604
#define ES5340_VOLT_NUMERATOR 60
#define ES5340_INIT_VOLT 1050

#define ES5340_CMD_OPERATION 0x1
#define ES5340_CMD_VOUT_COMMAND 0x21
#define ES5340_CMD_VOUT_OV_WARN_LIMIT 0x42
#define ES5340_CMD_IOUT_OC_WARN_LIMIT 0x4A
#define ES5340_CMD_OT_WARN_LIMIT 0x51
#define ES5340_CMD_VIN_OV_WARN_LIMIT 0x57
#define ES5340_CMD_STATUS_BYTE 0x78
#define ES5340_CMD_STATUS_WORD 0x79
#define ES5340_CMD_READ_VIN 0x88
#define ES5340_CMD_READ_VOUT 0x8B
#define ES5340_CMD_READ_IOUT 0x8C
#define ES5340_CMD_READ_TEMPERATURE 0x8D

#define ES5340_LABEL_CNT 4

struct ES5340_DRIVER_DATA
{
	struct regulator_dev *rdev;
	struct regulator_desc *dev_desc;
	struct i2c_client *client;
	struct mutex config_lock;
	char es5340_label[ES5340_LABEL_CNT][20];
};

#define ES5340_MASK_OPERATION_ENABLE 0X80

#define ES5340_MASK_OV_VOLT 0x3FFF
#define ES5340_MASK_VOUT_VALUE 0xFF
#define ES5340_MASK_IOUT 0x3FF
#define ES5340_MASK_TOUT 0xFF
#define ES5340_VOLTE_IN_SENSE_LSB 8
#define ES5340_CURRENT_LSB 31
#define ES5340_TEMPERATURE_LSB 1000 /*1mC*/

static struct of_regulator_match es5340_matches[] = {
	{
		.name = "npu_svcc",
	},
};

static inline s32 es5340_str2ul(const char *buf, u32 *value)
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

static u8 es5340_read_byte(struct ES5340_DRIVER_DATA *data, u8 command)
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

static s32 es5340_write_byte(struct ES5340_DRIVER_DATA *data, u8 command,
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

static s32 es5340_update_byte(struct ES5340_DRIVER_DATA *data, u8 command,
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
	old_value = es5340_read_byte(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return es5340_write_byte(data, command, new_value);
}

static u16 es5340_read_word(struct ES5340_DRIVER_DATA *data, u8 command)
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

static u16 es5340_read_mask_word(struct ES5340_DRIVER_DATA *data, u8 command,
								 u16 mask)
{
	u16 ret = es5340_read_word(data, command);
	return (ret & mask);
}

static s32 es5340_write_word(struct ES5340_DRIVER_DATA *data, u8 command,
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

static s32 es5340_update_word(struct ES5340_DRIVER_DATA *data, u8 command,
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
	old_value = es5340_read_word(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return es5340_write_word(data, command, new_value);
}

static int es5340_get_enable(struct ES5340_DRIVER_DATA *data)
{
	u8 cache = 0;

	cache = es5340_read_byte(data, ES5340_CMD_OPERATION);

	return ((cache >> 7) & 0x1);
}

static const struct hwmon_channel_info *es5340_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MAX_ALARM | HWMON_I_LABEL,
					   HWMON_I_INPUT | HWMON_I_ENABLE | HWMON_I_MAX | HWMON_I_MAX_ALARM | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_MAX_ALARM | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_ALARM | HWMON_T_LABEL),
	NULL};

static umode_t es5340_is_visible(const void *_data,
								 enum hwmon_sensor_types type, u32 attr,
								 int channel)
{
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_input:
		case hwmon_in_label:
		case hwmon_in_max_alarm:
			return 0444;
		case hwmon_in_enable:
		case hwmon_in_max:
			return 0644;
		}

		break;
	case hwmon_curr:
		switch (attr)
		{
		case hwmon_curr_input:
		case hwmon_curr_max_alarm:
		case hwmon_curr_label:
			return 0444;
		case hwmon_curr_max:
			return 0644;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_input:
		case hwmon_temp_label:
		case hwmon_temp_max_alarm:
			return 0444;
		case hwmon_temp_max:
			return 0644;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int es5340_read(struct device *dev, enum hwmon_sensor_types type,
					   u32 attr, int channel, long *val)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 get_value = 0;

	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_input:
			if (channel == 0)
			{
				get_value = es5340_read_word(data, ES5340_CMD_READ_VIN);
				*val = get_value * ES5340_VOLTE_IN_SENSE_LSB;
			}
			else if (channel == 1)
			{
				*val = es5340_read_word(data, ES5340_CMD_READ_VOUT);
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		case hwmon_in_max_alarm:
			get_value = es5340_read_word(data, ES5340_CMD_STATUS_WORD);
			if (channel == 0)
			{
				*val = ((get_value >> 13) & 0x1);
			}
			else if (channel == 1)
			{
				*val = ((get_value >> 15) & 0x1);
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		case hwmon_in_enable:
			*val = es5340_get_enable(data);
			break;
		case hwmon_in_max:
			if (channel == 0)
			{
				get_value = es5340_read_mask_word(data, ES5340_CMD_VIN_OV_WARN_LIMIT, ES5340_MASK_OV_VOLT);
				*val = get_value * ES5340_VOLTE_IN_SENSE_LSB;
			}
			else if (channel == 1)
			{
				*val = es5340_read_mask_word(data, ES5340_CMD_VOUT_OV_WARN_LIMIT, ES5340_MASK_OV_VOLT);
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		}
		break;
	case hwmon_curr:
		switch (attr)
		{
		case hwmon_curr_input:
			get_value = es5340_read_mask_word(data, ES5340_CMD_READ_IOUT, ES5340_MASK_IOUT);
			*val = get_value * ES5340_CURRENT_LSB;
			break;
		case hwmon_curr_max_alarm:
			get_value = es5340_read_word(data, ES5340_CMD_STATUS_WORD);
			if (channel == 0)
			{
				*val = ((get_value >> 14) & 0x1);
			}
			break;

		case hwmon_curr_max:
			get_value = es5340_read_mask_word(data, ES5340_CMD_IOUT_OC_WARN_LIMIT, ES5340_MASK_IOUT);
			*val = get_value * ES5340_CURRENT_LSB;
			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_input:
			*val = es5340_read_mask_word(data, ES5340_CMD_READ_TEMPERATURE, 0x1ff);
			*val *= ES5340_TEMPERATURE_LSB;
			break;
		case hwmon_temp_crit_alarm:
			get_value = es5340_read_byte(data, ES5340_CMD_STATUS_BYTE); //?????
			if (channel == 0)
			{
				*val = ((get_value >> 2) & 0x1);
			}
			break;
		case hwmon_temp_max:
			*val = es5340_read_mask_word(data, ES5340_CMD_OT_WARN_LIMIT, 0xff);
			*val *= ES5340_TEMPERATURE_LSB;
			break;
		}
		break;

	default:
		break;
	}
	return 0;
}
static int es5340_read_string(struct device *dev,
							  enum hwmon_sensor_types type,
							  u32 attr, int channel, const char **str)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_label:
			if (channel == 0)
			{
				*str = data->es5340_label[0];
			}
			else if (channel == 1)
			{
				*str = data->es5340_label[1];
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		}
		break;
	case hwmon_curr:
		switch (attr)
		{
		case hwmon_curr_label:
			*str = data->es5340_label[2];
			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{

		case hwmon_temp_label:
			*str = data->es5340_label[3];
			break;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int es5340_write(struct device *dev, enum hwmon_sensor_types type,
						u32 attr, int channel, long val)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);

	int ret = 0;
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_enable:
			es5340_update_byte(data, ES5340_CMD_OPERATION,
							   ES5340_MASK_OPERATION_ENABLE, (u8)(val << 7));
			break;
		case hwmon_in_max:
			if (channel == 0)
			{
				ret = es5340_update_word(data, ES5340_CMD_VIN_OV_WARN_LIMIT, ES5340_MASK_OV_VOLT, (u16)(val / ES5340_VOLTE_IN_SENSE_LSB));
			}
			else if (channel == 1)
			{
				ret = es5340_update_word(data, ES5340_CMD_VOUT_OV_WARN_LIMIT, ES5340_MASK_OV_VOLT, (u16)(val));
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
		}
		break;
	case hwmon_curr:
		switch (attr)
		{

		case hwmon_curr_max:
			ret = es5340_update_word(data, ES5340_CMD_IOUT_OC_WARN_LIMIT,
									 ES5340_MASK_IOUT, (u16)(val / ES5340_CURRENT_LSB));
			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_max:
			ret = es5340_update_word(data, ES5340_CMD_OT_WARN_LIMIT,
									 ES5340_MASK_TOUT, (u16)(val / ES5340_TEMPERATURE_LSB));
			break;
		}
		break;

	default:
		break;
	}
	return ret;
}
static const struct hwmon_ops pac193x_hwmon_ops = {
	.is_visible = es5340_is_visible,
	.read = es5340_read,
	.write = es5340_write,
	.read_string = es5340_read_string,
};

static struct hwmon_chip_info es5340_chip_info = {
	.ops = &pac193x_hwmon_ops,
	.info = es5340_info,

};

static u8 es5340_volt2reg(struct ES5340_DRIVER_DATA *data, u32 volt_mv)
{
	s32 surplus_volt = (s32)ES5340_INIT_VOLT - (s32)volt_mv;
	s32 cache = 0;
	if (surplus_volt >= 0)
	{
		cache = (surplus_volt * ES5340_VOLT_NUMERATOR + ES5340_VOLT_DENOMINATOR - 1) / ES5340_VOLT_DENOMINATOR;
	}
	else
	{
		cache = (surplus_volt * ES5340_VOLT_NUMERATOR - ES5340_VOLT_DENOMINATOR + 1) / ES5340_VOLT_DENOMINATOR;
	}

	return (u8)cache;
}

static u32 es5340_reg2volt(struct ES5340_DRIVER_DATA *data, u8 reg_value)
{
	s32 surplus_volt = (s8)reg_value * ES5340_VOLT_DENOMINATOR / ES5340_VOLT_NUMERATOR;
	s32 cache = (s32)ES5340_INIT_VOLT - surplus_volt;

	return (u32)cache;
}

static u32 es5340_get_vout(struct ES5340_DRIVER_DATA *data)
{
	u32 get_value = es5340_read_mask_word(data, ES5340_CMD_VOUT_COMMAND,
										  ES5340_MASK_VOUT_VALUE);
	return es5340_reg2volt(data, (u8)get_value);
}

static s32 es5340_set_vout(struct ES5340_DRIVER_DATA *data, u32 volt_mv)
{
	u16 new_value = (u8)es5340_volt2reg(data, volt_mv);
	const struct regulation_constraints *constraints = &es5340_matches[0].init_data->constraints;

	if ((volt_mv > (constraints->max_uV / 1000)) || (volt_mv < (constraints->min_uV / 1000)))
	{
		dev_err(&data->rdev->dev, "max:%dmV,min:%dmV,now:%dmV\n",
				(constraints->max_uV / 1000), constraints->min_uV / 1000, volt_mv);
		return -EINVAL;
	}

	return es5340_update_word(data, ES5340_CMD_VOUT_COMMAND,
							  ES5340_MASK_VOUT_VALUE, (new_value));
}

static ssize_t es5340_vout_show(struct device *d,
								struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%u", es5340_get_vout(data));
}
static ssize_t es5340_vout_store(struct device *d,
								 struct device_attribute *attr,
								 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 volt_value = 0;
	int ret = 0;
	ret = es5340_str2ul(buf, &volt_value);

	if (ret)
	{
		return ret;
	}
	ret = es5340_set_vout(data, volt_value);
	if (0 != ret)
	{
		return ret;
	}
	return count;
}
DEVICE_ATTR(es5340_vout, 0600, es5340_vout_show, es5340_vout_store);

static struct attribute *es5340_attrs[] = {
	&dev_attr_es5340_vout.attr,
	NULL};

ATTRIBUTE_GROUPS(es5340);

static struct linear_range es5340_ext_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 320, 3125),
};

/**
 * es5340_set_voltage_sel -  set_voltage_sel for users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 */
static s32 es5340_set_voltage_sel(struct regulator_dev *rdev,
								  unsigned selector)
{
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 new_value = 0;

	if (selector > es5340_ext_ranges->max_sel)
	{
		dev_err(dev, "selector:%u out of rang 0~%u\n", selector,
				es5340_ext_ranges->max_sel);
		return -EINVAL;
	}

	new_value = es5340_ext_ranges->min + es5340_ext_ranges->step * selector;

	dev_dbg(dev, "%s_volt:%duV,selector:%u,step:%u,min:%u\n", __FUNCTION__,
			new_value, selector, es5340_ext_ranges->step,
			es5340_ext_ranges->min);

	es5340_set_vout(data, new_value / 1000);

	return 0;
}

/**
 * es5340_get_voltage_sel -  get_voltage_sel for users
 *
 * @rdev: regulator to operate on
 */
static s32 es5340_get_voltage_sel(struct regulator_dev *rdev)
{
	s32 index = 0;
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 volt_value = 0;
	u32 diff_volt = 0;

	volt_value = es5340_get_vout(data);
	volt_value *= 1000;

	if (volt_value >= es5340_ext_ranges->min)
	{
		diff_volt = volt_value - es5340_ext_ranges->min;
	}
	else
	{
		diff_volt = 0;
	}
	dev_dbg(dev, "%s_diff_volt:%duV,volt:%u,min:%u\n", __FUNCTION__, diff_volt,
			volt_value, es5340_ext_ranges->min);
	index = DIV_ROUND_CLOSEST(diff_volt, es5340_ext_ranges->step);
	if (index > es5340_ext_ranges->max_sel)
	{
		dev_err(dev, "volt:%duV out legal range\n", volt_value);
	}

	dev_dbg(dev, "%s_diff_volt:%duV,step:%d,index:%d\n", __FUNCTION__, diff_volt,
			es5340_ext_ranges->step, index);
	return index;
}

int es5340_regulator_enable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return es5340_update_byte(data, ES5340_CMD_OPERATION,
							  ES5340_MASK_OPERATION_ENABLE,
							  ES5340_MASK_OPERATION_ENABLE);
}

int es5340_regulator_disable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return es5340_update_byte(data, ES5340_CMD_OPERATION,
							  ES5340_MASK_OPERATION_ENABLE, 0);
}
int es5340_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct ES5340_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return es5340_get_enable(data);
}

static struct regulator_ops es5340_core_ops = {

	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,

	/* get/set regulator voltage */
	/* Only one of each(set_voltage&&set_voltage_sel) should be implemented */
	/* .set_voltage = es5340_set_voltage, */
	.set_voltage_sel = es5340_set_voltage_sel,

	/* Only one of each(get_voltage&&get_voltage_sel) should be implemented */
	/* .get_voltage=es5340_get_voltage, */
	.get_voltage_sel = es5340_get_voltage_sel,

	/* enable/disable regulator */
	.enable = es5340_regulator_enable,
	.disable = es5340_regulator_disable,
	.is_enabled = es5340_regulator_is_enabled,

};
static struct regulator_desc es5340_regulator_desc = {
	.name = "NPUVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 321,
	.ops = &es5340_core_ops,
	.linear_ranges = es5340_ext_ranges,
	.n_linear_ranges = ARRAY_SIZE(es5340_ext_ranges),
	.owner = THIS_MODULE,
};

static s32 es5340_init_data(struct ES5340_DRIVER_DATA *data,
							const struct regulation_constraints *constraints, u32 default_voltage)
{
	s32 ret = 0;
	struct device *dev = &data->client->dev;

	dev_info(dev,
			 "min_uV:%d,max_uV:%d,uV_offset:%d,min_uA:%d,max_uA:%d,"
			 "over_voltage_limits:%d,%d,%d\n",
			 constraints->min_uV, constraints->max_uV, constraints->uV_offset,
			 constraints->min_uA, constraints->max_uA,
			 constraints->over_voltage_limits.err,
			 constraints->over_voltage_limits.prot,
			 constraints->over_voltage_limits.warn);
	es5340_ext_ranges->min = constraints->min_uV;
	es5340_ext_ranges->min_sel = 0;
	es5340_ext_ranges->step = (ES5340_VOLT_DENOMINATOR / ES5340_VOLT_NUMERATOR)*1000;
	es5340_ext_ranges->max_sel = (constraints->max_uV - constraints->min_uV) / es5340_ext_ranges->step + 1;
	es5340_regulator_desc.n_voltages = es5340_ext_ranges->max_sel;
	dev_dbg(dev,"min:%duV,max:%duV,step:%duV,max_sel:%d\n", es5340_ext_ranges->min, constraints->max_uV, es5340_ext_ranges->step, es5340_ext_ranges->max_sel);

	es5340_set_vout(data, default_voltage / 1000);
	return ret;
}

static s32 es5340_probe(struct i2c_client *client)
{
	struct ES5340_DRIVER_DATA *data = NULL;
	s32 ret = 0;
	s32 regulator_cnt = 0;
	u32 default_voltage = 0;
	struct device *hwmon_dev;
	struct regulator_config config = {};
	struct device *dev = &client->dev;
	struct device_node *np, *parent;
	const char *output_names[4];

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
	{
		dev_err(dev, "not support smbus\n");
		return -EIO;
	}
	data = devm_kzalloc(dev, sizeof(struct ES5340_DRIVER_DATA), GFP_KERNEL);
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
	of_property_read_string_array(np, "eswin,regulator_label", output_names, 4);
	if (NULL != output_names[0])
	{
		strcpy(data->es5340_label[0], output_names[0]);
	}
	if (NULL != output_names[1])
	{
		strcpy(data->es5340_label[1], output_names[1]);
	}
	if (NULL != output_names[2])
	{
		strcpy(data->es5340_label[2], output_names[2]);
	}
	if (NULL != output_names[3])
	{
		strcpy(data->es5340_label[3], output_names[3]);
	}

	dev_dbg(dev, "default_voltage:%u,%s,%s,%s,%s\n", default_voltage, data->es5340_label[0],
			data->es5340_label[1], data->es5340_label[2], data->es5340_label[3]);
	/* fill isl6271a_matches array */
	regulator_cnt = of_regulator_match(dev, parent, es5340_matches, ARRAY_SIZE(es5340_matches));
	of_node_put(parent);
	if (regulator_cnt != 1)
	{
		dev_err(dev, "Error parsing regulator init data: %d\n", regulator_cnt);
		return regulator_cnt;
	}

	/* Fetched from device tree */
	config.init_data = es5340_matches[0].init_data;
	config.dev = dev;
	config.of_node = es5340_matches[0].of_node;
	/* config.ena_gpio = -EINVAL; */
	ret = es5340_init_data(data, &config.init_data->constraints, default_voltage);
	if (0 != ret)
	{
		dev_err(dev, "init es5340 error\n");
		return -EIO;
	}
	data->rdev = devm_regulator_register(dev, &es5340_regulator_desc, &config);
	if (IS_ERR(data->rdev))
	{
		dev_err(dev, "failed to register %s\n", es5340_regulator_desc.name);
	}
	hwmon_dev = devm_hwmon_device_register_with_info(
		dev, client->name, data, &es5340_chip_info, es5340_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_dbg(dev, "es5340_probe\n");

	return 0;
}

static void es5340_remove(struct i2c_client *client)
{
	dev_dbg(&client->dev, "es5340_remove\n");
}

static s32 es5340_detect(struct i2c_client *client,
						 struct i2c_board_info *info)
{
	dev_dbg(&client->dev, "es5340_detect\n");
	return 0;
}

static const struct i2c_device_id es5340_id[] = {{"es5340", 0}, {}};
MODULE_DEVICE_TABLE(i2c, es5340_id);

/* Addresses to scan */
static const unsigned short normal_i2c[] = {0x2c, 0x2d, 0x2e, 0x60,
											I2C_CLIENT_END};

static struct i2c_driver es5340_driver = {
	.class = I2C_CLASS_HWMON,
	.driver =
		{
			.name = "es5340",
		},
	.probe = es5340_probe,
	.remove = es5340_remove,
	.id_table = es5340_id,
	.detect = es5340_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(es5340_driver);

MODULE_AUTHOR("Yang Wei <yangwei1@eswincomputing.com>");
MODULE_DESCRIPTION("es5340 driver");
MODULE_LICENSE("GPL");