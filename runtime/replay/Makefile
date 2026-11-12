# M5 PSP drive replay: the PSP arm of the 0.5 m host-vs-PSP gate. Built as its
# own EBOOT (like the M2 bench) so it cannot overwrite the game's EBOOT.PBP and
# the shipping M3/M4 artifacts stay byte-identical while the physics spine is
# validated. It links world.c (the same region streamer M4 shipped) plus the
# same 9 physics files runtime/bench/ links, plus the shared drive/ground spine
# the host harness links - so PSP and host run one implementation.
#
#   make -C runtime/replay
TARGET = replay
OBJS = replay.o platform.o pool.o world.o \
       u2pk_read.o ground.o drive.o swizzle.o dxt.o \
       drive_stage1.o lateral.o contact.o body_response.o drive_contact.o \
       drivetrain.o braking.o resistance.o physics.o
VPATH = .. ../../third_party/OpenUG2/src ../../shared
INCDIR = ../../shared ../../third_party/OpenUG2/src
CFLAGS = -Os -G0 -Wall -std=gnu99 -ffunction-sections -fdata-sections -DPSP \
         -Wno-unused-function
LDFLAGS = -Wl,--gc-sections
LIBS = -lpspgum -lpspgu -lpspctrl -lpsprtc -lpsppower -lpspdisplay -lpspge -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = UG2 Drive Replay
# Distinct SFO name: build.mak's SFO rule has no prerequisites and VPATH points
# at .., so a PARAM.SFO under a shared name would be found (and skipped) rather
# than generated. See runtime/bench/Makefile for the full why.
PSP_EBOOT_SFO = replay.sfo
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION = 500
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
