# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -DSUPPORT_DMA_HEAP
es_vdec-objs := hantro_dec.o \
		hantro_mmu.o \
		hantro_axife.o \
		hantro_vcmd.o \
		bidirect_list.o \
		vcmdswhwregisters.o \
		subsys.o \
		hantro_afbc.o \
		dts_parser.o

obj-$(CONFIG_VIDEO_ESWIN_VDEC) += es_vdec.o