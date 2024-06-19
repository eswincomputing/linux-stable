USE_CMODEL ?= y

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
CCFLAGS = -fpic -Wall -g -O0
LIBS = -pthread
INCLUDES =
LIBS_PATH =
BUILD_TARGET = test_module

ifeq ($(strip $(USE_CMODEL)),y)
	RUN_PLATFORM = x86
	CC = gcc
	AR =
else
	RUN_PLATFORM = riscv
	DRIVER_PATH = middleware/ESSDK/driver/esw_dec_driver
	PWD = $(shell pwd)
	SDK_ROOT = $(shell echo $(PWD) | awk -F '$(DRIVER_PATH)' '{print $$1}')
	CC = $(SDK_ROOT)buildroot/output/host/bin/riscv64-unknown-linux-gnu-gcc
	AR = $(SDK_ROOT)buildroot/output/host/bin/riscv64-unknown-linux-gnu-gcc-ar
endif

#CCFLAGS= -g -no-pie

.PHONY: clean

$(BUILD_TARGET) : $(OBJS)
	@echo -------------------------------------------
	@echo - build for $(RUN_PLATFORM)
	@echo - usage: make USE_CMODEL=y/n
	@echo -------------------------------------------
	$(CC) $^ -o $@ $(CCFLAGS) $(INCLUDES) $(LIBS_PATH) $(LIBS)

%.o : %.c
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(BUILD_TARGET)
