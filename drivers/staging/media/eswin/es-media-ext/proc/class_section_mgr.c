#include "class_section_mgr.h"
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

static int class_section_mgr_set_section(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token);
static int class_section_mgr_write_title(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token);
static int class_section_mgr_write_data(struct _class_section_mgr_t *inst,
                                        char __user *buf,
                                        unsigned int token,
                                        int *has_more_data);
static void class_section_mgr_reset(struct _class_section_mgr_t *inst);
static int class_section_mgr_read_all(struct _class_section_mgr_t *inst,
                                      char __user *buf,
                                      unsigned int len,
                                      unsigned int off);

static int malloc_section_array(class_section_mgr_t *inst, int section_number);
static void free_section_array(class_section_mgr_t *inst);

typedef enum {
    SECTION_STATUS_DEFAULT = 0,
    SECTION_STATUS_SET,
    SECTION_STATUS_BUTT
} section_status_t;

static int malloc_section_array(class_section_mgr_t *inst, int section_number) {
    int ret = 0;
    int i = 0;

    inst->section = vzalloc(sizeof(class_section *) * section_number);
    if (!inst->section) {
        pr_err("malloc section* failed, size: %lu\n", sizeof(class_section));
        return -1;
    }

    inst->section_number = section_number;
    for (i = 0; i < section_number; i++) {
        inst->section[i] = init_class_section();
        if (!inst->section[i]) {
            pr_err("init class section:%d failed\n", i);
            free_section_array(inst);
            ret = -1;
            break;
        }
    }

    return ret;
}

static void free_section_array(class_section_mgr_t *inst) {
    int i;

    if (inst->section) {
        for (i = 0; i < inst->section_number; i++) {
            if (inst->section[i]) {
                deinit_class_section(inst->section[i]);
                inst->section[i] = NULL;
            }
        }
        vfree(inst->section);
        inst->section = NULL;
    }
}

class_section_mgr_t *init_class_section_mgr(void) {
    class_section_mgr_t *inst = vzalloc(sizeof(class_section_mgr_t));
    if (!inst) {
        pr_err("malloc class section mgr failed, size: %lu\n", sizeof(class_section_mgr_t));
        return NULL;
    }

    inst->set_section = class_section_mgr_set_section;
    inst->write_title = class_section_mgr_write_title;
    inst->write_data = class_section_mgr_write_data;
    inst->read_all = class_section_mgr_read_all;
    inst->reset = class_section_mgr_reset;

    if (malloc_section_array(inst, 1)) {
        vfree(inst);
        return NULL;
    }
    inst->section_status = SECTION_STATUS_DEFAULT;

    mutex_init(&inst->lock);
    return inst;
}

void deinit_class_section_mgr(class_section_mgr_t *inst) {
    RETURN_IF_FAIL(inst);

    free_section_array(inst);
    mutex_destroy(&inst->lock);
    vfree(inst);
}

//////////////////////////////////////////////////////////////////////////////
static int class_section_mgr_set_section(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token) {
    int ret = 0;
    unsigned int len = 0;
    es_proc_section_t section;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);

    __get_user(len, (__u32 __user *)buf);
    if (len != sizeof(es_proc_section_t)) {
        pr_err("%s: invalid title size, %u\n", __func__, len);
        return -1;
    }

    ret = copy_from_user(&section, buf, sizeof(section));
    if (ret) {
        pr_err("%s copy from user failed, returned %d\n", __func__, ret);
        return -1;
    }
    if (token != section.token) {
        pr_err("%s token mismatch %u != %u(expected)\n", __func__, section.token, token);
        return -1;
    }
    if (section.section_number >= 128) {
        pr_err("%s section number is too large %u\n", __func__, section.section_number);
        return -1;
    }

    mutex_lock(&inst->lock);
    do {
        if (SECTION_STATUS_DEFAULT != inst->section_status) {
            pr_info("section has already been initialized\n");
            break;
        }

        if (section.section_number == 1) {
            break;
        }

        free_section_array(inst);
        if (malloc_section_array(inst, section.section_number)) {
            ret = -ENOMEM;
            break;
        }

        inst->section_status = SECTION_STATUS_SET;
    } while (0);
    mutex_unlock(&inst->lock);

    pr_debug("%s: set section %u\n", __func__, section.section_number);
    return ret;
}

class_section *get_section(class_section_mgr_t *inst, unsigned int section_id) {
    class_section *section = NULL;
    RETURN_VAL_IF_FAIL(inst, NULL);
    RETURN_VAL_IF_FAIL(inst->section, NULL);
    RETURN_VAL_IF_FAIL(inst->section_number > 0, NULL);
    RETURN_VAL_IF_FAIL(section_id < inst->section_number, NULL);

    section = inst->section[section_id];
    if (section->title && section->title->section_id != section_id) {
        pr_err("%s section_id %u != %u (expected)\n", __func__, section->title->section_id, section_id);
        section = NULL;
    }

    return section;
}

static int class_section_mgr_write_title(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token) {
    int ret = 0;
    unsigned int len = 0;
    class_section *section = NULL;
    es_proc_grp_title_t title_header = {0};
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);

    __get_user(len, (__u32 __user *)buf);
    if (len > MAX_TITLE_SIZE || len < offsetof(es_proc_grp_title_t, data)) {
        pr_err("%s: invalid title size, %u\n", __func__, len);
        return -1;
    }

    ret = copy_from_user(&title_header, buf, sizeof(es_proc_grp_title_t));
    if (ret) {
        pr_err("%s copy from user failed, returned %d\n", __func__, ret);
        return -1;
    }
    if (token != title_header.token) {
        pr_err("%s token mismatch %u != %u(expected)\n", __func__, title_header.token, token);
        return -1;
    }

    mutex_lock(&inst->lock);
    do {
        section = get_section(inst, title_header.section_id);
        if (!section) {
            ret = -1;
            pr_err("%s no section %u\n", __func__, title_header.section_id);
            break;
        }

        ret = section->init_data(section, buf, token);
    } while (0);
    mutex_unlock(&inst->lock);

    return ret;
}

static int class_section_mgr_write_data(struct _class_section_mgr_t *inst,
                                        char __user *buf,
                                        unsigned int token,
                                        int *has_more_data) {
    int ret = 0;
    unsigned int off = sizeof(es_proc_grp_header_t);
    int write = 0;
    int i = 0;
    class_section *section = NULL;
    es_proc_grp_header_t grp_header = {0};
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(has_more_data, -EINVAL);

    ret = copy_from_user(&grp_header, buf, sizeof(grp_header));
    if (ret) {
        pr_err("%s copy from user failed, returned %d\n", __func__, ret);
        return -1;
    }
    if (token != grp_header.token) {
        pr_err("%s token mismatch %u != %u(expected)\n", __func__, grp_header.token, token);
        return -1;
    }
    *has_more_data = grp_header.has_more_data;

    mutex_lock(&inst->lock);
    do {
        section = get_section(inst, grp_header.section_id);
        if (!section) {
            ret = -1;
            pr_err("%s no section %u\n", __func__, grp_header.section_id);
            break;
        }

        for (; i < grp_header.sent_grp_num; i++) {
            write = section->write_data(section, buf + off);
            if (write <= 0) {
                break;
            }
            off += write;
        }
    } while (0);
    mutex_unlock(&inst->lock);

    pr_debug("%s:[%u] write group data %lu, grp num: %u\n",
            __func__,
            section ? grp_header.section_id : -1,
            off - sizeof(es_proc_grp_header_t),
            grp_header.sent_grp_num);
    return ret;
}

static void class_section_mgr_reset(struct _class_section_mgr_t *inst) {
    int i;
    class_section *section = NULL;
    RETURN_IF_FAIL(inst);
    RETURN_IF_FAIL(inst->section);
    RETURN_IF_FAIL(inst->section_number);

    mutex_lock(&inst->lock);
    for (i = 0; i < inst->section_number; i++) {
        section = inst->section[i];
        section->reset(section);
    }
    mutex_unlock(&inst->lock);
}

static int class_section_mgr_read_all(struct _class_section_mgr_t *inst,
                                      char __user *buf,
                                      unsigned int len,
                                      unsigned int off) {
    int i = 0;
    int read = 0;
    int total_read = 0;
    int complete = 0;
    int data_pos = 0;
    int cur_data_size = 0;
    int left_buf_size = 0;
    unsigned int grp_size = 0;
    unsigned int data_size = 0;
    class_section *section = NULL;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(len, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->section, -1);
    RETURN_VAL_IF_FAIL(inst->section_number, -1);

    pr_debug("%s: buf len %u, read off %u, inst->section_number %u\n", __func__, len, off, inst->section_number);
    for (i = 0; i < inst->section_number; i++) {
        section = inst->section[i];
        cur_data_size = section->get_data_size(section, &grp_size, &data_size);
        left_buf_size = len - total_read;

        if (left_buf_size <= 0) {
            pr_info("%s:[%d] no buf for current read cycle\n", __func__, i);
            break;
        }
        if (off + total_read >= data_pos + cur_data_size) {
            data_pos += cur_data_size;
            continue;
        }

        pr_debug("%s:[%d] expect to read: %d(grp %u, title %u), left buf size: %d, off %u, total_read %d, data_pos %d\n",
                __func__,
                i,
                cur_data_size,
                grp_size,
                data_size,
                left_buf_size,
                off,
                total_read,
                data_pos);

        complete = 0;
        read = section->read_all(section, buf + total_read, left_buf_size, (off + total_read) - data_pos, &complete);
        if (!complete) {
            if (read > 0) {
                total_read += read;
            }
            pr_warn("%s:[%d] no space for current read cycle, read %d bytes, left_buf_size: %d\n",
                    __func__,
                    i,
                    read,
                    left_buf_size - read);
            break;
        }

        total_read += read;
        data_pos += cur_data_size;
    }

    pr_debug("%s: finish, off: %u, read %d, left buf size: %d, section number: %d\n",
            __func__,
            off,
            total_read,
            len - total_read,
            inst->section_number);
    return total_read;
}
