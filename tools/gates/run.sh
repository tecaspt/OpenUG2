#!/bin/sh
# One run of a gate, detached, with the environment the game needs.
#
# Usage: tools/gates/run.sh [GATEFILE] [OUTDIR]   (defaults: tools/gates/m1.json, gate-out/<name>)
#
# OPENUG2_DATA comes from $DEV/env.sh; OPENUG2_GATE_REF (the reference screenshots the INV steps
# compare against) is not in env.sh and must be exported by the caller or defaulted here.
# OPENUG2_HEAVY_LOCK is set so this run queues behind any other build or race on the board - it only
# works if whoever else is running uses the same path.
#
# Logs, screenshots and the two summaries go to OUTDIR. A whole gate takes an hour or two, so this
# is meant to be run under nohup rather than in a shell that can die with a session.
set -e
GATE=${1:-tools/gates/m1.json}
NAME=$(basename "$GATE" .json)
OUT=${2:-gate-out/$NAME}
: "${DEV:=$(cd "$(dirname "$0")/../../.." && pwd)}"
. "$DEV/env.sh"
export OPENUG2_GATE_REF=${OPENUG2_GATE_REF:-$DEV/reports/parity/gint-gate}
export OPENUG2_HEAVY_LOCK=${OPENUG2_HEAVY_LOCK:-/tmp/openug2-heavy.lock}
cd "$(dirname "$0")/../.."
exec python3 -u -I tools/gate_run.py "$GATE" --out "$OUT" "${@:3}"
