ifeq ($(VVCAM_PATH),)
	VVCAM_PATH:=$(shell cd $(shell pwd)/../; pwd)
	export VVCAM_PATH
endif


include $(PWD)/../Param.mk

EXTRA_CFLAGS += -I$(PWD)/include
# Module names
obj-m := dw-csi.o dw-dphy.o es-csi-mipi.o

# Source files for each module
dw-csi-objs := dw-csi-plat.o dw-mipi-csi.o
dw-dphy-objs := dw-dphy-rx.o dw-dphy-plat.o
es-csi-mipi-objs := eswin-mipi-dma.o

PWD :=$(shell pwd)

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules
	@[ -e $(INSTALL_DIR)/lib ] || mkdir $(INSTALL_DIR)/lib -p
	@cp dw-csi.ko $(INSTALL_DIR)/lib
	@cp dw-dphy.ko $(INSTALL_DIR)/lib
	@cp es-csi-mipi.ko $(INSTALL_DIR)/lib
modules_install:
	make -C $(KERNEL_DIR) M=$(PWD) modules_install
clean:
	rm -rf $(dw-csi-objs)
	rm -rf $(dw-dphy-objs)
	rm -rf $(es-csi-mipi-objs)
	make -C $(KERNEL) M=`pwd` clean



