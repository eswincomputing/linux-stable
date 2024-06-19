#include "class_chn_mgr.h"
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

static int class_assign_chn(struct _class_chn_mgr_t *inst, channel_t *chn);
static int class_unassign_chn(struct _class_chn_mgr_t *inst, channel_t *chn);
static int class_add_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
static int class_sub_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
static int class_get_count(struct _class_chn_mgr_t *inst, channel_t *chn);
static int class_set_wakeup_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
static int class_get_wakeup_count(struct _class_chn_mgr_t *inst, channel_t *chn);
static wait_queue_head_t *class_get_wait_queue_head(struct _class_chn_mgr_t *inst, channel_t *chn);

/////////////////////////////////////////////////////////////
class_chn_mgr_t *init_chn_mgr_inst(int minor) {
    class_chn_mgr_t *inst = vzalloc(sizeof(class_chn_mgr_t));
    if (!inst) {
        pr_err("malloc class_chn_mgr_t failed, size: %lu, minor %d\n", sizeof(class_chn_mgr_t), minor);
        return NULL;
    }

    mutex_init(&inst->mtx);
    inst->minor = minor;

    inst->assign_chnl = class_assign_chn;
    inst->unassign_chnl = class_unassign_chn;
    inst->add_count = class_add_count;
    inst->sub_count = class_sub_count;
    inst->get_count = class_get_count;
    inst->set_wakeup_count = class_set_wakeup_count;
    inst->get_wakeup_count = class_get_wakeup_count;

    inst->get_wait_queue_head = class_get_wait_queue_head;

    pr_debug("%s: succeed %px, minor %d\n", __func__, inst, minor);
    return inst;
}

void deinit_chn_mgr_inst(class_chn_mgr_t *inst) {
    int i = 0;
    int j = 0;

    if (inst) {
        pr_debug("%s: %px, minor %d\n", __func__, inst, inst->minor);
        for (; i < MAX_GROUPS; i++) {
            for (j = 0; j < MAX_CHANNELS; j++) {
                if (inst->chn[i][j]) {
                    vfree(inst->chn[i][j]);
                    inst->chn[i][j] = NULL;
                }
            }
        }

        mutex_destroy(&inst->mtx);
        vfree(inst);
    }
}

//////////////////////////////////////////////////////////////
#define CHECK_CHN(chn, ret, desc)                                                 \
    if (!(chn) || (chn)->group >= MAX_GROUPS || (chn)->channel >= MAX_CHANNELS) { \
        pr_warn("%s\n", (desc));                                                  \
        return (ret);                                                             \
    }
#define CHECK_CHN_P(ch, ret, desc)                                           \
    if (!inst->chn[(ch)->group][(ch)->channel]) {                            \
        pr_warn("%s - chn[%u, %u] has not been initialized yet. minor %d\n", \
                (desc),                                                      \
                (ch)->group,                                                 \
                (ch)->channel,                                               \
                inst->minor);                                                \
        return (ret);                                                        \
    }

static int class_assign_chn(struct _class_chn_mgr_t *inst, channel_t *chn) {
    int count = 0;
    CHECK_CHN(chn, -1, "assign chn - invalid parameters")

    mutex_lock(&inst->mtx);
    if (!inst->chn[chn->group][chn->channel]) {
        inst->chn[chn->group][chn->channel] = vzalloc(sizeof(class_chn_mgr_t));
        if (!inst->chn[chn->group][chn->channel]) {
            pr_err("%s: vzalloc chn[%u, %u] failed, size: %lu, minor %d\n",
                   __func__,
                   chn->group,
                   chn->channel,
                   sizeof(class_chn_mgr_t),
                   inst->minor);
            mutex_unlock(&inst->mtx);
            return -1;
        }

        atomic_set(&inst->chn[chn->group][chn->channel]->data_count, 0);
        atomic_set(&inst->chn[chn->group][chn->channel]->ref, 0);
        atomic_set(&inst->chn[chn->group][chn->channel]->wakeup_count, 1);
        init_waitqueue_head(&inst->chn[chn->group][chn->channel]->wait_queue);
    }
    mutex_unlock(&inst->mtx);

    count = atomic_add_return(1, &inst->chn[chn->group][chn->channel]->ref);

    pr_debug("assign chn[%u, %u] count: %d, minor %d\n", chn->group, chn->channel, count, inst->minor);
    return count;
}

static int class_unassign_chn(struct _class_chn_mgr_t *inst, channel_t *chn) {
    int count = 0;
    CHECK_CHN(chn, -1, "unassign chn - invalid parameters")
    CHECK_CHN_P(chn, -1, "unassign chn")

    count = atomic_sub_return(1, &inst->chn[chn->group][chn->channel]->ref);
    if (0 == count) {
        atomic_set(&inst->chn[chn->group][chn->channel]->data_count, 0);
        atomic_set(&inst->chn[chn->group][chn->channel]->wakeup_count, 1);
        pr_debug("reset channel[%u, %u] count, minor %d\n", chn->group, chn->channel, inst->minor);
    }

    pr_debug("unassign chn[%u, %u] count: %d, minor %d\n", chn->group, chn->channel, count, inst->minor);
    return count;
}

static int class_add_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val) {
    int count = 0;
    CHECK_CHN(chn, -1, "add count - invalid parameters")
    CHECK_CHN_P(chn, -1, "add count")

    count = atomic_add_return(val, &inst->chn[chn->group][chn->channel]->data_count);
    if (count >= class_get_wakeup_count(inst, chn)) {
        wake_up_interruptible(&inst->chn[chn->group][chn->channel]->wait_queue);
    }
    return count;
}

static int class_sub_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val) {
    int count = 0;
    CHECK_CHN(chn, -1, "sub count - invalid parameters")
    CHECK_CHN_P(chn, -1, "sub count")

    count = atomic_sub_return(val, &inst->chn[chn->group][chn->channel]->data_count);
    return count;
}

static int class_get_count(struct _class_chn_mgr_t *inst, channel_t *chn) {
    CHECK_CHN(chn, -1, "get count - invalid parameters")
    CHECK_CHN_P(chn, -1, "add count")

    return atomic_read(&inst->chn[chn->group][chn->channel]->data_count);
}

static int class_set_wakeup_count(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val) {
    CHECK_CHN(chn, -1, "wakeup count - invalid parameters")
    CHECK_CHN_P(chn, -1, "wakeup count")

    atomic_set(&inst->chn[chn->group][chn->channel]->wakeup_count, val);
    return 0;
}

static int class_get_wakeup_count(struct _class_chn_mgr_t *inst, channel_t *chn) {
    CHECK_CHN(chn, -1, "wakeup count - invalid parameters")
    CHECK_CHN_P(chn, -1, "wakeup count")

    return atomic_read(&inst->chn[chn->group][chn->channel]->wakeup_count);
}

static wait_queue_head_t *class_get_wait_queue_head(struct _class_chn_mgr_t *inst, channel_t *chn) {
    CHECK_CHN(chn, NULL, "get_wait_queue - invalid parameters")
    CHECK_CHN_P(chn, NULL, "get_wait_queue")

    return &inst->chn[chn->group][chn->channel]->wait_queue;
}
