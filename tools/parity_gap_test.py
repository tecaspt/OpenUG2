#!/usr/bin/env python3
"""Self-test for tools/parity_gap.py: synthetic data only, no retail files, no network.

Builds a fake package catalogue and a fake src tree in a temp directory, so every rule the
inventory applies - USED by name, USED by hash, comment vs code, the classification table, the
milestone table, the JSON shape - is checked against inputs this file controls.

  python3 -I tools/parity_gap_test.py -v

Retail-only paths (fng_dump.Catalog, the language table) are exercised by the same functions
through a stub catalogue: the reader itself has its own test (tools/fng_dump_test.py).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fng_dump            # noqa: E402  ha_hash is the hash rule under test
import parity_gap as gap   # noqa: E402


def synthetic_source():
    """A source tree covering each USED-detection case, as {relpath: text}."""
    name = "UI_TestScreen.fng"
    hashed = "UI_HashedOnly.fng"
    stem_hash = "%08X" % fng_dump.ha_hash(hashed)
    return {
        # a code statement AND a trailing comment naming the same package: both must be seen,
        # and the load site must not be hidden by the comment
        "src/load_ok.c": (
            '#include "load_ok.h"\n'
            'const char *screen_name(void) { return "%s"; }  /* the %s frame */\n'
            'static const unsigned kPkg = 0x%08Xu;\n'
            % (name, name, fng_dump.ha_hash(name))),
        "src/only_comment.h": (
            "/**\n"
            " * Notes about UI_Commentary.fng: named inside a block comment and nowhere else.\n"
            " */\n"
            "void nothing(void);\n"),
        "src/hashed.c": (
            "#define HASH_PKG 0x%su\n" % stem_hash),                   # USED by hash only
        "src/nothing_here.c": (
            "int unrelated(void) { return 0; }\n"),
        "tools/not_source.txt": ("mention of %s in a non-source file\n" % name),
    }


def write_source(root, files):
    for rel, text in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    return root


class HashRuleTest(unittest.TestCase):
    def test_hash_matches_a_c_literal(self):
        """The hash rule finds the constant spelling src/gui_dialog.c actually uses."""
        lines = [("src/gui_dialog.c", 1, "#define KEY_PKG 0xAA469286u", "code")]
        hits = gap.find_hits("UI_InGameDialog.fng", lines)
        self.assertEqual([h["by"] for h in hits], ["hash"])

    def test_hash_of_name_is_case_insensitive(self):
        self.assertEqual(fng_dump.ha_hash("UI_Pause.fng"), fng_dump.ha_hash("ui_pause.fng"))


class UsedDetectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix="parity-gap-src-")
        write_source(cls.root, synthetic_source())
        cls.lines = gap.scan_source(cls.root)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def test_scan_reads_only_sources(self):
        self.assertFalse([l for l in self.lines if l[0].endswith(".txt")])

    def test_used_by_name_in_code(self):
        hits = gap.find_hits("UI_TestScreen.fng", self.lines)
        kinds = {(h["by"], h["kind"]) for h in hits}
        self.assertIn(("name", "code"), kinds)
        self.assertIn(("hash", "code"), kinds)      # the same file carries its hash too

    def test_the_suffix_may_not_be_stripped_from_the_quoted_spelling(self):
        """The rule that matches `"UI_pause.fng"` must not forbid the following "f".

        Stripping ".fng" and then rejecting a following "." looks like the tidy near-miss rule and
        matches nothing at all, because every quoted literal in src/ carries the suffix.
        """
        lines = [("src/x.c", 1, 'const char *s = "UI_Paused.fng";', "code")]
        self.assertEqual(gap.find_hits("UI_pause.fng", lines), [],
                         "UI_pause claimed UI_Paused.fng")
        self.assertEqual(len(gap.find_hits("UI_Paused.fng", lines)), 1)
        self.assertEqual(len(gap.find_hits("UI_pause.fng",
                         [("src/y.c", 1, 'om_screen = "UI_pause.fng";', "code")])), 1)

    def test_a_trailing_comment_does_not_hide_the_load_site(self):
        """The line mixes code and comment; the code half must still count as a code hit."""
        hits = [h for h in gap.find_hits("UI_TestScreen.fng", self.lines)
                if h["by"] == "name" and h["kind"] == "code"]
        self.assertTrue(hits, "return \"UI_TestScreen.fng\"; /* note */ read as comment-only")

    def test_code_span_cases(self):
        for line, depth, want_code, want_depth in (
                ('const char *s = "X.fng"; /* note */', 0, True, 0),
                ('/* only */', 0, False, 0),
                (' * inside a block */ int x;', 1, True, 0),
                (' * still inside a block', 1, False, 2),
                ('int x; // "X.fng" in a line comment', 0, True, 0),
                ('   ', 0, False, 0)):
            code, after = gap.code_span(line, depth)
            self.assertEqual(bool(code.strip()), want_code, line)
            self.assertEqual(after, want_depth, "depth after %r" % line)

    def test_comment_only_hit_is_tagged_comment(self):
        hits = gap.find_hits("UI_CommentOnly.fng", [
            ("src/a.h", 3, "/* UI_CommentOnly.fng is the retail frame */", "comment")])
        self.assertEqual([h["kind"] for h in hits], ["comment"])

    def test_block_comment_mention_is_a_comment_hit(self):
        hits = gap.find_hits("UI_Commentary.fng", self.lines)
        self.assertTrue(hits)
        self.assertEqual({h["kind"] for h in hits}, {"comment"})

    def test_hash_only_reference_is_found(self):
        hits = gap.find_hits("UI_HashedOnly.fng", self.lines)
        self.assertEqual({h["by"] for h in hits}, {"hash"})
        self.assertEqual({h["kind"] for h in hits}, {"code"})

    def test_near_miss_names_do_not_match(self):
        """A shorter name must not claim a package whose name extends it."""
        lines = [("src/near_miss.c", 1,
                  "/* GenericDialog_SMALL is another package */", "comment")]
        self.assertEqual(gap.find_hits("GenericDialog.fng", lines), [])
        self.assertEqual(len(gap.find_hits("GenericDialog_SMALL.fng", lines)), 1)

    def test_case_insensitive_name_matches(self):
        hits = gap.find_hits("ui_testscreen.fng", self.lines)
        self.assertTrue(hits)

    def test_code_only_mode_drops_comment_hits(self):
        lines = [("src/x.h", 1, "/* UI_Commentary.fng named here only */", "comment")]
        self.assertEqual(gap.counting_kinds("all"), ("code", "comment"))
        self.assertEqual(gap.counting_kinds("code-only"), ("code",))
        hits = gap.find_hits("UI_Commentary.fng", lines)
        self.assertFalse([h for h in hits if h["kind"] in gap.counting_kinds("code-only")])

    def test_missing_src_raises(self):
        with tempfile.TemporaryDirectory() as empty:
            with self.assertRaises(gap.GapError):
                gap.scan_source(empty)


class ClassificationTest(unittest.TestCase):
    def table_covers_every_class(self):
        self.assertEqual({c for c, _, _ in gap.CLASS_RULES} | {c for c, _, _ in gap.CONTENT_RULES},
                         set(gap.CLASSES))

    def cases(self):
        return (
            ("UI_Main.fng", "frontend-screen"),
            ("UI_QRTrackSelect.fng", "frontend-screen"),
            ("MU_QuickRaceCarSelect.fng", "frontend-screen"),
            ("WorldMapMain.fng", "frontend-screen"),
            ("GenericDialog_SMALL.fng", "dialog"),
            ("IG_GenericDialog_MED.fng", "dialog"),
            ("HelpDialog_LARGE.fng", "dialog"),
            ("UI_InGameDialog.fng", "dialog"),
            ("UI_EngageShopDialog.fng", "dialog"),
            ("MU_PostRaceConfirm.fng", "dialog"),
            ("HUD_Drag.fng", "ingame-hud"),
            ("Chyron_IG.fng", "ingame-hud"),
            ("UI_pause.fng", "ingame-menu"),
            ("UI_PauseOptionsMain.fng", "ingame-menu"),
            ("UI_InGame_WorldMap.fng", "ingame-menu"),
            ("UI_ReplayControl.fng", "ingame-menu"),
            ("2PHudTop_DRAG.fng", "2-player"),
            ("2P_PressStart.fng", "2-player"),
            ("UI_OLGameRoom_host.fng", "online-lan"),
            ("UI_PC_LAN_ServerSelect.fng", "online-lan"),
            ("PC_OL_Lobby.fng", "online-lan"),
            ("MC_Main.fng", "memory-card-console"),
            # UI_OLAgeVerif.fng is the ESRB "Parental Notice"/"Create New Account" gate: its name
            # says online, but the age gate is a console-only flow, so the console rule claims it
            # first - the order of CLASS_RULES is the decision, and this case pins it.
            ("UI_OLAgeVerif.fng", "memory-card-console"),
            ("LS_EAlogo.fng", "boot-attract"),
            ("MW_LS_IntroFMV.fng", "boot-attract"),
            ("UG_LS_Splash.fng", "boot-attract"),
            ("loading_boot.fng", "boot-attract"),
            ("IG_PlayMovie.fng", "boot-attract"),
            ("DiscErrorPC.fng", "boot-attract"),
        )

    def test_classification_table(self):
        for name, want in self.cases():
            cls, why = gap.class_of(name)
            got, why = gap.class_from_content(cls, why, name, 5, 0)
            self.assertEqual(got, want, "%s -> %s (%s)" % (name, got, why))

    def test_online_beats_the_front_end_sweep(self):
        """Order matters: UI_OL* must not be swallowed by the broad ^ui_ rule."""
        self.assertEqual(gap.class_of("UI_OLMAIN.fng")[0], "online-lan")

    def test_content_rules(self):
        self.assertEqual(gap.class_from_content(None, None, "Mystery.fng", 0, 0)[0], "other")
        self.assertEqual(gap.class_from_content(None, None, "Mystery.fng", 9, 9)[0], "other")
        self.assertEqual(gap.class_from_content(None, None, "Mystery.fng", 6, 0)[0],
                         "frontend-screen")

    def test_default_string_overrides_a_name_rule(self):
        """A package whose text is all placeholder is not a screen, whatever its name says."""
        self.assertEqual(gap.class_from_content("frontend-screen", "x", "ScreenPrintf.fng", 64, 64),
                         ("other", gap.CONTENT_RULES[0][2]))

    def test_placeholder_detection(self):
        self.assertTrue(gap.is_placeholder("$JOY_EVENT_FENG_HELP$"))
        self.assertFalse(gap.is_placeholder("Continue"))
        self.assertFalse(gap.is_placeholder(None))
        self.assertTrue(gap.is_default_string("DEFAULT STRING"))
        self.assertFalse(gap.is_default_string("DEFAULT STRING: extra"))

    def test_in_scope_flag(self):
        for cls in gap.IN_SCOPE:
            self.assertTrue(cls in gap.CLASSES)
        self.assertNotIn("online-lan", gap.IN_SCOPE)


class MilestoneTest(unittest.TestCase):
    def test_narrow_families_win(self):
        self.assertTrue(gap.milestone_of("UI_OLRankings.fng")[0].startswith("out-of-scope"))
        self.assertTrue(gap.milestone_of("MC_List.fng")[0].startswith("out-of-scope"))
        self.assertTrue(gap.milestone_of("LS_THXMovie.fng")[0].startswith("M3"))
        self.assertTrue(gap.milestone_of("HUD_Drift.fng")[0].startswith("M9"))
        self.assertTrue(gap.milestone_of("UI_SMS_Mailbox.fng")[0].startswith("M6"))
        self.assertTrue(gap.milestone_of("UI_QRCarSelect.fng")[0].startswith("M1"))
        self.assertTrue(gap.milestone_of("UI_ProfileManager.fng")[0].startswith("M2"))

    def test_every_real_name_gets_a_milestone(self):
        """The table is exhaustive: no package of this retail set may fall through unmapped."""
        names = ("UI_Main.fng", "HUD_Drag.fng", "Credits.fng", "EA_TRAX_Jukebox.fng",
                 "GarageMain.fng", "2PHudBot.fng", "MC_Bootup.fng", "PC_ContextHelp.fng")
        for name in names:
            label, pattern = gap.milestone_of(name)
            self.assertTrue(pattern, "%s fell through the milestone table" % name)
            self.assertTrue(label)

    def test_screenprintf_stays_unmapped(self):
        self.assertTrue(gap.milestone_of("ScreenPrintf.fng")[0].startswith("unmapped"))


class SizeTest(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual(gap.size_hint(3, 0), "S")
        self.assertEqual(gap.size_hint(12, 3), "S")
        self.assertEqual(gap.size_hint(13, 3), "M")
        self.assertEqual(gap.size_hint(40, 8), "M")
        self.assertEqual(gap.size_hint(41, 0), "L")
        self.assertEqual(gap.size_hint(258, 60), "L")

    def test_the_larger_dimension_wins(self):
        """A 200-node HUD with one caption is not small."""
        self.assertEqual(gap.size_hint(200, 1), "L")
        self.assertEqual(gap.size_hint(5, 30), "L")
        self.assertEqual(gap.size_hint(20, 2), "M")


# ---- the inventory and its JSON shape, against a stub catalogue ---------------------------------

class FakePkg(object):
    def __init__(self, rows):
        self.rows = rows


def fake_row(index, kind, sample=None, label=None):
    return {"index": index, "kind": kind, "kind_name": {1: "image", 2: "text", 5: "group"}[kind],
            "sample_full": sample, "label_text": label, "flags": []}


class StubCatalog(object):
    """Stands in for fng_dump.Catalog: the blobs and the per-node rows come from the test.

    inventory() gets its rows through the stubbed rows_for below rather than the real
    fng_dump.rows_for, which would need a fully parsed package (resources, nodes, hierarchy) this
    file has no business fabricating; that reader has its own coverage in fng_dump_test.py.
    """

    def __init__(self, blobs, rows_by_name, notes=()):
        self._blobs = blobs
        self._rows = rows_by_name
        self.notes = list(notes)

    def blobs(self):
        return self._blobs

    def select(self, wrapped, name):
        return FakePkg(self._rows[name]) if self._rows.get(name) else None

    def language_table(self):
        return {}


def stub_rows(pkg, table=None):
    """rows_for replacement: the fake package already carries the rows inventory() needs."""
    return pkg.rows


class InventoryTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="parity-gap-")
        write_source(self.root, synthetic_source())
        self.blobs = [
            ("FRONTEND/Fake.lzc", 0x100, 0x30203, "UI_TestScreen.fng", b"a"),
            ("FRONTEND/Fake.lzc", 0x200, 0x30203, "UI_HashedOnly.fng", b"b"),
            ("FRONTEND/Fake.lzc", 0x300, 0x30203, "UI_Commentary.fng", b"c"),
            ("FRONTEND/Fake.lzc", 0x400, 0x30203, "UI_NeverNamed.fng", b"d"),
            ("GLOBAL/Other.lzc", 0x500, 0x30203, "UI_TestScreen.fng", b"a"),   # pooled duplicate
        ]
        self.rows = {
            "UI_TestScreen.fng": [fake_row(0, 5), fake_row(1, 2, "Continue", "Continue"),
                                  fake_row(2, 2, "$JOY_EVENT_FENG_HELP$"), fake_row(3, 1)],
            "UI_HashedOnly.fng": [fake_row(0, 2, "Hello", "Hello")],
            "UI_Commentary.fng": [fake_row(0, 2, "Only in a comment", "Only in a comment")],
            "UI_NeverNamed.fng": [fake_row(0, 2, "DEFAULT STRING")],
        }
        self.catalog = StubCatalog(self.blobs, self.rows)
        self.saved = (gap.fng_dump.Catalog, gap.fng_dump.rows_for)
        gap.fng_dump.Catalog = lambda root, language="English": self.catalog
        gap.fng_dump.rows_for = stub_rows

    def tearDown(self):
        gap.fng_dump.Catalog, gap.fng_dump.rows_for = self.saved
        shutil.rmtree(self.root, ignore_errors=True)

    def run_inventory(self, **kw):
        extra = kw.pop("extra", [])
        return gap.inventory(self.root, self.root, extra, "English",
                             kw.pop("used_mode", "all"), quiet=True)

    def test_used_flags(self):
        payload = self.run_inventory()
        by = {r["stem"]: r for r in payload["packages"]}
        self.assertTrue(by["UI_TestScreen"]["used"])
        self.assertTrue(by["UI_HashedOnly"]["used"])          # hash-only reference counts
        self.assertTrue(by["UI_Commentary"]["used"])          # default mode counts the comment
        self.assertFalse(by["UI_NeverNamed"]["used"])

    def test_code_only_mode_keeps_comment_hits_out(self):
        """A package named only in a comment is not implemented; --used-mode code-only shows that."""
        # UI_Commentary.fng appears in src/only_comment.h inside a /* */ block and nowhere else.
        strict = {r["stem"]: r["used"] for r in self.run_inventory(used_mode="code-only")["packages"]}
        loose = {r["stem"]: r["used"] for r in self.run_inventory(used_mode="all")["packages"]}
        self.assertFalse(strict["UI_Commentary"])
        self.assertTrue(loose["UI_Commentary"])
        self.assertLess(sum(strict.values()), sum(loose.values()))

    def test_hits_carry_path_and_line(self):
        payload = self.run_inventory()
        rec = [r for r in payload["packages"] if r["stem"] == "UI_TestScreen"][0]
        self.assertTrue(rec["hits"])
        for hit in rec["hits"]:
            self.assertRegex(hit["path"], r"^src/")
            self.assertIsInstance(hit["line"], int)
            self.assertIn(hit["kind"], ("code", "comment"))
            self.assertIn(hit["by"], ("name", "hash", "name+hash"))

    def test_duplicate_occurrence_reuses_the_analysis(self):
        payload = self.run_inventory()
        dups = [r for r in payload["packages"] if r["stem"] == "UI_TestScreen"]
        self.assertEqual(len(dups), 2)
        self.assertIsNone(dups[0]["duplicate_of"])
        self.assertEqual(dups[1]["duplicate_of"], dups[0]["file"])
        self.assertEqual(dups[1]["node_count"], dups[0]["node_count"])

    def test_totals_and_class_counts(self):
        payload = self.run_inventory()
        t = payload["totals"]
        self.assertEqual(t["occurrences"], 5)
        self.assertEqual(t["distinct_packages"], 4)
        self.assertEqual(sum(c["distinct"] for c in payload["counts_by_class"].values()), 4)
        self.assertEqual(sum(c["occurrences"] for c in payload["counts_by_class"].values()), 5)
        self.assertEqual(t["used"] + t["unused"], t["distinct_packages"])

    def test_json_shape(self):
        payload = self.run_inventory()
        rec = payload["packages"][0]
        required = {"package", "stem", "file", "offset", "node_count", "text_node_count",
                    "image_node_count", "group_node_count", "language_keys", "placeholders",
                    "class", "class_rule", "in_scope", "milestone", "size", "used", "hit_count",
                    "hits", "hits_truncated", "duplicate_of"}
        self.assertTrue(required <= set(rec), sorted(required - set(rec)))
        self.assertIsInstance(rec["language_keys"], list)
        self.assertIsInstance(rec["in_scope"], bool)
        self.assertIn(rec["class"], gap.CLASSES)
        text = json.dumps(payload)                     # the payload must serialise
        self.assertIn("packages", json.loads(text))
        for key in ("data_root", "repo", "language", "used_mode", "extra_bundles", "notes",
                    "totals", "counts_by_class", "packages"):
            self.assertIn(key, payload)

    def test_language_keys_exclude_placeholders(self):
        payload = self.run_inventory()
        rec = [r for r in payload["packages"] if r["stem"] == "UI_TestScreen"][0]
        self.assertEqual(rec["language_keys"], ["Continue"])
        self.assertEqual(rec["placeholders"], ["$JOY_EVENT_FENG_HELP$"])

    def test_unreadable_package_is_noted_not_fatal(self):
        self.rows["UI_HashedOnly.fng"] = None
        payload = self.run_inventory()
        self.assertEqual([r["stem"] for r in payload["packages"]].count("UI_HashedOnly"), 0)
        self.assertTrue(any("not readable" in n for n in payload["notes"]))

    def test_missing_data_root_raises(self):
        gap.fng_dump.Catalog = lambda root, language="English": (_ for _ in ()).throw(
            fng_dump.FngError("no package bundle found"))
        with self.assertRaises(gap.GapError):
            gap.inventory(self.root, self.root, [], "English", "all", quiet=True)

    def test_extra_bundle_missing_raises(self):
        with self.assertRaises(gap.GapError):
            gap.extra_bundles(self.root, ["GLOBAL/NoSuchBundle.bun"])


class CliTest(unittest.TestCase):
    """The CLI contract: exit codes and the --json file, on a stubbed catalogue."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="parity-gap-cli-")
        write_source(self.root, synthetic_source())
        rows = {"UI_TestScreen.fng": [fake_row(0, 2, "Continue", "Continue")]}
        blobs = [("FRONTEND/Fake.lzc", 0x100, 0x30203, "UI_TestScreen.fng", b"a")]
        catalog = StubCatalog(blobs, rows)
        self.saved = (gap.fng_dump.Catalog, gap.fng_dump.rows_for, sys.stdout)
        gap.fng_dump.Catalog = lambda root, language="English": catalog
        gap.fng_dump.rows_for = stub_rows
        sys.stdout = open(os.devnull, "w")          # keep the report off the test output

    def tearDown(self):
        sys.stdout.close()
        gap.fng_dump.Catalog, gap.fng_dump.rows_for, sys.stdout = self.saved
        shutil.rmtree(self.root, ignore_errors=True)

    def test_main_writes_json_and_returns_zero(self):
        out = os.path.join(self.root, "gap.json")
        rc = gap.main(["--data", self.root, "--repo", self.root, "--json", out, "--quiet"])
        self.assertEqual(rc, 0)
        with open(out, encoding="utf-8") as handle:
            payload = json.load(handle)
        self.assertEqual(payload["totals"]["occurrences"], 1)
        self.assertEqual(len(payload["packages"]), 1)

    def test_filters(self):
        rc = gap.main(["--data", self.root, "--repo", self.root, "--quiet",
                       "--class", "ingame-hud"])
        self.assertEqual(rc, 0)

    def test_bad_repo_returns_two(self):
        sys.stderr = open(os.devnull, "w")          # the error line belongs to the test, not you
        try:
            rc = gap.main(["--data", self.root, "--repo", os.path.join(self.root, "nowhere"),
                           "--quiet"])
        finally:
            sys.stderr.close()
            sys.stderr = sys.__stderr__
        self.assertEqual(rc, 2)

    def test_script_compiles_and_advertises_usage(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "parity_gap.py")
        result = subprocess.run([sys.executable, "-I", path, "--help"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0)
        self.assertIn("--repo", result.stdout.decode())
        self.assertIn("--extra-bundle", result.stdout.decode())


if __name__ == "__main__":
    unittest.main()
