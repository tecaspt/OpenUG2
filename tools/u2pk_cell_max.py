#!/usr/bin/env python3
"""Print the largest cell-blob size referenced by an M4 index.u2pk CELLTAB.

index.u2pk layout (shared/u2pk.h, region_convert.c): a 32-byte U2PK header,
then section_count 16-byte sections {u32 type, u32 offset, u32 size, u32 count}.
The CELLTAB section (type 8) holds `count` 24-byte entries laid out as
    {i32 cx, i32 cy, u32 cell_offset, u32 cell_size, f32 zmin, f32 zmax}
An empty cell has cell_size 0. Prints the maximum non-zero cell_size, which the
M4 budget caps at 262144 bytes (256 KB). Exits 2 on a malformed file.
"""
import struct
import sys

CELLTAB = 8


def main(path):
    with open(path, "rb") as f:
        d = f.read()
    if len(d) < 32 or d[:4] != b"U2PK":
        sys.exit(2)
    _, _, kind, scount, total, _flags = struct.unpack_from("<4sHHIII", d, 0)
    maxsz = 0
    n = 0
    for i in range(scount):
        base = 32 + i * 16
        if base + 16 > len(d):
            sys.exit(2)
        typ, off, size, count = struct.unpack_from("<IIII", d, base)
        if typ != CELLTAB:
            continue
        for e in range(count):
            eb = off + e * 24
            if eb + 24 > len(d):
                sys.exit(2)
            _cx, _cy, coff, csz = struct.unpack_from("<iiII", d, eb)
            if csz:
                n += 1
                if csz > maxsz:
                    maxsz = csz
    print(maxsz)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: u2pk_cell_max.py <index.u2pk>")
    main(sys.argv[1])
