/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/
#ifndef _ISP_VVDEFS_H_
#define _ISP_VVDEFS_H_

#define viv_check_retval(x)          \
	do {                         \
		if ((x))             \
			return -EIO; \
	} while (0)

#ifndef VIV_MEDIA_PIX_FMT
#define VIV_MEDIA_PIX_FMT
enum {
	MEDIA_PIX_FMT_YUV422SP = 0,
	MEDIA_PIX_FMT_YUV422I,
	MEDIA_PIX_FMT_YUV420SP,
	MEDIA_PIX_FMT_YUV444,
	MEDIA_PIX_FMT_RGB888,
	MEDIA_PIX_FMT_RGB888P,
	MEDIA_PIX_FMT_RAW8,
	MEDIA_PIX_FMT_RAW12,
	MEDIA_PIX_FMT_BGR888,
	MEDIA_PIX_FMT_BGR888P,
};
#endif

#ifndef __KERNEL__
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define pr_info(...) printf(__VA_ARGS__)
#define pr_err(...) printf(__VA_ARGS__)
#define pr_debug(...) printf(__VA_ARGS__)
#define __user
#define __iomem
#else /* __KERNEL__ */

/* if v4l2 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <linux/platform_device.h>
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef ALIGN_UP
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#endif

#define RESV_STREAMID_ISP(id) ((id) ? RESV_STREAMID_ISP1 : RESV_STREAMID_ISP0)
#define RESV_STREAMID_ISP0 (-2)
#define RESV_STREAMID_ISP1 (-3)
#define RESV_STREAMID_DWE (-4)

#define kzfree(x) kfree_sensitive(x) /* For backward compatibility */

#endif /* _ISP_VVDEFS_H_ */
