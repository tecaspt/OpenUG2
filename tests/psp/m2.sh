#!/usr/bin/env bash
# M2: physics cost per car per substep, measured twice from one source file.
#
#   usage: tests/psp/m2.sh
#
# Arm 1, host: build runtime/bench_phys.c with gcc and the same upstream
# sources, run it natively. Arm 2, PSP: build the bench EBOOT and run it in
# PPSSPPHeadless. The FINAL STATES must agree - that is the gate.
#
# The timing column needs reading carefully, and this script used to describe it
# wrongly ("PPSSPP's microseconds say nothing about a 333 MHz PSP"). Measured:
# the two microsecond figures differ by 1500x, and neither is the PSP's number.
# The host is fast because this machine's CPU does double-precision arithmetic
# in hardware; the PSP build does the same arithmetic through libgcc software
# helpers, because the PSP has no double-precision FPU. The emulator's own
# figure is unusable in the other direction: sceRtcGetCurrentTick under headless
# PPSSPP advances with emulated frames, not host time.
#
# So the two figures this script prints are only a comparison of two machines.
# The cost question is answered by two other measurements, which do not read any
# clock from inside the guest:
#
#   tests/psp/m2-budget.sh          wall time of the emulator, differenced
#                                   between rounds=0 and rounds=N
#   make -C runtime/bench DFCOUNT=1 counts the software double operations one
#                                   car-substep actually executes
#
# Both are recorded in PROGRESS.md under M2.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
up="$repo/third_party/OpenUG2"
out="$repo/out/m2"
mkdir -p "$out"
. /home/nike/openug2-dev/psp/env-psp.sh

UNITS="drive_stage1 lateral contact body_response drive_contact drivetrain braking resistance physics"
rc=0

# ------------------------------------------------------------- host arm ----
# -Wno-misleading-indentation: upstream nfsu2.h packs two statements onto one
# line, which -Wextra flags in every TU that includes it.
srcs=""
for u in $UNITS; do srcs="$srcs $up/src/$u.c"; done
( cd "$repo" && gcc -O2 -std=gnu99 -Wall -Wextra -Wno-misleading-indentation \
		-I "$up/src" -I "$repo/shared" \
		runtime/bench_phys.c $srcs -lm -o "$out/bench_host" ) > "$out/host-build.log" 2>&1
if [ $? -ne 0 ]; then
	echo "m2: host build FAILED, see $out/host-build.log"
	tail -20 "$out/host-build.log"
	exit 1
fi
"$out/bench_host" > "$out/bench.host.txt" 2>&1
echo "host: $(grep -m1 '^PHYS us_per_substep_per_car' "$out/bench.host.txt")"

# -------------------------------------------------------------- PSP arm ----
( cd "$repo/runtime/bench" && make -B EBOOT.PBP ) > "$out/psp-build.log" 2>&1
if [ $? -ne 0 ]; then
	echo "m2: PSP build FAILED, see $out/psp-build.log"
	tail -20 "$out/psp-build.log"
	exit 1
fi
TEST=bench TIMEOUT=${TIMEOUT:-120} sh "$repo/tests/psp/run.sh" "$repo/runtime/bench" 0 0
guest="$repo/out/bench.log.txt"
[ -f "$guest" ] || { echo "m2: no $guest - the bench did not log"; exit 1; }
cp "$guest" "$out/bench.psp.txt"
echo "psp:  $(grep -m1 '^PHYS us_per_substep_per_car' "$out/bench.psp.txt")"

# --------------------------------------------------------------- compare ----
a=$(grep '^PHYS car' "$out/bench.host.txt")
b=$(grep '^PHYS car' "$out/bench.psp.txt")
if [ -n "$a" ] && [ "$a" = "$b" ]; then
	states=identical
else
	states=DIFFERENT
	rc=1
fi
kb=$(psp-size "$repo/runtime/bench/bench.elf" 2>/dev/null | awk 'NR==2{printf "%.0f", ($1+$2+$3)/1024}')

printf '\n%-26s %-14s %-14s\n' "" HOST PSP
printf '%-26s %-14s %-14s\n' "us/substep/car" \
	"$(sed -n 's/^PHYS us_per_substep_per_car=//p' "$out/bench.host.txt")" \
	"$(sed -n 's/^PHYS us_per_substep_per_car=//p' "$out/bench.psp.txt")"
printf '%-26s %-14s %-14s\n' "frame us, 8 substeps" \
	"$(sed -n 's/^PHYS frame_us_worst8=\([0-9.]*\) .*/\1/p' "$out/bench.host.txt")" \
	"$(sed -n 's/^PHYS frame_us_worst8=\([0-9.]*\) .*/\1/p' "$out/bench.psp.txt")"
printf '%-26s %-14s %-14s\n' "final states" "$states" "$states"
printf '%-26s %-14s %-14s\n' "bench size KB" "-" "$kb"
echo
echo "budget: 4 cars x 8 substeps x us_per_substep must be <= 8000 us"
echo "Neither column above measures a PSP. The host is hardware-double; the"
echo "emulator's RTC is not a wall clock. See tests/psp/m2-budget.sh (wall time"
echo "differenced across the emulator) and DFCOUNT=1 (software double ops per"
echo "substep); PROGRESS.md M2 grades the budget from those. states: $states"
exit $rc
