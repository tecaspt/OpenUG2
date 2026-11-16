#!/usr/bin/env python3
"""Unit tests for tools/fng_dump.py.

Every input here is synthetic bytes assembled in this file - no retail data, no bundle from
$OPENUG2_DATA - so the tests run anywhere and cannot leak anything. The framing they exercise is
the framing src/hud_asset.c reads: a 0x30203 package chunk holding an "FEn\\xe7" region of
PkHd / Res / Obj chunks, the object bodies holding ObjD field records.

    python3 -I tools/fng_dump_test.py -v
"""

import contextlib
import io
import json
import math
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fng_dump  # noqa: E402


# ---- synthetic builders ------------------------------------------------------------------------

def rec(field, payload):
    """One ObjD record: 2-byte field id, u16 payload size, payload (src/hud_asset.c:17)."""
    return field + struct.pack("<H", len(payload)) + payload


def chunk(tag, payload):
    """A chunk is a 4-byte tag (or u32 id) plus a u32 size and the payload."""
    return tag + struct.pack("<I", len(payload)) + payload


def appearance(rgba=(255, 255, 255, 255), position=(0.0, 0.0, 0.0), size=(1.0, 1.0, 1.0),
               pivot=(0.0, 0.0, 0.0), quaternion=(0.0, 0.0, 0.0, 1.0), uv=(0.0, 0.0, 0.0, 0.0)):
    """An "SA" payload, 84 bytes: colour stored B, G, R, A, then pivot, position, quaternion,
    size and uv (src/hud_asset.c:22)."""
    blue, green, red, alpha = rgba
    return (struct.pack("<4I", blue, green, red, alpha)
            + struct.pack("<3f", *pivot)
            + struct.pack("<3f", *position)
            + struct.pack("<4f", *quaternion)
            + struct.pack("<3f", *size)
            + struct.pack("<4f", *uv))


def obj(object_id, name, kind, sa=None, parent_id=None, justify=None, sample=None,
        resource=0, label_hash=None):
    """One FOb wrapper carrying a single ObjD chunk (src/hud_asset.c:13)."""
    identity = (struct.pack("<II", object_id, fng_dump.ha_hash(name))
                + b"\x00" * 4 + struct.pack("<H", resource) + b"\x00" * 2)  # OP: id, name, res@12
    fields = rec(b"Ot", struct.pack("<I", kind)) + rec(b"OP", identity)
    if parent_id is not None:
        fields += rec(b"PA", struct.pack("<I", parent_id))
    if justify is not None:
        fields += rec(b"Sj", struct.pack("<I", justify))
    if sample is not None:
        fields += rec(b"St", sample.encode("utf-16-le") + b"\x00\x00")
    if label_hash is not None:
        fields += rec(b"SH", struct.pack("<I", label_hash))
    if sa is not None:
        fields += rec(b"SA", sa)
    return chunk(fng_dump.FOBE, chunk(b"ObjD", fields))


def package(name, objects, resources=(("Font_ConduitMdITCTT38bi.ffn", 3), ("gear_drag.png", 1))):
    """A package body: the "FEn\\xe7" region a 0x30203 chunk carries.

    `resources` is the (alias, kind) pool that RsNm/RsRq hold; an object's OP resource field is an
    index into it.
    """
    pool = b"".join(alias.encode("utf-8") + b"\x00" for alias, _ in resources)
    request = struct.pack("<I", len(resources))
    offset = 0
    for index, (alias, kind) in enumerate(resources):
        request += struct.pack("<III", 0xA000 + index, offset, kind) + b"\x00" * 12
        offset += len(alias) + 1
    header = (b"\x00" * 16 + struct.pack("<II", 32, 0)
              + name.encode("utf-8") + b"\x00" * (32 - len(name)))       # PkHd: names, paths, name
    region = chunk(b"PkHd", header)
    region += chunk(b"Res\xcc", chunk(b"RsNm", pool) + chunk(b"RsRq", request))
    region += chunk(b"Obj\xcc", b"".join(objects))
    return fng_dump.FEN_E7 + struct.pack("<I", len(region)) + region


def bundle(body):
    """A bundle buffer holding one raw package chunk: what --file reads."""
    return struct.pack("<II", fng_dump.CHUNK_PKG_RAW, len(body)) + body


GROUP_SA = appearance(rgba=(255, 255, 255, 255), position=(10.0, 20.0, 0.0), size=(2.0, 2.0, 1.0))
TEXT_SA = appearance(rgba=(138, 244, 202, 0), position=(0.0, -110.0, 99.0), size=(0.7, 0.7, 1.0))
IMAGE_SA = appearance(rgba=(3, 2, 1, 4), position=(5.0, 6.0, 7.0))

NODES = (obj(1, "grp_hud", 5, sa=GROUP_SA),
         obj(2, "shift_text", 2, sa=TEXT_SA, parent_id=1, justify=1, sample="Perfect Shift!"),
         obj(3, "gear_icon", 1, sa=IMAGE_SA, resource=1))


def parse(objects=NODES, name="Test.fng"):
    """The parsed package of a synthetic bundle, exactly as the CLI would read it."""
    wrapped = bundle(package(name, objects))
    return fng_dump.ha_package_select(wrapped, 0, len(wrapped), name)


def render(rows):
    """render_table() as a string (it writes to a stream, as the CLI needs)."""
    sink = io.StringIO()
    fng_dump.render_table(rows, out=sink)
    return sink.getvalue()


def run_cli(args):
    """main() with its streams captured: (exit code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = fng_dump.main(args)
    return code, out.getvalue(), err.getvalue()


def write_bundle(root, rel, body):
    """Lay a synthetic bundle under a temporary data root, as --file expects."""
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(bundle(body))
    return path


class RowsTest(unittest.TestCase):
    """The three-node package: one row each, with the fields the brief asks for."""

    def setUp(self):
        self.pkg = parse()
        self.assertIsNotNone(self.pkg)
        self.assertEqual(0, self.pkg.diagnostics)
        self.rows = fng_dump.rows_for(self.pkg)

    def test_three_nodes_in_file_order(self):
        self.assertEqual([0, 1, 2], [r["index"] for r in self.rows])
        self.assertEqual(["group", "text", "image"], [r["kind_name"] for r in self.rows])

    def test_names_and_parents(self):
        self.assertEqual("0x%08x" % fng_dump.ha_hash("grp_hud"), self.rows[0]["name_hash"])
        self.assertIsNone(self.rows[0]["parent"])           # prints as '-'
        self.assertEqual(0, self.rows[1]["parent"])         # the group, matched by stored id
        self.assertIsNone(self.rows[2]["parent"])

    def test_text_node_fields(self):
        text = self.rows[1]
        self.assertEqual("Perfect Shift!", text["sample_full"])
        self.assertEqual(1, text["justification"])
        self.assertEqual("centre", text["justification_name"])
        self.assertEqual([0.0, -110.0, 99.0], text["position"])
        self.assertAlmostEqual(0.7, text["size"][0], places=6)
        self.assertEqual([202, 244, 138, 0], text["color"])
        self.assertEqual("Font_ConduitMdITCTT38bi.ffn", text["resource_alias"])
        self.assertEqual([], text["flags"])

    def test_image_node_alias(self):
        image = self.rows[2]
        self.assertEqual("gear_drag.png", image["resource_alias"])
        self.assertEqual(1, image["resource_kind"])
        self.assertEqual([1, 2, 3, 4], image["color"])
        self.assertEqual([5.0, 6.0, 7.0], image["point"])   # no parent: the point is the position

    def test_a_node_without_appearance_is_flagged(self):
        """No SA field stores no appearance at all, so the row says '-' rather than inventing 0."""
        bare = obj(4, "bare_text", 2, sample="no appearance here")
        pkg = parse(NODES + (bare,))
        self.assertEqual(4, len(pkg.nodes))
        self.assertIn("no-SA", fng_dump.rows_for(pkg)[3]["flags"])

    def test_point_composes_through_the_group(self):
        """ha_point on the text node: (0,-110,99) scaled 2x in y by the group, then offset."""
        for got, want in zip(self.rows[1]["point"], (10.0, -200.0, 99.0)):
            self.assertAlmostEqual(want, got, places=4)

    def test_render_table_shows_the_rows(self):
        header, group, text, image = render(self.rows).splitlines()
        self.assertEqual(("#", "kind", "name", "flags", "parent", "just"),
                         tuple(header.split()[:6]))
        self.assertIn("Perfect Shift!", text)
        self.assertIn("gear_drag.png", image)
        self.assertIn("sample", header)
        self.assertIn("-", group.split("left")[0])         # '-' where the parent index would be


class LabelTest(unittest.TestCase):
    """Sample text that stands for a LANGUAGES/English.bin string."""

    def setUp(self):
        self.key = fng_dump.ha_hash("HUD_SHIFT_PERFECT")
        self.table = {self.key: "PERFECT"}

    def rows_of(self, node):
        return fng_dump.rows_for(parse((node,)), self.table)

    def test_explicit_SH_key(self):
        rows = self.rows_of(obj(7, "shift", 2, sa=TEXT_SA, sample="Perfect Shift!",
                                label_hash=self.key))
        self.assertEqual("PERFECT", rows[0]["label_text"])
        self.assertEqual("SH", rows[0]["label_src"])
        self.assertEqual("0x%08x" % self.key, rows[0]["label_hash"])

    def test_sample_text_that_is_itself_the_label(self):
        rows = self.rows_of(obj(8, "pause", 2, sa=TEXT_SA, sample="HUD_SHIFT_PERFECT"))
        self.assertEqual("PERFECT", rows[0]["label_text"])
        self.assertEqual("sample", rows[0]["label_src"])

    def test_a_key_the_table_does_not_hold_draws_the_sample(self):
        rows = self.rows_of(obj(9, "drag", 2, sa=TEXT_SA, sample="Perfect Shift!",
                                label_hash=0xDEADBEEF))
        self.assertIsNone(rows[0]["label_text"])
        self.assertEqual("0x%08x" % 0xDEADBEEF, rows[0]["label_hash"])

    def test_no_table_no_label(self):
        rows = fng_dump.rows_for(parse((obj(8, "pause", 2, sa=TEXT_SA,
                                            sample="HUD_SHIFT_PERFECT"),)), None)
        self.assertIsNone(rows[0]["label_text"])
        self.assertIsNone(rows[0]["label_src"])


class TruncationTest(unittest.TestCase):
    """A cut buffer must be refused cleanly, never as a traceback."""

    def test_cli_on_a_truncated_bundle(self):
        with tempfile.TemporaryDirectory() as root:
            path = write_bundle(root, "GLOBAL/InGameTest.bun", package("Test.fng", NODES))
            size = os.path.getsize(path)
            args = ["--data", root, "--file", "GLOBAL/InGameTest.bun", "--package", "Test.fng"]
            code, out, err = run_cli(args)
            self.assertEqual(0, code, err)                  # whole file first: it has to read
            self.assertIn("Perfect Shift!", out)
            for cut in (size - 1, size // 2, size // 3, 40, 8, 1, 0):
                with open(path, "r+b") as handle:
                    handle.truncate(cut)
                code, out, err = run_cli(args)
                self.assertEqual(2, code, "cut to %d bytes: %r / %r" % (cut, out, err))
                self.assertNotIn("Traceback", err + out)
                self.assertIn("data error", err)

    def test_a_chunk_header_with_no_body(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "GLOBAL", "InGameTest.bun")
            os.makedirs(os.path.dirname(path))
            with open(path, "wb") as handle:
                handle.write(struct.pack("<II", fng_dump.CHUNK_PKG_RAW, 6))
            code, _, err = run_cli(["--data", root, "--file", "GLOBAL/InGameTest.bun",
                                    "--package", "Test.fng"])
            self.assertEqual(2, code)
            self.assertNotIn("Traceback", err)

    def test_reader_only_refuses_or_returns(self):
        """Cutting a package anywhere may fail, but only with the errors the callers catch."""
        wrapped = bundle(package("Test.fng", NODES))
        for cut in range(1, len(wrapped)):
            try:
                pkg = fng_dump.ha_package_select(wrapped[:cut], 0, cut, "Test.fng")
            except fng_dump.FngError:                       # a documented refusal
                continue
            except (struct.error, ValueError, IndexError):  # what Catalog.select catches
                continue
            self.assertTrue(pkg is None or len(pkg.nodes) <= 3)

    def test_object_without_identity_is_dropped(self):
        """No OP field means no id and no name: the C drops the object and counts a diagnostic."""
        no_op = chunk(fng_dump.FOBE, chunk(b"ObjD", rec(b"Ot", struct.pack("<I", 2))))
        pkg = parse((NODES[0], no_op))
        self.assertEqual(1, len(pkg.nodes))
        self.assertEqual(1, pkg.diagnostics)


class SelectionTest(unittest.TestCase):
    """selected() wants a name exactly once per region (src/hud_asset.c:52)."""

    def test_a_name_stored_twice_is_not_selectable(self):
        body = package("Test.fng", NODES)
        region = body[8:]
        header = region[:8 + struct.unpack_from("<I", region, 4)[0]]      # the PkHd chunk
        twice = header + region
        body = fng_dump.FEN_E7 + struct.pack("<I", len(twice)) + twice    # same name, twice over
        entries, notes = fng_dump.package_blobs(bundle(body))
        self.assertEqual([], entries)
        self.assertIn("stored 2 times", notes[0])

    def test_unknown_package_lists_close_matches(self):
        with tempfile.TemporaryDirectory() as root:
            write_bundle(root, "GLOBAL/InGameTest.bun", package("HUD_Drag.fng", NODES))
            code, _, err = run_cli(["--data", root, "--file", "GLOBAL/InGameTest.bun",
                                    "--package", "HUD_Dra.fng"])
            self.assertEqual(2, code)
            self.assertIn("close matches: HUD_Drag.fng", err)


class CliTest(unittest.TestCase):
    """--package / --text-only / --json / --list / --find-sample over one synthetic bundle."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.bundle = write_bundle(self.root, "GLOBAL/InGameTest.bun", package("Test.fng", NODES))
        self.args = ["--data", self.root, "--file", "GLOBAL/InGameTest.bun"]

    def tearDown(self):
        self._tmp.cleanup()

    def test_package_dump(self):
        code, out, err = run_cli(self.args + ["--package", "Test.fng"])
        self.assertEqual(0, code, err)
        self.assertNotIn("Traceback", err)
        self.assertIn("package Test.fng", out)
        self.assertIn("nodes 3 (1 text, 1 image, 1 group)", out)
        self.assertIn("rows: 3", out)

    def test_text_only_keeps_text_nodes(self):
        code, out, err = run_cli(self.args + ["--package", "Test.fng", "--text-only"])
        self.assertEqual(0, code, err)
        self.assertNotIn("Traceback", err)
        self.assertIn("text nodes only (1 of 3)", out)
        self.assertIn("rows: 1 of 3 nodes", out)
        self.assertIn("Perfect Shift!", out)
        self.assertNotIn("gear_drag.png", out)

    def test_json_rows_have_the_documented_keys(self):
        with tempfile.TemporaryDirectory() as scratch:
            target = os.path.join(scratch, "out.json")
            code, _, err = run_cli(self.args + ["--package", "Test.fng", "--json", target])
            self.assertEqual(0, code, err)
            with open(target, encoding="utf-8") as handle:
                rows = json.load(handle)
        self.assertEqual(3, len(rows))
        for row in rows:
            self.assertEqual(
                {"index", "kind", "kind_name", "name_hash", "name_id", "parent", "flags",
                 "justification", "justification_name", "resource", "resource_kind",
                 "resource_alias", "position", "size", "color", "point", "sample",
                 "sample_full", "label_hash", "label_src", "label_text"},
                set(row))
            self.assertEqual(3, len(row["position"]))
            self.assertEqual(3, len(row["size"]))
            self.assertEqual(4, len(row["color"]))
            self.assertEqual(3, len(row["point"]))
            self.assertTrue(all(math.isfinite(v) for v in row["position"] + row["point"]))
            self.assertLessEqual(len(row["sample"] or ""), fng_dump.SAMPLE_WIDTH)
        self.assertEqual("gear_drag.png", rows[2]["resource_alias"])
        self.assertEqual([202, 244, 138, 0], rows[1]["color"])

    def test_json_matches_the_table_text_only_keeps(self):
        with tempfile.TemporaryDirectory() as scratch:
            target = os.path.join(scratch, "text.json")
            run_cli(self.args + ["--package", "Test.fng", "--text-only", "--json", target])
            with open(target, encoding="utf-8") as handle:
                rows = json.load(handle)
        self.assertEqual(["text"], [row["kind_name"] for row in rows])

    def test_list_reports_the_package_and_its_file(self):
        code, out, err = run_cli(self.args + ["--list"])
        self.assertEqual(0, code, err)
        self.assertIn("Test.fng", out)
        self.assertIn("GLOBAL/InGameTest.bun", out)
        self.assertIn("packages: 1 occurrences, 1 distinct names", out)

    def test_find_sample_searches_all_packages(self):
        code, out, err = run_cli(self.args + ["--find-sample", "perfect shift"])
        self.assertEqual(0, code, err)
        self.assertIn("0x%08x" % fng_dump.ha_hash("shift_text"), out)
        self.assertIn('"Perfect Shift!"', out)
        self.assertIn("nodes: 1 match 'perfect shift' in 1 packages scanned", out)

    def test_a_sample_is_cut_to_forty_characters(self):
        with open(self.bundle, "wb") as handle:
            handle.write(bundle(package("Test.fng", NODES + (
                obj(9, "marquee", 2, sa=TEXT_SA, sample="S" * 55),))))
        code, out, err = run_cli(self.args + ["--package", "Test.fng", "--text-only"])
        self.assertEqual(0, code, err)
        self.assertIn('"' + "S" * 40 + '"...', out)


class ImportDisciplineTest(unittest.TestCase):
    """The tool may import nothing but the standard library and its sibling reader."""

    def test_stdlib_only(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fng_dump.py")
        with open(path, encoding="utf-8") as handle:
            lines = [line for line in handle if line.startswith(("import ", "from "))]
        self.assertTrue(lines)
        for line in lines:
            root = line.split()[1].split(".")[0]
            if root == "lang_key_audit":                    # the one allowed sibling
                continue
            self.assertIn(root, sys.stdlib_module_names, "non-stdlib import: %s" % line.strip())


if __name__ == "__main__":
    unittest.main()
