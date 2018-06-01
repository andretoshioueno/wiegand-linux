KERNEL_DIR ?= /home/toshio/Temp/wiegand_driver/kernel
COMPILER_PREFIX = arm-linux-gnueabihf-
ARCHITECTURE = arm
ROOTFS := /home/toshio/Temp/wiegand_driver/nfs_rootfs/

obj-m := wiegand-gpio.o
PWD := $(shell pwd)

all: wiegand-gpio.c
	$(MAKE) ARCH=$(ARCHITECTURE) CROSS_COMPILE=$(COMPILER_PREFIX) -C $(KERNEL_DIR) M=$(PWD) modules

install:
	@sudo $(MAKE) -C $(KERNEL_DIR) M=$(PWD) INSTALL_MOD_PATH=$(ROOTFS) modules_install

clean:
	make -C $(KERNEL_DIR) SUBDIRS=$(PWD) clean
