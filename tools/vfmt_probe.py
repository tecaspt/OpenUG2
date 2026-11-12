#!/usr/bin/env python3
"""Reads out/vfmt.ppm and turns the probe quads into U2PK format facts.

Nothing here trusts the geometry or the UV mapping the probe intended: bounding
boxes and repeat counts are measured from the pixels, so a wrong expectation
shows up as a wrong measurement instead of as a missing pixel. The quads are 1 m
wide, centred 4 m from the camera at cx = -3, -1, 1, 3 with a 60 deg vertical
FOV: 58.89 px per metre, centres at x = 63, 181, 299, 417.

Q3's texture deliberately asks for UV 0..65535 with TexScale 1.0. Under
uv = u16/32768 that is 0..2.0, so the texture must tile exactly twice across the
quad - and that doubling is the measurement of the divisor.

usage: vfmt_probe.py [out/vfmt.ppm]
"""
import sys

EXPECT_CX = [63, 181, 299, 417]
PX_PER_M = 58.89
CLEAR = (16, 32, 48)         # gfx_begin()'s clear colour
PRIM = {(248, 0, 0): "R", (0, 248, 0): "G", (0, 0, 248): "B",
        (248, 248, 248): "W", (248, 0, 248): "M", (248, 252, 0): "Y",
        (0, 248, 248): "C", (0, 0, 0): "K", (16, 32, 48): " "}
LABEL = {"R": "red", "G": "green", "B": "blue", "W": "white",
         "M": "magenta (texture row 0)", "Y": "yellow (texture row 15)",
         "C": "cyan", "K": "black", " ": "background", "?": "?"}


def load(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"P6":
            sys.exit("vfmt_probe: not a P6 ppm")
        w, h = (int(x) for x in f.readline().split())
        if f.readline().strip() != b"255":
            sys.exit("vfmt_probe: maxval is not 255")
        data = f.read()
    if len(data) != w * h * 3:
        sys.exit("vfmt_probe: %d bytes of pixels, expected %d" % (len(data), w * h * 3))
    return w, h, data


def runlen(s):
    """[(char, count), ...] for the whole string, last run included."""
    out, prev, n = [], None, 0
    for c in s:
        if c != prev:
            if prev is not None:
                out.append((prev, n))
            prev, n = c, 1
        else:
            n += 1
    if prev is not None:
        out.append((prev, n))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "out/vfmt.ppm"
    w, h, d = load(path)

    def rgb(x, y):
        i = (y * w + x) * 3
        return (d[i], d[i + 1], d[i + 2])

    def letter(x, y):
        p = rgb(x, y)
        best, bd = "?", 999
        for k, v in PRIM.items():
            dd = abs(k[0] - p[0]) + abs(k[1] - p[1]) + abs(k[2] - p[2])
            if dd < bd:
                best, bd = v, dd
        return best if bd < 130 else "?"

    bg = letter(2, 2)
    print("vfmt_probe: %s is %dx%d; background reads as %s" % (path, w, h, LABEL[bg]))
    fails = []

    boxes = []
    print("\n-- quad bounding boxes (measured, positions) --")
    for i, cx in enumerate(EXPECT_CX):
        x0 = x1 = y0 = y1 = None
        for x in range(max(0, cx - 70), min(w, cx + 70)):
            for y in range(h):
                if letter(x, y) not in (" ", "K", "?"):
                    x0 = x if x0 is None else min(x0, x)
                    x1 = x if x1 is None else max(x1, x)
                    y0 = y if y0 is None else min(y0, y)
                    y1 = y if y1 is None else max(y1, y)
        boxes.append((x0, x1, y0, y1) if x0 is not None else None)
        if not boxes[-1]:
            print("  Q%d (expected centre x=%d): NOTHING DRAWN" % (i + 1, cx))
            fails.append("Q%d missing" % (i + 1))
            continue
        a, b, c, e = boxes[-1]
        print("  Q%d %dx%d at x%d..%d y%d..%d, centre (%d,%d) = %.2f m at 4 m"
              % (i + 1, b - a + 1, e - c + 1, a, b, c, e, (a + b) // 2,
                 (c + e) // 2, (b - a + 1) / PX_PER_M))

    print("\n-- GU_COLOR_5650 bit order --")
    if boxes[0] and boxes[1]:
        for i, src in enumerate(("0xF800", "0x07E0")):
            a, b, c, e = boxes[i]
            print("  Q%d vertex colour %s renders as %s"
                  % (i + 1, src, LABEL[letter((a + b) // 2, (c + e) // 2)]))
        a, b, c, e = boxes[0]
        if letter((a + b) // 2, (c + e) // 2) == "B":
            print("  VERDICT bits 11..15 of a 5650 vertex colour are BLUE and bits")
            print("        0..4 are RED (0xF800 is blue, 0x001F is red); bits 5..10")
            print("        are GREEN. Same channel order as the 0xAABBGGRR CLUT, so")
            print("        the converter packs (r>>3) | ((g>>2) << 5) | ((b>>3) << 11).")
        else:
            fails.append("5650 order not the expected R-in-low-bits")

    print("\n-- GU_VERTEX_16BIT divisor (Q1 width) --")
    if boxes[0]:
        wid = boxes[0][1] - boxes[0][0] + 1
        if 54 <= wid <= 64:
            print("  VERDICT %d px for a quad whose local +-0.5 m was scaled by 2.0:"
                  "\n        positions are s16/32768 metres x pos_scale. (s16/16 would\n"
                  "        be 2048x off screen.)" % wid)
        else:
            print("  VERDICT %d px, expected 59 - the divisor or the pos_scale path\n"
                  "        is not what docs/U2PK.md claims" % wid)
            fails.append("position scale wrong")

    print("\n-- GU_TEXTURE_16BIT divisor and origin (Q3, UV 0..65535) --")
    if boxes[2]:
        a, b, c, e = boxes[2]
        col = a + (b - a) // 8          # texel column 4: the white/red half
        seq = "".join(letter(col, y) for y in range(e, c - 1, -1))   # bottom->top
        print("  column at u=0.25, read bottom (v=0) to top (v=2):")
        print("    " + " ".join("%s%d" % (k, n) for k, n in runlen(seq)))
        # A row 1/8 of the way up lands in texture rows 1..7, the white/blue
        # half of each repeat, so W runs there count horizontal repeats.
        row = e - (e - c) // 8
        hseq = "".join(letter(x, row) for x in range(a, b + 1))      # left->right
        print("  row at v=0.125, read left (u=0) to right (u=2):")
        print("    " + " ".join("%s%d" % (k, n) for k, n in runlen(hseq)))
        vert = [k for k, n in runlen(seq) if k in "MY"]
        nrep = sum(1 for k, _ in runlen(seq) if k == "M")
        nhrep = sum(1 for k, _ in runlen(hseq) if k == "W")
        if nrep == 2:
            print("  VERDICT the texture tiles %d times vertically for UV 0..65535,"
                  "\n        i.e. uv = u16/32768 and 32768 is ONE repeat." % nrep)
        else:
            print("  %d marker rows seen, expected 2 - the divisor is not 32768" % nrep)
            fails.append("uv divisor")
        if nhrep == 2:
            print("  VERDICT the same holds horizontally (2 repeats).")
        else:
            print("  %d white runs seen, expected 2" % nhrep)
            fails.append("uv horizontal")
        if vert[:1] == ["M"]:
            print("  VERDICT the first marker row above v=0 is texture ROW 0, so the")
            print("        GE samples v=0 at the first row of the data and the")
            print("        converter must NOT flip rows (this matches the GL build,")
            print("        which uploads the same top-left-origin rows unflipped).")
        elif vert[:1] == ["Y"]:
            print("  VERDICT v=0 lands on the LAST data row - the converter would")
            print("        need to flip every texture. Not what docs/U2PK.md assumes.")
            fails.append("uv row order")
        else:
            fails.append("uv origin unreadable")
        print("  (row order seen bottom->top: %s)" % " ".join(vert))

    print("\n-- 12-byte stride from a 4-byte-aligned vertex base (Q4 vs Q3) --")
    if boxes[2] and boxes[3]:
        w3 = (boxes[2][1] - boxes[2][0], boxes[2][3] - boxes[2][2])
        w4 = (boxes[3][1] - boxes[3][0], boxes[3][3] - boxes[3][2])
        print("  Q3 %dx%d, Q4 %dx%d" % (w3[0] + 1, w3[1] + 1, w4[0] + 1, w4[1] + 1))
        if w3 == w4:
            print("  VERDICT PPSSPP reads the 12-byte stride identically from a 16- and")
            print("        a 4-byte-aligned base. The converter still pads every batch\n"
                  "        run to a 16-byte boundary, because PPSSPP is more forgiving")
            print("        than the GE is documented to be.")
        else:
            print("  VERDICT alignment changes the draw - padding is mandatory")
            fails.append("alignment sensitive")

    print("\nRESULT: %s" % ("; ".join(fails) if fails else "format facts measured"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
