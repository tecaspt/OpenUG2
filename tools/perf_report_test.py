#!/usr/bin/env python3
"""Self-test for tools/perf_report.py. Synthetic CSVs live in a temp dir only."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "perf_report.py")

COUNTERS = ["traffic", "racers", "target", "ai_visible", "draws", "contact_passes",
            "pair_checks", "pair_hits", "world_fixes", "wall_candidates",
            "spawn_attempts", "spawn_checks"]
SECS = ["top", "player_sim", "ai_follow", "ai_step", "ai_contact", "ai_spawn", "camera",
        "world_draw", "car_shadow", "player_draw", "ai_draw", "mid_draw", "ai_lamps",
        "post_draw", "swap"]
GPU = ["gpu_lag_frame", "gpu_scene", "gpu_ai_cars", "gpu_rest", "gpu_total"]
HEADER = (["frame"] + COUNTERS + ["cpu_" + s for s in SECS] + ["cpu_total"] + GPU)

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL: " + what)


def make_csv(path, n, base, spike=None, gpu=False, eol="\n", tail=""):
    """base: dict section->ms. spike: (every, section, extra_ms)."""
    lines = [",".join(HEADER)]
    for f in range(n):
        ms = dict(base)
        if spike and f % spike[0] == spike[0] - 1:
            ms[spike[1]] = ms.get(spike[1], 0.0) + spike[2]
        total = sum(ms.get(s, 0.0) for s in SECS)
        cells = [str(f)]
        for i, _ in enumerate(COUNTERS):
            cells.append(str((f * (i + 3)) % 17 + i))
        cells += ["%.4f" % ms.get(s, 0.0) for s in SECS]
        cells.append("%.4f" % total)
        if gpu:
            cells += [str(f - 3), "2.0000", "1.0000", "1.5000", "4.5000"]
        else:
            cells += ["", "", "", "", ""]
        lines.append(",".join(cells))
    with open(path, "w", newline="") as fh:
        fh.write(eol.join(lines) + eol + tail)


def run(path, *extra):
    p = subprocess.run([sys.executable, "-I", TOOL, path] + list(extra),
                       capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def verdict(out):
    return out.split("== 7. Verdict ==")[-1]


def main():
    smooth = {"top": 0.1, "player_sim": 0.5, "ai_step": 0.5, "world_draw": 3.0,
              "ai_draw": 1.0, "swap": 1.0}
    with tempfile.TemporaryDirectory() as d:
        # 1. steady 60 fps (16.6 ms per frame)
        steady = dict(smooth, swap=11.5)  # total 16.6
        p = os.path.join(d, "steady.csv")
        make_csv(p, 600, dict(steady, world_draw=3.0, swap=11.5))
        rc, out, err = run(p)
        check(rc == 0, "steady: exit 0")
        check("rows read: 600, rows skipped: 0" in out, "steady: row counts")
        check("duration at 60 fps: 10.0 s" in out, "steady: duration")
        check("frames over 16.7 ms: 0.0%" in out, "steady: no slow frames")
        check("no hitches" in out, "steady: no hitches")
        check("no GPU timings in this file" in out, "steady: gpu absent message")

        # 2. swap-bound
        p = os.path.join(d, "swap.csv")
        make_csv(p, 600, {"top": 0.1, "world_draw": 2.0, "ai_draw": 1.0, "swap": 20.0})
        rc, out, err = run(p)
        check(rc == 0 and "GPU/vsync-bound" in verdict(out), "swap-bound verdict")
        check("<- time blocked in buffer swap" in out, "swap row marked")

        # 3. CPU-bound in world_draw
        p = os.path.join(d, "cpu.csv")
        make_csv(p, 600, {"top": 0.2, "player_sim": 1.0, "world_draw": 30.0, "ai_draw": 4.0, "swap": 1.0})
        rc, out, err = run(p)
        v = verdict(out)
        check("CPU-bound" in v and "cpu_world_draw" in v, "cpu-bound names world_draw")
        first_sec = [l for l in out.split("== 3.")[1].splitlines() if l.startswith("  cpu_")][0]
        check(first_sec.split()[0] == "cpu_world_draw", "sections sorted by share")

        # 4. periodic hitches from ai_spawn
        p = os.path.join(d, "hitch.csv")
        make_csv(p, 1000, {"top": 0.1, "world_draw": 4.0, "ai_draw": 1.0, "swap": 2.0},
                 spike=(100, "ai_spawn", 60.0))
        rc, out, err = run(p)
        v = verdict(out)
        check("Hitch-bound" in v and "cpu_ai_spawn" in v, "hitch-bound names ai_spawn")
        check("hitch frames: 10 " in out and "10 episode(s)" in out, "hitch count and episodes")
        worst = out.split("== 4.")[1].split("== 5.")[0]
        check("cpu_ai_spawn" in worst, "hitch cause listed per frame")

        # 5. GPU columns present
        p = os.path.join(d, "gpu.csv")
        make_csv(p, 300, steady, gpu=True)
        rc, out, err = run(p)
        g = out.split("== 6.")[1].split("== 7.")[0]
        check("no GPU timings" not in g and "gpu_total" in g and "4.50" in g, "gpu present stats")

        # 6. truncated last line
        p = os.path.join(d, "trunc.csv")
        make_csv(p, 200, steady, tail="200,1,2,3")
        rc, out, err = run(p)
        check(rc == 0 and "rows read: 200, rows skipped: 1" in out and "first:" in out, "truncated last line skipped")

        # 7. CRLF
        p = os.path.join(d, "crlf.csv")
        make_csv(p, 200, steady, eol="\r\n")
        rc, out, err = run(p)
        check(rc == 0 and "rows read: 200, rows skipped: 0" in out, "CRLF handled")

        # 8. extra / missing trailing columns, empty cells
        p = os.path.join(d, "cols.csv")
        make_csv(p, 100, steady)
        with open(p) as fh:
            ls = fh.read().split("\n")
        ls[2] = ls[2] + ",9,9"                          # extra columns
        ls[3] = ",".join(ls[3].split(",")[:-5])         # gpu columns missing
        ls[4] = ls[4].replace(",3.0000,", ",,", 1)      # empty cell
        with open(p, "w") as fh:
            fh.write("\n".join(ls))
        rc, out, err = run(p)
        check(rc == 0 and "rows read: 100" in out, "extra/missing columns tolerated")

        # 9. garbage and empty and missing files exit 2 without traceback
        p = os.path.join(d, "garbage.csv")
        with open(p, "wb") as fh:
            fh.write(bytes(range(256)) * 20)
        rc, out, err = run(p)
        check(rc == 2 and "Traceback" not in err and err.strip(), "garbage file exit 2")
        p = os.path.join(d, "empty.csv")
        open(p, "w").close()
        rc, out, err = run(p)
        check(rc == 2, "empty file exit 2")
        rc, out, err = run(os.path.join(d, "nope.csv"))
        check(rc == 2 and "Traceback" not in err, "missing file exit 2")
        p = os.path.join(d, "hdr.csv")
        with open(p, "w") as fh:
            fh.write(",".join(HEADER) + "\n")
        rc, out, err = run(p)
        check(rc == 2, "header-only file exit 2")
        rc, out, err = run(os.path.join(d, "steady.csv"), "--fps", "abc")
        check(rc == 2, "bad --fps exit 2")

        # 10. column order independence (parse by header name)
        p = os.path.join(d, "shuf.csv")
        make_csv(p, 100, steady)
        with open(p) as fh:
            rows = [l.split(",") for l in fh.read().strip().split("\n")]
        order = list(reversed(range(len(HEADER))))
        with open(p, "w") as fh:
            for r in rows:
                fh.write(",".join(r[i] for i in order) + "\n")
        rc, out, err = run(p)
        check(rc == 0 and "rows read: 100, rows skipped: 0" in out and "mean 16.60" in out, "header-name parsing")

    print("perf_report_test: %d checks, %d failures" % (checks, failures))
    print("PASS" if failures == 0 else "FAIL")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
