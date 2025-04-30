##############################################################################
#
#    The MIT License (MIT)
#
#    Copyright (c) 2014 - 2024 Vivante Corporation
#
#    Permission is hereby granted, free of charge, to any person obtaining a
#    copy of this software and associated documentation files (the "Software"),
#    to deal in the Software without restriction, including without limitation
#    the rights to use, copy, modify, merge, publish, distribute, sublicense,
#    and/or sell copies of the Software, and to permit persons to whom the
#    Software is furnished to do so, subject to the following conditions:
#
#    The above copyright notice and this permission notice shall be included in
#    all copies or substantial portions of the Software.
#
#    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#    DEALINGS IN THE SOFTWARE.
#
##############################################################################
#
#    The GPL License (GPL)
#
#    Copyright (C) 2014 - 2024 Vivante Corporation
#
#    This program is free software; you can redistribute it and/or
#    modify it under the terms of the GNU General Public License
#    as published by the Free Software Foundation; either version 2
#    of the License, or (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software Foundation,
#    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
##############################################################################
#
#    Note: This software is released under dual MIT and GPL licenses. A
#    recipient may use this file under the terms of either the MIT license or
#    GPL License. If you wish to use only one license not the other, you can
#    indicate your decision by deleting one of the above license notices in your
#    version of this file.
#
##############################################################################


#!/bin/sh

param_sensor_num="2sensor"
param_mcm_mask="false"
param_mss="false"

if [ -n "$1" ];then
    if [ "$1" = "mcm" ];then
        param_mcm_mask="true"
    elif [ "$1" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$1" = "mss" ];then
        param_mss="true"
    fi
fi

if [ -n "$2" ];then
    if [ "$2" = "mcm" ];then
        param_mcm_mask="true"
    elif [ "$2" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$2" = "mss" ];then
        param_mss="true"
    fi
fi

if [ -n "$3" ];then
    if [ "$3" = "mcm" ];then
        param_mcm_mask="true"
    elif [ "$3" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$3" = "mss" ];then
        param_mss="true"
    fi
fi

modprobe i2c-dev

if [ "$param_sensor_num" = "4sensor" ];then
    insmod ./i2c/vvcam_i2c.ko devices_mask=0x0f
    insmod ./mipi/vvcam_mipi.ko devices_mask=0x0f
else
    insmod ./i2c/vvcam_i2c.ko devices_mask=0x03
    insmod ./mipi/vvcam_mipi.ko devices_mask=0x03
fi

insmod ./isp/vvcam_isp.ko
insmod ./vb/vvcam_vb.ko
modprobe videobuf2-v4l2
modprobe videodev
modprobe mc
modprobe v4l2-async
modprobe videobuf2-dma-contig
modprobe v4l2-fwnode
modprobe videobuf2-common
insmod ./v4l2/isp/vvcam_isp_subdev.ko

if [ "$param_mcm_mask" = "true" ];then
    insmod ./v4l2/video/vvcam_video.ko mcm_mask=0x01
else
    insmod ./v4l2/video/vvcam_video.ko
fi

if [ "$param_mss" = "true" ];then
    insmod ./mi_subsystem/vvcam_mss.ko
fi
