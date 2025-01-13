# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
ccflags-y += -I$(srctree)/drivers/soc/eswin/
ccflags-y += -I$(srctree)/drivers/memory/eswin/es_iommu_rsv/include/linux

snd-sof-eswin-dsp-objs := es-sof-dsp.o es-common.o

obj-$(CONFIG_SND_SOC_SOF_ESWIN) += snd-sof-eswin-dsp.o
