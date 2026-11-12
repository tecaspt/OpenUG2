#!/usr/bin/env python3
"""Generate the M5 replay script deterministically.

Writes `frame,steer,throttle,brake` lines: full throttle for the first 70% of
the run (a launch), then coast, then a firm brake to the last frame - the shape
`drive_stage1_test` uses for its launch/coast/brake reference. One file feeds
both the host arm and the PSP arm (dropped on the stick), so the two compare the
same inputs.

usage: gen_replay.py [frames] [throttle_frames] [brake_frames] > replay.txt
"""
import sys

frames = int(sys.argv[1]) if len(sys.argv) > 1 else 600
throttle = int(sys.argv[2]) if len(sys.argv) > 2 else int(frames * 0.7)
brake = int(sys.argv[3]) if len(sys.argv) > 3 else 120

lines = []
for f in range(frames):
    if f < throttle:
        thr, brk = 1.0, 0.0
    elif f >= frames - brake:
        thr, brk = 0.0, 1.0
    else:
        thr, brk = 0.0, 0.0
    lines.append("%d,0.0,%.1f,%.1f" % (f, thr, brk))
sys.stdout.write("\n".join(lines) + "\n")
