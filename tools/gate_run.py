#!/usr/bin/env python3
r"""Run a gate file (JSON) and end in one machine verdict, so any branch can be checked the same way.

A gate is a list of steps: builds, test targets, headless game runs. Each step says what to run and
what evidence proves it - a log spec, byte-identical screenshots, a minimum number of shots. The
runner decides PASS/FAIL per step and prints one line per step, so the verdict needs no reading of
prose and no grepping by eye:

  python3 -I tools/gate_run.py GATEFILE [--repo DIR] [--out DIR] [--only ID[,ID..]] [--list] [--keep-going]

Exit code: 0 if every selected step PASSed, 1 if any selected step FAILed / SKIPped / was NOT RUN,
2 for a bad gate file or a usage error. --list prints 'id  kind' per step and exits 0.

Step kinds and every optional key are documented in tools/gates/README.md (and in tools/gates/m1.json,
which is the port of the orchestrator's hand-written gate for parity/m1).

Two rules shape the whole file:
  * A step that was not run is never a PASS. A condition that was never checked is never a PASS.
    So a missing spec file, a missing screenshot or an unset ${VAR} is a FAIL, not a shrug.
  * Every step's evidence is on disk under --out (log at <out>/<id>.log, shots in <out>/<id>/), and
    summary.tsv / summary.json repeat the verdict in machine form.

Heavy steps (make, game, and cmd by default) are wrapped in `flock $OPENUG2_HEAVY_LOCK nice -n 10` and
wait for memory first, because this runs on a single board that is also someone's desktop.
"""
import argparse
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

EXIT_PASS, EXIT_FAIL, EXIT_USAGE = 0, 1, 2
HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_TIMEOUT = 900.0
DEFAULT_JOBS = 2
MEM_MIN_MIB = 2500      # below this, a heavy step waits instead of swapping the board away
MEM_WAIT_S = 600.0      # ... for up to 10 minutes,
MEM_POLL_S = 30.0       # ... polling every 30 s, then it fails with "low memory"
TERM_GRACE_S = 5.0      # signal the process group TERM, then KILL after this
KILL_WAIT_S = 5.0       # how long to wait for each of the two to take effect

VAR_RE = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")
WARN_TEXT = "warning:"
PNG_SUFFIX = ".png"

PASS, FAIL, SKIP, NOT_RUN = "PASS", "FAIL", "SKIP", "NOT RUN"
STATUSES = (PASS, FAIL, SKIP, NOT_RUN)

CRASH_FORBID = r"[Ss]egmentation [Ff]ault"   # a game run that died on its own log
CRASH_DETAIL = "log reports a segmentation fault"


class GateError(Exception):
    """A gate file or a command line that cannot be honoured: exit 2, checked before any step runs."""


class Refused(Exception):
    """A step that cannot be evaluated at all (no log, no binary): reported as FAIL, never as PASS."""


# ---------------------------------------------------------------- gate file in

STEP_KEYS = {
    "make": {"id", "kind", "targets", "jobs", "touch", "max_warnings", "target_timeout",
             "timeout", "env", "heavy"},
    "game": {"id", "kind", "binary", "args", "timeout", "env", "xdg", "exit", "spec",
             "shot_equal", "shots_min", "heavy"},
    "cmd": {"id", "kind", "cmd", "expect_stdout", "timeout", "env", "xdg", "exit", "heavy"},
}


def _want_int(step, key, minimum=0):
    value = step.get(key)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise GateError("step %s: %s must be an integer >= %d, got %r" % (step.get("id"), key, minimum, value))
    return value


def _want_number(step, key):
    value = step.get(key)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)) or value <= 0:
        raise GateError("step %s: %s must be a positive number of seconds, got %r" % (step.get("id"), key, value))
    return float(value)


def _want_str_list(step, key, required=False):
    value = step.get(key)
    if value is None:
        if required:
            raise GateError("step %s: %s is required" % (step.get("id"), key))
        return []
    if not isinstance(value, list) or not all(isinstance(x, str) for x in value):
        raise GateError("step %s: %s must be a list of strings" % (step.get("id"), key))
    if required and not value:
        raise GateError("step %s: %s must not be empty" % (step.get("id"), key))
    return list(value)


def _want_str(step, key, required=False):
    value = step.get(key)
    if value is None:
        if required:
            raise GateError("step %s: %s is required" % (step.get("id"), key))
        return None
    if not isinstance(value, str) or not value:
        raise GateError("step %s: %s must be a non-empty string" % (step.get("id"), key))
    return value


def _want_dict(step, key, required=False):
    value = step.get(key)
    if value is None:
        if required:
            raise GateError("step %s: %s is required" % (step.get("id"), key))
        return {}
    if not isinstance(value, dict) or not all(isinstance(k, str) and isinstance(v, str)
                                             for k, v in value.items()):
        raise GateError("step %s: %s must be an object of string keys and string values" % (step.get("id"), key))
    return dict(value)


def _check_bool(step, key):
    value = step.get(key)
    if value is not None and not isinstance(value, bool):
        raise GateError("step %s: %s must be true or false" % (step.get("id"), key))
    return value


def parse_gate(data, source):
    """Validated gate dict, or GateError naming the fault. All of it happens before step 0 runs."""
    if not isinstance(data, dict):
        raise GateError("%s: gate file must be a JSON object" % source)
    name = data.get("name")
    if not isinstance(name, str) or not name.strip():
        raise GateError("%s: 'name' must be a non-empty string" % source)
    steps = data.get("steps")
    if not isinstance(steps, list) or not steps:
        raise GateError("%s: 'steps' must be a non-empty list" % source)
    env_required = data.get("env_required", [])
    if not isinstance(env_required, list) or not all(isinstance(x, str) and x for x in env_required):
        raise GateError("%s: 'env_required' must be a list of variable names" % source)

    parsed, seen = [], set()
    for index, step in enumerate(steps, 1):
        if not isinstance(step, dict):
            raise GateError("%s: step %d must be an object" % (source, index))
        step_id = step.get("id")
        if not isinstance(step_id, str) or not step_id:
            raise GateError("%s: step %d needs a non-empty 'id'" % (source, index))
        if not re.match(r"^[A-Za-z0-9][A-Za-z0-9._-]*$", step_id):
            # The id names a directory under --out, so it must not be able to point out of it.
            raise GateError("%s: step id %r must be [A-Za-z0-9._-]+" % (source, step_id))
        if step_id in seen:
            raise GateError("%s: duplicate step id %r" % (source, step_id))
        seen.add(step_id)
        kind = step.get("kind")
        if kind not in STEP_KEYS:
            raise GateError("%s: step %s: kind %r unknown (make, game, cmd)" % (source, step_id, kind))
        unknown = sorted(set(step) - STEP_KEYS[kind])
        if unknown:
            raise GateError("%s: step %s (%s): unknown key(s) %s"
                            % (source, step_id, kind, ", ".join(unknown)))

        out = {
            "id": step_id,
            "kind": kind,
            "env": _want_dict(step, "env"),
            "timeout": _want_number(step, "timeout") or DEFAULT_TIMEOUT,
            "exit": _want_int(step, "exit") or 0,
            "heavy": True if _check_bool(step, "heavy") is None else step["heavy"],
        }
        if kind == "make":
            out["targets"] = _want_str_list(step, "targets", required=True)
            out["jobs"] = _want_int(step, "jobs", minimum=1) or DEFAULT_JOBS
            out["touch"] = _want_str(step, "touch")
            out["max_warnings"] = _want_int(step, "max_warnings")
            out["target_timeout"] = _want_number(step, "target_timeout")
        elif kind == "game":
            out["binary"] = _want_str(step, "binary", required=True)
            out["args"] = _want_str_list(step, "args")
            out["xdg"] = _want_str(step, "xdg")
            out["spec"] = _want_str(step, "spec")
            out["shot_equal"] = _want_dict(step, "shot_equal")
            out["shots_min"] = _want_int(step, "shots_min")
            if out["shots_min"] is not None and out["shots_min"] < 1:
                raise GateError("step %s: shots_min must be >= 1 (0 asserts nothing; leave it out)"
                                % step_id)
        else:
            out["cmd"] = _want_str_list(step, "cmd", required=True)
            out["xdg"] = _want_str(step, "xdg")
            pattern = _want_str(step, "expect_stdout")
            if pattern is not None:
                try:
                    re.compile(pattern)
                except re.error as exc:
                    raise GateError("step %s: expect_stdout: bad regex /%s/: %s" % (step_id, pattern, exc))
            out["expect_stdout"] = pattern
        parsed.append(out)

    return {"name": name, "env_required": list(env_required), "steps": parsed}


def load_gate(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except OSError as exc:
        raise GateError("cannot read %s: %s" % (path, exc.strerror or exc))
    except ValueError as exc:
        raise GateError("%s: not valid JSON: %s" % (path, exc))
    return parse_gate(data, path)


# ---------------------------------------------------------------- small helpers

def expand(value, missing, env):
    """${VAR} from the run's environment. An unset name records itself and leaves the text usable."""
    if not isinstance(value, str):
        return value

    def sub(match):
        name = match.group(1)
        text = env.get(name)
        if text is None:
            if name not in missing:
                missing.append(name)
            return match.group(0)
        return text
    return VAR_RE.sub(sub, value)


def count_warnings(text):
    """Lines containing 'warning:', which is how gate.sh counted them."""
    return sum(1 for line in text.splitlines() if WARN_TEXT in line)


def shot_state(candidate, reference):
    """'equal' | 'different' | 'missing:X' for one shot_equal pair, compared byte for byte."""
    for path in (candidate, reference):
        if not os.path.isfile(path):
            return "missing:" + path
    with open(candidate, "rb") as fh:
        mine = fh.read()
    with open(reference, "rb") as fh:
        theirs = fh.read()
    return "equal" if mine == theirs else "different"


def count_shots(directory):
    """Files ending .png directly in `directory` - the shots a game run wrote there."""
    try:
        names = os.listdir(directory)
    except OSError:
        return 0
    return sum(1 for n in names
               if n.lower().endswith(PNG_SUFFIX) and os.path.isfile(os.path.join(directory, n)))


def mem_available_mib(path="/proc/meminfo"):
    """MemAvailable in MiB, or None when the kernel does not tell us (never a reason to skip the check)."""
    try:
        with open(path, "r") as fh:
            for line in fh:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) // 1024
    except (OSError, ValueError, IndexError):
        return None
    return None


def run_script(script, cwd):
    """Run an input script through the game, honouring a `#!/bin/sh` first line (gate.sh's inline scripts)."""
    if script.startswith("#!") or "\n" in script:
        path = os.path.join(cwd, "gate-input.txt")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(script if script.endswith("\n") else script + "\n")
        return "@" + path
    return script


class Proc:
    """One child in its own process group, so a timeout can take the whole tree with it."""

    def __init__(self, argv, cwd, env, stdout, stderr=None):
        self.argv = argv
        self.group = None
        self.proc = subprocess.Popen(argv, cwd=cwd, env=env, stdout=stdout, stderr=stderr or stdout,
                                     start_new_session=True)
        self.group = self.proc.pid

    def wait(self, timeout=None):
        """Return the exit status, or None after the timeout has killed the group.

        The child runs with stderr merged into the log file, so poll() cannot deadlock on a full
        pipe the way communicate() can when a step is killed mid-write.
        """
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            try:
                return self.proc.wait(0.2)
            except subprocess.TimeoutExpired:
                pass
            if deadline is not None and time.monotonic() >= deadline:
                self.kill()
                return None

    def kill(self):
        if self.group is None:
            return
        for sig, grace in ((signal.SIGTERM, TERM_GRACE_S), (signal.SIGKILL, KILL_WAIT_S)):
            try:
                os.killpg(self.group, sig)
            except ProcessLookupError:
                return
            except OSError:
                return
            end = time.monotonic() + grace
            while self.proc.poll() is None and time.monotonic() < end:
                time.sleep(0.1)
            if self.proc.poll() is not None:
                return


# ---------------------------------------------------------------- the runner

class Runner:
    def __init__(self, gate, repo, out, only=None, keep_going=False, write=sys.stdout,
                 env=None, sleep=time.sleep):
        self.gate = gate
        self.repo = repo
        self.out = out
        self.only = only
        self.keep_going = keep_going
        self.write = write
        self.env = dict(env or {})      # base environment for every step (os.environ when None)
        self.sleep = sleep
        self.xdg_used = set()
        self.lock_note = False       # 'no OPENUG2_HEAVY_LOCK' is said once per run, not per step
        self.results = []

    # ------------------------------------------------------------------ output

    def say(self, text):
        self.write(text + "\n")
        if hasattr(self.write, "flush"):
            self.write.flush()

    def report(self, step, status, detail, seconds=None):
        record = {"id": step["id"], "kind": step["kind"], "status": status,
                  "detail": detail, "seconds": seconds}
        self.results.append(record)
        self.say("%s  %s  %s" % (step["id"], status, detail))
        return record

    # ------------------------------------------------------------------ paths

    def step_dir(self, step):
        path = os.path.join(self.out, step["id"])
        os.makedirs(path, exist_ok=True)
        return path

    def log_path(self, step):
        return os.path.join(self.out, step["id"] + ".log")

    def xdg_dirs(self, step):
        """<out>/xdg/<name>/{c,d}; wiped the first time a name is used in this run, reused after that.

        A step without 'xdg' gets its own name, which is how every step is guaranteed a clean
        config/data home: one stale camera.cfg in the user's ~/.config once changed a screenshot.
        """
        name = step.get("xdg") or step["id"]
        root = os.path.join(self.out, "xdg", name)
        if name not in self.xdg_used:
            shutil.rmtree(root, ignore_errors=True)
            self.xdg_used.add(name)
        cfg, data = os.path.join(root, "c"), os.path.join(root, "d")
        os.makedirs(cfg, exist_ok=True)
        os.makedirs(data, exist_ok=True)
        return cfg, data

    # ------------------------------------------------------------------ environment

    def step_env(self, step, missing, base):
        env = dict(base)
        for key, value in sorted(step["env"].items()):
            env[expand(key, missing, base)] = expand(value, missing, base)
        return env

    def base_env(self):
        """The environment every step starts from: os.environ unless the caller supplied one."""
        return dict(self.env) if self.env else os.environ.copy()

    def heavy_prefix(self):
        lock = os.environ.get("OPENUG2_HEAVY_LOCK")
        if lock:
            return ["flock", lock, "nice", "-n", "10"]
        if not self.lock_note:
            self.say("note: OPENUG2_HEAVY_LOCK unset - heavy steps run without flock")
            self.lock_note = True
        return ["nice", "-n", "10"]

    def wait_for_memory(self):
        """MemAvailable >= MEM_MIN_MIB, waiting up to MEM_WAIT_S; returns None or a failure reason."""
        available = mem_available_mib()
        if available is None:
            return None
        if available >= MEM_MIN_MIB:
            return None
        waited = 0.0
        while waited < MEM_WAIT_S:
            self.sleep(MEM_POLL_S)
            waited += MEM_POLL_S
            available = mem_available_mib()
            if available is None or available >= MEM_MIN_MIB:
                return None
        return "low memory: MemAvailable %d MiB, need %d MiB (waited %.0f s)" % (
            available, MEM_MIN_MIB, waited)

    # ------------------------------------------------------------------ process run

    def spawn(self, argv, cwd, env, log_path, timeout, append=False):
        mode = "ab" if append else "wb"
        with open(log_path, mode) as log:
            proc = Proc(argv, cwd=cwd, env=env, stdout=log)
            started = time.monotonic()
            rc = proc.wait(timeout)
            elapsed = time.monotonic() - started
        if rc is None:
            return None, elapsed
        return rc, elapsed

    def read_log(self, log_path):
        try:
            with open(log_path, "rb") as fh:
                return fh.read().decode("utf-8", "replace")
        except OSError as exc:
            raise Refused("cannot read log %s: %s" % (log_path, exc.strerror or exc))

    # ------------------------------------------------------------------ kinds

    def run_make(self, step, missing, base):
        """One `make -j<jobs> <target>` per target, appending to one step log.

        One target at a time (as run-tests.sh does) rather than one make for all of them: a target
        that hangs or fails then names itself, instead of taking the whole step's timeout with it.
        """
        targets = [expand(t, missing, base) for t in step["targets"]]
        cwd = self.repo
        log_path = self.log_path(step)
        env = self.step_env(step, missing, base)
        prefix = self.heavy_prefix()
        deadline = time.monotonic() + step["timeout"]
        started = time.monotonic()
        touch_reason = None
        if step["touch"]:
            pattern = os.path.join(cwd, expand(step["touch"], missing, base))
            matches = sorted(glob.glob(pattern))
            if not matches:
                touch_reason = "touch '%s' matched nothing, so the build would not be forced" % step["touch"]
            for path in matches:
                try:
                    os.utime(path, None)
                except OSError as exc:
                    touch_reason = "cannot touch %s: %s" % (path, exc.strerror or exc)

        problems = [] if touch_reason is None else [touch_reason]
        for index, target in enumerate(targets):
            budget = deadline - time.monotonic()
            if budget <= 0:
                problems.append("step timeout %.0f s reached before '%s' started" % (step["timeout"], target))
                break
            argv = prefix + ["make", "-j%d" % step["jobs"], target]
            timeout = budget
            if step["target_timeout"] is not None:
                timeout = min(budget, step["target_timeout"])
            rc, _ = self.spawn(argv, cwd, env, log_path, timeout, append=index > 0)
            if rc is None:
                problems.append("'%s' timed out after %.0f s; killed the process group"
                                % (target, timeout))
                break
            if rc != 0:
                problems.append("'%s' exited %d" % (target, rc))
                if not self.keep_going:
                    break

        warnings = count_warnings(self.read_log(log_path))
        if step["max_warnings"] is not None and warnings > step["max_warnings"]:
            problems.append("%d 'warning:' lines > max_warnings %d" % (warnings, step["max_warnings"]))
        detail = "targets=%d warnings=%d" % (len(targets), warnings)
        if problems:
            return FAIL, detail + "; " + "; ".join(problems), time.monotonic() - started
        return PASS, detail, time.monotonic() - started

    def resolve_arg(self, value, cwd):
        """An `--input-script` value of '@tools/...' names a file in the repo, not in the step dir."""
        if value.startswith("@") and not os.path.isabs(value[1:]):
            candidate = os.path.join(self.repo, value[1:])
            if os.path.isfile(candidate):
                return "@" + candidate
        if value.startswith("@") and not os.path.isfile(os.path.join(cwd, value[1:])):
            # A script that names a file in neither place makes the game start with no script at all,
            # and a run with no scripted input proves nothing; say so instead of reporting a pass.
            raise Refused("input script %s not found in the repo or the step dir" % value)
        return value

    def game_argv(self, step, cwd, missing, base):
        binary = expand(step["binary"], missing, base)
        path = binary if os.path.isabs(binary) else os.path.join(self.repo, binary)
        if not os.path.isfile(path):
            raise Refused("game binary %s not found (build the 'nfsu2' target first)" % path)
        argv = [path]
        previous = None
        for value in step["args"]:
            value = expand(value, missing, base)
            if previous == "--input-script":
                value = run_script(value, cwd)
                if value.startswith("@"):
                    value = self.resolve_arg(value, cwd)
            argv.append(value)
            previous = value
        return argv

    def check_shots(self, step, cwd, missing, base):
        """(problems, notes) for shot_equal and shots_min; byte-identical or the step did not pass."""
        problems, notes = [], []
        for name, reference in sorted(step["shot_equal"].items()):
            name = expand(name, missing, base)
            reference = expand(reference, missing, base)
            state = shot_state(os.path.join(cwd, name), reference)
            if state == "equal":
                notes.append("%s == %s" % (name, os.path.basename(reference)))
            elif state == "different":
                problems.append("%s differs from %s" % (name, reference))
            else:
                problems.append("%s missing (%s)" % (name, state.split(":", 1)[1]))
        if step["shots_min"] is not None:
            found = count_shots(cwd)
            notes.append("shots %d/%d" % (found, step["shots_min"]))
            if found < step["shots_min"]:
                problems.append("only %d PNG(s) in the step dir, need %d" % (found, step["shots_min"]))
        return problems, notes

    def check_spec(self, step, log_path, base):
        """Run log_assert over the step log. No spec file -> FAIL: an unchecked rule is not a pass."""
        spec = os.path.join(self.repo, expand(step["spec"], [], base))
        if not os.path.isfile(spec):
            return ["spec %s not found in the repo" % step["spec"]], []
        assert_tool = os.path.join(HERE, "log_assert.py")
        if not os.path.isfile(assert_tool):
            return ["cannot run %s: %s not found" % (step["spec"], assert_tool)], []
        report_path = os.path.splitext(log_path)[0] + ".spec.log"
        with open(report_path, "wb") as report:
            proc = subprocess.run([sys.executable, "-I", assert_tool, spec, log_path],
                                  cwd=self.repo, stdout=report, stderr=subprocess.STDOUT)
        tally, first_fail = "", ""
        try:
            with open(report_path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.read().splitlines()
        except OSError:
            lines = []
        for line in lines:
            if first_fail and "rules," in line:
                tally = line.strip()
            elif line.startswith("FAIL ") and not first_fail:
                first_fail = line.strip()
        if proc.returncode == 0:
            return [], (["spec pass"] + ([tally] if tally else []))
        reason = first_fail or tally or ("log_assert exited %d" % proc.returncode)
        return ["spec %s failed: %s" % (step["spec"], reason)], []

    def run_game(self, step, missing, base):
        cwd = self.step_dir(step)
        cfg, data = self.xdg_dirs(step)
        env = self.step_env(step, missing, base)
        env["SDL_VIDEODRIVER"] = "offscreen"
        env["XDG_CONFIG_HOME"], env["XDG_DATA_HOME"] = cfg, data
        argv = self.heavy_prefix() + self.game_argv(step, cwd, missing, base)
        rc, elapsed = self.spawn(argv, cwd, env, self.log_path(step), step["timeout"])
        log_text = self.read_log(self.log_path(step))
        if rc is None:
            return FAIL, "timeout after %.0f s; killed the process group" % step["timeout"], elapsed
        problems, notes = [], []
        if rc != step["exit"]:
            problems.append("exit %s != %d" % (rc, step["exit"]))
        shot_problems, shot_notes = self.check_shots(step, cwd, missing, base)
        problems += shot_problems
        notes += shot_notes
        if step["spec"]:
            spec_problems, spec_notes = self.check_spec(step, self.log_path(step), base)
            problems += spec_problems
            notes += spec_notes
        if re.search(CRASH_FORBID, log_text):
            problems.append(CRASH_DETAIL)
        detail = "rc=%s" % rc + ((" " + "; ".join(notes)) if notes else "")
        if problems:
            return FAIL, detail + "; " + "; ".join(problems), elapsed
        return PASS, detail, elapsed

    def run_cmd(self, step, missing, base):
        cwd = self.step_dir(step)
        env = self.step_env(step, missing, base)
        if step.get("xdg"):
            cfg, data = self.xdg_dirs(step)
            env["XDG_CONFIG_HOME"], env["XDG_DATA_HOME"] = cfg, data
        argv = (self.heavy_prefix() if step["heavy"] else []) + [expand(a, missing, base) for a in step["cmd"]]
        rc, elapsed = self.spawn(argv, cwd, env, self.log_path(step), step["timeout"])
        if rc is None:
            return FAIL, "timeout after %.0f s; killed the process group" % step["timeout"], elapsed
        problems = []
        if rc != step["exit"]:
            problems.append("exit %s != %d" % (rc, step["exit"]))
        if step["expect_stdout"]:
            text = self.read_log(self.log_path(step))
            if re.search(step["expect_stdout"], text):
                notes = ["stdout matches /%s/" % step["expect_stdout"]]
            else:
                problems.append("stdout has no match for /%s/" % step["expect_stdout"])
                notes = []
        else:
            notes = []
        detail = "rc=%s" % rc + ((" " + "; ".join(notes)) if notes else "")
        if problems:
            return FAIL, detail + "; " + "; ".join(problems), elapsed
        return PASS, detail, elapsed

    # ------------------------------------------------------------------ the run

    def run_step(self, step):
        # env_required is the gate saying "these variables must be in the environment"; a step that
        # ran without one ran with the wrong inputs, so it is a FAIL and never a silent SKIP.
        base = self.base_env()
        missing = [name for name in self.gate["env_required"] if name not in base]
        try:
            heavy = step["kind"] in ("make", "game") or (step["kind"] == "cmd" and step["heavy"])
            if heavy:
                reason = self.wait_for_memory()
                if reason:
                    return report_of(FAIL, reason, 0.0)
            if step["kind"] == "make":
                status, detail, seconds = self.run_make(step, missing, base)
            elif step["kind"] == "game":
                status, detail, seconds = self.run_game(step, missing, base)
            else:
                status, detail, seconds = self.run_cmd(step, missing, base)
        except Refused as exc:
            status, detail, seconds = FAIL, str(exc), 0.0
        if missing:
            detail = "env %s unset; %s" % (", ".join(missing), detail)
            status = FAIL
        return report_of(status, detail, seconds)

    def run(self):
        os.makedirs(self.out, exist_ok=True)
        stopped = False
        selected = None
        if self.only is not None:
            selected = set(self.only)
        for step in self.gate["steps"]:
            if selected is not None and step["id"] not in selected:
                self.report(step, SKIP, "not selected by --only")
                continue
            if stopped:
                self.report(step, NOT_RUN, "earlier step failed; re-run with --keep-going")
                continue
            outcome = self.run_step(step)
            seconds = outcome.pop("seconds", None)
            self.report(step, outcome["status"], outcome["detail"], seconds)
            if outcome["status"] != PASS and not self.keep_going:
                stopped = True
        return self.verdict()

    def verdict(self):
        selected = set(self.only) if self.only is not None else {s["id"] for s in self.gate["steps"]}
        chosen = [r for r in self.results if r["id"] in selected]
        counts = {status: 0 for status in STATUSES}
        for record in self.results:
            counts[record["status"]] += 1
        bad = [r["id"] for r in chosen if r["status"] != PASS]
        code = EXIT_PASS if not bad else EXIT_FAIL
        return {"name": self.gate["name"], "repo": self.repo, "out": self.out, "exit": code,
                "steps": self.results, "counts": counts, "not_pass": bad}


def report_of(status, detail, seconds):
    return {"status": status, "detail": detail, "seconds": seconds}


# ---------------------------------------------------------------- summaries

def write_summaries(out, verdict, started, finished):
    ts_path = os.path.join(out, "summary.tsv")
    js_path = os.path.join(out, "summary.json")
    with open(ts_path, "w", encoding="utf-8") as fh:
        fh.write("id\tkind\tstatus\tseconds\tdetail\n")
        for record in verdict["steps"]:
            seconds = "" if record["seconds"] is None else "%.1f" % record["seconds"]
            fh.write("%s\t%s\t%s\t%s\t%s\n" % (record["id"], record["kind"], record["status"],
                                               seconds, record["detail"]))
    payload = dict(verdict)
    payload["started"] = started
    payload["finished"] = finished
    with open(js_path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1, sort_keys=True)
        fh.write("\n")
    return ts_path, js_path


# ---------------------------------------------------------------- cli

def parse_env(items):
    """['A=b', 'C'] -> {'A': 'b', 'C': None}; a bare NAME means 'remove it from the child env'."""
    overlay = {}
    for item in items:
        name, sep, value = item.partition("=")
        if not name or (sep and not value):
            raise GateError("--env wants NAME=VALUE (or a bare NAME to unset), got %r" % item)
        overlay[name] = value if sep else None
    return overlay


def parse_only(text, gate):
    ids = [item for item in re.split(r"[,\s]+", (text or "").strip()) if item]
    if not ids:
        raise GateError("--only needs at least one step id")
    known = {step["id"] for step in gate["steps"]}
    unknown = [item for item in ids if item not in known]
    if unknown:
        raise GateError("--only names unknown step(s): %s (known: %s)"
                        % (", ".join(unknown), ", ".join(sorted(known))))
    return ids


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gate_run.py",
        description="run a gate file and end in one machine verdict",
        epilog="the gate file format is documented in tools/gates/README.md")
    parser.add_argument("gate", metavar="GATEFILE", help="gate file (JSON)")
    parser.add_argument("--repo", metavar="DIR", default=os.getcwd(),
                        help="repo to run from (default: cwd)")
    parser.add_argument("--out", metavar="DIR",
                        help="where to put logs and shots (default: ./gate-out/<gate name>)")
    parser.add_argument("--only", metavar="ID[,ID..]", help="run only these step ids")
    parser.add_argument("--env", metavar="NAME=VAL", action="append", default=[],
                        help="add NAME=VAL to the child environment (repeatable); a bare NAME "
                             "removes it, which is how a gate can be shown to fail on an unset variable")
    parser.add_argument("--list", action="store_true", help="print step ids and kinds, run nothing")
    parser.add_argument("--keep-going", action="store_true",
                        help="keep running after a step fails (default: stop at the first FAIL)")
    try:
        args = parser.parse_args(argv[1:] if argv is not None else None)
    except SystemExit as exc:
        return EXIT_PASS if not exc.code else EXIT_USAGE

    try:
        gate = load_gate(args.gate)
        repo = os.path.abspath(args.repo)
        if not os.path.isdir(repo):
            raise GateError("--repo %s is not a directory" % args.repo)
        out = os.path.abspath(args.out or os.path.join(os.getcwd(), "gate-out", gate["name"]))
        only = parse_only(args.only, gate) if args.only else None
        overlay = parse_env(args.env)
    except GateError as exc:
        sys.stderr.write("gate_run: %s\n" % exc)
        return EXIT_USAGE

    if args.list:
        for step in gate["steps"]:
            print("%s  %s" % (step["id"], step["kind"]))
        return EXIT_PASS

    base = os.environ.copy()
    for name, value in overlay.items():
        if value is None:
            base.pop(name, None)
        else:
            base[name] = value
    runner = Runner(gate, repo, out, only=only, keep_going=args.keep_going,
                    env=base, sleep=time.sleep)
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    verdict = runner.run()
    finished = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    ts_path, js_path = write_summaries(out, verdict, started, finished)
    tally = ", ".join("%s %d" % (status, verdict["counts"][status]) for status in STATUSES)
    print("%s: %s%s" % (verdict["name"], "GATE PASS" if verdict["exit"] == EXIT_PASS else "GATE FAIL",
                        "" if not verdict["not_pass"] else " (not PASS: %s)" % ", ".join(verdict["not_pass"])))
    print("%s; summaries: %s, %s" % (tally, ts_path, js_path))
    return verdict["exit"]


if __name__ == "__main__":
    sys.exit(main())
