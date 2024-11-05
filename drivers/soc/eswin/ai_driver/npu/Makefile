NPU_PERF_STATS ?= 0
LOG_LEVEL ?= 0
NPU_DEV_SIM ?= 0
SMALL_PEC_MAT ?= 0

ccflags-y += -I$(src)
ccflags-y += -I$(srctree)/drivers/soc/eswin
ccflags-y += -I$(srctree)/drivers/memory/eswin/codacache/
ccflags-y += -I$(srctree)/$(src)/include
ccflags-y += -I$(srctree)/$(src)/../dsp
ccflags-y += -I$(srctree)/$(src)/../common
ccflags-y += -I$(srctree)/$(src)/../common/devices
ccflags-y += -I$(srctree)/drivers/memory/eswin/es_dev_buf/include/linux

ccflags-y += -DNPU_DEV_SIM=$(NPU_DEV_SIM)
ccflags-y += -DENABLE_PROGRAM_CORE=$(ENABLE_PROGRAM_CORE)
ccflags-y += -DDUAL_DIE=$(DUAL_DIE)
ccflags-y += -DSMALL_PEC_MAT=$(SMALL_PEC_MAT)
ccflags-y += -DNPU_DRV_SUPPORT_DSP=$(NPU_DSP_MODULE)
ccflags-y += -DNPU_PERF_STATS=$(NPU_PERF_STATS)
ccflags-y += -DNPU_DBG
#ccflags-y += -DCONV_DUMP
ccflags-y += -DLOG_LEVEL=$(LOG_LEVEL)
ccflags-y += -Wno-unused-function
ccflags-y += -Werror
ccflags-y += -Wno-int-to-pointer-cast

obj-$(CONFIG_ESWIN_NPU) += eic7700_npu.o
eic7700_npu-y := edma.o \
		engine.o \
		npu_main.o \
		pdp.o \
		common.o \
		rubik.o \
		sdp.o \
		conv.o \
		debug.o \
		dla_log.o \
		npu_spram.o \
		dla_driver.o \
		user_context.o   \
		parse_dep.o   \
		scheduler.o   \
		npu_frame.o \
		dla_buffer.o \
		event.o	\
		nvdla_proc.o \
		dsp.o

ifeq ($(NPU_DEV_SIM), 0)
eic7700_npu-y += npu_e31.o
eic7700_npu-y += nvdla_hw.o
else
eic7700_npu-y += nvdla_sim.o
endif

