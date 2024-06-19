#ifndef __CLASS_CHN_MGR__H
#define __CLASS_CHN_MGR__H

#include "dev_common.h"
#include "es_media_ext_drv.h"
#include <linux/mutex.h>
#include <linux/wait.h>

#define PUBLIC_WAIT_QUEUE

typedef struct {
    atomic_t data_count;
    atomic_t ref;
    atomic_t wakeup_count;
    wait_queue_head_t wait_queue;
} chnl_data_t;

typedef struct _class_chn_mgr_t {
    chnl_data_t *chn[MAX_GROUPS][MAX_CHANNELS];
    struct mutex mtx;
    int minor;

    int (*assign_chnl)(struct _class_chn_mgr_t *inst, channel_t *chn);
    int (*unassign_chnl)(struct _class_chn_mgr_t *inst, channel_t *chn);
    int (*add_count)(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
    int (*sub_count)(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
    int (*get_count)(struct _class_chn_mgr_t *inst, channel_t *chn);

    int (*set_wakeup_count)(struct _class_chn_mgr_t *inst, channel_t *chn, u32 val);
    int (*get_wakeup_count)(struct _class_chn_mgr_t *inst, channel_t *chn);
    wait_queue_head_t *(*get_wait_queue_head)(struct _class_chn_mgr_t *inst, channel_t *chn);
} class_chn_mgr_t;

class_chn_mgr_t *init_chn_mgr_inst(int tag);
void deinit_chn_mgr_inst(class_chn_mgr_t *inst);

#endif
