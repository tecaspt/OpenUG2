#!/usr/bin/env python3
"""Prove each log spec can fail: every spec must pass on its real gate log and fail on a copy with
one deliberate change aimed at the rule that carries that step's meaning.

This is the check task T established for log_assert, applied to the specs the gate depends on. A
corruption that does not change the log, or that leaves the spec passing, is reported - a rule
nothing can break is not a rule. Takes the log directory from argv[1] (default the m1 gate output).
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# The gate output directory to read real logs from: pass it as argv[1]. There is no default, because
# where a gate run lands is a property of the machine, not the repo - point it at whatever the last
# `gate_run.py ... --out DIR` produced.
GATE = sys.argv[1] if len(sys.argv) > 1 else None

# spec -> (log file, corruption, the rule the corruption is meant to break). A spec may appear under
# more than one key, which is how one step's meaning gets spread over the rules that carry it - each
# key breaks exactly one of them, so a BAD line names the rule that is not load-bearing.
CASES = {
    "s2_menu.spec": ("s2.log",
        lambda t: t.replace("fe: launch free run event 0", "fe: launch quick race event 0"),
        "the Free Run leg must launch a free run, not a second quick race"),
    "s6c_career.spec": ("s6c.log",
        lambda t: t.replace("career: new stage 0", "career: new stage 3", 1),
        "a first career must start at stage 0"),
    # The real lines read: drag shift: grade 1 gear 2 caption "Short Shift!"  (four of them).
    # Dropping the caption from one of them is the failure the HUD would show - a shift with no
    # word on it - which is what s11_drag's forbid rule is for.
    "s11_drag.spec": ("s11.log",
        lambda t: t.replace('caption "Short Shift!"', 'caption ""', 1),
        "every shift must arrive with a caption, not an empty one"),
    "s12_drift.spec": ("s12.log",
        lambda t: t.replace("race armed: event 4174 (Drift)", "race armed: event 4174 (Drag)", 1),
        "4174 must be a Drift race"),
    # s13 is the ask-then-race flow: asking, answered Yes, engage. A No answer is the bug the spec
    # forbids, so turn the answer into that.
    "s13_ask.spec": ("s13.log",
        lambda t: t.replace("race question answered Yes", "race question answered No", 1),
        "the prompt was answered Yes, and the flow orders ask -> Yes -> engage"),
    "s7_carlot.spec": ("s7.log",
        lambda t: t.replace("career: bought 240SX", "career: want 240SX", 1),
        "the purchase, not an intention"),
    "s7b_continue.spec": ("s7b.log",
        lambda t: t.replace("career: continue stage", "career: new stage", 1),
        "continuing, not starting over"),
    "s7m_lot.spec": ("s7m.log",
        lambda t: t.replace("career: bought ", "career: wanted ", 1),
        "a purchase in the manual-buy run"),
    "p3_desktop.spec": ("p3-desktop.log",
        lambda t: t.replace("garage rims: NFSU STYLE02 action=1", "garage rims: NFSU STYLE01 action=1", 1),
        "the rim change the desktop run is supposed to make"),
    "p3_cold.spec": ("p3-cold.log",
        lambda t: t.replace("garage saved: GOLF body=1 rim=NFSU STYLE02",
                            "garage saved: GOLF body=0 rim=NFSU STYLE01", 1),
        "the kit surviving the restart"),
    # s7s_shop_ask is the shop-ask flow: zone, ask, answered Yes, and a lot whose cars are all
    # unowned. Turning the answer into No is the bug its forbid rule is written for.
    "s7s_shop_ask.spec": ("s7s_shop_ask.log",
        lambda t: t.replace("career: shop question answered Yes", "career: shop question answered No", 1),
        "the shop prompt was answered Yes, and the flow orders ask -> Yes -> owned-car lot"),
    # mirror_on proves the rear mirror drew: its only positive line is the --shot write. Removing it
    # leaves a run that never took the screenshot the step is named after.
    "mirror_on.spec": ("mirror_on.log",
        lambda t: re.sub(r"^wrote mirror-on-.*\n", "", t, count=1, flags=re.M),
        "the mirror screenshot must exist"),
    # worldmap_gps is the route-and-arrive flow, and the id ties are what make the whole chain mean
    # one destination: perturb the id of the icon the run asked for and none of the route, guidance
    # or arrival lines refer to an icon the map ever offered.
    "worldmap_gps.spec": ("worldmap_gps.log",
        lambda t: re.sub(r"world map: selected kind=1 id=([0-9a-f]{6})[0-9a-f]{2}",
                         r"world map: selected kind=1 id=\1ff", t, count=1),
        "the destination must be one the map really offered"),
    # The same flow a second way, because two of its rules carry all the meaning and neither is
    # touched by the id mutation above: a route that is not a route, and an "arrival" that never
    # cleared the guidance. Each is its own case so each failure names its own rule.
    "worldmap_gps.spec (zero-node route)": ("worldmap_gps.log",
        lambda t: re.sub(r"GPS route: [1-9]\d* nodes", "GPS route: 0 nodes", t, count=1),
        "a destination with no route nodes was never routed to"),
    "worldmap_gps.spec (guidance never cleared)": ("worldmap_gps.log",
        lambda t: re.sub(r"world map: destination zone reached (kind=\d+ id=[0-9a-f]{8}) "
                         r"route=0 marker=0",
                         r"world map: destination zone reached \1 route=9 marker=1", t, count=1),
        "reaching the zone has to drop the route and the HUD marker, not keep them"),
}


def main():
    if GATE is None:
        print("usage: spec_negative_check.py GATE_OUT_DIR   (the --out directory of a gate run)")
        return 2
    if not os.path.isdir(GATE):
        print("no log directory %s" % GATE)
        return 2
    bad = 0
    for case, (log, mutate, why) in sorted(CASES.items()):
        spec = case.split(" (")[0]       # "x.spec (extra mutation)" keys share one spec file
        real = os.path.join(GATE, log)
        if not os.path.isfile(real):
            print("SKIP %-40s no %s in %s" % (case, log, GATE))
            continue
        text = open(real, encoding="utf-8", errors="replace").read()
        broken = mutate(text)
        if broken == text:
            print("BAD  %-40s the corruption changed nothing in %s" % (case, log))
            bad += 1
            continue
        tmp = os.path.join("/tmp", "broken-" + log)
        with open(tmp, "w", encoding="utf-8") as fh:
            fh.write(broken)
        ok = run(spec, real)
        ko = run(spec, tmp)
        if ok == 0 and ko == 1:
            print("OK   %-40s real pass, broken fail under: %s" % (case, why))
        else:
            print("BAD  %-40s real=%d broken=%d (want 0 and 1) - %s" % (case, ok, ko, why))
            bad += 1
        os.remove(tmp)
    return 1 if bad else 0


def run(spec, log):
    return subprocess.run([sys.executable, "-I", os.path.join(HERE, "log_assert.py"),
                           os.path.join(HERE, "log_specs", spec), log],
                          capture_output=True, text=True).returncode


if __name__ == "__main__":
    sys.exit(main())
