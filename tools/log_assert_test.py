#!/usr/bin/env python3
"""Self-test for tools/log_assert.py.

Every log is synthetic and written into a temp dir by the test; nothing here opens a real game log.
The tool is driven the way a gate drives it - `python3 -I tools/log_assert.py ...` - so exit codes,
stderr wording and the JSON file are all covered through the same door.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "log_assert.py")


class LogAssert(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name

    # ------------------------------------------------------------------ helpers

    def log(self, name, *lines):
        """Write a log into the temp dir; return the name to pass to the tool (evidence says it back)."""
        with open(os.path.join(self.dir, name), "w") as fh:
            fh.write("".join(s + "\n" for s in lines))
        return name

    def spec(self, text, name="spec.txt"):
        """Write a spec file (verbatim, so blank and comment lines land where the test put them)."""
        with open(os.path.join(self.dir, name), "w") as fh:
            fh.write(text)
        return name

    def run_tool(self, *args):
        p = subprocess.run([sys.executable, "-I", TOOL] + list(args),
                           capture_output=True, text=True, cwd=self.dir)
        self.assertNotIn("Traceback", p.stderr, "the tool crashed:\n" + p.stderr)
        return p.returncode, p.stdout, p.stderr

    def check(self, rules, *logs):
        """Run an inline spec; returns (rc, stdout, stderr)."""
        return self.run_tool("--spec", rules, *logs)

    def tally(self, out):
        return out.strip().splitlines()[-1]

    # ------------------------------------------------------------------ expect

    def test_expect_plain(self):
        log = self.log("a.log", "boot", "career: new stage 0", "quit")
        rc, out, err = self.check("expect /career: new stage/", log)
        self.assertEqual((rc, self.tally(out)), (0, "1 rules, 1 pass, 0 fail"), err)
        rc, out, _ = self.check("expect /career: sold/", log)
        self.assertEqual((rc, self.tally(out)), (1, "1 rules, 0 pass, 1 fail"))
        self.assertIn("FAIL spec:1  expect /career: sold/", out)
        self.assertIn("found 0 match(es), wanted at least 1", out)

    def test_expect_exact_count(self):
        log = self.log("a.log", "drag shift: 1", "drag shift: 2", "drag shift: 3")
        self.assertEqual(self.check("expect 3 /drag shift:/", log)[0], 0)
        self.assertEqual(self.check("expect 2 /drag shift:/", log)[0], 1)
        self.assertEqual(self.check("expect 4 /drag shift:/", log)[0], 1)
        rc, out, _ = self.check("expect 2 /drag shift:/", log)
        self.assertIn("found 3 match(es), wanted exactly 2", out)
        # the count is per line, not per match: two hits on one line still count once
        both = self.log("two.log", "drag shift: 1 drag shift: 2")
        self.assertEqual(self.check("expect 1 /drag shift:/", both)[0], 0)

    def test_expect_at_least_and_at_most(self):
        log = self.log("a.log", "x 1", "x 2", "x 3")
        for rules, want in (("expect >=3 /x /", 0), ("expect >=4 /x /", 1), ("expect >=0 /nope/", 0),
                            ("expect <=3 /x /", 0), ("expect <=2 /x /", 1), ("expect <=0 /nope/", 0)):
            self.assertEqual(self.check(rules, log)[0], want, rules)
        rc, out, _ = self.check("expect <=2 /x /", log)
        self.assertIn("found 3 match(es), wanted at most 2", out)

    # ------------------------------------------------------------------ forbid

    def test_forbid(self):
        clean = self.log("clean.log", "boot", "career: ok")
        crash = self.log("crash.log", "boot", "==1234==ERROR: AddressSanitizer", "career: ok")
        self.assertEqual(self.check("forbid /AddressSanitizer|Segmentation|abort/", clean)[0], 0)
        rc, out, _ = self.check("forbid /AddressSanitizer|Segmentation|abort/", crash)
        self.assertEqual(rc, 1)
        self.assertIn("found 1 match(es), wanted none", out)
        self.assertIn("crash.log:2: ==1234==ERROR: AddressSanitizer", out)

    def test_forbid_lists_only_the_first_lines(self):
        log = self.log("a.log", *[("bad line %d" % i) for i in range(1, 12)])
        rc, out, _ = self.check("forbid /bad line/", log)
        self.assertEqual(rc, 1)
        self.assertIn("found 11 match(es)", out)
        shown = [l for l in out.splitlines() if l.startswith("      ")]
        self.assertEqual(len(shown), 3, out)

    # ------------------------------------------------------------------ order

    def test_order(self):
        log = self.log("a.log", "new stage 0", "bought a car", "crib", "stage 0 -> 1")
        self.assertEqual(self.check("order /new stage/ /bought/ /crib/ /stage 0 -> 1/", log)[0], 0)
        rc, out, _ = self.check("order /crib/ /new stage/", log)
        self.assertEqual(rc, 1)
        self.assertIn("not in order", out)
        self.assertIn("a.log:1", out)
        self.assertIn("a.log:3", out)

    def test_order_needs_every_step(self):
        log = self.log("a.log", "new stage 0", "bought a car")
        rc, out, _ = self.check("order /new stage/ /bought/ /stage complete/", log)
        self.assertEqual(rc, 1)
        self.assertIn("steps missing from the log: /stage complete/", out)
        self.assertIn("/stage complete/ not found", out)

    def test_order_is_strict(self):
        # one line carries both steps: the first match of each is the same line, so nothing is after
        log = self.log("a.log", "x", "asking to race and answered Yes", "y")
        self.assertEqual(self.check("order /asking to race/ /answered Yes/", log)[0], 1)

    # ------------------------------------------------------------------ capture / equal / range

    def test_capture_feeds_equal_and_range(self):
        log = self.log("a.log", "career: lot 6 cars: 240SX FOCUS", "career: bought 240SX")
        self.assertEqual(self.check("capture N /career: lot (\\d+) cars/; equal N 6", log)[0], 0)
        self.assertEqual(self.check("capture N /career: lot (\\d+) cars/; range N 4 8", log)[0], 0)
        rc, out, _ = self.check("capture N /career: lot (\\d+) cars/; equal N 7", log)
        self.assertEqual(rc, 1)
        self.assertIn("N is '6', wanted '7'", out)
        rc, out, _ = self.check("capture N /career: lot (\\d+) cars/; range N 7 9", log)
        self.assertIn("N is 6, outside 7..9", out)
        # the capture rule itself reports what it took
        rc, out, _ = self.check("capture CAR /bought (\\S+)/", log)
        self.assertIn("CAR=240SX", out)

    def test_capture_uses_the_first_matching_line(self):
        log = self.log("a.log", "car A first", "car B second")
        rc, out, _ = self.check("capture CAR /car (\\w+)/; equal CAR A", log)
        self.assertEqual(rc, 0, out)
        rc, out, _ = self.check("capture CAR /car (\\w+)/; equal CAR B", log)
        self.assertEqual(rc, 1, out)

    def test_capture_then_regex_reference(self):
        log = self.log("a.log", "career: bought 240SX for $0", "career: engage STAGE_1_DRIFT_1",
                       "race armed: 240SX on the grid")
        self.assertEqual(self.check("capture CAR /bought (\\S+) for/; expect /race armed: {CAR}/", log)[0], 0)
        rc, out, _ = self.check("capture CAR /bought (\\S+) for/; expect /race armed: {CAR}/; "
                                "expect /winning {CAR}/", log)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL spec:3", out)

    def test_capture_escapes_metacharacters_in_the_value(self):
        log = self.log("a.log", 'caption "Short Shift!"', 'hud drew caption "Short Shift!" on screen')
        self.assertEqual(self.check('capture C /caption ("[^"]+")/; expect /{C} on screen/', log)[0], 0)
        # the dot here is log text, not a wildcard: an unescaped expansion would match this line
        loose = self.log("b.log", 'caption "X.Y Z"', 'hud drew caption "XqY Z" on screen')
        self.assertEqual(self.check('capture C /caption ("[^"]+")/; expect /{C} on screen/', loose)[0], 1)

    def test_range_needs_a_number(self):
        log = self.log("a.log", "career: bought DRIFTCAR now")
        self.assertEqual(self.check("capture C /bought (\\S+) now/; range C 1 9", log)[0], 1)
        rc, out, _ = self.check("capture C /bought (\\S+) now/; range C 1 9", log)
        self.assertIn("C is 'DRIFTCAR', which is not a number", out)
        # float bounds are inclusive and accept decimal captures
        dec = self.log("b.log", "placed at (-717.4 -902.9 17.9)")
        self.assertEqual(self.check("capture X /placed at \\((-?[\\d.]+)/; range X -718 -717", dec)[0], 0)

    def test_capture_that_misses_fails_and_leaves_the_name_unset(self):
        log = self.log("a.log", "career: lot empty")
        rc, out, _ = self.check("capture N /career: lot (\\d+) cars/; equal N 6", log)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL spec:1", out)
        self.assertIn("career: lot (\\d+) cars/ matched nothing", out)
        self.assertIn("FAIL spec:2", out)
        self.assertIn("N was never captured", out)

    # ------------------------------------------------------------------ within

    def test_within(self):
        log = self.log("a.log", "race rules live", "junk", "junk", "lap-ko live", "junk")
        self.assertEqual(self.check("within /race rules live/ /lap-ko live/ 3", log)[0], 0)
        self.assertEqual(self.check("within /race rules live/ /lap-ko live/ 2", log)[0], 1)
        self.assertEqual(self.check("within /race rules live/ /lap-ko live/ 0", log)[0], 1)
        same = self.log("b.log", "race rules live and lap-ko live")
        self.assertEqual(self.check("within /race rules live/ /lap-ko live/ 0", same)[0], 0)
        rc, out, _ = self.check("within /race rules live/ /lap-ko live/ 2", log)
        self.assertIn("first appears 3 line(s) after", out)
        self.assertIn("only 2 allowed", out)

    def test_within_uses_the_first_a_and_measures_after_it(self):
        log = self.log("a.log", "A", "junk", "B", "A", "junk", "junk", "B")
        self.assertEqual(self.check("within /A/ /B/ 2", log)[0], 0)   # A at 1, B at 3: gap 2
        self.assertEqual(self.check("within /A/ /B/ 1", log)[0], 1)
        never = self.log("b.log", "junk", "B", "A")
        rc, out, _ = self.check("within /A/ /B/ 5", never)
        self.assertEqual(rc, 1)
        self.assertIn("/B/ never appears after /A/ at b.log:3", out)

    # ------------------------------------------------------------------ several logs

    def test_logs_concatenate_in_order_with_their_own_numbers(self):
        one = self.log("1.log", "new stage 0", "bought a car")
        two = self.log("2.log", "crib", "stage 0 -> 1")
        rc, out, _ = self.check("order /new stage/ /crib/ /stage 0 -> 1/", one, two)
        self.assertEqual(rc, 0, out)
        # evidence names the file the line came from, with that file's own line number
        rc, out, _ = self.check("order /crib/ /new stage/", one, two)
        self.assertEqual(rc, 1)
        self.assertIn("1.log:1", out)
        self.assertIn("2.log:1", out)

    def test_failures_and_evidence_across_a_log_boundary(self):
        one = self.log("1.log", "boot", "career: lot 6 cars")
        two = self.log("2.log", "career: bought 240SX")
        rc, out, _ = self.check("expect 3 /career:/", one, two)
        self.assertEqual(rc, 1)
        self.assertIn("found 2 match(es)", out)
        self.assertIn("1.log:2", out)
        self.assertIn("2.log:1", out)
        # the second file alone cannot satisfy a rule that spans both
        self.assertEqual(self.check("expect 3 /career:/", one)[0], 1)

    def test_line_numbers_survive_crlf_and_a_missing_final_newline(self):
        with open(os.path.join(self.dir, "crlf.log"), "w", newline="") as fh:
            fh.write("one\r\ntwo\r\nthree")           # CRLF, and no newline on the last line
        rc, out, _ = self.check("expect /three/; forbid /two/", "crlf.log")
        self.assertEqual(rc, 1)
        self.assertIn("crlf.log:2: two", out)
        rc, out, _ = self.check("expect /two\\r/", "crlf.log")
        self.assertEqual(rc, 1, "the trailing CR is stripped before matching:\n" + out)

    # ------------------------------------------------------------------ spec wording

    def test_spec_file_line_numbers_and_comments(self):
        log = self.log("a.log", "career: new stage 0")
        path = self.spec("# the run must reach stage 0\n\nexpect /career: new stage/\n"
                         "expect /career: sold/\n")
        rc, out, err = self.run_tool(path, log)
        self.assertEqual(rc, 1)
        self.assertIn("PASS spec:3", out)
        self.assertIn("FAIL spec:4", out)
        self.assertEqual(err, "")

    def test_a_hash_inside_a_regex_is_not_a_comment(self):
        log = self.log("a.log", "issue #4212 in the log")
        self.assertEqual(self.check("expect /issue #4212/", log)[0], 0)
        self.assertEqual(self.check("expect /issue #9999/ # trailing comment", log)[0], 1)

    def test_a_semicolon_inside_a_regex_is_not_a_rule_break(self):
        log = self.log("a.log", "x=a;y=b")
        rc, out, _ = self.check("expect /x=a;y=b/; forbid /zzz/", log)
        self.assertEqual(rc, 0, out)
        self.assertEqual(len([l for l in out.splitlines() if l.startswith("PASS")]), 2)

    def test_slash_and_backslash_escapes(self):
        log = self.log("a.log", "path a/b/c", "ratio 3\\4", "xxx")
        self.assertEqual(self.check(r"expect /a\/b\/c/", log)[0], 0)
        self.assertEqual(self.check(r"expect /3\\4/", log)[0], 0)
        self.assertEqual(self.check(r"expect /a\/b.c/", log)[0], 0)    # . matches the next separator
        self.assertEqual(self.check(r"expect /x{3}/", log)[0], 0)       # {3} is a quantifier, not {NAME}
        self.assertEqual(self.check(r"expect /x\{3\}/", log)[0], 1)     # escaped braces match no brace here
        self.assertEqual(self.check(r"expect /a.b\/c|nope/", log)[0], 0)

    def test_json_stdout_and_file(self):
        log = self.log("a.log", "career: lot 6 cars", "career: boom")
        rules = "capture N /career: lot (\\d+) cars/; equal N 6; forbid /boom/; expect /missing/"
        rc, out, err = self.run_tool("--json", "--spec", rules, log)
        self.assertEqual(rc, 1, out)
        doc = json.loads(out)
        self.assertEqual(sorted(doc), ["logs", "results", "spec", "summary"])
        self.assertEqual(doc["summary"], {"rules": 4, "pass": 2, "fail": 2})
        self.assertEqual(doc["logs"], [{"log": log, "lines": 2}])
        rec = doc["results"][0]
        self.assertEqual(sorted(rec), ["capture", "detail", "evidence", "index", "kind",
                                       "rule", "spec_line", "status"])
        self.assertEqual((rec["index"], rec["spec_line"], rec["kind"], rec["status"]),
                         (1, 1, "capture", "pass"))
        self.assertEqual(rec["capture"], {"name": "N", "value": "6"})
        self.assertEqual(doc["results"][2]["status"], "fail")
        self.assertEqual(doc["results"][2]["evidence"],
                         [{"log": log, "line": 2, "text": "career: boom"}])
        self.assertNotIn("rules,", out)                     # no human tally mixed into the JSON
        with self.assertRaises(ValueError):                 # stdout is clean JSON
            json.loads(out + "junk")

        dest = os.path.join(self.dir, "res.json")
        rc, out, err = self.run_tool("--json", dest, "--spec", rules, log)
        self.assertEqual(rc, 1)
        self.assertEqual(self.tally(out), "4 rules, 2 pass, 2 fail")
        self.assertIn("FAIL", out)                          # the human report still prints
        with open(dest) as fh:
            self.assertEqual(json.load(fh)["summary"], {"rules": 4, "pass": 2, "fail": 2})

    # ------------------------------------------------------------------ bad specs and usage

    def test_bad_specs_report_spec_LINE(self):
        log = self.log("a.log", "anything")
        cases = [
            ("expect /(/", "bad regex"),                                   # unbalanced group
            ("expect /unterminated", "unterminated /regex/"),
            ("frobnicate /x/", "unknown rule"),
            ("expect", "usage: expect"),                                   # no regex at all
            ("expect 2", "usage: expect"),
            ("expect >=x /a/", "usage: expect"),
            ("order /a/", "at least two"),
            ("order /a/ extra /b/", "only /regex/ arguments"),
            ("capture /a(b)/", "usage: capture"),
            ("capture N /nogroup/", "no (group)"),
            ("capture N", "usage: capture"),
            ("equal N", "usage: equal"),
            ("range N 3", "usage: range"),
            ("range N 9 3", "above HI"),
            ("range N x y", "is not a number"),
            ("within /a/ /b/", "usage: within"),
            ("within /a/ /b/ 1.5", "usage: within"),
            ("expect /x/ extra", "unexpected"),
            ("expect /{BOGUS}x/", "before it is captured"),
            ("expect /{BOGUS}{BOGUS}/", "before it is captured"),
            ("equal GHOST x", "before it is captured"),
            ("range GHOST 1 2", "before it is captured"),
            ("expect /{B}/; capture B /a (c)/", "before it is captured"),
        ]
        for rules, frag in cases:
            rc, out, err = self.check(rules, log)
            self.assertEqual((rc, out), (2, ""), "%s -> rc=%s out=%r err=%r" % (rules, rc, out, err))
            self.assertTrue(err.startswith("spec:1: "), "%s -> %r" % (rules, err))
            self.assertIn(frag, err, rules)
            self.assertNotIn("Traceback", err)

    def test_bad_spec_names_the_physical_line(self):
        log = self.log("a.log", "anything")
        path = self.spec("# comment\n\nexpect /fine(x)/\nbogus /x/\n")
        rc, out, err = self.run_tool(path, log)
        self.assertEqual((rc, out), (2, ""))
        self.assertTrue(err.startswith("spec:4: "), err)

    def test_a_spec_with_no_rules_is_an_error_not_a_pass(self):
        log = self.log("a.log", "anything")
        for text, name in (("", "empty.txt"), ("# only a comment\n", "comments.txt"),
                           ("\n\n  \n", "blanks.txt")):
            path = self.spec(text, name)
            rc, out, err = self.run_tool(path, log)
            self.assertEqual((rc, out), (2, ""), name)
            self.assertIn("no rules", err)
        rc, out, err = self.check(";", log)
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("no rules", err)

    def test_usage_errors(self):
        log = self.log("a.log", "anything")
        rc, out, err = self.run_tool()
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("usage", err)
        rc, out, err = self.run_tool(log)                                  # a spec file is missing
        self.assertEqual((rc, out), (2, ""))
        rc, out, err = self.run_tool(self.spec("expect /anything/"))
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("LOG_FILE", err)
        rc, out, err = self.run_tool(os.path.join(self.dir, "nope.spec"), log)
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("cannot read", err)
        rc, out, err = self.check("expect /x/", os.path.join(self.dir, "nope.log"))
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("cannot read", err)
        rc, out, err = self.check("expect /x/", log, "--json", os.path.join(self.dir, "no/such.json"))
        self.assertEqual((rc, out), (2, ""))
        self.assertIn("cannot write", err)

    def test_a_missing_log_stops_before_any_rule_runs(self):
        log = self.log("a.log", "anything")
        rc, out, err = self.run_tool(self.spec("expect /anything/"), log,
                                     os.path.join(self.dir, "late-missing.log"))
        self.assertEqual((rc, out), (2, ""), out + err)

    def test_near_miss_hints(self):
        # tier 1: a line that holds the pattern's own literal run is the interesting almost-match
        near = self.log("near.log", "boot", "career: lot 6 cars", "quit")
        rc, out, _ = self.check("expect /career: lot (\\d+) bikes/", near)
        self.assertEqual(rc, 1)
        self.assertIn("closest lines:", out)
        self.assertIn("near.log:2: career: lot 6 cars", out)
        # tier 2: no line holds the whole run, so a line has to carry two of its words
        said = self.log("said.log", "career: new stage 0 bank 0, region B, car lot", "career: placed")
        rc, out, _ = self.check("expect /career: lot (\\d+) cars:/", said)
        self.assertEqual(rc, 1)
        self.assertIn("said.log:1: career: new stage 0 bank 0, region B, car lot", out)
        self.assertNotIn("said.log:2", out, "one shared word is not a near miss:\n" + out)
        # nothing close: say so instead of naming a random line
        rc, out, _ = self.check("expect /zzqq (\\d+) wxyz/", near)
        self.assertEqual(rc, 1)
        self.assertNotIn("closest lines:", out)
        self.assertIn("no line of the log carries its words", out)

    def test_empty_log_file_reports_zero_matches(self):
        log = self.log("empty.log")                        # zero bytes, zero lines
        rc, out, _ = self.check("expect /x/", log)
        self.assertEqual(rc, 1)
        self.assertIn("found 0 match(es)", out)
        self.assertEqual(self.check("forbid /x/", log)[0], 0)

    def test_binary_junk_and_long_lines_survive(self):
        path = os.path.join(self.dir, "junk.log")
        with open(path, "wb") as fh:
            fh.write(b"ok \xff\xfe line\n" + b"x" * 400 + b"\n")
        rc, out, _ = self.check("expect /ok/; expect /xxxx/", path)
        self.assertEqual(rc, 0, out)
        rc, out, _ = self.check("forbid /xxxx/", path)
        self.assertEqual(rc, 1)
        self.assertIn("...", out, "long evidence lines are cut")

    def test_it_refuses_to_write_the_report_over_a_file_it_reads(self):
        # --json's path is optional, so `--json LOG LOG` in a gate loop names the log as the report;
        # replacing a gate log with its own report would destroy the evidence the gate is arguing.
        log = self.log("a.log", "anything")
        spec = self.spec("expect /anything/")
        rc, out, err = self.run_tool("--json", log, "--spec", "expect /anything/", log)
        self.assertEqual((rc, out), (2, ""), out + err)
        self.assertIn("--json", err)
        self.assertIn("also reads", err)
        with open(os.path.join(self.dir, "a.log")) as fh:
            self.assertEqual(fh.read(), "anything\n", "the log must be untouched")
        rc, out, err = self.run_tool("--json", spec, spec, log)
        self.assertEqual((rc, out), (2, ""), out + err)
        with open(os.path.join(self.dir, spec)) as fh:
            self.assertEqual(fh.read(), "expect /anything/", "the spec must be untouched")
        # the unambiguous spellings still work
        rc, out, err = self.run_tool("--json=out.json", spec, log)
        self.assertEqual((rc, out), (0, "PASS spec:1  expect /anything/\n1 rules, 1 pass, 0 fail\n"), err)
        with open(os.path.join(self.dir, "out.json")) as fh:
            self.assertEqual(json.load(fh)["summary"], {"rules": 1, "pass": 1, "fail": 0})
        rc, out, _ = self.run_tool(spec, log, "--json", "report.json")
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.exists(os.path.join(self.dir, "report.json")))

    def test_rules_run_in_spec_order_and_fail_independently(self):
        log = self.log("a.log", "one", "two")
        rc, out, _ = self.check("expect /one/; expect /nope/; expect /two/; forbid /one/", log)
        self.assertEqual(rc, 1)
        self.assertEqual(self.tally(out), "4 rules, 2 pass, 2 fail")
        self.assertEqual([l.split()[0] for l in out.splitlines() if l.startswith(("PASS", "FAIL"))],
                         ["PASS", "FAIL", "PASS", "FAIL"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
