// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef MD5_H
#define MD5_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

#define MD5_LENGTH 33
#define DSP_BUF_NUM 8

typedef struct _conv_md5 {
    unsigned char wgt_md5[MD5_LENGTH];
} conv_md5_t;
typedef struct _sdp_md5 {
    unsigned char x1_md5[MD5_LENGTH];
    unsigned char x2_md5[MD5_LENGTH];
    unsigned char y_md5[MD5_LENGTH];
} sdp_md5_t;
typedef struct _dsp_md5 {
    int16_t src_num;
    int16_t dst_num;
    unsigned char src_md5[DSP_BUF_NUM][MD5_LENGTH];
    unsigned char dst_md5[DSP_BUF_NUM][MD5_LENGTH];
} dsp_md5_t;
typedef union _md5_spec {
    conv_md5_t conv_md5;
    sdp_md5_t sdp_md5;
    dsp_md5_t dsp_md5;
} md5_spec_t;

typedef struct _md5_container {
    int16_t calced_flag;
    int16_t writed_flag;
    uint32_t op_type;
    unsigned char src_md5[MD5_LENGTH];
    unsigned char dst_md5[MD5_LENGTH];
    md5_spec_t md5_spec;
} md5_container_t;

#define MD5_DIGEST_LEN 16

typedef struct _md5_ctx_t {
    uint32_t state[4];
    uint32_t count[2];
    unsigned char buffer[64];
} md5_ctx_t;

void MD5Init(md5_ctx_t *context);
void MD5Update(md5_ctx_t *context, const unsigned char *input, size_t inputLen);
void MD5Final(unsigned char digest[MD5_DIGEST_LEN], md5_ctx_t *context);

#ifdef __cplusplus
}
#endif
#endif
