#!/bin/sh
# Measures the 12-byte U2PK vertex format on the emulator and prints the facts
# docs/U2PK.md claims. Run it whenever pspsdk or PPSSPP changes.
#
#   tests/psp/vfmt.sh
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
LOCK=/home/nike/openug2-dev/.heavy.lock

cd "$REPO/runtime/vfmt" || exit 1
. /home/nike/openug2-dev/psp/env-psp.sh

echo "== build =="
flock "$LOCK" nice -n 10 make || exit 1

echo "== run in PPSSPPHeadless =="
TEST=vfmt sh "$REPO/tests/psp/run.sh" "$REPO/runtime/vfmt" 5 2
echo "-- runtime log --"
cat "$REPO/out/vfmt.log.txt" 2>/dev/null || echo "(no log.txt)"

echo "== measure =="
python3 "$REPO/tools/vfmt_probe.py" "$REPO/out/vfmt.ppm"
rc=$?
echo
echo "vfmt.sh: measurement exit=$rc"
exit $rc
