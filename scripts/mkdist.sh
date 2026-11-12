#!/usr/bin/env bash
# Builds the user-testable dist/M<n>/ folders from the current tree.
#
# Nothing under dist/ is committed: it holds EBOOT.PBP and .ppm build products,
# which scripts/presubmit-guard.sh rejects by type, and they are fully
# reproducible from the pinned submodule plus this repo. This script is
# therefore the versioned description of the deliverable, README text included.
#
#   usage: scripts/mkdist.sh          (after tests/psp/m0.sh and tests/psp/m1/psp.sh)
set -eu
repo="$(cd "$(dirname "$0")/.." && pwd)"
up="$repo/third_party/OpenUG2"

# Folder names are kept to 8 uppercase characters: the PSP's XMB is happier
# with short GAME folder names, and the visible name comes from PARAM.SFO
# anyway (PSP_EBOOT_TITLE, i.e. the makefile TARGET).
declare -A SHORT=(
  [camera-test]=UG2CAM [map-bake-test]=UG2MAP [drivetrain-test]=UG2DRIV
  [braking-test]=UG2BRK [resistance-test]=UG2RES [lateral-test]=UG2LAT
  [contact-test]=UG2CON [body-response-test]=UG2BODY [drive-stage1-test]=UG2ST1
)
# Order of the README table.
ORDER="camera-test map-bake-test drivetrain-test braking-test resistance-test lateral-test contact-test body-response-test drive-stage1-test"

rm -rf "$repo/dist"
mkdir -p "$repo/dist"

# ---------------------------------------------------------------- M0 ----------
m0="$repo/dist/M0/PSP/GAME/OPENUG2"
[ -f "$repo/runtime/EBOOT.PBP" ] || { echo "mkdist: runtime/EBOOT.PBP missing (run tests/psp/m0.sh)" >&2; exit 1; }
mkdir -p "$m0"
cp "$repo/runtime/EBOOT.PBP" "$m0/"
cp "$repo/out/goldens/m0.ppm" "$repo/dist/M0/golden-m0.ppm"
cat > "$repo/dist/M0/README.txt" <<'TXT'
OpenUG2 PSP - M0: hello GU
==========================

Copy the PSP folder to the root of the memory stick, so the game ends up at
ms0:/PSP/GAME/OPENUG2/EBOOT.PBP, on a PSP-1000 with custom firmware. Then start
"OpenUG2" from the XMB.

What you should see:
  - a checkerboard triangle, drawn with the GU, filling most of the screen;
  - a square split red and blue (a DXT1 texture) beside it;
  - pressing buttons does nothing visible - M0 has no input handling yet.

The program never exits by itself. A log is appended to
ms0:/PSP/GAME/OPENUG2/log.txt; the first lines look like

  M0 START argv0=... dir=ms0:/PSP/GAME/OPENUG2
  pool: arena=0x... carve=11796480 of 17825792 bytes
  gfx: 480x272 stride 512, vram=0x4000000
  M0 OK arena_top=0x09a36e50 ...

"M0 OK" means the 17 MB static arena was carved and every byte of it is below
0x0A000000, i.e. inside the 24 MB of RAM a PSP-1000 actually has. If instead the
log stops after "M0 START", or the screen stays black, copy log.txt out and
report it.

Do NOT create data/autotest.cfg here: that file makes the program take a
screenshot and quit after N frames, which is what the emulator harness uses.
golden-m0.ppm next to this README is the emulator reference picture, 480x272.
TXT

# ---------------------------------------------------------------- M1 ----------
m1="$repo/dist/M1"
mkdir -p "$m1/expected"
n=0
for t in $ORDER; do
	dir="$repo/tests/psp/m1/$t"
	[ -f "$dir/EBOOT.PBP" ] || { echo "mkdist: $dir/EBOOT.PBP missing" >&2; exit 1; }
	g="$m1/PSP/GAME/${SHORT[$t]}"
	mkdir -p "$g"
	cp "$dir/EBOOT.PBP" "$g/"
	mkdir -p "$g/docs"
	cp -r "$up/docs/cleanroom-camera" "$up/docs/cleanroom-driving-physics" "$g/docs/"
	# camera_test writes its settings file under build/camera_test_settings and
	# counts a missing directory as five failures.
	if [ "$t" = camera-test ]; then
		mkdir -p "$g/build/camera_test_settings"
	fi
	cp "$repo/out/m1/$t.host.txt" "$m1/expected/$t.txt"
	n=$((n+1))
done
{
	echo "OpenUG2 PSP - M1: upstream physics tests on the PSP CPU"
	echo "======================================================"
	echo
	echo "$n of the 11 upstream GL-free tests are here; ground-motion-test and"
	echo "race-ai-test are skipped because they need GL (render.c and the generated"
	echo "shaders), which the PSP build does not have."
	echo
	echo "Copy the PSP folder to the root of the memory stick (games land in"
	echo "ms0:/PSP/GAME/), then start a test from the XMB. Each test runs to"
	echo "completion and quits by itself, writing its verdict to stdout.txt in its"
	echo "own game folder. Compare that file with expected/<test>.txt, which is the"
	echo "same test built for x86-64 on the same sources; the two are expected to"
	echo "agree byte for byte."
	echo
	echo "Folder    test                   the last line of stdout.txt reads"
	for t in $ORDER; do
		printf '%-9s %-22s %s\n' "${SHORT[$t]}" "$t" "$(tail -1 "$m1/expected/$t.txt")"
	done
	echo
	echo "Known, explained difference: UG2MAP (map-bake-test) carries five lines of"
	echo "COMPAT malloc FAILED n=4194304 that the host run does not have. Upstream"
	echo "map_bake.h calls mb_fill_seams() while about 20 MB is already live, and that"
	echo "step needs a 4 MB queue plus a 1 MB bitmap on top: a ~25 MB peak, while a"
	echo "PSP-1000 has a 24 MB user partition and the heap is asked for 'everything"
	echo "but 1 MB', so it cannot fit however the allocator behaves. Upstream guards"
	echo "the step with 'if (queue && seen)', so it is skipped and all 37 checks still"
	echo "match the host run exactly. The map bake ships as an offline step in"
	echo "psp_convert, where memory is not scarce."
	echo
	echo "Nothing in this folder came from retail game data: the vector trees under"
	echo "docs/ are the cleanroom MIT test vectors from the OpenUG2 submodule."
} > "$m1/README.txt"

# ---------------------------------------------------------------- M2 ----------
m2="$repo/dist/M2/PSP/GAME/UG2BENCH"
[ -f "$repo/runtime/bench/EBOOT.PBP" ] || {
	echo "mkdist: runtime/bench/EBOOT.PBP missing (run tests/psp/m2.sh)" >&2; exit 1; }
[ -f "$repo/out/m2/bench.host.txt" ] || {
	echo "mkdist: out/m2/bench.host.txt missing (run tests/psp/m2.sh)" >&2; exit 1; }
mkdir -p "$m2" "$repo/dist/M2/expected"
cp "$repo/runtime/bench/EBOOT.PBP" "$m2/"
cp "$repo/out/m2/bench.host.txt" "$repo/dist/M2/expected/bench.txt"
cat > "$repo/dist/M2/README.txt" <<'TXT'
OpenUG2 PSP - M2: how much does one physics substep cost
=========================================================

Copy the PSP folder to the root of the memory stick, so the program lands at
ms0:/PSP/GAME/UG2BENCH/EBOOT.PBP, on a PSP-1000 with custom firmware. Delete any
log.txt that is already there, then start "UG2 Physics Bench" from the XMB.

It steps four cars through 1000 physics substeps, three times, and writes
log.txt next to itself. Nothing appears on screen - the milestone is about CPU
time, not pictures. Expect it to take a while: on the model in PROGRESS.md the
12000 car-substeps take about 30 seconds, so a screen that looks frozen for a
minute is the measurement working, not a hang. Press HOME if you want out.

The number this milestone needs and could not get honestly from the emulator is
the line

  PHYS us_per_substep_per_car=<x>

(sceRtcGetCurrentTick is a real clock on hardware; under PPSSPP headless it is
not, which is why the emulator cannot answer this - see PROGRESS.md, M2).
Report back the whole log.txt either way. The four "PHYS car" lines at the end
must match expected/bench.txt exactly, which says the physics computes the same
bits on MIPS that it does on x86/ARM; the timing line says what it costs.

  predicted from the instruction count:  x about 2000 to 3000 microseconds
  if x is far below 500, say so: that would mean the model is wrong and the
  budget should be re-graded from your number

Optional, for a longer run: create data/bench.cfg next to the EBOOT containing

  mode=long rounds=300

and the bench instead steps 300 frames' worth of physics (4 cars x 8 substeps
each) and prints one PHYS long line. That mode is what the wall-time measurement
uses; do not create the file for the standard run above.

Nothing here came from retail game data: the car is the cleanroom fixture from
the OpenUG2 submodule's drive-stage1 test.
TXT
{
	echo
	echo "Reference, the same program built for this Pi (gcc -O2, hardware double"
	echo "arithmetic - 1500x faster than a PSP is expected to be, see PROGRESS.md):"
	grep -E '^PHYS (pass|us_per|frame|car)' "$repo/out/m2/bench.host.txt" | sed 's/^/  /'
} >> "$repo/dist/M2/README.txt"

# ---------------------------------------------------------------- M3 ----------
m3="$repo/dist/M3/PSP/GAME/OPENUG2"
[ -f "$repo/converter/psp_convert" ] || {
	echo "mkdist: converter/psp_convert missing (make -C converter)" >&2; exit 1; }
mkdir -p "$m3"
cp "$repo/runtime/EBOOT.PBP" "$m3/"
cp "$repo/converter/psp_convert" "$repo/dist/M3/"
cp "$repo/out/goldens/m3_miata.ppm" "$repo/dist/M3/golden-m3.ppm"
cat > "$repo/dist/M3/README.txt" <<'TXT'
OpenUG2 PSP - M3: a real car, its LODs and its textures
========================================================

This milestone draws one real car, read from a file you build yourself with the
psp_convert tool in this folder. Two files go onto the memory stick:

  1. EBOOT.PBP ->  ms0:/PSP/GAME/OPENUG2/EBOOT.PBP
     (copy the whole PSP folder here to the root of the memory stick)

  2. car.u2pk ->   ms0:/PSP/GAME/OPENUG2/data/car.u2pk
     (build it in one step, see below; create the data folder first)

Make car.u2pk on this Pi (the same machine's aarch64 Linux):

  ./psp_convert car /path/to/NFSU2 MIATA car.u2pk

Replace /path/to/NFSU2 with the folder that holds CARS/ and GLOBAL/ (the
unpacked NFS Underground 2 PC data), and MIATA with any of MIATA, 350Z, 240SX,
A3, 3000GT (or the full name of any other single-car CARS/<name> folder). The
tool prints the LOD budget table and ends with "VALIDATED". It writes nothing
derived back into your NFSU2 folder.

Then, on the stick, it should look like

  ms0:/PSP/GAME/OPENUG2/EBOOT.PBP
  ms0:/PSP/GAME/OPENUG2/data/car.u2pk

Start "OpenUG2" from the XMB on a PSP-1000 with custom firmware. You should see
a car turning slowly in front of a plain background, drawn with its textures and
its four wheels. Left analog nudges the camera; START exits.

About the colour: M3 has no per-car paint system yet, so every car's body is
baked the single fleet-default colour - red (0xC82828). So MIATA is meant to look
red here even if your favourite MIATA was silver. A car with the right SHAPE and
wheels but the wrong HUE is the expected M3 result; a car of exploded triangles,
missing or misplaced wheels, or black/garbage textures is a bug - report it with
the log.txt that lands next to the EBOOT.

If instead the screen is black and log.txt says

  M3 FAIL could not load data/car.u2pk (run psp_convert first)

then car.u2pk is missing or in the wrong place - it must be in data/ next to the
EBOOT, spelled exactly car.u2pk.

Do NOT create data/autotest.cfg here: that makes the program take a screenshot
and quit after N frames, which is only what the emulator harness uses.
golden-m3.ppm next to this README is the emulator reference picture (MIATA,
480x272); the emulator, not the PSP's hardware GE, drew it.
TXT

# ---------------------------------------------------------------- M4 ----------
m4="$repo/dist/M4/PSP/GAME/OPENUG2"
mkdir -p "$m4"
cp "$repo/runtime/EBOOT.PBP" "$m4/"
cp "$repo/converter/psp_convert" "$repo/dist/M4/"
for i in 0 1 2; do
	[ -f "$repo/out/goldens/m4_l4rh_p$i.ppm" ] &&
		cp "$repo/out/goldens/m4_l4rh_p$i.ppm" "$repo/dist/M4/golden-m4-p$i.ppm"
done
cat > "$repo/dist/M4/README.txt" <<'TXT'
OpenUG2 PSP - M4: a slice of the world, drawn from real region data
====================================================================

M4 has no driving and no camera control yet (that is M5). It converts one
region of your NFS Underground 2 PC data into a grid of world "cells" on this
Pi, streams the 3x3 cells around a few fixed road-side camera poses, and draws
them - roads and terrain, textured, with distance fog.

Files that go on the memory stick:

  1. EBOOT.PBP ->  ms0:/PSP/GAME/OPENUG2/EBOOT.PBP
     (copy the whole PSP folder to the root of the memory stick)

  2. the data folder -> ms0:/PSP/GAME/OPENUG2/data/
     containing
        data/L4RH/index.u2pk
        data/L4RH/cells.u2pk
        data/L4RH/tex.u2pk
        data/world.cfg

Build the data on this Pi (aarch64 Linux), from this folder:

  ./psp_convert region convert /path/to/NFSU2 STREAML4RH PSP/GAME/OPENUG2/data/L4RH
  ./psp_convert region poses   /path/to/NFSU2 STREAML4RH PSP/GAME/OPENUG2/data/world.cfg 3

Replace /path/to/NFSU2 with the folder that holds TRACKS/ and GLOBAL/ (the
unpacked NFS Underground 2 PC data). The first command prints a budget table and
writes the three .u2pk files; the second writes three on-road camera poses.
Neither writes anything back into your NFSU2 folder.

Start "OpenUG2" from the XMB on a PSP-1000 with custom firmware. You should see
three brief views - about a third of a second each - of textured ground and
roads stretching to a foggy horizon, from a camera 2 m above a road looking
along it. The program writes a screenshot of each view and then quits.

  IMPORTANT: it writes ms0:/PSP/GAME/OPENUG2/m4_p0.ppm, m4_p1.ppm and
  m4_p2.ppm next to the EBOOT. Those are the real PSP-1000 pictures, which is
  the one thing the emulator cannot prove - please copy them out and send them
  back. A grey/white or all-clear screen, or a view with a hole in the ground
  under the camera, is a bug to report; roads and terrain reaching a horizon is
  the M4 result.

If instead the screen is black and log.txt says

  M4 FAIL could not open world data/L4RH (run psp_convert first)

then the data folder is missing or misnamed - it must sit next to the EBOOT as
data/L4RH/{index,cells,tex}.u2pk with data/world.cfg beside it.

L4RA (the airport) is deliberately NOT included: it holds so much ground and
building geometry that its worst 3x3 window is 57,745 triangles against the
15,000 slice budget, and its worst single cell exceeds the 256 KB blob cap. The
spec's only tuning levers (drop small props, cap textures) do not reduce that
density, so L4RA is flagged for the planner rather than shipped here. L4RH is
the slice that fits.

Do NOT create data/autotest.cfg here: that is only what the emulator harness
uses to cap frames and pick a screenshot frame.
TXT

echo "mkdist: dist/M0 (1 EBOOT), dist/M1 ($n EBOOTs), dist/M2 (1 EBOOT), dist/M3 and dist/M4 (EBOOT + psp_convert) written"
du -sh "$repo/dist/M0" "$repo/dist/M1" "$repo/dist/M2" "$repo/dist/M3" "$repo/dist/M4"
