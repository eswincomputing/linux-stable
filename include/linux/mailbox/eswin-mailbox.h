/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_ESWIN_MAILBOX_H_
#define _LINUX_ESWIN_MAILBOX_H_

/**
 * struct eswin_mbox_msg - Eswin mailbox message structure
 * @data: message payload, only 63 bit valid
 *
 */
struct eswin_mbox_msg {
    u64 data;
};

#endif /* _LINUX_ESWIN_MAILBOX_H_ */
