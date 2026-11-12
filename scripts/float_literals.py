#!/usr/bin/env python3
"""Suffix floating-point literals with `f`, conservatively.

Written to test one hypothesis about M2's cost: that the physics pays for
software double arithmetic because bare literals like 2.237 drag float maths
into double. The hypothesis is false and this script is the measurement that
falsified it - run over the nine physics units and their headers it finds six
literals to suffix, because the double arithmetic is structural (the physics
functions take and return double) rather than incidental. An earlier grep
counting 1035 unsuffixed literals was counting comments.

Kept because the question recurs: on any code where the maths *is* float and the
literals are not, suffixing is a real win on a CPU with no double FPU, and the
rules below are what make it safe to do in bulk. It refuses a line that mentions
`double`, a name the file declares as double, an explicit `(double)` cast, a
transcendental call, or an accuracy-sensitive one, and it never rewrites inside
a comment or a string. A change like this still has to be proven: rebuild every
test and compare against the recorded baseline output, because the host and the
PSP build come from the same source and so agree with each other whatever this
script does to the numbers.

It is not a formatter: it rewrites only what it can classify, reports per file,
and DRY=1 prints the plan without writing.
"""
import re
import os
import sys
import pathlib

# A decimal literal with a fractional part and no suffix yet.
NUM = re.compile(r'(?<![\w.])(\d*\.\d+(?:[eE][-+]?\d+)?)(?![\w.])')
# The same, already float-suffixed - used to keep the pass idempotent.
NUMF = re.compile(r'(?<![\w.])(\d*\.\d+(?:[eE][-+]?\d+)?)[fF](?![\w.])')

# A call whose double version is more accurate than its float version is not a
# free win: sinf/cosf are cheaper and bit-different. Refuse to promote those.
TRANSCENDENTAL = {"pow", "exp", "log", "log2", "log10", "sin", "cos", "tan",
                  "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
                  "hypot", "cbrt", "exp2", "expm1", "log1p", "fmod", "frexp",
                  "modf", "ldexp", "nextafter"}
# These differ in accuracy class between float and double in glibc/newlib too.
ACCURACY_SENSITIVE = {"sqrt", "sqrtf", "fmin", "fmax", "fabs"}


def strip_for_scan(line):
    """Blank out comments and string contents so a literal inside them is never
    mistaken for code, while keeping the character count so columns still line
    up with the original."""
    out = []
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if c == '/' and i + 1 < n and line[i + 1] == '/':
            out.append(' ' * (n - i))
            break
        if c == '"':
            j = i + 1
            while j < n and line[j] != '"':
                j += 2 if line[j] == '\\' else 1
            out.append('"' + ' ' * (min(j, n - 1) - i) + ('"' if j < n else ''))
            i = j + 1
            continue
        if c == "'":
            j = i + 1
            while j < n and line[j] != "'":
                j += 2 if line[j] == '\\' else 1
            out.append("'" + ' ' * (min(j, n - 1) - i) + ("'" if j < n else ''))
            i = j + 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def double_decls(text):
    """Names and members the file declares as double, plus its #include lines.
    A line that mentions one of these - or includes a header we have not vetted
    for double macros - is left alone."""
    names = set()
    for m in re.finditer(r'\bdouble\s+([A-Za-z_]\w*)', text):
        names.add(m.group(1))
    for m in re.finditer(r'\bdouble\s+[* ]*([A-Za-z_]\w*)\s*(?:\(|,|;|=|\[)', text):
        names.add(m.group(1))
    includes = re.findall(r'#\s*include\s*[<"]([^">]+)[>"]', text)
    return names, includes


def transform(path):
    text = path.read_text()
    names, includes = double_decls(text)
    lines = text.split('\n')
    out, changed, skipped = [], 0, []
    in_block_comment = False

    for no, line in enumerate(lines, 1):
        scan = strip_for_scan(line)
        if in_block_comment:
            out.append(line)
            if '*/' in scan:
                in_block_comment = False
            continue
        if '/*' in scan:
            in_block_comment = '*/' not in scan
            out.append(line)
            continue
        if '#' in scan and not NUM.search(scan):
            out.append(line)
            continue

        reasons = []
        if re.search(r'\bdouble\b', scan):
            reasons.append('declares double')
        if any(re.search(r'(?<!\w)' + re.escape(n) + r'(?!\w)', scan) for n in names):
            reasons.append('uses a double variable')
        if any(re.search(r'(?<!\w)' + re.escape(h.split('/')[-1].split('.')[0]) + r'(?!\w)', scan)
               for h in includes):
            reasons.append('mentions an included header')
        if re.search(r'\(\s*double\s*\)', scan):
            reasons.append('explicit double cast')
        if any(re.search(r'(?<!\w)' + f + r'\s*\(', scan) for f in TRANSCENDENTAL):
            reasons.append('transcendental call')
        if any(re.search(r'(?<!\w)' + f + r'\s*\(', scan) for f in ACCURACY_SENSITIVE):
            reasons.append('accuracy-sensitive call')

        if reasons or not NUM.search(scan):
            out.append(line)
            if reasons and NUM.search(scan):
                skipped.append((no, ','.join(reasons), line.strip()[:70]))
            continue

        # Rebuild from the original so column alignment survives: rewrite only
        # the literal spans, using the scan to know which spans are code.
        pieces, pos = [], 0
        for m in NUM.finditer(scan):
            pieces.append(line[pos:m.start()])
            pieces.append(m.group(1) + 'f')
            pos = m.end()
        pieces.append(line[pos:])
        new = ''.join(pieces)
        if new != line:
            changed += 1
        out.append(new)

    return '\n'.join(out), changed, skipped, len(names)


def main():
    paths = [pathlib.Path(p) for p in sys.argv[1:]]
    dry = os.environ.get("DRY") == "1"
    total = 0
    for p in paths:
        new, changed, skipped, nd = transform(p)
        if changed and not dry:
            p.write_text(new)
        total += changed
        print(f'{p.name:20s} suffixed={changed:4d} left-alone={len(skipped):4d} '
              f'double-names={nd}' + (' (dry run, nothing written)' if dry else ''))
        for no, why, txt in skipped:
            print(f'    {p.name}:{no}: not suffixed ({why}): {txt}')
    print(f'total literals suffixed: {total}')


if __name__ == '__main__':
    main()
