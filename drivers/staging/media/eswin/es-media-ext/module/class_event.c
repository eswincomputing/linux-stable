#include "dev_common.h"
#include "class_event.h"
#include <linux/vmalloc.h>
#include <linux/sched.h>

//////////////////////////////////////////////////////////////////
static int class_event_add_event(struct _class_event_t *inst, es_module_event_t *event);
static module_event_list_t* class_event_pop_event(struct _class_event_t *inst);

static int class_event_clear_events(struct _class_event_t *inst);
static int class_event_find_event(struct _class_event_t *inst, int id);
static int class_event_remove_event(struct _class_event_t *inst, int id);
//////////////////////////////////////////////////////////////////

class_event_t *init_class_event(pid_t pid) {
    class_event_t *inst = vmalloc(sizeof(class_event_t));
    if (!inst) {
        pr_err("init_class_event_mgr vmalloc failed\n");
        return NULL;
    }

    mutex_init(&inst->mtx);
    inst->pid = pid;
    atomic_set(&inst->event_count, 0);
    init_waitqueue_head(&inst->wait_queue);
    INIT_LIST_HEAD(&inst->event_q.q_head);

    inst->add_event = class_event_add_event;
    inst->pop_event = class_event_pop_event;
    inst->remove_event = class_event_remove_event;
    inst->clear_events = class_event_clear_events;
    inst->find_event = class_event_find_event;

    pr_debug("%s: pid %d, %px\n", __func__, inst->pid, inst);

    return inst;
}

void deinit_class_event(class_event_t *inst) {
    if (inst) {
        pr_debug("%s: pid %d, %px\n", __func__, inst->pid, inst);
        inst->clear_events(inst);
        mutex_destroy(&inst->mtx);

        vfree(inst);
    }
}

//////////////////////////////////////////////////////////////////
static int class_event_add_event(struct _class_event_t *inst, es_module_event_t *event) {
    module_event_list_t *evt = NULL;
    if (!inst) {
        return -1;
    }

    evt = vmalloc(sizeof(es_module_event_t));
    if (!evt) {
        pr_err("append event vmalloc failed\n");
        return -1;
    }
    evt->event = *event;

    mutex_lock(&inst->mtx);
    list_add_tail(&evt->q_head, &inst->event_q.q_head);
    atomic_add(1, &inst->event_count);
    mutex_unlock(&inst->mtx);

    wake_up_interruptible(&inst->wait_queue);

    pr_debug("pid %d add event %u, token %u\n", inst->pid, event->id, event->token);
    return 0;
}

static module_event_list_t* class_event_pop_event(struct _class_event_t *inst) {
    module_event_list_t *event = NULL;
    if (!inst) {
        return NULL;
    }

    if (atomic_read(&inst->event_count) > 0) {
        mutex_lock(&inst->mtx);
        if (!list_empty(&inst->event_q.q_head)) {
            event = list_first_entry(&inst->event_q.q_head, module_event_list_t, q_head);
            list_del(&event->q_head);
            atomic_sub(1, &inst->event_count);
        } else {
            pr_warn("del event-should not be here!\n");
        }
        mutex_unlock(&inst->mtx);
    }

    return event;
}

static int class_event_clear_events(struct _class_event_t *inst) {
    module_event_list_t *pos = NULL;
    module_event_list_t *tmp = NULL;

    if (!inst) {
        return -1;
    }

    mutex_lock(&inst->mtx);
    list_for_each_entry_safe(pos, tmp, &inst->event_q.q_head, q_head) {
        list_del(&pos->q_head);
        vfree(pos);
    }
    mutex_unlock(&inst->mtx);

    return 0;
}

static int class_event_find_event(struct _class_event_t *inst, int id) {
    module_event_list_t *pos = NULL;
    module_event_list_t *tmp = NULL;

    if (!inst) {
        return -1;
    }

    mutex_lock(&inst->mtx);
    list_for_each_entry_safe(pos, tmp, &inst->event_q.q_head, q_head) {
        if (pos->event.id == id) {
            return 1;
        }
    }
    mutex_unlock(&inst->mtx);

    return 0;
}

static int class_event_remove_event(struct _class_event_t *inst, int id) {
    module_event_list_t *pos = NULL;
    module_event_list_t *tmp = NULL;
    int ret = 0;

    if (!inst) {
        return -1;
    }

    mutex_lock(&inst->mtx);
    list_for_each_entry_safe(pos, tmp, &inst->event_q.q_head, q_head) {
        if (pos->event.id == id) {
            list_del(&pos->q_head);
            vfree(pos);
            ret = 1;
            break;
        }
    }
    mutex_unlock(&inst->mtx);

    return ret;
}
