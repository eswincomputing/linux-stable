#ifndef __CLASS_EVENT__H
#define __CLASS_EVENT__H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include "es_media_ext_drv.h"

typedef struct {
    es_module_event_t event;
    struct list_head q_head;
} module_event_list_t;

//module private data
typedef struct _class_event_t{
    pid_t pid;
    struct mutex mtx;
    atomic_t event_count;

    wait_queue_head_t wait_queue;
    module_event_list_t event_q;

    int (*add_event)(struct _class_event_t *inst, es_module_event_t *event);
    module_event_list_t* (*pop_event)(struct _class_event_t *inst);
    int (*remove_event)(struct _class_event_t *inst, int id);

    int (*clear_events)(struct _class_event_t *inst);
    int (*find_event)(struct _class_event_t *inst, int id);
} class_event_t;

class_event_t *init_class_event(pid_t pid);
void deinit_class_event(class_event_t *inst);

#endif
