# Vertex-format probe (docs/U2PK.md evidence), its own EBOOT so it can never
# overwrite the game build. See vfmt.c for what it measures.
#
#   make -C runtime/vfmt
TARGET = vfmt
OBJS = vfmt.o platform.o gfx.o shot.o ../shared/swizzle.o
VPATH = ..
INCDIR = ../../shared ..
CFLAGS = -Os -G0 -Wall -std=gnu99 -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections
LIBS = -lpspgum -lpspgu -lpspctrl -lpsprtc -lpspdisplay -lpspge -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = UG2 Vertex Probe
# Distinct SFO name: build.mak's SFO rule has no prerequisites, so with the
# default PARAM.SFO and VPATH=.. make finds the runtime's file, thinks the
# target is satisfied, and pack-pbp then fails on a file that was never built.
PSP_EBOOT_SFO = vfmt.sfo
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION = 500
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
