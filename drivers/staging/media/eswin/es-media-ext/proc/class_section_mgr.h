#ifndef __CLASS_SECTION_MGR__H
#define __CLASS_SECTION_MGR__H

#include "class_section.h"

typedef struct _class_section_mgr_t {
    int (*set_section)(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token);
    int (*write_title)(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token);
    int (*write_data)(struct _class_section_mgr_t *inst, char __user *buf, unsigned int token, int *has_more_data);

    /* read all sections title and group data */
    int (*read_all)(struct _class_section_mgr_t *inst, char __user *buf, unsigned int len, unsigned int off);
    void (*reset)(struct _class_section_mgr_t *inst);

    class_section **section;
    int section_number;
    int section_status;

    struct mutex lock;
} class_section_mgr_t;

class_section_mgr_t *init_class_section_mgr(void);
void deinit_class_section_mgr(class_section_mgr_t *inst);

#endif
