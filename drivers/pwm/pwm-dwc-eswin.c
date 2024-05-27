// SPDX-License-Identifier: GPL-2.0
/*
 * DesignWare PWM Controller driver
 *
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * Author: Felipe Balbi (Intel)
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Author: Raymond Tan <raymond.tan@intel.com>
 *
 * Limitations:
 * - The hardware cannot generate a 0 % or 100 % duty cycle. Both high and low
 *   periods are one or more input clock periods long.
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Author: xuxiang@eswincomputing.com
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>

#define DWC_TIM_LD_CNT(n)	((n) * 0x14)
#define DWC_TIM_LD_CNT2(n)	(((n) * 4) + 0xb0)
#define DWC_TIM_CUR_VAL(n)	(((n) * 0x14) + 0x04)
#define DWC_TIM_CTRL(n)		(((n) * 0x14) + 0x08)
#define DWC_TIM_EOI(n)		(((n) * 0x14) + 0x0c)
#define DWC_TIM_INT_STS(n)	(((n) * 0x14) + 0x10)

#define DWC_TIMERS_INT_STS	0xa0
#define DWC_TIMERS_EOI		0xa4
#define DWC_TIMERS_RAW_INT_STS	0xa8
#define DWC_TIMERS_COMP_VERSION	0xac

#define DWC_TIMERS_TOTAL	8
#define DWC_CLK_PERIOD_NS	10

/* Timer Control Register */
#define DWC_TIM_CTRL_EN		BIT(0)
#define DWC_TIM_CTRL_MODE	BIT(1)
#define DWC_TIM_CTRL_MODE_FREE	(0 << 1)
#define DWC_TIM_CTRL_MODE_USER	(1 << 1)
#define DWC_TIM_CTRL_INT_MASK	BIT(2)
#define DWC_TIM_CTRL_PWM	BIT(3)

struct dwc_pwm_ctx {
	u32 cnt;
	u32 cnt2;
	u32 ctrl;
};

struct dwc_pwm {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	struct dwc_pwm_ctx ctx[DWC_TIMERS_TOTAL];
};
#define to_dwc_pwm(p)	(container_of((p), struct dwc_pwm, chip))

static inline u32 dwc_pwm_readl(struct dwc_pwm *dwc, u32 offset)
{
	return readl(dwc->base + offset);
}

static inline void dwc_pwm_writel(struct dwc_pwm *dwc, u32 value, u32 offset)
{
	writel(value, dwc->base + offset);
}

static void __dwc_pwm_set_enable(struct dwc_pwm *dwc, int pwm, int enabled)
{
	u32 reg;

	reg = dwc_pwm_readl(dwc, DWC_TIM_CTRL(pwm));

	if (enabled)
		reg |= DWC_TIM_CTRL_EN;
	else
		reg &= ~DWC_TIM_CTRL_EN;

	dwc_pwm_writel(dwc, reg, DWC_TIM_CTRL(pwm));
}

static int __dwc_pwm_configure_timer(struct dwc_pwm *dwc,
				     struct pwm_device *pwm,
				     const struct pwm_state *state)
{
	u64 tmp;
	u32 ctrl;
	u32 high;
	u32 low;

	/*
	 * Calculate width of low and high period in terms of input clock
	 * periods and check are the result within HW limits between 1 and
	 * 2^32 periods.
	 */
	tmp = DIV_ROUND_CLOSEST_ULL(state->duty_cycle, DWC_CLK_PERIOD_NS);
	if (tmp < 1 || tmp > (1ULL << 32))
		return -ERANGE;
	low = tmp - 1;

	tmp = DIV_ROUND_CLOSEST_ULL(state->period - state->duty_cycle,
				    DWC_CLK_PERIOD_NS);
	if (tmp < 1 || tmp > (1ULL << 32))
		return -ERANGE;
	high = tmp - 1;

	/*
	 * Specification says timer usage flow is to disable timer, then
	 * program it followed by enable. It also says Load Count is loaded
	 * into timer after it is enabled - either after a disable or
	 * a reset. Based on measurements it happens also without disable
	 * whenever Load Count is updated. But follow the specification.
	 */
	__dwc_pwm_set_enable(dwc, pwm->hwpwm, false);

	/*
	 * Write Load Count and Load Count 2 registers. Former defines the
	 * width of low period and latter the width of high period in terms
	 * multiple of input clock periods:
	 * Width = ((Count + 1) * input clock period).
	 */
	dwc_pwm_writel(dwc, low, DWC_TIM_LD_CNT(pwm->hwpwm));
	dwc_pwm_writel(dwc, high, DWC_TIM_LD_CNT2(pwm->hwpwm));

	/*
	 * Set user-defined mode, timer reloads from Load Count registers
	 * when it counts down to 0.
	 * Set PWM mode, it makes output to toggle and width of low and high
	 * periods are set by Load Count registers.
	 */
	ctrl = DWC_TIM_CTRL_MODE_USER | DWC_TIM_CTRL_PWM;
	dwc_pwm_writel(dwc, ctrl, DWC_TIM_CTRL(pwm->hwpwm));

	/*
	 * Enable timer. Output starts from low period.
	 */
	__dwc_pwm_set_enable(dwc, pwm->hwpwm, state->enabled);

	return 0;
}

static int dwc_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct dwc_pwm *dwc = to_dwc_pwm(chip);

	if (state->polarity != PWM_POLARITY_INVERSED)
		return -EINVAL;

	if (state->enabled) {
		if (!pwm->state.enabled)
			pm_runtime_get_sync(chip->dev);
		return __dwc_pwm_configure_timer(dwc, pwm, state);
	} else {
		if (pwm->state.enabled) {
			__dwc_pwm_set_enable(dwc, pwm->hwpwm, false);
			pm_runtime_put_sync(chip->dev);
		}
	}

	return 0;
}

static int dwc_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct dwc_pwm *dwc = to_dwc_pwm(chip);
	u64 duty, period;

	pm_runtime_get_sync(chip->dev);

	state->enabled = !!(dwc_pwm_readl(dwc,
				DWC_TIM_CTRL(pwm->hwpwm)) & DWC_TIM_CTRL_EN);

	duty = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT(pwm->hwpwm));
	duty += 1;
	duty *= DWC_CLK_PERIOD_NS;
	state->duty_cycle = duty;

	period = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT2(pwm->hwpwm));
	period += 1;
	period *= DWC_CLK_PERIOD_NS;
	period += duty;
	state->period = period;

	state->polarity = PWM_POLARITY_INVERSED;

	pm_runtime_put_sync(chip->dev);

	return 0;
}

static const struct pwm_ops dwc_pwm_ops = {
	.apply = dwc_pwm_apply,
	.get_state = dwc_pwm_get_state,
	.owner = THIS_MODULE,
};

static struct dwc_pwm *dwc_pwm_alloc(struct device *dev)
{
	struct dwc_pwm *dwc;

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);
	if (!dwc)
		return NULL;

	dwc->chip.dev = dev;
	dwc->chip.ops = &dwc_pwm_ops;
	dwc->chip.npwm = DWC_TIMERS_TOTAL;

	dev_set_drvdata(dev, dwc);
	return dwc;
}

static int dwc_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dwc_pwm *dwc;
	int ret;

	dwc = dwc_pwm_alloc(dev);
	if (!dwc)
		return -ENOMEM;

	dwc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dwc->base))
		return PTR_ERR(dwc->base);

	dwc->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dwc->clk))
		return PTR_ERR(dwc->clk);

	ret = clk_prepare_enable(dwc->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	dwc->rst = devm_reset_control_get_optional(&pdev->dev, "rst");
	if (IS_ERR(dwc->rst))
		return PTR_ERR(dwc->rst);

	ret = reset_control_deassert(dwc->rst);
	if (ret) {
		dev_err(&pdev->dev, "failed to deasser reset: %d\n", ret);
		return ret;
	}


	ret = devm_pwmchip_add(dev, &dwc->chip);
	if (ret)
		return ret;

	pm_runtime_put(dev);
	pm_runtime_allow(dev);

	return 0;
}

static int dwc_pwm_remove(struct platform_device *pdev)
{
	struct dwc_pwm *dwc = platform_get_drvdata(pdev);
	pwmchip_remove(&dwc->chip);
	clk_disable_unprepare(dwc->clk);
	reset_control_assert(dwc->rst);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc_pwm_suspend(struct device *dev)
{
	struct dwc_pwm *dwc = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		if (dwc->chip.pwms[i].state.enabled) {
			dev_err(dev, "PWM %u in use by consumer (%s)\n",
				i, dwc->chip.pwms[i].label);
			return -EBUSY;
		}
		dwc->ctx[i].cnt = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT(i));
		dwc->ctx[i].cnt2 = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT2(i));
		dwc->ctx[i].ctrl = dwc_pwm_readl(dwc, DWC_TIM_CTRL(i));
	}

	return 0;
}

static int dwc_pwm_resume(struct device *dev)
{
	struct dwc_pwm *dwc = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < DWC_TIMERS_TOTAL; i++) {
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt, DWC_TIM_LD_CNT(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].cnt2, DWC_TIM_LD_CNT2(i));
		dwc_pwm_writel(dwc, dwc->ctx[i].ctrl, DWC_TIM_CTRL(i));
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dwc_pwm_pm_ops, dwc_pwm_suspend, dwc_pwm_resume);

static const struct of_device_id dwc_pwm_id_table[] = {
	{ .compatible = "eswin,pwm-eswin", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc_pwm_id_table);

static struct platform_driver dwc_pwm_driver = {
	.probe = dwc_pwm_probe,
	.remove = dwc_pwm_remove,
	.driver = {
		.name	= "dwc-pwm",
		.pm = &dwc_pwm_pm_ops,
		.of_match_table = of_match_ptr(dwc_pwm_id_table),
	},
};

module_platform_driver(dwc_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xuxiang <xuxiang@eswincomputing.com>");
MODULE_DESCRIPTION("DesignWare PWM Controller");
