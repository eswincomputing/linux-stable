#include "dev_common.h"
#include "es_media_ext_drv.h"
#include <linux/fs.h>

dev_module_index_t dev_minor_to_module_index(dev_minor_t minor) {
    switch(minor) {
    case DEC_MODULE:
        return DEC_MOD;
    case ENC_MODULE:
        return ENC_MOD;
    case BMS_MODULE:
        return BMS_MOD;
    case VPS_MODULE:
        return VPS_MOD;
    case VO_MODULE:
        return VO_MOD;
    default:
        pr_warn("module not support minor %d\n", minor);
        return MAX_MOD;
    }
}

dev_proc_index_t dev_minor_to_proc_index(dev_minor_t minor) {
    switch(minor) {
    case DEC_MODULE:
        return DEC_PROC;
    case ENC_MODULE:
        return ENC_PROC;
    case BMS_MODULE:
        return BMS_PROC;
    case VPS_MODULE:
        return VPS_PROC;
    case VO_MODULE:
        return VO_PROC;
    default:
        pr_warn("proc not support minor %d\n", minor);
        return MAX_PROC;
    }
}

dev_chn_index_t dev_minor_to_chn_index(dev_minor_t minor) {
    switch(minor) {
    case DEC_CHANNEL:
        return DEC_CHN;
    case ENC_CHANNEL:
        return ENC_CHN;
    case VPS_CHANNEL:
        return VPS_CHN;
    case VO_CHANNEL:
        return VO_CHN;
    default:
        pr_warn("channel not support minor %d\n", minor);
        return MAX_CHN;
    }
}

char* get_dev_name(dev_minor_t minor) {
    switch(minor) {
    case DEC_MODULE:
    case DEC_CHANNEL:
        return ES_DEVICE_DEC;
    case ENC_MODULE:
    case ENC_CHANNEL:
        return ES_DEVICE_ENC;
    case BMS_MODULE:
        return ES_DEVICE_BMS;
    case VPS_MODULE:
    case VPS_CHANNEL:
        return ES_DEVICE_VPS;
    case VO_MODULE:
    case VO_CHANNEL:
        return ES_DEVICE_VO;
    default:
        pr_warn("%s: should not be here!!!, minor %d\n", __func__, minor);
        return "es_unknown";
    }
}

int get_dev_sub_name(dev_minor_t minor) {
    switch (minor) {
    case DEC_MODULE:
    case ENC_MODULE:
    case BMS_MODULE:
    case VPS_MODULE:
    case VO_MODULE:
        return 0;
    case DEC_CHANNEL:
    case ENC_CHANNEL:
    case VPS_CHANNEL:
    case VO_CHANNEL:
        return 1;
    default:
        pr_warn("%s: should not be here!!!, minor %d\n", __func__, minor);
        return 0;
    }
}

char* get_proc_name(dev_proc_index_t index) {
    switch (index) {
    case DEC_PROC:
        return DEC_PROC_NAME;
    case ENC_PROC:
        return ENC_PROC_NAME;
    case BMS_PROC:
        return BMS_PROC_NAME;
    case VPS_PROC:
        return VPS_PROC_NAME;
    case VO_PROC:
        return VO_PROC_NAME;
    default:
        pr_warn("%s: should not be here!!!, index %d\n", __func__, index);
        return "unknown_proc";
    }
}
