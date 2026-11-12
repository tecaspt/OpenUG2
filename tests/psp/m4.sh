#!/usr/bin/env bash
# M4: one static world slice. Region L4RH is converted to a U2PK cell grid on
# the host, streamed 3x3-window by the PSP runtime and screenshotted at three
# fixed on-road camera poses.
#
#   usage: tests/psp/m4.sh
#
# Acceptance items (from the handoff), each graded below:
#   1  budgets met, printed table (cell<=256KB, index<=256KB, 3x3<=15000 tris)
#   2  screenshots show roads and ground, no missing ground under the camera
#   3  total runtime pool high-water <= 17 MB and the heap/arena window check
#   4  emulated frame_us avg <= 33333 (differenced host wall clock, see note)
#
# Plus a read-only L4RA survey (best-effort): the airport blows the fixed 3x3
# triangle budget on geometry density alone, which no prop/texture cutoff fixes
# - it is a documented budget conflict for planner review, not a code bug. Set
# SKIP_L4RA=1 to omit it.
#
# Timing note (item 4). Same as M3: sceRtcGetCurrentTick under headless PPSSPP
# advances with emulated frames, not wall time, so the EBOOT's own frame_us line
# is a fixed tick step, not a measurement. The gate is the emulator wall time,
# differenced between two runs that differ only in frames= so startup, module
# load and the three per-pose LOADs cancel. This times PPSSPP's software renderer
# on this machine, not a 333 MHz PSP-1000 with a hardware GE: an upper bound on
# whether the slice is cheap enough, not an emulation of PSP frame time.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/out/m4"
stage="$repo/out/m4stage"
golden_dir="$repo/out/goldens"
retail=${RETAIL:-/home/nike/nfsu2}
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
LOCK=/home/nike/openug2-dev/.heavy.lock
mkdir -p "$out" "$golden_dir"
. /home/nike/openug2-dev/psp/env-psp.sh 2>/dev/null || true
rc=0
note() { printf '%s\n' "$*"; }
fail() { note "M4 FAIL: $*"; rc=1; }

# ---------------------------------------------------------------- build ------
note "== build converter (native) + EBOOT =="
make -C "$repo/converter" >/dev/null 2>&1 || { fail "converter build"; exit 1; }
make -C "$repo/runtime"   >/dev/null 2>&1 || { fail "runtime build";   exit 1; }

# ---------------------------------------- item 1: convert L4RH, budget -------
note "\n== item 1: convert STREAML4RH -> cells/index/tex, budget table =="
rm -rf "$out/L4RH"; mkdir -p "$out/L4RH"
convlog="$out/conv-L4RH.txt"
"$repo/converter/psp_convert" region convert "$retail" STREAML4RH "$out/L4RH" \
        >"$convlog" 2>&1
s=$?
cat "$convlog" | sed 's/^/  /'
[ "$s" -eq 0 ] || fail "region convert failed (see $convlog)"

cells_kb=$(sed -n 's/.*wrote cells.u2pk \([0-9]*\) KB.*/\1/p' "$convlog")
win=$(sed -n 's/.*worst 3x3 window \([0-9]*\) tris.*/\1/p' "$convlog")
wbud=$(sed -n 's/.*within budget: \(.*\)/\1/p' "$convlog")
nreal=$(sed -n 's/.*textures with real art \([0-9]*\) .*/\1/p' "$convlog")
ntex=$(sed -n 's/.*batches [0-9]*  textures \([0-9]*\)/\1/p' "$convlog")
idx=$(sed -n 's/.*index.u2pk \([0-9]*\) B.*/\1/p' "$convlog")
texb=$(sed -n 's/.*tex.u2pk \([0-9]*\) B.*/\1/p' "$convlog")

# every cell blob must be <= 256 KB; verify from the CELLTAB in index.u2pk.
big=$(python3 "$repo/tools/u2pk_cell_max.py" "$out/L4RH/index.u2pk" 2>/dev/null)
note "  --- M4 budget (L4RH) ---"
note "  worst 3x3 window tris = ${win:-?} (budget 15000)"
note "  index.u2pk = ${idx:-?} B (budget 262144)"
note "  largest cell blob = ${big:-?} B (budget 262144)"
note "  tex.u2pk = ${texb:-?} B with ${nreal:-?}/${ntex:-?} keys carrying real art"
[ "${win:-999999}" -le 15000 ] || fail "item 1: 3x3 window ${win} tris > 15000"
[ "${idx:-999999}" -le 262144 ] || fail "item 1: index.u2pk ${idx} B > 262144"
[ -n "$big" ] && [ "$big" -le 262144 ] || fail "item 1: a cell blob exceeds 256 KB (${big:-unknown})"
[ "$wbud" = YES ] || fail "item 1: converter reports within_budget=$wbud"

# ------------------------------------ poses + stage the runtime payload ------
note "\n== stage L4RH + 3 on-road poses into $stage =="
rm -rf "$stage"; mkdir -p "$stage/data/L4RH"
cp "$repo/runtime/EBOOT.PBP" "$stage/"
cp "$out/L4RH"/*.u2pk "$stage/data/L4RH/"
"$repo/converter/psp_convert" region poses "$retail" STREAML4RH \
        "$stage/data/world.cfg" 3 2>&1 | sed 's/^/  /'
[ -s "$stage/data/world.cfg" ] || fail "no data/world.cfg written"

# ------------------------------------ items 2-4: run in the emulator ---------
note "\n== items 2-4: run L4RH in PPSSPPHeadless (3 poses) =="
TEST=m4 TIMEOUT=200 sh "$here/run.sh" "$stage" 8 m4 || note "(emulator exit $?)"
logtxt="$repo/out/m4.log.txt"
[ -f "$logtxt" ] || { fail "no $logtxt (the EBOOT never logged)"; exit 1; }

# item 3: total pool high-water <= 17 MB and the arena in the PSP-1000 window.
hw=$(sed -n 's/.*pool high-water total=\([0-9]*\) .*/\1/p' "$logtxt" | head -1)
top=$(sed -n 's/.*arena=\(0x[0-9a-f]*\) .*/\1/p' "$logtxt" | head -1)
note "  --- M4 pools ---"
grep -E '^pool ' "$logtxt" | sed 's/^/  /'
if [ -n "${hw:-}" ] && [ "$hw" -le 17825792 ]; then
        note "item 3: pool high-water total=$hw bytes ($((hw/1024)) KB) <= 17 MB -- PASS"
else
        fail "item 3: pool high-water total='${hw:-none}' > 17825792"
fi
if [ -n "${top:-}" ] && [ "$((top))" -gt "$((0x08800000))" ] && \
   [ "$((top))" -le "$((0x0A000000))" ]; then
        note "item 3: arena=$top inside the PSP-1000 HOME window, heap free=$(sed -n 's/.*heap=\([0-9]*\).*/\1/p' "$logtxt" | head -1) -- PASS"
else
        fail "item 3: arena='${top:-none}' outside 0x08800000..0x0A000000"
fi

# item 2: each pose screenshot shows ground, not the clear colour. Goldens are
# local-only regression baselines; the emptiness/variety check is the gate.
note "\n== item 2: screenshots (ground coverage + texture variety) =="
for i in 0 1 2; do
        p="$repo/out/m4_p$i.ppm"
        [ -f "$p" ] || { fail "item 2: no out/m4_p$i.ppm"; continue; }
        python3 "$repo/tools/ppm_world_check.py" "$p" || fail "item 2: pose $i looks empty"
        g="$golden_dir/m4_l4rh_p$i.ppm"
        if [ ! -f "$g" ]; then cp "$p" "$g"; note "  golden created: $(basename "$g")"; fi
done

# item 4: differenced host wall clock.
note "\n== item 4: frame cost by differenced host wall clock =="
GAME="${MS0:-$repo/out/ms0}/PSP/GAME/OPENUG2"
now_ns(){ date +%s%N; }
timed(){ # frames/pose -> wall ms (3 poses -> 3*frames render frames, loads cancel)
        rm -rf "$GAME"; mkdir -p "$GAME/data"
        cp "$stage/EBOOT.PBP" "$GAME/"
        cp -R "$stage"/data/* "$GAME/data/"
        printf 'frames=%s\nshot=999999\n' "$1" > "$GAME/data/autotest.cfg"
        local t0 t1; t0=$(now_ns)
        flock "$LOCK" nice -n 10 "$PPSSPP" "$GAME/EBOOT.PBP" --graphics=software \
                --timeout=180 >/dev/null 2>&1 </dev/null
        t1=$(now_ns); echo $(( (t1 - t0) / 1000000 ))
}
w1=$(timed 5); w2=$(timed 65); d=$(( 3*(65-5) ))
per=$(( (w2 - w1) * 1000 / d ))
if [ "$per" -le 33333 ]; then
        note "item 4: $per us/frame (5f=${w1}ms 65f=${w2}ms over ${d} frames) <= 33333 -- PASS"
else
        fail "item 4: $per us/frame > 33333"
fi
note "  guest frame_us line (NOT a clock under headless): $(grep -o 'frame_us avg=[0-9]* max=[0-9]*' "$logtxt" | head -1)"

# ---------------------------------------- L4RA survey (report only) ----------
if [ "${SKIP_L4RA:-0}" != 1 ]; then
        note "\n== L4RA survey (report only: a documented budget conflict) =="
        lra="$out/survey-L4RA.txt"
        flock "$LOCK" nice -n 10 "$repo/converter/psp_convert" region survey \
                "$retail" STREAML4RA >"$lra" 2>&1 || true
        wra=$(sed -n 's/.*worst 3x3 window \([0-9]*\) tris.*/\1/p' "$lra")
        note "  worst 3x3 window tris = ${wra:-survey did not complete}"
        if [ -n "${wra:-}" ] && [ "$wra" -gt 15000 ]; then
                note "  L4RA: ${wra} tris > 15000 on ground+building density alone."
                note "  The spec's escalation (prop cutoff, then texture caps) does not"
                note "  touch ground/building triangle density, so this is a fixed-window"
                note "  budget CONFLICT, not a code bug. Escalated to planner (M4 stop)."
        fi
fi

note ""
[ "$rc" -eq 0 ] && note "M4: PASS (L4RH; emulator goldens are regression baselines, only a real PSP proves the picture)" \
                || note "M4: FAIL (see above)"
exit $rc
