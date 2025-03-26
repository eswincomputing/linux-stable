// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gas Gauge driver for SBS Compliant Batteries
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>

#include <linux/notifier.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/of.h>
#include <linux/power/cros-sbs-battery.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <asm/unaligned.h>

#define DRV_NAME "cros-ec-bat"

enum {
	REG_MANUFACTURER_DATA,
	REG_BATTERY_MODE,
	REG_TEMPERATURE,
	REG_VOLTAGE,
	REG_CURRENT_NOW,
	REG_CURRENT_AVG,
	REG_MAX_ERR,
	REG_CAPACITY,
	REG_TIME_TO_EMPTY_NOW,
	REG_TIME_TO_EMPTY_AVG,
	REG_TIME_TO_FULL_AVG,
	REG_STATUS,
	REG_CAPACITY_LEVEL,
	REG_CYCLE_COUNT,
	REG_SERIAL_NUMBER,
	REG_REMAINING_CAPACITY,
	REG_REMAINING_CAPACITY_CHARGE,
	REG_FULL_CHARGE_CAPACITY,
	REG_FULL_CHARGE_CAPACITY_CHARGE,
	REG_DESIGN_CAPACITY,
	REG_DESIGN_CAPACITY_CHARGE,
	REG_DESIGN_VOLTAGE_MIN,
	REG_DESIGN_VOLTAGE_MAX,
	REG_CHEMISTRY,
	REG_MANUFACTURER,
	REG_MODEL_NAME,
	REG_CHARGE_CURRENT,
	REG_CHARGE_VOLTAGE,
};

#define REG_ADDR_SPEC_INFO		0x1A
#define SPEC_INFO_VERSION_MASK		GENMASK(7, 4)
#define SPEC_INFO_VERSION_SHIFT		4

#define CROS_SBS_VERSION_1_0			1
#define CROS_SBS_VERSION_1_1			2
#define CROS_SBS_VERSION_1_1_WITH_PEC	3

#define REG_ADDR_MANUFACTURE_DATE	0x1B

/* Battery Mode defines */
#define BATTERY_MODE_OFFSET		0x03
#define BATTERY_MODE_CAPACITY_MASK	BIT(15)
enum cros_sbs_capacity_mode {
	CAPACITY_MODE_AMPS = 0,
	CAPACITY_MODE_WATTS = BATTERY_MODE_CAPACITY_MASK
};
#define BATTERY_MODE_CHARGER_MASK	(1<<14)

/* manufacturer access defines */
#define MANUFACTURER_ACCESS_STATUS	0x0006
#define MANUFACTURER_ACCESS_SLEEP	0x0011

/* battery status value bits */
#define BATTERY_INITIALIZED		0x80
#define BATTERY_DISCHARGING		0x40
#define BATTERY_FULL_CHARGED		0x20
#define BATTERY_FULL_DISCHARGED		0x10

/* min_value and max_value are only valid for numerical data */
#define CROS_SBS_DATA(_psp, _addr, _min_value, _max_value) { \
	.psp = _psp, \
	.addr = _addr, \
	.min_value = _min_value, \
	.max_value = _max_value, \
}

static const struct chip_data {
	enum power_supply_property psp;
	u8 addr;
	int min_value;
	int max_value;
} cros_sbs_data[] = {
	[REG_MANUFACTURER_DATA] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_PRESENT, 0x00, 0, 65535),
	[REG_BATTERY_MODE] =
		CROS_SBS_DATA(-1, 0x03, 0, 65535),
	[REG_TEMPERATURE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_TEMP, 0x08, 0, 65535),
	[REG_VOLTAGE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_VOLTAGE_NOW, 0x09, 0, 65535),
	[REG_CURRENT_NOW] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CURRENT_NOW, 0x0A, -32768, 32767),
	[REG_CURRENT_AVG] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CURRENT_AVG, 0x0B, -32768, 32767),
	[REG_MAX_ERR] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN, 0x0c, 0, 100),
	[REG_CAPACITY] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CAPACITY, 0x0D, 0, 100),
	[REG_REMAINING_CAPACITY] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_ENERGY_NOW, 0x0F, 0, 65535),
	[REG_REMAINING_CAPACITY_CHARGE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CHARGE_NOW, 0x0F, 0, 65535),
	[REG_FULL_CHARGE_CAPACITY] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_ENERGY_FULL, 0x10, 0, 65535),
	[REG_FULL_CHARGE_CAPACITY_CHARGE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CHARGE_FULL, 0x10, 0, 65535),
	[REG_TIME_TO_EMPTY_NOW] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW, 0x11, 0, 65535),
	[REG_TIME_TO_EMPTY_AVG] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG, 0x12, 0, 65535),
	[REG_TIME_TO_FULL_AVG] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_TIME_TO_FULL_AVG, 0x13, 0, 65535),
	[REG_CHARGE_CURRENT] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, 0x14, 0, 65535),
	[REG_CHARGE_VOLTAGE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX, 0x15, 0, 65535),
	[REG_STATUS] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_STATUS, 0x16, 0, 65535),
	[REG_CAPACITY_LEVEL] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CAPACITY_LEVEL, 0x16, 0, 65535),
	[REG_CYCLE_COUNT] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CYCLE_COUNT, 0x17, 0, 65535),
	[REG_DESIGN_CAPACITY] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, 0x18, 0, 65535),
	[REG_DESIGN_CAPACITY_CHARGE] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, 0x18, 0, 65535),
	[REG_DESIGN_VOLTAGE_MIN] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN, 0x19, 0, 65535),
	[REG_DESIGN_VOLTAGE_MAX] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, 0x19, 0, 65535),
	[REG_SERIAL_NUMBER] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_SERIAL_NUMBER, 0x1C, 0, 65535),
	/* Properties of type `const char *' */
	[REG_MANUFACTURER] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_MANUFACTURER, 0x20, 0, 65535),
	[REG_MODEL_NAME] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_MODEL_NAME, 0x21, 0, 65535),
	[REG_CHEMISTRY] =
		CROS_SBS_DATA(POWER_SUPPLY_PROP_TECHNOLOGY, 0x22, 0, 65535)
};

static const enum power_supply_property cros_sbs_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_MANUFACTURE_YEAR,
	POWER_SUPPLY_PROP_MANUFACTURE_MONTH,
	POWER_SUPPLY_PROP_MANUFACTURE_DAY,
	/* Properties of type `const char *' */
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME
};

/* Supports special manufacturer commands from TI BQ20Z65 and BQ20Z75 IC. */
#define CROS_SBS_FLAGS_TI_BQ20ZX5		BIT(0)

static const enum power_supply_property string_properties[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

#define NR_STRING_BUFFERS	ARRAY_SIZE(string_properties)

struct cros_sbs_info {
	struct device *dev;
	struct cros_ec_dev *ec_dev;
	struct cros_ec_device *ec_device;
	struct notifier_block notifier;

	struct power_supply		*power_supply;
	bool				is_present;
	struct gpio_desc		*gpio_detect;
	bool				charger_broadcasts;
	int				last_state;
	int				poll_time;
	u32				i2c_retry_count;
	u32				poll_retry_count;
	struct delayed_work		work;
	struct mutex			mode_lock;
	u32				flags;
	int				technology;
	char				strings[NR_STRING_BUFFERS][I2C_SMBUS_BLOCK_MAX + 1];
};

static int cros_sbs_ec_command(const struct cros_sbs_info *chip,
			       unsigned int version, unsigned int command,
			       const void *outdata, unsigned int outsize,
			       void *indata, unsigned int insize)
{
	struct cros_ec_dev *ec_dev = chip->ec_dev;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(struct_size(msg, data, max(outsize, insize)), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = version;
	msg->command = ec_dev->cmd_offset + command;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, outdata, outsize);

	ret = cros_ec_cmd_xfer_status(chip->ec_device, msg);
	if (ret >= 0 && insize)
		memcpy(indata, msg->data, insize);

	kfree(msg);
	return ret;
}

static char *cros_sbs_get_string_buf(struct cros_sbs_info *chip,
				enum power_supply_property psp)
{
	int i = 0;

	for (i = 0; i < NR_STRING_BUFFERS; i++)
		if (string_properties[i] == psp)
			return chip->strings[i];

	return ERR_PTR(-EINVAL);
}

static void cros_sbs_invalidate_cached_props(struct cros_sbs_info *chip)
{
	int i = 0;

	chip->technology = -1;

	for (i = 0; i < NR_STRING_BUFFERS; i++)
		chip->strings[i][0] = 0;
}

static bool force_load;

static int cros_sbs_read_word_data(struct cros_sbs_info *chip, u8 address);
static int cros_sbs_write_word_data(struct cros_sbs_info *chip, u8 address, u16 value);

static void cros_sbs_disable_charger_broadcasts(struct cros_sbs_info *chip)
{
	int val = cros_sbs_read_word_data(chip, BATTERY_MODE_OFFSET);
	if (val < 0)
		goto exit;

	val |= BATTERY_MODE_CHARGER_MASK;

	val = cros_sbs_write_word_data(chip, BATTERY_MODE_OFFSET, val);

exit:
	if (val < 0)
		dev_err(chip->dev,
			"Failed to disable charger broadcasting: %d\n", val);
	else
		dev_dbg(chip->dev, "%s\n", __func__);
}

static int cros_sbs_update_presence(struct cros_sbs_info *chip, bool is_present)
{
	struct ec_params_sb_rd req;
	struct ec_response_sb_rd_word rsp;
	int retries = chip->i2c_retry_count;
	s32 ret = 0;
	u8 version;

	if (chip->is_present == is_present)
		return 0;

	if (!is_present) {
		chip->is_present = false;
		cros_sbs_invalidate_cached_props(chip);
		return 0;
	}

	/* Check if device supports packet error checking and use it */
	while (retries > 0) {
		req.reg = REG_ADDR_SPEC_INFO;
		ret = cros_sbs_ec_command(chip, 0, EC_CMD_SB_READ_WORD, &req,
					  sizeof(req), &rsp, sizeof(rsp));
		if (ret >= 0)
			break;

		/*
		 * Some batteries trigger the detection pin before the
		 * I2C bus is properly connected. This works around the
		 * issue.
		 */
		msleep(100);

		retries--;
	}

	if (ret < 0) {
		dev_dbg(chip->dev, "failed to read spec info: %d\n", ret);
		chip->is_present = true;

		return ret;
	}

	version = (rsp.value & SPEC_INFO_VERSION_MASK) >> SPEC_INFO_VERSION_SHIFT;
	dev_dbg(chip->dev, "version: %d\n", version);

	if (!chip->is_present && is_present && !chip->charger_broadcasts)
		cros_sbs_disable_charger_broadcasts(chip);

	chip->is_present = true;

	return 0;
}

static int cros_sbs_read_word_data(struct cros_sbs_info *chip, u8 address)
{
	struct ec_params_sb_rd req;
	struct ec_response_sb_rd_word rsp;
	int retries = chip->i2c_retry_count;
	s32 ret = 0;

	while (retries > 0) {
		req.reg = address;
		ret = cros_sbs_ec_command(chip, 0, EC_CMD_SB_READ_WORD, &req,
					  sizeof(req), &rsp, sizeof(rsp));
		if (ret >= 0)
			break;
		retries--;
	}

	if (ret < 0) {
		dev_dbg(chip->dev, "%s: i2c read at address 0x%x failed: %d\n",
			__func__, address, ret);
		return ret;
	}
	return rsp.value;
}

static int cros_sbs_read_string_data(struct cros_sbs_info *chip, u8 address, char *values)
{
	struct ec_params_sb_rd req;
	struct ec_response_sb_rd_block rsp_block;

	int retries = chip->i2c_retry_count;
	int ret = 0;

	while (retries > 0) {
		req.reg = address;
		ret = cros_sbs_ec_command(chip, 0, EC_CMD_SB_READ_BLOCK, &req,
				sizeof(req), &rsp_block, sizeof(rsp_block));
		if (ret >= 0)
			break;
		retries--;
	}

	if (ret < 0) {
		dev_dbg(chip->dev, "failed to read block 0x%x: %d\n", address, ret);
		return ret;
	}

	/* block_buffer[0] == block_length */
	ret = min(rsp_block.data[0], sizeof(rsp_block.data) - 1);
	memcpy(values, &rsp_block.data[1], ret);

	/* add string termination */
	values[ret] = '\0';
	return ret;
}

static int cros_sbs_write_word_data(struct cros_sbs_info *chip, u8 address,
	u16 value)
{
	struct ec_params_sb_wr_word req;
	int retries = chip->i2c_retry_count;
	s32 ret = 0;

	req.reg = address;
	req.value = value;

	while (retries > 0) {
		ret = cros_sbs_ec_command(chip, 0, EC_CMD_SB_WRITE_WORD, &req,
					  sizeof(req), NULL, 0);
		if (ret >= 0)
			break;
		retries--;
	}

	if (ret < 0) {
		dev_dbg(chip->dev,
			"%s: i2c write to address 0x%x failed: %d\n",
			__func__, address, ret);
		return ret;
	}

	return 0;
}

static int cros_sbs_status_correct(struct cros_sbs_info *chip, int *intval)
{
	int ret;

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_CURRENT_NOW].addr);
	if (ret < 0)
		return ret;

	ret = (s16)ret;

	/* Not drawing current -> not charging (i.e. idle) */
	if (*intval != POWER_SUPPLY_STATUS_FULL && ret == 0)
		*intval = POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (*intval == POWER_SUPPLY_STATUS_FULL) {
		/* Drawing or providing current when full */
		if (ret > 0)
			*intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ret < 0)
			*intval = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	return 0;
}

static bool cros_sbs_bat_needs_calibration(struct cros_sbs_info *chip)
{
	int ret;

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_BATTERY_MODE].addr);
	if (ret < 0)
		return false;

	return !!(ret & BIT(7));
}

static int cros_sbs_get_ti_battery_presence_and_health(
	struct cros_sbs_info *chip, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;

	/*
	 * Write to ManufacturerAccess with ManufacturerAccess command
	 * and then read the status.
	 */
	ret = cros_sbs_write_word_data(chip, cros_sbs_data[REG_MANUFACTURER_DATA].addr,
				  MANUFACTURER_ACCESS_STATUS);
	if (ret < 0) {
		if (psp == POWER_SUPPLY_PROP_PRESENT)
			val->intval = 0; /* battery removed */
		return ret;
	}

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_MANUFACTURER_DATA].addr);
	if (ret < 0) {
		if (psp == POWER_SUPPLY_PROP_PRESENT)
			val->intval = 0; /* battery removed */
		return ret;
	}

	if (ret < cros_sbs_data[REG_MANUFACTURER_DATA].min_value ||
	    ret > cros_sbs_data[REG_MANUFACTURER_DATA].max_value) {
		val->intval = 0;
		return 0;
	}

	/* Mask the upper nibble of 2nd byte and
	 * lower byte of response then
	 * shift the result by 8 to get status*/
	ret &= 0x0F00;
	ret >>= 8;
	if (psp == POWER_SUPPLY_PROP_PRESENT) {
		if (ret == 0x0F)
			/* battery removed */
			val->intval = 0;
		else
			val->intval = 1;
	} else if (psp == POWER_SUPPLY_PROP_HEALTH) {
		if (ret == 0x09)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (ret == 0x0B)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (ret == 0x0C)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (cros_sbs_bat_needs_calibration(chip))
			val->intval = POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
	}

	return 0;
}

static int cros_sbs_get_battery_presence_and_health(
	struct cros_sbs_info *chip, enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret;

	if (chip->flags & CROS_SBS_FLAGS_TI_BQ20ZX5)
		return cros_sbs_get_ti_battery_presence_and_health(chip, psp, val);

	/* Dummy command; if it succeeds, battery is present. */
	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_STATUS].addr);

	if (ret < 0) { /* battery not present*/
		if (psp == POWER_SUPPLY_PROP_PRESENT) {
			val->intval = 0;
			return 0;
		}
		return ret;
	}

	if (psp == POWER_SUPPLY_PROP_PRESENT)
		val->intval = 1; /* battery present */
	else { /* POWER_SUPPLY_PROP_HEALTH */
		if (cros_sbs_bat_needs_calibration(chip)) {
			val->intval = POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED;
		} else {
			/* SBS spec doesn't have a general health command. */
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		}
	}

	return 0;
}

static int cros_sbs_get_battery_property(struct cros_sbs_info *chip,
	int reg_offset, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[reg_offset].addr);
	if (ret < 0)
		return ret;

	/* returned values are 16 bit */
	if (cros_sbs_data[reg_offset].min_value < 0)
		ret = (s16)ret;

	if (ret >= cros_sbs_data[reg_offset].min_value &&
	    ret <= cros_sbs_data[reg_offset].max_value) {
		val->intval = ret;
		if (psp == POWER_SUPPLY_PROP_CAPACITY_LEVEL) {
			if (!(ret & BATTERY_INITIALIZED))
				val->intval =
					POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
			else if (ret & BATTERY_FULL_CHARGED)
				val->intval =
					POWER_SUPPLY_CAPACITY_LEVEL_FULL;
			else if (ret & BATTERY_FULL_DISCHARGED)
				val->intval =
					POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
			else
				val->intval =
					POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			return 0;
		} else if (psp != POWER_SUPPLY_PROP_STATUS) {
			return 0;
		}

		if (ret & BATTERY_FULL_CHARGED)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (ret & BATTERY_DISCHARGING)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;

		cros_sbs_status_correct(chip, &val->intval);

		if (chip->poll_time == 0)
			chip->last_state = val->intval;
		else if (chip->last_state != val->intval) {
			cancel_delayed_work_sync(&chip->work);
			power_supply_changed(chip->power_supply);
			chip->poll_time = 0;
		}
	} else {
		if (psp == POWER_SUPPLY_PROP_STATUS)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		else if (psp == POWER_SUPPLY_PROP_CAPACITY)
			/* sbs spec says that this can be >100 %
			 * even if max value is 100 %
			 */
			val->intval = min(ret, 100);
		else
			val->intval = 0;
	}

	return 0;
}

static int cros_sbs_get_property_index(struct cros_sbs_info *chip,
	enum power_supply_property psp)
{
	int count;

	for (count = 0; count < ARRAY_SIZE(cros_sbs_data); count++)
		if (psp == cros_sbs_data[count].psp)
			return count;

	dev_warn(chip->dev, 
		"%s: Invalid Property - %d\n", __func__, psp);

	return -EINVAL;
}

static const char *cros_sbs_get_constant_string(struct cros_sbs_info *chip,
			enum power_supply_property psp)
{
	int ret;
	char *buf;
	u8 addr;

	buf = cros_sbs_get_string_buf(chip, psp);
	if (IS_ERR(buf))
		return buf;

	if (!buf[0]) {
		ret = cros_sbs_get_property_index(chip, psp);
		if (ret < 0)
			return ERR_PTR(ret);

		addr = cros_sbs_data[ret].addr;

		ret = cros_sbs_read_string_data(chip, addr, buf);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	return buf;
}

static void cros_sbs_unit_adjustment(struct cros_sbs_info *chip,
	enum power_supply_property psp, union power_supply_propval *val)
{
#define BASE_UNIT_CONVERSION		1000
#define BATTERY_MODE_CAP_MULT_WATT	(10 * BASE_UNIT_CONVERSION)
#define TIME_UNIT_CONVERSION		60
#define TEMP_KELVIN_TO_CELSIUS		2731
	switch (psp) {
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		/* sbs provides energy in units of 10mWh.
		 * Convert to µWh
		 */
		val->intval *= BATTERY_MODE_CAP_MULT_WATT;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval *= BASE_UNIT_CONVERSION;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		/* sbs provides battery temperature in 0.1K
		 * so convert it to 0.1°C
		 */
		val->intval -= TEMP_KELVIN_TO_CELSIUS;
		break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		/* sbs provides time to empty and time to full in minutes.
		 * Convert to seconds
		 */
		val->intval *= TIME_UNIT_CONVERSION;
		break;

	default:
		dev_dbg(chip->dev,
			"%s: no need for unit conversion %d\n", __func__, psp);
	}
}

static enum cros_sbs_capacity_mode cros_sbs_set_capacity_mode(struct cros_sbs_info *chip,
	enum cros_sbs_capacity_mode mode)
{
	int ret, original_val;

	original_val = cros_sbs_read_word_data(chip, BATTERY_MODE_OFFSET);
	if (original_val < 0)
		return original_val;

	if ((original_val & BATTERY_MODE_CAPACITY_MASK) == mode)
		return mode;

	if (mode == CAPACITY_MODE_AMPS)
		ret = original_val & ~BATTERY_MODE_CAPACITY_MASK;
	else
		ret = original_val | BATTERY_MODE_CAPACITY_MASK;

	ret = cros_sbs_write_word_data(chip, BATTERY_MODE_OFFSET, ret);
	if (ret < 0)
		return ret;

	usleep_range(1000, 2000);

	return original_val & BATTERY_MODE_CAPACITY_MASK;
}

static int cros_sbs_get_battery_capacity(struct cros_sbs_info *chip,
	int reg_offset, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
	enum cros_sbs_capacity_mode mode = CAPACITY_MODE_WATTS;

	if (power_supply_is_amp_property(psp))
		mode = CAPACITY_MODE_AMPS;

	mode = cros_sbs_set_capacity_mode(chip, mode);
	if ((int)mode < 0)
		return mode;

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[reg_offset].addr);
	if (ret < 0)
		return ret;

	val->intval = ret;

	ret = cros_sbs_set_capacity_mode(chip, mode);
	if (ret < 0)
		return ret;

	return 0;
}

static char cros_sbs_serial[5];
static int cros_sbs_get_battery_serial_number(struct cros_sbs_info *chip,
	union power_supply_propval *val)
{
	int ret;

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_SERIAL_NUMBER].addr);
	if (ret < 0)
		return ret;

	sprintf(cros_sbs_serial, "%04x", ret);
	val->strval = cros_sbs_serial;

	return 0;
}

static int cros_sbs_get_chemistry(struct cros_sbs_info *chip,
		union power_supply_propval *val)
{
	const char *chemistry;

	if (chip->technology != -1) {
		val->intval = chip->technology;
		return 0;
	}

	chemistry = cros_sbs_get_constant_string(chip, POWER_SUPPLY_PROP_TECHNOLOGY);

	if (IS_ERR(chemistry))
		return PTR_ERR(chemistry);

	if (!strncasecmp(chemistry, "LION", 4))
		chip->technology = POWER_SUPPLY_TECHNOLOGY_LION;
	else if (!strncasecmp(chemistry, "LiP", 3))
		chip->technology = POWER_SUPPLY_TECHNOLOGY_LIPO;
	else if (!strncasecmp(chemistry, "NiCd", 4))
		chip->technology = POWER_SUPPLY_TECHNOLOGY_NiCd;
	else if (!strncasecmp(chemistry, "NiMH", 4))
		chip->technology = POWER_SUPPLY_TECHNOLOGY_NiMH;
	else
		chip->technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;

	if (chip->technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
		dev_warn(chip->dev, "Unknown chemistry: %s\n", chemistry);

	val->intval = chip->technology;

	return 0;
}

static int cros_sbs_get_battery_manufacture_date(struct cros_sbs_info *chip,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret;
	u16 day, month, year;

	ret = cros_sbs_read_word_data(chip, REG_ADDR_MANUFACTURE_DATE);
	if (ret < 0)
		return ret;

	day   = ret   & GENMASK(4,  0);
	month = (ret  & GENMASK(8,  5)) >> 5;
	year  = ((ret & GENMASK(15, 9)) >> 9) + 1980;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURE_YEAR:
		val->intval = year;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_MONTH:
		val->intval = month;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_DAY:
		val->intval = day;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cros_sbs_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct cros_sbs_info *chip = power_supply_get_drvdata(psy);
	const char *str;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_HEALTH:
		ret = cros_sbs_get_battery_presence_and_health(chip, psp, val);

		/* this can only be true if no gpio is used */
		if (psp == POWER_SUPPLY_PROP_PRESENT)
			return 0;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		ret = cros_sbs_get_chemistry(chip, val);
		if (ret < 0)
			break;

		goto done; /* don't trigger power_supply_changed()! */

	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = cros_sbs_get_property_index(chip, psp);
		if (ret < 0)
			break;

		/* cros_sbs_get_battery_capacity() will change the battery mode
		 * temporarily to read the requested attribute. Ensure we stay
		 * in the desired mode for the duration of the attribute read.
		 */
		mutex_lock(&chip->mode_lock);
		ret = cros_sbs_get_battery_capacity(chip, ret, psp, val);
		mutex_unlock(&chip->mode_lock);
		break;

	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = cros_sbs_get_battery_serial_number(chip, val);
		break;

	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN:
		ret = cros_sbs_get_property_index(chip, psp);
		if (ret < 0)
			break;

		ret = cros_sbs_get_battery_property(chip, ret, psp, val);
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
	case POWER_SUPPLY_PROP_MANUFACTURER:
		str = cros_sbs_get_constant_string(chip, psp);
		if (IS_ERR(str))
			ret = PTR_ERR(str);
		else
			val->strval = str;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURE_YEAR:
	case POWER_SUPPLY_PROP_MANUFACTURE_MONTH:
	case POWER_SUPPLY_PROP_MANUFACTURE_DAY:
		ret = cros_sbs_get_battery_manufacture_date(chip, psp, val);
		break;

	default:
		dev_err(chip->dev,
			"%s: INVALID property\n", __func__);
		return -EINVAL;
	}

	if (!chip->gpio_detect && chip->is_present != (ret >= 0)) {
		bool old_present = chip->is_present;
		union power_supply_propval val;
		int err = cros_sbs_get_battery_presence_and_health(
				chip, POWER_SUPPLY_PROP_PRESENT, &val);

		cros_sbs_update_presence(chip, !err && val.intval);

		if (old_present != chip->is_present)
			power_supply_changed(chip->power_supply);
	}

done:
	if (!ret) {
		/* Convert units to match requirements for power supply class */
		cros_sbs_unit_adjustment(chip, psp, val);
		dev_dbg(chip->dev,
			"%s: property = %d, value = %x\n", __func__,
			psp, val->intval);
	} else if (!chip->is_present)  {
		/* battery not present, so return NODATA for properties */
		ret = -ENODATA;
	}
	return ret;
}

static void cros_sbs_supply_changed(struct cros_sbs_info *chip)
{
	struct power_supply *battery = chip->power_supply;
	int ret;
	cros_sbs_update_presence(chip, ret);
	power_supply_changed(battery);
}

static void cros_sbs_external_power_changed(struct power_supply *psy)
{
	struct cros_sbs_info *chip = power_supply_get_drvdata(psy);

	/* cancel outstanding work */
	cancel_delayed_work_sync(&chip->work);

	schedule_delayed_work(&chip->work, HZ);
	chip->poll_time = chip->poll_retry_count;
}

static void cros_sbs_delayed_work(struct work_struct *work)
{
	struct cros_sbs_info *chip;
	s32 ret;

	chip = container_of(work, struct cros_sbs_info, work.work);

	ret = cros_sbs_read_word_data(chip, cros_sbs_data[REG_STATUS].addr);
	/* if the read failed, give up on this work */
	if (ret < 0) {
		chip->poll_time = 0;
		return;
	}

	if (ret & BATTERY_FULL_CHARGED)
		ret = POWER_SUPPLY_STATUS_FULL;
	else if (ret & BATTERY_DISCHARGING)
		ret = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		ret = POWER_SUPPLY_STATUS_CHARGING;

	cros_sbs_status_correct(chip, &ret);

	if (chip->last_state != ret) {
		chip->poll_time = 0;
		power_supply_changed(chip->power_supply);
		return;
	}
	if (chip->poll_time > 0) {
		schedule_delayed_work(&chip->work, HZ);
		chip->poll_time--;
		return;
	}
}

static const struct power_supply_desc cros_sbs_default_desc = {
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = cros_sbs_properties,
	.num_properties = ARRAY_SIZE(cros_sbs_properties),
	.get_property = cros_sbs_get_property,
	.external_power_changed = cros_sbs_external_power_changed,
};

static int cros_sbs_ec_notify(struct notifier_block *nb,
			  unsigned long queued_during_suspend, void *data)
{
	struct cros_ec_device *ec_dev = data;
	struct cros_sbs_info *chip = container_of(nb, struct cros_sbs_info, notifier);
	u32 host_event;
	u32 mask;

	if (ec_dev->event_size != sizeof(host_event))
		return NOTIFY_DONE;

	host_event = get_unaligned_le32(&ec_dev->event_data.data.host_event);

	if (ec_dev->event_data.event_type != EC_MKBP_EVENT_HOST_EVENT)
		return NOTIFY_DONE;

	mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |
	       EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED);
	if (host_event & mask) {
		dev_info(chip->dev, "%s: %s\n", __func__,
			 (host_event &
			  EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED)) ?
				 "AC_CONNECTED" :
				 "AC_DISCONNECTED");
		cros_sbs_supply_changed(chip);
	}

	return NOTIFY_DONE;
}

static int cros_sbs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *ec_device = ec_dev->ec_dev;

	struct cros_sbs_info *chip;
	struct power_supply_desc *cros_sbs_desc;
	struct cros_sbs_platform_data *pdata = pdev->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	int rc;

	cros_sbs_desc = devm_kmemdup(dev, &cros_sbs_default_desc,
				     sizeof(*cros_sbs_desc), GFP_KERNEL);
	if (!cros_sbs_desc)
		return -ENOMEM;

	cros_sbs_desc->name = devm_kasprintf(dev, GFP_KERNEL, "%s", dev_name(dev));
	if (!cros_sbs_desc->name)
		return -ENOMEM;

	chip = devm_kzalloc(dev, sizeof(struct cros_sbs_info), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->ec_dev = ec_dev;
	chip->ec_device = ec_device;
	chip->flags = (u32)(uintptr_t)device_get_match_data(chip->dev);

	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.drv_data = chip;
	chip->last_state = POWER_SUPPLY_STATUS_UNKNOWN;
	cros_sbs_invalidate_cached_props(chip);
	mutex_init(&chip->mode_lock);

	chip->i2c_retry_count = 10;
	chip->poll_retry_count = 10;
	chip->charger_broadcasts = true;

	if (pdata) {
		chip->poll_retry_count = pdata->poll_retry_count;
		chip->i2c_retry_count  = pdata->i2c_retry_count;
	}

	platform_set_drvdata(pdev, chip);

	/*
	 * Before we register, we might need to make sure we can actually talk
	 * to the battery.
	 */
	if (!(force_load || chip->gpio_detect)) {
		union power_supply_propval val;

		rc = cros_sbs_get_battery_presence_and_health(
				chip, POWER_SUPPLY_PROP_PRESENT, &val);
		if (rc < 0 || !val.intval)
			return dev_err_probe(chip->dev, -ENODEV,
					     "Failed to get present status\n");
	}

	rc = devm_delayed_work_autocancel(chip->dev, &chip->work,
					  cros_sbs_delayed_work);
	if (rc)
		return rc;

	chip->power_supply = devm_power_supply_register(chip->dev, cros_sbs_desc,
						   &psy_cfg);
	if (IS_ERR(chip->power_supply))
		return dev_err_probe(dev, PTR_ERR(chip->power_supply),
				     "Failed to register power supply\n");

	dev_info(chip->dev,
		"%s: battery gas gauge device registered\n", pdev->name);

	/* Get battery events from the EC */
	chip->notifier.notifier_call = cros_sbs_ec_notify;
	rc = blocking_notifier_chain_register(&ec_dev->ec_dev->event_notifier,
					       &chip->notifier);
	if (rc < 0)
		dev_err(dev, "Failed to register notifier (err:%d)\n", rc);

	return 0;
}

#if defined CONFIG_PM_SLEEP

static int cros_sbs_suspend(struct device *dev)
{
	struct cros_sbs_info *chip = dev_get_drvdata(dev);
	int ret;

	if (chip->poll_time > 0)
		cancel_delayed_work_sync(&chip->work);

	if (chip->flags & CROS_SBS_FLAGS_TI_BQ20ZX5) {
		/* Write to manufacturer access with sleep command. */
		ret = cros_sbs_write_word_data(chip,
					  cros_sbs_data[REG_MANUFACTURER_DATA].addr,
					  MANUFACTURER_ACCESS_SLEEP);
		if (chip->is_present && ret < 0)
			return ret;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cros_sbs_pm_ops, cros_sbs_suspend, NULL);
#define CROS_SBS_PM_OPS (&cros_sbs_pm_ops)

#else
#define CROS_SBS_PM_OPS NULL
#endif

static const struct platform_device_id cros_sbs_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_sbs_id);

static struct platform_driver cros_sbs_battery_driver = {
	.probe		= cros_sbs_probe,
	.id_table	= cros_sbs_id,
	.driver = {
		.name	= DRV_NAME,
		.pm	= CROS_SBS_PM_OPS,
	},
};
module_platform_driver(cros_sbs_battery_driver);

MODULE_DESCRIPTION("CROS SBS battery monitor driver");
MODULE_LICENSE("GPL");

module_param(force_load, bool, 0444);
MODULE_PARM_DESC(force_load,
		 "Attempt to load the driver even if no battery is connected");
