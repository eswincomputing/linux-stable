# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_ESWIN_MC)				+= eswin_cpuid_hartid_convert.o
obj-$(CONFIG_ESWIN_RSV_MEMBLOCK)		+= eswin_memblock.o
obj-$(CONFIG_ESWIN_CODACACHE_CONTROLLER)	+= codacache/
obj-$(CONFIG_ESWIN_BUDDY)			+= es_buddy/
obj-$(CONFIG_ESWIN_PROC)			+= es_proc/
obj-$(CONFIG_ESWIN_RSVMEM_HEAP)			+= es_rsvmem_heap/
obj-$(CONFIG_ESWIN_RSVMEM_HEAP)			+= es_mmz_vb/

ES_MEM_HEADER := drivers/memory/eswin/

COPY_HEADERS := $(shell cp $(ES_MEM_HEADER)/*.h include/linux)