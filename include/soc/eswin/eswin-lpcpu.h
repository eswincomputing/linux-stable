/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 */

#ifndef _LINUX_ESWIN_LPCPU_H_
#define _LINUX_ESWIN_LPCPU_H_

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mailbox_client.h>

enum lpcpu_ctl_fid {
	PM_SUSPEND_MEM_ENTER = 0,
};

int eswin_lpcpu_service_ctl(int fid);

bool eic770x_system_device_is_wakeup_capable(struct device *dev);
struct mbox_chan *lpcpu_get_mboxchan(struct platform_device *pdev);
int lpcpu_send_message(struct mbox_chan *mbox_channel, u8 *msg);
#endif /* _LINUX_ESWIN_LPCPU_H_ */
