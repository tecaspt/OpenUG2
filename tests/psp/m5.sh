#!/usr/bin/env bash
# M5: drive in the slice - step the real physics on real COLLIDE, PSP vs host.
#
#   usage: tests/psp/m5.sh
#
# The shared drive spine (shared/drive.c: stage1_drive_step + the upstream
# sprung-ride integrator, ground through a tight per-wheel reach window) is run
# twice - once natively (tests/host/drive_replay.c over host_world.c) and once
# as the replay EBOOT (runtime/replay.c over the same region M4 streamed) - on
# the SAME replay.txt, and the final poses are compared. Gates graded here:
#   1  host-vs-PSP final pose within 0.5 m  (the milestone's numeric spine)
#   2  PSP run twice bit-identical          (the EBOOT self-checks, we read it)
#   3  pool high-water within the 17 MB budget
#   4  one drive tick (physics + ground, NO render) fits the frame budget, by
#      differenced host wall clock (headless PPSSPP's RTC is not a stopwatch)
#
# M5's VISUAL acceptance is NOT graded here: it is inherited-open from the M3
# render defect (distorted on real PSP-1000, correct in emulation) pending the
# one probe run. This script proves the numeric spine only.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/out/m5"
up="$repo/third_party/OpenUG2"
retail=${RETAIL:-/home/nike/nfsu2}
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
LOCK=/home/nike/openug2-dev/.heavy.lock
mkdir -p "$out"
. /home/nike/openug2-dev/psp/env-psp.sh 2>/dev/null || true
rc=0
note() { printf '%s\n' "$*"; }
fail() { note "M5 FAIL: $*"; rc=1; }

UNITS="drive_stage1 lateral contact body_response drive_contact drivetrain braking resistance physics"

# ---------------------------------------------------------------- data -------
# Reuse M4's converted L4RH if present, else convert it fresh.
reg="$repo/out/m4/L4RH"
if [ ! -f "$reg/index.u2pk" ]; then
	note "== convert STREAML4RH (no cached M4 data) =="
	make -C "$repo/converter" >/dev/null 2>&1 || { fail "converter build"; exit 1; }
	mkdir -p "$reg"
	"$repo/converter/psp_convert" region convert "$retail" STREAML4RH "$reg" \
		>"$out/conv.txt" 2>&1 || { fail "region convert (see $out/conv.txt)"; exit 1; }
fi
cfg="$repo/dist/M4/PSP/GAME/OPENUG2/data/world.cfg"
[ -f "$cfg" ] || { fail "no world.cfg at $cfg"; exit 1; }

# One deterministic script feeds both arms.
python3 "$repo/tools/gen_replay.py" 600 > "$out/replay.txt"

# ------------------------------------------------------------- host arm ------
note "== host arm: build + run drive_replay =="
srcs=""; for u in $UNITS; do srcs="$srcs $up/src/$u.c"; done
( cd "$repo" && gcc -O2 -std=c99 -Wno-misleading-indentation -Wno-unused-parameter \
		-Wno-unused-function -I "$up/src" -I "$repo/shared" -I "$repo/tests/host" \
		tests/host/drive_replay.c shared/drive.c shared/ground.c shared/u2pk_read.c \
		tests/host/host_world.c $srcs -lm -o "$out/drive_replay" ) >"$out/host-build.log" 2>&1
[ $? -eq 0 ] || { fail "host build (see $out/host-build.log)"; tail -20 "$out/host-build.log"; exit 1; }
"$out/drive_replay" "$reg" 1 "$out/replay.txt" "$cfg" >"$out/host.txt" 2>&1
hline=$(grep -m1 '^run A' "$out/host.txt")
note "  $hline"
grep -q 'determinism(A==B): PASS' "$out/host.txt" || fail "host arm not deterministic"
grep -q 'crossed a cell boundary: yes' "$out/host.txt" || note "  note: host drive did not cross a cell boundary"

# -------------------------------------------------------------- PSP arm ------
note "== PSP arm: build + run the replay EBOOT =="
make -C "$repo/runtime/replay" EBOOT.PBP >"$out/psp-build.log" 2>&1
[ $? -eq 0 ] || { fail "replay EBOOT build (see $out/psp-build.log)"; tail -20 "$out/psp-build.log"; exit 1; }
stage="$repo/out/m5stage"
rm -rf "$stage"; mkdir -p "$stage/data/L4RH"
cp "$repo/runtime/replay/EBOOT.PBP" "$stage/"
cp "$reg"/*.u2pk "$stage/data/L4RH/"
cp "$cfg" "$stage/data/world.cfg"
cp "$out/replay.txt" "$stage/data/replay.txt"
printf 'region=L4RH\npose=1\n' > "$stage/data/replay.cfg"
TEST=m5replay TIMEOUT=${TIMEOUT:-200} sh "$here/run.sh" "$stage" 8 0 >/dev/null 2>&1
logtxt="$repo/out/m5replay.log.txt"
[ -f "$logtxt" ] || { fail "no $logtxt (EBOOT never logged)"; exit 1; }
pline=$(grep -m1 '^REPLAY final' "$logtxt")
[ -n "$pline" ] || { fail "no 'REPLAY final' line"; cat "$logtxt" | sed 's/^/  /'; exit 1; }
note "  $pline"

# item 2: PSP-run-twice bit-identical (the EBOOT runs the script twice itself).
grep -q 'determinism(A==B)=PASS' "$logtxt" || fail "item 2: PSP replay not self-deterministic"

# --------------------------------------------- item 1: host vs PSP 0.5 m -----
note "\n== item 1: host-vs-PSP final pose =="
dist=$(python3 - "$hline" "$pline" <<'PY'
import re,sys,math
def pose(s):
    return {k:float(v) for k,v in re.findall(r'(x|y|z|yaw)=(-?[0-9.eE+-]+)', s)}
a,b=pose(sys.argv[1]),pose(sys.argv[2])
print("%.6f %.6f %.6f" % (math.hypot(a['x']-b['x'],a['y']-b['y']), a['yaw']-b['yaw'], a['z']-b['z']))
PY
)
xy=$(echo "$dist" | awk '{print $1}')
dyaw=$(echo "$dist" | awk '{print $2}')
dz=$(echo "$dist" | awk '{print $3}')
note "  |dXY|=$xy m   |dyaw|=$dyaw rad   |dz|=$dz m"
awk -v d="$xy" 'BEGIN{exit !(d<=0.5)}' && note "item 1: |dXY|=$xy <= 0.5 m -- PASS" \
	|| fail "item 1: |dXY|=$xy > 0.5 m"

# --------------------------------------------- item 3: pools -----------------
hw=$(sed -n 's/.*pool high-water total=\([0-9]*\) .*/\1/p' "$logtxt" | head -1)
[ -n "${hw:-}" ] && [ "$hw" -le 17825792 ] \
	&& note "item 3: pool high-water total=$hw ($((hw/1024)) KB) <= 17 MB -- PASS" \
	|| fail "item 3: pool high-water total='${hw:-none}' > 17825792"

# --------------------------------- item 4: drive-tick cost, differenced ------
# replay.cfg frames=N caps the script; the EBOOT runs N ticks twice. Difference
# wall time between two caps to cancel startup/module load; per drive tick =
# (T600-T60) / (2*(600-60)). Times PPSSPP's translation on a fast ARM, so it is
# an upper bound on a 333 MHz PSP-1000, not a measurement of one (see M2).
note "\n== item 4: drive-tick cost (physics+ground, no render), differenced =="
GAME="${MS0:-$repo/out/ms0}/PSP/GAME/OPENUG2"
timed(){ local f=$1; rm -rf "$GAME"; mkdir -p "$GAME/data/L4RH"
	cp "$repo/runtime/replay/EBOOT.PBP" "$GAME/"
	cp "$reg"/*.u2pk "$GAME/data/L4RH/"; cp "$cfg" "$GAME/data/world.cfg"
	cp "$out/replay.txt" "$GAME/data/replay.txt"
	printf 'region=L4RH\npose=1\nframes=%s\n' "$f" > "$GAME/data/replay.cfg"
	printf 'frames=8\nshot=999999\n' > "$GAME/data/autotest.cfg"
	local t0 t1; t0=$(date +%s%N)
	flock "$LOCK" nice -n 10 "$PPSSPP" "$GAME/EBOOT.PBP" --graphics=software \
		--timeout=180 >/dev/null 2>&1 </dev/null
	t1=$(date +%s%N); echo $(( (t1 - t0) / 1000000 )); }
w1=$(timed 60); w2=$(timed 600); dt=$(( 2*(600-60) ))
per=$(( (w2 - w1) * 1000 / dt ))
if [ "$per" -ge 0 ] && [ "$per" -le 33333 ]; then
	note "item 4: $per us/drive-tick (60f=${w1}ms 600f=${w2}ms over ${dt} ticks) <= 33333 -- PASS"
else
	fail "item 4: $per us/drive-tick out of range"
fi
note "  guest_us_2runs line is NOT a clock under headless: $(grep -o 'guest_us_2runs=[0-9]*' "$logtxt" | head -1)"

note ""
[ "$rc" -eq 0 ] && note "M5: PASS (numeric spine host==PSP; visual inherited-open from M3 pending the probe)" \
				|| note "M5: FAIL (see above)"
exit $rc
