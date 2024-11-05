// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#ifndef __ESWIN_DSP_OP_TYPES_H__
#define __ESWIN_DSP_OP_TYPES_H__

#include "es_type.h"

#define CACHE_LINE_SIZE 64
#define KERNEL_NAME_MAXLEN 128
#define KERNEL_LIB_NAME_MAXLEN 128
#define BUFFER_CNT_MAXSIZE 64

// tensor shape info in dsp ping pong tiling
typedef struct {
    ES_U16 tileC;
    ES_U16 tileH;
    ES_U16 tileW;
    ES_U32 offset;
} ES_DSP_TILE_S;

// padding information for tensor along h and w
typedef struct {
    ES_U8 top;
    ES_U8 bottom;
    ES_U8 left;
    ES_U8 right;
} ES_DSP_PAD_S;

// tensor shape and padding info in dsp ping pong tiling
typedef struct {
    ES_DSP_PAD_S pad;
    ES_DSP_TILE_S input;
    ES_DSP_TILE_S output;
} ES_DSP_TILE_INFO_S;

#define OPERATOR_NAME_MAXLEN 128

typedef struct DSP_OPERATOR_DESC_S {
    ES_U32 totalSize;                           /* Total byte size of this data structure. */
    ES_CHAR operatorName[OPERATOR_NAME_MAXLEN]; /* The authoritative name of the operator. */
    ES_U32 bufferCntCfg;                        /* Specify total number of parameter buffers. */
    ES_U32 bufferCntInput;                      /* Specify total number of input buffers. */
    ES_U32 bufferCntOutput;                     /* Specify total number of output buffers. */
    ES_U32 bufferSize[BUFFER_CNT_MAXSIZE];      /* Specify the byte size of each buffer. */
    ES_CHAR paramData[0];                       /* All parameter buffers are sequentially laid out in this field. */
} ES_DSP_OPERATOR_DESC_S;

#endif  // __ESWIN_DSP_OP_TYPES_H__
