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
is no `flock`, and the runner says so once. The lock is the caller's to set and to *share*:
serialising only works if every caller names the same path, and the runner cannot see who else is
running, so it warns rather than pretending. `tools/gates/run.sh` exports one.

Before each heavy step `MemAvailable` from `/proc/meminfo` is checked: below 2500 MiB the runner waits
up to 10 minutes, polling every 30 s. If the wait runs out it still starts above 1200 MiB, with a note
in the output saying the step ran under pressure - a step that passes while swapping has passed, and
refusing a run this board would have served is a verdict nobody asked for. Below 1200 MiB it FAILs the
step with the numbers it saw.

## Running it

    python3 -I tools/gate_run.py tools/gates/m1.json

That is the contract: stop at the first FAIL and mark the rest `NOT RUN`. Add `--keep-going` for a
full diagnostic of a branch, where every step reports its own verdict; the two produce different
summary counts, so say which one a set of results came from. A whole-gate run takes an hour or two on
this board, most of it in the two builds and the scripted races - run it detached, not in a shell
that can die with your session.

## Adding a gate

Copy `m1.json`, change the steps, and add a spec per game step - a game step with no `spec`, no
`shot_equal` and no `shots_min` passes on exit status alone, which is how a silent run looks green.
`tools/gate_run_test.py` covers the runner's logic with fake gates that run `true` and `false`; it
needs no game and no data dir. `tools/spec_negative_check.py` checks the other half - that every
spec the gate uses can actually fail, by breaking one meaningful line in a real gate log and
insisting the spec rejects it (`python3 -I tools/spec_negative_check.py [log dir]`).

## Where the pieces live

| path | holds |
| --- | --- |
| `tools/gates/<name>.json` | the gate: steps, in the order they run |
| `tools/log_specs/<step>.spec` | one spec per game step, the rules its log must satisfy |
| `tools/gate_inputs/<step>.txt` | the longer `--input-script` files, one event per line |

`m1.json`'s steps and specs pair up by name (`s7s_shop_ask` -> `s7s_shop_ask.spec`), and a step's
`xdg` name is what shares a save between steps (`s7` -> `s7b`). The three shortest scripts are inline
in the gate file; anything worth reading line by line belongs in `gate_inputs/`.

Five things a ported run tends to smuggle in, all seen while porting runs out of `reports/`:
- **A scripted run's frame numbers belong to the state it was calibrated on.** The S2/S3 world-map
  run (`s7s_shop_ask` and `worldmap_gps` are its gate versions) started from a copied career save,
  so it drove straight out of the menu; from a fresh profile the same frames land in the mandatory
  Stage 0 car lot and the run never leaves it. Port the sequence, then re-derive the frames against a
  run of your own - the shot lines and `SCRIPT f=` probe lines in the log say where each frame really
  landed.
- **The front end swallows input on a schedule.** `fe_screen.c` ignores every key while a carousel's
  authored exit script runs, and `pause_screen.c` drops the first Accept/Back after a screen opens.
  A press 30 frames after the last one can simply not exist as far as the game is concerned, so
  scripted pause-menu sequences need real gaps between presses - and a step that fails with the game
  parked in `screen=PAUSE` to its last frame is usually that, not a missing feature.
  `tools/gate_inputs/worldmap_gps.txt` records the gap it needed and why.
- **Two ways to take a screenshot print two different lines.** A `shot=name.png` event in an input
  script prints `script: wrote name.png (WxH)`; the `--shot` flag prints `wrote name.png (WxH) after
  driving to (X,Y)`. A spec written against one rejects the other, and the step's own "closest lines"
  hint is what shows it - `tools/log_specs/mirror_on.spec` asserts the `--shot` wording because
  `mirror_on` has no input script at all.
- **A step's `timeout` is wall clock, and this board's wall clock is not the frame rate.** A scripted
  step needs at least one second per frame, and can need six times that when another worktree is
  building while it runs. Two rules keep a good run from being killed by its own budget: end the input
  script with `quit` on the frame right after the last thing that has to happen (nothing may be
  scheduled after it, including shots), and read the seconds back off `summary.tsv` - it is the
  runner's own measurement, where `SCRIPT f=` lines only count frames.
  Measured on a board with nothing else running (`worldmap_gps`, 640x480, unlocked): 2001 scripted
  frames in 217.7 s, so around 9 frames a second of real time. The frames themselves are cheap; the
  startup, streaming and menu waits are what the budget pays for. Size a `timeout` off that, not off
  the frame count, and never off another machine's.
- **`Escape` is the pause key as well as the back key.** A step that taps Escape to leave a menu gets
  a pause menu instead if that menu had already closed itself, and every driving key after it is
  swallowed by the pause screen with nothing in the log to say so. Where one input both makes the
  choice and drops the menu - the world map does, `Return` sets the destination *and* closes it - use
  that input and skip the Escape. The `SCRIPT f= ... screen=` probe lines are what show a menu nobody
  ordered.

