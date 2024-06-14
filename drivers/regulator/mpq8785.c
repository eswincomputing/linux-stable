
// SPDX-License-Identifier: GPL-2.0
/*
 * eswin Specific Glue layer
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights
 * reserved. SPDX-License-Identifier: GPL-2.0
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

#define MPQ8785_CMD_PAGE 0x0
#define MPQ8785_CMD_OPERATION 0x1
#define MPQ8785_CMD_ON_OFF_CONFIG 0x2
#define MPQ8785_CMD_CLEAR_FAULT 0x3
#define MPQ8785_CMD_CLEAR_LAST_FAULT 0x8
#define MPQ8785_CMD_LAST_FAULT_RESTORE 0xc
#define MPQ8785_CMD_WRITE_PROTECTION 0x10
#define MPQ8785_CMD_STORE_ALL 0x15
#define MPQ8785_CMD_RESTORE_ALL 0x16
#define MPQ8785_CMD_CAPABILITY 0x19
#define MPQ8785_CMD_PMBUS_PS_NUM 0x1C
#define MPQ8785_CMD_VOUT_MODE 0x20
#define MPQ8785_CMD_VOUT_COMMAND 0x21
#define MPQ8785_CMD_VOUT_MAX 0x24
#define MPQ8785_CMD_VOUT_MARGIN_HIGH 0x25
#define MPQ8785_CMD_VOUT_MARGIN_LOW 0x26
#define MPQ8785_CMD_VOUT_SCALE_LOOP 0x29
#define MPQ8785_CMD_VOUT_MIN 0x2B
#define MPQ8785_CMD_COEFFICIENT 0x30
#define MPQ8785_CMD_VIN_ON 0x35
#define MPQ8785_CMD_VIN_OFF 0x36
#define MPQ8785_CMD_IOUT_CAL_GAIN 0x38
#define MPQ8785_CMD_IOUT_CAL_OFFSET 0x39
#define MPQ8785_CMD_IOUT_OC_FAULT_LIMIT 0x46
#define MPQ8785_CMD_IOUT_OC_WARN_LIMIT 0x4A
#define MPQ8785_CMD_VBOOT_SET_FOR_XOh_ADDR 0x4D
#define MPQ8785_CMD_OT_FAULT_LIMIT 0x4F
#define MPQ8785_CMD_OT_WARN_LIMIT 0x51
#define MPQ8785_CMD_VIN_OV_FAULT_LIMIT 0x55
#define MPQ8785_CMD_VIN_OV_WARN_LIMIT 0x57
#define MPQ8785_CMD_VBOOT_SET_FOR_X4h_ADDR 0x5E
#define MPQ8785_CMD_VBOOT_SET_FOR_X8h_ADDR 0x5F
#define MPQ8785_CMD_TON_DELAY 0x60
#define MPQ8785_CMD_TON_RISE 0x61
#define MPQ8785_CMD_TOFF_DELAY 0x64
#define MPQ8785_CMD_TOFF_FALL 0x65
#define MPQ8785_CMD_VBOOT_SET_FOR_XEh_ADDR 0x6A
#define MPQ8785_CMD_STATUS_WORD 0x79
#define MPQ8785_CMD_STATUS_VOUT 0x7A
#define MPQ8785_CMD_STATUS_IOUT 0x7B
#define MPQ8785_CMD_STATUS_INPUT 0x7C
#define MPQ8785_CMD_STATUS_TEMPERATURE 0x7D
#define MPQ8785_CMD_STATUS_CML 0x7E
#define MPQ8785_CMD_REV_ID 0x80
#define MPQ8785_CMD_READ_VIN 0x88
#define MPQ8785_CMD_READ_VOUT 0x8B
#define MPQ8785_CMD_READ_IOUT 0x8C
#define MPQ8785_CMD_READ_TEMPERATURE 0x8D
#define MPQ8785_CMD_PMBUS_REV_CONST 0x98
#define MPQ8785_CMD_MFR_ID 0x99
#define MPQ8785_CMD_MFR_REVISION 0x9B
#define MPQ8785_CMD_MFR_CONFIG_ID 0xC0
#define MPQ8785_CMD_MFR_CONFIG_CODE_REV 0xC1
#define MPQ8785_CMD_MFR_PRODUCT_REV_USER 0xC2
#define MPQ8785_CMD_MFR_SILICON_REV 0xC3
#define MPQ8785_CMD_MFR_APS_LEVEL 0xC5
#define MPQ8785_CMD_MFR_CONFIG_A 0xD0
#define MPQ8785_CMD_MFR_FS_CFG 0xD1
#define MPQ8785_CMD_MFR_ADDR_PMBUS 0xD2
#define MPQ8785_CMD_MFR_VOUT_RATE 0xD3
#define MPQ8785_CMD_MFR_PWM_TIME_CFG 0xD4
#define MPQ8785_CMD_MFR_PWM_TIME_CFG2 0xD5
#define MPQ8785_CMD_MFR_PHASE_BLANK_TIME 0xD6
#define MPQ8785_CMD_MFR_PHASE_SLOPE_BLANK_TIME 0xD7
#define MPQ8785_CMD_MFR_SLOPE_BLANK_TIME 0xD8
#define MPQ8785_CMD_MFR_BLANK_TIME_LV 0xD9
#define MPQ8785_CMD_MFR_SLOPE_CNT_DCM 0xDA
#define MPQ8785_CMD_MFR_SLOPE_SR_DCM 0xDB
#define MPQ8785_CMD_MFR_SW_BLOCK_LIMIT 0xDC
#define MPQ8785_CMD_MFR_VCOMP 0xDD
#define MPQ8785_CMD_MFR_DROOP_CFG 0xDE
#define MPQ8785_CMD_MFR_CONFIG_B 0xDF
#define MPQ8785_CMD_MFR_DC_LOOP_CTRL 0xE0
#define MPQ8785_CMD_MFR_CB_LOOP_CTRL 0xE1
#define MPQ8785_CMD_MFR_FS_LOOP_CTRL 0xE2
#define MPQ8785_CMD_MFR_VIN_CFG 0xE3
#define MPQ8785_CMD_MFR_VIN_SCALE 0xE4
#define MPQ8785_CMD_MFR_TEMP_TUNE 0xE5
#define MPQ8785_CMD_MFR_PROTECT_CFG 0xE6
#define MPQ8785_CMD_MFR_PROTECT_LEVEL 0xE7
#define MPQ8785_CMD_MFR_PRT_DELAY 0xE8
#define MPQ8785_CMD_SMBALERT_MASK 0xE9
#define MPQ8785_CMD_MFR_NOCP_OCP_SET 0xEA
#define MPQ8785_CMD_MFR_LEVEL_SEL2 0xEB
#define MPQ8785_CMD_MFR_PG_CFG 0xEC
#define MPQ8785_CMD_MFR_PS_CTRL 0xED
#define MPQ8785_CMD_MFR_PMBUS_LOCK 0xEE
#define MPQ8785_CMD_MFR_SET_SYNC_CFG 0xEF
#define MPQ8785_CMD_MFR_SLAVE_PROTECT 0xF0
#define MPQ8785_CMD_MFR_CTRL 0xF1
#define MPQ8785_CMD_MFR_AUTO_SLOPE_CFG 0xF2
#define MPQ8785_CMD_MFR_SLOPE_DELTA_LIMIT 0xF3
#define MPQ8785_CMD_MFR_RETRY_TIMES 0xF4
#define MPQ8785_CMD_MFR_CFG_EXT 0xF5
#define MPQ8785_CMD_MFR_CDROOP_SET 0xF6
#define MPQ8785_CMD_MFR_CFG_BACKUP 0xF7
#define MPQ8785_CMD_CHECK_SUM_FUNC 0xF8
#define MPQ8785_CMD_PROTECTION 0xFA
#define MPQ8785_CMD_PROTECTION_LAST 0xFB
#define MPQ8785_CMD_MFR_VBOOT_CFG 0xFC
#define MPQ8785_CMD_CLEAR_NVM_FAULT 0xFE

#define MPQ8785_PMBUS_EXTRA_READ_FLAG (1 << 7)
#define MPQ8785_LABEL_CNT 4

struct MPQ8785_DRIVER_DATA
{
	u32 volt_format;
	u32 volt_numerator;
	struct regulator_dev *rdev;
	struct regulator_desc *dev_desc;
	struct i2c_client *client;
	struct mutex config_lock;
	char mpq8785_label[MPQ8785_LABEL_CNT][16];
};

#define MPQ8785_MASK_OPERATION_ENABLE 0X80
#define MPQ8785_MASK_VIN_OV_FAULT 0x7f
#define MPQ8785_MASK_VOUT_LIMIT 0xFFF
#define MPQ8785_MASK_VOUT_VALUE 0xFFF
#define MPQ8785_MASK_IOUT_LIMIT 0x3FF
#define MPQ8785_MASK_TOUT_LIMIT 0xFF
#define MPQ8785_MASK_SW_FREQ_FREQ 0x1FF

// Voltage LSB= {125/64, 125/80, 125/80,
// 125/80}; 1.953125mV/1.5625mV/1.5625mV/1.5625mV
#define MPQ8785_VOLT_DENOMINATOR 125
#define MPQ8785_VOLT_DENOMINATOR_HALF (MPQ8785_VOLT_DENOMINATOR >> 1)
// Voltage_in LSB=200mV
#define MPQ8785_VOLT_IN_LSB 200
#define MPQ8785_VOLT_IN_LSB_HALF (MPQ8785_VOLT_IN_LSB >> 1)

// sense_Voltage_in LSB=25mV
#define MPQ8785_VOLTE_IN_SENSE_LSB 25

/* current*2 LSB=125mA */
#define MPQ8785_CURRENT_LSB_DENOMINATOR 125
#define MPQ8785_CURRENT_LSB_DENOMINATOR_HALF (MPQ8785_CURRENT_LSB_DENOMINATOR >> 1)
#define MPQ8785_CURRENT_LSB_NUMERATOR_BIT 1 // 2

#define MPQ8785_REGULATOT_CURRENT_LSB 1000000 /*1uA*/
#define MPQ8785_CURRENT_OUT_LSB 1000		  /*1mA*/

/*mini sw frequency is 300kHz, frequency lsb = 10kHz */
#define MPQ8785_FREQUENCY_LSB 10
#define MPQ8785_FREQUENCY_BASE_MINI 300 /* 300kHz=30*10kHz */
#define MPQ8785_FREQUENCY_BASE_MAX 2000
#define MPQ8785_TEMPERATURE_LSB 1000		  /*1mC*/

static u32 garr_volt_numerator[] = {64, 80, 80, 80};
static char garr_bool_string[][2] = {"N", "Y"};
static char garr_speed_string[][8] = {"100kHz", "400kHz", "1MHz", "resv"};

static struct of_regulator_match mpq8785_matches[] = {
	{
		.name = "npu_svcc",
	},
};

static inline u32 mpq8785_volt2reg(u32 vlot, u32 volt_numerator)
{
	u32 value = 0;

	value = (vlot * volt_numerator + MPQ8785_VOLT_DENOMINATOR_HALF) /
			MPQ8785_VOLT_DENOMINATOR;
	return value;
}

static inline u32 mpq8785_reg2volt(u32 value, u32 volt_numerator)
{
	u32 vlot = 0;

	vlot = value * MPQ8785_VOLT_DENOMINATOR / volt_numerator;

	return vlot;
}

static inline s32 mpq8785_str2ul(const char *buf, u32 *value)
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

static u8 mpq8785_read_byte(struct MPQ8785_DRIVER_DATA *data, u8 command)
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

static s32 mpq8785_write_byte(struct MPQ8785_DRIVER_DATA *data, u8 command,
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

static s32 mpq8785_update_byte(struct MPQ8785_DRIVER_DATA *data, u8 command,
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
	old_value = mpq8785_read_byte(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return mpq8785_write_byte(data, command, new_value);
}

static u16 mpq8785_read_word(struct MPQ8785_DRIVER_DATA *data, u8 command)
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

static u16 mpq8785_read_mask_word(struct MPQ8785_DRIVER_DATA *data, u8 command,
								  u16 mask)
{
	u16 ret = mpq8785_read_word(data, command);
	return (ret & mask);
}

static s32 mpq8785_write_word(struct MPQ8785_DRIVER_DATA *data, u8 command,
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

static s32 mpq8785_update_word(struct MPQ8785_DRIVER_DATA *data, u8 command,
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
	old_value = mpq8785_read_word(data, command);
	new_value = ~mask & old_value;
	new_value = new_value | val;
	return mpq8785_write_word(data, command, new_value);
}

static int mpq8785_read_block(struct MPQ8785_DRIVER_DATA *data, u8 command,
							  u8 *buf, u8 len, bool extra_len)
{
	int ret = 0;
	int num = 0;

	if (extra_len)
	{
		len++;
	}
	mutex_lock(&data->config_lock);
	ret = i2c_smbus_read_i2c_block_data(data->client, command, len, buf);
	mutex_unlock(&data->config_lock);
	if (ret < 0)
	{
		dev_err(&data->client->dev, "get command:0x%x value error:%d\n", command,
				ret);
		return -EIO;
	}
	if (extra_len)
	{
		for (num = 0; num < (len - 1); num++)
		{
			buf[num] = buf[num + 1];
		}
		buf[num] = 0;
	}
	return 0;
}

static int mpq8785_get_enable(struct MPQ8785_DRIVER_DATA *data)
{
	u8 cache = 0;

	cache = mpq8785_read_byte(data, MPQ8785_CMD_OPERATION);

	return ((cache >> 7) & 0x1);
}

static const struct hwmon_channel_info *mpq8785_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MAX_ALARM | HWMON_I_LABEL,
					   HWMON_I_INPUT | HWMON_I_ENABLE | HWMON_I_MAX | HWMON_I_MAX_ALARM | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_CRIT | HWMON_C_CRIT_ALARM | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_LABEL),
	NULL};

static umode_t mpq8785_is_visible(const void *_data,
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
		case hwmon_in_min:
		case hwmon_in_max:
			return 0644;
		}

		break;
	case hwmon_curr:
		switch (attr)
		{
		case hwmon_curr_input:
		case hwmon_curr_crit_alarm:
		case hwmon_curr_label:
			return 0444;
		case hwmon_curr_crit:
		case hwmon_curr_max:
			return 0644;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_input:
		case hwmon_temp_label:
		case hwmon_temp_crit_alarm:
			return 0444;
		case hwmon_temp_max:
		case hwmon_temp_crit:
			return 0644;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int mpq8785_read(struct device *dev, enum hwmon_sensor_types type,
						u32 attr, int channel, long *val)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 get_value = 0;

	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_input:
			if (channel == 0)
			{
				get_value = mpq8785_read_word(data, MPQ8785_CMD_READ_VIN);
				*val = get_value * MPQ8785_VOLTE_IN_SENSE_LSB;
			}
			else if (channel == 1)
			{
				get_value = mpq8785_read_word(data, MPQ8785_CMD_READ_VOUT);
				*val = mpq8785_reg2volt(get_value, data->volt_numerator);
			}
			else
			{
				dev_err(dev, "not support channel%d\n", channel);
			}
			break;
		case hwmon_in_max_alarm:
			get_value = mpq8785_read_word(data, MPQ8785_CMD_STATUS_WORD);
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
			*val = mpq8785_get_enable(data);
			break;
		case hwmon_in_min:
			get_value = mpq8785_read_mask_word(data, MPQ8785_CMD_VOUT_MIN,
											   MPQ8785_MASK_VOUT_LIMIT);
			*val = mpq8785_reg2volt(get_value, data->volt_numerator);
			break;
		case hwmon_in_max:
			if (channel == 0)
			{
				get_value =
					mpq8785_read_mask_word(data, MPQ8785_CMD_VIN_OV_FAULT_LIMIT,
										   MPQ8785_MASK_VIN_OV_FAULT);
				*val = get_value * MPQ8785_VOLT_IN_LSB;
			}
			else if (channel == 1)
			{
				get_value = mpq8785_read_mask_word(data, MPQ8785_CMD_VOUT_MAX,
												   MPQ8785_MASK_VOUT_LIMIT);
				*val = mpq8785_reg2volt(get_value, data->volt_numerator);
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
			get_value = mpq8785_read_word(data, MPQ8785_CMD_READ_IOUT);
			*val = (get_value * MPQ8785_CURRENT_LSB_DENOMINATOR) >>
				   MPQ8785_CURRENT_LSB_NUMERATOR_BIT;
			break;
		case hwmon_curr_crit_alarm:
			get_value = mpq8785_read_word(data, MPQ8785_CMD_STATUS_WORD);
			if (channel == 0)
			{
				*val = ((get_value >> 4) & 0x1);
			}
			break;
		case hwmon_curr_crit:
			*val = mpq8785_read_mask_word(data, MPQ8785_CMD_IOUT_OC_FAULT_LIMIT,
										  MPQ8785_MASK_IOUT_LIMIT);

			break;
		case hwmon_curr_max:
			*val = mpq8785_read_mask_word(data, MPQ8785_CMD_IOUT_OC_WARN_LIMIT,
										  MPQ8785_MASK_IOUT_LIMIT);

			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_input:
			*val = mpq8785_read_byte(data, MPQ8785_CMD_READ_TEMPERATURE);
			*val *= MPQ8785_TEMPERATURE_LSB;
			break;
		case hwmon_temp_crit_alarm:
			get_value = mpq8785_read_word(data, MPQ8785_CMD_STATUS_WORD);
			if (channel == 0)
			{
				*val = ((get_value >> 2) & 0x1);
			}
			break;
		case hwmon_temp_max:
			*val = mpq8785_read_mask_word(data, MPQ8785_CMD_OT_WARN_LIMIT,
										  MPQ8785_MASK_TOUT_LIMIT);
			*val *= MPQ8785_TEMPERATURE_LSB;
			break;
		case hwmon_temp_crit:
			*val = mpq8785_read_mask_word(data, MPQ8785_CMD_OT_FAULT_LIMIT,
										  MPQ8785_MASK_TOUT_LIMIT);
			*val *= MPQ8785_TEMPERATURE_LSB;
			break;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int mpq8785_read_string(struct device *dev,
							   enum hwmon_sensor_types type,
							   u32 attr, int channel, const char **str)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_label:
			if (channel == 0)
			{
				*str = data->mpq8785_label[0];
			}
			else if (channel == 1)
			{
				*str = data->mpq8785_label[1];
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
			*str = data->mpq8785_label[2];
			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{

		case hwmon_temp_label:
			*str = data->mpq8785_label[3];
			break;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int mpq8785_write(struct device *dev, enum hwmon_sensor_types type,
						 u32 attr, int channel, long val)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u16 new_value = 0;

	int ret = 0;
	switch (type)
	{
	case hwmon_in:
		switch (attr)
		{
		case hwmon_in_enable:
			mpq8785_update_byte(data, MPQ8785_CMD_OPERATION,
								MPQ8785_MASK_OPERATION_ENABLE, (u8)(val << 7));
			break;
		case hwmon_in_min:
			new_value = mpq8785_volt2reg(val, data->volt_numerator);
			ret = mpq8785_update_word(data, MPQ8785_CMD_VOUT_MIN,
									  MPQ8785_MASK_VOUT_LIMIT, new_value);
			break;
		case hwmon_in_max:
			if (channel == 0)
			{
				new_value = (MPQ8785_VOLT_IN_LSB_HALF + val) / MPQ8785_VOLT_IN_LSB;
				ret = mpq8785_update_word(data, MPQ8785_CMD_VIN_OV_FAULT_LIMIT,
										  MPQ8785_MASK_VIN_OV_FAULT, new_value);
			}
			else if (channel == 1)
			{
				new_value = mpq8785_volt2reg(val, data->volt_numerator);
				ret = mpq8785_update_word(data, MPQ8785_CMD_VOUT_MAX,
										  MPQ8785_MASK_VOUT_LIMIT, new_value);
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
		case hwmon_curr_crit:
			ret = mpq8785_update_word(data, MPQ8785_CMD_IOUT_OC_FAULT_LIMIT,
									  MPQ8785_MASK_IOUT_LIMIT, (u16)val);
			break;
		case hwmon_curr_max:
			ret = mpq8785_update_word(data, MPQ8785_CMD_IOUT_OC_WARN_LIMIT,
									  MPQ8785_MASK_IOUT_LIMIT, (u16)val);
			break;
		}
		break;
	case hwmon_temp:
		switch (attr)
		{
		case hwmon_temp_max:
			ret = mpq8785_update_word(data, MPQ8785_CMD_OT_WARN_LIMIT,
									  MPQ8785_MASK_TOUT_LIMIT, (u16)(val / MPQ8785_TEMPERATURE_LSB));
			break;
		case hwmon_temp_crit:
			ret = mpq8785_update_word(data, MPQ8785_CMD_OT_FAULT_LIMIT,
									  MPQ8785_MASK_TOUT_LIMIT, (u16)(val / MPQ8785_TEMPERATURE_LSB));
			break;
		}
		break;

	default:
		break;
	}
	return ret;
}
static const struct hwmon_ops pac193x_hwmon_ops = {
	.is_visible = mpq8785_is_visible,
	.read = mpq8785_read,
	.write = mpq8785_write,
	.read_string = mpq8785_read_string,
};

static struct hwmon_chip_info mpq8785_chip_info = {
	.ops = &pac193x_hwmon_ops,
	.info = mpq8785_info,

};

static ssize_t mpq8785_status_show(struct device *d,
								   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 value = mpq8785_read_word(data, MPQ8785_CMD_STATUS_WORD);

	return sysfs_emit(
		buf,
		"An output voltage fault or warning has occurred:%s\n"
		"An output current or output power fault or warning has occurred:%s\n"
		"An input voltage, input current, or input power fault or warning has "
		"occurred:%s\n"
		"Power Good:%s\n"
		"Watch Dog overflow fault:%s\n"
		"PMBus BUSY:%s\n"
		"power OFF:%s\n"
		"An output over-voltage fault has occurred:%s\n"
		"An output over-current fault has occurred:%s\n"
		"An input under-voltage fault has occurred:%s\n"
		"A temperature fault or warning has occurred:%s\n"
		"A communications, memory or logic fault has occurred:%s\n"
		"DrMOS fault:%s\n",
		garr_bool_string[(value >> 15) & 0x1],
		garr_bool_string[(value >> 14) & 0x1],
		garr_bool_string[(value >> 13) & 0x1],
		garr_bool_string[(value >> 11) & 0x1],
		garr_bool_string[(value >> 8) & 0x1],
		garr_bool_string[(value >> 7) & 0x1],
		garr_bool_string[(value >> 6) & 0x1],
		garr_bool_string[(value >> 5) & 0x1],
		garr_bool_string[(value >> 4) & 0x1],
		garr_bool_string[(value >> 3) & 0x1],
		garr_bool_string[(value >> 2) & 0x1],
		garr_bool_string[(value >> 1) & 0x1],
		garr_bool_string[value & 0x1]);
}
DEVICE_ATTR(mpq8785_status, S_IRUGO, mpq8785_status_show, NULL);

static ssize_t mpq8785_cap_version_show(struct device *d,
										struct device_attribute *attr,
										char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u8 cap_value = mpq8785_read_byte(data, MPQ8785_CMD_CAPABILITY);
	u8 rev_id = mpq8785_read_byte(data, MPQ8785_CMD_REV_ID);
	u8 pmbus_rev = mpq8785_read_byte(data, MPQ8785_CMD_PMBUS_REV_CONST);
	u32 mfr_id = 0;
	u8 mfr_rev = mpq8785_read_byte(data, MPQ8785_CMD_MFR_REVISION);
	mpq8785_read_block(data, MPQ8785_CMD_MFR_ID, (u8 *)&mfr_id, 3, true);
	return sysfs_emit(buf,
					  "PEC_SUPPORT:%s\n"
					  "MAX_BUS_SPEED:%s\n"
					  "SMBALERT_SUPPORT:%s\n"
					  "AVSBUS_SUPPORT:%s\n"
					  "Silicon revision number:%x\n"
					  "pmbus_revision:1.%c\n"
					  "mfr_id:%c%c%c\n"
					  "mfr_revision:%x\n",
					  garr_bool_string[(cap_value >> 7) & 0x1],
					  garr_speed_string[(cap_value >> 5) & 0x3],
					  garr_bool_string[(cap_value >> 4) & 0x1],
					  garr_bool_string[(cap_value >> 2) & 0x1], rev_id,
					  pmbus_rev & 0xff, (mfr_id >> 16) & 0xff,
					  (mfr_id >> 8) & 0xff, mfr_id & 0xff, mfr_rev & 0xff);
}
DEVICE_ATTR(mpq8785_cap_verison, S_IRUGO, mpq8785_cap_version_show, NULL);

static u32 mpq8785_get_vout(struct MPQ8785_DRIVER_DATA *data)
{
	u32 get_value = mpq8785_read_mask_word(data, MPQ8785_CMD_VOUT_COMMAND,
										   MPQ8785_MASK_VOUT_VALUE);

	return mpq8785_reg2volt(get_value, data->volt_numerator);
}

static s32 mpq8785_set_vout(struct MPQ8785_DRIVER_DATA *data, u32 volt_mv)
{
	u16 new_value = mpq8785_volt2reg(volt_mv, data->volt_numerator);
	return mpq8785_update_word(data, MPQ8785_CMD_VOUT_COMMAND,
							   MPQ8785_MASK_VOUT_VALUE, (new_value));
}

static ssize_t mpq8785_vout_show(struct device *d,
								 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);

	return sysfs_emit(buf, "%u", mpq8785_get_vout(data));
}
static ssize_t mpq8785_vout_store(struct device *d,
								  struct device_attribute *attr,
								  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 volt_value = 0;
	int ret = 0;
	ret = mpq8785_str2ul(buf, &volt_value);

	if (ret)
	{
		return ret;
	}
	ret = mpq8785_set_vout(data, volt_value);
	if (0 != ret)
	{
		return ret;
	}
	return count;
}
DEVICE_ATTR(mpq8785_vout, 0600, mpq8785_vout_show, mpq8785_vout_store);

static ssize_t mpq8785_sw_freq_show(struct device *d,
									struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 get_value = mpq8785_read_mask_word(data, MPQ8785_CMD_MFR_FS_CFG,
										   MPQ8785_MASK_SW_FREQ_FREQ);

	return sysfs_emit(
		buf, "%u",
		get_value * MPQ8785_FREQUENCY_LSB + MPQ8785_FREQUENCY_BASE_MINI);
}
static ssize_t mpq8785_sw_freq_store(struct device *d,
									 struct device_attribute *attr,
									 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 new_value = 0;
	int ret = 0;

	ret = mpq8785_str2ul(buf, &new_value);

	if (ret)
	{
		return ret;
	}
	if ((new_value < MPQ8785_FREQUENCY_BASE_MINI) ||
		(new_value > MPQ8785_FREQUENCY_BASE_MAX))
	{
		return -EINVAL;
	}
	ret = mpq8785_update_word(
		data, MPQ8785_CMD_MFR_FS_CFG, MPQ8785_MASK_SW_FREQ_FREQ,
		((new_value - MPQ8785_FREQUENCY_BASE_MINI) / MPQ8785_FREQUENCY_LSB));
	return count;
}
DEVICE_ATTR(mpq8785_sw_freq, 0600, mpq8785_sw_freq_show, mpq8785_sw_freq_store);

static struct attribute *mp8785_attrs[] = {
	&dev_attr_mpq8785_status.attr, &dev_attr_mpq8785_cap_verison.attr,
	&dev_attr_mpq8785_vout.attr, &dev_attr_mpq8785_sw_freq.attr, NULL};

ATTRIBUTE_GROUPS(mp8785);

static struct linear_range mpq8785_ext_ranges[] = {
	/* REGULATOR_LINEAR_RANGE(700000, 0, 100, 15625), //=1.953125mV*1000*8  */
	REGULATOR_LINEAR_RANGE(600000, 0, 320, 3125), /* =1.5625mV*1000*2 */
};

/**
 * mpq8785_set_voltage_sel -  set_voltage_sel for users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 */
static s32 mpq8785_set_voltage_sel(struct regulator_dev *rdev,
								   unsigned selector)
{
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 new_value = 0;

	if (selector > mpq8785_ext_ranges->max_sel)
	{
		dev_err(dev, "selector:%u out of rang 0~%u\n", selector,
				mpq8785_ext_ranges->max_sel);
		return -EINVAL;
	}

	new_value = mpq8785_ext_ranges->min + mpq8785_ext_ranges->step * selector;

	dev_dbg(dev, "%s_volt:%duV,selector:%u,step:%u,min:%u\n", __FUNCTION__,
			new_value, selector, mpq8785_ext_ranges->step,
			mpq8785_ext_ranges->min);

	mpq8785_set_vout(data, new_value / 1000);

	return 0;
}

/**
 * mpq8785_get_voltage_sel -  get_voltage_sel for users
 *
 * @rdev: regulator to operate on
 */
static s32 mpq8785_get_voltage_sel(struct regulator_dev *rdev)
{
	s32 index = 0;
	struct device *dev = &rdev->dev;
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 volt_value = 0;
	u32 diff_volt = 0;

	volt_value = mpq8785_get_vout(data);
	volt_value *= 1000;

	if (volt_value >= mpq8785_ext_ranges->min)
	{
		diff_volt = volt_value - mpq8785_ext_ranges->min;
	}
	else
	{
		diff_volt = 0;
	}
	dev_dbg(dev, "%s_diff_volt:%duV,volt:%u,min:%u\n", __FUNCTION__, diff_volt,
			volt_value, mpq8785_ext_ranges->min);
	index = DIV_ROUND_CLOSEST(diff_volt, mpq8785_ext_ranges->step);
	if (index > mpq8785_ext_ranges->max_sel)
	{
		dev_err(dev, "volt:%duV out legal range\n", volt_value);
	}

	dev_dbg(dev, "%s_diff_volt:%duV,step:%d,index:%d\n", __FUNCTION__, diff_volt,
			mpq8785_ext_ranges->step, index);
	return index;
}
/**
 * mpq8785_set_current_limit- set_current_limit for users
 * @rdev: regulator to operate on
 * @min_uA: Lower bound for current limit
 * @max_uA: Upper bound for current limit
 */
static s32 mpq8785_set_current_limit(struct regulator_dev *rdev, s32 min_uA,
									 s32 max_uA)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u16 new_value = 0;

	new_value = max_uA / MPQ8785_REGULATOT_CURRENT_LSB;
	mpq8785_update_word(data, MPQ8785_CMD_IOUT_OC_FAULT_LIMIT,
						MPQ8785_MASK_IOUT_LIMIT, new_value);
	dev_dbg(&rdev->dev, "mpq8785_set_current_limit,min_uA:%d,max_uA:%d,now_A:%d\n", min_uA,
			max_uA, new_value);
	return 0;
}

static s32 mpq8785_get_current_limit(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	u32 get_value = mpq8785_read_mask_word(data, MPQ8785_CMD_IOUT_OC_FAULT_LIMIT,
										   MPQ8785_MASK_IOUT_LIMIT);
	get_value = get_value * MPQ8785_REGULATOT_CURRENT_LSB;
	dev_dbg(&rdev->dev, "mpq8785_get_current_limit_%duA\n", get_value);
	return get_value;
}

static s32 mpq8785_get_error_flags(struct regulator_dev *rdev, u32 *flags)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);

	*flags = mpq8785_read_word(data, MPQ8785_CMD_STATUS_WORD);

	dev_dbg(&rdev->dev, "mpq8785_get_error_flags_%u\n", *flags);
	return 0;
}

int mpq8785_regulator_enable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return mpq8785_update_byte(data, MPQ8785_CMD_OPERATION,
							   MPQ8785_MASK_OPERATION_ENABLE,
							   MPQ8785_MASK_OPERATION_ENABLE);
}

int mpq8785_regulator_disable(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return mpq8785_update_byte(data, MPQ8785_CMD_OPERATION,
							   MPQ8785_MASK_OPERATION_ENABLE, 0);
}
int mpq8785_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct i2c_client *client = to_i2c_client(rdev->dev.parent);
	struct MPQ8785_DRIVER_DATA *data = i2c_get_clientdata(client);
	dev_dbg(&rdev->dev, "%s.%d\n", __FUNCTION__, __LINE__);
	return mpq8785_get_enable(data);
}

static struct regulator_ops mpq8785_core_ops = {

	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,

	/* get/set regulator voltage */
	/* Only one of each(set_voltage&&set_voltage_sel) should be implemented */
	/* .set_voltage = mpq8785_set_voltage, */
	.set_voltage_sel = mpq8785_set_voltage_sel,

	/* Only one of each(get_voltage&&get_voltage_sel) should be implemented */
	/* .get_voltage=mpq8785_get_voltage, */
	.get_voltage_sel = mpq8785_get_voltage_sel,

	/* get/set regulator current  */
	.set_current_limit = mpq8785_set_current_limit,
	.get_current_limit = mpq8785_get_current_limit,

	/* enable/disable regulator */
	.enable = mpq8785_regulator_enable,
	.disable = mpq8785_regulator_disable,
	.is_enabled = mpq8785_regulator_is_enabled,

	.get_error_flags = mpq8785_get_error_flags,

};
static struct regulator_desc mpq8785_regulator_desc = {
	.name = "NPUVDD",
	.type = REGULATOR_VOLTAGE,
	.n_voltages = 321,
	.ops = &mpq8785_core_ops,
	.linear_ranges = mpq8785_ext_ranges,
	.n_linear_ranges = ARRAY_SIZE(mpq8785_ext_ranges),
	.owner = THIS_MODULE,
};

static s32 mpq8785_init_data(struct MPQ8785_DRIVER_DATA *data,
							 const struct regulation_constraints *constraints, u32 default_voltage)
{
	u8 value = 0;
	s32 ret = 0;
	u16 new_value = 0;
	struct device *dev = &data->client->dev;

	/*set voltage format to VID to get better step for regulator*/
	mpq8785_write_byte(data, MPQ8785_CMD_VOUT_MODE, 0x20);
	value = mpq8785_read_byte(data, MPQ8785_CMD_VOUT_MODE); /*format*/
	data->volt_format = (value >> 5) & 0x3;
	data->volt_numerator = garr_volt_numerator[data->volt_format];

	dev_info(dev,
			 "min_uV:%d,max_uV:%d,uV_offset:%d,min_uA:%d,max_uA:%d,"
			 "over_voltage_limits:%d,%d,%d\n",
			 constraints->min_uV, constraints->max_uV, constraints->uV_offset,
			 constraints->min_uA, constraints->max_uA,
			 constraints->over_voltage_limits.err,
			 constraints->over_voltage_limits.prot,
			 constraints->over_voltage_limits.warn);
	mpq8785_ext_ranges->min = constraints->min_uV;
	mpq8785_ext_ranges->min_sel = 0;
	mpq8785_ext_ranges->max_sel = (constraints->max_uV - constraints->min_uV) / mpq8785_ext_ranges->step + 1;
	mpq8785_regulator_desc.n_voltages = mpq8785_ext_ranges->max_sel;
	new_value = mpq8785_volt2reg(constraints->over_voltage_limits.prot / 1000,
								 data->volt_numerator);
	ret = mpq8785_update_word(data, MPQ8785_CMD_VOUT_MAX, MPQ8785_MASK_VOUT_LIMIT,
							  new_value);
	if (ret < 0)
	{
		dev_err(dev, "set vout limit error\n");
		return ret;
	}

	mpq8785_set_vout(data, default_voltage / 1000);
	return ret;
}

static s32 mpq8785_probe(struct i2c_client *client)
{
	struct MPQ8785_DRIVER_DATA *data = NULL;
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
	data = devm_kzalloc(dev, sizeof(struct MPQ8785_DRIVER_DATA), GFP_KERNEL);
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
		strcpy(data->mpq8785_label[0], output_names[0]);
	}
	if (NULL != output_names[1])
	{
		strcpy(data->mpq8785_label[1], output_names[1]);
	}
	if (NULL != output_names[2])
	{
		strcpy(data->mpq8785_label[2], output_names[2]);
	}
	if (NULL != output_names[3])
	{
		strcpy(data->mpq8785_label[3], output_names[3]);
	}

	dev_dbg(dev, "default_voltage:%u,%s,%s,%s,%s\n", default_voltage, data->mpq8785_label[0],
			data->mpq8785_label[1], data->mpq8785_label[2], data->mpq8785_label[3]);
	/* fill isl6271a_matches array */
	regulator_cnt = of_regulator_match(dev, parent, mpq8785_matches, ARRAY_SIZE(mpq8785_matches));
	of_node_put(parent);
	if (regulator_cnt != 1)
	{
		dev_err(dev, "Error parsing regulator init data: %d\n", regulator_cnt);
		return regulator_cnt;
	}

	/* Fetched from device tree */
	config.init_data = mpq8785_matches[0].init_data;
	config.dev = dev;
	config.of_node = mpq8785_matches[0].of_node;
	/* config.ena_gpio = -EINVAL; */
	ret = mpq8785_init_data(data, &config.init_data->constraints, default_voltage);
	if (0 != ret)
	{
		dev_err(dev, "init mpq8785 error\n");
		return -EIO;
	}
	data->rdev = devm_regulator_register(dev, &mpq8785_regulator_desc, &config);
	if (IS_ERR(data->rdev))
	{
		dev_err(dev, "failed to register %s\n", mpq8785_regulator_desc.name);
	}
	hwmon_dev = devm_hwmon_device_register_with_info(
		dev, client->name, data, &mpq8785_chip_info, mp8785_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_dbg(dev, "mpq8785_probe\n");

	return 0;
}

static void mpq8785_remove(struct i2c_client *client)
{
	dev_dbg(&client->dev, "mpq8785_remove\n");
}

static s32 mpq8785_detect(struct i2c_client *client,
						  struct i2c_board_info *info)
{
	dev_dbg(&client->dev, "mpq8785_detect\n");
	return 0;
}

static const struct i2c_device_id mpq8785_id[] = {{"mpq8785", 0}, {}};
MODULE_DEVICE_TABLE(i2c, mpq8785_id);

/* Addresses to scan */
static const unsigned short normal_i2c[] = {0x2c, 0x2d, 0x2e, 0x60,
											I2C_CLIENT_END};

static struct i2c_driver mpq8785_driver = {
	.class = I2C_CLASS_HWMON,
	.driver =
		{
			.name = "mpq8785",
		},
	.probe = mpq8785_probe,
	.remove = mpq8785_remove,
	.id_table = mpq8785_id,
	.detect = mpq8785_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(mpq8785_driver);

MODULE_AUTHOR("Yang Wei <yangwei1@eswincomputing.com>");
MODULE_DESCRIPTION("mpq8785 driver");
MODULE_LICENSE("GPL");