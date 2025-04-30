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

# Example: ./insmod.sh 4sensor 2isp vi mss

param_sensor_num="2sensor" 
param_isp_num="1isp"
param_videoin="false"
param_mss="false"
param_rdc="false"

if [ -n "$1" ];then
    if [ "$1" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$1" = "2isp" ];then
        param_isp_num="2isp"
    elif [ "$1" = "3isp" ];then
        param_isp_num="3isp"
    elif [ "$1" = "vi" ];then
        param_videoin="true"
    elif [ "$1" = "mss" ];then
        param_mss="true"
    elif [ "$1" = "rdc" ];then
        param_rdc="true"
    fi
fi

if [ -n "$2" ];then
    if [ "$2" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$2" = "2isp" ];then
        param_isp_num="2isp"
    elif [ "$2" = "3isp" ];then
        param_isp_num="3isp"
    elif [ "$2" = "vi" ];then
        param_videoin="true"
    elif [ "$2" = "mss" ];then
        param_mss="true"
    elif [ "$2" = "rdc" ];then
        param_rdc="true"
    fi
fi

if [ -n "$3" ];then
    if [ "$3" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$3" = "2isp" ];then
        param_isp_num="2isp"
    elif [ "$3" = "3isp" ];then
        param_isp_num="3isp"
    elif [ "$3" = "vi" ];then
        param_videoin="true"
    elif [ "$3" = "mss" ];then
        param_mss="true"
    elif [ "$3" = "rdc" ];then
        param_rdc="true"
    fi
fi

if [ -n "$4" ];then
    if [ "$4" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$4" = "2isp" ];then
        param_isp_num="2isp"
    elif [ "$4" = "3isp" ];then
        param_isp_num="3isp"
    elif [ "$4" = "vi" ];then
        param_videoin="true"
    elif [ "$4" = "mss" ];then
        param_mss="true"
    elif [ "$4" = "rdc" ];then
        param_rdc="true"
    fi
fi

if [ -n "$5" ];then
    if [ "$5" = "4sensor" ];then
        param_sensor_num="4sensor"
    elif [ "$5" = "2isp" ];then
        param_isp_num="2isp"
    elif [ "$5" = "3isp" ];then
        param_isp_num="3isp"
    elif [ "$5" = "vi" ];then
        param_videoin="true"
    elif [ "$5" = "mss" ];then
        param_mss="true"
    elif [ "$5" = "rdc" ];then
        param_rdc="true"
    fi
fi

insmod ./vb/vvcam_vb.ko

if [ "$param_sensor_num" = "4sensor" ];then
    insmod ./i2c/vvcam_i2c.ko devices_mask=0x0f
    insmod ./mipi/vvcam_mipi.ko devices_mask=0x0f
else
    insmod ./i2c/vvcam_i2c.ko devices_mask=0x03
    insmod ./mipi/vvcam_mipi.ko devices_mask=0x03
fi

if [ "$param_isp_num" = "3isp" ];then
    devices_mask=0x07
elif [ "$param_isp_num" = "2isp" ];then
    devices_mask=0x03
else
    devices_mask=0x01
fi

if [ "$param_rdc" = "true" ];then
    rdc_enable=1
elif [ "$param_rdc" = "false" ];then
    rdc_enable=0
else
    rdc_enable=0
fi

insmod ./isp/vvcam_isp.ko devices_mask=${devices_mask} rdc_enable=${rdc_enable}

if [ "$param_videoin" = "true" ];then
    insmod ./videoin/vvcam_vi.ko
fi

if [ "$param_mss" = "true" ];then
    insmod ./mi_subsystem/vvcam_mss.ko
fi
