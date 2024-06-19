#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "../../include/es_media_ext_drv.h"
#include "../../common/dev_common.h"

int fd;

#define offsetof(type, member) (unsigned long)(&(((type *)0)->member))

static void send_module_data(int fd, int rows, unsigned int token);
static void send_group_title(
    int fd, int title_count, int title_len, int max_grp_count, unsigned int section_id, unsigned int token);
static void send_group_data(
    int fd, int id_start, int total_grp, int row_len, int rows, unsigned int section_id, unsigned int token);
static void send_section(int fd, int section_num, unsigned int token);

int main(int argc, char **argv) {
    printf("*********************************\n");
    printf("********** test module **********\n");
    printf("* usage: ./test_module dev_type token_adjust grp_start grp_count is_sendmodule wait_time(ms)\n");
    printf("         0-dec;  1-enc; 2-bms; 3-vps; 4-vo\n");
    printf("         ./test_module 3 0 10 3 0 500\n");
    printf("*********************************\n");

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // printf("[%ld.%ld]\n", ts.tv_sec, ts.tv_nsec);
    int dev_type = 0;
    if (argc < 7) {
        printf("invalid parameters!!!\n");
        return 0;
    }

    dev_type = atoi(argv[1]);
    char dev_path[256] = {0};
    char *dev_name = NULL;
    switch (dev_type) {
        case DEC_MOD:
            dev_name = ES_DEVICE_DEC;
            break;
        case ENC_MOD:
            dev_name = ES_DEVICE_ENC;
            break;
        case BMS_MOD:
            dev_name = ES_DEVICE_BMS;
            break;
        case VPS_MOD:
            dev_name = ES_DEVICE_VPS;
            break;
        case VO_MOD:
            dev_name = ES_DEVICE_VO;
            break;
        default:
            printf("invalid device\n");
            return -1;
    }
    int token_add = atoi(argv[2]);
    int grp_start = atoi(argv[3]);
    int grp_count = atoi(argv[4]);
    int send_module = atoi(argv[5]);
    int wait_time = atoi(argv[6]) * 1000;
    printf("token_add: %d, grp_start: %d, grp_count: %d, send_module: %s, wait_time: %d (us)\n",
           token_add,
           grp_start,
           grp_count,
           send_module ? "yes" : "no",
           wait_time);

    sprintf(dev_path, "/dev/%s%d", dev_name, 0);
    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        printf("Cannot open device file..., %s\n", strerror(errno));
        return -1;
    }
    printf("test_module open file %s success, fd: %d\n", dev_path, fd);

    struct timeval tv;
    fd_set fdread;
    int ret;

    while (1) {
        tv.tv_sec = 100;
        tv.tv_usec = 0;

        FD_ZERO(&fdread);
        FD_SET(fd, &fdread);

        ret = select(fd + 1, &fdread, NULL, NULL, &tv);
        if (ret < 0) {
            printf("module select ret=%d, errno=%d, fd: %d\n", ret, errno, fd);
            break;
        } else if (0 == ret) {
        } else {
            if (FD_ISSET(fd, &fdread)) {
                es_module_event_t event;
                struct timespec ts;

                clock_gettime(CLOCK_MONOTONIC, &ts);
                printf("%ld.%ld readable\n", ts.tv_sec, ts.tv_nsec);
                if (!ioctl(fd, ES_MOD_IOC_GET_EVENT, &event)) {
                    printf("fd %d: get event %d\n", fd, event.id);
                }
                if (event.id == MODULE_PROC) {
                    int rows = 3;
                    int has_group = 1;

                    printf("read event: %d, token: %u\n", event.id, event.token);
                    if (wait_time) {
                        usleep(wait_time);
                    }
                    if (send_module) {
                        send_module_data(fd, rows, (dev_type != BMS_MOD) ? 0 : (event.token + token_add));
                    }

                    if (has_group) {
                        int title_count = 3;
                        int title_len = 64;
                        int max_grp_count = 64;
                        int sections = 3;
                        int i = 0;

                        send_section(fd, sections, event.token + token_add);
                        for (; i < sections; i++) {
                            send_group_title(fd, title_count, title_len, max_grp_count, i, event.token + token_add);
                            send_group_data(
                                fd, grp_start, grp_count, title_len, title_count, i, event.token + token_add);
                        }
                    }
                }
            }
        }
    }

    printf("test module exit\n");
    return 0;
}

static void send_module_data(int fd, int rows, unsigned int token) {
    char buf[1024 * 10] = {0};
    es_proc_mod_t *mod = (es_proc_mod_t *)buf;
    int off = 0;
    int str_len = 0;

    mod->token = token;
    mod->cur_pos = 0;
    mod->left_data = 0;
    for (int i = 0; i < rows; i++) {
        sprintf(mod->data + str_len,
                "==========%d===========\n"
                "-- module[%d] title ---\n"
                "-- module[%d] data  ---\n",
                i,
                i,
                i);
        str_len += strlen(mod->data + off);
        off = str_len;
    }
    mod->len = sizeof(es_proc_mod_t) + str_len;
    printf("module rows: %d, total len %u, str len: %u, token: %u\n", rows, mod->len, str_len, token);

    ioctl(fd, ES_MOD_IOC_PROC_SEND_MODULE, buf);
    printf("  send module %u\n", mod->len);
}

static void send_group_title(
    int fd, int title_count, int title_len, int max_grp_count, unsigned int section_id, unsigned int token) {
    char buf[10240] = {0};
    int off = offsetof(es_proc_grp_title_t, data);
    es_proc_row_t *row = NULL;
    es_proc_grp_title_t *title = (es_proc_grp_title_t *)buf;

    title->token = token;
    title->section_id = section_id;
    title->title_count = title_count;
    title->max_grp_count = max_grp_count;
    for (int i = 0; i < title_count; i++) {
        row = (es_proc_row_t *)(buf + off);
        row->type = ROW_SPLIT_LINE;
        if (i % 2) {
            row->len = offsetof(es_proc_row_t, data);  // empty split line
        } else {
            row->len = title_len + offsetof(es_proc_row_t, data);
            sprintf(row->data, "\n--- sec[%u] split line[%d] ---\n", section_id, i);
        }

        off += row->len;

        row = (es_proc_row_t *)(buf + off);
        row->type = ROW_DATA;
        row->len = title_len + offsetof(es_proc_row_t, data);
        sprintf(row->data, "*** sec[%u] title[%d] ***\n", section_id, i);
        off += row->len;
    }
    title->len = off;

    ioctl(fd, ES_MOD_IOC_PROC_SEND_GRP_TITLE, buf);
    printf("  send title %u bytes, title_count: %d, max_grp_count: %d, section_id: %u, token: %u\n",
           title->len,
           title_count,
           max_grp_count,
           section_id,
           token);
}

static void send_group_data(
    int fd, int id_start, int total_grp, int row_len, int rows, unsigned int section_id, unsigned int token) {
    int i = 0;
    int j = 0;
    es_proc_grp_t *grp = NULL;
    char group_data[1024 * 500] = {0};
    es_proc_grp_header_t *header = (es_proc_grp_header_t *)group_data;
    int grp_off = 0;
    int row_off = 0;
    es_proc_row_t *prow;

    header->token = token;
    header->section_id = section_id;
    header->sent_grp_num = total_grp;
    header->has_more_data = 0;

    grp_off = sizeof(es_proc_grp_header_t);
    for (; i < total_grp; i++) {
        grp = (es_proc_grp_t *)(group_data + grp_off);
        grp->id = id_start + i;
        grp->len = sizeof(es_proc_grp_t);

        row_off = grp_off + sizeof(es_proc_grp_t);
        for (j = 0; j < rows; j++) {
            prow = (es_proc_row_t *)(group_data + row_off);
            prow->type = ROW_DATA;
            prow->len = sizeof(es_proc_row_t) + row_len;
            sprintf(prow->data, "-- sec[%u] grp[%d] row[%d] data --\n", section_id, grp->id, j);

            row_off += prow->len;
            grp->len += prow->len;
        }
        grp_off += grp->len;
    }

    ioctl(fd, ES_MOD_IOC_PROC_SEND_GRP_DATA, group_data);
    printf("  send group data, start %d, count %d, row_len %d, rows %d, section_id %u, token %u\n",
           id_start,
           total_grp,
           row_len,
           rows,
           section_id,
           token);
}

static void send_section(int fd, int section_num, unsigned int token) {
    char buf[128] = {0};
    es_proc_section_t *proc_section = NULL;

    proc_section = (es_proc_section_t *)buf;
    proc_section->len = sizeof(es_proc_section_t);
    proc_section->token = token;
    proc_section->section_number = section_num;

    ioctl(fd, ES_MOD_IOC_PROC_SET_SECTION, proc_section);
    printf("  send section: number %d token %u\n", section_num, token);
}
