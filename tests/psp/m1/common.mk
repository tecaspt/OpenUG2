# Shared settings for the M1 upstream tests cross-compiled for the PSP.
# A test's Makefile sets TARGET, OBJS and then includes this file.
INCDIR = ../../../../third_party/OpenUG2/src ../../../../third_party/OpenUG2/tools
VPATH = ../../../../third_party/OpenUG2/src ../../../../third_party/OpenUG2/tools ../..
CFLAGS = -O2 -G0 -Wall -std=gnu99 -DPSP -Wno-unused-function -include ../../psp_compat.h
LIBS = -lm
# psp_module.c carries PSP_MODULE_INFO (PRX links require exactly one).
OBJS += psp_module.o
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = $(TARGET)
PSP_LARGE_MEMORY = 0
PSP_FW_VERSION = 500
BUILD_PRX = 1
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
