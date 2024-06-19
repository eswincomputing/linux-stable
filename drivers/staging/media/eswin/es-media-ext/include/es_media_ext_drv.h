#ifndef __ES_MEDIA_EXT_DRV__H
#define __ES_MEDIA_EXT_DRV__H
#include <linux/ioctl.h>

typedef struct {
    unsigned short group;
    unsigned short channel;
} channel_t;

/******************************************************************************
 *                                  module                                    *
 ******************************************************************************/
#define ES_IOC_MAGIC_M 'm'

typedef enum {
    MODULE_UNKNOWN = 0,
    MODULE_PROC,
    MODULE_BIND,
    MODULE_UNBIND,
} MODULE_EVENT_E;

typedef struct {
    MODULE_EVENT_E id;
    unsigned int token;
    union {
        channel_t chn;
        unsigned int value;
    };
} es_module_event_t;

#define ES_IOC_BASE 0

#define ES_MOD_IOC_GET_EVENT _IOR(ES_IOC_MAGIC_M, ES_IOC_BASE + 0, es_module_event_t *)

/* module public info: es_proc_mod_t */
#define ES_MOD_IOC_PROC_SEND_MODULE _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 1, void *)

/* group title: es_proc_grp_title_t */
#define ES_MOD_IOC_PROC_SEND_GRP_TITLE _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 2, void *)

/* group info: es_proc_grp_data_t */
#define ES_MOD_IOC_PROC_SEND_GRP_DATA _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 3, void *)

/* if driver receives this, the module fd is only used to send module public configuration
 * and will not receive event requests.
 */
#define ES_MOD_IOC_PUB_USER _IO(ES_IOC_MAGIC_M, ES_IOC_BASE + 4)

/* section: es_proc_section_t */
#define ES_MOD_IOC_PROC_SET_SECTION _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 5, void *)

/* set timeout value */
#define ES_MOD_IOC_PROC_SET_TIMEOUT _IOW(ES_IOC_MAGIC_M, ES_IOC_BASE + 6, unsigned int *)

#define ES_MOD_IOC_MAX_NR (ES_IOC_BASE + 7)

/******************************************************************************
 *                                  group                                     *
 ******************************************************************************/
#define ES_IOC_MAGIC_C 'c'

#define ES_CHN_IOC_COUNT_ADD _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 0, unsigned int *)
#define ES_CHN_IOC_COUNT_SUB _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 1, unsigned int *)
#define ES_CHN_IOC_COUNT_GET _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 2, unsigned int *)

#define ES_CHN_IOC_ASSIGN_CHANNEL _IOW(ES_IOC_MAGIC_C, ES_IOC_BASE + 3, channel_t *)
#define ES_CHN_IOC_UNASSIGN_CHANNEL _IOW(ES_IOC_MAGIC_C, ES_IOC_BASE + 4, channel_t *)

#define ES_CHN_IOC_WAKEUP_COUNT_SET _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 5, unsigned int *)
#define ES_CHN_IOC_WAKEUP_COUNT_GET _IOWR(ES_IOC_MAGIC_C, ES_IOC_BASE + 6, unsigned int *)

#define ES_CHN_IOC_MAX_NR (ES_IOC_BASE + 7)

#define ES_DEVICE_DEC "es_dec" /* /dev/es_dec */
#define ES_DEVICE_ENC "es_enc" /* /dev/es_enc */
#define ES_DEVICE_BMS "es_bms" /* /dev/es_bms */
#define ES_DEVICE_VPS "es_vps" /* /dev/es_vps */
#define ES_DEVICE_VO "es_vo"   /* /dev/es_vo  */

// proc
#define UMAP_LOG_PROC_DIR "esmap"
#define DEC_PROC_NAME "dec" /* /proc/esmap/dec */
#define ENC_PROC_NAME "enc" /* /proc/esmap/enc */
#define BMS_PROC_NAME "bms" /* /proc/esmap/bms */
#define VPS_PROC_NAME "vps" /* /proc/esmap/vps */
#define VO_PROC_NAME "vo"   /* /proc/esmap/vo  */

/******************************************************************************
 *                                  proc                                      *
 ******************************************************************************/
/*
 * module data example:
 * [0] split line ------
 * [0] title
 * [0] data
 * [1] split line ------
 * [1] title
 * [1] data
 */
typedef struct {
    unsigned int len;       /* sizeof(es_proc_mod_t) + data len */
    unsigned int token;     /* 0 indicates token will not be used */
    unsigned int cur_pos;   /* current data offset, grand total = cur_pos + left_data + data len */
    unsigned int left_data; /* how long bytes are left to send later */
    char data[0];           /* context string */
} es_proc_mod_t;

/*
 * group/channel data example:
 *
 * split [0] --------
 * title [0]
 *  group[0][0] data
 *  group[1][0] data
 *  ...
 *  group[n][0] data
 * split [1] -------
 * title [1]
 *  group[0][1] data
 *  group[1][1] data
 *  ...
 *  group[n][1] data
 * ...
 */
typedef enum {
    ROW_DATA = 0,
    ROW_SPLIT_LINE,
    ROW_BUTT,
} ROW_TYPE;

typedef struct {
    unsigned short len;  /* sizeof(es_proc_row_t) + data len */
    unsigned short type; /* ROW_TYPE */
    char data[0];        /* context string */
} es_proc_row_t;

typedef struct {
    unsigned int len; /* sizeof(es_proc_grp_title_t) + data len */
    unsigned int token;
    unsigned int section_id;
    unsigned int title_count;
    unsigned int max_grp_count; /* how many groups are supported */
    unsigned int not_sort;      /* 0: sort the group data; 1: not sort the group data */
    es_proc_row_t data[0];      /* group header: split line, title (split line first) */
} es_proc_grp_title_t;

typedef struct {
    unsigned int token;
    unsigned int section_id;
    unsigned int sent_grp_num;
    unsigned int has_more_data;
} es_proc_grp_header_t;

typedef struct {
    unsigned int len; /* sizeof(es_proc_grp_t) + data len */
    unsigned int id;
    es_proc_row_t data[0];
} es_proc_grp_t;

typedef struct {
    es_proc_grp_header_t header;
    es_proc_grp_t grp[0]; /* the data len of group must equal the corresponding title len */
} es_proc_grp_data_t;

typedef struct {
    unsigned int len;
    unsigned int token;
    unsigned int section_number; /* how many sections, section_id should in range [0, section_number - 1] */
} es_proc_section_t;

#endif
