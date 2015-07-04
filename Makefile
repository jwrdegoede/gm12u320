# Makefile for GM12U320-based devices driver

ccflags-y := -Iinclude/drm

gm12u320-y :=	gm12u320_drv.o gm12u320_modeset.o gm12u320_connector.o \
		gm12u320_encoder.o gm12u320_main.o gm12u320_fb.o \
		gm12u320_gem.o gm12u320_dmabuf.o

obj-m += gm12u320.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules

modules_install:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules_install

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
