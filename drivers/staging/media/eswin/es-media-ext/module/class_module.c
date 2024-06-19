#include "dev_common.h"
#include "class_module.h"
#include <linux/vmalloc.h>

static module_t* class_module_find(struct _class_module_t *inst, pid_t pid);
static int class_module_add(struct _class_module_t *inst, struct file *filp, pid_t pid);
static int class_module_del(struct _class_module_t *inst, pid_t pid);
static void class_module_clear_q(struct _class_module_t *inst);
static int class_module_send_event(struct _class_module_t *inst, es_module_event_t *event);
static int class_module_get_module_count(struct _class_module_t *inst);
static int class_remove_event(struct _class_module_t *inst, int id);

////////////////////////////////////////////////////////////////
class_module_t* init_class_module(void) {
    class_module_t *inst = vmalloc(sizeof(class_module_t));
    if (!inst) {
        pr_err("init class module failed\n");
        return NULL;
    }

    mutex_init(&inst->mtx);
    atomic_set(&inst->count, 0);
    INIT_LIST_HEAD(&inst->module_q.q_head);

    inst->find = class_module_find;
    inst->add = class_module_add;
    inst->del = class_module_del;
    inst->clear_q = class_module_clear_q;
    inst->send_event = class_module_send_event;
    inst->get_count = class_module_get_module_count;
    inst->remove_event = class_remove_event;
    pr_debug("%s: %px\n", __func__, inst);

    return inst;
}

void deinit_class_module(class_module_t *inst) {
    pr_debug("%s: %px\n", __func__, inst);
    if (inst) {
        inst->clear_q(inst);
        mutex_destroy(&inst->mtx);
        vfree(inst);
    }
}

////////////////////////////////////////////////////////////////
static int class_module_add(struct _class_module_t *inst, struct file *filp, pid_t pid) {
    module_t *module = NULL;

    if (!inst || !filp) {
        pr_warn("class_module_add - invalid parameters\n");
        return -EINVAL;
    }

    if (inst->find(inst, pid)) {
        pr_warn("module pid %d has been added.\n", pid);
        return -EEXIST;
    }

    module = vmalloc(sizeof(module_t));
    if (!module) {
        pr_err("malloc module failed\n");
        return -ENOMEM;
    }

    module->pid = pid;
    module->filp = filp;

    mutex_lock(&inst->mtx);
    list_add_tail(&module->q_head, &inst->module_q.q_head);
    atomic_add(1, &inst->count);
    mutex_unlock(&inst->mtx);
    pr_debug("%s - [pid %d] add class module %px\n", __func__, pid, module);

    return 0;
}

static int class_module_del(struct _class_module_t *inst, pid_t pid) {
    module_t *entry = NULL;
    struct list_head *pos = NULL;

    if (!inst) {
        pr_warn("class_module_del - invalid parameters\n");
        return -EINVAL;
    }

    mutex_lock(&inst->mtx);
    list_for_each(pos, &inst->module_q.q_head) {
        entry = list_entry(pos, module_t, q_head);
        if (entry && entry->pid == pid) {
            list_del(&entry->q_head);
            vfree(entry);
            atomic_sub(1, &inst->count);
            break;
        }
    }
    mutex_unlock(&inst->mtx);

    pr_debug("%s - [pid %d] del class module %px\n", __func__, pid, entry);
    return 0;
}

static module_t* class_module_find(struct _class_module_t *inst, pid_t pid) {
    struct list_head *pos = NULL;
    module_t *entry = NULL;
    if (!inst) {
        return NULL;
    }

    mutex_lock(&inst->mtx);
    list_for_each(pos, &inst->module_q.q_head) {
        entry = list_entry(pos, module_t, q_head);
        if (entry && entry->pid == pid) {
            mutex_unlock(&inst->mtx);
            pr_debug("module pid %d found\n", pid);
            return entry;
        }
    }
    mutex_unlock(&inst->mtx);
    pr_debug("%s - find module [pid %d] %px\n", __func__, pid, entry);

    return NULL;
}

static void class_module_clear_q(struct _class_module_t *inst) {
    module_t *pos = NULL;
    module_t *tmp = NULL;

    if (!inst) {
        return;
    }

    mutex_lock(&inst->mtx);
    list_for_each_entry_safe(pos, tmp, &inst->module_q.q_head, q_head) {
        list_del(&pos->q_head);
        vfree(pos);
    }
    atomic_set(&inst->count, 0);
    mutex_unlock(&inst->mtx);
}

static int class_module_send_event(struct _class_module_t *inst, es_module_event_t *event) {
    module_private_data_t *data = NULL;
    module_t *module_entry = NULL;
    struct list_head *pos = NULL;
    int ret = 0;

    if (!inst || !event) {
        pr_warn("class_module_send_event - invalid parameters\n");
        return -EINVAL;
    }

    mutex_lock(&inst->mtx);
    list_for_each(pos, &inst->module_q.q_head) {
        module_entry = list_entry(pos, module_t, q_head);
        if (module_entry && module_entry->filp && module_entry->filp->private_data) {
            data = (module_private_data_t*)module_entry->filp->private_data;
            if (!data->event->find_event(data->event, event->id)) {
                data->event->add_event(data->event, event);

                ret++;
            }
        } else {
            pr_info("module_entry is null, should not be here\n");
        }
    }
    mutex_unlock(&inst->mtx);
    pr_debug("send module event: id %u, token %u, module count %d\n", event->id, event->token, ret);

    return ret;
}

static int class_module_get_module_count(struct _class_module_t *inst) {
    if (inst) {
        return atomic_read(&inst->count);
    }

    return 0;
}

static int class_remove_event(struct _class_module_t *inst, int id) {
    module_private_data_t *data = NULL;
    module_t *module_entry = NULL;
    struct list_head *pos = NULL;

    if (!inst) {
        pr_warn("%s - invalid parameters\n", __func__);
        return -EINVAL;
    }

    pr_debug("%s: id %d\n", __func__, id);
    mutex_lock(&inst->mtx);
    list_for_each(pos, &inst->module_q.q_head) {
        module_entry = list_entry(pos, module_t, q_head);
        if (module_entry && module_entry->filp && module_entry->filp->private_data) {
            data = (module_private_data_t*)module_entry->filp->private_data;
            data->event->remove_event(data->event, id);
        } else {
            pr_info("module_entry is null, should not be here\n");
        }
    }
    mutex_unlock(&inst->mtx);

    return 0;
}
