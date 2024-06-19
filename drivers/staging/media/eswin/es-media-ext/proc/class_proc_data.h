#ifndef __CLASS_PROC_DATA__H
#define __CLASS_PROC_DATA__H

#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include "class_proc_module.h"
#include "class_section_mgr.h"

typedef struct {
    ktime_t stamp;
    u16 response_modules;
    wait_queue_head_t wait_q;
} proc_wait_t;

typedef struct _class_proc_data_t {
    int (*write_module)(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
    int (*write_title)(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
    long (*write_data)(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
    int (*read)(struct _class_proc_data_t *inst, char __user *buf, size_t len, loff_t *off);
    int (*set_section)(struct _class_proc_data_t *inst, char __user *buf, unsigned int token);
    void (*reset)(struct _class_proc_data_t *inst);

    int (*is_timeout)(struct _class_proc_data_t *inst);
    void (*set_module_count)(struct _class_proc_data_t *inst, int count);
    int (*is_all_module_responsed)(struct _class_proc_data_t *inst);

    class_proc_module_t *module;
    class_section_mgr_t *section_mgr;

    proc_wait_t wait;
    struct mutex lock;
    int module_count;
    ktime_t time_out;  //ns
} class_proc_data_t;

class_proc_data_t* init_class_proc_data(void);
void deinit_class_proc_data(class_proc_data_t *inst);

#endif
