
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
 * Authors: Wang Jinlong <wangjinlong@eswincomputing.com>
 */
#include <linux/debugfs.h>
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

#define es501x_VOLT_DENOMINATOR 604
#define es501x_VOLT_NUMERATOR 60
#define es501x_INIT_VOLT 800

#define es501x_CMD_OPERATION 0x1
#define es501x_CMD_VOUT_COMMAND 0x21
#define es501x_CMD_VOUT_OV_WARN_LIMIT 0x42
#define es501x_CMD_IOUT_OC_WARN_LIMIT 0x4A
#define es501x_CMD_OT_WARN_LIMIT 0x51
#define es501x_CMD_VIN_OV_WARN_LIMIT 0x57
#define es501x_CMD_STATUS_BYTE 0x78
#define es501x_CMD_STATUS_WORD 0x79
#define es501x_CMD_READ_VIN 0x88
#define es501x_CMD_READ_VOUT 0x8B
#define es501x_CMD_READ_IOUT 0x8C
#define es501x_CMD_READ_TEMPERATURE 0x8D

#define es501x_LABEL_CNT 2

#define ES501X_SWEN_BIT           (1 << 5)    // BIT5: DC/DC switch
/* ES501x register definition - according to the VSET table */
#define ES501X_VSET_REG          0x00    /* Output voltage setting register */
#define ES501X_CONTROL1_REG      0x01    /* Control Register 1 - Includes DC/DC Switch Bit */
#define ES501X_CONTROL2_REG      0x02    /* Control Register 2 - Includes VRANGE bit */
#define ES501X_VRANGE_SHIFT      2
#define ES501X_VRANGE_MASK      (0x3 << ES501X_VRANGE_SHIFT)  /* VRANGE bit mask */
#define ES501X_STATUS_REG        0x02    /* Status register */
#define ES501X_ID_REG            0x0F    /* Chip ID Register */

/* VRANGE mode definition - according to the vrange table */
enum es501x_vrange {
    VRANGE_00 = 0,  /* VOUT = 400mV + VSET × 1.25mV */
    VRANGE_01 = 1,  /* VOUT = 400mV + VSET × 2.5mV */
    VRANGE_10 = 2,  /* VOUT = 400mV + VSET × 5mV */
    VRANGE_11 = 3   /* VOUT = 800mV + VSET × 10mV */
};

/* Voltage configuration structure - based on VSET table formula */
struct es501x_voltage_config {
    unsigned int base_uv;         /* Base voltage (400mV or 800mV) */
    unsigned int step_uv;         /* Step voltage (1.25mV, 2.5mV, 5mV, 10mV) */
    unsigned int max_vset;        /* Maximum VSET value (255) */
    const char *description;      /* Pattern description */
};

static const struct es501x_voltage_config es5035_voltage_cfg[] = {
    [VRANGE_00] = {400000, 1250,  255, "400mV + VSET × 1.25mV"},
    [VRANGE_01] = {400000, 2500,  255, "400mV + VSET × 2.5mV"},
    [VRANGE_10] = {400000, 5000,  255, "400mV + VSET × 5mV"},
    [VRANGE_11] = {400000, 5000,  255, "400mV + VSET × 5mV"},
};

static const struct es501x_voltage_config es501x_voltage_cfg[] = {
    [VRANGE_00] = {400000, 1250,  255, "400mV + VSET × 1.25mV"},
    [VRANGE_01] = {400000, 2500,  255, "400mV + VSET × 2.5mV"},
    [VRANGE_10] = {400000, 5000,  255, "400mV + VSET × 5mV"},
    [VRANGE_11] = {800000, 10000, 255, "800mV + VSET × 10mV"},
};

enum es501x_chip_type {
    ES501X,
    ES5035,
};

struct es501x_DRIVER_DATA {
	struct device *dev;
	struct i2c_client *client;
	struct regulator_dev *rdev;
	struct mutex config_lock;

	/* from DT */
	const struct regulator_init_data *init_data;
	const struct regulation_constraints *constraints;

	/* per-device voltage description */
	struct linear_range ranges[1];
	struct regulator_desc desc;

    u32 default_uV;
    bool init_voltage_applied;
    enum es501x_chip_type chip_type;
    enum es501x_vrange vrange;
    const struct es501x_voltage_config *vconfig;

	char es501x_label[es501x_LABEL_CNT][20];
#ifdef CONFIG_DEBUG_FS
	struct dentry *debug_root;
	u32 force_uV;
#endif
};

#define es501x_MASK_OPERATION_ENABLE 0X80

#define es501x_MASK_OV_VOLT 0x3FFF
#define es501x_MASK_VOUT_VALUE 0xFF
#define es501x_MASK_IOUT 0x3FF
#define es501x_MASK_TOUT 0xFF
#define es501x_VOLTE_IN_SENSE_LSB 8
#define es501x_CURRENT_LSB 31
#define es501x_TEMPERATURE_LSB 1000 /*1mC*/

static u32 es501x_get_vout(struct es501x_DRIVER_DATA *data);
static int es501x_regulator_enable(struct regulator_dev *rdev);
static int es501x_regulator_disable(struct regulator_dev *rdev);
static int es501x_regulator_is_enabled(struct regulator_dev *rdev);
/* Set VRANGE mode */
static int es501x_set_vrange(struct i2c_client *client, enum es501x_vrange vrange)
{
    int ret, current_ctrl2;
    struct es501x_DRIVER_DATA *data = i2c_get_clientdata(client);
    /* Read the current value of the CONTROL2 register */
    current_ctrl2 = i2c_smbus_read_byte_data(client, ES501X_CONTROL2_REG);
    if (current_ctrl2 < 0) {
        dev_err(&client->dev, "Failed to read CONTROL2 register\n");
        return current_ctrl2;
    }
    u8 value = (u8)current_ctrl2;
    dev_dbg(&client->dev, "Eswin:%s read CONTROL2 value=%d\n",__func__, value);
    /* Clear the VRANGE bit and set a new value */
    current_ctrl2 &= ~ES501X_VRANGE_MASK;
    current_ctrl2 |= (vrange << ES501X_VRANGE_SHIFT);

    ret = i2c_smbus_write_byte_data(client, ES501X_CONTROL2_REG, current_ctrl2);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to set VRANGE mode\n");
        return ret;
    }

    dev_dbg(&client->dev, "VRANGE mode set to %d: %s\n", 
             vrange, data->vconfig->description);

    /* Read the current value of the CONTROL2 register */
    current_ctrl2 = i2c_smbus_read_byte_data(client, ES501X_CONTROL2_REG);
    if (current_ctrl2 < 0) {
        dev_err(&client->dev, "Failed to read CONTROL2 register\n");
        return current_ctrl2;
    }
    value = (u8)current_ctrl2;
    dev_dbg(&client->dev, "Eswin:%s read CONTROL2 value=%d\n",__func__, value);
    return 0;
}

static struct of_regulator_match es501x_matches[] = {
    { .name = "vdd_soc_cpu" },
    { .name = "npu_svcc"   },
};

static inline s32 es501x_str2ul(const char *buf, u32 *value)
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

static const struct hwmon_channel_info *es501x_info[] = {
	HWMON_CHANNEL_INFO(in,  // Voltage monitor
        HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_ENABLE),
	NULL
};

static umode_t es501x_is_visible(const void *_data,
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
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int es501x_read(struct device *dev, enum hwmon_sensor_types type,
					   u32 attr, int channel, long *val)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct es501x_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 get_value = 0;

	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_input:
			if (channel == 0)
			{
				get_value = es501x_get_vout(data);
				*val = get_value;
			}else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int es501x_read_string(struct device *dev,
							  enum hwmon_sensor_types type,
							  u32 attr, int channel, const char **str)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct es501x_DRIVER_DATA *data = i2c_get_clientdata(client);
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_label:
			if (channel == 0)
			{
				*str = data->es501x_label[1];
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int es501x_write(struct device *dev, enum hwmon_sensor_types type,
						u32 attr, int channel, long val)
{
	//struct i2c_client *client = to_i2c_client(dev);
	//struct es501x_DRIVER_DATA *data = i2c_get_clientdata(client);
	int ret = 0;
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_enable:
			if (val != 0) es501x_regulator_enable(dev_get_drvdata(dev));
			else es501x_regulator_disable(dev_get_drvdata(dev));
			break;
		break;
		}
	default:
		break;
	}
	return ret;
}

static const struct hwmon_ops pac193x_hwmon_ops = {
	.is_visible = es501x_is_visible,
	.read = es501x_read,
	.write = es501x_write,
	.read_string = es501x_read_string,
};

static struct hwmon_chip_info es501x_chip_info = {
	.ops = &pac193x_hwmon_ops,
	.info = es501x_info,

};

static ssize_t es501x_vout_show(struct device *d,
								struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct es501x_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%u", es501x_get_vout(data));
}
static ssize_t es501x_vout_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	dev_err(dev, "Eswin:%s todo\n", __func__);
	return count;
}

DEVICE_ATTR(es501x_vout, 0600,
            es501x_vout_show,
            es501x_vout_store);


static struct attribute *es501x_attrs[] = {
	&dev_attr_es501x_vout.attr,
	NULL};

ATTRIBUTE_GROUPS(es501x);

static u32 es501x_reg2volt(struct es501x_DRIVER_DATA *data, u8 vset)
{
    const struct es501x_voltage_config *cfg = data->vconfig;
    return cfg->base_uv + vset * cfg->step_uv;
}

static u32 es501x_get_vout(struct es501x_DRIVER_DATA *data)
{
	u32 vset_value = i2c_smbus_read_byte_data(data->client, ES501X_VSET_REG);
	return es501x_reg2volt(data, (u8)vset_value);
}

/* Read the current VSET value */
static int es501x_get_vset(struct i2c_client *client)
{
    int vset;

    vset = i2c_smbus_read_byte_data(client, ES501X_VSET_REG);
    if (vset < 0) {
        dev_err(&client->dev, "Failed to read VSET register\n");
        return vset;
    }

    return vset;
}

/* Regulator operation function */
static int es501x_get_voltage_sel(struct regulator_dev *rdev)
{
    struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	dev_dbg(&rdev->dev, "%s\n", __FUNCTION__);
    return es501x_get_vset(client);
}

static int es501x_set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
    int ret;
    struct i2c_client *client = to_i2c_client(rdev->dev.parent);
    dev_dbg(&rdev->dev, "%s selector=%d\n", __FUNCTION__, selector);
    ret = es501x_regulator_is_enabled(rdev);
    if (ret == 0) {
        dev_err(&rdev->dev, "%s regulator is disabled\n", __FUNCTION__);
    }
    ret = i2c_smbus_write_byte_data(client, ES501X_VSET_REG, selector);
    if (ret < 0)
        return ret;
    return 0;
}

static int es501x_regulator_initvolt(struct regulator_dev *rdev)
{
    struct es501x_DRIVER_DATA *data = rdev_get_drvdata(rdev);
    u32 init_uV;
    int sel, ret = 0;

    mutex_lock(&data->config_lock);

    if (data->init_voltage_applied)
        goto out;

    if (data->default_uV > 0)
        init_uV = data->default_uV;
    else
        goto out;

    sel = regulator_map_voltage_linear_range(rdev, init_uV, init_uV);
    if (sel < 0) {
        ret = sel;
        goto out;
    }

    ret = es501x_set_voltage_sel(rdev, sel);
    if (!ret)
        data->init_voltage_applied = true;

out:
    mutex_unlock(&data->config_lock);
    return ret;
}

static int es501x_regulator_enable(struct regulator_dev *rdev)
{
    int ret;
    struct i2c_client *client = to_i2c_client(rdev->dev.parent);

    /* Read the current value of the CONTROL1 register */
    int current_ctrl1 = i2c_smbus_read_byte_data(client, ES501X_CONTROL1_REG);
    if (current_ctrl1 < 0) {
        dev_err(&rdev->dev, "Failed to read CONTROL1 register\n");
    }
    u8 value = (u8)current_ctrl1;
    dev_dbg(&rdev->dev, "Eswin:%s read CONTROL1 value=%d\n",__func__, value);

    /* Set SWEN=1 (enable DC/DC switch) */
    value |= ES501X_SWEN_BIT;
    ret = i2c_smbus_write_byte_data(client, ES501X_CONTROL1_REG, value);
    if (ret < 0) {
        dev_err(&rdev->dev, "Failed to enable regulator (SWEN=1)\n");
        return ret;
    }

    dev_dbg(&rdev->dev, "Regulator enabled (SWEN=1)\n");
    return 0;
}

static int es501x_regulator_disable(struct regulator_dev *rdev)
{
    int ret;
    struct i2c_client *client = to_i2c_client(rdev->dev.parent);

    /* Read the current value of the CONTROL1 register */
    int current_ctrl1 = i2c_smbus_read_byte_data(client, ES501X_CONTROL1_REG);
    if (current_ctrl1 < 0) {
        dev_err(&rdev->dev, "Failed to read CONTROL1 register\n");
    }
    u8 value = (u8)current_ctrl1;
    dev_dbg(&rdev->dev, "Eswin:%s read CONTROL1 value=%d\n", __func__, value);

    /* Set SWEN=0 (disable DC/DC switch) */
    value &= ~ES501X_SWEN_BIT;

    ret = i2c_smbus_write_byte_data(client, ES501X_CONTROL1_REG, value);
    if (ret < 0) {
        dev_err(&rdev->dev, "Failed to disable regulator (SWEN=0)\n");
        return ret;
    }

    dev_dbg(&rdev->dev, "Regulator disabled (SWEN=0)\n");
    return 0;
}

static int es501x_regulator_is_enabled(struct regulator_dev *rdev)
{
    struct i2c_client *client = to_i2c_client(rdev->dev.parent);
    int reg_val;

    /* Read the CONTROL1 register and check the status of the SWEN bit */
    reg_val = i2c_smbus_read_byte_data(client, ES501X_CONTROL1_REG);
    if (reg_val < 0) {
        dev_err(&rdev->dev, "Failed to read CONTROL1 register\n");
        return reg_val;
    }
    dev_dbg(&rdev->dev, "Eswin:%s reg_val=%d\n", __func__, reg_val);
    return !!(reg_val & ES501X_SWEN_BIT);  // Return 1 (enabled) or 0 (disabled)
}

static struct regulator_ops es501x_core_ops = {
    .list_voltage = regulator_list_voltage_linear_range,
    .map_voltage = regulator_map_voltage_linear_range,

    .set_voltage_sel = es501x_set_voltage_sel,
    .get_voltage_sel = es501x_get_voltage_sel,

    /* enable/disable regulator */
    .enable = es501x_regulator_enable,
    .disable = es501x_regulator_disable,
    .is_enabled = es501x_regulator_is_enabled,
};

static const struct regulator_desc es501x_regulator_desc_template = {
    .type = REGULATOR_VOLTAGE,
    .ops = &es501x_core_ops,
    .owner = THIS_MODULE,
};

static s32 es501x_init_data(struct es501x_DRIVER_DATA *data,
							const struct regulation_constraints *constraints)
{
    s32 ret = 0;
    struct device *dev = &data->client->dev;

    dev_info(dev, "input_uV :%d,min_uV:%d,max_uV:%d,uV_offset:%d\n",
        constraints->input_uV, constraints->min_uV, constraints->max_uV, constraints->uV_offset);
    const struct es501x_voltage_config *cfg = data->vconfig;
    //unsigned int min_uv = max(constraints->min_uV, cfg->base_uv);
    unsigned int min_uv = max_t(int, constraints->min_uV, cfg->base_uv);
    unsigned int min_sel = DIV_ROUND_UP(min_uv - cfg->base_uv, cfg->step_uv);
    unsigned int max_sel = (constraints->max_uV - cfg->base_uv) / cfg->step_uv;
    data->ranges[0].min = min_uv;
    data->ranges[0].min_sel = min_sel;
    data->ranges[0].step = cfg->step_uv; /* 10mV */
    data->ranges[0].max_sel = max_sel;
    dev_info(dev,"min:%duV,min_sel=%d step:%duV,max_sel:%d\n", data->ranges[0].min, min_sel, data->ranges[0].step, max_sel);
    return ret;
}

static const struct of_device_id es501x_of_match[] = {
	{
		.compatible = "einno,es501x",
		.data = (void *)ES501X,
	},
	{
		.compatible = "einno,es5035",
		.data = (void *)ES5035,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, es501x_of_match);

static int es501x_of_parse_cb(struct device_node *np,
                             const struct regulator_desc *desc,
                             struct regulator_config *config)
{
    const struct regulator_init_data *init_data = config->init_data;
    struct regulator_init_data *new_init_data;
    struct regulation_constraints *c;

    if (!init_data)
        return 0;

    c = devm_kzalloc(config->dev, sizeof(*c), GFP_KERNEL);
    if (!c)
        return -ENOMEM;

    *c = init_data->constraints;

    if (!c->min_uA && !c->max_uA) {
        c->min_uA = 0;
        c->max_uA = 0;
    }

    c->over_voltage_detection = false;
    c->under_voltage_detection = false;

    new_init_data = devm_kzalloc(config->dev, sizeof(*new_init_data), GFP_KERNEL);
    if (!new_init_data)
        return -ENOMEM;

    *new_init_data = *init_data;
    new_init_data->constraints = *c;

    config->init_data = new_init_data;

    return 0;
}

#ifdef CONFIG_DEBUG_FS
static int es501x_debug_set_voltage(struct es501x_DRIVER_DATA *data, u32 uV)
{
	struct regulator_dev *rdev = data->rdev;
	const struct es501x_voltage_config *cfg = data->vconfig;
	int sel, ret;
	u32 min_uV, max_uV;
	if (!cfg)
		return -EINVAL;
	u32 min_limit, max_limit;

	min_limit = (u32)data->constraints->min_uV;
	max_limit = (u32)data->constraints->max_uV;
	min_uV = max(cfg->base_uv, min_limit);
	max_uV = min(cfg->base_uv + cfg->step_uv * cfg->max_vset,
	     max_limit);

	if (uV < min_uV || uV > max_uV) {
		dev_warn(data->dev,
			 "debug voltage %u uV out of current VRANGE [%u, %u]\n",
			 uV, min_uV, max_uV);
		return -EINVAL;
	}

	sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	if (sel < 0)
		return sel;

	if (!es501x_regulator_is_enabled(rdev))
		dev_warn(data->dev,
			 "regulator disabled, voltage applies after enable\n");

	mutex_lock(&data->config_lock);
	ret = es501x_set_voltage_sel(rdev, sel);
	mutex_unlock(&data->config_lock);

	if (!ret)
		dev_info(data->dev,
			 "debug set voltage %u uV (sel=%d, description=%s)\n",
			 uV, sel, cfg->description);

	return ret;
}

static ssize_t es501x_force_voltage_write(struct file *file,
					  const char __user *buf,
					  size_t count,
					  loff_t *ppos)
{
	struct es501x_DRIVER_DATA *data = file->private_data;
	u32 uV;
	int ret;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtou32_from_user(buf, count, 0, &uV))
		return -EINVAL;

	ret = es501x_debug_set_voltage(data, uV);
	if (ret)
		dev_err(data->dev, "Eswin force voltage failed: %d\n", ret);

	return count;
}

static ssize_t es501x_force_voltage_read(struct file *file,
        char __user *buf, size_t count, loff_t *ppos)
{
	struct es501x_DRIVER_DATA *data = file->private_data;
	char tmp[32];
	int len;
	u32 uV;
	uV = es501x_get_vout(data);
	len = snprintf(tmp, sizeof(tmp), "%u\n", uV);
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static const struct file_operations es501x_force_voltage_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read  = es501x_force_voltage_read,
	.write = es501x_force_voltage_write,
};
#endif

static s32 es501x_probe(struct i2c_client *client)
{
    struct es501x_DRIVER_DATA *data = NULL;
    s32 ret = 0;
    s32 regulator_cnt = 0;
    u32 default_voltage = 0;
    struct device *hwmon_dev;
    struct regulator_config config = {};
    struct device *dev = &client->dev;
    struct device_node *np, *parent;
    const char *output_names[2];
	const struct of_device_id *match;
	enum es501x_chip_type chip;

    dev_dbg(dev, "Eswin %s Enter\n", __func__);

	match = i2c_of_match_device(es501x_of_match, client);
	if (!match)
		return -ENODEV;

	chip = (enum es501x_chip_type)match->data;
	dev_info(dev, "Detected %s\n", chip == ES5035 ? "ES5035" : "ES501X");

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
    {
        dev_err(dev, "not support smbus\n");
        return -EIO;
    }
    data = devm_kzalloc(dev, sizeof(struct es501x_DRIVER_DATA), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    mutex_init(&data->config_lock);
    data->client = client;
    data->dev = &client->dev;
	data->chip_type = chip;
    i2c_set_clientdata(client, data);
    /* Get the device (PMIC) node */
    //np = of_node_get(dev->of_node);
    np = dev->of_node;
    if (!np)
        return -EINVAL;

    data->vrange = VRANGE_10;
    switch (chip) {
    case ES5035:
        data->vconfig = &es5035_voltage_cfg[data->vrange];
        break;
    case ES501X:
    default:
        data->vconfig = &es501x_voltage_cfg[data->vrange];
        break;
	}
    //data->vconfig = &vrange_configs[data->vrange];
    /* Set VRANGE mode */
    ret = es501x_set_vrange(client, data->vrange);
    if (ret < 0) {
        return ret;
    }
    /* Get 'regulators' subnode */
    parent = of_get_child_by_name(np, "regulators");
    if (!parent)
    {
        dev_err(dev, "regulators node not found\n");
        return -EINVAL;
    }
    of_property_read_string_array(np, "eswin,regulator_label", output_names, 2);
    if (NULL != output_names[0])
    {
        strcpy(data->es501x_label[0], output_names[0]);
        dev_err(dev, "regulators label %s\n", output_names[0]);
	}
    if (NULL != output_names[1])
    {
        strcpy(data->es501x_label[1], output_names[1]);
        dev_err(dev, "regulators label %s\n", output_names[1]);
    }

    ret = of_property_read_u32(np, "eswin,regulator_default-microvolt", &default_voltage);
    if (ret)
    {
        dev_err(dev, "Eswin %s can`t find regulator_default-microvolt form dts, ret=%d\n", __func__, ret);
        return ret;
    }

	dev_dbg(dev, "Eswin %s default_voltage=%d\n", __func__, default_voltage);

    regulator_cnt = of_regulator_match(dev, parent, es501x_matches, ARRAY_SIZE(es501x_matches));
    of_node_put(parent);
    if (regulator_cnt <= 0)
    {
        dev_err(dev, "Eswin Error parsing regulator init data: %d\n", regulator_cnt);
        return regulator_cnt;
    }
    int i;
    for (i = 0; i < ARRAY_SIZE(es501x_matches); i++) {
        if (es501x_matches[i].of_node) {
            dev_info(dev, "matched es501x_matches[%d]: %s\n", i,  es501x_matches[i].name);
            break;
        } else {
            dev_dbg(dev, "unmatched es501x_matches[%d]: %s\n", i, es501x_matches[i].name);
        }
    }

    if (!es501x_matches[i].of_node || !es501x_matches[i].init_data) {
        dev_err(dev, "%s has no init_data, skip\n", es501x_matches[i].name);
        return -EINVAL;
    }

    /* Fetched from device tree */
    config.init_data = es501x_matches[i].init_data;
    config.dev = dev;
    config.of_node = es501x_matches[i].of_node;

    data->init_data  = config.init_data;
    data->constraints = &config.init_data->constraints;
    data->default_uV = default_voltage;
    data->init_voltage_applied = false;
    config.driver_data = data;

    ret = es501x_init_data(data, &config.init_data->constraints);
    //es501x_set_vout(data, default_voltage);
    data->desc = (struct regulator_desc) {
        .name = es501x_matches[i].name,
        .type = REGULATOR_VOLTAGE,
        .owner = THIS_MODULE,
        .ops = &es501x_core_ops,
        .linear_ranges = data->ranges,
        .n_linear_ranges = 1,
        .n_voltages = data->ranges[0].max_sel + 1,
		.of_parse_cb = es501x_of_parse_cb,
    };
    data->rdev = devm_regulator_register(dev, &data->desc, &config);
    if (IS_ERR(data->rdev))
    {
        dev_err(dev, "failed to register %s\n", data->desc.name);
		return IS_ERR(data->rdev);
    }
    ret = es501x_regulator_initvolt(data->rdev);
    if (ret) {
        dev_err(dev, "Error regulator enable failed, because init voltage failed ret=%d\n", ret);
        //return ret;
    }

    hwmon_dev = devm_hwmon_device_register_with_info(
        dev, client->name, data, &es501x_chip_info, es501x_groups);
    if (IS_ERR(hwmon_dev)) {
        dev_err(dev, "Eswin failed to register hwmon device %ld\n", PTR_ERR(hwmon_dev));
        return PTR_ERR(hwmon_dev);
    }

#ifdef CONFIG_DEBUG_FS
    char name[32];
    const char *bus_name = dev_name(&client->adapter->dev);
    if (chip == ES5035) {
        snprintf(name, sizeof(name), "es5035-%s-%02x", bus_name, client->addr);
    } else {
        snprintf(name, sizeof(name), "es501x-%s-%02x", bus_name, client->addr);
    }
    data->debug_root = debugfs_create_dir(name, NULL);
    if (data->debug_root) {
        debugfs_create_file("force_microvolt", 0644, data->debug_root,
							data, &es501x_force_voltage_fops);
    }
#endif

    dev_dbg(dev, "Eswin es501x_probe\n");
    return 0;
}

static void es501x_remove(struct i2c_client *client)
{
    dev_dbg(&client->dev, "es501x_remove\n");
}

static s32 es501x_detect(struct i2c_client *client,
						 struct i2c_board_info *info)
{
    dev_dbg(&client->dev, "es501x_detect\n");
    return 0;
}

static const struct i2c_device_id es501x_id[] = {{"es501x", 0}, {}};
MODULE_DEVICE_TABLE(i2c, es501x_id);

/* Addresses to scan */
static const unsigned short normal_i2c[] = {0x2c, 0x2d, 0x2e, 0x60,
											I2C_CLIENT_END};

static struct i2c_driver es501x_driver = {
    .class = I2C_CLASS_HWMON,
    .driver =
    {
        .name = "es501x",
		.of_match_table = es501x_of_match,
    },
    .probe = es501x_probe,
    .remove = es501x_remove,
    .id_table = es501x_id,
    .detect = es501x_detect,
    .address_list = normal_i2c,
};

module_i2c_driver(es501x_driver);

MODULE_AUTHOR("Wang Jinlong <wangjinlong@eswincomputing.com>");
MODULE_DESCRIPTION("es501x driver");
MODULE_LICENSE("GPL");
