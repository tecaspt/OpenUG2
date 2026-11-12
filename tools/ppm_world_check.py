#!/usr/bin/env python3
"""Grade one M4 world screenshot: does it actually show the ground, and real
texture variety, rather than an empty frame or a flat placeholder?

An M4 pose is a camera 2 m above a road looking along it, so the lower part of
the frame must be drawn ground, not the framebuffer clear colour. Two gates:

  * no missing ground: in the bottom third of the frame the clear colour
    (0xff102030 -> RGB(16,32,48), runtime/gfx.c) covers under half the pixels;
  * real variety: the frame has >= 16 distinct colours (a broken/white pipeline
    collapses to a flat fill).

Prints a one-line description (coverage, colour count, dominant colours) for the
human to record, exits 0 if both gates pass, 1 otherwise, 2 if unreadable.
"""
import sys

CLEAR = (16, 32, 48)
TOL = 8


def load_p6(fn):
    d = open(fn, "rb").read()
    if d[:2] != b"P6":
        raise ValueError("not a binary PPM (P6)")
    i, toks = 2, []
    while len(toks) < 3:
        while d[i] in b" \t\r\n":
            i += 1
        if d[i:i + 1] == b"#":
            while d[i] not in b"\r\n":
                i += 1
            continue
        j = i
        while d[j] not in b" \t\r\n":
            j += 1
        toks.append(d[i:j])
        i = j
    i += 1
    w, h, _mx = int(toks[0]), int(toks[1]), int(toks[2])
    return w, h, d[i:i + w * h * 3]


def near_clear(c):
    return abs(c[0] - CLEAR[0]) <= TOL and abs(c[1] - CLEAR[1]) <= TOL \
        and abs(c[2] - CLEAR[2]) <= TOL


def main(fn):
    try:
        w, h, px = load_p6(fn)
    except Exception as e:            # noqa: BLE001
        sys.stderr.write("%s: %s\n" % (fn, e))
        sys.exit(2)
    cols, bottom_clear, bottom_n = {}, 0, 0
    ysplit = h * 2 // 3
    for y in range(h):
        row = y * w
        for x in range(w):
            k = (row + x) * 3
            c = (px[k], px[k + 1], px[k + 2])
            cols[c] = cols.get(c, 0) + 1
            if y >= ysplit:
                bottom_n += 1
                if near_clear(c):
                    bottom_clear += 1
    n = w * h
    clear_all = sum(v for c, v in cols.items() if near_clear(c))
    distinct = len(cols)
    bc_frac = 100 * bottom_clear // bottom_n
    ca_frac = 100 * clear_all // n
    top = sorted(cols.items(), key=lambda kv: -kv[1])[:4]
    tops = " ".join("%02x%02x%02x:%d%%" % (c[0], c[1], c[2], 100 * v // n)
                    for c, v in top)
    ok = bc_frac < 50 and distinct >= 16
    print("  %s: %dx%d distinct=%d clear%%=%d ground-clear%%=%d [%s] -> %s"
          % (fn.split("/")[-1], w, h, distinct, ca_frac, bc_frac, tops,
             "ground shown" if ok else "MISSING GROUND / flat"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: ppm_world_check.py <screenshot.ppm>")
    main(sys.argv[1])
