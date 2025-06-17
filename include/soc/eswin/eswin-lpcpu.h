/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_ESWIN_LPCPU_H_
#define _LINUX_ESWIN_LPCPU_H_

#include <linux/device.h>

enum lpcpu_ctl_fid {
	PM_SUSPEND_MEM_ENTER = 0,
};

int eswin_lpcpu_service_ctl(int fid);

bool eic770x_system_device_is_wakeup_capable(struct device *dev);

#endif /* _LINUX_ESWIN_LPCPU_H_ */
