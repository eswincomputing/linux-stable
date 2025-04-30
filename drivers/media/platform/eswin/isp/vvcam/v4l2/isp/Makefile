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


ifeq ($(VVCAM_PATH),)
        VVCAM_PATH:=$(abspath $(dir $(lastword $(MAKEFILE_LIST))))/../../../vvcam
        export VVCAM_PATH
endif
ISP_PATH := $(VVCAM_PATH)/v4l2/isp

include $(VVCAM_PATH)/Param.mk
TARGET = vvcam_isp_subdev

obj-$(CONFIG_VIDEO_ESWIN_ISP) += $(TARGET).o
$(TARGET)-objs += vvcam_isp_platform.o
$(TARGET)-objs += vvcam_isp_driver.o
$(TARGET)-objs += vvcam_isp_event.o
$(TARGET)-objs += vvcam_isp_procfs.o
$(TARGET)-objs += isp_ctrl/vvcam_isp_ctrl.o

EXTRA_CFLAGS += -I$(ISP_PATH)
EXTRA_CFLAGS += -I$(ISP_PATH)/../common/
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl

USER_INC:=$(ISP_PATH)/isp_ctrl/vvcam_isp_ctrl.h

ifeq ($(ISP_AE_V3), 1)
$(TARGET)-objs += isp_ctrl/ae/ae_v3/vvcam_isp_ae.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ae/ae_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/ae/ae_v3/*.h
endif

ifeq ($(ISP_AE_V4), 1)
$(TARGET)-objs += isp_ctrl/ae/ae_v4/vvcam_isp_ae.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ae/ae_v4/
USER_INC += $(ISP_PATH)/isp_ctrl/ae/ae_v4/*.h
endif

ifeq ($(ISP_AE_V4_1), 1)
$(TARGET)-objs += isp_ctrl/ae/ae_v4_1/vvcam_isp_ae.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ae/ae_v4_1/
USER_INC += $(ISP_PATH)/isp_ctrl/ae/ae_v4_1/*.h
endif

ifeq ($(ISP_AWB_V3), 1)
$(TARGET)-objs += isp_ctrl/awb/awb_v3/vvcam_isp_awb.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/awb/awb_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/awb/awb_v3/*.h
endif

ifeq ($(ISP_AWB_V4), 1)
$(TARGET)-objs += isp_ctrl/awb/awb_v4/vvcam_isp_awb.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/awb/awb_v4/
USER_INC += $(ISP_PATH)/isp_ctrl/awb/awb_v4/*.h
endif

ifeq ($(ISP_AWB_V4_1), 1)
$(TARGET)-objs += isp_ctrl/awb/awb_v4_1/vvcam_isp_awb.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/awb/awb_v4_1/
USER_INC += $(ISP_PATH)/isp_ctrl/awb/awb_v4_1/*.h
endif

ifeq ($(ISP_GC_V1), 1)
$(TARGET)-objs += isp_ctrl/gc/gc_v1/vvcam_isp_gc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/gc/gc_v1/
USER_INC += $(ISP_PATH)/isp_ctrl/gc/gc_v1/*.h
endif

ifeq ($(ISP_GC_V2), 1)
$(TARGET)-objs += isp_ctrl/gc/gc_v2/vvcam_isp_gc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/gc/gc_v2/
USER_INC += $(ISP_PATH)/isp_ctrl/gc/gc_v2/*.h
endif

ifeq ($(ISP_2DNR_V3), 1)
$(TARGET)-objs += isp_ctrl/2dnr/2dnr_v3/vvcam_isp_2dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/2dnr/2dnr_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/2dnr/2dnr_v3/*.h
endif

ifeq ($(ISP_2DNR_V5_2), 1)
$(TARGET)-objs += isp_ctrl/2dnr/2dnr_v5_2/vvcam_isp_2dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/2dnr/2dnr_v5_2/
USER_INC += $(ISP_PATH)/isp_ctrl/2dnr/2dnr_v5_2/*.h
endif

ifeq ($(ISP_2DNR_V6), 1)
$(TARGET)-objs += isp_ctrl/2dnr/2dnr_v6/vvcam_isp_2dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/2dnr/2dnr_v6/
USER_INC += $(ISP_PATH)/isp_ctrl/2dnr/2dnr_v6/*.h
endif

ifeq ($(ISP_EE_V1), 1)
$(TARGET)-objs += isp_ctrl/ee/ee_v1/vvcam_isp_ee.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ee/ee_v1/
USER_INC += $(ISP_PATH)/isp_ctrl/ee/ee_v1/*.h
endif

ifeq ($(ISP_EE_V2), 1)
$(TARGET)-objs += isp_ctrl/ee/ee_v2/vvcam_isp_ee.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ee/ee_v2/
USER_INC += $(ISP_PATH)/isp_ctrl/ee/ee_v2/*.h
endif

ifeq ($(ISP_EE_V3), 1)
$(TARGET)-objs += isp_ctrl/ee/ee_v3/vvcam_isp_ee.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ee/ee_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/ee/ee_v3/*.h
endif

ifeq ($(ISP_CPROC), 1)
$(TARGET)-objs += isp_ctrl/cproc/cproc/vvcam_isp_cproc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cproc/cproc/
USER_INC += $(ISP_PATH)/isp_ctrl/cproc/cproc/*.h
endif

ifeq ($(ISP_WDR_V3), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v3/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v3/*.h
endif

ifeq ($(ISP_WDR_V4), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v4/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v4/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v4/*.h
endif

ifeq ($(ISP_WDR_V5), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v5/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v5/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v5/*.h
endif

ifeq ($(ISP_WDR_V5_1), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v5_1/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v5_1/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v5_1/*.h
endif

ifeq ($(ISP_WDR_V5_2), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v5_2/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v5_2/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v5_2/*.h
endif

ifeq ($(ISP_WDR_V5_2_1), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v5_2_1/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v5_2_1/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v5_2_1/*.h
endif

ifeq ($(ISP_WDR_V5_3), 1)
$(TARGET)-objs += isp_ctrl/wdr/wdr_v5_3/vvcam_isp_wdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wdr/wdr_v5_3/
USER_INC += $(ISP_PATH)/isp_ctrl/wdr/wdr_v5_3/*.h
endif

ifeq ($(ISP_BLS), 1)
$(TARGET)-objs += isp_ctrl/bls/bls/vvcam_isp_bls.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/bls/bls/
USER_INC += $(ISP_PATH)/isp_ctrl/bls/bls/*.h
endif

ifeq ($(ISP_CCM), 1)
$(TARGET)-objs += isp_ctrl/ccm/ccm/vvcam_isp_ccm.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ccm/ccm/
USER_INC += $(ISP_PATH)/isp_ctrl/ccm/ccm/*.h
endif

ifeq ($(ISP_DPCC), 1)
$(TARGET)-objs += isp_ctrl/dpcc/dpcc/vvcam_isp_dpcc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dpcc/dpcc/
USER_INC += $(ISP_PATH)/isp_ctrl/dpcc/dpcc/*.h
endif

ifeq ($(ISP_GE), 1)
$(TARGET)-objs += isp_ctrl/ge/ge/vvcam_isp_ge.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ge/ge/
USER_INC += $(ISP_PATH)/isp_ctrl/ge/ge/*.h
endif

ifeq ($(ISP_LSC_V1), 1)
$(TARGET)-objs += isp_ctrl/lsc/lsc_v1/vvcam_isp_lsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/lsc/lsc_v1/
endif

ifeq ($(ISP_LSC_V2), 1)
$(TARGET)-objs += isp_ctrl/lsc/lsc_v2/vvcam_isp_lsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/lsc/lsc_v2/
USER_INC += $(ISP_PATH)/isp_ctrl/lsc/lsc_v2/*.h
endif

ifeq ($(ISP_LSC_V3), 1)
$(TARGET)-objs += isp_ctrl/lsc/lsc_v3/vvcam_isp_lsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/lsc/lsc_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/lsc/lsc_v3/*.h
endif

ifeq ($(ISP_LSC_V4), 1)
$(TARGET)-objs += isp_ctrl/lsc/lsc_v4/vvcam_isp_lsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/lsc/lsc_v4/
USER_INC += $(ISP_PATH)/isp_ctrl/lsc/lsc_v4/*.h
endif

ifeq ($(ISP_WB_V1_1), 1)
$(TARGET)-objs += isp_ctrl/wb/wb_v1_1/vvcam_isp_wb.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wb/wb_v1_1/
USER_INC += $(ISP_PATH)/isp_ctrl/wb/wb_v1_1/*.h
endif

ifeq ($(ISP_WB_V1), 1)
$(TARGET)-objs += isp_ctrl/wb/wb_v1/vvcam_isp_wb.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/wb/wb_v1/
USER_INC += $(ISP_PATH)/isp_ctrl/wb/wb_v1/*.h
endif

ifeq ($(ISP_3DNR_V1_2), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v1_2/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v1_2
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v1_2/*.h
endif

ifeq ($(ISP_3DNR_V2_1), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v2_1/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v2_1
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v2_1/*.h
endif

ifeq ($(ISP_3DNR_V2_4), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v2_4/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v2_4
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v2_4/*.h
endif

ifeq ($(ISP_3DNR_V3), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v3/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v3
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v3/*.h
endif

ifeq ($(ISP_3DNR_V3_1), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v3_1/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v3_1
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v3_1/*.h
endif

ifeq ($(ISP_3DNR_V4), 1)
$(TARGET)-objs += isp_ctrl/3dnr/3dnr_v4/vvcam_isp_3dnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/3dnr/3dnr_v4
USER_INC += $(ISP_PATH)/isp_ctrl/3dnr/3dnr_v4/*.h
endif

ifeq ($(ISP_AF_V3), 1)
$(TARGET)-objs += isp_ctrl/af/af_v3/vvcam_isp_af.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/af/af_v3
USER_INC += $(ISP_PATH)/isp_ctrl/af/af_v3/*.h
endif

ifeq ($(ISP_AF_V4), 1)
$(TARGET)-objs += isp_ctrl/af/af_v4/vvcam_isp_af.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/af/af_v4
USER_INC += $(ISP_PATH)/isp_ctrl/af/af_v4/*.h
endif

ifeq ($(ISP_AF_V4_1_1), 1)
$(TARGET)-objs += isp_ctrl/af/af_v4_1_1/vvcam_isp_af.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/af/af_v4_1_1
USER_INC += $(ISP_PATH)/isp_ctrl/af/af_v4_1_1/*.h
endif

ifeq ($(ISP_AF_V4_3), 1)
$(TARGET)-objs += isp_ctrl/af/af_v4_3/vvcam_isp_af.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/af/af_v4_3
USER_INC += $(ISP_PATH)/isp_ctrl/af/af_v4_3/*.h
endif

ifeq ($(ISP_PDAF_V1), 1)
$(TARGET)-objs += isp_ctrl/pdaf/pdaf_v1/vvcam_isp_pdaf.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/pdaf/pdaf_v1
USER_INC += $(ISP_PATH)/isp_ctrl/pdaf/pdaf_v1/*.h
endif

ifeq ($(ISP_AFM_V1), 1)
$(TARGET)-objs += isp_ctrl/afm/afm_v1/vvcam_isp_afm.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/afm/afm_v1/
USER_INC += $(ISP_PATH)/isp_ctrl/afm/afm_v1/*.h
endif

ifeq ($(ISP_AFM_V1_1), 1)
$(TARGET)-objs += isp_ctrl/afm/afm_v1_1/vvcam_isp_afm.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/afm/afm_v1_1/
USER_INC += $(ISP_PATH)/isp_ctrl/afm/afm_v1_1/*.h
endif

ifeq ($(ISP_AFM_V3), 1)
$(TARGET)-objs += isp_ctrl/afm/afm_v3/vvcam_isp_afm.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/afm/afm_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/afm/afm_v3/*.h
endif

ifeq ($(ISP_DMSC_V1), 1)
$(TARGET)-objs += isp_ctrl/dmsc/dmsc_v1/vvcam_isp_dmsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dmsc/dmsc_v1/
USER_INC += $(ISP_PATH)/isp_ctrl/dmsc/dmsc_v1/*.h
endif

ifeq ($(ISP_DMSC_V2), 1)
$(TARGET)-objs += isp_ctrl/dmsc/dmsc_v2/vvcam_isp_dmsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dmsc/dmsc_v2/
USER_INC += $(ISP_PATH)/isp_ctrl/dmsc/dmsc_v2/*.h
endif

ifeq ($(ISP_DMSC_V3), 1)
$(TARGET)-objs += isp_ctrl/dmsc/dmsc_v3/vvcam_isp_dmsc.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dmsc/dmsc_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/dmsc/dmsc_v3/*.h
endif

ifeq ($(ISP_EXP_V2), 1)
$(TARGET)-objs += isp_ctrl/exp/exp_v2/vvcam_isp_exp.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/exp/exp_v2/
USER_INC += $(ISP_PATH)/isp_ctrl/exp/exp_v2/*.h
endif

ifeq ($(ISP_EXP_V3), 1)
$(TARGET)-objs += isp_ctrl/exp/exp_v3/vvcam_isp_exp_v3.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/exp/exp_v3/
USER_INC += $(ISP_PATH)/isp_ctrl/exp/exp_v3/*.h
endif

ifeq ($(ISP_GTM_V1), 1)
$(TARGET)-objs += isp_ctrl/gtm/gtm_v1/vvcam_isp_gtm.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/gtm/gtm_v1
USER_INC += $(ISP_PATH)/isp_ctrl/gtm/gtm_v1/*.h
endif

ifeq ($(ISP_HDR_V1_3), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v1_3/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v1_3
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v1_3/*.h
endif

ifeq ($(ISP_HDR_V2), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v2/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v2
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v2/*.h
endif

ifeq ($(ISP_HDR_V2_1), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v2_1/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v2_1
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v2_1/*.h
endif

ifeq ($(ISP_HDR_V2_1_2DOL), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v2_1_2dol/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v2_1_2dol
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v2_1_2dol/*.h
endif

ifeq ($(ISP_HDR_V3), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v3/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v3
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v3/*.h
endif

ifeq ($(ISP_HDR_V3_2DOL), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v3_2dol/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v3_2dol
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v3_2dol/*.h
endif

ifeq ($(ISP_HDR_V3_1), 1)
$(TARGET)-objs += isp_ctrl/hdr/hdr_v3_1/vvcam_isp_hdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/hdr/hdr_v3_1
USER_INC += $(ISP_PATH)/isp_ctrl/hdr/hdr_v3_1/*.h
endif

ifeq ($(ISP_RGBIR_V1), 1)
$(TARGET)-objs += isp_ctrl/rgbir/rgbir_v1/vvcam_isp_rgbir.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/rgbir/rgbir_v1
USER_INC += $(ISP_PATH)/isp_ctrl/rgbir/rgbir_v1/*.h
endif

ifeq ($(ISP_RGBIR_V1_1), 1)
$(TARGET)-objs += isp_ctrl/rgbir/rgbir_v1_1/vvcam_isp_rgbir.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/rgbir/rgbir_v1_1
USER_INC += $(ISP_PATH)/isp_ctrl/rgbir/rgbir_v1_1/*.h
endif

ifeq ($(ISP_RGBIR_V2), 1)
$(TARGET)-objs += isp_ctrl/rgbir/rgbir_v2/vvcam_isp_rgbir.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/rgbir/rgbir_v2
USER_INC += $(ISP_PATH)/isp_ctrl/rgbir/rgbir_v2/*.h
endif

ifeq ($(ISP_RGBIR_V2_1), 1)
$(TARGET)-objs += isp_ctrl/rgbir/rgbir_v2_1/vvcam_isp_rgbir.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/rgbir/rgbir_v2_1
USER_INC += $(ISP_PATH)/isp_ctrl/rgbir/rgbir_v2_1/*.h
endif

ifeq ($(ISP_DG_V1), 1)
$(TARGET)-objs += isp_ctrl/dg/dg_v1/vvcam_isp_dg.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dg/dg_v1
USER_INC += $(ISP_PATH)/isp_ctrl/dg/dg_v1/*.h
endif

ifeq ($(ISP_CMPD_20BIT), 1)
$(TARGET)-objs += isp_ctrl/cpd/cpd_v1/vvcam_isp_cpd.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cpd/cpd_v1
USER_INC += $(ISP_PATH)/isp_ctrl/cpd/cpd_v1/*.h
endif

ifeq ($(ISP_CMPD_24BIT), 1)
$(TARGET)-objs += isp_ctrl/cpd/cpd_v1_1/vvcam_isp_cpd.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cpd/cpd_v1_1
USER_INC += $(ISP_PATH)/isp_ctrl/cpd/cpd_v1_1/*.h
endif

ifeq ($(ISP_DPF_V1), 1)
$(TARGET)-objs += isp_ctrl/dpf/dpf_v1/vvcam_isp_dpf.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/dpf/dpf_v1
USER_INC += $(ISP_PATH)/isp_ctrl/dpf/dpf_v1/*.h
endif

ifeq ($(ISP_CNR_V1), 1)
$(TARGET)-objs += isp_ctrl/cnr/cnr_v1/vvcam_isp_cnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cnr/cnr_v1
USER_INC += $(ISP_PATH)/isp_ctrl/cnr/cnr_v1/*.h
endif

ifeq ($(ISP_CNR_V2_1), 1)
$(TARGET)-objs += isp_ctrl/cnr/cnr_v2_1/vvcam_isp_cnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cnr/cnr_v2_1
USER_INC += $(ISP_PATH)/isp_ctrl/cnr/cnr_v2_1/*.h
endif

ifeq ($(ISP_CNR_V2_2), 1)
$(TARGET)-objs += isp_ctrl/cnr/cnr_v2_2/vvcam_isp_cnr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/cnr/cnr_v2_2
USER_INC += $(ISP_PATH)/isp_ctrl/cnr/cnr_v2_2/*.h
endif

ifeq ($(ISP_GWDR_V1), 1)
$(TARGET)-objs += isp_ctrl/gwdr/gwdr_v1/vvcam_isp_gwdr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/gwdr/gwdr_v1
USER_INC += $(ISP_PATH)/isp_ctrl/gwdr/gwdr_v1/*.h
endif

ifeq ($(ISP_YNR_V1), 1)
$(TARGET)-objs += isp_ctrl/ynr/ynr_v1/vvcam_isp_ynr.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/ynr/ynr_v1
USER_INC += $(ISP_PATH)/isp_ctrl/ynr/ynr_v1/*.h
endif

ifeq ($(ISP_LUT3D_V1), 1)
$(TARGET)-objs += isp_ctrl/lut3d/lut3d_v1/vvcam_isp_lut3d.o
EXTRA_CFLAGS += -I$(ISP_PATH)/isp_ctrl/lut3d/lut3d_v1
USER_INC += $(ISP_PATH)/isp_ctrl/lut3d/lut3d_v1/*.h
endif

