#!/usr/bin/env python3
"""Draw the OpenUG2 app icon (original procedural art) as a 1024x1024 opaque RGB PNG.

Usage: make_icon.py OUT.png     Standard library only.
"""
import struct
import sys
import zlib

N = 1024


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return tuple(lerp(c1[i], c2[i], t) for i in range(3))


def pixel(x, y):
    hy = 560.0  # horizon
    if y < hy:
        col = mix((14, 10, 40), (150, 40, 110), (y / hy) ** 1.6)
        # setting sun
        dx, dy = x - 512, y - 470
        d = (dx * dx + dy * dy) ** 0.5
        if d < 190:
            col = mix((255, 210, 70), (255, 80, 120), (y - 280) / 380.0)
            if y > 440 and int((y - 440) / 18) % 2 == 1 and (y - 440) % 18 < (y - 440) / 12:
                col = mix((14, 10, 40), (150, 40, 110), (y / hy) ** 1.6)
        elif d < 260:
            col = mix(col, (255, 120, 140), (260 - d) / 70.0 * 0.5)
        return col
    t = (y - hy) / (N - hy)  # 0 at horizon, 1 at bottom
    half = lerp(14, 600, t)
    off = abs(x - 512)
    ground = mix((30, 14, 50), (12, 8, 24), t)
    if off < half:
        col = mix((46, 46, 66), (24, 24, 38), t)
        # road edge lines
        if off > half * 0.93:
            col = (235, 235, 245)
        # dashed centre line, spaced in perspective
        elif off < half * 0.035 + 1:
            z = 1.0 / (t + 0.04)
            if int(z * 3.2) % 2 == 0:
                col = (255, 196, 40)
        return col
    # neon glow beside the road
    g = max(0.0, 1.0 - (off - half) / 70.0)
    return mix(ground, (0, 200, 255), g * 0.55)


def main():
    out = sys.argv[1]
    raw = bytearray()
    for y in range(N):
        raw.append(0)
        for x in range(N):
            r, g, b = pixel(x, y)
            raw += bytes((int(r), int(g), int(b)))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 2, 0, 0, 0))  # 8-bit RGB, no alpha
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(out, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    main()
