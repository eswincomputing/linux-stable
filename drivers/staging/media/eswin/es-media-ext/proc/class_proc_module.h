#ifndef __CLASS_PROC_MODULE__H
#define __CLASS_PROC_MODULE__H

#include "dev_common.h"
#include "es_media_ext_drv.h"

typedef struct _class_proc_module_t {
    //private
    int (*malloc)(struct _class_proc_module_t *inst, unsigned int size);
    void (*free)(struct _class_proc_module_t *inst);

    //public
    int (*write)(struct _class_proc_module_t *inst, char __user *buf, unsigned int token, int *finish);
    int (*read)(struct _class_proc_module_t *inst, char __user *buf, unsigned int len,
        unsigned int off);
    void (*reset)(struct _class_proc_module_t *inst);

    /* module */
    char *module_buf;
    unsigned int buf_size;
    unsigned int data_len;
    int do_not_reset;
} class_proc_module_t;

class_proc_module_t *init_class_proc_module(void);
void deinit_class_proc_module(class_proc_module_t *inst);

#endif
