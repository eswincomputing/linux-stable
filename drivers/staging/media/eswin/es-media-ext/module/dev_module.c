#include "dev_module.h"
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include "es_media_ext_drv.h"
#include "dev_proc.h"
#include "class_module.h"

static class_module_t *module_data[MAX_MOD];

static int dev_module_open(struct inode *inode, struct file *filp);
static int dev_module_release(struct inode *inode, struct file *filp);
static long dev_module_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static unsigned int dev_module_poll(struct file *filp, poll_table *wait);

static module_private_data_t* malloc_private_data(class_module_t *module, int minor, pid_t pid);

//ioctl
static long on_get_event(class_event_t *event_inst, unsigned long arg);

static int dev_module_open(struct inode *inode, struct file *filp) {
    int minor = 0;
    int index = -1;
    module_private_data_t *data = NULL;
    pid_t pid = current->pid;

    minor = MINOR(inode->i_rdev);
    index = dev_minor_to_module_index(minor);
    if (index < 0 || index >= MAX_MOD) {
        pr_warn("module open minor %d failed\n", minor);
        return -ENODEV;
    }
    if (!module_data[index]) {
        pr_warn("module %d has not been initialized yet.", index);
        return -EFAULT;
    }

    // malloc event class
    data = malloc_private_data(module_data[index], minor, pid);
    if (!data) {
        return -ENOMEM;
    }
    filp->private_data = data;

    return module_data[index]->add(module_data[index], filp, pid);
}

static int dev_module_release(struct inode *inode, struct file *filp) {
    module_private_data_t *data = (module_private_data_t*)filp->private_data;

    if (data && data->module) {
        pr_debug("close module %px, pid %d\n", data->module, data->pid);
        data->module->del(data->module, data->pid);

        deinit_class_event(data->event);
        vfree(data);
    }
    filp->private_data = NULL;

    return 0;
}

static module_private_data_t* malloc_private_data(class_module_t *module, int minor, pid_t pid) {
    module_private_data_t *data = NULL;

    data = vmalloc(sizeof(module_private_data_t));
    if (!data) {
        pr_err("malloc module private data failed\n");
        return NULL;
    }

    data->pid = pid;
    data->event = init_class_event(data->pid);
    if (!data->event) {
        vfree(data);
        return NULL;
    }
    data->module = module;
    data->minor = minor;

    return data;
}

static long on_get_event(class_event_t *event_inst, unsigned long arg) {
    module_event_list_t *item = NULL;
    long ret = 0;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);
    RETURN_VAL_IF_FAIL(event_inst, -EINVAL);

    item = event_inst->pop_event(event_inst);
    if (item) {
        ret = copy_to_user((es_module_event_t __user *)arg, &item->event,
                sizeof(es_module_event_t));
        vfree(item);
        if (ret) {
            pr_err("get event, copy_to_user failed, returned %li\n", ret);
            return -EFAULT;
        }
    }

    pr_debug("pid %d get event\n", event_inst->pid);
    return ret;
}

static long dev_module_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    module_private_data_t *data = (module_private_data_t*)filp->private_data;
    if (!data || !data->module) {
        return -ENOMEM;
    }

    if(_IOC_TYPE(cmd) != ES_IOC_MAGIC_M || _IOC_NR(cmd) >= ES_MOD_IOC_MAX_NR) {
        pr_info("module-invalid cmd %u\n", cmd);
        return ENOTTY;
    }

    pr_debug("module: received ioctl cmd %d\n", _IOC_NR(cmd));
    switch(cmd) {
    case ES_MOD_IOC_GET_EVENT:
        return on_get_event(data->event, arg);
    case ES_MOD_IOC_PROC_SEND_MODULE:
        return dev_proc_recv_module(arg, data->minor);
    case ES_MOD_IOC_PROC_SEND_GRP_TITLE:
        return dev_proc_recv_grp_title(arg, data->minor);
    case ES_MOD_IOC_PROC_SEND_GRP_DATA:
        return dev_proc_recv_grp_data(arg, data->minor);
    case ES_MOD_IOC_PUB_USER:
        data->module->del(data->module, data->pid);
        break;
    case ES_MOD_IOC_PROC_SET_SECTION:
        return dev_proc_recv_section(arg, data->minor);
    case ES_MOD_IOC_PROC_SET_TIMEOUT:
        return dev_proc_set_timeout(arg, data->minor);
    default:
        pr_warn("module-default: unhandled cmd %u\n", cmd);
        return -EINVAL;
    }

    return 0;
}

static unsigned int dev_module_poll(struct file *filp, poll_table *wait) {
    module_private_data_t *data = (module_private_data_t*)filp->private_data;
    unsigned int mask = 0;

    if (data && data->event) {
        poll_wait(filp, &data->event->wait_queue, wait);
        if (atomic_read(&data->event->event_count) > 0) {
            mask |= POLLIN | POLLRDNORM;	/* readable */
        }
    }

    return mask;
}

static struct file_operations m_fops_module =
{
    .owner          = THIS_MODULE,
    .open           = dev_module_open,
    .release        = dev_module_release,
    .poll           = dev_module_poll,
    .unlocked_ioctl = dev_module_ioctl,
};

void dev_module_init_cdec(void *dev) {
    struct cdev *module_cdev = (struct cdev*)dev;
    cdev_init(module_cdev, &m_fops_module);
}

int dev_module_init(dev_minor_t minor) {
    int index = dev_minor_to_module_index(minor);
    if (index < 0 || index >= MAX_MOD) {
        pr_warn("module init minor %d failed\n", minor);
        return -ENOMEM;
    }
    pr_debug("dev module init minor %d, index %d\n", minor, index);
    module_data[index] = init_class_module();
    if (!module_data[index]) {
        return -ENOMEM;
    }
    es_media_dev_proc_init(minor, module_data[index]);
    pr_debug("init dev module minor %d, index %d succeed\n", minor, index);
    return 0;
}

void dev_module_deinit(void) {
    int i = 0;
    pr_debug("dev module - start deinit\n");
    for (; i < MAX_MOD; i++) {
        if (module_data[i]) {
            deinit_class_module(module_data[i]);
            module_data[i] = NULL;
        }
    }

    dev_proc_deinit();
    pr_debug("dev module - deinit succeed\n");
}
