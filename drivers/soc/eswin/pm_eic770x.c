// SPDX-License-Identifier: GPL-2.0
/*
 * Power Magagement Driver For ESWIN EIC770x SOC
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
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <asm/sbi.h>
#include <soc/eswin/eswin-lpcpu.h>

bool eic770x_system_support_ddr_self_refresh(void)
{
	if (!sbi_spec_is_0_1() && sbi_probe_extension(SBI_EXT_SUSP) > 0) {
#ifdef CONFIG_ESWIN_LPCPU
		return true;
#else
		return false;
#endif
	} else {
		return false;
	}
}

/*
 When entering self-refresh, only devices supported by the LPCPU can act as wake-up sources;
 otherwise, the MCPU will wake up prematurely, leading to a system crash.
*/
bool eic770x_system_device_is_wakeup_capable(struct device *dev)
{
	bool support;
	bool ddr_self_refresh;

	// If DDR self-refresh is not supported, return true
	if (!eic770x_system_support_ddr_self_refresh()) {
		ddr_self_refresh = false;
		support = true;
	} else {
		ddr_self_refresh = true;
		// Check if the device's device tree node has the "wakeup-source" property
		if (dev->of_node && of_property_read_bool(dev->of_node, "wakeup-source")) {
			support = true;
		} else {
			// Default return false if the property is not present
			support = false;
		}
	}
	if (true == support)
		pr_info("ddr self refresh %s, device %s %s to be wakeup source\n",
			ddr_self_refresh == true ? "enabled" : "disabled", dev_name(dev),
			support == true ? "support" : "not support");
	return support;
}
EXPORT_SYMBOL(eic770x_system_device_is_wakeup_capable);

static int eic770x_system_suspend(unsigned long sleep_type,
			      unsigned long resume_addr,
			      unsigned long opaque)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_SUSP, SBI_EXT_SUSP_SYSTEM_SUSPEND,
			sleep_type, resume_addr, opaque, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);

	return ret.value;
}

#ifdef CONFIG_ESWIN_LPCPU
static int eic770x_lpcpu_notify(int fid)
{
	int ret = 0;
	ret = eswin_lpcpu_service_ctl(fid);
	return ret;
}
#endif

static int eic770x_system_suspend_enter(suspend_state_t state)
{
	/*
		Add codes that need to be performed for pm_enter. such as:
			1.map the wakeup irq to mailbox box irq
			2.notify the lpcpu to enter self refresh
	*/
#ifdef CONFIG_ESWIN_LPCPU
	int ret;
	ret = eic770x_lpcpu_notify(PM_SUSPEND_MEM_ENTER);
	if (ret < 0) {
		pr_err("Failed to notify lpcpu to enter self refresh, ret %d\n", ret);
		return ret;
	} else {
		pr_info("Notify lpcpu to enter self refresh\n");
	}
#endif

	return cpu_suspend(SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM, eic770x_system_suspend);
}

static const struct platform_suspend_ops eic770x_system_suspend_ops = {
	.valid = suspend_valid_only_mem,
	.enter = eic770x_system_suspend_enter,
};

static int __init eic770x_system_suspend_init(void)
{
	if (!sbi_spec_is_0_1() && sbi_probe_extension(SBI_EXT_SUSP) > 0) {
		pr_info("SBI SUSP extension detected\n");
		if (IS_ENABLED(CONFIG_SUSPEND))
			suspend_set_ops(&eic770x_system_suspend_ops);
	} else {
		pr_info("SBI SUSP extension not detected\n");
	}

	return 0;
}

arch_initcall(eic770x_system_suspend_init);
