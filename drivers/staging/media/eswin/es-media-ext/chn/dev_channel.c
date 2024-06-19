#include "dev_channel.h"
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/atomic.h>
#include "es_media_ext_drv.h"
#include "class_chn_mgr.h"

typedef struct {
    channel_t chn;
    class_chn_mgr_t *pmgr;
} channel_data_t;

static class_chn_mgr_t *chn_mgr[MAX_CHN];

static long dev_channel_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int dev_channel_open(struct inode *inode, struct file *filp);
static int dev_channel_release(struct inode *inode, struct file *filp);
static unsigned int dev_channel_poll(struct file *filp, poll_table *wait);
static ssize_t dev_channel_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t dev_channel_write(struct file *filp, const char *buf, size_t len, loff_t *off);

static long dev_channel_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    channel_data_t *chn_data = (channel_data_t *)filp->private_data;
    if (!chn_data || !arg) {
        return EFAULT;
    }

    if (_IOC_TYPE(cmd) != ES_IOC_MAGIC_C || _IOC_NR(cmd) >= ES_CHN_IOC_MAX_NR) {
        pr_info("channel - invalid cmd %u\n", cmd);
        return ENOTTY;
    }

    switch (cmd) {
        case ES_CHN_IOC_COUNT_ADD: {
            int count = 0;
            __u32 val;
            __get_user(val, (__u32 __user *)arg);

            if (chn_data->pmgr) {
                count = chn_data->pmgr->add_count(chn_data->pmgr, &chn_data->chn, val);
            } else {
                pr_warn(
                    "add - [%u, %u]chn_mgr has not been initialized yet\n", chn_data->chn.group, chn_data->chn.channel);
            }
            __put_user(count, (__u32 __user *)arg);
        } break;
        case ES_CHN_IOC_COUNT_SUB: {
            int count = 0;
            __u32 val;
            __get_user(val, (__u32 __user *)arg);

            if (chn_data->pmgr) {
                count = chn_data->pmgr->sub_count(chn_data->pmgr, &chn_data->chn, val);
            } else {
                pr_warn(
                    "sub - [%u, %u]chn_mgr has not been initialized yet\n", chn_data->chn.group, chn_data->chn.channel);
            }
            __put_user(count, (__u32 __user *)arg);
        } break;
        case ES_CHN_IOC_COUNT_GET: {
            int count = 0;
            if (chn_data->pmgr) {
                count = chn_data->pmgr->get_count(chn_data->pmgr, &chn_data->chn);
            } else {
                pr_warn(
                    "get - [%u, %u]chn_mgr has not been initialized yet\n", chn_data->chn.group, chn_data->chn.channel);
            }
            __put_user(count, (__u32 __user *)arg);
        } break;
        case ES_CHN_IOC_ASSIGN_CHANNEL: {
            channel_t chn;
            long tmp = 0;
            tmp = copy_from_user(&chn, (void __user *)arg, sizeof(channel_t));
            if (tmp) {
                pr_err("channel-copy_from_user failed, returned %li\n", tmp);
                return -EFAULT;
            }

            if (chn_data->pmgr && chn_data->pmgr->assign_chnl(chn_data->pmgr, &chn) > 0) {
                chn_data->chn.group = chn.group;
                chn_data->chn.channel = chn.channel;
            } else {
                pr_warn("assign channel - [%u, %u]chn_mgr has not been initialized yet\n",
                        chn_data->chn.group,
                        chn_data->chn.channel);
            }

            return 0;
        } break;
        case ES_CHN_IOC_UNASSIGN_CHANNEL: {
            channel_t chn;
            long tmp = 0;
            tmp = copy_from_user(&chn, (void __user *)arg, sizeof(channel_t));
            if (tmp) {
                pr_err("channel-copy_from_user failed, returned %li\n", tmp);
                return -EFAULT;
            }

            if (chn_data->pmgr && chn_data->pmgr->unassign_chnl(chn_data->pmgr, &chn) > 0) {
                chn_data->chn.group = INVALID_GROUP;
                chn_data->chn.channel = -1;
            } else {
                pr_warn("unassign channel - [%u, %u]chn_mgr has not been initialized yet\n",
                        chn_data->chn.group,
                        chn_data->chn.channel);
            }

            return 0;
        } break;
        case ES_CHN_IOC_WAKEUP_COUNT_SET: {
            __u32 val;
            __get_user(val, (__u32 __user *)arg);

            if (val == 0 || !chn_data->pmgr || chn_data->pmgr->set_wakeup_count(chn_data->pmgr, &chn_data->chn, val)) {
                pr_warn(
                    "set wake up count %u failed, chnInfo[%u, %u]\n", val, chn_data->chn.group, chn_data->chn.channel);
            }
        } break;
        case ES_CHN_IOC_WAKEUP_COUNT_GET: {
            int count = 0;

            if (chn_data->pmgr) {
                count = chn_data->pmgr->get_wakeup_count(chn_data->pmgr, &chn_data->chn);
            } else {
                pr_warn("get wake up count - [%u, %u]chn_mgr has not been initialized yet\n",
                        chn_data->chn.group,
                        chn_data->chn.channel);
            }
            __put_user(count, (__u32 __user *)arg);
        } break;
        default:
            pr_warn("channel-default: unhandled cmd %u\n", cmd);
            break;
    }

    return 0;
}

static int dev_channel_open(struct inode *inode, struct file *filp) {
    int minor = 0;
    int index = -1;
    channel_data_t *data = NULL;

    minor = MINOR(inode->i_rdev);
    index = dev_minor_to_chn_index(minor);
    if (index < 0 || index >= MAX_CHN) {
        pr_warn("channel open minor %d failed\n", minor);
        return -ENODEV;
    }
    if (!chn_mgr[index]) {
        pr_warn("channel mgr index %d has not been initialized yet.", index);
        return -EFAULT;
    }

    data = vmalloc(sizeof(channel_data_t));
    if (data) {
        data->chn.group = INVALID_GROUP;
        data->chn.channel = -1;
        data->pmgr = chn_mgr[index];
        filp->private_data = data;

        return 0;
    } else {
        pr_err("channel open-vmalloc failed, %d\n", minor);
        return ENOMEM;
    }
}

static int dev_channel_release(struct inode *inode, struct file *filp) {
    channel_data_t *data = (channel_data_t *)filp->private_data;
    if (data) {
        if (data->pmgr) {
            data->pmgr->unassign_chnl(data->pmgr, &data->chn);
        }

        vfree(filp->private_data);
        filp->private_data = NULL;
    }

    return 0;
}

static unsigned int dev_channel_poll(struct file *filp, poll_table *wait) {
    channel_data_t *data = (channel_data_t *)filp->private_data;
    unsigned int mask = 0;
    wait_queue_head_t *wait_queue = NULL;

    if (data->pmgr) {
        wait_queue = data->pmgr->get_wait_queue_head(data->pmgr, &data->chn);
        if (wait_queue) {
            poll_wait(filp, wait_queue, wait);
            if (data->pmgr->get_count(data->pmgr, &data->chn) >= data->pmgr->get_wakeup_count(data->pmgr, &data->chn)) {
                mask |= POLLIN | POLLRDNORM; /* readable */
            }
        }
    }

    return mask;
}

static ssize_t dev_channel_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    return -1;
}

static ssize_t dev_channel_write(struct file *filp, const char *buf, size_t len, loff_t *off) {
    return -1;
}

static struct file_operations fops_channel = {
    .owner = THIS_MODULE,
    .open = dev_channel_open,
    .release = dev_channel_release,
    .read = dev_channel_read,
    .write = dev_channel_write,
    .poll = dev_channel_poll,
    .unlocked_ioctl = dev_channel_ioctl,
};

void dev_channel_init_cdec(void *dev) {
    struct cdev *channel_cdev = (struct cdev *)dev;
    cdev_init(channel_cdev, &fops_channel);
}

int dev_channel_init(dev_minor_t minor) {
    int index = dev_minor_to_chn_index(minor);
    if (index < 0 || index >= MAX_CHN) {
        pr_warn("channel init minor %d failed\n", minor);
        return -1;
    }

    chn_mgr[index] = init_chn_mgr_inst(minor);
    if (!chn_mgr[index]) {
        pr_err("init dev chn for minor %d failed\n", minor);
        return -1;
    }

    pr_info("init dev channel succeed, minor %d\n", minor);
    return 0;
}

void dev_channel_deinit(void) {
    int i = 0;

    pr_info("dev channel - start deinit\n");
    for (; i < MAX_CHN; i++) {
        if (chn_mgr[i]) {
            deinit_chn_mgr_inst(chn_mgr[i]);
        }
        chn_mgr[i] = NULL;
    }

    pr_info("dev channel - deinit succeed\n");
}
