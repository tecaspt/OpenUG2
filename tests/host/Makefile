# Host unit tests for the shared/converter primitives M3 depends on. Each test
# is a standalone program built from synthetic data only (no retail bytes), run
# by `make test` and again under ASan+UBSan by `make asan`.
CC     ?= gcc
ROOT    = ../..
CFLAGS  = -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter \
          -I$(ROOT)/shared -I$(ROOT)/converter -I$(ROOT)/third_party/OpenUG2/src
LDLIBS  = -lm
SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer

TESTS   = t_swizzle t_dxt t_quant t_vertex t_reader
OBJS_   = $(addprefix build/,$(TESTS))

# Which sources each test links: the code under test, nothing more.
SRC_t_swizzle = t_swizzle.c $(ROOT)/shared/swizzle.c
SRC_t_dxt     = t_dxt.c     $(ROOT)/shared/dxt.c
SRC_t_quant   = t_quant.c   $(ROOT)/converter/quant.c
SRC_t_vertex  = t_vertex.c
SRC_t_reader  = t_reader.c  $(ROOT)/shared/u2pk_read.c

.PHONY: all test asan clean
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
