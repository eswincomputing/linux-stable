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

int fd;
int thread_exit = 0;
int auto_read = 1;
unsigned int group = 0xFFFFFFFF;

void* readThread(void* param) {
    fd_set fdread;
    struct timeval tv;
    int ret;
    struct timeval tvp;
    struct tm* ptm;
    char time_string[64] = {0};
    char time_out[128] = {0};
    long milliseconds;

    while (!thread_exit) {
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        FD_ZERO(&fdread);
        FD_SET(fd, &fdread);

        ret = select(fd + 1, &fdread, NULL, NULL, &tv);

        gettimeofday(&tvp, NULL);
        ptm = localtime(&tvp.tv_sec);
        strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);
        milliseconds = tvp.tv_usec / 1000;
        snprintf(time_out, sizeof(time_out), "%s.%03ld", time_string, milliseconds);

        if (ret < 0) {
            printf("%s group %u error, ret=%d, errno=%d, fd: %d\n", time_out, group, ret, errno, fd);
            break;
        } else if (0 == ret) {
            printf("%s group %u timeout, fd: %d\n", time_out, group, fd);
        } else {
            if (FD_ISSET(fd, &fdread)) {
                printf("%s group %u readable, fd: %d\n", time_out, group, fd);
                unsigned int read = 1;
                ioctl(fd, ES_CHN_IOC_COUNT_GET, &read);
                printf("%s group %u read count %u, fd: %d\n", time_out, group, read, fd);
                if (auto_read) {
                    ioctl(fd, ES_CHN_IOC_COUNT_SUB, &read);
                } else {
                    usleep(3000000);
                }
            }
        }
    }

    printf("group %u read thread exit\n", group);
    return NULL;
}

int main(int argc, char** argv) {
    char dev_path[256] = {0};
    pthread_t hthread;
    int opt;
    int dev_type = 0;
    channel_t chn;

    printf("*********************************\n");
    printf("******* cmodel driver test ******\n");
    printf("* usage: ./testdrv dev_type group\n");
    printf("*         0-dec;  1-enc\n");
    printf("*********************************\n");

    if (argc < 3) {
        printf("usage: ./testdrv dev_type group\n");
        printf("         0-dec;  1-enc\n");
        return -1;
    }
    dev_type = atoi(argv[1]);
    group = atoi(argv[2]);
    if (argc > 3) {
        auto_read = 0;
        printf("Data is not automatically read\n");
    }

    if (0 == dev_type) {
        sprintf(dev_path, "/dev/%s%d", ES_DEVICE_DEC, 1);
    } else {
        sprintf(dev_path, "/dev/%s%d", ES_DEVICE_ENC, 1);
    }

    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        printf("Cannot open device file..., %s\n", strerror(errno));
        return -1;
    }
    printf("open file %s success, fd: %d\n", dev_path, fd);

    chn.group = group;
    chn.channel = 0;
    if (0 != ioctl(fd, ES_CHN_IOC_ASSIGN_CHANNEL, &chn)) {
        printf("assign chn %u failed\n", chn.group);
        goto exit0;
    }

    printf("****Please Enter the Option******\n");
    printf("         wakeup count            \n");
    printf("*********************************\n");
    scanf(" %d", &opt);
    printf("group %u Your Option = %d\n", group, opt);

    unsigned int val = opt;
    ioctl(fd, ES_CHN_IOC_WAKEUP_COUNT_SET, &val);
    val = 0;
    ioctl(fd, ES_CHN_IOC_WAKEUP_COUNT_GET, &val);
    printf("group %u get wakeup count %u, fd: %d\n", group, val, fd);

    pthread_create(&hthread, NULL, readThread, NULL);

    while (1) {
        printf("****Please Enter the Option******\n");
        printf("        >0. send                 \n");
        printf("        0. Exit                  \n");
        printf("*********************************\n");
        scanf(" %d", &opt);
        printf("group %u Your Option = %d\n", group, opt);

        if (opt > 0) {
            unsigned int send = opt;
            val = send;
            ioctl(fd, ES_CHN_IOC_COUNT_ADD, &send);
            printf("send %u, left: %u\n", val, send);
        } else if (opt == 0) {
            thread_exit = 1;
            pthread_join(hthread, NULL);
            break;
        } else {
            val = -opt;
            unsigned int get = -opt;
            ioctl(fd, ES_CHN_IOC_COUNT_SUB, &get);
            printf("send %u, left: %u\n", val, get);
        }
    }
exit0:
    printf("cmodel driver test exit\n");

    return 0;
}
