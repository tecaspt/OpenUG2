#!/usr/bin/env python3
"""Dumps U2PK batch transforms + the raw vertex range they map into.

The float-vertex render path applies, per batch, T(pos_offset) . S(pos_scale) to
an i16 vertex grid. If a batch's scale or offset is absurd the geometry is
distorted regardless of the GPU, so this separates a converter/data bug from a
hardware render bug without needing a console.

usage: u2pk_batches.py <file.u2pk>
"""
import struct
import sys

KIND = {1: "CAR", 2: "REGION", 3: "CELL"}
SECT = {1: "BATCHES", 2: "VERTS", 3: "INDICES", 4: "TEXTURES", 5: "CLUTS",
        6: "CARINFO", 7: "COLLIDE", 8: "CELLTAB"}


def main():
    path = sys.argv[1]
    blob = open(path, "rb").read()
    magic, ver, kind, nsec, total, flags = struct.unpack_from("<4sHHIII", blob, 0)
    if magic != b"U2PK":
        sys.exit("not a U2PK file")
    print("%s kind=%s ver=%d sections=%d size=%d"
          % (path, KIND.get(kind, kind), ver, nsec, total))

    secs = {}
    for i in range(nsec):
        t, off, size, count = struct.unpack_from("<IIII", blob, 32 + i * 16)
        secs.setdefault(t, (off, size, count))
        print("  sect %-8s off=%-9d size=%-8d count=%d"
              % (SECT.get(t, t), off, size, count))

    if 1 not in secs or 2 not in secs:
        sys.exit("no BATCHES/VERTS section")
    boff, bsz, bcount = secs[1]
    voff, vsz, vcount = secs[2]
    print("\nbatches=%d verts=%d (vert stride expected 12: u16 uv[2] + i16 xyz[3])"
          % (bcount, vcount))

    # On-disk vertex: u16 u, u16 v, u16 color, i16 x, i16 y, i16 z (12 bytes).
    xs, ys, zs = [], [], []
    for v in range(vcount):
        u, vv, col, x, y, z = struct.unpack_from("<6h", blob, voff + v * 12)
        xs.append(x); ys.append(y); zs.append(z)
    def rng(a):
        return (min(a), max(a)) if a else (0, 0)
    print("raw i16 pos range x=%s y=%s z=%s" % (rng(xs), rng(ys), rng(zs)))

    print("\nper-batch transform (final metres = i16*scale + offset):")
    worst = None
    for b in range(bcount):
        (fv, vc, fi, ic, tex, mode,
         sx, sy, sz, ox, oy, oz, usx, usy, uox, uoy) = \
            struct.unpack_from("<IIIIiI10f", blob, boff + b * 64)
        bx = xs[fv:fv + vc]; by = ys[fv:fv + vc]; bz = zs[fv:fv + vc]
        # metres = raw_i16/32768 * pos_scale + pos_offset (the /32768 the runtime
        # applies in CPU code, car.c build_float_verts).
        Q = 32768.0
        mx = [v / Q * s + o for v, s, o in
              ((min(bx), sx, ox), (max(bx), sx, ox))]
        my = [v / Q * s + o for v, s, o in
              ((min(by), sy, oy), (max(by), sy, oy))]
        mz = [v / Q * s + o for v, s, o in
              ((min(bz), sz, oz), (max(bz), sz, oz))]
        span = (max(mx) - min(mx), max(my) - min(my), max(mz) - min(mz))
        big = max(span)
        flag = ""
        if big > 8.0:                       # no car part should span > 8 m
            flag = "  <== SUSPECT"
        if worst is None or big > worst[1]:
            worst = (b, big, span)
        print("  b%-2d v%5d n%4d i%5d m%4d mode=%d tex=%-3d "
              "scale=(%.4f %.4f %.4f) off=(%.3f %.3f %.3f) "
              "extent=(%.2f %.2f %.2f)%s"
              % (b, fv, vc, fi, ic, mode, tex, sx, sy, sz, ox, oy, oz,
                 span[0], span[1], span[2], flag))
    if worst:
        print("\nlargest batch extent: batch %d = %.2f m (span %s)"
              % (worst[0], worst[1], tuple(round(x, 2) for x in worst[2])))


main()
