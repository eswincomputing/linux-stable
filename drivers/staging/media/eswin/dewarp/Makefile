# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -DES_DW200_SDK
#ccflags-y += -D_ES_DW200_DEBUG_PRINT

es_dewarp-objs := dw200_fe.o dw200_ioctl.o vivdw200_irq_queue.o vvcam_dwe_driver.o dw200_dump.o

obj-$(CONFIG_VIDEO_ESWIN_DEWARP) += es_dewarp.o
