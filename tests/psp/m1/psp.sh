#!/usr/bin/env bash
# M1 PSP arm: cross-compile each GL-free upstream test, run it in PPSSPP with
# the MIT test-vector tree staged next to the EBOOT (tests read their default
# relative docs/... directory from the working directory, which PSP firmware
# sets to the game folder), then diff the guest capture against the host run.
#
#   usage: psp.sh [test ...]        (default: all nine)
#
# Outputs per test: out/m1/<test>.psp.txt (guest stdout) and a status table.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
up="$repo/third_party/OpenUG2"
out="$repo/out/m1"
game="$repo/out/ms0/PSP/GAME/OPENUG2"
PPSSPP=${PPSSPP:-/home/nike/openug2-dev/psp/ppsspp/build/PPSSPPHeadless}
LOCK=/home/nike/openug2-dev/.heavy.lock
TIMEOUT=${TIMEOUT:-60}
mkdir -p "$out"
. /home/nike/openug2-dev/psp/env-psp.sh

ALL="camera-test map-bake-test drivetrain-test braking-test resistance-test lateral-test contact-test body-response-test drive-stage1-test"
rc_all=0
printf '%-22s %-6s %-6s %-6s %-10s %s\n' TEST BUILD PSP PASS IDENTICAL KB
for t in ${*:-$ALL}; do
	dir="$here/$t"
	[ -d "$dir" ] || { echo "$t: no such test"; rc_all=1; continue; }

	build=FAIL
	if (cd "$dir" && make -B EBOOT.PBP >/dev/null 2>&1); then build=ok; fi
	[ "$build" = ok ] || { printf '%-22s %-8s\n' "$t" FAIL; rc_all=1; continue; }

	# stage: EBOOT + the cleanroom vector docs the tests read by default
	rm -rf "$game"
	mkdir -p "$game"
	cp "$dir/EBOOT.PBP" "$game/" || { printf '%-22s %-8s\n' "$t" STAGE; rc_all=1; continue; }
	# The nine tests read only the cleanroom vector trees (2 MB); the rest of
	# docs/ is prose and retail-comparison material they never open.
	mkdir -p "$game/docs"
	cp -r "$up/docs/cleanroom-camera" "$up/docs/cleanroom-driving-physics" "$game/docs/"
	# camera_test writes settings under build/camera_test_settings (host
	# make creates it; PSP has no mkdir here, and a missing dir shows up as
	# five bogus FAILs)
	mkdir -p "$game/build/camera_test_settings"

	flock "$LOCK" nice -n 10 "$PPSSPP" "$game/EBOOT.PBP" --graphics=software \
		--timeout="$TIMEOUT" >/dev/null 2>&1 </dev/null
	if [ -f "$game/stdout.txt" ]; then
		cp "$game/stdout.txt" "$out/$t.psp.txt"
		psp=ran
	else
		psp=NORUN
	fi

	ident=-
	pass=-
	if [ "$psp" = ran ]; then
		# The guest prints its own summary; "identical" is the strict gate:
		# capture must equal the host run byte-for-byte after CR removal and
		# after the harness's own MEM lines (psp_module.c boot report, which
		# has no host counterpart).
		a=$(tr -d '\r' < "$out/$t.host.txt")
		b=$(tr -d '\r' < "$out/$t.psp.txt" | grep -v '^MEM ')
		if [ "$a" = "$b" ]; then
			ident=yes
		else
			# yes* = the test's own output is byte-identical and the only
			# extra lines are the harness's COMPAT diagnostics (an allocation
			# that fits on the host but not in a PSP-1000 partition; see
			# PROGRESS.md M1). Any other difference stays "no".
			n=$(printf '%s\n' "$b" | grep -c '^COMPAT ')
			c=$(printf '%s\n' "$b" | grep -v '^COMPAT ')
			if [ "$a" = "$c" ] && [ "$n" -gt 0 ]; then
				ident="yes*($n)"
			else
				ident=no
			fi
		fi
		if grep -qE "0 failures|0 failed so far$|^PASS|tests passed|map_bake_test: PASS" "$out/$t.psp.txt"; then
			pass=PASS
		else
			pass=FAIL
		fi
	fi

	kb=$(psp-size "$dir/$(sed -n 's/^TARGET *= *//p' "$dir/Makefile" | head -1).elf" \
		2>/dev/null | awk 'NR==2{printf "%.0f", ($1+$2+$3)/1024}')
	printf '%-22s %-6s %-6s %-6s %-10s %s\n' "$t" "$build" "$psp" "$pass" "$ident" "$kb"
	case "$ident" in yes*) ok_ident=1 ;; *) ok_ident=0 ;; esac
	{ [ "$psp" = ran ] && [ "$pass" = PASS ] && [ "$ok_ident" = 1 ]; } || rc_all=1
done
exit $rc_all
