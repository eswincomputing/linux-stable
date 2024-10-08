// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __HETERO_TYPES_H__
#define __HETERO_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#define DSP_MAX_CORE_NUM 4

#if __KERNEL__
#include <linux/types.h>
#else
#if __GNUC__
/*code for GNU C compiler */
typedef char s8;
typedef unsigned char u8;
typedef short int s16;
typedef unsigned short int u16;
typedef int s32;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;

#ifndef __cplusplus
typedef u8 bool;
#define true 1
#define false 0
#endif

#elif _MSC_VER
/*code specific to MSVC compiler*/
typedef __int8 s8;
typedef unsigned __int8 u8;
typedef __int16 s16;
typedef unsigned __int16 u16;
typedef __int32 s32;
typedef unsigned __int32 u32;
typedef __int64 s64;
typedef unsigned __int64 u64;

#endif
#endif  // KERNEL
#ifdef __cplusplus
}
#endif

#endif
