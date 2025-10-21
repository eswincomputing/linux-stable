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
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/mailbox_controller.h>

#define LPCPU_FW_LOAD_UNKNOW 0
#define LPCPU_FW_LOAD_SUCC   0xacce55
#define LPCPU_FW_WDT_INIT    0x94e445
#define LPCPU_FW_WDT_ENABLE  0x54e414
#define LPCPU_FW_WDT_PING    0x0594e4
#define LPCPU_FW_WDT_DISABLE 0x863738

#define LPCPU_FW_WDT_TIMEOUT 0x425945

struct esw_wdt {
    struct watchdog_device wdd;
    struct mbox_chan *mbox_channel;
    struct mutex lock;
    wait_queue_head_t waitq;
    unsigned int numa_id;
    unsigned int timeout;        /* 当前超时值(sec) */
    unsigned int max_timeout;    /* 最大超时值(sec) */
};

enum esw_wdt_cmd {
    WDT_INIT = 1,
    WDT_ENABLE = 2,
    WDT_PING = 3,
    WDT_DISABLE = 4,
};

static char* wdt_cmd_str[4] = {"WDT_INIT", "WDT_ENABLE", "WDT_PING", "WDT_DISABLE"};

struct mbox_msg {
    u32 data_l;
    u32 data_h;
};

static u32 load_event = LPCPU_FW_LOAD_UNKNOW;
static struct esw_wdt *esw_wdt_inst = NULL;

#ifdef CONFIG_KEXEC_CORE
extern struct resource crashk_res;       // Crash kernel reserved mem
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
#endif

static void eswin_wdt_rx_callback(struct mbox_client *client, void *msg) {
    struct mbox_msg *umsg = msg;
    struct device *dev = client->dev;
    dev_dbg(dev, "eswin_wdt_rx_callback : 0x%llx\n", *(u64 *)msg);
    dev_dbg(dev, "data_l= 0x%x, data_h = 0x%x\n", umsg->data_l, umsg->data_h);
    load_event = *(u32 *)msg;
    if (load_event != LPCPU_FW_WDT_TIMEOUT) {
        if (esw_wdt_inst != NULL) {
            wake_up(&esw_wdt_inst->waitq);
        }
        dev_dbg(dev, "eswin_wdt_rx_callback returned\n");
    } else if (load_event == LPCPU_FW_WDT_TIMEOUT) {
        dev_info(dev, "eswin_wdt_rx_callback: wdt timeout\n");
        #ifdef CONFIG_KEXEC_CORE
        bool reset_panic = false;
        reset_panic = is_crashkernel_reserved();
        if (reset_panic) {
            dev_info(dev, "eswin_wdt_rx_callback: wdt timeout, bye...\n");
            emergency_sync();
            panic("Eswin wdt trigger: triggering kernel panic for kdump");
        }
        #endif
    }
    return;
}

static void eswin_wdt_tx_done(struct mbox_client *client, void *msg, int r) {
    if (r) {
        dev_warn(client->dev, "Client: Message(0x%llx) could not be sent : %d\n", *(u64 *)msg, r);
    } else {
        dev_dbg(client->dev, "Client: Message(0x%llx) sent\n", *(u64 *)msg);
    }
}

static int wdt_send_message(struct mbox_chan *mbox_channel, u8 *msg)
{
	int ret;
	ret = mbox_send_message(mbox_channel, msg);
	if (ret < 0){
		ret = -EAGAIN;
		printk("Failed to send message via mailbox\r\n");
		return ret;
	}
	if (mbox_channel->txdone_method & BIT(2)/*TXDONE_BY_ACK*/)
		mbox_client_txdone(mbox_channel, 0);

	return 0;
}

static int lpcpu_boot_status(struct mbox_chan *mbox_channel) {
    int ret = 0;
    u8 msg[8];
    msg[0] = 0xca;
    msg[1] = 0xec;
    msg[2] = 0x55;

    ret = wdt_send_message(mbox_channel, msg);
    return ret;
}

static int esw_wdt_call(struct watchdog_device *wdd, enum esw_wdt_cmd call,
                        unsigned long arg) {
    int ret = 0;
    u8 msg[8];
    struct esw_wdt *wdt = watchdog_get_drvdata(wdd);
    struct mbox_chan *mbox_channel = wdt->mbox_channel;
    u32 reply_event = LPCPU_FW_LOAD_UNKNOW;
    unsigned int timeout = 0;
    timeout = arg;
    if (mbox_channel == NULL) {
        return -1;
    }
    switch (call) {
        case WDT_INIT: // INT
            msg[0] = 0x49;
            msg[1] = 0x4E;
            msg[2] = 0x54;
            msg[3] = 0x0;
            msg[4] = timeout;
            reply_event = LPCPU_FW_WDT_INIT;
            break;
        case WDT_ENABLE: // ENA
            msg[0] = 0x45;
            msg[1] = 0x4E;
            msg[2] = 0x41;
            reply_event = LPCPU_FW_WDT_ENABLE;
            break;
        case WDT_PING: // PIN
            msg[0] = 0x50;
            msg[1] = 0x49;
            msg[2] = 0x4E;
            reply_event = LPCPU_FW_WDT_PING;
            break;
        case WDT_DISABLE: // DIS
            msg[0] = 0x68;
            msg[1] = 0x73;
            msg[2] = 0x83;
            reply_event = LPCPU_FW_WDT_DISABLE;
            break;
        default:
            return -1;
    }

    // printk("esw_wdt_call: wdt_send_message (call = %d)(%s)\n", call, wdt_cmd_str[call - 1]);
    ret = wdt_send_message(mbox_channel, msg);
    if (ret < 0) {
        ret = -EAGAIN;
        return ret;
    }

    long event_timeout = wait_event_timeout(wdt->waitq, load_event == reply_event,
                                        usecs_to_jiffies(200000));

    if (!event_timeout) {
        ret = -EBUSY;
        printk("esw_wdt_call: wdt_send_message (call = %d)(%s) reply_event timeout\n", call, wdt_cmd_str[call - 1]);
        // return ret;
    }
    return 0;
}

static inline struct esw_wdt *to_esw_wdt(struct watchdog_device *wdd) {
    return container_of(wdd, struct esw_wdt, wdd);
}

static int esw_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout) {
    int ret = 0;
    struct esw_wdt *wdt = to_esw_wdt(wdd);

    if (timeout < 1 || timeout > wdt->max_timeout)
        return -EINVAL;

    wdt->timeout = timeout;
    wdd->timeout = timeout;
    printk("esw_wdt_set_timeout: timeout = %d\n", timeout);
    ret = esw_wdt_call(wdd, WDT_INIT, wdt->timeout);
    return ret;
}

static int esw_wdt_start(struct watchdog_device *wdd) {
    int ret = 0;
    // struct esw_wdt *wdt = to_esw_wdt(wdd);
    // printk("esw_wdt_start\n");
    ret = esw_wdt_call(wdd, WDT_ENABLE, 0);
    return ret;
}

static int esw_wdt_stop(struct watchdog_device *wdd) {
    int ret = 0;
    // struct esw_wdt *wdt = to_esw_wdt(wdd);
    // printk("esw_wdt_stop\n");
    ret = esw_wdt_call(wdd, WDT_DISABLE, 0);
    return ret;
}

static int esw_wdt_ping(struct watchdog_device *wdd) {
    int ret = 0;
    // struct esw_wdt *wdt = to_esw_wdt(wdd);
    // printk("esw_wdt_ping\n");
    ret = esw_wdt_call(wdd, WDT_PING, 0);
    return ret;
}

static const struct watchdog_ops esw_wdt_ops = {
    .owner = THIS_MODULE,
    .start = esw_wdt_start,
    .stop = esw_wdt_stop,
    .ping = esw_wdt_ping,
    .set_timeout = esw_wdt_set_timeout,
};

static const struct watchdog_info esw_wdt_info = {
    .identity = "Eswin Watchdog Timer",
    .options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static struct mbox_chan *eswin_wdt_request_channel(struct platform_device *pdev,
                                                    const char *name) {
    struct mbox_client *client;
    struct mbox_chan *channel;

    client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
    if (!client)
        return NULL;

    client->dev = &pdev->dev;
    client->rx_callback = eswin_wdt_rx_callback;
    client->tx_prepare = NULL;
    client->tx_done = eswin_wdt_tx_done;
    client->tx_block = false;
    client->knows_txdone = false;

    channel = mbox_request_channel_byname(client, name);
    if (IS_ERR(channel)) {
        dev_warn(&pdev->dev, "failed to request %s channel\n", name);
        return NULL;
    }
    dev_info(&pdev->dev, "request mbox chan %s\n", name);

    return channel;
}

static int my_panic_handler(struct notifier_block *this,
                             unsigned long event, void *ptr)
{
    if (esw_wdt_inst == NULL) {
        return 0;
    }
    pr_emerg("Panic! disabling esw_wdt...\n");
    esw_wdt_stop(&esw_wdt_inst->wdd);
    return NOTIFY_DONE;
}

static struct notifier_block my_panic_block = {
    .notifier_call = my_panic_handler,
    .priority = INT_MAX,
};

static int esw_wdt_probe(struct platform_device *pdev) {
    struct esw_wdt *wdt;
    const char *mbox_channel_name;
    unsigned int numa_id = 0;
    u32 max_timeout;
    int ret = 0;

    wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
    if (!wdt)
        return -ENOMEM;

    if (of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
        numa_id = 0;
    }
    dev_info(&pdev->dev, "numa_id = %d\n", numa_id);
    wdt->numa_id = numa_id;

    /* get max-timeout-sec from DTS */
    of_property_read_u32(pdev->dev.of_node, "max-timeout-sec", &max_timeout);
    wdt->max_timeout = max_timeout ? max_timeout : 60;

    wdt->wdd.info = &esw_wdt_info;
    wdt->wdd.ops = &esw_wdt_ops;
    wdt->wdd.min_timeout = 1;
    wdt->wdd.max_timeout = wdt->max_timeout;
    wdt->wdd.timeout = 30;  /* default: 30 sec */
    wdt->timeout = wdt->wdd.timeout;

    watchdog_set_drvdata(&wdt->wdd, wdt);

    /*use eswin mailbox0 to send msg and mailbox1 receive msg*/
    ret = device_property_read_string(&pdev->dev, "mbox-names",
                        &mbox_channel_name);
    if (ret == 0) {
        wdt->mbox_channel = eswin_wdt_request_channel(pdev, mbox_channel_name);
        if (wdt->mbox_channel == NULL) {
            dev_err(&pdev->dev, "eswin_wdt_request_channel: %s fail\n", mbox_channel_name);
            ret = -EBUSY;
            goto err_misc;
        }
    } else {
        dev_err(&pdev->dev, "given arguments are not valid: %d\n", ret);
        goto err_misc;
    }

    ret = devm_watchdog_register_device(&pdev->dev, &wdt->wdd);
    if (ret) {
        dev_err(&pdev->dev, "failed to register watchdog device\n");
        goto err_mbox;
    }

    mutex_init(&wdt->lock);
    init_waitqueue_head(&wdt->waitq);

    platform_set_drvdata(pdev, wdt);
    esw_wdt_inst = wdt;

    ret = lpcpu_boot_status(wdt->mbox_channel);
    if (ret < 0) {
        dev_err(&pdev->dev, "send message to lpcpu via mailbox failed!\n");
        goto err_mbox;
    }
    long timeout = wait_event_timeout(wdt->waitq, load_event == LPCPU_FW_LOAD_SUCC,
                                        usecs_to_jiffies(100000));

    if (!timeout) {
        dev_err(&pdev->dev, "lpcpu is not boot!\n");
        ret = -EBUSY;
        goto err_mbox;
    }

    atomic_notifier_chain_register(&panic_notifier_list, &my_panic_block);

    dev_info(&pdev->dev, "Eswin watchdog driver initialized (timeout=%d sec, max_timeout=%d sec)\n",
                wdt->timeout, wdt->max_timeout);
    return 0;
err_mbox:
    mbox_free_channel(wdt->mbox_channel);
err_misc:
    devm_kfree(&pdev->dev, wdt);
    return ret;
}

static int esw_wdt_remove(struct platform_device *pdev) {
    struct esw_wdt *wdt = platform_get_drvdata(pdev);
    if (wdt == NULL) {
        return 0;
    }
    esw_wdt_stop(&wdt->wdd);

    atomic_notifier_chain_unregister(&panic_notifier_list, &my_panic_block);
    dev_info(&pdev->dev, "Eswin watchdog driver removed\n");
    return 0;
}

static void esw_wdt_shutdown(struct platform_device *pdev)
{
    struct esw_wdt *wdt = platform_get_drvdata(pdev);
    if (wdt == NULL) {
        return;
    }
    dev_info(&pdev->dev, "Eswin watchdog driver esw_wdt_shutdown (timeout=%d sec, max_timeout=%d sec)\n", wdt->timeout, wdt->max_timeout);
    esw_wdt_stop(&wdt->wdd);
    return;
}

static int esw_wdt_suspend(struct device *dev)
{
    struct esw_wdt *wdt = dev_get_drvdata(dev);
    if (wdt == NULL) {
        return 0;
    }
    dev_info(dev, "Eswin watchdog driver esw_wdt_suspend (timeout=%d sec, max_timeout=%d sec)\n", wdt->timeout, wdt->max_timeout);
    esw_wdt_stop(&wdt->wdd);
    return 0;
}

static int esw_wdt_resume(struct device *dev)
{
    struct esw_wdt *wdt = dev_get_drvdata(dev);
    if (wdt == NULL) {
        return 0;
    }
    dev_info(dev, "Eswin watchdog driver esw_wdt_resume (timeout=%d sec, max_timeout=%d sec)\n", wdt->timeout, wdt->max_timeout);

    esw_wdt_start(&wdt->wdd);
    esw_wdt_set_timeout(&wdt->wdd, wdt->timeout);
    esw_wdt_ping(&wdt->wdd);
    return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(esw_wdt_pm_ops, esw_wdt_suspend, esw_wdt_resume);

static const struct of_device_id esw_wdt_of_match[] = {
    { .compatible = "eswin,win2030-wdt" },
    { /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, esw_wdt_of_match);

static struct platform_driver esw_wdt_driver = {
    .probe = esw_wdt_probe,
    .remove = esw_wdt_remove,
    .shutdown = esw_wdt_shutdown,
    .driver = {
        .name = "win2030-wdt",
        .of_match_table = esw_wdt_of_match,
        .pm	= pm_ptr(&esw_wdt_pm_ops),
    },
};
module_platform_driver(esw_wdt_driver);

MODULE_AUTHOR("Chen ZhanZhan <chenzhanzhan@eswincomputing.com>");
MODULE_DESCRIPTION("Eswin WIN2030 Watchdog Driver");
MODULE_LICENSE("GPL v2");
