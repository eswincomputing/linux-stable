#include "class_section.h"
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

typedef struct {
    int row;
    int col;
    int buf_off;   // the offset within a title or group
    int data_off;  // the offset within title or group
    int is_group;
} read_position;

static unsigned int get_title_data_len(es_proc_grp_title_t *title);
static int get_one_group_size(class_section *inst, int *data_len, int *buf_size);
static int get_read_position(class_section *inst, unsigned int off, read_position *read_pos);
static int section_read_title(
    class_section *inst, char __user *buf, unsigned int index, int data_off, unsigned int len, int *complete);
static int section_write_title(class_section *inst, char __user *buf, unsigned int token);
static int section_read_data(class_section *inst,
                             char __user *buf,
                             unsigned int row,
                             unsigned int col,
                             int data_off,
                             unsigned int len,
                             int *complete);

static int class_section_init(struct _class_section *inst, char __user *title, unsigned int token);
static void class_section_deinit(struct _class_section *inst);
static void class_section_reset(struct _class_section *inst);
static int class_section_write_data(struct _class_section *inst, char __user *buf);
static int class_section_read_all(
    struct _class_section *inst, char __user *buf, unsigned int len, unsigned int off, int *complete);
static int class_section_get_id(struct _class_section *inst);
static int class_section_get_data_size(struct _class_section *inst, unsigned int *grp_size, unsigned int *title_size);

class_section *init_class_section(void) {
    class_section *inst = vzalloc(sizeof(class_section));
    if (!inst) {
        pr_err("malloc class section failed, size: %lu\n", sizeof(class_section));
        return NULL;
    }

    inst->init_data = class_section_init;
    inst->deinit_data = class_section_deinit;
    inst->reset = class_section_reset;
    inst->write_data = class_section_write_data;
    inst->read_all = class_section_read_all;
    inst->get_session_id = class_section_get_id;
    inst->get_data_size = class_section_get_data_size;

    mutex_init(&inst->lock);
    return inst;
}

void deinit_class_section(class_section *inst) {
    RETURN_IF_FAIL(inst);
    if (inst->deinit_data) {
        inst->deinit_data(inst);
        mutex_destroy(&inst->lock);
    }
    vfree(inst);
}

///////////////////////////////////////////////////////////////
static int malloc_raw_data(raw_data *data, unsigned int size) {
    RETURN_VAL_IF_FAIL(data, -EINVAL);

    data->data_buf = vzalloc(size);
    if (!data->data_buf) {
        pr_err("%s: vzalloc %u failed\n", __func__, size);
        return -ENOMEM;
    }

    data->data_buf_size = size;
    return 0;
}

static void free_raw_data(raw_data *data) {
    if (data && data->data_buf) {
        vfree(data->data_buf);
        memset(data, 0, sizeof(raw_data));
    }
}

////////////////////////////////////////////////////////////////////
static int class_section_get_id(struct _class_section *inst) {
    int section_id = -1;
    RETURN_VAL_IF_FAIL(inst, -1);

    mutex_lock(&inst->lock);
    if (inst->title) {
        section_id = inst->title->section_id;
    }
    mutex_unlock(&inst->lock);

    return section_id;
}

static unsigned int get_title_data_len(es_proc_grp_title_t *title) {
    es_proc_row_t *row = NULL;
    unsigned int offset = 0;
    unsigned int data_len = 0;

    offset = offsetof(es_proc_grp_title_t, data);
    while (offset < title->len) {
        row = (es_proc_row_t *)((char *)title + offset);
        if (row->len < offsetof(es_proc_row_t, data)) {
            pr_err("%s: invalid row len %u, offset %u\n", __func__, row->len, offset);
            break;
        } else if (row->len >= MAX_TITLE_SIZE) {
            pr_warn("%s: title row len is too large, %u\n", __func__, row->len);
            break;
        }
        data_len += row->len - offsetof(es_proc_row_t, data);
        offset += row->len;
    }

    return data_len;
}

static int section_write_title(class_section *inst, char __user *buf, unsigned int token) {
    unsigned int len = 0;
    unsigned int title_token = 0;
    long ret = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);

    __get_user(len, (__u32 __user *)buf);
    if (len > MAX_TITLE_SIZE || len < offsetof(es_proc_grp_title_t, data)) {
        pr_err("%s: invalid title size, %u\n", __func__, len);
        return -1;
    }

    __get_user(title_token, (__u32 __user *)(buf + sizeof(len)));
    if (title_token != token) {
        pr_info("%s: token mismatch, %u != %u(expected)\n", __func__, title_token, token);
        return -1;
    }

    mutex_lock(&inst->lock);
    do {
        if (inst->title_data.data_buf) {
            pr_warn("%s: do not update title again!!!", __func__);
            ret = -1;
            break;
        } else if (inst->title_data.data_buf_size < len) {
            free_raw_data(&inst->title_data);
        }
        if (!inst->title_data.data_buf) {
            ret = malloc_raw_data(&inst->title_data, len);
            if (ret) {
                break;
            }
        }

        ret = copy_from_user(inst->title_data.data_buf, buf, len);
        if (ret) {
            pr_err("%s copy from user failed, returned %li\n", __func__, ret);
            ret = -1;
            break;
        }
        inst->title = (es_proc_grp_title_t *)inst->title_data.data_buf;
        inst->title_data.data_size = get_title_data_len(inst->title);
    } while (0);
    mutex_unlock(&inst->lock);

    if (!ret) {
        pr_debug(
            "%s:[%u] write title %u succeed. data len %u, title_count %u, max_grp_count %u, not_sort %u, token %u\n",
            __func__,
            inst->title->section_id,
            len,
            inst->title_data.data_size,
            inst->title->title_count,
            inst->title->max_grp_count,
            inst->title->not_sort,
            token);
    } else {
        pr_err("write title failed, token=%u\n", token);
    }

    return 0;
}

static int get_one_group_size(class_section *inst, int *data_len, int *buf_size) {
    es_proc_row_t *row = NULL;
    unsigned int offset = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);

    if (inst->title) {
        if (data_len) {
            *data_len = 0;
        }

        if (buf_size) {
            *buf_size = 0;
        }

        offset = offsetof(es_proc_grp_title_t, data);
        while (offset < inst->title->len) {
            row = (es_proc_row_t *)((char *)inst->title + offset);
            if (ROW_SPLIT_LINE != row->type) {
                if (data_len) {
                    *data_len += row->len - offsetof(es_proc_row_t, data);
                }

                if (buf_size) {
                    *buf_size += row->len;
                }
            }

            offset += row->len;
        }
        if (buf_size) {
            *buf_size += sizeof(es_proc_grp_t);
        }

        return 0;
    }

    return -1;
}

static int class_section_init(struct _class_section *inst, char __user *title, unsigned int token) {
    long ret = 0;
    unsigned int size;
    int buf_size = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(title, -EINVAL);

    if (inst->title) {
        pr_warn("%s - section_id %u data has been initialized!!!\n", __func__, inst->title->section_id);
        return -EINVAL;
    }

    ret = section_write_title(inst, title, token);
    if (ret < 0) {
        return -1;
    } else if (ret == 0) {
        get_one_group_size(inst, NULL, &buf_size);
        size = inst->title->max_grp_count * buf_size;
        if (size > MAX_GROUP_SIZE) {
            pr_err("%s - memory size is too large, %u\n", __func__, size);
            return -EINVAL;
        }
        pr_debug(
            "[%u]data buf size: %u, max_grp_count: %u\n", inst->title->section_id, size, inst->title->max_grp_count);

        ret = malloc_raw_data(&inst->data, size);
        if (ret) {
            return ret;
        }

        inst->group_status = vzalloc(inst->title->max_grp_count * sizeof(unsigned char));
        if (!inst->group_status) {
            pr_err("%s - malloc group status failed, max grp count: %u\n", __func__, inst->title->max_grp_count);
            return -ENOMEM;
        }
    }

    return inst->title_data.data_buf_size;
}

static void class_section_deinit(struct _class_section *inst) {
    RETURN_IF_FAIL(inst);

    mutex_lock(&inst->lock);

    free_raw_data(&inst->data);
    free_raw_data(&inst->title_data);
    if (inst->group_status) {
        vfree(inst->group_status);
        inst->group_status = NULL;
    }

    inst->first_group = NULL;
    inst->title = NULL;
    inst->group_count = 0;
    inst->one_group_data_len = 0;
    inst->one_group_buf_size = 0;

    mutex_unlock(&inst->lock);

    pr_debug("%s\n", __func__);
}

static void class_section_reset(struct _class_section *inst) {
    RETURN_IF_FAIL(inst);
    pr_debug("%s section_id: %u\n", __func__, inst->title ? inst->title->section_id : 10000);

    mutex_lock(&inst->lock);
    if (inst->data.data_buf) {
        memset(inst->data.data_buf, 0, inst->data.data_buf_size);
        inst->data.data_size = 0;
    }

    if (inst->group_status && inst->title) {
        memset(inst->group_status, 0, inst->title->max_grp_count * sizeof(unsigned char));
    }

    inst->group_count = 0;
    inst->first_group = NULL;
    mutex_unlock(&inst->lock);
}

static int class_section_write_data(struct _class_section *inst, char __user *buf) {
    int offset = 0;
    unsigned int len;
    unsigned int id;
    long ret = 0;
    int data_size = 0;

    RETURN_VAL_IF_FAIL(inst, -1);
    RETURN_VAL_IF_FAIL(buf, -1);
    RETURN_VAL_IF_FAIL(inst->title, -1);
    RETURN_VAL_IF_FAIL(inst->group_status, -1);

    __get_user(len, (__u32 __user *)buf);
    __get_user(id, (__u32 __user *)(buf + sizeof(len)));

    mutex_lock(&inst->lock);
    do {
        if (!inst->data.data_buf) {
            ret = -1;
            pr_err("data buffer has not been allocated\n");
            break;
        }

        if (id >= inst->title->max_grp_count) {
            pr_err("%s: id(%u) >= max_grp_count(%u)\n", __func__, id, inst->title->max_grp_count);
            ret = -1;
            break;
        }
        if (len < offsetof(es_proc_grp_t, data)) {
            pr_err("%s: invalid group data %u\n", __func__, len);
            ret = -1;
            break;
        }

        if (inst->title->not_sort) {
            offset = inst->data.write_buf_size;
        } else {
            offset = len * id;
        }

        if (offset + len > inst->data.data_buf_size) {
            pr_err("%s: buffer overflow, offset[%u] + len[%u] > %u\n", __func__, offset, len, inst->data.data_buf_size);
            ret = -1;
            break;
        }
        ret = copy_from_user(inst->data.data_buf + offset, buf, len);
        if (0 != ret) {
            pr_err("%s: copy from user failed, return %ld\n", __func__, ret);
            ret = -1;
            break;
        }

        if (!inst->first_group) {
            inst->first_group = (es_proc_grp_t *)(inst->data.data_buf + offset);
        }
        if (inst->one_group_data_len == 0) {
            get_one_group_size(inst, &data_size, NULL);
            inst->one_group_data_len = data_size;
            inst->one_group_buf_size = len;
        }
        inst->data.data_size += inst->one_group_data_len;
        inst->data.write_buf_size += len;
        if (!inst->title->not_sort) {
            inst->group_status[id] = 1;
        } else {
            inst->group_status[inst->group_count] = 1;
        }
        inst->group_count++;

        ret = len;
    } while (0);
    mutex_unlock(&inst->lock);

    pr_debug("%s:[%u], group[%u], grp->len %u, data size %u, grp count %u, offset %d, ret: %ld\n",
             __func__,
             inst->title->section_id,
             id,
             len,
             inst->one_group_data_len,
             inst->group_count,
             offset,
             ret);

    return ret;
}

static int get_read_position_in_title(class_section *inst,
                                      unsigned int off,
                                      int check_one_title,
                                      int *row,
                                      unsigned int buf_start,
                                      unsigned int *buf_checked,
                                      unsigned int *data_checked,
                                      unsigned int total_data_len,
                                      read_position *read_pos) {
    unsigned int data_len = 0;
    const unsigned int data_offset = offsetof(es_proc_row_t, data);
    es_proc_row_t *proc_row = NULL;
    int tmp_buf_off = 0;
    int total_buf_off = 0;
    int tmp_data_off = 0;
    int total_data_off = 0;
    int found = 0;

    if (buf_checked) {
        *buf_checked = 0;
    }
    if (data_checked) {
        *data_checked = 0;
    }

    do {
        proc_row = (es_proc_row_t *)((char *)inst->title->data + buf_start + total_buf_off + tmp_buf_off);
        data_len = proc_row->len - data_offset;
        if (tmp_data_off + total_data_off + total_data_len + data_len > off) {
            found = 1;
            if (row) {
                read_pos->row = *row;
            }
            read_pos->col = 0;
            read_pos->is_group = 0;
            read_pos->data_off = off - (total_data_len + total_data_off);
            break;
        }

        tmp_data_off += data_len;
        tmp_buf_off += proc_row->len;

        if (buf_checked) {
            *buf_checked += proc_row->len;
        }
        if (data_checked) {
            *data_checked += data_len;
        }
        if (ROW_DATA == proc_row->type) {
            total_buf_off += tmp_buf_off;
            tmp_buf_off = 0;

            total_data_off += tmp_data_off;
            tmp_data_off = 0;

            if (row) {
                *row += 1;
            }
            if (check_one_title) {
                break;
            }
        }
    } while (buf_start + total_buf_off + tmp_buf_off < inst->title_data.data_buf_size);

    pr_debug(
        "%s: title - found %d, off %u, section_id: %u, buf_start %u, total_data_len %u, buf_checked %u, data_checked "
        "%u, row %d, col %d, buf_off %d, data_off %d\n",
        __func__,
        found,
        off,
        inst->title ? inst->title->section_id : -1,
        buf_start,
        total_data_len,
        buf_checked ? (*buf_checked) : -1,
        data_checked ? (*data_checked) : -1,
        read_pos->row,
        read_pos->col,
        read_pos->buf_off,
        read_pos->data_off);
    return found;
}

static int get_read_position(class_section *inst, unsigned int off, read_position *read_pos) {
    int i;
    unsigned int buf_off = sizeof(es_proc_grp_t);
    unsigned int data_len = 0;
    int title_row = 0;
    unsigned int title_buf_off = 0;
    unsigned int buf_checked = 0;
    unsigned int data_checked = 0;
    unsigned int total_data_len = 0; /* title_data_len + group_data_len */
    unsigned int found = 0;
    unsigned int grp_row_len = 0;
    const unsigned int data_offset = sizeof(es_proc_row_t);
    es_proc_row_t *proc_row = NULL;
    RETURN_VAL_IF_FAIL(inst, -1);
    RETURN_VAL_IF_FAIL(inst->title, -1);
    RETURN_VAL_IF_FAIL(read_pos, -1);

    memset(read_pos, 0, sizeof(read_position));
    if (off == 0) {
        found = 1;
        goto ret;
    }

    // only title
    if (!inst->data.data_size) {
        found = get_read_position_in_title(inst, off, 0, &title_row, 0, NULL, NULL, total_data_len, read_pos);
    } else {
        pr_debug("[%u]: inst->one_group_buf_size = %u\n", inst->title->section_id, inst->one_group_buf_size);
        while (!found && buf_off < inst->one_group_buf_size) {
            // check one title
            found = get_read_position_in_title(
                inst, off, 1, &title_row, title_buf_off, &buf_checked, &data_checked, total_data_len, read_pos);
            if (found) {
                break;
            } else {
                title_buf_off += buf_checked;
                total_data_len += data_checked;
            }

            if (!inst->first_group) {
                pr_info("[%u]: has no group data\n", inst->title->section_id);
                break;
            }

            // check one row data
            grp_row_len = 0;
            for (i = 0; i < inst->title->max_grp_count; i++) {
                if (inst->group_status[i]) {
                    proc_row = (es_proc_row_t *)(inst->data.data_buf + buf_off + inst->one_group_buf_size * i);
                    data_len = proc_row->len - data_offset;
                    if (total_data_len + data_len > off) {
                        found = 1;
                        read_pos->col = i;
                        read_pos->is_group = 1;
                        read_pos->buf_off = buf_off;
                        read_pos->data_off = off - total_data_len;
                        pr_debug("found in grp[%d]: grp_data_len %u, off: %u, total_data_len: %u\n",
                                i,
                                data_len,
                                off,
                                total_data_len);
                        break;
                    }
                    pr_debug("[%u][%d], data_len %d, off: %u\n", inst->title->section_id, i, data_len, off);
                    total_data_len += data_len;
                    if (!grp_row_len) {
                        grp_row_len = proc_row->len;
                    }
                    if (grp_row_len != proc_row->len) {
                        pr_warn("[%u][%d] all of grp size is different %u != %u\n",
                                inst->title->section_id,
                                i,
                                grp_row_len,
                                proc_row->len);
                    }
                }
            }
            buf_off += grp_row_len;

            if (found) {
                break;
            }
            read_pos->row++;
        }
    }

ret:
    if (found) {
        pr_debug("%s:[%u] found position for off %u, pos in [%s], row %d, col %d, buf_off %d, data_off %d\n",
                __func__,
                inst->title ? inst->title->section_id : -1,
                off,
                read_pos->is_group ? "group" : "title",
                read_pos->row,
                read_pos->col,
                read_pos->buf_off,
                read_pos->data_off);
        return 0;
    } else {
        pr_warn("%s:[%u] did not find position, off: %u\n", __func__, inst->title ? inst->title->section_id : -1, off);
        return -1;
    }
}

// data_off: data offset within a title row(title split line + title data line)
static int section_read_title(
    class_section *inst, char __user *buf, unsigned int index, int data_off, unsigned int len, int *complete) {
    int row = 0;
    long ret = 0;
    int total_read = 0;
    unsigned int off = offsetof(es_proc_grp_title_t, data);
    es_proc_row_t *cur_row = NULL;
    int one_row_data_len = 0;
    int total_data_len = 0;
    int left = 0;
    int read = 0;
    int read_off = data_off;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);

    *complete = 1;
    while (off < inst->title->len) {
        cur_row = (es_proc_row_t *)((char *)inst->title + off);
        if (row == index) {
            total_data_len = 0;
            do {
                cur_row = (es_proc_row_t *)((char *)inst->title + off);
                one_row_data_len = cur_row->len - offsetof(es_proc_row_t, data);

                if (one_row_data_len) {
                    left = total_data_len + one_row_data_len - data_off;
                    if (left > 0) {
                        if (left > one_row_data_len) {
                            left = one_row_data_len;
                        }
                        read = left;
                        if (total_read + read > len) {
                            read = len - total_read;
                        }
                        ret = copy_to_user((char __user *)buf + total_read, cur_row->data + read_off, read);
                        if (ret) {
                            *complete = 0;
                            pr_err("%s:[%u] row[%u] copy_to_user split line failed, ret %ld\n",
                                   __func__,
                                   inst->title->section_id,
                                   index,
                                   ret);
                            return -1;
                        }

                        total_read += read;
                        pr_debug(
                            "[%u] row[%d], total_read: %d, cur_read: %d, cur_title_len: %d, data_off: %d, type: %u, "
                            "buf size: %u, read_off: %d\n",
                            inst->title->section_id,
                            row,
                            total_read,
                            read,
                            one_row_data_len,
                            data_off,
                            cur_row->type,
                            len,
                            read_off);
                        if (left != read) {
                            *complete = 0;
                            break;
                        } else if (cur_row->type == ROW_DATA) {
                            break;
                        }
                        read_off = 0;
                        data_off = 0;
                    } else {
                        read_off -= one_row_data_len;
                    }
                }

                off += cur_row->len;
                total_data_len += one_row_data_len;
            } while (off < inst->title->len);

            break;
        }

        if (cur_row->type == ROW_DATA) {
            row++;
        }
        off += cur_row->len;
    }
    pr_debug("%s:[%u] row[%u] read %d bytes, data_off: %d, buf size: %u, complete: %d\n",
             __func__,
             inst->title->section_id,
             index,
             total_read,
             data_off,
             len,
             *complete);
    return total_read;
}

static int section_read_data(class_section *inst,
                             char __user *buf,
                             unsigned int row,
                             unsigned int col,
                             int data_off,
                             unsigned int len,
                             int *complete) {
    int read_buf_off = sizeof(es_proc_grp_t);
    int i = 0;
    es_proc_row_t *row_data = NULL;
    int read = 0;
    long ret = 0;
    int data_len = 0;
    int total_read = 0;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->first_group, -1);

    for (i = 0; i < row; i++) {
        row_data = (es_proc_row_t *)((char *)inst->first_group + read_buf_off);
        read_buf_off += row_data->len;
    }

    *complete = 1;
    for (i = col; i < inst->title->max_grp_count; i++) {
        if (inst->group_status[i]) {
            row_data = (es_proc_row_t *)(inst->data.data_buf + inst->one_group_buf_size * i + read_buf_off);
            data_len = row_data->len - sizeof(es_proc_row_t) - data_off;
            read = data_len;
            if (total_read + read > len) {
                read = len - (total_read + data_off);
                pr_debug("[%u][%d]: off read, data_len %d, data_off %d, total_read %d buf size: %d\n",
                        inst->title->section_id,
                        i,
                        data_len,
                        data_off,
                        total_read,
                        len);
            }
            pr_debug("[%d][%d] row[%u] data_len: %d, read: %d, data_off: %d, total_read: %d, buf size: %u\n",
                     inst->title->section_id,
                     i,
                     row,
                     data_len,
                     read,
                     data_off,
                     total_read,
                     len);
            ret = copy_to_user((char __user *)buf + total_read, row_data->data + data_off, read);
            if (ret) {
                pr_err("%s:[%u] row[%u] col[%u], copy_to_user failed, read: %d, grp: [%d], data_off: %d\n",
                       __func__,
                       inst->title ? inst->title->section_id : -1,
                       row,
                       col,
                       read,
                       i,
                       data_off);
                return -1;
            }

            data_off = 0;
            total_read += read;
            if (data_len != read) {
                *complete = 0;
                break;
            }
        }
    }

    pr_debug("%s:[%u] row[%u] start grp[%u] total_read: %d, data_off: %d, buf size: %u, complete: %d\n",
             __func__,
             inst->title ? inst->title->section_id : -1,
             row,
             col,
             total_read,
             data_off,
             len,
             *complete);
    return total_read;
}

static int class_section_read_all(
    struct _class_section *inst, char __user *buf, unsigned int len, unsigned int off, int *complete) {
    long ret = 0;
    int read = 0;
    int read_title = 0;
    int read_group = 0;
    read_position read_pos = {0};
    int i = 0;
    int first = 1;
    int left_buf_size = 0;
    unsigned int section_id = inst->title ? inst->title->section_id : -1;
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    RETURN_VAL_IF_FAIL(buf, -EINVAL);
    RETURN_VAL_IF_FAIL(complete, -EINVAL);
    RETURN_VAL_IF_FAIL(inst->title, -1);
    RETURN_VAL_IF_FAIL(inst->group_status, -1);
    *complete = 0;

    if (off >= inst->title_data.data_size + inst->data.data_size) {
        pr_debug("%s:[%u] has already read completed, off %u, total data size: %d\n",
                __func__,
                section_id,
                off,
                inst->title_data.data_size + inst->data.data_size);
        goto read_return;
    }

    ret = get_read_position(inst, off, &read_pos);
    if (ret) {
        goto read_return;
    }

    *complete = 1;
    for (i = read_pos.row; i < inst->title->title_count; i++) {
        if (first) {
            left_buf_size = len - read;
            if (!read_pos.is_group) {
                ret = section_read_title(inst, buf + read, i, read_pos.data_off, left_buf_size, complete);
                if (ret < 0) {
                    pr_info("%s:[%u] read last title[%d], data_off: %d failed\n",
                            __func__,
                            section_id,
                            i,
                            read_pos.data_off);
                    goto read_return;
                }

                read += ret;
                read_title += ret;
                left_buf_size = len - read;
                if (left_buf_size <= 0 || !*complete) {
                    goto read_return;
                }
                ret = section_read_data(inst, buf + read, i, 0, 0, left_buf_size, complete);
            } else {
                ret = section_read_data(
                    inst, buf + read, read_pos.row, read_pos.col, read_pos.data_off, left_buf_size, complete);
            }

            if (ret < 0) {
                pr_info("%s:[%u] read last data[row: %d, col: %d, data_off: %d] failed\n",
                        __func__,
                        section_id,
                        i,
                        read_pos.col,
                        read_pos.data_off);
                goto read_return;
            }

            read += ret;
            read_group += ret;
            first = 0;
        } else {
            left_buf_size = len - read;
            ret = section_read_title(inst, buf + read, i, 0, left_buf_size, complete);
            if (ret < 0) {
                pr_info("%s:[%u] read title[%d] failed\n", __func__, section_id, i);
                goto read_return;
            }

            read += ret;
            read_title += ret;
            left_buf_size = len - read;
            if (left_buf_size <= 0 || !*complete) {
                goto read_return;
            }
            ret = section_read_data(inst, buf + read, i, 0, 0, left_buf_size, complete);
            if (ret < 0) {
                pr_info("%s:[%u] read data[%d] failed\n", __func__, section_id, i);
                goto read_return;
            }
            read += ret;
            read_group += ret;
        }

        left_buf_size = len - read;
        if (left_buf_size <= 0 || !*complete) {
            goto read_return;
        }
    }

read_return:
    pr_debug("%s:[%u] read %d(title %d, grp %d) bytes, off: %u, buf size: %u, complete %d\n",
            __func__,
            section_id,
            read,
            read_title,
            read_group,
            off,
            len,
            *complete);
    return read;
}

static int class_section_get_data_size(struct _class_section *inst, unsigned int *grp_size, unsigned int *title_size) {
    RETURN_VAL_IF_FAIL(inst, -EINVAL);
    if (grp_size) {
        *grp_size = inst->data.data_size;
    }
    if (title_size) {
        *title_size = inst->title_data.data_size;
    }
    return inst->data.data_size + inst->title_data.data_size;
}
