.PHONY: clean all

obj-m := kweb.o

KDIR  := /lib/modules/$(shell uname -r)/build
KMOD  := /lib/modules/$(shell uname -r)/kernel
PWD   := $(shell pwd)
EXTRA_CFLAGS = -Wall

DEBUG = y

ifeq ($(DEBUG),y)
	EXTRA_CFLAGS += -O -g -DKWEB_DEBUG
endif

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.ko *.o *.mod.* .H* .tm* .*cmd Module.symvers modules.order

