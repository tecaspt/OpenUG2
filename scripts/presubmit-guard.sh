#!/bin/sh
# Presubmit guard for the OpenUG2 PSP repo.
# Checks the STAGED files for retail-derived data, oversized blobs, and retail
# paths embedded in binaries. Exits 1 on any rejection.
#
# Adapted from /home/nike/openug2-dev/scripts/presubmit-guard.sh, which checked
# commits BASE..HEAD for the OpenUG2 contribution workflow. This version checks
# the index instead: the PSP repo commits small steps continuously and has no
# upstream base to diff against. The "local infrastructure" rule of the original
# is dropped -- this repo legitimately names local toolchain paths -- and the
# credential rule is kept.
#
# Limitation: filenames containing whitespace are not handled (as in the original).
set -u
cd "$(git rev-parse --show-toplevel)" || exit 2
fail=0
note() { echo "presubmit: REJECT $*"; fail=1; }

files=$(git diff --cached --name-only --diff-filter=ACM)
[ -n "$files" ] || { echo "presubmit: nothing staged"; exit 0; }

for f in $files; do
    # 1. Retail-derived or build-derived types are never committed.
    case "$f" in
        *.u2pk|*.ppm|*.BIN|*.BUN|*.PBP)
            note "retail/derived type: $f"; continue;;
    esac

    # 2. Images are only allowed as self-made synthetic fixtures.
    case "$f" in
        *.png|*.PNG)
            case "$f" in
                tests/synthetic/*) : ;;
                *) note "png outside tests/synthetic/: $f"; continue;;
            esac;;
    esac

    blob=$(git rev-parse ":$f" 2>/dev/null) || continue

    # 3. Size cap: 256 KB.
    size=$(git cat-file -s "$blob")
    [ "$size" -gt 262144 ] && note "file larger than 256 KB ($size bytes): $f"

    # 4. Retail data paths embedded in a binary blob. Binaries only: docs and
    #    test scripts legitimately name the read-only retail data directory.
    nul=$(git cat-file blob "$blob" | tr -d -c '\0' | wc -c)
    if [ "$nul" -gt 0 ]; then
        if git cat-file blob "$blob" | LC_ALL=C grep -qaE '/home/nike/nfsu2/(TRACKS|CARS|GLOBAL)'; then
            note "retail data path inside binary: $f"
        fi
    fi
done

# 5. Credential-like strings in staged content.
git diff --cached | LC_ALL=C grep -a '^+' | LC_ALL=C grep -av '^+++' \
  | LC_ALL=C grep -qaE 'gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}|BEGIN [A-Z ]*PRIVATE KEY|xox[baprs]-' \
  && note "credential-like string in staged diff"

[ $fail -eq 0 ] && echo "presubmit: PASS ($(echo "$files" | wc -l) staged files)"
exit $fail
