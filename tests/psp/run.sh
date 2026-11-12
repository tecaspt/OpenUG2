#!/bin/sh
# Run a built EBOOT in PPSSPPHeadless with the software renderer.
#
#   usage: run.sh <dir-containing-EBOOT.PBP> <frames> <shot>
#
# Environment:
#   TEST     name used for the log/screenshot copies (default: dir basename)
#   TIMEOUT  emulator timeout in seconds (default 60)
#   PPSSPP   path to PPSSPPHeadless
#   MS0      host directory the emulator maps as ms0:
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
MS0=${MS0:-$REPO/out/ms0}
TIMEOUT=${TIMEOUT:-60}
LOCK=/home/nike/openug2-dev/.heavy.lock

SRC_DIR=${1:-}
FRAMES=${2:-}
SHOT=${3:-}
[ -n "$SRC_DIR" ] && [ -n "$FRAMES" ] && [ -n "$SHOT" ] || {
	echo "usage: run.sh <EBOOT-dir> <frames> <shot>" >&2; exit 2; }
TEST=${TEST:-$(basename "$SRC_DIR")}

[ -x "$PPSSPP" ] || { echo "run.sh: no emulator at $PPSSPP" >&2; exit 1; }

GAME="$MS0/PSP/GAME/OPENUG2"
rm -rf "$GAME"
mkdir -p "$GAME/data" || exit 1
cp "$SRC_DIR/EBOOT.PBP" "$GAME/" || exit 1
# Optional data payload: milestones that ship a runtime asset (e.g. M3's
# car.u2pk, M4's data/<REGION>/ cell dir) drop it in <EBOOT-dir>/data and
# run.sh stages it beside the autotest config. Guarded so M0-M2 (no data dir)
# are unaffected. Recursive so a payload may carry subdirectories.
if [ -d "$SRC_DIR/data" ]; then
        cp -R "$SRC_DIR"/data/* "$GAME/data/" 2>/dev/null
fi
printf 'frames=%s\nshot=%s\n' "$FRAMES" "$SHOT" > "$GAME/data/autotest.cfg"

LOG="$REPO/out/$TEST.log"
mkdir -p "$REPO/out"

# Emulator runs count as heavy work for the shared-lock rule.
flock "$LOCK" nice -n 10 "$PPSSPP" "$GAME/EBOOT.PBP" --graphics=software \
	--timeout="$TIMEOUT" > "$LOG" 2>&1 </dev/null
rc=$?

# The runtime writes log.txt and screenshots next to the game folder; copy them
# into out/ so tests can compare them. It may land either in the ms0 tree or in
# the emulator's own memory-stick default, so check both.
for base in "$MS0/PSP/GAME/OPENUG2" "$HOME/.ppsspp/PSP/GAME/OPENUG2"; do
	[ -f "$base/log.txt" ] && cp "$base/log.txt" "$REPO/out/$TEST.log.txt"
	[ -f "$base/stdout.txt" ] && cp "$base/stdout.txt" "$REPO/out/$TEST.stdout.txt"
	for p in "$base"/*.ppm; do
		[ -f "$p" ] && cp "$p" "$REPO/out/"
	done
done

echo "run.sh: exit=$rc log=$LOG"
exit $rc
