# Rear-view mirror

## Evidence

[data] `tools/fng_dump.py --list` finds 227 packages in this installation.
`--find-sample mirror` finds zero matching nodes. Enumerating every node (including
image aliases and groups) from every InGame bundle and Options package finds no
`mirror`, `rearview`, or `rear_view` name/alias. Examined packages include
HUD_SingleRace (both Race and FreeRoam copies), HUD_Short_Track, HUD_Drag,
HUD_Drift and both split-screen variants. No named mirror rectangle or frame
was identified. A negative name search does not rule out an unnamed image.

[data] LANGUAGES/English.bin contains these keys, read through the string-table
reader used by fng_dump (no executable inspection):

| Key | Text |
| --- | --- |
| 85a6ce05 | Rearview Mirror |
| d02c18bc | Rearview Mirror: |
| e4215d4e | Rearview mirror |
| e32ee46c | To turn the rearview mirror on or off while in-game, select Options and then select Gameplay from the Pause Menu. |

[inferred] Reuse the existing Gameplay option and profile field
`StGameplay.rearview_mirror`. The existing settings implementation defaults it
to 1. This is implementation evidence, not a newly established retail data
encoding: previous settings documents include static-analysis provenance and
are not used here as clean-room data evidence. Default on follows the supplied
retail still and the existing profile policy. An explicit `--rear-mirror on|off`
overrides the profile for reproducible comparisons.

[observed] The supplied retail still is 1170 by 643; its bevelled panel spans
approximately x=450..715, y=75..120. [inferred] Until an authored frame is
identified, use a centred 265 by 45 panel scaled by drawable height / 643,
with a procedural dark bevel and tapered sides. This is an approximation, not recovered retail
art. The scene is horizontally reflected during composition.

[inferred] Show during free roam and races; hide for front-end, pause/world map,
post-race, race presentation, orbit/free-camera and diagnostic passes. These
visibility rules are conservative runtime policy; the language table establishes
the toggle, not the complete visibility state machine.

[data] Searching printable names in the decoded InGame and Global bundles also
finds `ENGAGE_TIPS_MIRROR` in GlobalB, plus `_RIGHT_SIDE_MIRROR` and
`_LEFT_SIDE_MIRROR`. An attribute record named `MIRROR` at decoded GlobalB
0x41a0 (tag 0x00135200, payload 160 bytes) sits between METPAINT_9 and MOLDINGS,
with other paint/material records nearby. [inferred] This is material data,
not a camera/layout definition; its numeric fields are deliberately not treated
as camera parameters. No named mirror texture/frame was recovered by these
searches. Unnamed/hashed assets remain an open limitation.

## Implementation

[inferred] The target is RGBA8 plus depth/stencil, 256 by 40 pixels (10,240
shaded pixels before overdraw). The rear camera is at the car roof plus 0.3 m,
faces against car heading and pitches slightly down. Vertical FOV is 0.30 rad;
far plane and scenery AABB gate are 220 m. Opaque/cutout scenery is frustum
culled; translucent scenery, extra lighting/reflection/shadow/effect passes
are omitted. Vehicles within 120 m use body-mounted opaque geometry without
separate wheels/glass/material detail. This deliberately reduced detail is
visible at close range; it is not an authored retail LOD selection.

The pass runs before the main scene target, then composites after main upscale.
RHI command state and scene shader parameters are saved independently. Scale
1 retains the main target bypass. The existing profiler's scene GPU span
includes the rear pass; CPU time is charged to world_draw. Off/hidden frames
never allocate, select or draw the mirror target. Resources live until shutdown.
If target/shader creation fails, the mirror disables for the session.

The existing profile Gameplay option is connected in pause as well as front-end;
a direct-track launch without a loaded profile uses session-only settings.
The extra mouse-look row remains available in pause. CLI override takes priority
until restart. GPU tests check horizontal reversal, upright orientation, tapered corners, rectangle placement, state
restoration and composition with main scales 1, 0.5 and 0.75 on GL and GLES.

## Validation

Builds: desktop 24 warnings, GLES 29, ios-host 29; all exit 0, matching the
requested baseline counts. No new warning messages. Required tests all pass:
rear_mirror_test (48 assertions), hud-race-test, hud-minimap-test (89 checks),
gps-marker-test (149 checks, including retail geometry), fe-screen-test and
game-flow-test (252 checks). Additional options-menu-test passes (1,448 checks,
plus 1,866 supporting GUI-art checks and pause-options checks).

On the Pi host's V3D 7.1.10.2 renderer, GL and GLES framebuffer tests pass at
main scene scales 1, .5 and .75. Desktop exposes GPU timers; GLES does not.
The sandbox hides graphics device nodes and falls back to llvmpipe: the first software INV
comparison was different and is excluded from the host results.

With fresh XDG homes, both STREAML4RA and STREAML4RB INV captures are byte-identical
to the supplied invx references when --rear-mirror off is set. Default on changes
them; at 640x480 every changed pixel lies inside the mirror rectangle. The rest
of the scene is pixel-identical. Scale 1 still bypasses the main scene target.

`reports/mirror/retail-vs-mirror.png` and `panel-details.png` were visually
inspected. Both source frames are 1170x643. The approximate panel footprint and
taper match the supplied still; frame art is procedural, and vehicle, location
and lighting differ. This is a geometry/appearance comparison, not a claim of
identical retail scene rendering. `ios-on.png` verifies the GLES touch-host
build at explicit scene scale .75 (877x482 scene, 1170x643 drawable).

Performance protocol: GOLF, STREAML4RA, 640x480, default deterministic seed,
--follow-roam-ai --perf-still --shot OUTPUT --frames 1860 --perf-csv OUTPUT,
with separate fresh XDG directories and explicit --rear-mirror off/on. Discard
60 warm-up ticks and GPU lag samples originating before tick 60; run
`tools/perf_report.py` on each resulting CSV. Each reported sample contains
1,800 frames, exactly 30 seconds of simulated free roam on the same route;
wall-clock duration is longer when the Pi renders below 60 fps. Raw and filtered
CSVs remain in reports/mirror. This warm-up excludes initial asset loading,
which the profiler charges to its first CPU frame.

[inferred] A supplementary name-hash search used plausible Mirror/RearView node
names with HUD_/IG_/IMAGE_ prefixes and frame/background/panel/group suffixes.
[data] 288 candidate strings (228 distinct case-folded hashes) match zero nodes
in the InGame packages. This does not identify unnamed/otherwise-named assets.

Measured on V3D 7.1.10.2 (one paired run; milliseconds):

| Metric | Mirror off mean | Off p99 | Mirror on mean | On p99 |
| --- | ---: | ---: | ---: | ---: |
| Frame / cpu_total | 27.14 | 85.19 | 35.19 | 121.57 |
| GPU / gpu_total | 26.95 | 73.58 | 34.99 | 116.78 |

Mean increases: frame +8.05 ms (29.7%), GPU +8.04 ms. This is a material cost
on this Pi despite the small target and reduced scene. Each mode has 1,800 CPU
samples and 1,797 GPU samples after warm-up/lag filtering. The final paired
screenshots differ only inside the mirror, confirming matched route and main
scene state. See perf-warm-off/on-report.txt and performance-summary.json.

`ios-touch-on.png` is a scripted post-overlay capture, visually inspected with
both the mirror and touch controls visible at scale .75. Existing top touch
button rings meet the mirror's upper edge; their established positions are
retained. The mirror is passive and does not intercept touch input. No actual
phone/tablet performance measurement is claimed by the host test.
