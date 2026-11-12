TARGET = openug2
OBJS = main.o platform.o gfx.o pool.o shot.o car.o world.o \
       ../shared/u2pk_read.o ../shared/swizzle.o ../shared/dxt.o
INCDIR = ../shared
CFLAGS = -Os -G0 -Wall -std=gnu99 -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections
LIBS = -lpspgum -lpspgu -lpspctrl -lpsprtc -lpsppower -lpspdisplay -lpspge -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = OpenUG2
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION = 500
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
