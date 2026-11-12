#!/bin/sh
# M0: build, check the SFO, run in the emulator, verify heap and screenshot.
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
GOLDEN_DIR="$REPO/out/goldens"
GOLDEN="$GOLDEN_DIR/m0.ppm"
SHOT_PPM="$REPO/out/m0.ppm"

cd "$REPO/runtime" || exit 1
. /home/nike/openug2-dev/psp/env-psp.sh

echo "== build =="
flock /home/nike/openug2-dev/.heavy.lock nice -n 10 make || exit 1

echo "== sfo_check (acceptance M0.1) =="
python3 "$REPO/tools/sfo_check.py" EBOOT.PBP || exit 1

echo "== size (acceptance M0.5) =="
psp-size openug2.elf || exit 1

echo "== run in PPSSPPHeadless =="
TEST=m0 sh "$REPO/tests/psp/run.sh" "$REPO/runtime" 60 30
rc=$?
echo "-- emulator log --"
cat "$REPO/out/m0.log"
[ $rc -eq 0 ] || echo "note: emulator exit=$rc (checking further)"

LOGTXT="$REPO/out/m0.log.txt"
[ -f "$LOGTXT" ] || { echo "M0: no log.txt produced by the runtime -- FAIL"; exit 1; }

echo "== memory (acceptance M0.2) =="
# Partition free at entry cannot discriminate models (the loader pre-allocates
# the C heap), so the check is: the 17 MB arena exists AND every byte of it
# lives inside the 24 MB PSP-1000 HOME window (0x08800000..0x0A000000).
# Anything at or above 0x0A000000 could only come from extended memory, which
# MEMSIZE=0 in the SFO (checked above) already forbids; this proves it at run
# time. memalign succeeding at all is the "17 MB available" half of the check.
top=$(sed -n 's/^M0 OK arena_top=\(0x[0-9a-f]*\).*/\1/p' "$LOGTXT" | head -1)
if [ -z "${top:-}" ]; then
	echo "M0: no 'M0 OK' line -- FAIL"; exit 1
fi
topdec=$((top))
echo "arena_top=$top ($topdec)"
if [ "$topdec" -le $((0x08800000)) ] || [ "$topdec" -gt $((0x0A000000)) ]; then
	echo "M0: 17 MB arena outside PSP-1000 HOME window -- FAIL"; exit 1
fi
echo "M0: arena inside PSP-1000 HOME window (no extended memory)"

echo "== screenshot =="
if [ ! -f "$SHOT_PPM" ]; then
	echo "M0: no $SHOT_PPM produced -- FAIL"; exit 1
fi
gcc -std=c99 -O2 -Wall -o "$REPO/out/ppm_cmp" "$REPO/tools/ppm_cmp.c" -lm || exit 1
if [ ! -f "$GOLDEN" ]; then
	mkdir -p "$GOLDEN_DIR"
	cp "$SHOT_PPM" "$GOLDEN"
	echo "M0: golden created at $GOLDEN (inspect it before trusting it)"
	exit 0
fi
"$REPO/out/ppm_cmp" "$SHOT_PPM" "$GOLDEN" || exit 1
echo "M0: PASS"
