# Gate file format

A gate is one JSON file listing the steps that prove a branch is sound. The runner is
`tools/gate_run.py`; the port of the orchestrator's hand-written gate for parity/m1 is `m1.json`
here. Run it:

    python3 -I tools/gate_run.py tools/gates/m1.json                 # whole gate
    python3 -I tools/gate_run.py tools/gates/m1.json --list          # step ids and kinds, run nothing
    python3 -I tools/gate_run.py tools/gates/m1.json --only build,tests
    python3 -I tools/gate_run.py tools/gates/m1.json --keep-going     # do not stop at the first FAIL

Exit code 0 only if every selected step PASSed; 1 if any selected step FAILed, SKIPped or was
NOT RUN; 2 for a bad gate file or a bad command line. Logs land in `<out>/<id>.log`, screenshots
and any file the run writes in `<out>/<id>/`, and the verdict repeats in `<out>/summary.tsv` and
`<out>/summary.json`.

## Top level

| key | required | meaning |
| --- | --- | --- |
| `name` | yes | gate name; also the default `--out` folder name |
| `steps` | yes | the steps, run in this order |
| `env_required` | no | variables that must be set; a step that ran without one is FAIL, not SKIP |

## Every step

| key | meaning |
| --- | --- |
| `id` | unique, `[A-Za-z0-9._-]+` (it names directories under `--out`) |
| `kind` | `make`, `game` or `cmd` |
| `timeout` | seconds for the whole step, default 900; on expiry the process group is killed and the step FAILs |
| `env` | extra `NAME: VALUE` pairs for this step's child process |

`${VAR}` in any string of a step is expanded from the environment the run started with. A variable
that is not set FAILs the step with `env VAR unset` - a step that runs with an empty data path is
worse than one that does not run.

Steps run one at a time. Without `--keep-going` the first FAIL stops the run and every later step is
reported `NOT RUN`, never `PASS`.

## kind: make

| key | meaning |
| --- | --- |
| `targets` | one `make -j<jobs> <target>` per entry, in order |
| `jobs` | `-j` value, default 2 (this board has no more to give) |
| `touch` | glob of files to touch first, to force a rebuild; matching nothing is a FAIL, since the rebuild would silently not be forced |
| `max_warnings` | maximum number of log lines containing `warning:` |
| `target_timeout` | per-target timeout; the step still has `timeout` as its total budget |

PASS is every target exiting 0 and the warning count within `max_warnings`. Targets are run one at a
time rather than in one `make` call so that a hanging target names itself instead of eating the
step's whole timeout.

## kind: game

| key | meaning |
| --- | --- |
| `binary` | game to run, relative to the repo (usually `nfsu2`) |
| `args` | arguments, in order |
| `xdg` | name of the config/data folder to run under (see below) |
| `exit` | expected exit status, default 0 |
| `spec` | log spec for `tools/log_assert.py`, relative to the repo |
| `shot_equal` | `{ "<file in the step dir>": "<reference path>" }`, compared byte for byte |
| `shots_min` | minimum number of PNGs that must exist in the step dir |

The child runs with cwd = `<out>/<id>/`, so bare `shot=name.png` filenames land there.
`SDL_VIDEODRIVER=offscreen` is always set. A `--input-script` value can be a one-line script (passed
through), a multi-line script or a `#!`-script (written to `<out>/<id>/gate-input.txt`), or `@path`:
a path relative to the repo is rewritten to an absolute one, so `@tools/gate_inputs/s2.txt` works
from any step dir.

PASS is: exit status as expected, AND the spec passes, AND every `shot_equal` pair is identical, AND
at least `shots_min` PNGs exist, AND the log has no `Segmentation fault`. Each failed condition is
named in the step's detail.

### xdg folders

A step with `"xdg": "s7"` runs with `XDG_CONFIG_HOME=<out>/xdg/s7/c` and
`XDG_DATA_HOME=<out>/xdg/s7/d`. The folder is wiped the first time that name is used in a run; a
later step with the same name reuses it, which is how a save written by one step is read back by the
next (s7 -> s7b, p3-desktop -> p3-cold). A step without `xdg` gets a folder named after itself, so it
is always fresh. Never the user's real `~/.config`: one stale `camera.cfg` there once changed a
screenshot, which is why this exists at all.

## kind: cmd

| key | meaning |
| --- | --- |
| `cmd` | the command and its arguments, run with cwd = the repo |
| `expect_stdout` | a Python regex that must match somewhere in the step's output |
| `heavy` | `false` opts the step out of the lock and the memory wait |

## Heavy steps and memory

`make` and `game` steps, and `cmd` steps unless `heavy: false`, run as

    flock "$OPENUG2_HEAVY_LOCK" nice -n 10 <command>

so two agents on this board cannot build or race at the same time. Without `OPENUG2_HEAVY_LOCK` there
is no `flock`, and the runner says so once. Before each heavy step `MemAvailable` from
`/proc/meminfo` is checked: below 2500 MiB the runner waits up to 10 minutes, polling every 30 s, and
then FAILs the step with `low memory`.

## Adding a gate

Copy `m1.json`, change the steps, and add a spec per game step - a game step with no `spec`, no
`shot_equal` and no `shots_min` passes on exit status alone, which is how a silent run looks green.
`tools/gate_run_test.py` covers the runner's logic with fake gates that run `true` and `false`; it
needs no game and no data dir.
