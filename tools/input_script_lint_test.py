#!/usr/bin/env python3
"""Unit tests for input_script_lint.py.  python3 -I tools/input_script_lint_test.py -v

Every script here is written inside the test, in a temporary directory: nothing is read from
reports/, and nothing that was made from retail data is involved.

Two kinds of expectation live here:
  * the grammar cases, which come from src/input_script.c and from tools/input_script_test.c,
    the C harness the project already runs (`make input-script-test`);
  * the lint cases, which follow what main.c does with a parsed script.
"""

import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import input_script_lint as L

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "input_script_lint.py")

# A script that should raise nothing at all; every warning test adds one fault to this.
CLEAN = "10:key=Return,20:shot=a.png,30:tdown=gas,40:tup=gas,50:quit"


def kinds(text):
    return [(e.frame, e.kind, e.value) for e in L.parse_script(text)]


def warn(text):
    return [w.message for w in L.lint(L.parse_script(text))]


class TestEventKinds(unittest.TestCase):
    """Every command the parser supports parses, with the frame and value it should keep."""

    def test_every_kind(self):
        text = ("10:key=Return 11:pad=a 12:click=0.5:0.25 13:fdown=2:0.1:0.9 14:fup=2 "
                "15:tdown=gas 16:tup=shift_up 17:shot=/tmp/a.png 18:quit")
        self.assertEqual(kinds(text), [
            (10, "key", "Return"), (11, "pad", "a"), (12, "click", "0.5:0.25"),
            (13, "fdown", "2:0.1:0.9"), (14, "fup", "2"), (15, "tdown", "gas"),
            (16, "tup", "shift_up"), (17, "shot", "/tmp/a.png"), (18, "quit", "")])

    def test_separators_are_interchangeable(self):
        self.assertEqual(kinds("1:key=A,2:key=B 3:key=C\t4:key=D\n5:key=E"),
                         [(n, "key", chr(ord("A") + n - 1)) for n in range(1, 6)])

    def test_frame_may_repeat_or_be_unsorted(self):
        # tools/input_script_test.c parses "5:key=A 5:key=B\n5:pad=start": repeats are legal.
        self.assertEqual(kinds("5:key=A 5:key=B\n5:pad=start"),
                         [(5, "key", "A"), (5, "key", "B"), (5, "pad", "start")])

    def test_line_numbers_point_at_the_token_that_starts_the_line(self):
        events = L.parse_script("# comment\n1:key=A\n\n2:key=B\n")
        self.assertEqual([e.line for e in events], [2, 4])

    def test_touch_button_accepts_names_and_numbers(self):
        self.assertEqual([e.a for e in L.parse_script("1:tdown=steer_l 2:tup=10 3:tdown=63")], [0, 10, 63])

    def test_shot_path_keeps_awkward_bytes(self):
        events = L.parse_script("1:shot=a#b.png 2:shot=/tmp/x-y.png")
        self.assertEqual([e.value for e in events], ["a#b.png", "/tmp/x-y.png"])
        # A space cannot be part of a path: parse_text() (:129) splits the token on it, and the
        # remainder is then not a command. A script that needs a spaced path has no way to say so.
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("2:shot=/tmp/x y.png")
        self.assertEqual(str(caught.exception), "bad command 'y.png' (expected FRAME:what)")


class TestParserRejections(unittest.TestCase):
    """Each rejection is_parse() makes is a ParseError here, carrying the same message.

    The cases marked with a comment are the ones tools/input_script_test.c already asserts.
    """

    CASES = [
        # An empty *text* gets "input script has no commands" (input_script.c:165). The other
        # empty message, "empty input script" (:143), is a check on the spec string itself, so it
        # belongs to the spec layer and is pinned in TestSpecForms instead.
        ("", "input script has no commands"),
        ("key=Return", "bad command 'key=Return' (expected FRAME:what)"),      # CHECK(bad(...))
        ("x:key=Return", "bad command 'x:key=Return' (expected FRAME:what)"),
        ("-1:key=Return", "bad command '-1:key=Return' (expected FRAME:what)"),
        ("1:key=NoSuchKeyName", "unknown key name 'NoSuchKeyName'"),
        ("1:key=", "empty value in '1:key'"),                                  # no '=' in the text
        ("1:pad=nope", "unknown pad button 'nope'"),
        ("1:pad=lefttrigger", "unknown pad button 'lefttrigger'"),
        ("1:click=0.5", "bad click position '0.5' (expected X:Y)"),
        ("1:click=2:0.5", "click position '2:0.5' out of 0..1"),
        ("1:click=0.5:-0.1", "click position '0.5:-0.1' out of 0..1"),
        ("1:click=1.0000001:0.5", "click position '1.0000001:0.5' out of 0..1"),
        ("1:fdown=1:0.5", "bad fdown '1:0.5' (expected ID:X:Y)"),
        ("1:fup=x", "bad fup id 'x'"),
        ("1:fup=1x", "bad fup id '1x'"),
        ("1:tdown=nobutton", "unknown touch button 'nobutton'"),
        ("1:tdown=GAS", "unknown touch button 'GAS'"),                         # strcmp, not strcasecmp
        ("1:tdown=64", "unknown touch button '64'"),
        ("1:frobnicate=3", "unknown command 'frobnicate'"),
        ("1:quit=1", "unknown command 'quit'"),
        # "quit" is strcmp'ed BEFORE the '=' is required (:58-62), so only an exact "quit" escapes
        # the missing-'=' message; "QUIT" gets the generic one, not "unknown command".
        ("1:QUIT", "bad command '1:QUIT' (missing '=')"),
        ("1:shot", "bad command '1:shot' (missing '=')"),
        ("1:key=A,2:bogus", "bad command '2:bogus' (missing '=')"),            # first error wins
        ("1:shot=" + "x" * 192, "shot path too long: '%s'" % ("x" * 192)),
    ]

    def test_rejections(self):
        for text, expected in self.CASES:
            with self.assertRaises(L.ParseError, msg=repr(text)) as caught:
                L.parse_script(text)
            self.assertEqual(str(caught.exception), expected, msg=repr(text))

    def test_fdown_shape_is_read_left_to_right_like_the_c_chain(self):
        # input_script.c:90-96 parses ID, ':', float, ':', float, each float taking its maximal
        # prefix, so "1:2:0.5" is a *well-shaped* fdown whose x is out of range -- the shape check
        # only fires when a field is missing or has leftovers.
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("1:fdown=1:2:0.5")
        self.assertEqual(str(caught.exception), "fdown position '1:2:0.5' out of 0..1")
        for text in ("1:fdown=1:0.5:0.5:extra", "1:fdown=a:0.5:0.5", "1:fdown=1:0.5:",
                     "1:fdown=+1:0.5:0.5"):        # parse_long wants a leading digit, no sign
            with self.assertRaises(L.ParseError, msg=text) as caught:
                L.parse_script(text)
            self.assertEqual(str(caught.exception),
                             "bad fdown '%s' (expected ID:X:Y)" % text.split("=", 1)[1], msg=text)
        # strtod takes ".5" and "+0.5"; the click chain is the same shape minus the id.
        self.assertEqual(kinds("1:click=.5:0.5 2:click=+0.5:0.5"), [(1, "click", ".5:0.5"),
                                                                   (2, "click", "+0.5:0.5")])
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("1:click=0.5:0.5:0.5")
        self.assertEqual(str(caught.exception), "bad click position '0.5:0.5:0.5' (expected X:Y)")

    def test_whitespace_only_script_has_no_commands(self):
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("   \n\t # nothing here")
        self.assertEqual(str(caught.exception), "input script has no commands")

    def test_512_items_is_the_limit(self):
        L.parse_script(" ".join("%d:key=A" % n for n in range(1, L.MAX_ITEMS + 1)))
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script(" ".join("%d:key=A" % n for n in range(1, L.MAX_ITEMS + 2)))
        self.assertEqual(str(caught.exception), "too many commands (max 512)")

    def test_shot_path_of_191_bytes_is_fine(self):
        self.assertEqual(kinds("1:shot=" + "x" * 191), [(1, "shot", "x" * 191)])

    def test_frame_digits_saturate_like_strtol(self):
        self.assertEqual(kinds("99999999999999999999:quit"), [(L.LONG_MAX, "quit", "")])

    def test_error_carries_the_line_of_the_bad_token(self):
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("1:key=A\n2:nope=1\n")
        self.assertEqual(caught.exception.line, 2)


class TestSdlVocabulary(unittest.TestCase):
    """The two vocabularies, as measured off libSDL2 2.32.4 rather than guessed."""

    def test_key_names_are_case_insensitive(self):
        for spelling in ("Return", "RETURN", "return", "rEtUrN"):
            self.assertTrue(L._key_name_known(spelling), spelling)

    def test_underscore_is_a_space_only_after_the_retry(self):
        self.assertTrue(L._key_name_known("Keypad_1"))
        self.assertTrue(L._key_name_known("Left_Shift"))
        self.assertFalse(L._key_name_known("Keypad__1"))

    def test_names_sdl_does_not_know(self):
        for name in ("Enter", "Esc", "Spacebar", "KP_Enter", "Print Screen", "NoSuchKey", "ae80aa79"):
            self.assertFalse(L._key_name_known(name), name)

    def test_name_longer_than_name_64_is_cut(self):
        # snprintf into char name[64] (input_script.c:45) cuts the name before the lookup.
        self.assertFalse(L._key_name_known("Return" + "z" * 64))
        self.assertTrue(L._key_name_known("Return"[:63]))

    def known(self, raw):
        """Ask the tool about a name the way the C tokenizer would hand it over (bytes in)."""
        return L._key_name_known(raw.decode("utf-8", "surrogateescape"))

    def test_single_sequence_names(self):
        for name in ("a", "A", "~", "0"):
            self.assertTrue(L._key_name_known(name), name)
        self.assertFalse(L._key_name_known("ab"))     # two sequences, and not a table name
        self.assertFalse(L._key_name_known("A B"))

    def test_multi_byte_names_use_sdl_decoder_not_strict_utf8(self):
        """SDL accepts any name that is exactly ONE UTF-8 sequence whose decode is non-zero.

        Each row was read off libSDL2 2.32.4 (see TestAgainstRealSdl, which sweeps the whole
        lead-byte range against the library); the interesting ones are where that decoder differs
        from strict UTF-8 and therefore from what a port written from the RFC would do.
        """
        self.assertTrue(self.known(b"\xc3\xa9"))             # é
        self.assertTrue(self.known("你".encode("utf-8")))               # three bytes, one sequence
        self.assertTrue(self.known("\U0001F600".encode("utf-8")))       # four bytes
        self.assertTrue(self.known(b"\xc0\xaf"))             # overlong '/': SDL takes the value
        self.assertTrue(self.known(b"\xed\xa0\x80"))         # a surrogate: takes 0xD800
        self.assertTrue(self.known(b"\xf4\x90\x80\x80"))     # past U+10FFFF: takes 0x110000
        self.assertTrue(self.known(b"\xf9\x80\x80\x80"))     # 0xF8..0xFF act as four-byte leads
        self.assertFalse(self.known(b"\xc3"))                # truncated
        self.assertFalse(self.known(b"\xe4\xbd"))
        self.assertFalse(self.known(b"\xc0\x80"))            # decodes to 0 == SDLK_UNKNOWN
        self.assertFalse(self.known(b"\xe0\x80\x80"))
        self.assertFalse(self.known(b"\xf8\x80\x80\x80"))    # same: 3-bit lead mask leaves 0
        self.assertFalse(self.known(b"\x80\x80"))            # two sequences

    def test_lone_high_byte_is_a_key(self):
        self.assertTrue(L._key_name_known(bytes([0x80]).decode("utf-8", "surrogateescape")))
        self.assertFalse(L._key_name_known(bytes([0xff]).decode("utf-8", "surrogateescape")))

    def test_pad_buttons(self):
        for name in ("a", "A", "dpup", "DPUP", "leftstick", "touchpad", "paddle4"):
            self.assertTrue(L._fold(name) in L._PAD_LOOKUP, name)
        for name in ("nope", "lefttrigger", "righttrigger", "dp_up", "invalid", "backspace"):
            self.assertFalse(L._fold(name) in L._PAD_LOOKUP, name)

    def test_touch_control_names_are_case_sensitive(self):
        self.assertEqual(L.TOUCH_CONTROLS.index("gas"), 2)
        self.assertIsNone(L._touch_button("Gas"))
        self.assertEqual(L._touch_button("2"), 2)
        self.assertIsNone(L._touch_button("64"))
        self.assertIsNone(L._touch_button("12x"))


class TestWarnings(unittest.TestCase):
    """Every warning fires on a minimal script and none of them fires on CLEAN."""

    def setUp(self):
        self.assertEqual(warn(CLEAN), [], "the baseline script must be clean")

    def test_frames_not_ascending(self):
        # Both facts are worth saying: the order, and the consequence -- key@300 can never fire
        # because the run quits at 250 (main.c:6825).
        self.assertEqual(sorted(warn("300:key=Return,250:quit")), sorted([
            "frames not ascending: frame 250 is listed after frame 300",
            "frame 300 is never reached: the run quits at frame 250",
        ]))

    def test_an_event_on_the_quit_frame_itself_is_not_warned_about(self):
        # main.c:6825 sets running = 0 after the frame counter reaches quit, and main.c:6875
        # still pulls that frame's events, so a same-frame event does happen.
        self.assertEqual(warn("250:key=Return 250:quit"), [])

    def test_ascending_frames_do_not_warn(self):
        self.assertEqual(warn("250:key=Return,300:quit"), [])

    def test_press_never_released(self):
        found = warn("10:tdown=gas,20:quit")
        self.assertEqual([m for m in found if "never released" in m],
                         ["tdown=gas at frame 10 is never released: no tup=gas comes after it"])

    def test_release_without_press(self):
        found = warn("10:tup=gas,20:quit")
        self.assertEqual([m for m in found if "releases nothing" in m],
                         ["tup=gas at frame 10 releases nothing: no tdown=gas comes before it"])

    def test_press_and_release_across_frames_pairs_up(self):
        self.assertEqual(warn("10:tdown=gas 20:tup=gas 30:quit"), [])
        # The same control pressed twice and released once leaves one press open.
        self.assertEqual(len([m for m in warn("1:tdown=gas 2:tdown=gas 3:tup=gas 4:quit")
                             if "never released" in m]), 1)

    def test_press_released_on_the_same_frame_is_fine(self):
        self.assertEqual([m for m in warn("10:tdown=gas 10:tup=gas 20:quit")
                          if "released" in m or "releases" in m], [])

    def test_touch_index_without_a_layout(self):
        found = [m for m in warn("10:tdown=47 11:tup=47 20:quit") if "have a layout" in m]
        self.assertEqual(len(found), 2)
        self.assertIn("only touch controls 0..10 have a layout", found[0])

    def test_named_touch_controls_do_not_warn_about_layouts(self):
        self.assertEqual([m for m in warn("10:tdown=shift_up 11:tup=shift_up 20:quit")
                          if "have a layout" in m], [])

    def test_same_shot_file_twice(self):
        found = warn("10:shot=a.png 20:shot=a.png 30:quit")
        self.assertEqual(len(found), 1)
        self.assertIn("shot=a.png writes the same file as the shot at frame 10", found[0])

    def test_different_shot_files_do_not_warn(self):
        self.assertEqual(warn("10:shot=a.png 20:shot=b.png 30:quit"), [])

    def test_events_after_quit(self):
        found = warn("10:quit 20:key=Return")
        self.assertEqual(found, ["frame 20 is never reached: the run quits at frame 10"])

    def test_events_on_the_quit_frame_are_not_reported(self):
        # main.c:6825 stops at the end of the iteration, so that frame's own events still fire.
        self.assertEqual(warn("10:key=Return 10:quit"), [])

    def test_no_quit(self):
        found = warn("10:key=Return")
        self.assertEqual(len(found), 1)
        self.assertIn("no quit event", found[0])

    def test_out_of_range_click_is_a_parse_error_not_a_warning(self):
        # The brief lists this under warnings, but is_parse refuses it (input_script.c:88), so the
        # game never starts: reporting it as "warning, run continues" would be untrue.
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script("10:click=2:0.5")
        self.assertEqual(str(caught.exception), "click position '2:0.5' out of 0..1")
        with self.assertRaises(L.ParseError):
            L.parse_script("10:fdown=1:2:2")

    def test_nan_passes_the_range_check_like_c_does(self):
        # C's `x < 0 || x > 1` is false for NaN, so strtod's NaN gets through.
        self.assertEqual(kinds("10:click=nan:nan"), [(10, "click", "nan:nan")])

    def test_warnings_are_ordered_by_line(self):
        events = L.parse_script("300:key=Return\n250:shot=a.png\n250:shot=a.png\n400:tdown=gas\n")
        lines = [w.line for w in L.lint(events)]
        self.assertEqual(lines, sorted(lines))


class TestTimeline(unittest.TestCase):
    """--timeline for a three-event script, at the frame rate the brief's example uses."""

    def test_three_events_at_60_fps(self):
        events = L.parse_script("250:tdown=shift_up,500:shot=x.png,925:quit")
        self.assertEqual(L.timeline(events, 60.0), [
            "  250  4.17 s  tdown=shift_up",
            "  500  8.33 s  shot=x.png",
            "  925  15.42 s  quit"])

    def test_another_frame_rate(self):
        events = L.parse_script("30:key=A 60:quit")
        self.assertEqual(L.timeline(events, 30.0),
                         ["   30  1.00 s  key=A", "   60  2.00 s  quit"])

    def test_events_sharing_a_frame_share_a_line(self):
        events = L.parse_script("5:key=A 5:key=B 9:quit")
        self.assertEqual(L.timeline(events, 60.0),
                         ["    5  0.08 s  key=A, key=B", "    9  0.15 s  quit"])

    def test_frames_are_printed_in_time_order(self):
        events = L.parse_script("9:quit 4:key=A")
        self.assertEqual([line.split("s")[1].strip() for line in L.timeline(events, 60.0)],
                         ["key=A", "quit"])


class TestSpecForms(unittest.TestCase):
    """An inline spec and an @file with the same text parse to the same events."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="input-lint-test-")
        self.addCleanup(self._clean)

    def _clean(self):
        for name in os.listdir(self.dir):
            os.unlink(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def _write(self, text, name="s.txt", mode="wb"):
        path = os.path.join(self.dir, name)
        with open(path, mode) as handle:
            handle.write(text if isinstance(text, bytes) else text.encode())
        return path

    def test_inline_and_at_file_agree(self):
        text = "# a comment\n10:key=Return 20:quit\n"
        path = self._write(text)
        inline, display, empty = L.read_spec(text.strip("\n"))
        at_file, at_display, at_empty = L.read_spec("@" + path)
        self.assertFalse(empty)
        self.assertFalse(at_empty)
        self.assertEqual(kinds(inline), kinds(at_file))
        self.assertEqual(at_display, path)
        self.assertEqual(display, "inline")

    def test_a_bare_path_is_inline_text_not_a_file(self):
        # is_parse() opens a file only when spec[0] == '@' (input_script.c:144). Reading a bare
        # path as a file would make the linter clean where the game says "bad command".
        path = self._write("1:key=A\n2:quit\n")
        text, display, _empty = L.read_spec(path)
        self.assertEqual(display, "inline")
        self.assertEqual(text, path)
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script(text)
        self.assertEqual(str(caught.exception),
                         "bad command '%s' (expected FRAME:what)" % path)
        self.assertEqual(kinds(L.read_spec("@" + path)[0]), [(1, "key", "A"), (2, "quit", "")])

    def test_main_offers_the_at_form_for_a_path_it_refused(self):
        # Refusing like the game is right, but silent is unhelpful: main() says, on stderr only,
        # that the argument is a file and what to pass instead. stdout keeps just the verdict, so
        # a gate script's grep is unaffected.
        path = self._write("1:key=A\n2:quit\n")
        proc = subprocess.run([sys.executable, "-I", TOOL, path], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 2)
        self.assertIn("error: bad command", proc.stdout)
        self.assertNotIn("is a file on disk", proc.stdout)
        self.assertIn("is a file on disk", proc.stderr)
        self.assertIn("@" + path, proc.stderr)

    def test_comments_and_delimiters_survive_a_file(self):
        path = self._write("# header\n1:key=Up  # trailing\n\n2:quit\n")
        events = L.parse_script(L.read_spec("@" + path)[0])
        self.assertEqual([(e.kind, e.line) for e in events], [("key", 2), ("quit", 4)])

    def test_comment_runs_to_a_newline_only(self):
        # parse_text() (:127) clears the comment flag on '\n' and on nothing else, and '\r' is
        # merely a delimiter (:129), so a file with bare CR endings loses everything after the
        # first '#' -- here both events behind it, without an error of any kind.
        self.assertEqual(kinds("1:key=A\r#comment\r2:quit"), [(1, "key", "A")])
        self.assertEqual(kinds("1:key=A\n#comment\r2:quit"), [(1, "key", "A")])
        self.assertEqual(kinds("1:key=A\n#comment\n2:quit"), [(1, "key", "A"), (2, "quit", "")])

    def test_hash_inside_a_token_is_text(self):
        self.assertEqual(kinds("1:shot=a#b.png"), [(1, "shot", "a#b.png")])

    def test_nul_byte_ends_the_script(self):
        path = self._write(b"1:key=A\x002:key=B\n")
        self.assertEqual(kinds(L.read_spec("@" + path)[0]), [(1, "key", "A")])

    def test_missing_file(self):
        with self.assertRaises(L.ParseError) as caught:
            L.read_spec("@/nonexistent/dir/script.txt")
        self.assertEqual(str(caught.exception), "cannot open script file '/nonexistent/dir/script.txt'")

    def test_file_bigger_than_the_parser_reads(self):
        path = self._write(b"1:key=A\n" + b"# pad it out\n" * 200000)
        self.assertGreater(os.path.getsize(path), L.FILE_MAX)
        with self.assertRaises(L.ParseError) as caught:
            L.read_spec("@" + path)
        self.assertEqual(str(caught.exception), "script file '%s' too large or unreadable" % path)

    def test_an_empty_spec_and_an_empty_file_say_different_things(self):
        # is_parse() tests the spec *string* for emptiness at :143, before opening anything, so an
        # empty inline argument says "empty input script". An empty file is a non-empty spec
        # ("@path"), gets as far as parse_text, comes back with no commands, and says
        # "input script has no commands" (:165). Collapsing the two would hide which one the game
        # would have complained about -- and "@" alone is a spec that names the empty path.
        out = io.StringIO()
        self.assertEqual(L.check("", 60.0, out, False)[0], 1)
        self.assertIn("empty input script", out.getvalue())
        path = self._write("")
        out = io.StringIO()
        self.assertEqual(L.check("@" + path, 60.0, out, False)[0], 1)
        self.assertIn("input script has no commands", out.getvalue())

    def test_empty_file_has_no_commands(self):
        path = self._write("")
        with self.assertRaises(L.ParseError) as caught:
            L.parse_script(L.read_spec("@" + path)[0])
        self.assertEqual(str(caught.exception), "input script has no commands")


class TestScanHeuristic(unittest.TestCase):
    """--scan has to tell scripts from the gate logs that live beside them."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="input-lint-scan-")
        self.addCleanup(self._clean)

    def _clean(self):
        for root, _dirs, files in os.walk(self.dir, topdown=False):
            for name in files:
                os.unlink(os.path.join(root, name))
            os.rmdir(root)

    def _write(self, text, name):
        path = os.path.join(self.dir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_a_script_is_found(self):
        self.assertTrue(L.looks_like_script("10:key=Return\n20:quit\n")[0])

    def test_prose_and_gate_logs_are_not_scripts(self):
        log = ("race-ai-test    PASS    exit=0    38s  xvfb+llvmpipe\n"
               "4201: race started\n4202: lap 1 done\n")
        self.assertFalse(L.looks_like_script(log)[0])

    def test_a_typoed_verb_is_still_a_script(self):
        # "kay=" is unknown to the parser; the file must still be linted, not skipped.
        self.assertTrue(L.looks_like_script("10:kay=Return\n20:quit\n")[0])

    def test_a_file_of_only_typoed_verbs_is_still_a_script(self):
        # Shape is the only signal, on purpose: this file names no command at all, and it is
        # exactly the file a linter should refuse to shrug at. The alternative rule (also require
        # a known verb) selected the same 63 files over the 419 under reports/, so it only ever
        # cost files like this one -- see looks_like_script().
        self.assertTrue(L.looks_like_script("10:kay=Return\n20:quiat\n")[0])

    def test_empty_file_is_not_a_script(self):
        self.assertEqual(L.looks_like_script("# nothing\n"), (False, "no commands"))

    def test_scan_lists_txt_files_sorted(self):
        self._write("1:quit\n", "b.txt")
        self._write("1:quit\n", "a.txt")
        self._write("not a script\n", "notes.md")
        found = L.scan_dir(self.dir)
        self.assertEqual([os.path.basename(p) for p in found], ["a.txt", "b.txt"])

    def test_end_to_end_scan_reports_counts(self):
        self._write("10:key=Return\n20:quit\n", "good.txt")
        self._write("300:key=Return\n250:quit\n", "unsorted.txt")
        self._write("4201: race started\n", "gate-log.txt")
        self._write("1:bogus=1\n", "broken.txt")
        proc = self._run(["--scan", self.dir])
        self.assertIn("unsorted.txt:2: warning: frames not ascending", proc.stdout)
        self.assertIn("broken.txt:1: error: unknown command 'bogus'", proc.stdout)
        self.assertNotIn("gate-log.txt", proc.stdout)
        self.assertIn("scan: 3 of 4 .txt file(s)", proc.stdout)
        self.assertEqual(proc.returncode, 2)   # a parse error outranks warnings

    def _run(self, argv):
        return subprocess.run([sys.executable, "-I", TOOL] + argv, capture_output=True,
                              text=True, cwd=self.dir)


class TestCommandLine(unittest.TestCase):
    """Exit status is what a gate script branches on: 0 clean, 1 warnings, 2 refused or misused."""

    def _run(self, argv, cwd=None):
        return subprocess.run([sys.executable, "-I", TOOL] + argv, capture_output=True,
                              text=True, cwd=cwd or HERE)

    def test_clean_script_is_zero(self):
        proc = self._run([CLEAN])
        self.assertEqual((proc.returncode, proc.stdout.strip()), (0, ""))

    def test_warning_is_one(self):
        proc = self._run(["10:key=Return"])
        self.assertEqual(proc.returncode, 1)
        self.assertIn("no quit event", proc.stdout)

    def test_parse_error_is_two(self):
        proc = self._run(["1:nope=1"])
        self.assertEqual(proc.returncode, 2)
        self.assertEqual(proc.stdout, "inline:1: error: unknown command 'nope'\n")

    def test_usage_errors_are_two(self):
        for argv in ([], ["--fps", "0", CLEAN], ["--scan", "/nonexistent-dir-xyz"],
                     ["--scan", HERE, CLEAN]):
            proc = self._run(argv)
            self.assertEqual(proc.returncode, 2, msg="%r -> %r" % (argv, proc.stderr))
            self.assertTrue(proc.stderr.strip(), msg=repr(argv))

    def test_every_argument_is_reported(self):
        # Warnings in the first script must not stop the second one being checked, and a parse
        # error in the second must not stop the first one's warnings being printed.
        proc = self._run(["1:tdown=gas,2:quit", "1:nope=1"])
        self.assertEqual(proc.returncode, 2)
        self.assertIn("never released", proc.stdout)
        self.assertIn("unknown command 'nope'", proc.stdout)

    def test_timeline_on_a_file(self):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
            handle.write("250:tdown=shift_up\n500:shot=x.png\n925:quit\n")
            path = handle.name
        try:
            proc = self._run(["@" + path, "--timeline"])
            self.assertEqual(proc.returncode, 1)   # tdown is never released
            self.assertIn("  250  4.17 s  tdown=shift_up", proc.stdout)
        finally:
            os.unlink(path)


class TestAgainstCHarness(unittest.TestCase):
    """Differential check against the project's own C harness, tools/input_script_test.c.

    That file is the closest thing to an oracle this task is allowed: it states, in C, what
    is_parse() must do, and the tree's built copy of it (build/input_script_test, newer than the
    src/input_script.c this port was read from) reports "48 checks, 0 failures" when run -- so the
    parser really does behave as its cases say. This test compiles and runs nothing: it reads the
    case strings out of the harness SOURCE, so the list cannot drift away from it, and answers each
    one here. Agreement on these inputs, plus the harness passing on the C side, is what stands
    between this port and a claim of equivalence with the game.
    """

    HARNESS = os.path.join(HERE, "input_script_test.c")

    # Item counts the harness asserts with CHECK(s.n == N) next to the parse.
    COUNTS = {
        "10:key=Return,12:pad=a,20:click=0.5:0.25,30:fdown=2:0.1:0.9,31:fup=2,"
        "40:tdown=gas,41:tup=2,50:shot=/tmp/a.png,60:quit": 9,
        "5:key=A 5:key=B\n5:pad=start": 3,
        "3:key=Left_Shift": 1,
    }

    def setUp(self):
        if not os.path.exists(self.HARNESS):
            self.skipTest("the C harness is not next to this test")
        with open(self.HARNESS, encoding="utf-8") as handle:
            self.source = handle.read()

    @staticmethod
    def _unescape(text):
        return text.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")

    # Exactly as the harness writes them; nothing is stitched together, because both forms were
    # mis-stitched once while writing this file.
    REJECTS = 'CHECK(bad("'
    ACCEPTS = 'CHECK(is_parse(&s, "'

    def literals(self, prefix):
        """Spec literals handed to CHECK(bad("...")) or CHECK(is_parse(&s, "..."))."""
        pattern = re.escape(prefix) + r'((?:[^"\\]|\\.)*)"'
        return [self._unescape(text) for text in re.findall(pattern, self.source)]

    def test_the_harness_cases_are_still_here_to_read(self):
        # A silent regex miss would leave the two tests below looping over nothing and reporting
        # success, so the counts the harness source holds today are pinned here.
        self.assertEqual(len(self.literals(self.REJECTS)), 15, "expected 15 rejection cases")
        self.assertEqual(len(self.literals(self.ACCEPTS)), 3, "expected 3 accepted specs")

    def test_every_spec_the_harness_rejects_is_a_parse_error_here(self):
        for spec in self.literals(self.REJECTS):
            with self.assertRaises(L.ParseError, msg=repr(spec)):
                L.parse_script(spec)

    def test_every_spec_the_harness_accepts_parses_here_with_the_same_item_count(self):
        for spec in self.literals(self.ACCEPTS):
            events = L.parse_script(spec)
            self.assertEqual(len(events), self.COUNTS[spec], msg=repr(spec))

    def test_the_rejections_carry_c_wording_not_ours(self):
        # The harness only checks that an error came back, not its text, so the wording is pinned
        # here against src/input_script.c rather than against the harness.
        WORDING = [
            ("key=Return", "bad command 'key=Return' (expected FRAME:what)"),
            ("1:key=", "empty value in '1:key'"),
            ("1:frobnicate=3", "unknown command 'frobnicate'"),
            ("1:key=A,2:bogus", "bad command '2:bogus' (missing '=')"),
        ]
        for spec, expected in WORDING:
            with self.assertRaises(L.ParseError, msg=spec) as caught:
                L.parse_script(spec)
            self.assertEqual(str(caught.exception), expected, msg=spec)

    def test_the_frame_semantics_the_harness_shows_are_the_ones_the_warnings_assume(self):
        # CHECK(is_events(&s, 6, ...) == 3 && ev[0].type == SDL_KEYUP) in the harness: a press at
        # frame 5 releases on frame 6, and CHECK(!is_quit(&s, 59) && is_quit(&s, 60) &&
        # is_quit(&s, 61)): the quit frame is the last one that does anything. Both are what
        # lint() encodes -- a press with no later release is a real dangling press, and an event
        # past the quit frame can never fire.
        events = L.parse_script("5:key=A 5:key=B\n5:pad=start")
        self.assertEqual([e.frame for e in events], [5, 5, 5])
        self.assertEqual(warn("5:tdown=gas,10:quit"), [
            "tdown=gas at frame 5 is never released: no tup=gas comes after it"])
        # 10 -> 20 is ascending, so the only fault is that frame 20 can never arrive.
        self.assertEqual(warn("10:quit,20:key=A"), [
            "frame 20 is never reached: the run quits at frame 10"])


class TestAgainstRealSdl(unittest.TestCase):
    """Re-derive the vocabulary from libSDL2 on this machine and diff it against the tables.

    ctypes is stdlib, so the tool stays stdlib-only. Skipped when no libSDL2 is present, or when
    the one that is present is not the 2.32.4 build this tree links -- the tables are pinned to it.
    """

    CANDIDATES = [os.environ.get("OPENUG2_SDL2_LIB"),
                  os.path.join(os.environ["OPENUG2_DEV"], "sysroot/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0")
                  if os.environ.get("OPENUG2_DEV") else None]

    @classmethod
    def setUpClass(cls):
        import ctypes
        import ctypes.util
        for guess in cls.CANDIDATES + [ctypes.util.find_library("SDL2")]:
            if not guess:
                continue
            try:
                lib = ctypes.CDLL(guess)
                lib.SDL_GetKeyFromName
            except (OSError, AttributeError):
                continue
            cls.lib = lib
            lib.SDL_GetKeyFromName.restype = ctypes.c_int32
            lib.SDL_GetKeyFromName.argtypes = [ctypes.c_char_p]
            lib.SDL_GetKeyName.restype = ctypes.c_char_p
            lib.SDL_GetKeyName.argtypes = [ctypes.c_int32]
            lib.SDL_GetScancodeName.restype = ctypes.c_char_p
            lib.SDL_GetScancodeName.argtypes = [ctypes.c_int32]
            lib.SDL_GameControllerGetStringForButton.restype = ctypes.c_char_p
            lib.SDL_GameControllerGetStringForButton.argtypes = [ctypes.c_int]
            lib.SDL_GameControllerGetButtonFromString.restype = ctypes.c_int
            lib.SDL_GameControllerGetButtonFromString.argtypes = [ctypes.c_char_p]

            class Version(ctypes.Structure):
                _fields_ = [("major", ctypes.c_ubyte), ("minor", ctypes.c_ubyte),
                            ("patch", ctypes.c_ubyte)]

            ver = Version()
            lib.SDL_GetVersion(ctypes.byref(ver))
            if (ver.major, ver.minor, ver.patch) != (2, 32, 4):
                raise unittest.SkipTest("tables are pinned to SDL 2.32.4, found %d.%d.%d"
                                        % (ver.major, ver.minor, ver.patch))
            cls.uses_ctypes = ctypes
            return
        raise unittest.SkipTest("no libSDL2 to check the tables against")

    def known(self, raw):
        return self.lib.SDL_GetKeyFromName(raw) != 0

    def derived_names(self):
        """Every name the library will report for a key code or a scan code."""
        out = set()
        codes = list(range(1, 0x400)) + list(range(0x40000000, 0x40000300)) + list(range(1, 512))
        for code in codes:
            for raw in (self.lib.SDL_GetKeyName(code), self.lib.SDL_GetScancodeName(code & 0x3FFFFFF)):
                if raw:
                    out.add(raw.decode("utf-8", "surrogateescape"))
        return {n for n in out if self.known(n.encode("utf-8", "surrogateescape"))}

    def test_every_embedded_name_is_accepted_by_sdl(self):
        bad = [n for n in L.SDL_KEY_NAMES if not self.known(n.encode("utf-8", "surrogateescape"))]
        self.assertEqual(bad, [], "the table lists names SDL does not know")

    def test_every_name_sdl_reports_is_accepted_by_the_tool(self):
        missing = []
        for name in sorted(self.derived_names()):
            if len(name) == 1:
                continue      # one-code-point names are the rule's job, checked below
            if not L._key_name_known(name.replace(" ", "_")):
                missing.append(name)
        self.assertEqual(missing, [], "SDL accepts these but the tool would reject them")

    def test_single_byte_names_agree_with_sdl(self):
        for byte in range(1, 256):
            raw = bytes([byte])
            expected = self.known(raw)
            spelling = raw.decode("utf-8", "surrogateescape")
            self.assertEqual(L._key_name_known(spelling), expected,
                             msg="byte 0x%02X: SDL says %s" % (byte, expected))

    def test_the_utf8_fallback_agrees_with_sdl_over_every_lead_byte(self):
        """1275 forms: each lead byte against 0..4 continuation bytes.

        This is what keeps _one_codepoint() honest. It is not a re-statement of the UTF-8 rule,
        it is SDL's own decoder -- overlong forms and values past U+10FFFF included, a decode of
        zero refused -- and the only way to know that without the source is to sweep it.
        """
        for lead in range(1, 256):
            for extra in range(0, 5):
                raw = bytes([lead]) + b"\x80" * extra
                self.assertEqual(L._key_name_known(raw.decode("utf-8", "surrogateescape")),
                                 self.known(raw),
                                 msg="%r: SDL says %s" % (raw, self.known(raw)))

    def test_pad_buttons_agree_with_sdl(self):
        for index in range(0, 24):
            name = self.lib.SDL_GameControllerGetStringForButton(index)
            if name:
                self.assertIn(name.decode(), L.SDL_PAD_BUTTON_NAMES, name)
        for name in (b"nope", b"lefttrigger", b"righttrigger", b"dp_up", b"invalid", b"backspace",
                     b"q", b"aa"):
            self.assertEqual(self.lib.SDL_GameControllerGetButtonFromString(name), -1, name)
            self.assertNotIn(L._fold(name.decode()), L._PAD_LOOKUP, name)

    def test_touch_control_names_are_what_sdl_and_tc_expect(self):
        # input_script.c:13-14 lists them; the library is not involved, but the order matters.
        self.assertEqual(len(L.TOUCH_CONTROLS), 11)
        self.assertEqual(L.TOUCH_CONTROLS[2], "gas")


if __name__ == "__main__":
    unittest.main(verbosity=2)
