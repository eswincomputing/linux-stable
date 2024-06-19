#ifndef __CLASS_MODULE__H
#define __CLASS_MODULE__H
#include <linux/fs.h>
#include "class_event.h"

typedef struct {
    struct list_head q_head;
    pid_t pid;
    struct file *filp;
} module_t;

typedef struct _class_module_t {
    atomic_t count;
    module_t module_q;
    struct mutex mtx;

    int (*add)(struct _class_module_t *inst, struct file *filp, pid_t pid);
    int (*del)(struct _class_module_t *inst, pid_t pid);

    void (*clear_q)(struct _class_module_t *inst);
    module_t* (*find)(struct _class_module_t *inst, pid_t pid);
    int (*send_event)(struct _class_module_t *inst, es_module_event_t *event);
    int (*get_count)(struct _class_module_t *inst);

    int (*remove_event)(struct _class_module_t *inst, int id);
} class_module_t;

typedef struct {
    class_event_t *event;
    class_module_t *module;
    int minor;
    pid_t pid;
} module_private_data_t;

class_module_t* init_class_module(void);
void deinit_class_module(class_module_t *inst);

#endif
