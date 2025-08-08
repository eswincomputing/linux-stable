// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Fan Control CORE driver
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
 * Authors: Han Min <hanmin@eswincomputing.com>
 */


#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/wait.h>
#include <linux/delay.h>

#define FAN_PWM_DUTY			0x0
#define FAN_PWM_PERIOD			0x1
#define FAN_PWM_FREE			0x2
#define DDR_TRAINING_TEMP		0x3
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
#define PMIX_LOWEST_TEMP		0x4
#define D2D_RECOVERY			0x5
#endif

#define FAN_RPM_MAX_VALUE			(100000)
#define FAN_RPM_MAX_READ_CNT		(100)
#define FAN_RPM_RETRY_INTERVAL		(10)
#define FAN_RPM_SAMPLE_CNT			(10)//sampling times
#define FAM_RPM_TOLERANCE_RATIO 	(5)

/* register map */
#define REG_FAN_INT				0x0
#define REG_FAN_RPM				0x4
/* test register map */
#define SYS_CON_TESTREG0		(0x0) //0x51810668 and 0x71810668
#define SYS_CON_TESTREG1		(0x4) //0x5181066C and 0x7181066C
#define SYS_CON_TESTREG2		(0x8) //0x51810670 and 0x71810670
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
#define D2D_RECOVERY_MASK		BIT(31) //d2d recovery bit
#define PMIX_LOWEST_TEMP_MASK	GENMASK(20, 0) //pmix lowest temperature
#endif
#define SYS_CON_TESTREG3		(0xC) //0x51810674 and 0x71810674


/* wait for 50 times pwm period to trigger read interrupt */
#define TIMEOUT(period)        nsecs_to_jiffies(50*(period))

struct eswin_fan_control_data {
	struct reset_control *fan_rst;
	struct clk *clk;
	void __iomem *base;
	void __iomem *cur_die_test_reg_base;
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
	void __iomem *other_die_test_reg_base;
#endif
	struct device *hdev;
	unsigned long clk_rate;
	int pwm_id;
	struct pwm_device *pwm;
	wait_queue_head_t wq;
	bool wait_flag;
	int irq;
	/* pwm minimum period */
	u32 min_period;
	/* pulses per revolution */
	u32 ppr;
	/* revolutions per minute */
	u32 rpm;
	/* last revolutions per minute */
	u32 last_rpm;
	u8 pwm_inverted;
	/* for getting rpm and setting pwm */
	struct mutex fan_lock;
};

static inline void fan_iowrite(const u32 val, const u32 reg,
			       const struct eswin_fan_control_data *ctl)
{
	iowrite32(val, ctl->base + reg);
}

static inline u32 fan_ioread(const u32 reg,
			     const struct eswin_fan_control_data *ctl)
{
	return ioread32(ctl->base + reg);
}

static ssize_t eswin_ddr_training_temp_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int temp = 0;

	if (DDR_TRAINING_TEMP == attr->index) {
		temp = ioread32(ctl->cur_die_test_reg_base + SYS_CON_TESTREG3);
	} else {
		dev_err(dev, "get error attr index 0x%x\n", attr->index);
	}

	return sprintf(buf, "%d\n", temp);
}

#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
static ssize_t eswin_pmix_lowest_temp_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int temp = 0;

	if (PMIX_LOWEST_TEMP == attr->index) {
		temp = ioread32(ctl->cur_die_test_reg_base + SYS_CON_TESTREG2) & PMIX_LOWEST_TEMP_MASK;
	} else {
		dev_err(dev, "get error attr index 0x%x\n", attr->index);
	}

	return sprintf(buf, "%d\n", temp);
}

static ssize_t eswin_d2d_recovery_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int recovery = 0;
	int cur_die_val = 0;
	int other_die_val = 0;

	if (D2D_RECOVERY == attr->index) {
		cur_die_val = ioread32(ctl->cur_die_test_reg_base + SYS_CON_TESTREG2) & D2D_RECOVERY_MASK;
		other_die_val = ioread32(ctl->other_die_test_reg_base + SYS_CON_TESTREG2) & D2D_RECOVERY_MASK;
		if (cur_die_val || other_die_val) {
			recovery = 1;
		}
	} else {
		dev_err(dev, "get error attr index 0x%x\n", attr->index);
	}

	return sprintf(buf, "%d\n", recovery);
}
#endif

static long eswin_fan_control_get_pwm_duty(const struct eswin_fan_control_data *ctl)
{
	struct pwm_state state;
	int duty;

	pwm_get_state(ctl->pwm, &state);
	duty = pwm_get_relative_duty_cycle(&state, 100);

	return duty;
}

static long es_fan_rpm_filter_and_average(
	struct device *dev, u32 samples[], u32 count) {
	u32 idx = 0;
	u32 min = 0;
	u32 sum = 0;
	u32 cnt = 0;

	if ((samples == NULL) || (count <= 0) || (count > FAN_RPM_SAMPLE_CNT)) {
		dev_err(dev, "param error with count=%d\n", count);
		return 0;
	}

	min = samples[0];
	for (idx = 1; idx < count; idx++) {
		if (samples[idx] < min) {
			min = samples[idx];
		}
	}
	for (idx = 0; idx < count; idx++) {
		if ((samples[idx] - min) <= (min * FAM_RPM_TOLERANCE_RATIO / 100)) {
			sum += samples[idx];
			cnt++;
		}
	}

	return cnt ? (sum / cnt) : 0;
}

static long eswin_fan_control_get_fan_rpm(struct eswin_fan_control_data *ctl)
{
	unsigned int val;
	long period, timeout;
	int ret;

	ctl->wait_flag = false;
	period = pwm_get_period(ctl->pwm);
	timeout = msecs_to_jiffies(1500);

	val = fan_ioread(REG_FAN_INT, ctl);
	val = val | 0x1;
	fan_iowrite(val, REG_FAN_INT, ctl);

	/* wair read interrupt */
	ret = wait_event_interruptible_timeout(ctl->wq,
                                        ctl->wait_flag,
                                        timeout);

	if (!ret){
		/* timeout, set rpm to 0 */
		ctl->rpm = 0;
	}
	if(ctl->rpm)
		ctl->rpm = DIV_ROUND_CLOSEST(60 * ctl->clk_rate, ctl->ppr * ctl->rpm);

	return ret;
}

static int eswin_fan_control_read_fan(struct device *dev, u32 attr, long *val)
{
	long ret = 0;
	int retry = 0;
	u32 samples[FAN_RPM_SAMPLE_CNT] = {0};
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_fan_input:
		mutex_lock(&ctl->fan_lock);
		for (int32_t idx = 0; idx < FAN_RPM_SAMPLE_CNT; idx++) {
			retry = 0;
			while (retry < FAN_RPM_MAX_READ_CNT) {
				ret = eswin_fan_control_get_fan_rpm(ctl);
				if (ret == 0) {
					/* timeout case */
					*val = 0;
					mutex_unlock(&ctl->fan_lock);
					return 0;
				} else if (ret < 0) {
					if (ret == -ERESTARTSYS) {
						/* cancel case */
						mutex_unlock(&ctl->fan_lock);
						return -EINTR;
					}
					dev_err(dev, "wait read interrupt fail, ret=%ld\n", ret);
					retry++;
					continue;
				}
				if (ctl->rpm > FAN_RPM_MAX_VALUE) {
					msleep(FAN_RPM_RETRY_INTERVAL);
					retry++;
					continue;
				} else {
					break;
				}
			}
			if (retry == FAN_RPM_MAX_READ_CNT) {
				samples[idx] = ctl->last_rpm;
			} else {
				samples[idx] = ctl->rpm;
			}
			msleep(FAN_RPM_RETRY_INTERVAL);
		}
		*val = es_fan_rpm_filter_and_average(dev, samples, FAN_RPM_SAMPLE_CNT);
		ctl->last_rpm = *val;
		mutex_unlock(&ctl->fan_lock);
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_read_pwm(struct device *dev, u32 attr, long *val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		*val = eswin_fan_control_get_pwm_duty(ctl);
		if(1 ==  ctl->pwm_inverted)
		{
			*val = 100 - *val;
		}
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_set_pwm_duty(const long val, struct eswin_fan_control_data *ctl)
{
	struct pwm_state state;

	mutex_lock(&ctl->fan_lock);
	pwm_get_state(ctl->pwm, &state);
	pwm_set_relative_duty_cycle(&state, val, 100);
	pwm_apply_might_sleep(ctl->pwm, &state);
	mutex_unlock(&ctl->fan_lock);

	return 0;
}

static int eswin_fan_control_write_pwm(struct device *dev, u32 attr, long val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);
	switch (attr) {
		case hwmon_pwm_input:
	if((val < 10) || (val > 99))
	{
		dev_err(dev,"pwm range is form 10 to 99\n");
		return -EINVAL;
	}
	else
	{
		if(1 ==  ctl->pwm_inverted)
		{
			val = 100 - val;
		}
		return eswin_fan_control_set_pwm_duty(val, ctl);
	}
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int eswin_fan_control_read_labels(struct device *dev,
					enum hwmon_sensor_types type,
					u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		*str = "FAN";
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_read(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return eswin_fan_control_read_fan(dev, attr, val);
	case hwmon_pwm:
		return eswin_fan_control_read_pwm(dev, attr, val);
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_write(struct device *dev,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return eswin_fan_control_write_pwm(dev, attr, val);
	default:
		return -ENOTSUPP;
	}
}

static umode_t eswin_fan_control_fan_is_visible(const u32 attr)
{
	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_label:
		return 0444;
	default:
		return 0;
	}
}

static umode_t eswin_fan_control_pwm_is_visible(const u32 attr)
{
	switch (attr) {
	case hwmon_pwm_input:
		return 0644;
	default:
		return 0;
	}
}

static umode_t eswin_fan_control_is_visible(const void *data,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return eswin_fan_control_fan_is_visible(attr);
	case hwmon_pwm:
		return eswin_fan_control_pwm_is_visible(attr);
	default:
		return 0;
	}
}

static irqreturn_t eswin_fan_control_irq_handler(int irq, void *data)
{
	struct eswin_fan_control_data *ctl = (struct eswin_fan_control_data *)data;
	u32 status = 0;

	status = fan_ioread(REG_FAN_INT, ctl);
	if (0x3 == (status & 0x3)){
		ctl->rpm = fan_ioread(REG_FAN_RPM, ctl);

		/* clear interrupt */
		fan_iowrite(0x5, REG_FAN_INT, ctl);

		/* When the fan does not support obtaining speed, bit3 will be set to 1 */
		if (0x0 == (status & (0x1 << 3))) {
			/* wake up fan_rpm read */
			ctl->wait_flag = true;
			wake_up_interruptible(&ctl->wq);
		}
	}

	return IRQ_HANDLED;
}

static int eswin_fan_control_init(struct eswin_fan_control_data *ctl,
				const struct device_node *np)
{
	int ret;
	/* get fan pulses per revolution */
	ret = of_property_read_u32(np, "pulses-per-revolution", &ctl->ppr);
	if (ret)
		return ret;

	/* 1, 2 and 4 are the typical and accepted values */
	if (ctl->ppr != 1 && ctl->ppr != 2 && ctl->ppr != 4)
		return -EINVAL;

	/* get pwm minimum period */
	ret = of_property_read_u32(np, "pwm-minimum-period", &ctl->min_period);
	if (ret)
		return ret;

	return ret;
}

static void eswin_fan_control_remove(void *data)
{
	int ret;
	struct eswin_fan_control_data *ctl = data;
	pwm_put(ctl->pwm);
	ret = reset_control_assert(ctl->fan_rst);
	WARN_ON(0 != ret);
	clk_disable_unprepare(ctl->clk);
}

static const struct hwmon_channel_info *eswin_fan_control_info[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_ops eswin_fan_control_hwmon_ops = {
	.is_visible = eswin_fan_control_is_visible,
	.read = eswin_fan_control_read,
	.write = eswin_fan_control_write,
	.read_string = eswin_fan_control_read_labels,
};

static const struct hwmon_chip_info eswin_chip_info = {
	.ops = &eswin_fan_control_hwmon_ops,
	.info = eswin_fan_control_info,
};

static SENSOR_DEVICE_ATTR_RO(ddr_training_temp, eswin_ddr_training_temp, DDR_TRAINING_TEMP);
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
static SENSOR_DEVICE_ATTR_RO(pmix_lowest_temp, eswin_pmix_lowest_temp, PMIX_LOWEST_TEMP);
static SENSOR_DEVICE_ATTR_RO(d2d_recovery, eswin_d2d_recovery, D2D_RECOVERY);
#endif

static struct attribute *eswin_fan_control_attrs[] = {
	&sensor_dev_attr_ddr_training_temp.dev_attr.attr,
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
	&sensor_dev_attr_pmix_lowest_temp.dev_attr.attr,
	&sensor_dev_attr_d2d_recovery.dev_attr.attr,
#endif
	NULL,
};
ATTRIBUTE_GROUPS(eswin_fan_control);

static const struct of_device_id eswin_fan_control_of_match[] = {
	{ .compatible = "eswin-fan-control"},
	{}
};
MODULE_DEVICE_TABLE(of, eswin_fan_control_of_match);

static int eswin_fan_control_probe(struct platform_device *pdev)
{
	struct eswin_fan_control_data *ctl;
	const struct of_device_id *id;
	const char *name = "eswin_fan_control";
	struct pwm_state state;
	struct pwm_args pwm_args;
	struct resource *res;
	int ret;

	id = of_match_node(eswin_fan_control_of_match, pdev->dev.of_node);
	if (!id)
		return -EINVAL;

	ctl = devm_kzalloc(&pdev->dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	ctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctl->base))
		return PTR_ERR(ctl->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;
	ctl->cur_die_test_reg_base = ioremap(res->start, res->end - res->start + 1);
	if (IS_ERR_OR_NULL(ctl->cur_die_test_reg_base)) {
		return PTR_ERR(ctl->cur_die_test_reg_base);
	}
#ifdef CONFIG_ARCH_ESWIN_EIC7702_SOC
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res)
		return -ENODEV;
	ctl->other_die_test_reg_base = ioremap(res->start, res->end - res->start + 1);
	if (IS_ERR_OR_NULL(ctl->other_die_test_reg_base)) {
		return PTR_ERR(ctl->other_die_test_reg_base);
	}
#endif

	ctl->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(ctl->clk)) {
		dev_err(&pdev->dev, "Couldn't get the clock for fan-controller\n");
		return -ENODEV;
	}

	ret = clk_prepare_enable(ctl->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable clock for fan-controller\n");
		return ret;
	}

	ctl->clk_rate = clk_get_rate(ctl->clk);
	if (!ctl->clk_rate)
		return -EINVAL;

	ctl->fan_rst = devm_reset_control_get_optional(&pdev->dev, "fan_rst");
	if (IS_ERR_OR_NULL(ctl->fan_rst)) {
		dev_err(&pdev->dev, "Failed to get fan_rst reset handle\n");
		return -EFAULT;
	}
	ret = reset_control_reset(ctl->fan_rst);
	WARN_ON(0 != ret);
	ctl->pwm_inverted = of_property_read_bool(pdev->dev.of_node, "eswin,pwm_inverted");
	init_waitqueue_head(&ctl->wq);

	ctl->irq = platform_get_irq(pdev, 0);
	if (ctl->irq < 0)
		return ctl->irq;

	ret = devm_request_threaded_irq(&pdev->dev, ctl->irq,
					eswin_fan_control_irq_handler, NULL,
					IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					pdev->driver_override, ctl);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request an irq, %d", ret);
		return ret;
	}

	ret = eswin_fan_control_init(ctl, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize device\n");
		return ret;
	}
	ctl->pwm = pwm_get(&pdev->dev, NULL);
	if (IS_ERR(ctl->pwm)) {
		ret = PTR_ERR(ctl->pwm);
		dev_err(&pdev->dev, "Failed to request pwm device: %d\n", ret);
		return ret;
	}

	pwm_get_state(ctl->pwm, &state);

	/* Then fill it with the reference config */
	pwm_get_args(ctl->pwm, &pwm_args);

	if (0 == ctl->pwm_inverted)
	{
		state.period = pwm_args.period;
		state.duty_cycle = state.period * 50 / 100; /* default set medium speed */
	}
	else
	{
		state.period = pwm_args.period;
		state.duty_cycle = state.period / 100; /* default set medium speed */
		if(0 == state.duty_cycle)
		{
			state.duty_cycle = 50;
		}
	}
	dev_err(&pdev->dev, "state.period: %lld state.duty_cycle: %lld\n",
			state.period,state.duty_cycle);
	ret = pwm_apply_might_sleep(ctl->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d\n",
			ret);
	}
	pwm_enable(ctl->pwm);

	ret = devm_add_action_or_reset(&pdev->dev, eswin_fan_control_remove, ctl);
	if (ret)
		return ret;

	ctl->hdev = devm_hwmon_device_register_with_info(&pdev->dev,
							 name,
							 ctl,
							 &eswin_chip_info,
							 eswin_fan_control_groups);
	dev_set_drvdata(&pdev->dev, ctl);
	mutex_init(&ctl->fan_lock);
	dev_err(&pdev->dev, "eswin fan control init exit\n");

	return PTR_ERR_OR_ZERO(ctl->hdev);
}

#ifdef CONFIG_PM_SLEEP
static int eswin_fan_control_suspend(struct device *dev)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	pwm_disable(ctl->pwm);
	clk_disable_unprepare(ctl->clk);

	return 0;
}

static int eswin_fan_control_resume(struct device *dev)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	clk_prepare_enable(ctl->clk);
	pwm_enable(ctl->pwm);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(fan_control_pm_ops, eswin_fan_control_suspend, eswin_fan_control_resume);

static struct platform_driver eswin_fan_control_driver = {
	.driver = {
		.name = "eswin_fan_control_driver",
		.pm = &fan_control_pm_ops,
		.of_match_table = eswin_fan_control_of_match,
	},
	.probe = eswin_fan_control_probe,
};
module_platform_driver(eswin_fan_control_driver);

MODULE_AUTHOR("Han Min <hanmin@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN Fan Control CORE driver");
MODULE_LICENSE("GPL");
