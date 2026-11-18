#!/usr/bin/env python3
"""Unit tests for tools/lang_key_audit.py. Standard library only, no retail data.

  python3 -I tools/lang_key_audit_test.py -v

Fixtures are synthetic: the string table is built here, and the source is an in-memory snippet,
so the tests read nothing outside this file.
"""
import contextlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lang_key_audit as audit


def build_chunk(entries, name=b"TST", hdr=0x20, cmap=None):
    """Build a synthetic 0x00039000 chunk the way LANGUAGES/English.bin holds one.

    entries is a list of (hash, text-or-bytes); the payload layout follows ug_strtab_load:
    hdr|count|tab|area, an optional 16-byte name, the 256-entry code map, the entry table and
    then the NUL-terminated string area.
    """
    cmap = cmap or [0] * 256
    tab = hdr + 4 + 512
    area = tab + 8 * len(entries)
    blob = b""
    offsets = []
    for _, text in entries:
        raw = text.encode("utf-8") if isinstance(text, str) else bytes(text)
        offsets.append(len(blob))
        blob += raw + b"\x00"
    payload = struct.pack("<IIII", hdr, len(entries), tab, area)
    if hdr == 0x20:
        payload += name.ljust(16, b"\x00")[:16]
    payload += struct.pack("<I", 256) + struct.pack("<256H", *cmap)
    for (key, _), off in zip(entries, offsets):
        payload += struct.pack("<II", key, off)
    payload += blob
    return struct.pack("<II", audit.STRTAB_CHUNK_ID, len(payload)) + payload


def table_of(**texts):
    """A {hash: text} table from LABEL=text keyword arguments, keyed by ug_hash_upper(LABEL)."""
    return {audit.ug_hash_upper(name): text for name, text in texts.items()}


def scan(lines, table=None):
    return audit.scan_lines(lines, "t.c", table if table is not None else {})


class HashVectors(unittest.TestCase):
    def test_documented_labels(self):
        self.assertEqual(0x0598b065, audit.ug_hash_upper("HUD_SHIFT_PERFECT"))
        self.assertEqual(0x27d2dd45, audit.ug_hash_upper("HUD_SHIFT_GOOD"))
        self.assertEqual(0x75941dc1, audit.ug_hash_upper("HUD_SHIFT_MISSED"))

    def test_case_folds_to_upper(self):
        self.assertEqual(audit.ug_hash_upper("hud_shift_perfect"),
                         audit.ug_hash_upper("HUD_SHIFT_PERFECT"))
        self.assertEqual(audit.ug_hash_upper("Career"), audit.ug_hash_upper("CAREER"))

    def test_wraps_at_32_bits(self):
        # h = h*33 + c kept inside 32 bits, starting from 0xFFFFFFFF.
        h = 0xFFFFFFFF
        for c in b"OK":
            h = (h * 33 + c) & 0xFFFFFFFF
        self.assertEqual(h, audit.ug_hash_upper("OK"))

    def test_self_test_detects_a_broken_hash(self):
        good = audit.ug_hash_upper
        self.assertEqual(1, audit.self_test_hash())
        audit.ug_hash_upper = lambda text: 0
        try:
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(0, audit.self_test_hash())
            self.assertIn("self-test failed", err.getvalue())
        finally:
            audit.ug_hash_upper = good


class Loader(unittest.TestCase):
    def parse(self, chunk):
        offsets = audit.strtab_offsets(chunk)
        self.assertEqual(1, len(offsets), "expected the walker to find exactly one chunk")
        return audit.load_string_table(chunk, offsets[0], "synthetic.bin")

    def test_two_entries(self):
        chunk = build_chunk([(0x11111111, "Perfect Shift!"), (0x22222222, "Career")])
        name, count, table = self.parse(chunk)
        self.assertEqual(2, count)
        self.assertEqual("TST", name)
        self.assertEqual({"Perfect Shift!", "Career"}, set(table.values()))
        self.assertEqual("Career", table[0x22222222])

    def test_header_without_a_name(self):
        name, count, table = self.parse(build_chunk([(0x1, "One")], hdr=0x10))
        self.assertEqual("", name)
        self.assertEqual(1, count)
        self.assertEqual("One", table[0x1])

    def test_code_map_remaps_high_bytes(self):
        cmap = [0] * 256
        cmap[0xE9] = 0x00E9  # retail 'é' as Latin-1 -> U+00E9
        _, _, table = self.parse(build_chunk([(0x33, b"caf\xe9")], cmap=cmap))
        self.assertEqual("café", table[0x33])

    def test_duplicate_hash_keeps_the_last_text(self):
        # ug_strtab_load's insert() replaces an equal hash, so the count can exceed the table.
        chunk = build_chunk([(0x55, "first"), (0x55, "second")])
        _, count, table = self.parse(chunk)
        self.assertEqual(2, count)
        self.assertEqual(1, len(table))
        self.assertEqual("second", table[0x55])

    def test_nested_chunk_is_walked_into(self):
        inner = build_chunk([(0x77, "Nested")])
        outer = struct.pack("<II", 0x80010000, len(inner)) + inner
        offsets = audit.strtab_offsets(outer)
        self.assertEqual(1, len(offsets))
        _, _, table = audit.load_string_table(outer, offsets[0], "synthetic.bin")
        self.assertEqual("Nested", table[0x77])

    def test_truncated_chunk_is_a_data_error(self):
        chunk = build_chunk([(0x1, "text")])[:12]
        with self.assertRaises(audit.StrTabError):
            audit.load_string_table(chunk, 0, "synthetic.bin")

    def test_wrong_chunk_id_is_a_data_error(self):
        with self.assertRaises(audit.StrTabError):
            audit.load_string_table(struct.pack("<II", 0x12345678, 16) + b"\0" * 16, 0, "s.bin")

    def test_string_running_past_the_chunk_is_a_data_error(self):
        chunk = build_chunk([(0x1, b"unterminated")])
        chunk = chunk[:len(chunk) - 1]  # drop the final NUL, so the scan runs out of the payload
        with self.assertRaises(audit.StrTabError):
            audit.load_string_table(chunk, 0, "synthetic.bin")


class Scanner(unittest.TestCase):
    def setUp(self):
        self.table = table_of(HUD_SHIFT_PERFECT="Perfect Shift!", BACK="Back", SELECT="Select",
                             YES="Yes", CAREER="Career")

    def statuses(self, lines, table=None):
        rows, notes = scan(lines, self.table if table is None else table)
        return {r["key"]: (r["status"], r["label"], r["line"]) for r in rows}, notes

    def test_hex_literal_argument(self):
        rows, notes = self.statuses(['    s = gv_text(assets, 0x001335f0u, "Back");'])
        self.assertEqual(("OK", "", 1), rows["0x001335f0"])
        self.assertEqual([], notes["unresolved"])

    def test_hash_upper_argument_carries_the_label(self):
        rows, _ = self.statuses(['  t = gv_text(a, ug_hash_upper("HUD_SHIFT_PERFECT"), "x");'])
        self.assertEqual(("OK", "HUD_SHIFT_PERFECT", 1), rows["0x0598b065"])

    def test_each_accessor_is_checked(self):
        rows, _ = self.statuses([
            'a = gv_text(v, 0x00000001u, "x");',
            'b = pause_menu_text(p, 0x00000002u, "x");',
            'c = ug_strtab_get(&t->strings, 0x00000003u);',
            'd = ug_strtab_or(&t->strings, 0x00000004u, "x");',
        ])
        self.assertEqual({"0x00000001", "0x00000002", "0x00000003", "0x00000004"}, set(rows))
        same, _ = self.statuses(['a = gv_text(v, 0x001335f0u, "x");'])
        self.assertEqual(1, len(same), "one row per line and key, not one per pattern")

    def test_missing_key_vs_known_key(self):
        rows, _ = self.statuses(['a = gv_text(v, 0xdeadbeefu, "x");',
                                 'b = gv_text(v, 0x001335f0u, "Back");'])
        self.assertEqual("MISSING", rows["0xdeadbeef"][0])
        self.assertEqual("OK", rows["0x001335f0"][0])

    def test_labelled_literal_is_checked_against_its_own_label(self):
        rows, _ = self.statuses(['    case 3:return 0x0598b065u; /* HUD_SHIFT_PERFECT */'])
        self.assertEqual(("OK", "HUD_SHIFT_PERFECT", 1), rows["0x0598b065"])

    def test_labelled_literal_with_the_wrong_hash_is_a_mismatch(self):
        rows, notes = self.statuses(['    out[0] = 0xd05fc3a3u; /* Yes */'])
        row = [r for r in scan(['    out[0] = 0xd05fc3a3u; /* Yes */'], self.table)[0]][0]
        self.assertEqual(1, len(rows))
        self.assertEqual("LABEL-MISMATCH", row["status"])
        self.assertEqual("0x0000f770", row["label_hash"])
        self.assertEqual("0xd05fc3a3", row["literal"])
        self.assertEqual("Yes", row["label"])

    def test_prose_comment_is_not_read_as_a_label(self):
        rows, notes = self.statuses(
            ['    settext(v, gv_text(a, 0x001335f0u, "Back")); /* console-only: not drawn on PC */'])
        self.assertEqual(("OK", "", 1), rows["0x001335f0"])
        self.assertEqual(1, len(notes["ambiguous"]))

    def test_quoted_fourcc_is_not_read_as_a_label(self):
        rows, notes = self.statuses(['    if (fmt == 0x31545844) { /* "DXT1" */ })'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["ambiguous"])

    def test_quoted_name_never_claims_a_literal(self):
        # A name in quotes is the literal's *name*, not a label claim: this is the shape a comment
        # uses for a texture / node / material id, so no string-table row may come from it. An
        # unquoted `/* TRN_GRASSC */` would agree with the literal and be reported MISSING.
        rows, notes = self.statuses(
            ['           ? upload_tex_keyed(0x5671B2B5u /* texture "TRN_GRASSC" */, NULL, &g)'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["ambiguous"])

    def test_unquoted_single_name_does_claim_the_literal(self):
        # The counter-case of the one above: without the quotes the same comment is a label claim,
        # which is exactly why the tree-wide comments had to be quoted, not just prefixed.
        rows, notes = self.statuses(
            ['           ? upload_tex_keyed(0x5671B2B5u /* texture TRN_GRASSC */, NULL, &g)'])
        self.assertEqual(("MISSING", "TRN_GRASSC", 1), rows["0x5671b2b5"])
        self.assertEqual([], notes["ambiguous"])

    def test_two_candidate_names_are_ambiguous_not_a_row(self):
        # A second upper-case word makes the comment stop identifying the literal, so the tool
        # reports nothing but the informational note. That is the safe direction, but a fix must
        # not rely on it: `/* atlas TRN_X DM */` would read `DM` as a label claim instead.
        rows, notes = self.statuses(
            ['    static const uint32_t tex[] = { 0x0801e3a1u }; /* atlas "TRN_COASTROADLOD_A_DM" TRN_PANARAMA */'])
        self.assertEqual({}, rows)
        self.assertEqual(1, len(notes["ambiguous"]))

    def test_plain_hex_literal_is_not_reported(self):
        rows, notes = self.statuses(['    uint32_t m = 0x12345678;'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["multi-line"] + notes["unresolved"] + notes["ambiguous"])

    def test_wider_hex_literal_is_not_reported(self):
        rows, _ = self.statuses(['    uint32_t m = 0x123456789u; /* HUD_SHIFT_PERFECT */'])
        self.assertEqual({}, rows)

    def test_variable_key_argument_is_unresolved_not_guessed(self):
        rows, notes = self.statuses(['    t = gv_text(a, e->label_key, e->label);',
                                     '    t = gv_text(a, ug_hash_upper(keys[i]), "x");'])
        self.assertEqual({}, rows)
        self.assertEqual(2, len(notes["unresolved"]))

    def test_prototypes_are_not_calls(self):
        rows, notes = self.statuses([
            'const char *gv_text(const UgAssets *a, uint32_t key, const char *fallback) {',
            'const char *ug_strtab_get(const UgStrTab *t, uint32_t hash);'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["unresolved"])

    def test_multi_line_call_is_counted_as_skipped(self):
        rows, notes = self.statuses(['    t = gv_text(a,', '           0x001335f0u,', '           "Back");'])
        self.assertEqual({}, rows)
        self.assertEqual(1, len(notes["multi-line"]))
        self.assertIn("t.c:1", notes["multi-line"][0])

    def test_comment_does_not_hide_a_later_call(self):
        rows, _ = self.statuses(['    /* 0x0598b065 */ t = gv_text(a, 0x001335f0u, "Back");'])
        self.assertEqual({"0x001335f0"}, set(rows))

    def test_string_content_is_not_scanned_for_calls(self):
        rows, notes = self.statuses(['    log("see gv_text(0x0598b065u) in the docs");'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["unresolved"])

    def test_two_keys_on_one_line_are_both_reported(self):
        rows, _ = self.statuses(
            ['    {gv_text(a,ug_hash_upper("BACK"),"Back"),gv_text(a,ug_hash_upper("SELECT"),"Select"),1},'])
        self.assertEqual({"0x001335f0", "0x79ad657f"}, set(rows))

    def test_key_field_assignment_is_a_row(self):
        rows, notes = self.statuses([
            '        out->label_key[0] = 0x00000639u; out->label[0] = "OK";',
            '        out->message_key = 0x0aab553eu;'])
        self.assertEqual({"0x00000639", "0x0aab553e"}, set(rows))
        self.assertEqual([], notes["unresolved"])

    def test_designated_initialiser_is_a_row(self):
        rows, notes = self.statuses(['    GdSpec s = { .message_key = 0x00000639u, .message = "OK" };'])
        self.assertEqual({"0x00000639"}, set(rows))
        self.assertEqual([], notes["unresolved"])

    def test_text_record_table_is_a_row(self):
        rows, notes = self.statuses([
            'typedef struct LangKey { uint32_t key; const char *en; } LangKey;',
            'static const LangKey K_QUIT = { 0x00000639u, "OK" };',
            'static const LangKey K_BACK = { 0x001335f0u, "Back" };'])
        self.assertEqual({"0x00000639", "0x001335f0"}, set(rows))
        self.assertEqual([], notes["unresolved"])

    def test_a_package_table_row_is_not_read_as_a_key(self):
        rows, notes = self.statuses([
            'static const Frame gd_frames[] = {',
            '    { 0x6962C0CDu, "GenericDialog_SMALL.fng", 4 },',
            '    { 0x9943D584u, "U2_Menu_Arrow.tga", 1 },',
            '};'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["unresolved"] + notes["ambiguous"])

    def test_node_id_and_message_id_defines_are_not_rows(self):
        rows, notes = self.statuses([
            '#define FES_ROW_TOP     0x9a3674c8u   /* node 13: the top subject row */',
            '#define KEY_PKG_SMALL 0x6962C0CDu',
            'd->result_msg[0] = 0xD05FC3A3u;         /* FE message for the first button */'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["unresolved"])

    def test_other_namespaces_that_end_in_key_stay_out_of_the_rows(self):
        # A model handle shares the `_key` spelling and never becomes a row; `label` holds the
        # English text; and an unnamed `.key` outside a language record has no literal to judge,
        # so it is neither a row nor an unresolved note.
        rows, notes = self.statuses([
            '    e->model_key = model_key;',
            '    n->world_base_model_key = base_key;',
            '    s.label = label;',
            '    .key = key'])
        self.assertEqual({}, rows)
        self.assertEqual([], notes["unresolved"])

    def test_a_key_named_by_a_constant_is_unresolved(self):
        rows, notes = self.statuses(['    out->message_key = T_DEFAULTS.key;',
                                     '    out->label_key[0] = FIRST_LABEL;'])
        self.assertEqual({}, rows)
        self.assertEqual(2, len(notes["unresolved"]))
        self.assertIn("message_key", notes["unresolved"][0])


class CommandLine(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="langkey-test-")
        self.data = os.path.join(self.root, "data")
        self.src = os.path.join(self.root, "src")
        os.makedirs(os.path.join(self.data, "LANGUAGES"))
        os.makedirs(self.src)
        chunk = build_chunk([(audit.ug_hash_upper("HUD_SHIFT_PERFECT"), "Perfect Shift!"),
                             (audit.ug_hash_upper("BACK"), "Back")])
        with open(os.path.join(self.data, "LANGUAGES", "English.bin"), "wb") as handle:
            handle.write(chunk)
        # HUD_SHIFT_EARLY hashes to its own literal but is not in the table: the MISSING case.
        self.missing_key = audit.ug_hash_upper("HUD_SHIFT_EARLY")
        with open(os.path.join(self.src, "a.c"), "w", encoding="utf-8") as handle:
            handle.write('const char *t = gv_text(a, ug_hash_upper("HUD_SHIFT_PERFECT"), "x");\n')
            handle.write('uint32_t k = rr_key(3);\n')
            handle.write('return 0x%08xu; /* HUD_SHIFT_EARLY */\n' % self.missing_key)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def run_main(self, *extra):
        out = io.StringIO()
        err = io.StringIO()
        argv = ["--data", self.data, "--src", self.src] + list(extra)
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = audit.main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_reports_a_missing_key_and_exits_one(self):
        code, out, _ = self.run_main()
        self.assertEqual(1, code)
        self.assertIn("2 strings read", out)
        self.assertIn("%s  HUD_SHIFT_EARLY  MISSING" % ("0x%08x" % self.missing_key), out)
        self.assertIn("0x0598b065  HUD_SHIFT_PERFECT  OK  \"Perfect Shift!\"", out)
        self.assertIn("keys found: 2  OK: 1  MISSING: 1  LABEL-MISMATCH: 0  skipped: 0", out)

    def test_json_lists_the_same_rows(self):
        path = os.path.join(self.root, "sub", "audit.json")
        code, out, _ = self.run_main("--json", path)
        self.assertEqual(1, code, "the directory is created for the caller")
        with open(path, encoding="utf-8") as handle:
            rows = json.load(handle)
        self.assertEqual(2, len(rows))
        self.assertIn("0x%08x" % self.missing_key, [r["key"] for r in rows])
        self.assertTrue(all(r["file"].startswith(self.src) for r in rows),
                        "paths are printed as given under --src")

    def test_clean_source_exits_zero(self):
        with open(os.path.join(self.src, "a.c"), "w", encoding="utf-8") as handle:
            handle.write('const char *t = gv_text(a, ug_hash_upper("BACK"), "Back");\n')
        code, out, _ = self.run_main()
        self.assertEqual(0, code)
        self.assertIn("MISSING: 0  LABEL-MISMATCH: 0", out)

    def test_missing_data_exits_two(self):
        code, _, err = self.run_main("--data", os.path.join(self.root, "nowhere"))
        self.assertEqual(2, code)
        self.assertIn("not found", err)

    def test_missing_src_exits_two(self):
        code, _, err = self.run_main("--src", os.path.join(self.root, "nosuch"))
        self.assertEqual(2, code)
        self.assertIn("usage error", err)

    def test_unparsable_table_exits_two(self):
        with open(os.path.join(self.data, "LANGUAGES", "English.bin"), "wb") as handle:
            handle.write(struct.pack("<II", audit.STRTAB_CHUNK_ID, 64) + b"\0" * 64)
        code, _, err = self.run_main()
        self.assertEqual(2, code)
        self.assertIn("data error", err)

    def test_unwritable_json_exits_two(self):
        blocked = os.path.join(self.data, "LANGUAGES", "English.bin", "out", "audit.json")
        code, _, err = self.run_main("--json", blocked)
        self.assertEqual(2, code, "English.bin is a file, so its 'subdirectory' cannot exist")
        self.assertIn("usage error", err)


if __name__ == "__main__":
    unittest.main(verbosity=2)
