gm12u320-y :=   gm12u320_drv.o gm12u320_modeset.o gm12u320_connector.o \
                gm12u320_encoder.o gm12u320_main.o gm12u320_fb.o \
                gm12u320_gem.o gm12u320_dmabuf.o
obj-m += gm12u320.o

SRC := $(shell pwd)
KVER=$(shell uname -r)
KDIR=/lib/modules/$(KVER)/build

all:
	make -C $(KDIR) M=$(SRC)

modules_install:
	make -C $(KDIR) M=$(SRC) modules_install
