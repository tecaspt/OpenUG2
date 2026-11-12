# Host build of psp_convert. This runs on the Linux dev machine and turns retail
# NFSU2 data into U2PK v1 files; the PSP runtime itself is built by
# ../runtime/Makefile with psp-gcc and is a separate target.
#
#   make            -> ./psp_convert
#   make clean
#
# The shared/ sources are compiled into converter/build/, never into shared/,
# because the PSP runtime build owns the shared/*.o names.
CC    ?= gcc
CFLAGS = -std=c99 -O2 -g -D_DEFAULT_SOURCE \
         -Ishim -I../third_party/OpenUG2/src -I../shared \
         -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
         -Wno-misleading-indentation
# -MMD only on compiles: on the link line it emits a stray <prog>.d beside the
# source. The per-object .d files land in build/, which is gitignored.
DEPFLAGS = -MMD -MP
LDLIBS = -lm

# OpenUG2 world loader (MIT, vendored submodule) compiled GL/SDL-free for the
# host converter: shim/SDL.h stands in for the SDL types render.h names, and
# region_load.c stubs the two GL entry points the never-called texture-upload
# path references. See region_load.c/h.
UG2 = ../third_party/OpenUG2/src
SRCS = psp_convert.c car_load.c car_conv.c geom.c quant.c \
       region_load.c region_conv.c region_convert.c region_tex.c \
       $(UG2)/world.c $(UG2)/physics.c $(UG2)/resource.c $(UG2)/world_instance.c \
       ../shared/swizzle.c ../shared/dxt.c ../shared/u2pk_read.c
OBJS = $(addprefix build/,$(notdir $(SRCS:.c=.o)))

BIN = psp_convert
ASAN = psp_convert.asan

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The milestone requires an ASan+UBSan build of the converter. Build it under
# `asan` and run it over the acceptance cars to prove there are no memory or
# undefined-behaviour reports.
asan:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	      -o $(ASAN) $(SRCS) $(LDLIBS)

build/%.o: %.c | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/%.o: $(UG2)/%.c | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/%.o: ../shared/%.c | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build:
	@mkdir -p build

-include $(OBJS:.o=.d)

clean:
	rm -rf build $(BIN) $(ASAN)

.PHONY: all clean asan
