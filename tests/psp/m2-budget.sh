#!/bin/sh
# M2 budget measurement: how much PSP time does 4 cars x 8 substeps cost?
#
# The guest cannot answer this. sceRtcGetCurrentTick, the only clock user code
# can read, does not track wall time under PPSSPP headless: identical work
# reported ~7800 us per substep in a JIT run that took 11 s of real time and in
# an interpreter run that took 261 s (see out/m2/rtc-*.txt and PROGRESS.md M2).
# Reading the R4000 Count register instead was tried and is worse - a user-mode
# CP0 read takes a reserved-instruction exception, so the EBOOT printed nothing
# at all (out/m2/bench-cp0.EBOOT.PBP).
#
# So the timing is taken from outside the emulator, by differencing two runs of
# the same EBOOT that differ only in rounds= (data/bench.cfg): the subtraction
# cancels process start-up, module load, the 500 ms log flush and the frame
# count, leaving the guest's physics, timed on real hardware.
#
#   ROUNDS=300 tests/psp/m2-budget.sh
#
# Output: out/m2/budget.txt plus the arithmetic in the last lines.
set -eu

TEST=bench
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
MS0="$REPO/out/ms0"
GAME="$MS0/PSP/GAME/OPENUG2"
OUT="$REPO/out/m2"
ROUNDS=${ROUNDS:-3000}
FRAMES=${FRAMES:-30}
rc_file=0
[ -x "$PPSSPP" ] || { echo "m2-budget.sh: no emulator at $PPSSPP" >&2; exit 1; }

now_ns() { date +%s%N; }

echo "== building bench EBOOT"
make -C "$REPO/runtime/bench" -B EBOOT.PBP >"$OUT/bench-build.log" 2>&1 || {
	echo "build failed" >&2; exit 1; }

cd "$REPO"
[ -x "$OUT/bench_host" ] || {
	echo "run tests/psp/m2.sh first (it builds $OUT/bench_host)" >&2; exit 1; }

echo "== host: same long mode, real clock"
mkdir -p "$OUT/hostrun/data"
printf 'mode=long rounds=%s\n' "$ROUNDS" >"$OUT/hostrun/data/bench.cfg"
(cd "$OUT/hostrun" && ../bench_host) | grep -E '^PHYS (long|us_per)' |
	tee "$OUT/bench.host-long.txt"

run_long()
{
	rounds=$1 tag=$2
	rm -rf "$GAME"
	mkdir -p "$GAME/data" || exit 1
	cp "$REPO/runtime/bench/EBOOT.PBP" "$GAME/" || exit 1
	printf 'mode=long rounds=%s\n' "$rounds" >"$GAME/data/bench.cfg"
	rm -f "$GAME/log.txt"
	t0=$(now_ns)
	"$PPSSPP" --raise-signals -p "$MS0" --max-frames "$FRAMES" -j \
		"$GAME/EBOOT.PBP" >"$OUT/budget-$tag.emu.log" 2>&1 </dev/null || {
		echo "emulator failed, see $OUT/budget-$tag.emu.log" >&2; return 1; }
	t1=$(now_ns)
	rm -f "$GAME/data/bench.cfg"
	# The bench is a standalone EBOOT, so plat_log() writes log.txt next to the
	# game folder; stdout.txt is the test harness's file and does not appear.
	grep -E "^PHYS long rounds=$rounds " "$GAME/log.txt" >/dev/null || {
		echo "no 'PHYS long rounds=$rounds' in $GAME/log.txt:" >&2
		sed 's/^/    /' "$GAME/log.txt" >&2; return 1; }
	echo $(( (t1 - t0) / 1000000 )) >"$OUT/budget-$tag.wall_ms"
	grep -E '^PHYS long' "$GAME/log.txt" | tr -d '\r' | tail -1 \
		>"$OUT/budget-$tag.guest.txt"
	return 0
}

echo "== PSP arm: rounds=0 and rounds=$ROUNDS, host wall time each"
run_long 0 r0
run_long "$ROUNDS" rN
w0=$(cat "$OUT/budget-r0.wall_ms")
wN=$(cat "$OUT/budget-rN.wall_ms")
g0=$(cat "$OUT/budget-r0.guest.txt")
gN=$(cat "$OUT/budget-rN.guest.txt")
delta=$(( wN - w0 ))
echo "wall rounds=0     = $w0 ms"
echo "wall rounds=$ROUNDS = $wN ms"
echo "guest: $gN"
per_frame=$(( delta * 1000 / ROUNDS ))
echo "PHYS psp_us_per_frame_upper_bound=$per_frame"
{
	echo "host:      $(cat "$OUT/bench.host-long.txt")"
	echo "rounds=0:  $w0 ms   $g0"
	echo "rounds=$ROUNDS: $wN ms   $gN"
	echo "PHYS psp_us_per_frame_upper_bound=$per_frame (delta $delta ms / $ROUNDS frames)"
} >"$OUT/budget.txt"
cat "$OUT/budget.txt"
