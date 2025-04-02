/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2024 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2024 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __VVCAM_VI_H__
#define __VVCAM_VI_H__
#include "vvcam_event.h"

enum {
    VVCAM_EID_VIDEO_IN_MCM_PATH0        = 0x00,
    VVCAM_EID_VIDEO_IN_MCM_PATH1,
    VVCAM_EID_VIDEO_IN_MCM_PATH2,
    VVCAM_EID_VIDEO_IN_MCM_PATH3,
    VVCAM_EID_VIDEO_IN_MCM_PATH4,
    VVCAM_EID_VIDEO_IN_MCM_PATH5,
    VVCAM_EID_VIDEO_IN_MCM_PATH6,
    VVCAM_EID_VIDEO_IN_MCM_PATH7,
    VVCAM_EID_VIDEO_IN_MCM_PATH8,
    VVCAM_EID_VIDEO_IN_MCM_PATH9,
    VVCAM_EID_VIDEO_IN_MCM_PATH10,
    VVCAM_EID_VIDEO_IN_MCM_PATH11,
    VVCAM_EID_VIDEO_IN_MCM_PATH12,
    VVCAM_EID_VIDEO_IN_MCM_PATH13,
    VVCAM_EID_VIDEO_IN_MCM_PATH14,
    VVCAM_EID_VIDEO_IN_MCM_PATH15,
    VVCAM_EID_VIDEO_IN_MCM_MAX                      ,
};

enum {
    VVCAM_EID_VIDEO_IN_ADAPT_0  = VVCAM_EID_VIDEO_IN_MCM_PATH15 + 1,
    VVCAM_EID_VIDEO_IN_ADAPT_1,
    VVCAM_EID_VIDEO_IN_ADAPT_2,
    VVCAM_EID_VIDEO_IN_ADAPT_3,
    VVCAM_EID_VIDEO_IN_ADAPT_MAX                      ,
};

enum {
    VVCAM_EID_VIDEO_IN_FUSA_LV1 = VVCAM_EID_VIDEO_IN_ADAPT_3 + 1,
    VVCAM_EID_VIDEO_IN_FUSA_ECC,
    VVCAM_EID_VIDEO_IN_FUSA_PARITY_1,
    VVCAM_EID_VIDEO_IN_FUSA_PARITY_2,
    VVCAM_EID_VIDEO_IN_FUSA_ADAP_PIXCNT,
    VVCAM_EID_VIDEO_IN_FUSA_RDC_PIXCNT,
    VVCAM_EID_VIDEO_IN_FUSA_RDC_BIST_1,
    VVCAM_EID_VIDEO_IN_FUSA_RDC_BIST_2,
    VVCAM_EID_VIDEO_IN_FUSA_MAX
};

typedef struct {
    uint32_t addr;
    uint32_t value;
} vvcam_vi_reg_t;

#define VVCAM_VI_IOC_MAGIC 'I'          //video in magic
#define VVCAM_VI_RESET             _IOW(VVCAM_VI_IOC_MAGIC, 0x01, uint32_t)
#define VVCAM_VI_READ_REG          _IOWR(VVCAM_VI_IOC_MAGIC, 0x02, vvcam_vi_reg_t)
#define VVCAM_VI_WRITE_REG         _IOW(VVCAM_VI_IOC_MAGIC, 0x03, vvcam_vi_reg_t)
#define VVCAM_VI_SUBSCRIBE_EVENT   _IOW(VVCAM_VI_IOC_MAGIC, 0x04, vvcam_subscription_t)
#define VVCAM_VI_UNSUBSCRIBE_EVENT _IOW(VVCAM_VI_IOC_MAGIC, 0x05, vvcam_subscription_t)
#define VVCAM_VI_DQEVENT           _IOR(VVCAM_VI_IOC_MAGIC, 0x06, vvcam_event_t)

#endif
