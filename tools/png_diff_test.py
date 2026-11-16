#!/usr/bin/env python3
"""Tests for tools/png_diff.py, on synthetic images only.

Rows are filtered here with a second implementation of the PNG filters (the
encoding direction, straight from the specification) so that the decoder is
checked against bytes the tool's own encoder never produces.

Run: python3 -I tools/png_diff_test.py -v
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png_diff  # noqa: E402

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "png_diff.py")
BPP = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def filter_row(kind, raw, prev, bpp):
    """One row of filtered bytes, per the PNG filter definitions."""
    out = bytearray(len(raw))
    for i in range(len(raw)):
        left = raw[i - bpp] if i >= bpp else 0
        up = prev[i]
        upleft = prev[i - bpp] if i >= bpp else 0
        if kind == 0:
            predicted = 0
        elif kind == 1:
            predicted = left
        elif kind == 2:
            predicted = up
        elif kind == 3:
            predicted = (left + up) >> 1
        elif kind == 4:
            p = left + up - upleft
            pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
            if pa <= pb and pa <= pc:
                predicted = left
            elif pb <= pc:
                predicted = up
            else:
                predicted = upleft
        else:
            raise AssertionError("no such filter %r" % (kind,))
        out[i] = (raw[i] - predicted) & 0xFF
    return out


def chunk(kind, payload, bad_crc=False):
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    if bad_crc:
        crc ^= 0x000000FF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def png_bytes(width, height, rows, color_type=2, filters=0, bit_depth=8, interlace=0,
              palette=None, bad_crc_chunk=None, idat_size=None):
    """Assemble a PNG from unfiltered pixel rows, filtering them here."""
    if isinstance(filters, int):
        filters = [filters] * height
    bpp = BPP[color_type]
    raw = bytearray()
    prev = bytes(width * bpp)
    for y, row in enumerate(rows):
        row = bytes(row)
        assert len(row) == width * bpp, (len(row), width, bpp)
        raw.append(filters[y])
        raw += filter_row(filters[y], row, prev, bpp)
        prev = row
    payload = zlib.compress(bytes(raw))
    out = bytearray(png_diff.SIGNATURE)
    out += chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace),
        bad_crc_chunk == "IHDR",
    )
    if palette is not None:
        out += chunk(b"PLTE", palette)
    if idat_size is None:
        pieces = [payload]
    else:
        pieces = [payload[i : i + idat_size] for i in range(0, len(payload), idat_size)] or [b""]
    for piece in pieces:
        out += chunk(b"IDAT", piece, bad_crc_chunk == "IDAT")
    out += chunk(b"IEND", b"")
    return bytes(out)


def rgb(w, h, value_at):
    """Rows of an RGB image whose pixel (x, y) is value_at(x, y)."""
    return [
        bytearray(b"".join(bytes(value_at(x, y)) for x in range(w)))
        for y in range(h)
    ]


class DecodeTest(unittest.TestCase):
    def pixels(self, width, height):
        # Values chosen to move through every Paeth branch, including wraps.
        return rgb(
            width,
            height,
            lambda x, y: ((x * 47 + y * 13) % 256, (x * 9 - y * 5) % 256, (200 - x * 29) % 256),
        )

    def test_each_filter_type_decodes(self):
        rows = self.pixels(6, 4)
        for kind in range(5):
            data = png_bytes(6, 4, rows, filters=kind)
            image = png_diff.decode_png(data, "filter%d" % kind)
            self.assertEqual((image.width, image.height, image.channels), (6, 4, 3))
            self.assertEqual([bytes(r) for r in image.rows], [bytes(r) for r in rows],
                             "filter type %d decoded wrong" % kind)

    def test_mixed_filters_in_one_image_decode(self):
        rows = self.pixels(5, 5)
        image = png_diff.decode_png(png_bytes(5, 5, rows, filters=[0, 1, 2, 3, 4]), "mixed")
        self.assertEqual([bytes(r) for r in image.rows], [bytes(r) for r in rows])

    def test_grey_and_grey_alpha_expand(self):
        grey = [bytearray((0, 64, 128, 255))]
        image = png_diff.decode_png(png_bytes(4, 1, grey, color_type=0), "grey")
        self.assertEqual(image.channels, 3)
        self.assertEqual(bytes(image.rows[0]), bytes((0, 0, 0, 64, 64, 64, 128, 128, 128, 255, 255, 255)))

        grey_alpha = [bytearray((10, 200, 20, 0))]
        image = png_diff.decode_png(png_bytes(2, 1, grey_alpha, color_type=4), "grey+alpha")
        self.assertEqual(image.channels, 4)
        self.assertEqual(bytes(image.rows[0]), bytes((10, 10, 10, 200, 20, 20, 20, 0)))

    def test_multiple_idat_chunks_concatenate(self):
        rows = self.pixels(8, 3)
        image = png_diff.decode_png(png_bytes(8, 3, rows, idat_size=7), "many idat")
        self.assertEqual([bytes(r) for r in image.rows], [bytes(r) for r in rows])

    def test_encoder_round_trip(self):
        rows = self.pixels(7, 3)
        image = png_diff.decode_png(png_diff.encode_png_rgb(7, 3, rows), "own encoder")
        self.assertEqual([bytes(r) for r in image.rows], [bytes(r) for r in rows])

    def test_bad_crc_is_refused(self):
        rows = self.pixels(3, 2)
        for kind in ("IHDR", "IDAT"):
            with self.assertRaises(png_diff.PngError) as caught:
                png_diff.decode_png(png_bytes(3, 2, rows, bad_crc_chunk=kind), kind)
            self.assertIn("bad CRC", str(caught.exception))

    def test_palette_is_refused(self):
        data = png_bytes(2, 1, [bytearray((0, 1))], color_type=3, palette=bytes((0, 0, 0, 255, 255, 255)))
        with self.assertRaises(png_diff.PngError) as caught:
            png_diff.decode_png(data, "palette")
        self.assertIn("palette", str(caught.exception))

    def test_sixteen_bit_and_interlaced_are_refused(self):
        rows = self.pixels(2, 2)
        with self.assertRaises(png_diff.PngError) as caught:
            png_diff.decode_png(png_bytes(2, 2, rows, bit_depth=16), "16-bit")
        self.assertIn("8-bit", str(caught.exception))
        with self.assertRaises(png_diff.PngError) as caught:
            png_diff.decode_png(png_bytes(2, 2, rows, interlace=1), "adam7")
        self.assertIn("interlaced", str(caught.exception))

    def test_truncated_stream_is_refused(self):
        data = png_bytes(3, 2, self.pixels(3, 2))
        with self.assertRaises(png_diff.PngError):
            png_diff.decode_png(data[: len(data) - 8], "short")


class CliTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name
        self.addCleanup(self._tmp.cleanup)

    def path(self, name):
        return os.path.join(self.dir, name)

    def write(self, name, data):
        path = self.path(name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    def run_tool(self, *args):
        proc = subprocess.run(
            [sys.executable, "-I", TOOL, *[str(a) for a in args]],
            capture_output=True,
            text=True,
        )
        return proc.returncode, proc.stdout, proc.stderr

    def shot(self, name, rows, **kwargs):
        width = kwargs.pop("width")
        height = kwargs.pop("height")
        return self.write(name, png_bytes(width, height, rows, **kwargs))

    def flat(self, name, width, height, colour):
        return self.shot(name, [bytearray(bytes(colour) * width)] * height, width=width, height=height)

    def test_identical_pair_reports_identical(self):
        a = self.flat("a.png", 4, 3, (10, 20, 30))
        b = self.flat("b.png", 4, 3, (10, 20, 30))
        code, out, err = self.run_tool(a, b)
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "identical (tolerance 0)\n")

    def test_one_changed_pixel_counts_bboxes_and_grids(self):
        width, height = 8, 5  # 40 pixels, so one pixel is 2.50%
        other = [bytearray(bytes((4, 8, 15)) * width) for _ in range(height)]
        other[4][5 * 3 : 5 * 3 + 3] = bytes((84, 8, 15))  # pixel (5, 4), delta 80 in red
        a = self.flat("a.png", width, height, (4, 8, 15))
        b = self.shot("b.png", other, width=width, height=height)
        code, out, err = self.run_tool(a, b, "--grid", "4x3")
        self.assertEqual(code, 1, err)
        lines = out.splitlines()
        self.assertEqual(
            lines[0], "size 8x5  differing 1 px (2.50%)  max delta 80  mean delta 2.0"
        )  # 80 over all 40 pixels
        self.assertEqual(lines[1], "bbox x 5..5 y 4..4")
        self.assertEqual(lines[2], "grid 4x3 (percent differing per cell, row by row):")
        # The uneven 5-row split is (0,1) (1,3) (3,5), so that pixel sits in the
        # 2x2 cell at column 2, row 2.
        self.assertEqual(lines[3], "  0.0  0.0  0.0  0.0")
        self.assertEqual(lines[4], "  0.0  0.0  0.0  0.0")
        self.assertEqual(lines[5], "  0.0  0.0  25.0  0.0")

    def test_tolerance_boundary(self):
        a = self.flat("a.png", 2, 1, (100, 100, 100))
        b = self.flat("b.png", 2, 1, (105, 100, 100))  # both pixels +5 on red
        code, out, _ = self.run_tool(a, b, "--tolerance", 5)
        self.assertEqual(code, 0)
        self.assertEqual(out, "identical (tolerance 5)\n")
        code, out, _ = self.run_tool(a, b, "--tolerance", 4)
        self.assertEqual(code, 1)
        self.assertIn("differing 2 px (100.00%)  max delta 5  mean delta 5.0", out)

    def test_mean_delta_averages_over_every_pixel(self):
        a = self.shot("a.png", [bytearray(bytes((0, 0, 0)) * 2)] * 2, width=2, height=2)
        rows = [bytearray(bytes((100, 10, 0)) + bytes((0, 0, 0))), bytearray(bytes((0, 0, 0)) * 2)]
        b = self.shot("b.png", rows, width=2, height=2)
        code, out, _ = self.run_tool(a, b, "--grid", "2x2")
        self.assertEqual(code, 1)
        self.assertIn("differing 1 px (25.00%)  max delta 100  mean delta 25.0", out.splitlines()[0])
        self.assertEqual(out.splitlines()[-2:], ["  100.0  0.0", "  0.0  0.0"])

    def test_rgb_against_rgba_ignores_alpha(self):
        rgb_rows = [bytearray(bytes((1, 2, 3)) * 3)]
        rgba_rows = [bytearray(bytes((1, 2, 3, 0)) * 3)]
        a = self.shot("a.png", rgb_rows, width=3, height=1)
        b = self.shot("b.png", rgba_rows, width=3, height=1, color_type=6)
        code, out, err = self.run_tool(a, b)
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "identical (tolerance 0)\n")

        rgba_rows[0][:3] = bytes((1, 2, 200))
        c = self.shot("c.png", rgba_rows, width=3, height=1, color_type=6)
        code, out, _ = self.run_tool(a, c)
        self.assertEqual(code, 1)
        self.assertIn("differing 1 px (33.33%)  max delta 197", out)

    def test_both_alpha_channels_are_compared(self):
        a = self.shot("a.png", [bytearray(bytes((1, 2, 3, 250)) * 2)], width=2, height=1, color_type=6)
        b = self.shot("b.png", [bytearray(bytes((1, 2, 3, 255)) * 2)], width=2, height=1, color_type=6)
        code, out, _ = self.run_tool(a, b)
        self.assertEqual(code, 1)
        self.assertIn("differing 2 px (100.00%)  max delta 5", out)

    def test_different_sizes_exit_one(self):
        a = self.flat("a.png", 4, 3, (1, 2, 3))
        b = self.flat("b.png", 5, 3, (1, 2, 3))
        out_path = self.path("d.png")
        json_path = self.path("d.json")
        code, out, err = self.run_tool(a, b, "--out", out_path, "--json", json_path)
        self.assertEqual(code, 1, err)
        self.assertEqual(out, "sizes differ: %s is 4x3, %s is 5x3\n" % (a, b))
        self.assertFalse(os.path.exists(out_path))
        self.assertFalse(os.path.exists(json_path))

    def test_decode_errors_exit_two(self):
        rows = [bytearray(bytes((1, 2, 3)) * 3)]
        good = self.shot("good.png", rows, width=3, height=1)
        bad_crc = self.write("badcrc.png", png_bytes(3, 1, rows, bad_crc_chunk="IDAT"))
        palette = self.write(
            "palette.png",
            png_bytes(3, 1, [bytearray((0, 1, 2))], color_type=3, palette=bytes((0, 0, 0, 1, 1, 1))),
        )
        notpng = self.write("notes.png", b"i would prefer not to be a png")
        missing = self.path("nope.png")
        for target in (bad_crc, palette, notpng, missing):
            for other in (good, bad_crc):
                code, out, err = self.run_tool(target, other)
                self.assertEqual(code, 2, "%s vs %s: %s" % (target, other, err))
                self.assertIn("png_diff:", err)
                self.assertEqual(out, "")

    def test_unwritable_outputs_exit_two(self):
        a = self.flat("a.png", 2, 1, (1, 2, 3))
        b = self.flat("b.png", 2, 1, (1, 2, 4))
        for flag in ("--out", "--json"):
            code, out, err = self.run_tool(a, b, flag, self.path("now/here.d"))
            self.assertEqual(code, 2, flag)
            self.assertIn("png_diff:", err)
            self.assertIn("differing 2 px", out)  # the report is printed before writing

    def test_usage_errors_exit_two(self):
        a = self.flat("a.png", 2, 1, (1, 2, 3))
        b = self.flat("b.png", 2, 1, (1, 2, 3))
        for bad in (["--grid", "4"], ["--grid", "0x3"], ["--tolerance", "-1"], ["--grid", "4x3x2"]):
            code, _, err = self.run_tool(a, b, *bad)
            self.assertEqual(code, 2, bad)

    def test_out_image_is_grey_with_red_where_it_changed(self):
        a_rows = [bytearray(bytes((150, 120, 90)) + bytes((150, 120, 90)) + bytes((0, 0, 0)))]
        b_rows = [bytearray(bytes((150, 120, 90)) + bytes((151, 120, 90)) + bytes((0, 0, 0)))]
        a = self.shot("a.png", a_rows, width=3, height=1)
        b = self.shot("b.png", b_rows, width=3, height=1)
        out_path = self.path("diff.png")
        code, _, err = self.run_tool(a, b, "--out", out_path)
        self.assertEqual(code, 1, err)
        with open(out_path, "rb") as handle:
            image = png_diff.decode_png(handle.read(), "diff")
        self.assertEqual((image.width, image.height, image.channels), (3, 1, 3))
        # Unchanged: (150 + 120 + 90) // 3 = 120, a third of that is 40.
        self.assertEqual(bytes(image.rows[0][0:3]), bytes((40, 40, 40)))
        self.assertEqual(bytes(image.rows[0][3:6]), bytes((255, 0, 0)))
        # The black pixel is unchanged and stays black.
        self.assertEqual(bytes(image.rows[0][6:9]), bytes((0, 0, 0)))

    def test_json_object_matches_the_report(self):
        a_rows = [bytearray(bytes((0, 0, 0)) * 4)] * 2
        b_rows = [bytearray(bytes((0, 0, 0)) * 4), bytearray(bytes((0, 0, 0)) * 3 + bytes((20, 0, 0)))]
        a = self.shot("a.png", a_rows, width=4, height=2)
        b = self.shot("b.png", b_rows, width=4, height=2)
        json_path = self.path("d.json")
        code, out, err = self.run_tool(a, b, "--json", json_path, "--grid", "2x1")
        self.assertEqual(code, 1, err)
        with open(json_path) as handle:
            report = json.load(handle)
        self.assertEqual(
            sorted(report),
            ["bbox", "differing", "grid", "height", "max_delta", "mean_delta", "percent", "width"],
        )
        self.assertEqual(
            report,
            {
                "width": 4,
                "height": 2,
                "differing": 1,
                "percent": 12.5,
                "max_delta": 20,
                "mean_delta": 2.5,
                "bbox": {"x0": 3, "x1": 3, "y0": 1, "y1": 1},
                "grid": [[0.0, 25.0]],
            },
        )
        self.assertIn("mean delta 2.5", out)

    def test_json_object_when_identical_has_no_bbox_or_grid(self):
        a = self.flat("a.png", 3, 2, (7, 7, 7))
        b = self.flat("b.png", 3, 2, (7, 7, 7))
        json_path = self.path("d.json")
        code, _, err = self.run_tool(a, b, "--json", json_path)
        self.assertEqual(code, 0, err)
        with open(json_path) as handle:
            report = json.load(handle)
        self.assertEqual(report["differing"], 0)
        self.assertIsNone(report["bbox"])
        self.assertIsNone(report["grid"])


class HelperTest(unittest.TestCase):
    def test_parse_grid(self):
        self.assertEqual(png_diff.parse_grid("4x3"), (4, 3))
        self.assertEqual(png_diff.parse_grid("8X6"), (8, 6))
        for bad in ("4", "0x3", "-1x3", "a x b", "4x3x2", ""):
            with self.assertRaises(ValueError):
                png_diff.parse_grid(bad)


if __name__ == "__main__":
    unittest.main()
