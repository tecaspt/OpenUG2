#!/usr/bin/env python3
"""Say *how* two PNG screenshots differ, not just whether they do.

The parity gates used to compare game screenshots with `cmp`, which answers
"identical" or "different" and nothing else. This tool decodes both shots,
counts the differing pixels, reports where they are (bounding box plus a coarse
grid) and how much they moved (worst and mean per-pixel channel delta), and can
write a diff image: unchanged pixels as a dim grey version of A, differing
pixels as red.

PNG encoding and decoding are implemented here from the PNG specification with
the standard library only, so the tool runs anywhere the gates run. Supported
inputs are non-interlaced 8-bit greyscale, greyscale+alpha, RGB and RGBA.

Usage:
    python3 -I tools/png_diff.py A.png B.png [--tolerance N] [--grid CxR]
                                 [--out DIFF.png] [--json OUT.json]

Exit codes: 0 identical within tolerance, 1 different (or different sizes),
2 usage or decode error.
"""

import argparse
import json
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_CHUNK = 2 ** 31 - 1
RED = bytes((255, 0, 0))
# colour type -> (bytes per source pixel, channels after decoding)
COLOR_TYPES = {0: (1, 3), 2: (3, 3), 4: (2, 4), 6: (4, 4)}


class PngError(Exception):
    """Raised for anything this tool cannot turn into pixels."""


class Image:
    """Decoded pixels: rows[y] holds width * channels bytes, RGB or RGBA."""

    def __init__(self, width, height, channels, rows):
        self.width = width
        self.height = height
        self.channels = channels
        self.rows = rows


def chunk(kind, payload):
    """One length/type/crc-wrapped PNG chunk."""
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def encode_png_rgb(width, height, rows):
    """8-bit RGB PNG: filter type 0 on every row, a single IDAT."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw += row
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        SIGNATURE
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + chunk(b"IEND", b"")
    )


def write_png_rgb(path, width, height, rows):
    with open(path, "wb") as handle:
        handle.write(encode_png_rgb(width, height, rows))


def _read_ihdr(payload, name):
    if len(payload) != 13:
        raise PngError("%s: IHDR is %d bytes, expected 13" % (name, len(payload)))
    width, height, depth, color, compression, filter_method, interlace = struct.unpack(
        ">IIBBBBB", payload
    )
    if width == 0 or height == 0:
        raise PngError("%s: empty %dx%d image" % (name, width, height))
    if compression != 0:
        raise PngError("%s: compression method %d is not deflate" % (name, compression))
    if filter_method != 0:
        raise PngError("%s: filter method %d is unknown" % (name, filter_method))
    if interlace != 0:
        raise PngError("%s: interlaced (Adam7) PNGs are not supported" % name)
    if depth != 8:
        raise PngError("%s: %d-bit samples are not supported, need 8-bit" % (name, depth))
    if color not in COLOR_TYPES:
        if color == 3:
            what = "palette (colour type 3)"
        elif color == 1:
            what = "greyscale with reserved spare bits (colour type 1)"
        else:
            what = "colour type %d" % color
        raise PngError(
            "%s: %s is not supported, need 8-bit RGB, RGBA, greyscale or grey+alpha"
            % (name, what)
        )
    return width, height, color


def _unfilter(raw, height, stride, bpp, name):
    """Undo the per-row PNG filters (None, Sub, Up, Average, Paeth)."""
    plane = bytearray(stride * height)
    prev = bytes(stride)  # the implied all-zero row above the first row
    pos = 0
    for y in range(height):
        kind = raw[pos]
        pos += 1
        cur = bytearray(raw[pos : pos + stride])
        pos += stride
        if kind == 0:
            pass
        elif kind == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                up = prev[i]
                upleft = prev[i - bpp] if i >= bpp else 0
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                if pa <= pb and pa <= pc:
                    near = left
                elif pb <= pc:
                    near = up
                else:
                    near = upleft
                cur[i] = (cur[i] + near) & 0xFF
        else:
            raise PngError(
                "%s: row %d uses filter type %d, which is not one of the five PNG filter types"
                % (name, y, kind)
            )
        plane[y * stride : (y + 1) * stride] = cur
        prev = cur
    return plane


def decode_png(data, name="<png>"):
    """Turn PNG bytes into an Image, checking the CRC of every chunk."""
    if not data.startswith(SIGNATURE):
        raise PngError("%s: not a PNG (missing signature)" % name)
    header = None
    idat = bytearray()
    done = False
    pos = len(SIGNATURE)
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        if length > MAX_CHUNK:
            raise PngError("%s: %s chunk claims %d bytes" % (name, kind, length))
        body = pos + 8
        end = body + length
        if end + 4 > len(data):
            raise PngError("%s: %s chunk is truncated" % (name, kind))
        payload = data[body:end]
        stored = struct.unpack(">I", data[end : end + 4])[0]
        if stored != zlib.crc32(kind + payload) & 0xFFFFFFFF:
            raise PngError("%s: bad CRC in the %s chunk at offset %d" % (name, kind, body))
        if kind == b"IHDR":
            if header is not None:
                raise PngError("%s: a second IHDR chunk" % name)
            header = _read_ihdr(payload, name)
        elif kind == b"IDAT":
            if header is None:
                raise PngError("%s: IDAT before IHDR" % name)
            idat += payload
        elif kind == b"IEND":
            done = True
            break  # anything after IEND is not part of the image
        pos = end + 4
    if header is None:
        raise PngError("%s: no IHDR chunk" % name)
    if not done:
        raise PngError("%s: the file ends before IEND" % name)
    if not idat:
        raise PngError("%s: no IDAT chunk, so there is no image data" % name)

    width, height, color = header
    src_bpp, channels = COLOR_TYPES[color]
    stride = width * src_bpp
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise PngError("%s: the IDAT stream is corrupt (%s)" % (name, exc)) from None
    if len(raw) != height * (stride + 1):
        raise PngError(
            "%s: image data is %d bytes, expected %d for %dx%d"
            % (name, len(raw), height * (stride + 1), width, height)
        )
    plane = _unfilter(raw, height, stride, src_bpp, name)

    rows = []
    for y in range(height):
        line = plane[y * stride : (y + 1) * stride]
        if color in (2, 6):
            rows.append(line)
        elif color == 0:  # grey -> RGB
            rgb = bytearray(3 * width)
            rgb[0::3] = line
            rgb[1::3] = line
            rgb[2::3] = line
            rows.append(rgb)
        else:  # color == 4, grey+alpha -> RGBA
            grey, alpha = line[0::2], line[1::2]
            rgba = bytearray(4 * width)
            rgba[0::4] = grey
            rgba[1::4] = grey
            rgba[2::4] = grey
            rgba[3::4] = alpha
            rows.append(rgba)
    return Image(width, height, channels, rows)


def read_png(path):
    with open(path, "rb") as handle:
        return decode_png(handle.read(), path)


class Diff:
    """The numbers one comparison produced."""

    def __init__(self, differing, percent, max_delta, mean_delta, bbox, grid):
        self.differing = differing
        self.percent = percent
        self.max_delta = max_delta
        self.mean_delta = mean_delta
        self.bbox = bbox  # (x0, x1, y0, y1) inclusive, or None
        self.grid = grid  # rows of cell percentages, or None


def split_evenly(total, parts):
    """Edges of `parts` cells over `total` pixels, each pixel in exactly one cell."""
    return [(i * total // parts, (i + 1) * total // parts) for i in range(parts)]


def dim_grey_pixels(row, channels, width):
    """One diff-image row of unchanged pixels: A as grey, a third as bright."""
    out = bytearray(width * 3)
    at = 0
    for x in range(width):
        i = x * channels
        # Grey is the plain mean of the three colour channels, then dimmed.
        value = (row[i] + row[i + 1] + row[i + 2]) // 3 // 3
        out[at] = value
        out[at + 1] = value
        out[at + 2] = value
        at += 3
    return out


def compare(a, b, tolerance=0, cols=4, rows=3, want_diff_image=False):
    """Compare two same-size images. Returns (Diff, diff-image rows or None)."""
    width, height = a.width, a.height
    total = width * height
    # Alpha only counts when both images have it; greyscale arrived as RGB.
    channels = 4 if (a.channels == 4 and b.channels == 4) else 3
    fast = channels == a.channels == b.channels
    cell_of_x = [x * cols // width for x in range(width)]

    differing = 0
    delta_sum = 0
    max_delta = 0
    x0 = y0 = None
    x1 = y1 = None
    counts = [[0] * cols for _ in range(rows)]
    diff_rows = [] if want_diff_image else None

    for y in range(height):
        ra, rb = a.rows[y], b.rows[y]
        head = width * channels
        if (ra == rb) if fast else (ra[:head] == rb[:head]):
            if want_diff_image:
                diff_rows.append(dim_grey_pixels(ra, a.channels, width))
            continue
        out = dim_grey_pixels(ra, a.channels, width) if want_diff_image else None
        cell_row = y * rows // height
        for x in range(width):
            i, j = x * a.channels, x * b.channels
            delta = 0
            for c in range(channels):
                step = ra[i + c] - rb[j + c]
                if step < 0:
                    step = -step
                if step > delta:
                    delta = step
            if delta == 0:
                continue
            delta_sum += delta
            if delta > max_delta:
                max_delta = delta
            if delta > tolerance:
                differing += 1
                counts[cell_row][cell_of_x[x]] += 1
                if x0 is None:
                    x0, x1, y0, y1 = x, x, y, y
                else:
                    if x < x0:
                        x0 = x
                    elif x > x1:
                        x1 = x
                    if y < y0:
                        y0 = y
                    elif y > y1:
                        y1 = y
                if out is not None:
                    out[3 * x : 3 * x + 3] = RED
        if out is not None:
            diff_rows.append(out)

    bbox = None if x0 is None else (x0, x1, y0, y1)
    grid = None
    if differing:
        grid = []
        for r, (y_lo, y_hi) in enumerate(split_evenly(height, rows)):
            line = []
            for c, (x_lo, x_hi) in enumerate(split_evenly(width, cols)):
                area = (x_hi - x_lo) * (y_hi - y_lo)
                line.append(0.0 if area == 0 else 100.0 * counts[r][c] / area)
            grid.append(line)
    result = Diff(
        differing,
        100.0 * differing / total,
        max_delta,
        delta_sum / total,
        bbox,
        grid,
    )
    return result, diff_rows


def parse_grid(text):
    """Parse `CxR` (columns x rows) for --grid."""
    parts = text.lower().split("x")
    try:
        cols, rows = (int(part) for part in parts)
    except ValueError:
        raise ValueError("--grid must look like CxR, for example 4x3") from None
    if len(parts) != 2 or cols < 1 or rows < 1:
        raise ValueError("--grid must be two positive integers like 4x3, not %r" % text)
    return cols, rows


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="png_diff.py",
        description="Report how two PNG screenshots differ.",
        epilog="exit codes: 0 identical within tolerance, 1 different, "
        "2 usage or decode error",
    )
    parser.add_argument("a", help="the first screenshot")
    parser.add_argument("b", help="the second screenshot")
    parser.add_argument("--tolerance", type=int, default=0, help="largest channel delta that still counts as equal (0)")
    parser.add_argument("--grid", default="4x3", help="columnsxrows cells to break the difference down over (4x3)")
    parser.add_argument("--out", help="write an 8-bit RGB diff image here (grey = unchanged, red = differing)")
    parser.add_argument("--json", dest="json_path", help="write the same numbers as one JSON object here")
    args = parser.parse_args(argv)

    if args.tolerance < 0:
        parser.error("--tolerance must not be negative")
    try:
        cols, rows = parse_grid(args.grid)
    except ValueError as exc:
        parser.error(str(exc))

    try:
        image_a = read_png(args.a)
        image_b = read_png(args.b)
    except OSError as exc:
        print("png_diff: %s" % exc, file=sys.stderr)
        return 2
    except PngError as exc:
        print("png_diff: %s" % exc, file=sys.stderr)
        return 2

    if (image_a.width, image_a.height) != (image_b.width, image_b.height):
        print(
            "sizes differ: %s is %dx%d, %s is %dx%d"
            % (
                args.a,
                image_a.width,
                image_a.height,
                args.b,
                image_b.width,
                image_b.height,
            )
        )
        return 1

    result, diff_rows = compare(
        image_a, image_b, args.tolerance, cols, rows, want_diff_image=args.out is not None
    )

    if result.differing == 0:
        print("identical (tolerance %d)" % args.tolerance)
    else:
        print(
            "size %dx%d  differing %d px (%.2f%%)  max delta %d  mean delta %.1f"
            % (
                image_a.width,
                image_a.height,
                result.differing,
                result.percent,
                result.max_delta,
                result.mean_delta,
            )
        )
        print("bbox x %d..%d y %d..%d" % result.bbox)
        print("grid %dx%d (percent differing per cell, row by row):" % (cols, rows))
        for line in result.grid:
            print("  " + "  ".join("%.1f" % value for value in line))

    if args.out:
        try:
            write_png_rgb(args.out, image_a.width, image_a.height, diff_rows)
        except OSError as exc:
            print("png_diff: %s" % exc, file=sys.stderr)
            return 2
    if args.json_path:
        bbox = result.bbox
        report = {
            "width": image_a.width,
            "height": image_a.height,
            "differing": result.differing,
            "percent": round(result.percent, 2),
            "max_delta": result.max_delta,
            "mean_delta": round(result.mean_delta, 3),
            # null when identical; otherwise inclusive pixel bounds.
            "bbox": None if bbox is None else {"x0": bbox[0], "x1": bbox[1], "y0": bbox[2], "y1": bbox[3]},
            # null when identical; otherwise `rows` rows of `cols` percentages.
            "grid": None if result.grid is None else [[round(v, 1) for v in line] for line in result.grid],
        }
        try:
            with open(args.json_path, "w") as handle:
                json.dump(report, handle, indent=1, sort_keys=True)
                handle.write("\n")
        except OSError as exc:
            print("png_diff: %s" % exc, file=sys.stderr)
            return 2

    return 1 if result.differing else 0


if __name__ == "__main__":
    sys.exit(main())
