#!/usr/bin/env bash
# M3: the texture pipeline and LODs. One real car, converted from retail NFSU2
# data, drawn by the runtime at both LODs and screenshotted.
#
#   usage: tests/psp/m3.sh
#
# Acceptance items, each graded below:
#   1  host tests pass, and pass under ASan            (make -C tests/host)
#   2  converter clean under ASan+UBSan on 5 cars
#   3  every car inside the LOD budgets (printed table)
#   4  screenshot shows a correct car and matches the golden
#   5  carpool_high <= 1.5 MB with the player and AI LOD both loaded
#   6  frame cost <= 33333 us (differenced host wall clock, see the note)
#   7  sfo_check and the heap/arena window check
#
# Timing note (item 6). The guest cannot time itself: sceRtcGetCurrentTick under
# headless PPSSPP advances with emulated frames, not wall time, so the EBOOT's
# own "frame_us avg=199" line is a fixed tick step, not a measurement. The gate
# is therefore the wall time of the emulator itself, differenced between two
# runs that differ only in frames= so the subtraction cancels startup, module
# load and exit. This times PPSSPP's software renderer on this Pi, not a real
# 333 MHz PSP-1000 with a hardware GE; it is an upper bound on whether the scene
# is cheap enough, not an emulation of PSP frame time.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/out/m3"
golden_dir="$repo/out/goldens"
golden="$golden_dir/m3_miata.ppm"
car=MIATA
retail=${RETAIL:-/home/nike/nfsu2}
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
LOCK=/home/nike/openug2-dev/.heavy.lock
mkdir -p "$out" "$golden_dir"
. /home/nike/openug2-dev/psp/env-psp.sh
rc=0
note() { printf '%s\n' "$*"; }
fail() { note "M3 FAIL: $*"; rc=1; }

# ---------------------------------------------------------------- build ------
note "== build converter (native) + EBOOT =="
make -C "$repo/converter" >/dev/null 2>&1 || { fail "converter build"; exit 1; }
make -C "$repo/runtime"    >/dev/null 2>&1 || { fail "runtime build"; exit 1; }

# ------------------------------------------------- items 1: host tests -------
note "\n== item 1: host tests (plain and ASan) =="
if make -C "$repo/tests/host" test >/dev/null 2>&1 &&
   make -C "$repo/tests/host" asan >/dev/null 2>&1; then
        note "host tests: PASS (plain + ASan)"
else
        fail "a host test failed (run make -C tests/host test / asan)"
fi

# --------------------------------------- item 2: ASan+UBSan on 5 cars --------
# Budget table for item 3 is collected from the same ASan run: it must pass both
# gates on the sanitizer build so a budget that only holds on -O2 is not accepted.
note "\n== item 2 + 3: ASan+UBSan converter on 5 cars, and the LOD budget table =="
make -C "$repo/converter" asan >/dev/null 2>&1 || fail "ASan converter build"
printf '%-9s %8s %8s %8s %9s  %s\n' \
       car LOD0 LOD1 wheel file status
for c in MIATA 350Z 240SX A3 3000GT; do
        log="$out/conv-$c.txt"
        ASAN_OPTIONS=detect_leaks=1 "$repo/converter/psp_convert.asan" \
                car "$retail" "$c" "$out/conv-$c.u2pk" >"$log" 2>&1
        s=$?
        grep -qiE 'ERROR: (Address|Leak)Sanitizer|runtime error|VALIDATED.*FAIL|REJECTED' "$log" && s=1
        grep -q VALIDATED "$log" || s=1
        l0=$(sed -n 's/.*LOD0 (player)  tris *[0-9]* -> *\([0-9]*\).*/\1/p' "$log")
        l1=$(sed -n 's/.*LOD1 (AI)      tris *[0-9]* -> *\([0-9]*\).*/\1/p' "$log")
        wh=$(sed -n 's/.*wheels *tris *[0-9]* -> *\([0-9]*\).*/\1/p' "$log")
        bytes=$(sed -n 's/.*file \([0-9]*\) KB.*/\1/p' "$log")
        ok=ok
        [ "$s" -eq 0 ] || ok=SANITIZER
        [ -n "$l0" ] && [ "$l0" -le 3000 ] || ok=LOD0_TRIS
        [ -n "$l1" ] && [ "$l1" -le 1000 ] || ok=LOD1_TRIS
        [ -n "$wh" ] && [ "$wh" -le 400 ]  || ok=WHEEL_TRIS
        [ -n "$bytes" ] && [ "$bytes" -le 600 ] || ok=FILE_BYTES
        [ "$ok" = ok ] || { fail "$c: $ok (LOD0=$l0 LOD1=$l1 wheel=$wh file=${bytes}KB)"; }
        printf '%-9s %8s %8s %8s %8sKB  %s\n' "$c" "$l0" "$l1" "$wh" "$bytes" "$ok"
done
note "(LOD0 player tris <=3000, LOD1 AI tris <=1000, shared wheels <=400, file <=600 KB)"

# --------------------------------------- item 7: sfo_check (early gate) ------
note "\n== item 7: PARAM.SFO =="
python3 "$repo/tools/sfo_check.py" "$repo/runtime/EBOOT.PBP" || fail "sfo_check"

# ------------------------------------ items 4-6: run in the emulator ---------
# The runtime loads the car twice from one file (player LOD0, AI LOD1) so the
# Cars pool holds both at once, which is what item 5 measures.
note "\n== items 4-6: run $car in PPSSPPHeadless =="
mkdir -p "$repo/runtime/data"
"$repo/converter/psp_convert" car "$retail" "$car" "$repo/runtime/data/car.u2pk" >/dev/null 2>&1 \
        || { fail "could not convert $car"; exit 1; }
TEST=m3 sh "$here/run.sh" "$repo/runtime" 300 120 || note "(emulator exit $?)"
logtxt="$repo/out/m3.log.txt"
[ -f "$logtxt" ] || { fail "no $logtxt (the EBOOT never logged)"; }

# item 5: both LODs resident inside the 1.5 MB Cars pool.
high=$(sed -n 's/.*carpool_high=\([0-9]*\).*/\1/p' "$logtxt" | head -1)
if [ -n "${high:-}" ] && [ "$high" -le 1572864 ]; then
        note "item 5: carpool_high=$high bytes ($(( high / 1024 )) KB, cap 1536 KB) with player+AI loaded -- PASS"
else
        fail "item 5: carpool_high='${high:-none}' (cap 1572864)"
fi
# item 7 (heap): the arena must live inside the PSP-1000 24 MB HOME window.
top=$(sed -n 's/.*arena=\(0x[0-9a-f]*\).*/\1/p' "$logtxt" | head -1)
if [ -n "${top:-}" ] && [ "$(( top ))" -gt "$((0x08800000))" ] && \
   [ "$(( top ))" -le "$((0x0A000000))" ]; then
        note "item 7: arena=$top inside the PSP-1000 HOME window, heap free=$(sed -n 's/.*heap=\([0-9]*\).*/\1/p' "$logtxt" | head -1) -- PASS"
else
        fail "item 7: arena='${top:-none}' outside 0x08800000..0x0A000000"
fi

# item 6: differenced host wall clock (see the note at the top of this file).
note "\n== item 6: frame cost by differenced host wall clock =="
GAME="${MS0:-$repo/out/ms0}/PSP/GAME/OPENUG2"
now_ns(){ date +%s%N; }
timed(){ # frames -> wall ms
        rm -rf "$GAME"; mkdir -p "$GAME/data"
        cp "$repo/runtime/EBOOT.PBP" "$GAME/"
        cp "$repo/runtime/data/car.u2pk" "$GAME/data/"
        printf 'frames=%s\nshot=999999\n' "$1" > "$GAME/data/autotest.cfg"
        local t0 t1
        t0=$(now_ns)
        flock "$LOCK" nice -n 10 "$PPSSPP" "$GAME/EBOOT.PBP" --graphics=software \
                --timeout=180 >/dev/null 2>&1 </dev/null
        t1=$(now_ns)
        echo $(( (t1 - t0) / 1000000 ))
}
w1=$(timed 50); w2=$(timed 350)
per=$(( (w2 - w1) * 1000 / 300 ))
if [ "$per" -le 33333 ]; then
        note "item 6: $per us/frame (50f=${w1}ms 350f=${w2}ms, 300-frame difference) <= 33333 -- PASS"
else
        fail "item 6: $per us/frame > 33333"
fi
note "  guest frame_us line (NOT a clock under headless): $(grep -o 'frame_us [^ ]* [^ ]*' "$logtxt" | head -1)"

# item 4: the screenshot, checked against the golden.
note "\n== item 4: screenshot =="
if [ ! -f "$repo/out/m3.ppm" ]; then
        fail "item 4: no out/m3.ppm produced"
else
        gcc -std=c99 -O2 -Wall -o "$repo/out/ppm_cmp" "$repo/tools/ppm_cmp.c" -lm || fail "ppm_cmp build"
        if [ ! -f "$golden" ]; then
                cp "$repo/out/m3.ppm" "$golden"
                note "item 4: golden created at $golden"
                note "        Look at it before trusting it: a red car with wheels,"
                note "        seen from the front-left and above. If it shows exploded"
                note "        triangles, missing or misplaced wheels, or black/garbage"
                note "        textures, delete the golden and debug, then re-run."
        elif "$repo/out/ppm_cmp" "$repo/out/m3.ppm" "$golden" >/dev/null 2>&1; then
                note "item 4: screenshot matches $golden -- PASS"
        else
                fail "item 4: screenshot does not match the golden"
        fi
fi

note ""
[ "$rc" -eq 0 ] && note "M3: PASS" || note "M3: FAIL (see above)"
exit $rc
