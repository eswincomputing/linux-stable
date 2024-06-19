# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -DSUPPORT_DMA_HEAP
ccflags-y += -DHANTROAXIFE_SUPPORT -DHANTROVCMD_ENABLE_IP_SUPPORT
es_venc-objs := vc8000_axife.o vc8000_normal_driver.o vc8000_vcmd_driver.o vc8000e_driver.o bidirect_list.o vcmdswhwregisters.o

obj-$(CONFIG_VIDEO_ESWIN_VENC) += es_venc.o
