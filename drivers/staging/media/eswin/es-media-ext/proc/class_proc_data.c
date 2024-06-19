#include "dev_common.h"
#include "class_proc_data.h"
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

static int class_write_module(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
static int class_write_title(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
static long class_write_data(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
static int class_read(struct _class_proc_data_t *inst, char __user *buf, size_t len, loff_t *off);
static void class_reset(struct _class_proc_data_t *inst);
static int class_is_timeout(struct _class_proc_data_t *inst);
static void class_set_module_count(struct _class_proc_data_t *inst, int count);
static int class_is_all_module_responsed(struct _class_proc_data_t *inst);
static int class_set_section(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);

////////////////////////////////////////////////////////
class_proc_data_t *init_class_proc_data(void) {
    class_proc_data_t *inst = vmalloc(sizeof(class_proc_data_t));
    if (!inst) {
        pr_err("vmalloc proc inst failed\n");
        return NULL;
    }
    memset(inst, 0, sizeof(class_proc_data_t));
    mutex_init(&inst->lock);
    init_waitqueue_head(&inst->wait.wait_q);

    inst->module = init_class_proc_module();
    inst->section_mgr = init_class_section_mgr();

    inst->write_module = class_write_module;
    inst->write_title = class_write_title;
    inst->write_data = class_write_data;
    inst->read = class_read;
    inst->set_section = class_set_section;
    inst->reset = class_reset;

    inst->is_timeout = class_is_timeout;
    inst->set_module_count = class_set_module_count;
    inst->is_all_module_responsed = class_is_all_module_responsed;

    inst->time_out = PROC_INTERVAL_NS;
    pr_debug("%s %px\n", __func__, inst);

    return inst;
}

void deinit_class_proc_data(class_proc_data_t *inst) {
    pr_debug("%s %px\n", __func__, inst);
    RETURN_IF_FAIL(inst);

    if (inst->module) {
        deinit_class_proc_module(inst->module);
    }
    if (inst->section_mgr) {
        deinit_class_section_mgr(inst->section_mgr);
    }
    mutex_destroy(&inst->lock);
    vfree(inst);
}
/////////////////////////////////////////////////////////////////
static int class_write_module(struct _class_proc_data_t *inst, char __user *buf, unsigned int token) {
    int len = 0;
    int finish = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->module, -1);

    len = inst->module->write(inst->module, buf, token, &finish);
    if (finish) {
        mutex_lock(&inst->lock);
        inst->wait.response_modules++;
        mutex_unlock(&inst->lock);

        if (inst->is_all_module_responsed(inst)) {
            wake_up_interruptible(&inst->wait.wait_q);
        }
    }

    return len;
}

static int class_write_title(struct _class_proc_data_t *inst, char __user *buf, unsigned int token) {
    int tmp = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->section_mgr, -1);

    if (inst->is_timeout(inst)) {
        pr_warn("%s: timeout!!!\n", __func__);
        return -ETIMEDOUT;
    }
    tmp = inst->section_mgr->write_title(inst->section_mgr, buf, token);
    if (tmp <= 0) {
        return -EFAULT;
    }
    return 0;
}

static long class_write_data(struct _class_proc_data_t *inst, char __user *buf, unsigned int token) {
    long ret = 0;
    int has_more_data = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->section_mgr, -1);

    if (!inst->is_timeout(inst)) {
        ret = inst->section_mgr->write_data(inst->section_mgr, buf, token, &has_more_data);

        if (!ret && !has_more_data) {
            mutex_lock(&inst->lock);
            inst->wait.response_modules++;
            mutex_unlock(&inst->lock);
        }
    } else {
        pr_warn("%s: timeout !!! responsed %u\n", __func__, inst->wait.response_modules);
    }

    return ret;
}

static int class_read(struct _class_proc_data_t *inst, char __user *buf, size_t len, loff_t *off) {
    int read = 0;
    int ret = 0;
    unsigned int mod_size = 0;
    RETURN_VAL_IF_FAIL(inst, 0);
    RETURN_VAL_IF_FAIL(buf, 0);
    RETURN_VAL_IF_FAIL(off, 0);
    RETURN_VAL_IF_FAIL(inst->module, 0);
    RETURN_VAL_IF_FAIL(inst->section_mgr, 0);

    mod_size = inst->module->buf_size;
    // 1. read module
    if (*off < inst->module->buf_size) {
        read = inst->module->read(inst->module, buf, len, *off);
        if (read > 0) {
            *off += read;
            ret += read;
        }
        if (*off == 0) {
            mod_size = 0;
            pr_warn("module parameters is empty\n");
        } else if (*off < inst->module->data_len) {
            pr_debug("not all module data has been read, off: %llu, size: %u\n", *off, inst->module->data_len);
            return ret;
        } else if (len - read <= 0) {
            pr_debug("read module %d, left buf size 0\n", read);
            return ret;
        }
    }

    // 2. read group title and data
    read = inst->section_mgr->read_all(inst->section_mgr, buf + ret, len - ret, *off - mod_size);
    if (read > 0) {
        *off += read;
        ret += read;
    }

    return ret;
}

static void class_reset(struct _class_proc_data_t *inst) {
    ktime_t cur;
    cur = ktime_get_boottime();

    mutex_lock(&inst->lock);
    if (inst->section_mgr) {
        inst->section_mgr->reset(inst->section_mgr);
    }
    if (inst->module) {
        inst->module->reset(inst->module);
    }
    inst->wait.stamp = cur;
    inst->wait.response_modules = 0;
    inst->module_count = 0;
    mutex_unlock(&inst->lock);

    pr_debug("%s\n", __func__);
}

static int class_is_timeout(struct _class_proc_data_t *inst) {
    ktime_t cur;
    int timeout = 0;

    cur = ktime_get_boottime();
    if (!inst->wait.stamp || (cur - inst->time_out > inst->wait.stamp)) {
        timeout = 1;
        pr_warn("%s: timeout!!!\n", __func__);
    }

    return timeout;
}

static void class_set_module_count(struct _class_proc_data_t *inst, int count) {
    inst->module_count = count;
}

static int class_is_all_module_responsed(struct _class_proc_data_t *inst) {
    int resp = 0;
    RETURN_VAL_IF_FAIL(inst, 1);
    RETURN_VAL_IF_FAIL(inst->section_mgr, 1);

    resp = inst->module_count + inst->section_mgr->section_number - 1;
    return (inst->wait.response_modules >= resp) ? 1 : 0;
}

static int class_set_section(struct _class_proc_data_t *inst, char __user *buf, unsigned int token) {
    int ret = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->section_mgr, -1);

    ret = inst->section_mgr->set_section(inst->section_mgr, buf, token);
    return ret;
}
