# M7 race rules — Task F

[observed] Baseline is ea2c9c4. Task F owns physics/rule adapters, world/AI race parts and post-race
presentation. Host, front-end, HUD, game flow and build integration remain with the orchestrator.
No executable reverse engineering or external implementation code is used.

## Baseline and current status

[observed] Before source edits: physics-clock-test, world-collision-test, ai-drive-test and
world-resident-test passed. The complete installed route drive check is byte-identical to the
existing drive-all reference: 105 events decoded, 104 completed, one without a mode.
[observed] The initial required Xvfb race-ai wrapper returned 1 despite the executable's final PASS
line. An explicit diagnostic child exited 0. The server log reports shared display socket binding
failure and cleanup trying to kill a nonexistent server. The wrapper result is FAIL, not PASS;
a distinct-display retry also failed in the wrapper while printing the child's final PASS. The same
wrapper limitation persists after priority 1. No production services were changed.
[observed] Priority 1 diagnosis and opt-in adapter are complete. Synthetic tests pass desktop/GLES
with -Werror. Clock, collision, AI-drive, resident and direct race-AI checks pass after the change;
the complete route drive audit remains byte-identical. Main integration is not performed.

## Priority 1: drag longitudinal path

[observed] main.c:8023 excludes Drag/Drift from Stage 1–5; ordinary free roam uses that solver.
main.c:8332 scales the legacy car's arcade acceleration by phys_manual_step's multiplier.
[observed] The multiplier normalizes sampled torque by peak torque and multiplies by the current
gear ratio divided by first gear. chassis_step then applies a separate speed taper and fixed
per-tick arcade drag. This is an adapter composition, not a dimensional wheel-force calculation.
[observed] The supplied s4c script/log actually selects event 4202 and COROLLA. The 4201 measurement
removes its extra track-advance input, keeping the car and gas/shift timing. Script input is frame
indexed, so slow rendering does not itself reduce the pedal command or alter fixed tick duration.

[data] The existing car-record readers expose mass, idle/redline/limiter RPM, torque samples and
gear ratios/final drive. The same row contains front/rear wheel radii at +0x130/+0x190 and the
Stage-2 air coefficient at +0x370. The new reader validates and copies these values at runtime.
[inferred] phys_drag_step is an opt-in longitudinal adapter: wheel force is sampled torque times
gear/final drive divided by effective driven radius; acceleration divides net force by mass.
Air loss is quadratic, using the decoded coefficient. Torque interpreted as kN m and mass as tonnes
follow the existing dimensional solver contract; these interpretations do not recover retail code.
[inferred] Rolling loss .015 of weight, dry launch grip 1.0 and gravity 9.81 are named provisional
constants in physics.h. The effective radius interpolates front/rear by the decoded rear-drive share.
No extra normalized torque/gear multiplier, arcade speed taper or fixed arcade rolling drag is
applied in this path. Existing lateral/steering, airborne and handbrake adapters are retained.
[inferred] Invalid setup refuses to advance. The host must reject a drag launch with a status message
when the configuration cannot be decoded; exact hooks are in M7-RACE-RULES-INTEGRATION.md.
[observed] Existing physics entry points remain unchanged. Main does not yet call the new entry
point on this branch; full game-after-fix behavior is NOT RUN and belongs to host integration.

## Priority 1 measurements

[observed] The live 4201/COROLLA sample recorded throttle 1.000 and no wall/vehicle contacts.
Maximum sampled speeds per gear before stopping the diagnostic were 48.41, 55.51, 58.11, 58.38,
58.11 km/h. Fifth was only about 1,523 RPM. The run was intentionally interrupted after input/gear
sampling (exit 130); it is not a completed-race PASS. Initial PulseAudio startup stalled; the usable
sampling run used dummy audio. Audio output is not part of this physics measurement.
[observed] Full-throttle level-road probes run 300 seconds at 60 Hz. Ordinary/free-roam uses the
complete Stage1–5/brakes/lateral/body setup and a level ground query. Early shifting means 140 ticks
in first, then one upshift every 60 ticks; redline shifting waits for the decoded redline.

| COROLLA gear | Legacy early | New early | Legacy redline | New redline | Ordinary automatic |
|---|---:|---:|---:|---:|---:|
| 1 | 49.30 | 30.79 | 56.32 | 70.85 | 69.80 |
| 2 | 56.67 | 36.99 | 99.45 | 125.44 | 123.68 |
| 3 | 59.46 | 40.77 | 115.23 | 183.38 | 180.88 |
| 4 | 59.78 | 43.16 | — | 205.24 | 206.60 |
| 5 | 67.64 | 189.70 | — | — | — |

[observed] Values are maximum reached km/h per gear; a dash means that strategy never selected it.
The legacy standalone fixture supplies the decoded front radius; baseline main supplies its render
wheel radius instead. The new fixture uses phys_drag_config_read and the driven radius. These setup
differences are explicit; standalone baseline values are not presented as exact live-race telemetry.
[data] COROLLA is rear-driven and its row contains front radius .265 and rear radius .334 metres;
front/rear final drive are both 3.484. The new reader uses the rear radius. Using a single visual or
front radius for every axle does not reproduce this record's wheel-speed relationship.
[observed] GOLF ordinary reaches 215.35 km/h, new redline drag 241.40; MIATA ordinary 204.90, new drag
221.29. These are independently configured solver outputs, not claimed retail maximum speeds.
[inferred] The real slowdown is the legacy longitudinal composition, compounded by timed early
upshifts. It persists even with redline shifts. Full raw throttle is confirmed; ordinary COROLLA
reaches a plausible highway speed with the same pedal, while legacy drag cannot progress beyond
third at redline. The new adapter removes that artificial high-gear ceiling. It is a reduced drag
adapter, not the full ordinary clutch/contact solver; differences between their top speeds remain.
[observed] Synthetic vectors pass in desktop/GLES with no warnings in changed files, including
high-gear acceleration, coast/braking, airborne retention, parser transaction and invalid setup.
Final evidence is in the private report logs; earlier zero-contact/reduced ordinary fixtures and
an over-specific invented-speed assertion were corrected and are not counted as passing evidence.

[observed] Priority-1 verification artifacts relative to the private M7 report root:

- baseline/build-tests.log and baseline/drive.txt: original checks and identical reference trace.
- drag-test-final.log: synthetic PASS in desktop and GLES, -Werror.
- speeds-verified.log: final level-road runtime probe, finite-motion assertions, production config.
- p1/build-tests.log: post-change CPU/invariance tests PASS, no physics.c/h warnings.
- p1/drive.txt and p1-final-status.log: P1_DRIVE_IDENTICAL; 105 decoded/104 completed/one no-mode.
- baseline/xvfb/race-ai-test.log, baseline/race-ai-child.log, baseline/xvfb-errors.txt,
  p1/xvfb-summary.txt: wrapper FAIL and separately observed child exit 0. The orchestrator must
  rerun the required wrapper in a host environment where its X server can start/clean up correctly.

[inferred] No default physics/AI drive differences are expected because the new adapter is opt-in.
[observed] There are no reference-trace differences. A full game-after-fix run is NOT RUN: main.c
still selects the legacy adapter until the orchestrator applies the documented hook.

## Priority 2: drag grading and engine failure

[data] Fresh Labels/English parsing finds HUD_SHIFT_PERFECT (0x0598b065), HUD_SHIFT_GOOD
(0x27d2dd45), HUD_SHIFT_SHORT (0x2303d76c), HUD_SHIFT_OVER (0x27d75cd8). The drag tip describes
per-car/per-gear optimal shift points and an additional speed boost for perfect shifts.
[inferred] Accepted upward shifts expose Perfect/Good/Early/Late feedback; Early maps to the retail
Short Shift caption and Late to Over Rev. The center is caller-supplied shift_point_rpm, falling
back to the decoded redline. Named windows are ±150 RPM for Perfect, −600/+350 for Good, with
Early/Late outside those windows. These numeric grading windows are not decoded retail values.
[inferred] PhysManual appends shift_quality and feedback_seconds, lasting one simulated second.
Rejected/out-of-range shifts do not generate feedback; downshifts clear grading. Feedback has no
thrust bonus or shift-duration effect. The tip's bonus magnitude/application is still undecoded.
[inferred] Positive throttle at/above the decoded limiter heats by dt/1.5; other conditions cool at
.5 per second, clamped to 0–1. Heat reaching 1 latches failed until the host resets PhysManual.
Existing .15-second shift cuts and crash thresholds (wall impulse >8 or any positive vehicle
contact) remain unchanged, with heat/crash policy constants centralized in race_rules.h.
[observed] New synthetic tests pass in desktop/GLES with -Werror: grading boundaries, genuine label
keys, invalid/downshift cases, supplied-center feedback/expiry, limiter/cooling and latched failure.
Post-priority clock/collision/AI-drive/resident/direct race-AI tests pass and the complete drive
trace is byte-identical (p2/drive.txt; P2_DRIVE_IDENTICAL in p2-status.log). No new warnings in
changed sources. The required Xvfb wrapper remains the same documented environment FAIL.
Main/HUD feedback wiring is not performed here. Priorities 3–6 remain to be completed.

## Priority 3: drift chains and points results

[data] English loading/tip text says drift points depend on speed/angle, marked edge bonus zones
exist, and walls lose drift points. It does not establish numeric scoring/multiplier thresholds.
[inferred] The canonical pure kernel takes SI velocity, measured travel distance and seconds. The
legacy race.h API converts m/tick at the existing 60 Hz boundary. A qualifying grounded forward
slide above 35 km/h, between 10 and 80 degrees, adds metres × sin(angle) × 100 raw points.
[inferred] Every 1,000 raw chain points increases the multiplier, capped at ×5. Piecewise integration
splits threshold crossings, so changing tick partition does not multiply the entire earlier chain
retroactively. One second without a qualifying slide banks the pending chain; any collision first
cancels all pending points and resets its multiplier, preserving previously banked points. Finish
banks once. Inputs/score caps and coefficients are named in race_rules.h; the numeric formula is
inferred, not a decoded retail equation. Edge bonus-zone identification remains undecoded.
[inferred] Competition rank uses banked points, with equal points sharing a rank; road position has
no input to rr_points_place. Host snapshotting finalizes rival chains at player finish. A malformed
points array returns rank 0, which the host treats as an invalid-rule failure, never a win.
[observed] Existing main/AI score calls already supply final poses after contacts and measured travel.
world_race_progress_update already banks drift at finish. Their APIs remain intact through wrappers.
[inferred] PostRaceInfo appends optional drift_points/have_drift_points. When provided for Drift, the
authored results time column displays the supplied integer score under PRR_POINTS_TITLE (0x70802830).
Legacy callers with the flag unset retain their existing result presentation. No opponent score is
invented; this unit renders only supplied player data.
[observed] Desktop/GLES synthetic checks pass with -Werror: cadence partition, multiplier cap,
bank-on-end, collision reset preserving bank, airborne/invalid inputs, saturation and points ranks.
The installed results screen was inspected by eye in both backends: Points/12345 is visible under
the genuine header; unknown opponent rows remain hidden and touch Back remains visible. Final
PNGs are postrace-desktop/drift-points.png and postrace-gles/drift-points.png under the private report
root. These use a synthetic result payload, not an observed retail race score.
[observed] Post-priority clock/collision/AI-drive/resident/direct race-AI checks and postrace-test
(9 checks) pass; check-gl is OK. The full drive trace is identical (p3/drive.txt and p3-status.log).
No warnings originate in changed/new code; the same baseline Xvfb wrapper FAIL remains.
Main/HUD snapshot/multiplier wiring remains with the orchestrator. Priorities 4–6 remain.

## Priority 4: Lap Knockout and URL series

[data] The language table includes knocked-out messages and URL/points result captions. The event
catalog identifies URL courses; this pass has not decoded tournament grouping or numerical points.
[inferred] Knockout uses persistent entrant IDs (player 0), eliminates the lowest-progress surviving
car on each newly completed leader lap, and requires count−1 laps. Repeated notifications of the
same lap do nothing. Exact-progress ties eliminate the greatest ID; one survivor wins. Actor helpers
freeze/hide eliminated racers; host collision/render/minimap lists must use the supplied predicate.
[inferred] URL accepts an explicit event sequence and points table. The named QR default is three
distinct catalog URL events, wrapping from the selected event, with 8/6/4/3/2/1 points per place.
These values/grouping are provisional, not retail recovery. Entrant IDs stay stable across rounds.
[inferred] A round collector records actual first finish observations. After the player finishes,
the field continues for up to the named 30-second grace; unfinished actors become DNF (zero points).
Finish-time ties use entrant ID. This avoids inventing a finish order from current road position.
Invalid samples return −1; pending returns 0; ready returns 1. Simulation time stops during pause.
[inferred] Round awards validate the event/places before mutation. Restart undoes the last round's
points/wins/finish ledger; duplicate/wrong-event submissions are rejected. Overall rank is by total
points; equal totals share rank. Unique-winner lookup returns no winner for a tie or zero totals.
[inferred] PostRaceInfo accepts up to six supplied standings and round metadata. Retail results show
stable-sorted totals under URL Race/Points. Only supplied names/cars are shown; unknown cars are
hidden. This does not establish retail opponent identities or tournament grouping.
[observed] Final synthetic desktop/GLES series/actor/round-collector/finish-ledger tests pass with
no new warnings. Desktop/GLES URL PNGs were inspected: postrace-desktop/url-standings.png and
postrace-gles/url-standings.png under the private report root, with Leader16/Player14/Rival10 at2/3.
Post-unit clock/collision/AI-drive/resident/direct race-AI checks pass, postrace9/0 and check-gl OK.
Full drive byte comparison is identical (p4/drive.txt, p4-status.log); initial build attempt exited143
and was rerun completely. Required Xvfb wrapper remains baselineFAIL1 despite direct child PASS.
Actual main.c coordination/end-to-end URL/KO play is NOT RUN; owner integration remains required.
