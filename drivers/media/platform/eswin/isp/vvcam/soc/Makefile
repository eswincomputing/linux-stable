ifeq ($(VVCAM_PATH),)
	VVCAM_PATH:=$(shell cd $(shell pwd)/../; pwd)
	export VVCAM_PATH
endif


include $(PWD)/../Param.mk
TARGET = vvcam_soc

obj-m +=$(TARGET).o
$(TARGET)-objs += vvcam_soc_driver.o
$(TARGET)-objs += soc_ioctl.o
$(TARGET)-objs += vivsoc_hub.o
$(TARGET)-objs += vsi_core_gen6.o
$(TARGET)-objs += vivsoc_dvp2axi.o

PWD :=$(shell pwd)

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules
	@[ -e $(INSTALL_DIR)/lib ] || mkdir $(INSTALL_DIR)/lib -p
	@cp $(TARGET).ko $(INSTALL_DIR)/lib
modules_install:
	make -C $(KERNEL_DIR) M=$(PWD) modules_install
clean:
	rm -rf $($(TARGET)-objs)
	make -C $(KERNEL_DIR) M=$(PWD) clean
	@rm $(INSTALL_DIR)/lib/$(TARGET).ko

