#ifndef __CLASS_DATA_RW__H
#define __CLASS_DATA_RW__H

#include "dev_common.h"
#include "es_media_ext_drv.h"
#include <linux/mutex.h>

typedef struct {
    char *data_buf;
    unsigned int data_buf_size;
    unsigned int data_size;  // valid data length
    unsigned int write_buf_size;  // how many buf is written
} raw_data;

typedef struct _class_section {
    int (*init_data)(struct _class_section *inst, char __user *title, unsigned int token);
    void (*deinit_data)(struct _class_section *inst);

    void (*reset)(struct _class_section *inst);
    int (*get_session_id)(struct _class_section *inst);

    int (*write_data)(struct _class_section *inst, char __user *buf);
    /* read title and group data */
    int (*read_all)(struct _class_section *inst, char __user *buf, unsigned int len, unsigned int off, int *complete);

    /* title len + data len */
    int (*get_data_size)(struct _class_section *inst, unsigned int *grp_size, unsigned int *title_size);

    /* group */
    raw_data data;
    es_proc_grp_t *first_group;
    unsigned int one_group_data_len;
    unsigned int one_group_buf_size;

    /* title */
    raw_data title_data;
    es_proc_grp_title_t *title;

    unsigned char *group_status;  // u8
    unsigned int group_count;

    struct mutex lock;
} class_section;

class_section *init_class_section(void);
void deinit_class_section(class_section *inst);

#endif
