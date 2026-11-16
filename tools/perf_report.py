#!/usr/bin/env python3
"""Frame-time report for OpenUG2 per-frame profile CSVs (--perf-csv / OpenUG2-perf.csv).

Usage: python3 -I tools/perf_report.py FILE.csv [--fps 60]
Standard library only. Columns are looked up by header name, never by position.
Exit status: 0 ok, 2 unreadable / unusable file.
"""
import sys
import math

COUNTERS = ["traffic", "racers", "target", "ai_visible", "draws", "contact_passes",
            "pair_checks", "pair_hits", "world_fixes", "wall_candidates",
            "spawn_attempts", "spawn_checks"]
GPU_SECTIONS = ["gpu_scene", "gpu_ai_cars", "gpu_rest"]
SMOOTH_MS = 33.3


class ReportError(Exception):
    pass


def pct(sorted_vals, p):
    """Linear-interpolated percentile of an already sorted list."""
    n = len(sorted_vals)
    if n == 0:
        return float("nan")
    if n == 1:
        return sorted_vals[0]
    k = (n - 1) * p / 100.0
    lo = int(math.floor(k))
    hi = min(lo + 1, n - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def mean(v):
    return sum(v) / len(v) if v else float("nan")


def num(cell):
    """Float from a cell, or None if empty/unparseable/non-finite."""
    cell = cell.strip()
    if not cell:
        return None
    try:
        x = float(cell)
    except ValueError:
        return None
    if math.isnan(x) or math.isinf(x):
        return None
    return x


def load(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ReportError("cannot read %s: %s" % (path, e.strerror or e))
    text = raw.decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    # drop a trailing empty piece from the final newline
    if lines and lines[-1] == "":
        lines.pop()
    if not lines:
        raise ReportError("file is empty")
    header = [h.strip().lstrip("﻿") for h in lines[0].split(",")]
    if "frame" not in header or "cpu_total" not in header:
        raise ReportError("not an OpenUG2 perf CSV (header lacks 'frame' and 'cpu_total')")
    idx = {}
    for i, h in enumerate(header):
        idx.setdefault(h, i)
    rows = []
    skipped = 0
    first_reason = None
    ncols = len(header)
    for ln, line in enumerate(lines[1:], start=2):
        if not line.strip():
            continue
        cells = line.split(",")
        reason = None
        row = {}
        ft = num(cells[idx["frame"]]) if idx["frame"] < len(cells) else None
        tot = num(cells[idx["cpu_total"]]) if idx["cpu_total"] < len(cells) else None
        if ft is None:
            reason = "line %d: missing or bad frame number" % ln
        elif tot is None:
            reason = "line %d: missing or bad cpu_total (%d of %d cells)" % (ln, len(cells), ncols)
        elif tot < 0:
            reason = "line %d: negative cpu_total" % ln
        if reason:
            skipped += 1
            if first_reason is None:
                first_reason = reason
            continue
        for name, i in idx.items():
            row[name] = num(cells[i]) if i < len(cells) else None
        rows.append(row)
    if not rows:
        raise ReportError("no usable rows (%d skipped%s)" %
                          (skipped, "; first: " + first_reason if first_reason else ""))
    cpu_secs = [h for h in header if h.startswith("cpu_") and h != "cpu_total"]
    return rows, skipped, first_reason, cpu_secs, idx


def fmt(x, w=7):
    return ("%*.2f" % (w, x)) if x == x else "%*s" % (w, "n/a")


def out(s=""):
    sys.stdout.write(s + "\n")


def section_stats(rows, cpu_secs, total_mean):
    st = []
    for s in cpu_secs:
        v = [r[s] for r in rows if r.get(s) is not None]
        if not v:
            continue
        sv = sorted(v)
        m = sum(v) / len(rows)  # missing cells count as 0 so shares add up
        st.append({"name": s, "mean": m, "p50": pct(sv, 50), "p95": pct(sv, 95),
                   "share": 100.0 * m / total_mean if total_mean > 0 else 0.0})
    st.sort(key=lambda d: -d["share"])
    return st


def bucket_by(rows, key, nb, label):
    pairs = [(r[key], r["cpu_total"]) for r in rows if r.get(key) is not None]
    if len(pairs) < 8:
        out("  by %s: not enough data" % label)
        return
    vals = sorted(p[0] for p in pairs)
    if vals[0] == vals[-1]:
        out("  by %s: constant (%g), no relation measurable" % (label, vals[0]))
        return
    edges = [pct(vals, 100.0 * i / nb) for i in range(1, nb)]
    groups = [[] for _ in range(nb)]
    for x, t in pairs:
        b = 0
        while b < nb - 1 and x > edges[b]:
            b += 1
        groups[b].append((x, t))
    out("  by %s:" % label)
    prev_hi = None
    for g in groups:
        if not g:
            continue
        lo = min(p[0] for p in g)
        hi = max(p[0] for p in g)
        out("    %s %g..%g: %6d frames, mean cpu_total %s ms" %
            (label, lo, hi, len(g), fmt(mean([p[1] for p in g])).strip()))
        prev_hi = hi
    ms = [mean([p[1] for p in g]) for g in groups if g]
    if len(ms) >= 2 and ms[0] > 0:
        out("    -> top bucket costs %.2fx the bottom bucket" % (ms[-1] / ms[0]))


def report(path, fps):
    rows, skipped, reason, cpu_secs, idx = load(path)
    n = len(rows)
    tot = sorted(r["cpu_total"] for r in rows)
    tmean = mean(tot)
    p50, p90, p95, p99 = (pct(tot, p) for p in (50, 90, 95, 99))

    out("== 1. Input ==")
    out("rows read: %d, rows skipped: %d%s" % (n, skipped, " (first: %s)" % reason if reason else ""))
    out("duration at %g fps: %.1f s (%d frames)" % (fps, n / fps, n))
    out()
    out("== 2. Frame time (cpu_total, ms) ==")
    out("mean %.2f  p50 %.2f  p90 %.2f  p95 %.2f  p99 %.2f  max %.2f" %
        (tmean, p50, p90, p95, p99, tot[-1]))
    o167 = 100.0 * sum(1 for t in tot if t > 16.7) / n
    o333 = 100.0 * sum(1 for t in tot if t > 33.3) / n
    out("frames over 16.7 ms: %.1f%%   over 33.3 ms: %.1f%%" % (o167, o333))
    out("effective fps (1000/mean): %.1f   1%% low fps (1000/p99): %.1f" %
        (1000.0 / tmean if tmean > 0 else float("inf"), 1000.0 / p99 if p99 > 0 else float("inf")))
    out("note: cpu_total is CPU time incl. blocking swap; it is the frame time on a vsynced device")
    out()

    st = section_stats(rows, cpu_secs, tmean)
    out("== 3. Sections (sorted by share of mean total) ==")
    out("  %-18s %8s %8s %8s" % ("section", "mean ms", "p95 ms", "share"))
    for d in st:
        tag = ""
        if d["name"] == "cpu_swap":
            tag = "  <- time blocked in buffer swap: large share = GPU/vsync-bound"
        out("  %-18s %s %s %7.1f%%%s" % (d["name"], fmt(d["mean"], 8), fmt(d["p95"], 8), d["share"], tag))
    out()

    med = {d["name"]: d["p50"] for d in st}
    thr = max(2.0 * p50, SMOOTH_MS)
    hit = [r for r in rows if r["cpu_total"] > thr]
    out("== 4. Hitches (cpu_total > max(2 x median, 33.3 ms) = %.1f ms) ==" % thr)
    hitch_cause = {}
    if not hit:
        out("no hitches")
    else:
        episodes = 0
        prev = None
        order = {id(r): i for i, r in enumerate(rows)}
        for r in hit:
            i = order[id(r)]
            if prev is None or i != prev + 1:
                episodes += 1
            prev = i
        out("hitch frames: %d (%.2f%% of frames) in %d episode(s)" % (len(hit), 100.0 * len(hit) / n, episodes))
        worst = sorted(hit, key=lambda r: -r["cpu_total"])[:10]
        out("  %-10s %9s  %-18s %s" % ("frame", "total ms", "grew most", "(ms vs its median)"))
        for r in worst:
            best, bd = None, -1e30
            for s in cpu_secs:
                v = r.get(s)
                if v is None or s not in med:
                    continue
                d = v - med[s]
                if d > bd:
                    best, bd = s, d
            out("  %-10d %9.1f  %-18s +%.1f ms (%.1f vs %.2f)" %
                (r["frame"], r["cpu_total"], best, bd, r.get(best) or 0.0, med.get(best, 0.0)))
        for r in hit:
            best, bd = None, -1e30
            for s in cpu_secs:
                v = r.get(s)
                if v is None or s not in med:
                    continue
                if v - med[s] > bd:
                    best, bd = s, v - med[s]
            hitch_cause[best] = hitch_cause.get(best, 0) + 1
    out()

    out("== 5. Load relation (mean cpu_total ms) ==")
    bucket_by(rows, "draws", 4, "draws")
    bucket_by(rows, "traffic", 4, "traffic")
    out()

    out("== 6. GPU timings ==")
    gpu_total = sorted(r["gpu_total"] for r in rows if r.get("gpu_total") is not None)
    gpu_med = None
    if not gpu_total:
        out("no GPU timings in this file")
    else:
        out("%d rows with GPU timings" % len(gpu_total))
        out("  %-14s %8s %8s" % ("gpu span", "p50 ms", "p95 ms"))
        for s in ["gpu_total"] + GPU_SECTIONS:
            v = sorted(r[s] for r in rows if r.get(s) is not None)
            if v:
                out("  %-14s %s %s" % (s, fmt(pct(v, 50), 8), fmt(pct(v, 95), 8)))
        gpu_med = pct(gpu_total, 50)
    out()

    out("== 7. Verdict ==")
    swap = next((d for d in st if d["name"] == "cpu_swap"), None)
    swap_share = swap["share"] if swap else 0.0
    non_swap = [d for d in st if d["name"] != "cpu_swap"]
    top2 = non_swap[:2]
    top2_txt = ", ".join("%s %.1f%% (%.2f ms)" % (d["name"], d["share"], d["mean"]) for d in top2)
    hitchy = len(hit) > 0 and (o333 >= 0.5 or len(hit) >= 3) and p50 <= 16.7 * 1.25
    over = p50 > 16.7
    if swap_share >= 50.0:
        out("GPU/vsync-bound: cpu_swap is %.1f%% of frame time (%.2f ms of %.2f ms mean)." %
            (swap_share, swap["mean"], tmean))
        out("  The CPU work before the swap is only %.2f ms; fix GPU cost (draws, overdraw, resolution), not CPU code." %
            (tmean - swap["mean"]))
        if gpu_med is not None:
            out("  GPU timer agrees: gpu_total p50 %.2f ms." % gpu_med)
    elif hitchy and hitch_cause:
        cs = max(hitch_cause.items(), key=lambda kv: kv[1])
        out("Hitch-bound: median %.2f ms is smooth but %d frames (%.2f%%) exceed %.1f ms; p99 %.2f ms, max %.2f ms." %
            (p50, len(hit), 100.0 * len(hit) / n, thr, p99, tot[-1]))
        out("  Spikes come mostly from %s (%d of %d hitch frames)." % (cs[0], cs[1], len(hit)))
    elif over:
        out("CPU-bound: median %.2f ms exceeds the 16.7 ms budget; swap share is only %.1f%%." % (p50, swap_share))
        out("  Top sections: %s." % top2_txt)
    else:
        out("Smooth: median %.2f ms, p99 %.2f ms, %.1f%% of frames over 16.7 ms, swap share %.1f%%." %
            (p50, p99, o167, swap_share))
        out("  Heaviest sections: %s." % top2_txt)
        if hit and hitch_cause:
            cs = max(hitch_cause.items(), key=lambda kv: kv[1])
            out("  %d rare hitch frame(s), mostly from %s." % (len(hit), cs[0]))
    return 0


def main(argv):
    path = None
    fps = 60.0
    i = 1
    try:
        while i < len(argv):
            a = argv[i]
            if a == "--fps":
                i += 1
                fps = float(argv[i])
                if not fps > 0 or math.isinf(fps):
                    raise ValueError
            elif a.startswith("--fps="):
                fps = float(a[6:])
                if not fps > 0 or math.isinf(fps):
                    raise ValueError
            elif a.startswith("-") and a != "-":
                raise ValueError
            elif path is None:
                path = a
            else:
                raise ValueError
            i += 1
    except (ValueError, IndexError):
        sys.stderr.write("usage: perf_report.py FILE.csv [--fps 60]\n")
        return 2
    if path is None:
        sys.stderr.write("usage: perf_report.py FILE.csv [--fps 60]\n")
        return 2
    try:
        return report(path, fps)
    except ReportError as e:
        sys.stderr.write("perf_report: %s\n" % e)
        return 2
    except Exception as e:  # never crash with a traceback on a bad file
        sys.stderr.write("perf_report: cannot analyse %s: %s\n" % (path, e))
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
