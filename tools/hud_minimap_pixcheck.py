#!/usr/bin/env python3
"""M9 minimap icon pixel check (stdlib only; run with python3 -I).

Usage: hud_minimap_pixcheck.py OUTPUT_DIR

Reads hud-minimap-icons.json written by `hud_asset_probe --icons` and the
PNGs beside it, then checks for every expected icon that pixels in its legend
tint exist around the expected point (only pixels the icons changed versus
an icon-free background frame count), that their bounding-box centre lies
within TOL px of it, that a clamped icon sits on the ring (radius +-TOL px
from the map centre), that the rotated rival is wider than tall, and that the
frame with the fail-closed icon equals the frame without it. Retail pixels
are never stored: only the probe's own output is read.
"""
import json
import math
import os
import struct
import sys
import zlib

TOL = 2.0          # px, centre and ring radius
COLOUR_TOL = 12    # per channel, tint x white art (linear filtering at edges)
SEARCH = 24        # px half-window around the expected point
MIN_PIXELS = 6


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    at, idat, w = 8, b"", None
    while at < len(data):
        n, = struct.unpack(">I", data[at:at + 4])
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + n]
        at += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(path + ": unsupported PNG layout")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 255
            elif ftype == 2:
                line[x] = (line[x] + b) & 255
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        for x in range(w):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = line[x * bpp:x * bpp + 3]
        prev = line
    return w, h, bytes(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2])
        return 2
    d = sys.argv[1]
    with open(os.path.join(d, "hud-minimap-icons.json")) as f:
        spec = json.load(f)
    w, h, px = read_png(os.path.join(d, spec["image"]))
    if (w, h) != (spec["width"], spec["height"]):
        print("FAIL size", w, h)
        return 1
    _, _, bg = read_png(os.path.join(d, spec["background_image"]))
    ok = True
    cx, cy = spec["centre"]
    ring = spec["ring"]
    for icon in spec["icons"]:
        ex, ey = icon["x"], icon["y"]
        r0, g0, b0 = icon["rgb"]
        xs, ys = [], []
        for y in range(max(0, int(ey) - SEARCH), min(h, int(ey) + SEARCH + 1)):
            for x in range(max(0, int(ex) - SEARCH), min(w, int(ex) + SEARCH + 1)):
                i = (y * w + x) * 3
                if px[i:i + 3] == bg[i:i + 3]:
                    continue  # unchanged by the icons: map/background, not icon art
                if (abs(px[i] - r0) <= COLOUR_TOL and abs(px[i + 1] - g0) <= COLOUR_TOL
                        and abs(px[i + 2] - b0) <= COLOUR_TOL):
                    xs.append(x)
                    ys.append(y)
        if len(xs) < MIN_PIXELS:
            print("FAIL %-24s tint (%d,%d,%d) found at %d px" % (icon["name"], r0, g0, b0, len(xs)))
            ok = False
            continue
        bx = (min(xs) + max(xs) + 1) / 2.0
        by = (min(ys) + max(ys) + 1) / 2.0
        err = math.hypot(bx - ex, by - ey)
        line = "%-24s tint (%3d,%3d,%3d) %4d px  centre (%.1f,%.1f) expected (%.1f,%.1f) err %.2f" % (
            icon["name"], r0, g0, b0, len(xs), bx, by, ex, ey, err)
        good = err <= TOL
        if icon["clamped"]:
            rad = math.hypot(bx - cx, by - cy)
            line += "  ring %.2f (expect %.2f)" % (rad, ring)
            good = good and abs(rad - ring) <= TOL
        if icon["rotated"]:
            bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
            line += "  bbox %dx%d" % (bw, bh)
            good = good and bw > bh
        print(("PASS " if good else "FAIL ") + line)
        ok = ok and good
    if not spec.get("failclosed_identical"):
        print("FAIL fail-closed icon changed the frame (probe report)")
        ok = False
    else:
        _, _, closed = read_png(os.path.join(d, spec["closed_image"]))
        same = closed == px
        print(("PASS" if same else "FAIL") + " fail-closed icon draws nothing (frames identical)")
        ok = ok and same
    print("hud_minimap_pixcheck: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
