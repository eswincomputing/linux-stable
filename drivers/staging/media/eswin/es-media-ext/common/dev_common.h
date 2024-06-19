#ifndef __DEV_COMMON__H
#define __DEV_COMMON__H

#ifndef pr_fmt
#define pr_fmt(fmt) "ESWV: " fmt   /* for log output */
#endif

typedef enum {
    DEC_MODULE = 0,
    DEC_CHANNEL,
    ENC_MODULE,
    ENC_CHANNEL,
    BMS_MODULE,
    VPS_MODULE,
    VPS_CHANNEL,
    VO_MODULE,
    VO_CHANNEL,
    DEV_MAX,
} dev_minor_t;

#define NO_OF_DEVICES       DEV_MAX  //dev_minor_t

typedef enum {
    DEC_MOD = 0,
    ENC_MOD,
    BMS_MOD,
    VPS_MOD,
    VO_MOD,
    MAX_MOD,
} dev_module_index_t;

typedef enum {
    DEC_PROC = 0,
    ENC_PROC,
    BMS_PROC,
    VPS_PROC,
    VO_PROC,
    MAX_PROC,
} dev_proc_index_t;

typedef enum {
    DEC_CHN = 0,
    ENC_CHN,
    VPS_CHN,
    VO_CHN,
    MAX_CHN,
} dev_chn_index_t;

dev_module_index_t dev_minor_to_module_index(dev_minor_t minor);
dev_proc_index_t dev_minor_to_proc_index(dev_minor_t minor);
dev_chn_index_t dev_minor_to_chn_index(dev_minor_t minor);
char* get_dev_name(dev_minor_t minor);
int get_dev_sub_name(dev_minor_t minor);
char* get_proc_name(dev_proc_index_t index);

#define MAX_GROUPS           256 //vps max 256 groups
#define MAX_CHANNELS         3   //vps a group has 3 channels
#define INVALID_GROUP        0xFFFF

#define PROC_INTERVAL_MS     2000
#define PROC_INTERVAL_NS     (1000000L * PROC_INTERVAL_MS)  //ns

/* When type of function is void and expr is false, return. */
#define RETURN_IF_FAIL(expr)                                                                   \
    do {                                                                                       \
        if (!(expr)) {                                                                         \
            pr_err("Func:%s, Line:%d, expr \"%s\" failed.\n", __func__, __LINE__, #expr);      \
            return;                                                                            \
        }                                                                                      \
    } while (0)

/* When expr is false, return error info. */
#define RETURN_VAL_IF_FAIL(expr, ret)                                                        \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            pr_err("Func:%s, Line:%d, expr \"%s\" failed.", __func__, __LINE__, #expr);      \
            return ret;                                                                      \
        }                                                                                    \
    } while (0)

#endif

#define MAX_MODULE_SIZE   (3 * 1024 * 1024)
#define MAX_TITLE_SIZE    (1024 * 1024)
#define MAX_GROUP_SIZE    (10 * 1024 * 1024)
