# Host unit tests for the shared/converter primitives M3 depends on. Each test
# is a standalone program built from synthetic data only (no retail bytes), run
# by `make test` and again under ASan+UBSan by `make asan`.
CC     ?= gcc
ROOT    = ../..
CFLAGS  = -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter \
          -I$(ROOT)/shared -I$(ROOT)/converter -I$(ROOT)/third_party/OpenUG2/src
LDLIBS  = -lm
SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer

TESTS   = t_swizzle t_dxt t_quant t_vertex t_reader t_ground
OBJS_   = $(addprefix build/,$(TESTS))

# Which sources each test links: the code under test, nothing more.
SRC_t_swizzle = t_swizzle.c $(ROOT)/shared/swizzle.c
SRC_t_dxt     = t_dxt.c     $(ROOT)/shared/dxt.c
SRC_t_quant   = t_quant.c   $(ROOT)/converter/quant.c
SRC_t_vertex  = t_vertex.c
SRC_t_reader  = t_reader.c  $(ROOT)/shared/u2pk_read.c
SRC_t_ground  = t_ground.c  $(ROOT)/shared/ground.c

.PHONY: all test asan replay_host clean
all: $(OBJS_)

build:
	@mkdir -p build

define RULE
build/$(1): $(SRC_$(1)) | build
	$(CC) $(CFLAGS) -o $$@ $$(SRC_$(1)) $$(LDLIBS)
endef
$(foreach t,$(TESTS),$(eval $(call RULE,$(t))))

define ARULE
build/$(1).asan: $(SRC_$(1)) | build
	$(CC) $(CFLAGS) $(SAN) -o $$@ $$(SRC_$(1)) $$(LDLIBS)
endef
$(foreach t,$(TESTS),$(eval $(call ARULE,$(t))))

ASANB = $(addprefix build/,$(addsuffix .asan,$(TESTS)))

test: $(OBJS_)
	@fail=0; for t in $(TESTS); do \
	  printf '== %s ==\n' $$t; ./build/$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo 'HOST TESTS FAILED'; exit 1; fi; \
	echo 'ALL HOST TESTS PASS'

asan: $(ASANB)
	@fail=0; for t in $(TESTS); do \
	  printf '== %s (asan) ==\n' $$t; \
	  ASAN_OPTIONS=detect_leaks=1 ./build/$$t.asan || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo 'ASAN HOST TESTS FAILED'; exit 1; fi; \
	echo 'ALL HOST TESTS PASS (asan)'

clean:
	rm -rf build

# M5 host replay harnesses. Built separately from the synthetic `test` set
# because running them reads generated (retail-derived, gitignored) L4RH data.
# Both link the SAME shared/ground.c and load the region through host_world.c,
# the same walk the runtime uses, so host and PSP ground cannot drift.
#
#   make replay_host   REG=out/m4/L4RH   - ground primitive on real COLLIDE
#   make drive_replay  REG=out/m4/L4RH   - step the physics 600 frames, determinism
PHYS_SRCS = $(addprefix $(ROOT)/third_party/OpenUG2/src/, \
            drive_stage1.c lateral.c contact.c body_response.c drive_contact.c \
            drivetrain.c braking.c resistance.c physics.c)
# -Wno-unused-function: upstream nfsu2.h carries static helpers unused here.
HOSTWARN  = -Wno-misleading-indentation -Wno-unused-parameter -Wno-unused-function
CFLAGS   += $(HOSTWARN) -I$(ROOT)/tests/host

build/replay_host: replay_host.c host_world.c $(ROOT)/shared/ground.c $(ROOT)/shared/u2pk_read.c | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

replay_host: build/replay_host
	@REG=$${REG:-$(ROOT)/out/m4/L4RH}; CFG=$${CFG:-$(ROOT)/dist/M4/PSP/GAME/OPENUG2/data/world.cfg}; \
	 if [ -f "$$REG/index.u2pk" ]; then ./build/replay_host "$$REG" "$$CFG"; \
	 else echo "no converted data at $$REG (run psp_convert region first)"; fi

# The drive spine is IO-free; the host arm links the 9 physics files natively
# (the same set runtime/bench/Makefile links for the PSP bench).
build/drive_replay: drive_replay.c host_world.c $(ROOT)/shared/drive.c \
                    $(ROOT)/shared/ground.c $(ROOT)/shared/u2pk_read.c $(PHYS_SRCS) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Frames/throttle/brake come from tools/gen_replay.py so both arms get one file.
drive_replay: build/drive_replay
	@REG=$${REG:-$(ROOT)/out/m4/L4RH}; CFG=$${CFG:-$(ROOT)/dist/M4/PSP/GAME/OPENUG2/data/world.cfg}; \
	 if [ ! -f "$$REG/index.u2pk" ]; then echo "no converted data at $$REG (run psp_convert region first)"; exit 1; fi; \
	 python3 $(ROOT)/tools/gen_replay.py 600 > build/replay.txt; \
	 ./build/drive_replay "$$REG" $${POSE:-1} build/replay.txt "$$CFG"
