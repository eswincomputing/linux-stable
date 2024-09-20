#ifndef __VDEC_ALLOCATOR__H
#define __VDEC_ALLOCATOR__H

#include <linux/fs.h>
#include "hantrodec.h"

/* CPU cache operations */
typedef enum _ES_CACHE_OPERATION {
    ES_CACHE_CLEAN      = 0x01, /* Flush CPU cache to mem */
    ES_CACHE_INVALIDATE = 0x02, /* Invalidte CPU cache */
    ES_CACHE_FLUSH      = ES_CACHE_CLEAN | ES_CACHE_INVALIDATE, /* Both flush & invalidate */
    ES_CACHE_MEMORY_BARRIER = 0x04,
} ES_CACHE_OPERATION;

int vdec_attach_user_memory(struct file *filp, struct device *dev, user_memory_desc *desc);
int vdec_detach_user_memory(struct file *filp, user_memory_desc *desc);
void vdec_clean_user_memory(bi_list *list);
int vdec_sync_user_memory_cache(struct file *filp, user_memory_desc *desc, ES_CACHE_OPERATION opr);

#endif
