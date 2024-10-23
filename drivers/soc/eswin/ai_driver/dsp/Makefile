#
# Copyright (c) 2017 Cadence Design Systems Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Alternatively you can use and distribute this file under the terms of
# the GNU General Public License version 2 or later.
#

BUILD_RELEASE ?= 1
DEBUG_LEVEL ?= 0

ccflags-y := -I$(src)/
ccflags-y += -I$(srctree)/drivers/soc/eswin
ccflags-y += -I$(srctree)/drivers/memory/eswin/es_dev_buf/include/linux
ccflags-y += -I$(srctree)/drivers/memory/eswin/es_iommu_rsv/include/linux
ccflags-y += -I$(srctree)/$(src)/../common
ccflags-y += -I$(srctree)/$(src)/../common/devices

obj-$(CONFIG_ESWIN_DSP) += eic7700_dsp.o

eic7700_dsp-y += dsp_main.o dsp_ioctl.o dsp_proc.o dsp_log.o

ifeq ($(DSP_ENV_SIM), 1)
eic7700_dsp-y += dsp_platform_sim.o

else
ccflags-y +=-I
eic7700_dsp-y += dsp_platform.o dsp_sram.o dsp_firmware.o dsp_pool.o \
				mloader/dsp_load_operator.o \
				mloader/xt_mld_loader.o \
				mloader/xt_mld_relocate.o


endif

ccflags-y += -DDSP_ENV_SIM=${DSP_ENV_SIM} -DBUILD_RELEASE=${BUILD_RELEASE} -DDEBUG_LEVEL=${DEBUG_LEVEL} -w
