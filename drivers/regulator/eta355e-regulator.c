/*
 * Driver for ETA355E step-down switching regulator
 *
 * Datasheet: Vout = 0.6V + 10mV * N (N=0..63)
 * I2C registers:
 *   00h: BUCK_EN0 (B7) + MODE0 (B6) + VSEL0[5:0] (B5-B0)  (VSEL pin = 0)
 *   01h: BUCK_EN1 (B7) + MODE1 (B6) + VSEL1[5:0] (B5-B0)  (VSEL pin = 1)
 *   02h: Out_dis (B7) + SLEW[2:0] (B6-B4) + DVSMODE (B3) + Reserved (B2-B0)
 *   03h: Vendor ID, PGOOD, DIE ID (read-only)
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
 * Authors: Xiang Xu <xuxiang@eswincomputing.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

/* registers */

#define ETA355E_REG_VOUT0      0x00
#define ETA355E_REG_VOUT1      0x01
#define ETA355E_REG_CTRL       0x02
#define ETA355E_REG_STATUS     0x03

/* register 0/1 */

#define ETA355E_BUCK_EN        BIT(7)
#define ETA355E_MODE           BIT(6)
#define ETA355E_VSEL_MASK      0x3F

/* register 2 */

#define ETA355E_OUT_DIS        BIT(7)
#define ETA355E_SLEW_MASK      GENMASK(6,4)
#define ETA355E_DVS_MODE       BIT(3)

/* register 3 */

#define ETA355E_VENDOR_MASK    GENMASK(7,5)
#define ETA355E_PGOOD          BIT(4)
#define ETA355E_DIE_ID_MASK    0x0F

#define ETA355E_MIN_UV         600000
#define ETA355E_STEP_UV        10000
#define ETA355E_NUM_VOLTAGES   64

struct eta355e_chip {

	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev;
	struct gpio_desc *vsel_gpio;
	u8 vsel_reg;
	struct dentry *debug_root;
};

/* slew rate table (uV/us) */

static const int eta355e_slew_rate[] = {
	64000,
	32000,
	16000,
	8000,
	4000,
	2000,
	1000,
	500,
};

static const struct regmap_config eta355e_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ETA355E_REG_STATUS,
};

static int eta355e_get_status(struct regulator_dev *rdev)
{
	struct eta355e_chip *chip = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(chip->regmap, ETA355E_REG_STATUS, &val);
	if (ret)
		return ret;

	if (val & ETA355E_PGOOD)
		return REGULATOR_STATUS_ON;

	return REGULATOR_STATUS_OFF;
}

/* PWM/PFM mode */

static int eta355e_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct eta355e_chip *chip = rdev_get_drvdata(rdev);
	unsigned int val;

	switch (mode) {

	case REGULATOR_MODE_FAST:
		val = ETA355E_MODE;
		break;

	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(chip->regmap,
				  chip->vsel_reg,
				  ETA355E_MODE,
				  val);
}

static unsigned int eta355e_get_mode(struct regulator_dev *rdev)
{
	struct eta355e_chip *chip = rdev_get_drvdata(rdev);
	unsigned int val;

	regmap_read(chip->regmap, chip->vsel_reg, &val);

	if (val & ETA355E_MODE)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

/*
 * DVFS safe voltage change
 */

static int eta355e_set_voltage_sel(struct regulator_dev *rdev,
				   unsigned int sel)
{
	struct eta355e_chip *chip = rdev_get_drvdata(rdev);
	int old_sel;
	int ret;

	old_sel = regulator_get_voltage_sel_regmap(rdev);
	if (old_sel < 0)
		return old_sel;

	/* voltage drop → force PWM */

	if (sel < old_sel) {

		regmap_update_bits(chip->regmap,
				   chip->vsel_reg,
				   ETA355E_MODE,
				   ETA355E_MODE);
	}

	ret = regulator_set_voltage_sel_regmap(rdev, sel);
	if (ret)
		return ret;

	/* restore auto PFM */

	if (sel < old_sel) {

		usleep_range(200, 300);

		regmap_update_bits(chip->regmap,
				   chip->vsel_reg,
				   ETA355E_MODE,
				   0);
	}

	return 0;
}

/* regulator ops */

static const struct regulator_ops eta355e_reg_ops = {

	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,

	.set_voltage_sel = eta355e_set_voltage_sel,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

	.list_voltage = regulator_list_voltage_linear,

	.set_mode = eta355e_set_mode,
	.get_mode = eta355e_get_mode,

	.get_status = eta355e_get_status,
};

/* debugfs */

static int eta355e_debugfs_show(struct seq_file *s, void *data)
{
	struct eta355e_chip *chip = s->private;
	unsigned int val;
	int i;

	for (i = 0; i <= 3; i++) {

		regmap_read(chip->regmap, i, &val);

		seq_printf(s,
			   "reg[0x%02x] = 0x%02x\n",
			   i, val);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(eta355e_debugfs);

static void eta355e_debugfs_init(struct eta355e_chip *chip)
{
	chip->debug_root =
		debugfs_create_dir("eta355e", NULL);

	debugfs_create_file("regs",
			    0444,
			    chip->debug_root,
			    chip,
			    &eta355e_debugfs_fops);
}

/* chip identification */

static int eta355e_chip_init(struct eta355e_chip *chip)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chip->regmap,
			  ETA355E_REG_STATUS,
			  &val);
	if (ret)
		return ret;

	dev_info(chip->dev,
		 "Vendor=%x DIE=%x\n",
		 (val >> 5) & 0x7,
		 val & ETA355E_DIE_ID_MASK);

	return 0;
}

static int eta355e_apply_dt(struct eta355e_chip *chip,
			    struct regulator_desc *desc)
{
	struct device_node *np = chip->dev->of_node;
	u32 slew;
	int ret;

	if (!of_property_read_u32(np, "eta,slew-rate", &slew)) {

		if (slew > 7)
			return -EINVAL;

		ret = regmap_update_bits(chip->regmap,
					 ETA355E_REG_CTRL,
					 ETA355E_SLEW_MASK,
					 slew << 4);
		if (ret)
			return ret;

		desc->ramp_delay =
			eta355e_slew_rate[slew];
	}

	if (of_property_read_bool(np,
				  "regulator-active-discharge")) {

		ret = regmap_update_bits(chip->regmap,
					 ETA355E_REG_CTRL,
					 ETA355E_OUT_DIS,
					 ETA355E_OUT_DIS);
		if (ret)
			return ret;
	}

	return 0;
}

static int eta355e_probe(struct i2c_client *client)
{
	struct eta355e_chip *chip;
	struct regulator_desc *desc;
	struct regulator_config config = {};
	struct regulator_init_data *init_data;
	int vsel;
	int ret;

	chip = devm_kzalloc(&client->dev,
			    sizeof(*chip),
			    GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;

	chip->regmap =
		devm_regmap_init_i2c(client,
				     &eta355e_regmap_config);

	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);

	/* read VSEL GPIO */
	chip->vsel_gpio =
		devm_gpiod_get_optional(&client->dev,
					"vsel",
					GPIOD_IN);

	if (IS_ERR(chip->vsel_gpio))
		return PTR_ERR(chip->vsel_gpio);

	if (chip->vsel_gpio)
		vsel = gpiod_get_value(chip->vsel_gpio);
	else
		vsel = 0;

	chip->vsel_reg = vsel ?
			 ETA355E_REG_VOUT1 :
			 ETA355E_REG_VOUT0;

	dev_info(&client->dev,
		 "VSEL=%d register=0x%x\n",
		 vsel, chip->vsel_reg);

	ret = eta355e_chip_init(chip);
	if (ret)
		return ret;

	dev_info(&client->dev,"%s %d\r\n",__func__,__LINE__);
	desc = devm_kzalloc(&client->dev,
			    sizeof(*desc),
			    GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	dev_info(&client->dev,"%s %d\r\n",__func__,__LINE__);
	desc->name = "eta355e";
	desc->id = 0;
	desc->type = REGULATOR_VOLTAGE;
	desc->owner = THIS_MODULE;

	desc->ops = &eta355e_reg_ops;

	desc->min_uV = ETA355E_MIN_UV;
	desc->uV_step = ETA355E_STEP_UV;
	desc->n_voltages = ETA355E_NUM_VOLTAGES;

	desc->vsel_reg = chip->vsel_reg;
	desc->vsel_mask = ETA355E_VSEL_MASK;

	desc->enable_reg = chip->vsel_reg;
	desc->enable_mask = ETA355E_BUCK_EN;

	ret = eta355e_apply_dt(chip, desc);
	if (ret)
		return ret;
	dev_info(&client->dev,"%s %d\r\n",__func__,__LINE__);

	init_data =
		of_get_regulator_init_data(&client->dev,
					   client->dev.of_node,
					   desc);

	config.dev = &client->dev;
	config.driver_data = chip;
	config.regmap = chip->regmap;
	config.init_data = init_data;
	config.of_node = client->dev.of_node;

	chip->rdev =
		devm_regulator_register(&client->dev,
					desc,
					&config);

	if (IS_ERR(chip->rdev))
		return PTR_ERR(chip->rdev);

	dev_info(&client->dev,"%s %d\r\n",__func__,__LINE__);
	eta355e_debugfs_init(chip);

	dev_info(&client->dev,
		 "ETA355E regulator registered\n");

	return 0;
}

static const struct of_device_id eta355e_of_match[] = {
	{ .compatible = "eta,eta355e" },
	{}
};
MODULE_DEVICE_TABLE(of, eta355e_of_match);

static struct i2c_driver eta355e_driver = {

	.driver = {
		.name = "eta355e",
		.of_match_table = eta355e_of_match,
	},

	.probe = eta355e_probe,
};

module_i2c_driver(eta355e_driver);

MODULE_AUTHOR("XuXiang <xuxiang@eswincomputing.com>");
MODULE_DESCRIPTION("ETA355E regulator driver");
MODULE_LICENSE("GPL v2");
