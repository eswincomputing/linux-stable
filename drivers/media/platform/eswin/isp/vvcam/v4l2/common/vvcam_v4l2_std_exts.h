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


#ifndef __VVCAM_V4L2_STD_EXT_H__
#define __VVCAM_V4L2_STD_EXT_H__

#include <linux/videodev2.h>



// 400-8bit 
#ifndef V4L2_PIX_FMT_GREY
#define V4L2_PIX_FMT_GREY v4l2_fourcc('G', 'R', 'E', 'Y')
#endif

// 400-10bit unalign 
#ifndef V4L2_PIX_FMT_Y10BPACK
#define V4L2_PIX_FMT_Y10BPACK v4l2_fourcc('Y', '1', '0', 'B')
#endif

// 400-10bit align mode0 
#ifndef V4L2_PIX_FMT_Y10DWA
#define V4L2_PIX_FMT_Y10DWA v4l2_fourcc('Y', '1', '0', 'D')
#endif

// 400-10bit align mode1
#ifndef V4L2_PIX_FMT_Y10
#define V4L2_PIX_FMT_Y10 v4l2_fourcc('Y', '1', '0', '')
#endif

/////////////////////////////////////////////////////////
// 420sp-10bit unalign
#ifndef V4L2_PIX_FMT_P00BPACK
#define V4L2_PIX_FMT_P00BPACK v4l2_fourcc('P', '0', '0', 'B')
#endif

// 420sp-10bit double word align
#ifndef V4L2_PIX_FMT_P00DWA
#define V4L2_PIX_FMT_P00DWA v4l2_fourcc('P', '0', '0', 'D')
#endif

// 420sp-10bit align mode 1,every pixel 16bits
#ifndef V4L2_PIX_FMT_P010
#define V4L2_PIX_FMT_P010 v4l2_fourcc('P', '0', '1', '0')
#endif

// 420sp-12bit unalign
#ifndef V4L2_PIX_FMT_P02BPACK
#define V4L2_PIX_FMT_P02BPACK v4l2_fourcc('P', '0', '2', 'B')
#endif

/////////////////////////////////////////////////////////
// 422sp-10bit unalign
#ifndef V4L2_PIX_FMT_P20BPACK
#define V4L2_PIX_FMT_P20BPACK v4l2_fourcc('P', '2', '0', 'B')
#endif

// 422sp-10bit double word align
#ifndef V4L2_PIX_FMT_P20DWA
#define V4L2_PIX_FMT_P20DWA v4l2_fourcc('P', '2', '0', 'D')
#endif

// 422sp-10bit mode 1
#ifndef V4L2_PIX_FMT_P210
#define V4L2_PIX_FMT_P210 v4l2_fourcc('P', '2', '1', '0')
#endif

// 422sp-12bit unalign
#ifndef V4L2_PIX_FMT_P22BPACK
#define V4L2_PIX_FMT_P22BPACK v4l2_fourcc('P', '2', '2', 'B')
#endif

// 422I-10bit unalign
#ifndef V4L2_PIX_FMT_I20BPACK
#define V4L2_PIX_FMT_I20BPACK v4l2_fourcc('I', '2', '0', 'B')
#endif

// 422I-10bit align mode 1, every pixel 16bits
#ifndef V4L2_PIX_FMT_I210
#define V4L2_PIX_FMT_I210 v4l2_fourcc('I', '2', '1', '0')
#endif

/////////////////////////////////////////////////////////

// 444P-8bit unalign
#ifndef V4L2_PIX_FMT_M48BPACK
#define V4L2_PIX_FMT_M48BPACK v4l2_fourcc('M', '4', '8', 'B')
#endif

// 444I 8bit unalign
#ifndef V4L2_PIX_FMT_I48BPACK
#define V4L2_PIX_FMT_I48BPACK v4l2_fourcc('I', '4', '8', 'B')
#endif

// 444I 8bit align mode 0 
#ifndef V4L2_PIX_FMT_I48DWA
#define V4L2_PIX_FMT_I48DWA v4l2_fourcc('I', '4', '8', 'D')
#endif

// 444I 10bit mode0 
#ifndef V4L2_PIX_FMT_I40DWA
#define V4L2_PIX_FMT_I40DWA v4l2_fourcc('I', '4', '0', 'D')
#endif


/////////////////////////////////////////////////////////
// RGB
// RGB888
#ifndef V4L2_PIX_FMT_RGB24
#define V4L2_PIX_FMT_RGB24 v4l2_fourcc('R', 'G', 'B', '3')
#endif

// RGB888 align mode 0
#ifndef V4L2_PIX_FMT_RGB24DWA
#define V4L2_PIX_FMT_RGB24DWA v4l2_fourcc('R', 'G', '3', 'D')
#endif

// RGB888P
#ifndef V4L2_PIX_FMT_RGB24P
#define V4L2_PIX_FMT_RGB24P v4l2_fourcc('R', 'G', '3', 'P')
#endif

/////////////////////////////////////////////////////////
// RAW
// 10 bit bit-pack
#ifndef V4L2_PIX_FMT_SBGGR10BPACK
#define V4L2_PIX_FMT_SBGGR10BPACK v4l2_fourcc('B', 'G', '0', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGBRG10BPACK
#define V4L2_PIX_FMT_SGBRG10BPACK v4l2_fourcc('G', 'B', '0', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGRBG10BPACK
#define V4L2_PIX_FMT_SGRBG10BPACK v4l2_fourcc('G', 'R', '0', 'B')
#endif

#ifndef V4L2_PIX_FMT_SRGGB10BPACK
#define V4L2_PIX_FMT_SRGGB10BPACK v4l2_fourcc('R', 'G', '0', 'B')
#endif

// 10 bit dwa
#ifndef V4L2_PIX_FMT_SBGGR10DWA
#define V4L2_PIX_FMT_SBGGR10DWA v4l2_fourcc('B', 'G', '0', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGBRG10DWA
#define V4L2_PIX_FMT_SGBRG10DWA v4l2_fourcc('G', 'B', '0', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGRBG10DWA
#define V4L2_PIX_FMT_SGRBG10DWA v4l2_fourcc('G', 'R', '0', 'D')
#endif

#ifndef V4L2_PIX_FMT_SRGGB10DWA
#define V4L2_PIX_FMT_SRGGB10DWA v4l2_fourcc('R', 'G', '0', 'D')
#endif

// 12 bit bit-pack
#ifndef V4L2_PIX_FMT_SBGGR12BPACK
#define V4L2_PIX_FMT_SBGGR12BPACK v4l2_fourcc('B', 'G', '2', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGBRG12BPACK
#define V4L2_PIX_FMT_SGBRG12BPACK v4l2_fourcc('G', 'B', '2', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGRBG12BPACK
#define V4L2_PIX_FMT_SGRBG12BPACK v4l2_fourcc('G', 'R', '2', 'B')
#endif

#ifndef V4L2_PIX_FMT_SRGGB12BPACK
#define V4L2_PIX_FMT_SRGGB12BPACK v4l2_fourcc('R', 'G', '2', 'B')
#endif

// 12 bit dwa
#ifndef V4L2_PIX_FMT_SBGGR12DWA
#define V4L2_PIX_FMT_SBGGR12DWA v4l2_fourcc('B', 'G', '2', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGBRG12DWA
#define V4L2_PIX_FMT_SGBRG12DWA v4l2_fourcc('G', 'B', '2', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGRBG12DWA
#define V4L2_PIX_FMT_SGRBG12DWA v4l2_fourcc('G', 'R', '2', 'D')
#endif

#ifndef V4L2_PIX_FMT_SRGGB12DWA
#define V4L2_PIX_FMT_SRGGB12DWA v4l2_fourcc('R', 'G', '2', 'D')
#endif

// 14 bit bit-pack
#ifndef V4L2_PIX_FMT_SBGGR14BPACK
#define V4L2_PIX_FMT_SBGGR14BPACK v4l2_fourcc('B', 'G', '4', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGBRG14BPACK
#define V4L2_PIX_FMT_SGBRG14BPACK v4l2_fourcc('G', 'B', '4', 'B')
#endif

#ifndef V4L2_PIX_FMT_SGRBG14BPACK
#define V4L2_PIX_FMT_SGRBG14BPACK v4l2_fourcc('G', 'R', '4', 'B')
#endif

#ifndef V4L2_PIX_FMT_SRGGB14BPACK
#define V4L2_PIX_FMT_SRGGB14BPACK v4l2_fourcc('R', 'G', '4', 'B')
#endif

// 14 bit dwa
#ifndef V4L2_PIX_FMT_SBGGR14DWA
#define V4L2_PIX_FMT_SBGGR14DWA v4l2_fourcc('B', 'G', '4', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGBRG14DWA
#define V4L2_PIX_FMT_SGBRG14DWA v4l2_fourcc('G', 'B', '4', 'D')
#endif

#ifndef V4L2_PIX_FMT_SGRBG14DWA
#define V4L2_PIX_FMT_SGRBG14DWA v4l2_fourcc('G', 'R', '4', 'D')
#endif

#ifndef V4L2_PIX_FMT_SRGGB14DWA
#define V4L2_PIX_FMT_SRGGB14DWA v4l2_fourcc('R', 'G', '4', 'D')
#endif

// 14 bit align mode 1
#ifndef V4L2_PIX_FMT_SBGGR14
#define V4L2_PIX_FMT_SBGGR14 v4l2_fourcc('B', 'G', '1', '4')
#endif

#ifndef V4L2_PIX_FMT_SGBRG14
#define V4L2_PIX_FMT_SGBRG14 v4l2_fourcc('G', 'B', '1', '4')
#endif

#ifndef V4L2_PIX_FMT_SGRBG14
#define V4L2_PIX_FMT_SGRBG14 v4l2_fourcc('G', 'R', '1', '4')
#endif

#ifndef V4L2_PIX_FMT_SRGGB14
#define V4L2_PIX_FMT_SRGGB14 v4l2_fourcc('R', 'G', '1', '4')
#endif

// 16 bit unalign
#ifndef V4L2_PIX_FMT_SBGGR16
#define V4L2_PIX_FMT_SBGGR16 v4l2_fourcc('B', 'Y', 'R', '2')
#endif

#ifndef V4L2_PIX_FMT_SGBRG16
#define V4L2_PIX_FMT_SGBRG16 v4l2_fourcc('G', 'B', '1', '6')
#endif

#ifndef V4L2_PIX_FMT_SGRBG16
#define V4L2_PIX_FMT_SGRBG16 v4l2_fourcc('G', 'R', '1', '6')
#endif

#ifndef V4L2_PIX_FMT_SRGGB16
#define V4L2_PIX_FMT_SRGGB16 v4l2_fourcc('R', 'G', '1', '6')
#endif

// 24 bit unalign
#ifndef V4L2_PIX_FMT_SBGGR24
#define V4L2_PIX_FMT_SBGGR24 v4l2_fourcc('B', 'G', '2', '4')
#endif

#ifndef V4L2_PIX_FMT_SGBRG24
#define V4L2_PIX_FMT_SGBRG24 v4l2_fourcc('G', 'B', '2', '4')
#endif

#ifndef V4L2_PIX_FMT_SGRBG24
#define V4L2_PIX_FMT_SGRBG24 v4l2_fourcc('G', 'R', '2', '4')
#endif

#ifndef V4L2_PIX_FMT_SRGGB24
#define V4L2_PIX_FMT_SRGGB24 v4l2_fourcc('R', 'G', '2', '4')
#endif


/* media bus format */
#ifndef MEDIA_BUS_FMT_RGB888_3X8
#define MEDIA_BUS_FMT_RGB888_3X8        0x101c
#endif

#ifndef MEDIA_BUS_FMT_SBGGR14_1X14
#define MEDIA_BUS_FMT_SBGGR14_1X14      0x3019
#endif

#ifndef MEDIA_BUS_FMT_SGBRG14_1X14
#define MEDIA_BUS_FMT_SGBRG14_1X14      0x301a
#endif

#ifndef MEDIA_BUS_FMT_SGRBG14_1X14
#define MEDIA_BUS_FMT_SGRBG14_1X14      0x301b
#endif

#ifndef MEDIA_BUS_FMT_SRGGB14_1X14
#define MEDIA_BUS_FMT_SRGGB14_1X14      0x301c
#endif

#ifndef MEDIA_BUS_FMT_SBGGR16_1X16
#define MEDIA_BUS_FMT_SBGGR16_1X16       0x301d
#endif

#ifndef MEDIA_BUS_FMT_SGBRG16_1X16
#define MEDIA_BUS_FMT_SGBRG16_1X16       0x301e
#endif

#ifndef MEDIA_BUS_FMT_SGRBG16_1X16
#define MEDIA_BUS_FMT_SGRBG16_1X16       0x301f
#endif

#ifndef MEDIA_BUS_FMT_SRGGB16_1X16
#define MEDIA_BUS_FMT_SRGGB16_1X16       0x3020
#endif

#ifndef MEDIA_BUS_FMT_SBGGR24_1X24
#define MEDIA_BUS_FMT_SBGGR24_1X24       0x3021
#endif

#ifndef MEDIA_BUS_FMT_SGBRG24_1X24
#define MEDIA_BUS_FMT_SGBRG24_1X24       0x3022
#endif

#ifndef MEDIA_BUS_FMT_SGRBG24_1X24
#define MEDIA_BUS_FMT_SGRBG24_1X24       0x3023
#endif

#ifndef MEDIA_BUS_FMT_SRGGB24_1X24
#define MEDIA_BUS_FMT_SRGGB24_1X24       0x3024
#endif

#endif
