// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * ESWIN MMZ VB user state api header.
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#ifndef ES_VB_USER_H
#define ES_VB_USER_H

typedef unsigned int ES_U32;
typedef unsigned long long ES_U64;
typedef char ES_CHAR;

/**
 * mmz vb configurations
*/
#define ES_VB_MAX_MMZs		4

#define ES_VB_INVALID_POOLID	(-1U)

#define ES_VB_MAX_MOD_POOL	16

#define ES_MAX_MMZ_NAME_LEN	64

/**
 * numa id encoding definitions
*/
typedef enum {
    EIC770X_LOGICAL_FLAT_MEM_NODE_0 = 0,
    EIC770X_LOGICAL_FLAT_MEM_NODE_1 = 1,
    EIC770X_LOGICAL_SPRAM_NODE_0 = 500,
    EIC770X_LOGICAL_SPRAM_NODE_1 = 501,
    EIC770X_LOGICAL_INTERLEAVE_MEM_NODE = 1000,
} EIC770X_LOGICAL_MEM_NODE_E;

/**
 * mmz vb pool or block struct definition
 */
typedef enum esVB_UID_E {
     VB_UID_PRIVATE = 0,
     VB_UID_COMMON,
     VB_UID_VI,
     VB_UID_VO,
     VB_UID_VPS,
     VB_UID_VENC,
     VB_UID_VDEC,
     VB_UID_HAE,
     VB_UID_USER,
     VB_UID_BUTT,
     VB_UID_MAX,
} VB_UID_E;

typedef enum {
    SYS_CACHE_MODE_NOCACHE = 0,
    SYS_CACHE_MODE_CACHED = 1,
    SYS_CACHE_MODE_LLC = 2,
    SYS_CACHE_MODE_BUTT,
} SYS_CACHE_MODE_E;

typedef struct esVB_POOL_CONFIG_S {
    ES_U64 blkSize;
    ES_U32 blkCnt;
    SYS_CACHE_MODE_E enRemapMode;
    ES_CHAR mmzName[ES_MAX_MMZ_NAME_LEN];
} VB_POOL_CONFIG_S;

typedef struct esVB_CONFIG_S {
    ES_U32 poolCnt;
    VB_POOL_CONFIG_S poolCfgs[ES_VB_MAX_MOD_POOL];
} VB_CONFIG_S;

typedef struct ES_DEV_BUF {
    ES_U64 memFd;
    ES_U64 offset;
    ES_U64 size;
    ES_U64 reserve;
} ES_DEV_BUF_S;

#endif
