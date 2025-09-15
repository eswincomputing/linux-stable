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


#define FAN_PWM_CHAN_CNT			(3)

#define FAN_RPM_MAX_VALUE			(100000)
#define FAN_RPM_MAX_READ_CNT		(100)
#define FAN_RPM_RETRY_INTERVAL		(10)
#define FAN_RPM_SAMPLE_CNT			(10)//sampling times
#define FAN_RPM_TOLERANCE_RATIO 	(5)

/* register map */
#define REG_FAN_INT					(0x0)
#define REG_FAN_RPM					(0x4)

/* wait for 50 times pwm period to trigger read interrupt */
#define TIMEOUT(period)				nsecs_to_jiffies(50 * (period))

struct eswin_fan_control_data {
	struct reset_control *fan_rst;
	struct clk *clk;
	void __iomem *base;
	struct device *hdev;
	unsigned long clk_rate;
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
	/* for getting rpm and setting pwm */
	struct mutex fan_lock;
	int inst_cnt;
	const char *labels[FAN_PWM_CHAN_CNT];
	struct pwm_device *pwm_dev[FAN_PWM_CHAN_CNT];
	u32 *pwm_config;
	u32 *fan_config;
	struct hwmon_chip_info *chip_info;
	const struct hwmon_channel_info **channel_info;
};

static inline void fan_iowrite(
	const u32 val, const u32 reg, const struct eswin_fan_control_data *ctl)
{
	iowrite32(val, ctl->base + reg);
}

static inline u32 fan_ioread(
	const u32 reg, const struct eswin_fan_control_data *ctl)
{
	return ioread32(ctl->base + reg);
}

static long eswin_fan_control_get_pwm_duty(
	const struct eswin_fan_control_data *ctl, int channel)
{
	struct pwm_state state;
	int duty;

	pwm_get_state(ctl->pwm_dev[channel], &state);
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
		dev_err(dev, "%s():line%d param error with count=%d\n",
			__func__, __LINE__, count);
		return 0;
	}

	min = samples[0];
	for (idx = 1; idx < count; idx++) {
		if (samples[idx] < min) {
			min = samples[idx];
		}
	}
	for (idx = 0; idx < count; idx++) {
		if ((samples[idx] - min) <= (min * FAN_RPM_TOLERANCE_RATIO / 100)) {
			sum += samples[idx];
			cnt++;
		}
	}

	return cnt ? (sum / cnt) : 0;
}

static long eswin_fan_control_get_fan_rpm(
	struct eswin_fan_control_data *ctl, int channel)
{
	unsigned int val;
	long period, timeout;
	int ret;

	if(0 != channel) {
		ctl->rpm = 0;
		return 0;
	}

	ctl->wait_flag = false;
	period = pwm_get_period(ctl->pwm_dev[channel]);
	timeout = msecs_to_jiffies(1500);

	val = fan_ioread(REG_FAN_INT, ctl);
	val = val | 0x1;
	fan_iowrite(val, REG_FAN_INT, ctl);

	/* wair read interrupt */
	ret = wait_event_interruptible_timeout(ctl->wq, ctl->wait_flag, timeout);
	if (!ret){
		/* timeout, set rpm to 0 */
		ctl->rpm = 0;
	}
	if(ctl->rpm)
		ctl->rpm = DIV_ROUND_CLOSEST(60 * ctl->clk_rate, ctl->ppr * ctl->rpm);

	return ret;
}

static int eswin_fan_control_read_fan(
	struct device *dev, u32 attr, int channel, long *val)
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
				ret = eswin_fan_control_get_fan_rpm(ctl, channel);
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
					dev_err(dev, "%s():line%d wait interrupt fail, ret=%ld\n",
						__func__, __LINE__, ret);
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

static int eswin_fan_control_read_pwm(
	struct device *dev, u32 attr, int channel, long *val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		*val = eswin_fan_control_get_pwm_duty(ctl, channel);
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_set_pwm_duty(
	const long val, struct eswin_fan_control_data *ctl, int channel)
{
	struct pwm_state state;

	mutex_lock(&ctl->fan_lock);
	pwm_get_state(ctl->pwm_dev[channel], &state);
	pwm_set_relative_duty_cycle(&state, val, 100);
	pwm_apply_might_sleep(ctl->pwm_dev[channel], &state);
	mutex_unlock(&ctl->fan_lock);

	return 0;
}

static int eswin_fan_control_write_pwm(
	struct device *dev, u32 attr, int channel, long val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);
	switch (attr) {
	case hwmon_pwm_input:
		if ((val < 0) || (val > 100)) {
			dev_err(dev,"%s():line%d pwm range is 0 to 100, val=%ld\n",
				__func__, __LINE__, val);
			return -EINVAL;
		} else {
			return eswin_fan_control_set_pwm_duty(val, ctl, channel);
		}
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int eswin_fan_control_read_labels(struct device *dev,
	enum hwmon_sensor_types type, u32 attr, int channel, const char **str)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	if (!ctl) {
		dev_err(dev, "%s():line%d ctl is NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if ((channel < 0) || (channel >= ctl->inst_cnt)) {
		dev_err(dev, "%s():line%d channel error, channel=%d inst_cnt=%d\n",
			__func__, __LINE__, channel, ctl->inst_cnt);
		return -EINVAL;
	}

	switch (type) {
	case hwmon_fan:
		*str=ctl->labels[channel];
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_read(struct device *dev,
	enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	if (!ctl) {
		dev_err(dev, "%s():line%d ctl is NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if ((channel < 0) || (channel >= ctl->inst_cnt)) {
		dev_err(dev, "%s():line%d channel error, channel=%d inst_cnt=%d\n",
			__func__, __LINE__, channel, ctl->inst_cnt);
		return -EINVAL;
	}

	switch (type) {
	case hwmon_fan:
		return eswin_fan_control_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return eswin_fan_control_read_pwm(dev, attr, channel, val);
	default:
		return -ENOTSUPP;
	}
}

static int eswin_fan_control_write(struct device *dev,
	enum hwmon_sensor_types type, u32 attr, int channel, long val)
{
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	if (!ctl) {
		dev_err(dev, "%s():line%d ctl is NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if ((channel < 0) || (channel >= ctl->inst_cnt)) {
		dev_err(dev, "%s():line%d channel error, channel=%d inst_cnt=%d\n",
			__func__, __LINE__, channel, ctl->inst_cnt);
		return -EINVAL;
	}

	switch (type) {
	case hwmon_pwm:
		return eswin_fan_control_write_pwm(dev, attr, channel, val);
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
	enum hwmon_sensor_types type, u32 attr, int channel)
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

static void eswin_fan_control_remove(void *data)
{
	int idx = 0;
	struct eswin_fan_control_data *ctl = data;

	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		if (ctl->pwm_dev[idx] && !IS_ERR(ctl->pwm_dev[idx])) {
			pwm_disable(ctl->pwm_dev[idx]);
		}
	}
	if (ctl->fan_rst) {
		reset_control_assert(ctl->fan_rst);
	}
	if (ctl->clk) {
		clk_disable_unprepare(ctl->clk);
	}
}

static const struct hwmon_ops eswin_fan_control_hwmon_ops = {
	.is_visible = eswin_fan_control_is_visible,
	.read = eswin_fan_control_read,
	.write = eswin_fan_control_write,
	.read_string = eswin_fan_control_read_labels,
};

static const struct of_device_id eswin_fan_control_of_match[] = {
	{ .compatible = "eswin-fan-control"},
	{}
};
MODULE_DEVICE_TABLE(of, eswin_fan_control_of_match);

static int eswin_fan_control_init(struct device *dev)
{
	int ret = -1;
	int idx = 0;
	struct device_node *np = NULL;
	struct eswin_fan_control_data *ctl = NULL;
	struct hwmon_channel_info *pwm_chan = NULL;
	struct hwmon_channel_info *fan_chan = NULL;

	ctl = dev_get_drvdata(dev);
	if (!ctl) {
		dev_err(dev, "%s():line%d ctl is NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	np = dev_of_node(dev);
	if (!np) {
		dev_err(dev, "%s():line%d dev_of_node error\n", __func__, __LINE__);
		return -ENODEV;
	}
	/* get fan pulses per revolution */
	ret = of_property_read_u32(np, "pulses-per-revolution", &ctl->ppr);
	if (ret) {
		dev_err(dev, "%s():line%d read pulses-per-revolution error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}
	/* 1, 2 and 4 are the typical and accepted values */
	if ((ctl->ppr != 1) && (ctl->ppr != 2) && (ctl->ppr != 4)) {
		dev_err(dev, "%s():line%d invalid pulses-per-revolution, ppr=%u\n",
			__func__, __LINE__, ctl->ppr);
		return -EINVAL;
	}
	/* get pwm minimum period */
	ret = of_property_read_u32(np, "pwm-minimum-period", &ctl->min_period);
	if (ret) {
		dev_err(dev, "%s():line%d read pwm-minimum-period error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	if ((ctl->inst_cnt <= 0) || (ctl->inst_cnt > FAN_PWM_CHAN_CNT)) {
		dev_err(dev, "%s():line%d error inst_cnt=%d\n",
			__func__, __LINE__, ctl->inst_cnt);
		return -EINVAL;
	}
	/* dynamically allocate pwm_config and fan_config */
	ctl->pwm_config = devm_kcalloc(
		dev, ctl->inst_cnt + 1, sizeof(*ctl->pwm_config), GFP_KERNEL);
	if (!ctl->pwm_config) {
		dev_err(dev, "%s():line%d devm_kcalloc pwm_config error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	ctl->fan_config = devm_kcalloc(
		dev, ctl->inst_cnt + 1, sizeof(*ctl->fan_config), GFP_KERNEL);
	if (!ctl->fan_config) {
		dev_err(dev, "%s():line%d devm_kcalloc fan_config error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		ctl->pwm_config[idx] = HWMON_PWM_INPUT;
		ctl->fan_config[idx] = HWMON_F_INPUT | HWMON_F_LABEL;
	}
	ctl->pwm_config[ctl->inst_cnt] = 0;
	ctl->fan_config[ctl->inst_cnt] = 0;

	/* dynamically allocate channel_info */
	ctl->channel_info = devm_kcalloc(
		dev, 3, sizeof(*ctl->channel_info), GFP_KERNEL);
	if (!ctl->channel_info) {
		dev_err(dev, "%s():line%d devm_kcalloc channel_info error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	pwm_chan = devm_kzalloc(dev, sizeof(*pwm_chan), GFP_KERNEL);
	if (!pwm_chan) {
		dev_err(dev, "%s():line%d devm_kzalloc pwm_chan error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	pwm_chan->type = hwmon_pwm;
	pwm_chan->config = ctl->pwm_config;
	ctl->channel_info[0] = pwm_chan;
	fan_chan = devm_kzalloc(dev, sizeof(*fan_chan), GFP_KERNEL);
	if (!fan_chan) {
		dev_err(dev, "%s():line%d devm_kzalloc fan_chan error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	fan_chan->type = hwmon_fan;
	fan_chan->config = ctl->fan_config;
	ctl->channel_info[1] = fan_chan;
	ctl->channel_info[2] = NULL;

	/* dynamically allocate chip_info */
	ctl->chip_info = devm_kzalloc(dev, sizeof(*ctl->chip_info), GFP_KERNEL);
	if (!ctl->chip_info) {
		dev_err(dev, "%s():line%d devm_kzalloc chip_info error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	ctl->chip_info->ops = &eswin_fan_control_hwmon_ops;
	ctl->chip_info->info = ctl->channel_info;

	return 0;
}

static int eswin_fan_control_probe(struct platform_device *pdev)
{
	struct eswin_fan_control_data *ctl = NULL;
	const struct of_device_id *id = NULL;
	const char *name = NULL;
	struct pwm_state state;
	struct pwm_args pwm_args;
	struct fwnode_handle *fwnode = NULL;
	int ret = -1;
	int idx = 0;
	u32 numa_id = 0;

	ret = of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id);
	if(ret) {
		dev_warn(&pdev->dev, "%s():line%d could not get numa-node-id\n",
			__func__, __LINE__);
		numa_id = 0;
	}
	if (numa_id == 0) {
		name = "eswin_fan_control";
	} else {
		name = "d1_eswin_fan_control";
	}

	id = of_match_node(eswin_fan_control_of_match, pdev->dev.of_node);
	if (!id) {
		dev_err(&pdev->dev, "%s():line%d of_match_node error\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	ctl = devm_kzalloc(&pdev->dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl) {
		dev_err(&pdev->dev, "%s():line%d devm_kzalloc ctl error\n",
			__func__, __LINE__);
		return -ENOMEM;
	}
	mutex_init(&ctl->fan_lock);
	dev_set_drvdata(&pdev->dev, ctl);

	ctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctl->base)) {
		dev_err(&pdev->dev, "%s():line%d ioremap base reg error\n",
			__func__, __LINE__);
		return PTR_ERR(ctl->base);
	}

	ctl->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(ctl->clk)) {
		dev_err(&pdev->dev, "%s():line%d devm_clk_get pclk error\n",
			__func__, __LINE__);
		return PTR_ERR(ctl->clk);
	}
	ret = clk_prepare_enable(ctl->clk);
	if (ret) {
		dev_err(&pdev->dev, "%s():line%d clk_prepare_enable pclk error\n",
			__func__, __LINE__);
		return ret;
	}
	ctl->clk_rate = clk_get_rate(ctl->clk);
	if (!ctl->clk_rate) {
		dev_err(&pdev->dev, "%s():line%d clk_get_rate pclk error\n",
			__func__, __LINE__);
		ret = -EINVAL;
		goto err_clk_disable;
	}

	ctl->fan_rst = devm_reset_control_get_optional(&pdev->dev, "fan_rst");
	if (IS_ERR(ctl->fan_rst)) {
		dev_err(&pdev->dev, "%s():line%d get fan_rst error, %ld\n",
			__func__, __LINE__, PTR_ERR(ctl->fan_rst));
		ret = PTR_ERR(ctl->fan_rst);
		goto err_clk_disable;
	}
	if (ctl->fan_rst) {
		ret = reset_control_reset(ctl->fan_rst);
		if (ret) {
			dev_warn(&pdev->dev, "%s():line%d reset fan_rst error, %d\n",
				__func__, __LINE__, ret);
		}
	}

	init_waitqueue_head(&ctl->wq);

	ctl->irq = platform_get_irq(pdev, 0);
	if (ctl->irq < 0) {
		dev_err(&pdev->dev, "%s():line%d platform_get_irq error\n",
			__func__, __LINE__);
		ret = ctl->irq;
		goto err_clk_disable;
	}
	ret = devm_request_threaded_irq(&pdev->dev, ctl->irq,
					eswin_fan_control_irq_handler, NULL,
					IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					pdev->driver_override, ctl);
	if (ret) {
		dev_err(&pdev->dev, "%s():line%d request irq error, ret=%d\n",
			__func__, __LINE__, ret);
		goto err_clk_disable;
	}

	ctl->inst_cnt = 0;
	device_for_each_child_node(&pdev->dev, fwnode) {
		ret = fwnode_property_read_string(
			fwnode, "label", &ctl->labels[ctl->inst_cnt]);
		if (ret) {
			dev_warn(&pdev->dev, "%s():line%d no label for fan%d, using fan\n",
				__func__, __LINE__, ctl->inst_cnt);
			ctl->labels[ctl->inst_cnt] = devm_kstrdup(
				&pdev->dev, "fan", GFP_KERNEL);
			if (!ctl->labels[ctl->inst_cnt]) {
				fwnode_handle_put(fwnode);
				ret = -ENOMEM;
				goto err_clk_disable;
			}
		}
		ctl->pwm_dev[ctl->inst_cnt] = devm_fwnode_pwm_get(
			&pdev->dev, fwnode, NULL);
		if (IS_ERR(ctl->pwm_dev[ctl->inst_cnt])) {
			ret = PTR_ERR(ctl->pwm_dev[ctl->inst_cnt]);
			dev_err(&pdev->dev, "%s():line%d get pwm error, ret=%d inst_cnt=%d\n",
				__func__, __LINE__, ret, ctl->inst_cnt);
			fwnode_handle_put(fwnode);
			goto err_clk_disable;
		}
		fwnode_handle_put(fwnode);
		ctl->inst_cnt++;
		if(ctl->inst_cnt == FAN_PWM_CHAN_CNT) {
			break;
		}
	}
	if (ctl->inst_cnt == 0) {
		ctl->labels[0] = devm_kstrdup(&pdev->dev, "fan", GFP_KERNEL);
		if (!ctl->labels[0]) {
			dev_err(&pdev->dev, "%s():line%d devm_kstrdup error\n",
				__func__, __LINE__);
			ret = -ENOMEM;
			goto err_clk_disable;
		}
		ctl->pwm_dev[0] = devm_pwm_get(&pdev->dev, NULL);
		if (IS_ERR(ctl->pwm_dev[0])) {
			ret = PTR_ERR(ctl->pwm_dev[0]);
			dev_err(&pdev->dev, "%s():line%d get pwm error, ret=%d\n",
				__func__, __LINE__, ret);
			goto err_clk_disable;
		}
		ctl->inst_cnt = 1;
	}

	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		pwm_get_state(ctl->pwm_dev[idx], &state);
		/* Then fill it with the reference config */
		pwm_get_args(ctl->pwm_dev[idx], &pwm_args);
		/* default set medium speed */
		state.period = pwm_args.period;
		state.duty_cycle = state.period * 50 / 100;
		dev_info(&pdev->dev,
			"%s():line%d pwm%d polarity=%d period=%llu duty_cycle=%llu\n",
			__func__, __LINE__, idx, pwm_args.polarity,
			state.period, state.duty_cycle);
		ret = pwm_apply_might_sleep(ctl->pwm_dev[idx], &state);
		if (ret) {
			dev_err(&pdev->dev, "%s():line%d apply pwm state error, ret=%d\n",
				__func__, __LINE__, ret);
			goto err_pwm_disable;
		}
		pwm_enable(ctl->pwm_dev[idx]);
	}

	ret = devm_add_action_or_reset(&pdev->dev, eswin_fan_control_remove, ctl);
	if (ret) {
		dev_err(&pdev->dev, "%s():line%d devm_add_action_or_reset error\n",
			__func__, __LINE__);
		goto err_pwm_disable;
	}

	ret = eswin_fan_control_init(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "%s():line%d init fan config error\n",
			__func__, __LINE__);
		goto err_pwm_disable;
	}
	ctl->hdev = devm_hwmon_device_register_with_info(
		&pdev->dev, name, ctl, ctl->chip_info, NULL);
	ret = PTR_ERR_OR_ZERO(ctl->hdev);
	if (ret) {
		dev_err(&pdev->dev, "%s():line%d register hwmon device error\n",
			__func__, __LINE__);
		goto err_pwm_disable;
	}
	dev_info(&pdev->dev, "%s():line%d init success\n", __func__, __LINE__);

	return 0;

err_pwm_disable:
	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		if (ctl->pwm_dev[idx] && !IS_ERR(ctl->pwm_dev[idx])) {
			pwm_disable(ctl->pwm_dev[idx]);
		}
	}
err_clk_disable:
	clk_disable_unprepare(ctl->clk);
	dev_info(&pdev->dev, "%s():line%d init error\n", __func__, __LINE__);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int eswin_fan_control_suspend(struct device *dev)
{
	int idx;
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		if (ctl->pwm_dev[idx] && !IS_ERR(ctl->pwm_dev[idx])) {
			pwm_disable(ctl->pwm_dev[idx]);
		}
	}
	clk_disable_unprepare(ctl->clk);

	return 0;
}

static int eswin_fan_control_resume(struct device *dev)
{
	int idx;
	struct eswin_fan_control_data *ctl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	clk_prepare_enable(ctl->clk);
	for (idx = 0; idx < ctl->inst_cnt; idx++) {
		if (ctl->pwm_dev[idx] && !IS_ERR(ctl->pwm_dev[idx])) {
			pwm_enable(ctl->pwm_dev[idx]);
		}
	}

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
