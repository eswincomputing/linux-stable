/*
 * Copyright (c) 2025-2026 Eswin, Inc. All Rights Reserved.
 *
 * This software is the confidential and proprietary information of
 * Eswin, Inc. ("Confidential Information"). You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered into
 * with Eswin.
 *
 * ESWIN MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE SUITABILITY OF
 * THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, OR NON-INFRINGEMENT. ESWIN SHALL NOT BE LIABLE FOR
 * ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING OR
 * DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysrq.h>
#include <linux/reboot.h>
#include <linux/kexec.h>

extern struct resource crashk_res;       // Crash kernel reserved mem

#define LONG_PRESS_TIMEOUT_SEC 8  // long press timestamp duration（s）
#define LONG_PRESS_TIMEOUT (LONG_PRESS_TIMEOUT_SEC * HZ)

static struct timer_list reset_timer;
static bool reset_pressed = false;
static int reset_key_code = KEY_ESC;

static bool is_crashkernel_reserved(void)
{
    // check Crash kernel reserved mem
    if (crashk_res.start != crashk_res.end) {
        // printk(KERN_DEBUG "Crash kernel reserved: %llx-%llx\n",
        //        (unsigned long long)crashk_res.start,
        //        (unsigned long long)crashk_res.end);
        return true;
    }

    return false;
}

static void reset_timer_callback(struct timer_list *t) {
    if (reset_pressed) {
        printk(KERN_EMERG "Reset button long press detected, triggering kdump...\n");

        emergency_sync();
        // emergency_restart();

        reset_pressed = false;
        panic("Reset button long pressed: triggering kernel panic for kdump");
    }
}

static void reset_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    if (type != EV_KEY)
        return;

    if (code != reset_key_code)
        return;

    if (value == 1) {
        if (!reset_pressed) {
            reset_pressed = true;
            mod_timer(&reset_timer, jiffies + LONG_PRESS_TIMEOUT);
            printk(KERN_INFO "Reset button pressed, starting long press detection\n");
        }
    } else if (value == 0) {
        if (reset_pressed) {
            reset_pressed = false;
            del_timer(&reset_timer);
            printk(KERN_INFO "Reset button released, long press canceled\n");
        }
    }
}

static int reset_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;
    int error;

    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "reset_kdump_handler";

    error = input_register_handle(handle);
    if (error)
        goto err_free_handle;

    error = input_open_device(handle);
    if (error)
        goto err_unregister_handle;

    return 0;

err_unregister_handle:
    input_unregister_handle(handle);
err_free_handle:
    kfree(handle);
    return error;
}

static void reset_disconnect(struct input_handle *handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id reset_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT_MASK(EV_KEY) },
    },
    { },
};
MODULE_DEVICE_TABLE(input, reset_ids);

static struct input_handler reset_handler = {
    .event      = reset_event,
    .connect    = reset_connect,
    .disconnect = reset_disconnect,
    .name       = "reset_kdump_handler",
    .id_table   = reset_ids,
};

static int __init reset_kdump_init(void) {
    int error;

    if (!is_crashkernel_reserved()) {
        return 0;
    }
    timer_setup(&reset_timer, reset_timer_callback, 0);
    error = input_register_handler(&reset_handler);
    if (error) {
        printk(KERN_ERR "Failed to register reset input handler: %d\n", error);
        return error;
    }

    printk(KERN_INFO "Reset kdump module loaded. Long press Reset button for %d seconds to trigger kdump.\n",
           LONG_PRESS_TIMEOUT_SEC);
    return 0;
}

static void __exit reset_kdump_exit(void) {
    if (!is_crashkernel_reserved()) {
        return;
    }
    del_timer_sync(&reset_timer);
    input_unregister_handler(&reset_handler);
    printk(KERN_INFO "Reset kdump module unloaded.\n");
}

module_init(reset_kdump_init);
module_exit(reset_kdump_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Eswin Long press Reset button to trigger kdump");
MODULE_AUTHOR("chenzhanzhan@eswincomputing.com");