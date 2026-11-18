#!/usr/bin/env python3
r"""Self-test for tools/gate_run.py. No game, no data dir: every gate here is a temp file whose steps
run `true`, `false`, a fake game script or a tiny Makefile.

The runner is driven through main() - the same door the CLI opens - so exit codes, the printed
verdict and the two summaries are all covered as a user sees them. The cases that matter most are the
ones where a lazy implementation could report success: a step that never ran, a spec file that is not
there, an unset variable, a shot that was never written.

  python3 -I tools/gate_run_test.py [-v]
"""
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest


def read(path):
    """Whole file as text, closed on the way out."""
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def write_file(path, text):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gate_run

# The runner as a subprocess command line, and as the argv list for a direct main() call: the two
# differ only in that main() takes argv[0] too, and the .py file is not executable on its own.
RUN = [sys.executable, "-I", os.path.join(HERE, "gate_run.py")]
ARGV = [RUN[2]]


class Gate(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name
        self.repo = self.sub("repo")
        self.out = self.sub("out")
        # A board without OPENUG2_HEAVY_LOCK runs without flock; the tests must not depend on a lock
        # another process happens to hold, and not be slowed by nice either.
        os.environ.pop("OPENUG2_HEAVY_LOCK", None)

    # ------------------------------------------------------------------ helpers

    def sub(self, name):
        path = os.path.join(self.dir, name)
        os.makedirs(path, exist_ok=True)
        return path

    def write(self, directory, name, text, mode=None):
        path = os.path.join(directory, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as fh:
            fh.write(text)
        if mode is not None:
            os.chmod(path, mode)
        return path

    def game(self, name, body):
        """A stand-in for the binary: run with cwd = the step dir, free to write shots or read env."""
        return self.write(self.repo, name, "#!/usr/bin/env python3\n" + body,
                          mode=stat.S_IRWXU)

    def gate(self, *steps, **kwargs):
        data = {"name": kwargs.get("name", "t"), "steps": list(steps)}
        if "env_required" in kwargs:
            data["env_required"] = kwargs["env_required"]
        return self.write(self.dir, kwargs.get("file", "gate.json"), json.dumps(data, indent=1))

    def run_main(self, path, *args, **kwargs):
        """Call main() directly over a gate file; returns (exit code, printed text, results by id).

        stdout is redirected, not replaced, so print() inside the runner is captured as it is
        written rather than through an object the runner never sees.
        """
        argv = ARGV + [path, "--repo", self.repo, "--out", self.out] + list(args)
        for name, value in sorted(kwargs.get("env", {}).items()):
            argv += ["--env", "%s=%s" % (name, value)]
        printed = io.StringIO()
        keep = sys.stdout
        sys.stdout = printed
        try:
            code = gate_run.main(argv)
        finally:
            sys.stdout = keep
        steps = {r["id"]: r for r in self.summary()["steps"]}
        return code, printed.getvalue(), steps

    def summary(self):
        return json.loads(read(os.path.join(self.out, "summary.json")))

    def status_of(self, steps, step_id):
        self.assertIn(step_id, steps, "step %s missing from the run" % step_id)
        return steps[step_id]["status"]

    def detail_of(self, steps, step_id):
        return steps[step_id]["detail"]

    def cmd(self, step_id, *argv, **kwargs):
        step = {"id": step_id, "kind": "cmd", "cmd": list(argv), "heavy": False}
        step.update(kwargs)
        return step

    def sh(self, step_id, code, **kwargs):
        """A cmd step that runs Python code - the portable `true`/`false` of this suite."""
        return self.cmd(step_id, sys.executable, "-c", code, **kwargs)

    # ------------------------------------------------------------------ verdict logic

    def test_pass_and_fail_and_not_run(self):
        gate = self.gate(
            self.sh("ok", "print('hello')"),
            self.sh("bad", "import sys; sys.exit(3)"),
            self.sh("later", "print('never reached')"))
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1, out)
        self.assertEqual(self.status_of(steps, "ok"), "PASS")
        self.assertEqual(self.status_of(steps, "bad"), "FAIL")
        self.assertEqual(self.status_of(steps, "later"), "NOT RUN")
        self.assertFalse(os.path.exists(os.path.join(self.out, "later.log")),
                         "the step after the FAIL must not have run at all")
        self.assertIn("hello", read(os.path.join(self.out, "ok.log")))

    def test_not_run_is_never_pass(self):
        """The single most important property: a stopped step cannot be read as a success."""
        gate = self.gate(self.sh("bad", "raise SystemExit(1)"), self.sh("later", "pass"))
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "later"), "NOT RUN")
        tally = self.summary()["counts"]
        self.assertEqual((tally["PASS"], tally["FAIL"], tally["NOT RUN"]), (0, 1, 1))
        self.assertEqual(self.summary()["not_pass"], ["bad", "later"],
                         "a step that never ran must be named as not-PASS")
        self.assertIn("later  NOT RUN", out)

    def test_keep_going_runs_the_rest(self):
        gate = self.gate(
            self.sh("one", "raise SystemExit(1)"),
            self.sh("two", "print('two ran')"),
            self.sh("three", "pass"))
        code, out, steps = self.run_main(gate, "--keep-going")
        self.assertEqual(code, 1)
        self.assertEqual([self.status_of(steps, s) for s in ("one", "two", "three")],
                         ["FAIL", "PASS", "PASS"])
        self.assertIn("two ran", read(os.path.join(self.out, "two.log")))

    def test_all_pass_exits_zero(self):
        gate = self.gate(self.sh("a", "pass"), self.sh("b", "pass"))
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 0, out)
        self.assertEqual([r["status"] for r in self.summary()["steps"]], ["PASS", "PASS"])
        self.assertIn("GATE PASS", out)

    # ------------------------------------------------------------------ --only / --list

    def test_only_selects_and_skips(self):
        gate = self.gate(self.sh("a", "print('a')"), self.sh("b", "raise SystemExit(1)"),
                         self.sh("c", "print('c')"))
        code, out, steps = self.run_main(gate, "--only", "a,c")
        self.assertEqual(code, 0, out)   # the failing middle step was not selected
        self.assertEqual(self.status_of(steps, "a"), "PASS")
        self.assertEqual(self.status_of(steps, "b"), "SKIP")
        self.assertEqual(self.status_of(steps, "c"), "PASS")
        self.assertFalse(os.path.exists(os.path.join(self.out, "b.log")),
                         "a skipped step must not run")

    def test_only_failure_still_exits_one(self):
        gate = self.gate(self.sh("a", "pass"), self.sh("b", "raise SystemExit(1)"))
        code, out, steps = self.run_main(gate, "--only", "b")
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "a"), "SKIP")
        self.assertEqual(self.status_of(steps, "b"), "FAIL")

    def test_only_unknown_id_is_usage(self):
        gate = self.gate(self.sh("a", "pass"))
        p = subprocess.run(RUN + [gate, "--repo", self.repo, "--out", self.out, "--only", "nope"],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 2)

    def test_list_runs_nothing(self):
        marker = os.path.join(self.dir, "marker")
        gate = self.gate(self.sh("a", "open(%r, 'w').close()" % marker))
        argv = RUN + [gate, "--repo", self.repo, "--out", self.out, "--list"]
        p = subprocess.run(argv, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("a  cmd", p.stdout)
        self.assertFalse(os.path.exists(marker), "--list must not run the steps")

    # ------------------------------------------------------------------ usage errors

    def test_bad_gate_file_is_exit_two(self):
        for body in ('{"name": "t"}',                       # no steps
                     '{"steps": []}',                        # name missing / empty steps
                     'not json at all',
                     '{"name": "t", "steps": [{"id": "a", "kind": "dance"}]}',
                     '{"name": "t", "steps": [{"id": "a", "kind": "cmd"}]}',        # no cmd
                     '{"name": "t", "steps": [{"id": "a", "kind": "make"}]}',       # no targets
                     '{"name": "t", "steps": [{"id": "a", "kind": "cmd", "cmd": ["true"], "nope": 1}]}',
                     '{"name": "t", "steps": [{"id": "a", "kind": "cmd", "cmd": ["true"]},'
                     ' {"id": "a", "kind": "cmd", "cmd": ["true"]}]}',              # duplicate id
                     '{"name": "t", "steps": [{"id": "../out", "kind": "cmd", "cmd": ["true"]}]}'):
            path = self.write(self.dir, "bad.json", body)
            argv = RUN + [path, "--repo", self.repo, "--out", self.out]
            p = subprocess.run(argv, capture_output=True, text=True)
            self.assertEqual(p.returncode, 2, "%s -> %d: %s" % (body, p.returncode, p.stdout + p.stderr))
            self.assertIn("gate_run:", p.stderr)

    def test_missing_gate_and_repo_are_exit_two(self):
        p = subprocess.run(RUN + [self.dir + "/absent.json"], capture_output=True, text=True)
        self.assertEqual(p.returncode, 2)
        gate = self.gate(self.sh("a", "pass"))
        p = subprocess.run(RUN + [gate, "--repo", self.dir + "/nope"], capture_output=True, text=True)
        self.assertEqual(p.returncode, 2)

    def test_traceback_is_a_test_failure(self):
        """main() must not raise: a crash would look like a runner bug rather than a verdict."""
        gate = self.gate(self.sh("a", "pass"))
        p = subprocess.run(RUN + [gate, "--repo", self.repo, "--out", self.out],
                           capture_output=True, text=True)
        self.assertNotIn("Traceback", p.stderr, p.stderr)

    # ------------------------------------------------------------------ summaries

    def test_summaries_agree_with_stdout(self):
        gate = self.gate(self.sh("a", "pass"), self.sh("b", "raise SystemExit(1)"))
        code, out, steps = self.run_main(gate, "--keep-going")
        self.assertEqual(code, 1)
        tsv = read(os.path.join(self.out, "summary.tsv")).splitlines()
        self.assertEqual(tsv[0], "id\tkind\tstatus\tseconds\tdetail")
        self.assertEqual(len(tsv), 1 + 2)
        self.assertTrue(tsv[1].startswith("a\tcmd\tPASS\t"))
        self.assertTrue(tsv[2].startswith("b\tcmd\tFAIL\t"))
        for line, step_id in zip(tsv[1:], ("a", "b")):
            self.assertEqual(line.split("\t")[2], steps[step_id]["status"])

    # ------------------------------------------------------------------ warnings

    def test_warning_limit_parses_build_log(self):
        self.write(self.repo, "Makefile",
                   "all:\n\t@echo 'src/a.c:3:1: warning: unused x'\n\t@echo 'note: no word here'\n"
                   "\t@echo 'warning: two'\n")
        gate = self.gate({"id": "build", "kind": "make", "targets": ["all"], "max_warnings": 2,
                          "heavy": False})
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 0, out)
        self.assertIn("warnings=2", self.detail_of(steps, "build"))

        gate2 = self.gate({"id": "build", "kind": "make", "targets": ["all"], "max_warnings": 1,
                           "heavy": False})
        code, out, steps = self.run_main(gate2)
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "build"), "FAIL")
        self.assertIn("max_warnings 1", self.detail_of(steps, "build"))

    def test_make_failure_names_the_target(self):
        self.write(self.repo, "Makefile",
                   "ok:\n\t@true\nbad:\n\t@sh -c 'exit 7'\nlate:\n\t@true\n")
        gate = self.gate({"id": "m", "kind": "make", "targets": ["ok", "bad", "late"],
                          "heavy": False})
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        # make exits 2 for any recipe failure and names the real code in its message: the detail has
        # to carry that message, or the step says 'exited 2' and nobody learns which target broke.
        detail = self.detail_of(steps, "m")
        self.assertIn("'bad' exited 2", detail)
        self.assertIn("Error 7", detail)
        self.assertIn("targets=3", self.detail_of(steps, "m"))
        # the run stopped at the failing target rather than ploughing on with a broken tree
        self.assertNotIn("'late'", self.detail_of(steps, "m"))
        self.assertTrue(os.path.isfile(os.path.join(self.out, "m.log")))

    def test_touch_must_match_something(self):
        """An empty touch glob means the rebuild was never forced, so the step may not pass."""
        self.write(self.repo, "Makefile", "all:\n\t@true\n")
        gate = self.gate({"id": "build", "kind": "make", "targets": ["all"], "touch": "src/*.c",
                          "heavy": False})
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        self.assertIn("matched nothing", self.detail_of(steps, "build"))

    def test_touch_forces_work_and_glob_reaches_deep(self):
        src = os.path.join(self.repo, "src")
        os.makedirs(src, exist_ok=True)
        self.write(src, "a.c", "int main(void){return 0;}\n")
        self.write(self.repo, "Makefile", "all:\n\t@true\n")
        old = time.time() - 4000
        os.utime(os.path.join(src, "a.c"), (old, old))
        gate = self.gate({"id": "build", "kind": "make", "targets": ["all"], "touch": "src/*.c",
                          "heavy": False})
        code, out, _ = self.run_main(gate)
        self.assertEqual(code, 0, out)
        self.assertGreater(os.path.getmtime(os.path.join(src, "a.c")), old + 1,
                           "the source was not touched, so the rebuild would not be forced")

    def test_target_timeout_names_the_hanging_target(self):
        self.write(self.repo, "Makefile", "hang:\n\t@sleep 30\n")
        gate = self.gate({"id": "m", "kind": "make", "targets": ["hang"], "target_timeout": 1,
                          "timeout": 20, "heavy": False})
        started = time.time()
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        self.assertIn("'hang' timed out after 1 s", self.detail_of(steps, "m"))
        self.assertLess(time.time() - started, 12, "the hung target was not killed")

    # ------------------------------------------------------------------ shots

    def test_shot_compare_all_three_states(self):
        ref = self.write(self.dir, "ref.png", "abc")
        same = self.write(self.dir, "same.png", "abc")
        other = self.write(self.dir, "other.png", "abd")
        self.assertEqual(gate_run.shot_state(same, ref), "equal")
        self.assertEqual(gate_run.shot_state(other, ref), "different")
        self.assertTrue(gate_run.shot_state(self.dir + "/absent.png", ref).startswith("missing:"))
        self.assertTrue(gate_run.shot_state(same, self.dir + "/absent-ref.png").startswith("missing:"))

    def test_shot_equal_passes_then_fails_on_one_byte(self):
        ref = self.write(self.dir, "ref.png", "pixels")
        body = ("import sys\n"
                "open('shot.png','w').write('pixels' if len(sys.argv)<2 else 'pixelss')\n")
        self.game("fake", body)
        step = {"id": "inv", "kind": "game", "binary": "fake", "args": [],
                "shot_equal": {"shot.png": ref}}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        self.assertIn("shot.png == ref.png", self.detail_of(steps, "inv"))

        # flip the last byte the fake game writes by handing it an argument
        step["args"] = ["x"]
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "inv"), "FAIL")
        self.assertIn("differs", self.detail_of(steps, "inv"))

    def test_missing_shot_is_a_fail_not_a_pass(self):
        ref = self.write(self.dir, "ref.png", "pixels")
        self.game("fake", "pass")                      # writes nothing at all
        step = {"id": "inv", "kind": "game", "binary": "fake", "args": [],
                "shot_equal": {"shot.png": ref}, "shots_min": 1}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        detail = self.detail_of(steps, "inv")
        self.assertIn("missing", detail)
        self.assertIn("need 1", detail)

    # ------------------------------------------------------------------ spec hookup

    def test_spec_pass_fail_and_missing_file(self):
        self.game("fake", "print('career: bought 240SX for $0')")
        # a spec path is repo-relative, which is where a game step's evidence rules belong
        self.write(self.repo, "spec.txt", "expect /career: bought \\S+ for/\n")
        step = {"id": "g", "kind": "game", "binary": "fake", "args": [], "spec": "spec.txt"}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        self.assertIn("spec pass", self.detail_of(steps, "g"))

        self.write(self.repo, "spec.txt", "expect /career: sold/\n")
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1, out)
        detail = self.detail_of(steps, "g")
        self.assertIn("spec spec.txt failed", detail)
        self.assertIn("career: sold", detail)

        step["spec"] = "tools/log_specs/not_here.spec"
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertIn("not found in the repo", self.detail_of(steps, "g"))

    # ------------------------------------------------------------------ game step plumbing

    def test_game_env_xdg_and_offscreen(self):
        body = (
            "import json, os\n"
            "json.dump({'video': os.environ.get('SDL_VIDEODRIVER'),\n"
            "           'audio': os.environ.get('SDL_AUDIODRIVER'),\n"
            "           'cfg': os.environ.get('XDG_CONFIG_HOME'),\n"
            "           'data': os.environ.get('XDG_DATA_HOME'),\n"
            "           'cwd': os.path.realpath(os.getcwd())},\n"
            "          open('env.json', 'w'))\n")
        self.game("fake", body)
        step = {"id": "g", "kind": "game", "binary": "fake", "args": [], "xdg": "shared",
                "env": {"SDL_AUDIODRIVER": "dummy"}}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        seen = json.loads(read(os.path.join(self.out, "g", "env.json")))
        self.assertEqual(seen["video"], "offscreen")
        self.assertEqual(seen["audio"], "dummy")
        self.assertEqual(seen["cwd"], os.path.realpath(os.path.join(self.out, "g")))
        self.assertEqual(seen["cfg"], os.path.join(self.out, "xdg", "shared", "c"))
        self.assertEqual(seen["data"], os.path.join(self.out, "xdg", "shared", "d"))
        for path in (seen["cfg"], seen["data"]):
            self.assertTrue(os.path.isdir(path))
        self.assertNotEqual(seen["cfg"], os.environ.get("XDG_CONFIG_HOME"),
                            "the user's real config home leaked into the step")

    def test_xdg_shared_then_wiped_next_run(self):
        """Same name = same folder, kept between steps; a new run wipes it rather than reusing it."""
        writer = ("import os,sys\n"
                  "d=os.environ['XDG_DATA_HOME']\n"
                  "open(os.path.join(d,'save.txt'),'w').write('career')\n")
        reader = ("import os,sys\n"
                  "d=os.environ['XDG_DATA_HOME']\n"
                  "raise SystemExit(0 if open(os.path.join(d,'save.txt')).read()=='career' else 1)\n")
        self.game("writer", writer)
        self.game("reader", reader)
        gate = self.gate({"id": "w", "kind": "game", "binary": "writer", "args": [], "xdg": "s7"},
                         {"id": "r", "kind": "game", "binary": "reader", "args": [], "xdg": "s7"})
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 0, "the second step could not read the save: " + out)
        save = os.path.join(self.out, "xdg", "s7", "d", "save.txt")
        self.assertTrue(os.path.isfile(save))

        # a second run must wipe the named folder before the writer runs, or the test above would
        # pass on the old save instead of a new one
        os.remove(save)
        write_file(save, "stale")
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 0, out)
        self.assertEqual(read(save), "career", "the shared folder was not wiped between runs")

    def test_distinct_xdg_names_do_not_share(self):
        reader = ("import os\n"
                  "raise SystemExit(0 if not os.listdir(os.environ['XDG_DATA_HOME']) else 1)\n")
        writer = "import os\nopen(os.path.join(os.environ['XDG_DATA_HOME'],'x'),'w')\n"
        self.game("reader", reader)
        self.game("writer", writer)
        gate = self.gate({"id": "w", "kind": "game", "binary": "writer", "args": [], "xdg": "a"},
                         {"id": "r", "kind": "game", "binary": "reader", "args": [], "xdg": "b"})
        code, out, _ = self.run_main(gate)
        self.assertEqual(code, 0, out)

    def test_expected_exit_status(self):
        self.game("fake", "raise SystemExit(2)")
        gate = self.gate({"id": "g", "kind": "game", "binary": "fake", "args": [], "exit": 2})
        code, out, _ = self.run_main(gate)
        self.assertEqual(code, 0, out)
        gate2 = self.gate({"id": "g", "kind": "game", "binary": "fake", "args": [], "exit": 0})
        code, out, steps = self.run_main(gate2)
        self.assertEqual(code, 1)
        self.assertIn("exit 2 != 0", self.detail_of(steps, "g"))

    def test_missing_binary_is_a_fail(self):
        gate = self.gate({"id": "g", "kind": "game", "binary": "not-built", "args": []})
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        self.assertIn("not found", self.detail_of(steps, "g"))

    def test_input_script_from_the_repo_reaches_the_game(self):
        """@tools/gate_inputs/x.txt is a repo path; the game's cwd is the step dir, so the runner
        has to hand over an absolute path and the file must actually exist."""
        self.write(self.repo, "tools/gate_inputs/hello.txt", "10:key=Return\n")
        body = ("import sys\n"
                "assert sys.argv[1] == '--input-script', sys.argv\n"
                "p = sys.argv[2][1:]\n"
                "assert p.startswith('/') , 'the runner must hand over an absolute path: ' + p\n"
                "open('seen.txt','w').write(open(p).read())\n")
        self.game("fake", body)
        step = {"id": "g", "kind": "game", "binary": "fake",
                "args": ["--input-script", "@tools/gate_inputs/hello.txt"]}
        code, out, _ = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        self.assertEqual(read(os.path.join(self.out, "g", "seen.txt")), "10:key=Return\n")

    def test_input_script_missing_from_repo_fails(self):
        self.game("fake", "pass")
        step = {"id": "g", "kind": "game", "binary": "fake",
                "args": ["--input-script", "@tools/gate_inputs/absent.txt"]}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertIn("not found", self.detail_of(steps, "g"))

    def test_input_script_reaches_the_game_as_an_absolute_path(self):
        """The game resolves '@tools/gate_inputs/x.txt' against its own cwd, which the runner sets to
        the step's output directory - so the runner must hand over an absolute path. Every real step
        in m1.json passes one through ${OPENUG2_DATA}-style expansion today; a step that wrote the
        relative path plain would fail inside the game with a message that blames the script file.
        """
        self.write(self.repo, "tools/gate_inputs/abs.txt", "10:key=Return\n")
        self.game("fake", "import sys\n"
                          "v = sys.argv[2]\n"
                          "open('seen.txt','w').write(v)\n")
        step = {"id": "g", "kind": "game", "binary": "fake",
                "args": ["--input-script", "@tools/gate_inputs/abs.txt"]}
        code, out, _ = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        seen = read(os.path.join(self.out, "g", "seen.txt"))
        self.assertTrue(seen.startswith("@" + self.repo),
                        "the game gets cwd = the step dir, so the path must be absolute: %r" % seen)
        self.assertEqual(read(seen[1:]), "10:key=Return\n")

    def test_shots_min_catches_a_script_that_stops_early(self):
        """shots_min is the rule that catches a scripted sequence dying before its last shot, and it
        is the only one the log spec cannot stand in for: the runner counts PNGs in the step dir, and
        no spec line can see the file system. A fake game that writes the shot of every frame it is
        told about is enough - what matters is that the missing one fails the step and names itself.
        """
        self.write(self.repo, "tools/gate_inputs/short.txt",
                   "10:shot=early.png\n5000:shot=late.png\n9999:quit\n")
        self.game("fake", "import sys, re\n"
                          "spec = open(sys.argv[2][1:]).read()\n"
                          "for f, name in re.findall(r'(\\d+):shot=(\\S+)', spec):\n"
                          "    if int(f) < 2000: open(name, 'w').write('PNG')\n")
        step = {"id": "g", "kind": "game", "binary": "fake",
                "args": ["--input-script", "@tools/gate_inputs/short.txt"], "shots_min": 2}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertIn("shots 1/2", self.detail_of(steps, "g"))
        self.assertTrue(os.path.isfile(os.path.join(self.out, "g", "early.png")))
        self.assertFalse(os.path.isfile(os.path.join(self.out, "g", "late.png")))

    def test_inline_and_multiline_input_scripts(self):
        # An inline one-liner goes through untouched; a multi-line script cannot be an argv entry
        # the game would parse, so it must reach the game as a @file it can read.
        body = ("import sys\n"
                "v = sys.argv[2]      # argv[1] is the --input-script flag itself\n"
                "open('how.txt','w').write('inline' if not v.startswith('@') else open(v[1:]).read())\n")
        self.game("fake", body)
        inline = {"id": "one", "kind": "game", "binary": "fake",
                  "args": ["--input-script", "10:key=Left,20:key=Return"]}
        multiline = {"id": "two", "kind": "game", "binary": "fake",
                     "args": ["--input-script", "10:key=Left\n20:key=Return\n"]}
        code, out, _ = self.run_main(self.gate(inline, multiline))
        self.assertEqual(code, 0, out)
        self.assertEqual(read(os.path.join(self.out, "one", "how.txt")), "inline",
                         "a one-line script must reach the game exactly as the gate wrote it")
        self.assertEqual(read(os.path.join(self.out, "two", "how.txt")),
                         "10:key=Left\n20:key=Return\n",
                         "a multi-line script must reach the game as a file with the same content")

    # ------------------------------------------------------------------ timeouts

    def test_timeout_kills_the_group_and_fails(self):
        self.game("fake", "import time; time.sleep(60)")
        step = {"id": "g", "kind": "game", "binary": "fake", "args": [], "timeout": 1}
        started = time.time()
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertIn("timeout after 1 s", self.detail_of(steps, "g"))
        self.assertLess(time.time() - started, 15, "the step outlived its timeout")

    def test_cmd_timeout(self):
        self.game("slow", "import time; time.sleep(60)\n")
        step = {"id": "slow", "kind": "game", "binary": "slow", "args": [], "timeout": 1}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertIn("timeout", self.detail_of(steps, "slow"))

    # ------------------------------------------------------------------ environment

    def test_env_required_unset_is_fail_not_skip(self):
        gate = self.gate(self.sh("a", "pass"), env_required=["OPENUG2_TEST_UNSET"])
        code, out, steps = self.run_main(gate)
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "a"), "FAIL", "an unset variable must never be a SKIP")
        self.assertIn("env OPENUG2_TEST_UNSET unset", self.detail_of(steps, "a"))

    def test_env_required_satisfied_passes(self):
        gate = self.gate(self.sh("a", "pass"), env_required=["OPENUG2_TEST_SET"])
        code, out, steps = self.run_main(gate, env={"OPENUG2_TEST_SET": "1"})
        self.assertEqual(code, 0, out)
        self.assertEqual(self.status_of(steps, "a"), "PASS")

    def test_dollar_var_expands_in_args(self):
        body = "import sys\nopen('seen.txt','w').write(sys.argv[1])\n"
        self.game("fake", body)
        step = {"id": "g", "kind": "game", "binary": "fake", "args": ["${OPENUG2_TEST_PATH}"]}
        gate = self.gate(step, env_required=["OPENUG2_TEST_PATH"])
        code, out, _ = self.run_main(gate, env={"OPENUG2_TEST_PATH": "/tmp/data"})
        self.assertEqual(code, 0, out)
        self.assertEqual(read(os.path.join(self.out, "g", "seen.txt")), "/tmp/data")

    def test_dollar_var_unset_fails_the_step(self):
        """A step that runs with an empty path is worse than one that does not run."""
        body = ("import sys\n"
                "open('seen.txt','w').write(sys.argv[1])\n"
                "raise SystemExit(1 if sys.argv[1] == '' else 0)\n")
        self.game("fake", body)
        step = {"id": "g", "kind": "game", "binary": "fake", "args": ["${OPENUG2_TEST_UNSET}"]}
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 1)
        self.assertEqual(self.status_of(steps, "g"), "FAIL")
        self.assertIn("env OPENUG2_TEST_UNSET unset", self.detail_of(steps, "g"))

    def test_step_env_reaches_the_child(self):
        self.game("fake", "import os\nopen('v.txt','w').write(os.environ.get('MY_FLAG','none'))\n")
        step = {"id": "g", "kind": "game", "binary": "fake", "args": [], "env": {"MY_FLAG": "on"}}
        code, out, _ = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        self.assertEqual(read(os.path.join(self.out, "g", "v.txt")), "on")

    # ------------------------------------------------------------------ memory guard

    def test_low_memory_fails_after_waiting(self):
        """A board with no headroom waits its 10 minutes and then FAILs - never passes quietly."""
        gate = self.gate({"id": "g", "kind": "game", "binary": "true", "args": []})
        slept = []
        restore = self.fake_memory(100)
        try:
            runner = gate_run.Runner(gate_run.load_gate(gate), self.repo, self.out,
                                     env=dict(os.environ), sleep=slept.append)
            verdict = runner.run()
        finally:
            restore()
        self.assertEqual(verdict["exit"], 1)
        self.assertIn("low memory", verdict["steps"][0]["detail"])
        self.assertIn("need 2500 MiB", verdict["steps"][0]["detail"])
        self.assertEqual(slept, [30] * 20, "a 600 s wait polls every 30 s")

    def test_tight_board_starts_after_waiting_with_a_note(self):
        """Below the floor for the whole wait, but far from empty: start, and say so.

        The board is shared and a game run holds over a gig. A step that would pass while swapping
        has passed; refusing it is a verdict nobody asked for. The note is the record that it ran
        under pressure.
        """
        restore = self.fake_memory(2000)      # under the 2500 floor, well over the 1200 risk line
        try:
            runner = self.runner_with_no_sleep()
            self.assertIsNone(runner.wait_for_memory())
        finally:
            restore()
        printed = io.StringIO()
        keep = sys.stdout
        sys.stdout = printed
        try:
            restore = self.fake_memory(2000)
            try:
                self.assertIsNone(self.runner_with_no_sleep().wait_for_memory())
            finally:
                restore()
        finally:
            sys.stdout = keep
        self.assertIn("below the 2500 MiB floor", printed.getvalue())

    def test_genuinely_empty_board_fails_without_a_note(self):
        """Under the risk line there is no polite way to start: the honest answer is the failure."""
        restore = self.fake_memory(300)
        try:
            runner = self.runner_with_no_sleep()
            reason = runner.wait_for_memory()
        finally:
            restore()
        self.assertIsNotNone(reason)
        self.assertIn("need 2500 MiB", reason)

    def test_memory_check_lets_a_healthy_board_through(self):
        gate = self.gate({"id": "g", "kind": "game", "binary": "true", "args": []})
        restore = self.fake_memory(8000)
        try:
            runner = gate_run.Runner(gate_run.load_gate(gate), self.repo, self.out,
                                     env=dict(os.environ), sleep=lambda s: None)
            self.assertIsNone(runner.wait_for_memory())
        finally:
            restore()

    def test_memory_recovery_continues(self):
        """Low, then recovered: the step runs, and the wait is what bought the difference."""
        readings = iter([100, 100, 4000])
        real = gate_run.mem_available_mib
        gate_run.mem_available_mib = lambda path="/proc/meminfo": next(readings)
        try:
            self.assertIsNone(self.runner_with_no_sleep().wait_for_memory())
        finally:
            gate_run.mem_available_mib = real

    def test_mem_available_mib_reads_proc(self):
        """The guard is only as good as its reading of /proc/meminfo."""
        sample = self.write(self.dir, "meminfo",
                            "MemTotal:       8253440 kB\nMemFree:    1234 kB\n"
                            "MemAvailable:   3145728 kB\n")
        self.assertEqual(gate_run.mem_available_mib(sample), 3072)
        self.assertIsNone(gate_run.mem_available_mib(self.dir + "/absent"))
        junk = self.write(self.dir, "junk-meminfo", "MemTotal: nonsense\n")
        self.assertIsNone(gate_run.mem_available_mib(junk))
        # the real board: this is the file the guard actually reads, and it must yield a number
        self.assertIsInstance(gate_run.mem_available_mib(), int)

    def fake_memory(self, available):
        """Pin what the guard reads; returns the undo."""
        real = gate_run.mem_available_mib
        gate_run.mem_available_mib = lambda path="/proc/meminfo": available
        def restore():
            gate_run.mem_available_mib = real
        return restore

    def runner_with_no_sleep(self):
        """A runner that does not actually wait, so the memory paths run at test speed."""
        return gate_run.Runner({"name": "t", "env_required": [], "steps": []},
                               self.repo, self.out, env=dict(os.environ), sleep=lambda s: None)

    # ------------------------------------------------------------------ heavy wrapping

    def test_no_lock_notes_once_and_heavy_wraps(self):
        os.environ.pop("OPENUG2_HEAVY_LOCK", None)
        runner = gate_run.Runner({"name": "t", "env_required": [], "steps": []}, self.repo,
                                 self.out, env={})
        printed = io.StringIO()
        keep = sys.stdout
        sys.stdout = printed
        try:
            self.assertEqual(runner.heavy_prefix(), ["nice", "-n", "10"])
            self.assertEqual(runner.heavy_prefix(), ["nice", "-n", "10"])
        finally:
            sys.stdout = keep
        self.assertEqual(printed.getvalue().count("without flock"), 1, "the note is said once")

        os.environ["OPENUG2_HEAVY_LOCK"] = os.path.join(self.dir, "heavy.lock")
        try:
            prefix = runner.heavy_prefix()
            self.assertEqual(prefix[:2], ["flock", os.path.join(self.dir, "heavy.lock")])
            self.assertEqual(prefix[2:], ["nice", "-n", "10"])
        finally:
            os.environ.pop("OPENUG2_HEAVY_LOCK", None)

    def test_heavy_lock_serialises_two_gates(self):
        """Two runners that share OPENUG2_HEAVY_LOCK must not build at the same time."""
        lock = os.path.join(self.dir, "heavy.lock")
        self.game("hold", "import time; time.sleep(1.5)\n")
        step = {"id": "h", "kind": "game", "binary": "hold", "args": [], "timeout": 60}
        gate = self.gate(step, name="a")
        procs = []
        os.environ["OPENUG2_HEAVY_LOCK"] = lock
        try:
            for i in (0, 1):
                procs.append(subprocess.Popen(
                    RUN + [gate, "--repo", self.repo, "--out", os.path.join(self.out, str(i))],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
            wall_start = time.time()
            codes = [p.wait() for p in procs]
        finally:
            os.environ.pop("OPENUG2_HEAVY_LOCK", None)
        self.assertEqual(codes, [0, 0])
        self.assertGreaterEqual(time.time() - wall_start, 2.9,
                                "both runs finished in less than two sleeps, so they overlapped")

    def test_cmd_heavy_false_skips_the_wrapper(self):
        step = self.sh("plain", "print('plain')")
        self.assertTrue(step["heavy"] is False)
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)

    # ------------------------------------------------------------------ cmd kind

    def test_cmd_expect_stdout(self):
        step = self.sh("chk", "print('career_host_check: 107 checks, 0 failures')",
                       expect_stdout=r"career_host_check: \d+ checks, 0 failures")
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        step_bad = self.sh("chk", "print('2 failures')",
                           expect_stdout=r"career_host_check: \d+ checks, 0 failures")
        code, out, steps = self.run_main(self.gate(step_bad))
        self.assertEqual(code, 1)
        self.assertIn("no match", self.detail_of(steps, "chk"))

    def test_cmd_runs_in_the_repo(self):
        """A cmd step is `make -s something`, so it has to start where the Makefile is."""
        self.write(self.repo, "Makefile", "chk:\n\t@pwd\n")
        step = self.cmd("pwd", "make", "-s", "chk")
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0, out)
        self.assertEqual(read(os.path.join(self.out, "pwd.log")).strip(),
                         os.path.realpath(self.repo))

    def test_cmd_without_expect_stdout_is_not_a_free_pass(self):
        """No regex means the exit status alone decides - and a zero exit still says so."""
        step = self.sh("ok", "print('nothing checked')")
        code, out, steps = self.run_main(self.gate(step))
        self.assertEqual(code, 0)
        self.assertIn("rc=0", self.detail_of(steps, "ok"))


if __name__ == "__main__":
    unittest.main()
