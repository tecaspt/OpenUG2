# M2 physics-cost benchmark, built as its own EBOOT so it cannot overwrite the
# game's EBOOT.PBP (both targets write that name into their build directory):
#
#   make -C runtime/bench
#
# bench_phys.c lives one directory up, where the milestone puts it, and
# platform.c is the runtime's real platform layer, so this measures the code as
# the shipped game will compile it - the flags below are runtime/Makefile's, -Os
# included. An -O2 benchmark would understate what the runtime pays.
TARGET = bench
OBJS = bench_phys.o platform.o \
       drive_stage1.o lateral.o contact.o body_response.o drive_contact.o \
       drivetrain.o braking.o resistance.o physics.o
VPATH = .. ../../third_party/OpenUG2/src
INCDIR = ../../shared ../../third_party/OpenUG2/src
CFLAGS = -Os -G0 -Wall -std=gnu99 -ffunction-sections -fdata-sections -DPSP
LDFLAGS = -Wl,--gc-sections
# make DFCOUNT=1 counts the software double-precision helper calls the physics
# executes (see dfcount.c). The wrappers change the code, so this build answers
# "how many double operations", never "how long"; the budget run uses the plain
# build above.
ifeq ($(DFCOUNT),1)
OBJS += dfcount.o
CFLAGS += -DBENCH_DFCOUNT
LDFLAGS += -Wl,--wrap=__muldf3 -Wl,--wrap=__adddf3 -Wl,--wrap=__subdf3 \
	-Wl,--wrap=__divdf3 -Wl,--wrap=__ltdf2 -Wl,--wrap=__ledf2 \
	-Wl,--wrap=__gtdf2 -Wl,--wrap=__gedf2 -Wl,--wrap=__eqdf2 \
	-Wl,--wrap=__unorddf2 -Wl,--wrap=__truncdfsf2 -Wl,--wrap=__extendsfdf2 \
	-Wl,--wrap=__floatsidf -Wl,--wrap=__fixdfsi -Wl,--wrap=cosf
endif
LIBS = -lpsprtc -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = UG2 Physics Bench
# build.mak builds $(PSP_EBOOT_SFO) with a rule that has no prerequisites, and
# VPATH below points at .. - so with the default name, make finds the runtime's
# PARAM.SFO through VPATH, calls the target satisfied, and pack-pbp then fails to
# open a local PARAM.SFO that was never generated. A distinct name cannot be
# shadowed.
PSP_EBOOT_SFO = bench.sfo
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION = 500
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
