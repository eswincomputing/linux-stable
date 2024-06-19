#ifndef __DEC_PROC__H
#define __DEC_PROC__H

#include "dev_common.h"

int es_media_dev_proc_init(dev_minor_t minor, void *module);
void dev_proc_deinit(void);

long dev_proc_recv_grp_data(unsigned long arg, int minor);
long dev_proc_recv_grp_title(unsigned long arg, int minor);
long dev_proc_recv_module(unsigned long arg, int minor);
long dev_proc_recv_section(unsigned long arg, int minor);
long dev_proc_set_timeout(unsigned long arg, int minor);

#endif
