# SPDX-License-Identifier: GPL-2.0-only
HAE_DIR := $(srctree)/drivers/staging/media/eswin/hae

VIVANTE_ENABLE_3D                 ?= 0
VIVANTE_ENABLE_2D                 ?= 1
VIVANTE_ENABLE_VG                 ?= 0
VIVANTE_ENABLE_DRM                ?= 0
NO_DMA_COHERENT                   ?= 0
USE_PLATFORM_DRIVER               ?= 1
ENABLE_GPU_CLOCK_BY_DRIVER        ?= 1
CACHE_FUNCTION_UNIMPLEMENTED      ?= 0
USE_BANK_ALIGNMENT                ?= 0
BANK_BIT_START                    ?= 0
BANK_BIT_END                      ?= 0
BANK_CHANNEL_BIT                  ?= 0
SECURITY                          ?= 0
SOC_PLATFORM                      ?= eswin-win2030

soc_vendor    := $(firstword $(subst -, ,$(SOC_PLATFORM)))
soc_board     := $(lastword  $(subst -, ,$(SOC_PLATFORM)))

OS_KERNEL_DIR   := hal/os/linux/kernel
ARCH_KERNEL_DIR := hal/kernel/arch
ARCH_VG_KERNEL_DIR := hal/kernel/archvg
HAL_KERNEL_DIR  := hal/kernel
TA_DIR          := hal/security_v1

MODULE_NAME ?= es_hae
CUSTOMER_ALLOCATOR_OBJS    ?=
ALLOCATOR_ARRAY_H_LOCATION ?= $(OS_KERNEL_DIR)/allocator/default/
SOC_PLATFORM_LOCATION      ?= $(OS_KERNEL_DIR)/platform/$(soc_vendor)

# Set common platform driver source
SOC_PLATFORM_OBJS ?= $(SOC_PLATFORM_LOCATION)/gc_hal_kernel_platform_$(soc_board).o
# Include platform config if exists.
-include $(SOC_PLATFORM_LOCATION)/gc_hal_kernel_platform_$(soc_board).config

ifeq ($(CONFIG_KASAN),)
EXTRA_CFLAGS += -Werror -Wno-implicit-fallthrough -Wno-enum-int-mismatch
ccflags-$(CONFIG_WERROR) += -Werror
endif

OBJS := $(OS_KERNEL_DIR)/gc_hal_kernel_device.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_linux.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_math.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_os.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_iommu.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_debug.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_debugfs.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_allocator.o \
        $(OS_KERNEL_DIR)/allocator/default/gc_hal_kernel_allocator_user_memory.o \
        $(OS_KERNEL_DIR)/allocator/default/gc_hal_kernel_allocator_dma.o \
        $(OS_KERNEL_DIR)/allocator/default/gc_hal_kernel_allocator_gfp.o \
        $(OS_KERNEL_DIR)/allocator/default/gc_hal_kernel_allocator_reserved_mem.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_driver.o

# Source files for soc platform board
OBJS += $(SOC_PLATFORM_OBJS)

ifneq ($(CONFIG_DMA_SHARED_BUFFER),)
OBJS += $(OS_KERNEL_DIR)/allocator/default/gc_hal_kernel_allocator_dmabuf.o
endif

ifneq ($(CONFIG_DRM),)
OBJS += $(OS_KERNEL_DIR)/gc_hal_kernel_drm.o
endif

OBJS += $(HAL_KERNEL_DIR)/gc_hal_kernel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_command.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_db.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_event.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_heap.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_mmu.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_video_memory.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_power.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_security_v1.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_preemption.o

OBJS += $(ARCH_KERNEL_DIR)/gc_hal_kernel_context.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_func.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_func_flop_reset.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_async_fe.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_mc_fe.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_waitlink_fe.o



ifneq ($(CONFIG_SYNC),)
EXTRA_CFLAGS += -Idrivers/staging/android
EXTRA_CFLAGS += -DgcdLINUX_SYNC_FILE=1

OBJS += $(OS_KERNEL_DIR)/gc_hal_kernel_sync.o
else
  ifneq ($(CONFIG_SYNC_FILE),)
  EXTRA_CFLAGS += -DgcdLINUX_SYNC_FILE=1
  OBJS += $(OS_KERNEL_DIR)/gc_hal_kernel_sync.o
  endif
endif

ifeq ($(SECURITY),1)
OBJS += $(OS_KERNEL_DIR)/gc_hal_kernel_security_channel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_security.o
endif

ifneq ($(CUSTOMER_ALLOCATOR_OBJS),)
OBJS += $(CUSTOMER_ALLOCATOR_OBJS)
endif

OBJS += $(OS_KERNEL_DIR)/gc_hal_kernel_security_channel_emulator.o \
        $(TA_DIR)/gc_hal_ta.o \
        $(TA_DIR)/gc_hal_ta_hardware.o \
        $(TA_DIR)/gc_hal_ta_mmu.o \
        $(TA_DIR)/os/emulator/gc_hal_ta_emulator.o

ifeq ($(FLAREON),1)
EXTRA_CFLAGS += -DFLAREON
endif

ifeq ($(DEBUG),1)
EXTRA_CFLAGS += -DDBG=1 -DDEBUG -D_DEBUG -g
else
EXTRA_CFLAGS += -DDBG=0
endif

ifeq ($(NO_DMA_COHERENT),1)
EXTRA_CFLAGS += -DNO_DMA_COHERENT
endif

ifeq ($(CONFIG_DOVE_GPU),1)
EXTRA_CFLAGS += -DCONFIG_DOVE_GPU=1
endif

ifneq ($(USE_PLATFORM_DRIVER),0)
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=1
else
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=0
endif

EXTRA_CFLAGS += -DVIVANTE_PROFILER=1
EXTRA_CFLAGS += -DVIVANTE_PROFILER_CONTEXT=1

ifeq ($(ENABLE_GPU_CLOCK_BY_DRIVER),1)
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=1
else
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=0
endif

ifeq ($(USE_NEW_LINUX_SIGNAL),1)
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=1
else
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=0
endif

ifeq ($(USE_LINUX_PCIE),1)
EXTRA_CFLAGS += -DUSE_LINUX_PCIE=1
else
EXTRA_CFLAGS += -DUSE_LINUX_PCIE=0
endif

ifeq ($(CACHE_FUNCTION_UNIMPLEMENTED),1)
EXTRA_CFLAGS += -DgcdCACHE_FUNCTION_UNIMPLEMENTED=1
else
EXTRA_CFLAGS += -DgcdCACHE_FUNCTION_UNIMPLEMENTED=0
endif

EXTRA_CFLAGS += -DgcdENABLE_3D=0

EXTRA_CFLAGS += -DgcdENABLE_2D=1

EXTRA_CFLAGS += -DgcdENABLE_VG=0

EXTRA_CFLAGS += -DgcdSUPPORT_DEVICE_TREE_SOURCE


ifeq ($(USE_BANK_ALIGNMENT),1)
    EXTRA_CFLAGS += -DgcdENABLE_BANK_ALIGNMENT=1
    ifneq ($(BANK_BIT_START),0)
	        ifneq ($(BANK_BIT_END),0)
	            EXTRA_CFLAGS += -DgcdBANK_BIT_START=$(BANK_BIT_START)
	            EXTRA_CFLAGS += -DgcdBANK_BIT_END=$(BANK_BIT_END)
	        endif
    endif

    ifneq ($(BANK_CHANNEL_BIT),0)
        EXTRA_CFLAGS += -DgcdBANK_CHANNEL_BIT=$(BANK_CHANNEL_BIT)
    endif
endif

ifeq ($(FPGA_BUILD),1)
EXTRA_CFLAGS += -DgcdFPGA_BUILD=1
else
EXTRA_CFLAGS += -DgcdFPGA_BUILD=0
endif

ifeq ($(SECURITY),1)
EXTRA_CFLAGS += -DgcdSECURITY=1
endif

ifneq ($(CONFIG_DRM),)
EXTRA_CFLAGS += -DgcdENABLE_DRM=$(VIVANTE_ENABLE_DRM)
else
EXTRA_CFLAGS += -DgcdENABLE_DRM=0
endif

ccflags-y += -I$(HAE_DIR)/$(HAL_KERNEL_DIR)/inc
ccflags-y += -I$(HAE_DIR)/$(HAL_KERNEL_DIR)
ccflags-y += -I$(HAE_DIR)/$(ARCH_KERNEL_DIR)
ccflags-y += -I$(HAE_DIR)/$(OS_KERNEL_DIR)
ccflags-y += -I$(HAE_DIR)/$(ALLOCATOR_ARRAY_H_LOCATION)
ccflags-y += -I$(HAE_DIR)/$(TA_DIR)
ccflags-y += -I$(HAE_DIR)/$(SOC_PLATFORM_LOCATION)

HOST := $(shell hostname)
EXTRA_CFLAGS += -DHOST=\"$(HOST)\"

ifneq ($(CONFIG_AR),)
EXTRA_CFLAGS += -Iarch/arm/mm
endif

EXTRA_CFLAGS += -DgcdENABLE_TRUST_APPLICATION=1

ifeq ($(ENABLE_VIDEO_MEMORY_MIRROR),1)
EXTRA_CFLAGS += -DgcdENABLE_VIDEO_MEMORY_MIRROR=1
endif

ifeq ($(ESW_DEBUG_PERF),1)
EXTRA_CFLAGS += -DESW_DEBUG_PERF=1
else
EXTRA_CFLAGS += -DESW_DEBUG_PERF=0
endif

ifeq ($(ESW_DEBUG_PERF_SHAKE),1)
EXTRA_CFLAGS += -DESW_DEBUG_PERF_SHAKE=1
else
EXTRA_CFLAGS += -DESW_DEBUG_PERF_SHAKE=0
endif

$(MODULE_NAME)-objs := $(OBJS)

obj-$(CONFIG_VIDEO_ESWIN_HAE) += $(MODULE_NAME).o
