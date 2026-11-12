#!/usr/bin/env bash
# M1 step 5 (host arm): build each GL-free upstream test with the host gcc and
# run it from the OpenUG2 repo root, so the default relative test-vector
# directories resolve. Saves stdout+stderr per test as out/m1/<test>.host.txt.
# Light work: no heavy lock needed.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"        # openug2-psp repo root
up="$root/third_party/OpenUG2"
out="$root/out/m1"
mkdir -p "$out/host"
CC=${CC:-gcc}
CFLAGS="-O2 -std=gnu99 -Wall -Wextra -Wno-unused-function -I$up/src -I$up/tools"

# test name -> sources (mirrors the upstream top-level Makefile rules)
declare -A SRC=(
  [camera-test]="tools/camera_test.c src/camera.c"
  [map-bake-test]="tools/map_bake_test.c"
  [drivetrain-test]="tools/drivetrain_test.c src/drivetrain.c src/braking.c"
  [braking-test]="tools/braking_test.c src/braking.c src/drivetrain.c"
  [resistance-test]="tools/resistance_test.c src/resistance.c src/drivetrain.c src/braking.c"
  [lateral-test]="tools/lateral_test.c src/lateral.c"
  [contact-test]="tools/contact_test.c src/contact.c src/resistance.c src/braking.c"
  [body-response-test]="tools/body_response_test.c src/body_response.c src/contact.c src/resistance.c src/braking.c"
  [drive-stage1-test]="tools/drive_stage1_test.c src/lateral.c src/drive_stage1.c src/contact.c src/body_response.c src/drive_contact.c src/drivetrain.c src/braking.c src/resistance.c src/physics.c"
)
tests=${*:-"camera-test map-bake-test drivetrain-test braking-test resistance-test lateral-test contact-test body-response-test drive-stage1-test"}
rc=0
for t in $tests; do
  bin="$out/host/${t%-test}"
  echo "== $t"
  # shellcheck disable=SC2086
  if ! ( cd "$up" && $CC $CFLAGS -o "$bin" ${SRC[$t]} -lm ); then
    { echo "HOST BUILD FAILED"; } > "$out/$t.host.txt"
    rc=1
    continue
  fi
  ( cd "$up" && "$bin" ) > "$out/$t.host.txt" 2>&1
  st=$?
  echo "   exit=$st -> $out/$t.host.txt"
  [ $st -eq 0 ] || rc=1
done
exit $rc
