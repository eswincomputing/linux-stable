#include "class_proc_module.h"
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

static int proc_module_malloc(struct _class_proc_module_t *inst, unsigned int size);
static void proc_module_free(struct _class_proc_module_t *inst);

static int proc_module_write(struct _class_proc_module_t *inst, char __user *buf, unsigned int token, int *finish);
static int proc_module_read(struct _class_proc_module_t *inst, char __user *buf, unsigned int len,
    unsigned int off);
static void proc_module_reset(struct _class_proc_module_t *inst);

class_proc_module_t *init_class_proc_module(void) {
    class_proc_module_t *inst = vmalloc(sizeof(class_proc_module_t));
    if (!inst) {
        pr_err("vmalloc class_module_t failed\n");
        return NULL;
    }
    memset(inst, 0, sizeof(class_proc_module_t));

    inst->malloc = proc_module_malloc;
    inst->free = proc_module_free;

    inst->write = proc_module_write;
    inst->read = proc_module_read;
    inst->reset = proc_module_reset;
    pr_debug("%s %px\n", __func__, inst);

    return inst;
}

void deinit_class_proc_module(class_proc_module_t *inst) {
    pr_debug("%s %px\n", __func__, inst);
    if (inst) {
        inst->free(inst);
        vfree(inst);
    }
}

///////////////////////////////////////////////////////////////////
static int proc_module_malloc(struct _class_proc_module_t *inst, unsigned int size) {
    if (inst->module_buf || size == 0) {
        return -1;
    }

    inst->module_buf = vzalloc(size);
    if (!inst->module_buf) {
        pr_err("malloc module %u failed\n", size);
        return -1;
    }
    inst->buf_size = size;
    pr_debug("%s: size %u\n", __func__, size);

    return 0;
}

static void proc_module_free(struct _class_proc_module_t *inst) {
    if (inst->module_buf) {
        vfree(inst->module_buf);
        inst->module_buf = NULL;
        inst->buf_size = 0;
        inst->data_len = 0;
        pr_debug("%s\n", __func__);
    }
}

static int proc_module_write(struct _class_proc_module_t *inst, char __user *buf, unsigned int token, int *finish) {
    unsigned int len = 0;
    unsigned int mod_token;
    unsigned int cur_pos = 0;
    unsigned int left_data = 0;
    unsigned int grand_total = 0;
    int off = 0;
    long ret = 0;

    __get_user(len, (__u32 __user *)buf);
    __get_user(mod_token, (__u32 __user *)(buf + sizeof(len) * ++off));
    __get_user(cur_pos, (__u32 __user *)(buf + sizeof(len)  * ++off));
    __get_user(left_data, (__u32 __user *)(buf + sizeof(len) * ++off));
    grand_total = left_data + cur_pos + (len - offsetof(es_proc_mod_t, data));

    if (len < offsetof(es_proc_mod_t, data)) {
        pr_err("%s: invalid module data len %u\n", __func__, len);
        inst->data_len = 0;
        return -1;
    }
    if (grand_total > MAX_MODULE_SIZE) {
        pr_err("%s: grand total size(%u) is too large, %u, %u, %u\n", __func__, grand_total, left_data, cur_pos, len);
        return -1;
    }
    if (inst->buf_size && inst->buf_size < grand_total) {
        inst->free(inst);
    }
    if (!inst->buf_size) {
        inst->malloc(inst, grand_total);
    }
    if ((mod_token != 0) && (token != mod_token)) {
        pr_info("%s: token mismatch, %u != %u(expected)\n", __func__, mod_token, token);
        memset(inst->module_buf, 0, inst->buf_size);
        inst->data_len = 0;
        return -1;
    }
    inst->do_not_reset = (mod_token == 0);
    if (inst->data_len != grand_total) {
        inst->data_len = grand_total;
    }

    ret = copy_from_user(inst->module_buf + cur_pos, buf + offsetof(es_proc_mod_t, data),
        len - offsetof(es_proc_mod_t, data));
    if (ret) {
        pr_err("%s copy_from_user failed, return %li\n", __func__, ret);
        return -1;
    }
    if (!left_data && (mod_token != 0)) {
        *finish = 1;
    }
    pr_debug("%s: len %u, grand total %u, data_len %u, cur_pos %u, left_data %u, do_not_reset %d, finish %d, token %u\n",
        __func__, len, grand_total, inst->data_len, cur_pos, left_data, inst->do_not_reset, *finish, mod_token);

    return ret;
}

static int proc_module_read(struct _class_proc_module_t *inst, char __user *buf, unsigned int len,
    unsigned int off) {
    int read = 0;
    long ret = 0;

    if (!inst->buf_size || !inst->data_len) {
        pr_warn("%s: module data is not init\n", __func__);
        return -1;
    }
    if (off >= inst->data_len) {
        return 0;
    }
    read = inst->data_len - off;
    if (len < read) {
        read = len;
    }
    ret = copy_to_user(buf, inst->module_buf + off, read);
    if (ret) {
        pr_err("%s copy_to_user %d failed, return %li\n", __func__, read, ret);
        return -1;
    }

    pr_debug("%s: read %d bytes, left %u\n", __func__, read, inst->buf_size - read - off);
    return read;
}

static void proc_module_reset(struct _class_proc_module_t *inst) {
    if (inst && !inst->do_not_reset) {
        memset(inst->module_buf, 0, inst->buf_size);
        inst->data_len = 0;

        pr_debug("%s\n", __func__);
    }
}
////////////////////////////////////////////////////////
