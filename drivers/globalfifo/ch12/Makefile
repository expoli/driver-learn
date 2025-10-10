# Makefile for globalfifo device and driver modules

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# 目标模块对象，使用模块的基本名称
obj-m += globalfifo_dev.o
obj-m += globalfifo_drv.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
