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
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <asm/sbi.h>

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

static int eic770x_system_suspend_enter(suspend_state_t state)
{
	int ret;

	pr_info("[%s %d]:\n",__func__,__LINE__);
	/*
		/*
		Add codes that need to be performed for pm_enter. such as:
			1.map the wakeup irq to mailbox box irq
			2.notify the lpcpu to enter self refresh

		ret = eic770x_lpcpu_notify(data, PM_SUSPEND_MEM_ENTER);
		if (ret) {
			pr_err("[%s %d]:\n",__func__,__LINE__, ret);
			return ret
		}
	*/
	return cpu_suspend(SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM, eic770x_system_suspend);
}

static const struct platform_suspend_ops eic770x_system_suspend_ops = {
	.valid = suspend_valid_only_mem,
	.enter = eic770x_system_suspend_enter,
};

static int __init eic770x_system_suspend_init(void)
{
	pr_info("[%s %d]:\n",__func__,__LINE__);
	if (!sbi_spec_is_0_1() && sbi_probe_extension(SBI_EXT_SUSP) > 0) {
		pr_info("SBI SUSP extension detected\n");
		if (IS_ENABLED(CONFIG_SUSPEND))
			suspend_set_ops(&eic770x_system_suspend_ops);
	}

	return 0;
}

arch_initcall(eic770x_system_suspend_init);
