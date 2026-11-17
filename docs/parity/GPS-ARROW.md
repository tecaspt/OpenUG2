# GPS guidance presentation

[observed] The owner supplied the 1280x720 straight-road Focus/default-chase
capture at 173 km/h: reports/retail-gps/retail-gps-straight-1280x720.jpg.
Its GPS arrow has a dark-blue core and light-blue rim, appears high above the
car roof, and projects as a downward chevron. No 2D arrow or distance is present.

## Controls and rendering

[data] Normal HUD rendering omits the old 2D navigation arrow and distance.
`--hud-nav-arrow-2d` explicitly enables that diagnostic in the current process
through OPENUG2_HUD_NAV_ARROW_2D=1; it does not persist a setting. The minimap
GPS selector and route code remain unchanged.

[data] `--gps-marker "ahead,height,pitch_deg,scale"` configures the 3D pose.
Ahead and height use metres. Positive pitch about the local right axis raises
the authored tip; negative pitch lowers it. Scale is uniform. Parsing is atomic,
with exactly four finite, comma-separated values; whitespace is accepted.
[inferred] Calibration safeguards permit ahead 0..1000, height -1000..1000,
pitch -180..180, and scale greater than 0 through 100. These are operational
bounds. No iOS environment override for pose is added.

[data] Genuine MARKER_GPS_AID geometry contains 32 core and 50 rim triangles
with 158 source vertices. Source vertex alpha is 255. Referenced mean RGB is
approximately (9,84,161) for the core and (58,163,196) for the rim. Both decoded
ranges reference one band texture, whose record retains additive usage/blend tags.
[observed] Adding both ranges washes the core bright cyan; applying the band
opaquely creates black stripes. The retail still has an opaque blue interior.
[inferred] Rendering therefore draws the additive band/rim first, then paints
the depth-writing opaque core from its authored prelight. A neutral shader
multiplier preserves those source colours. Darker/brighter mean prelight
identifies the two ranges; missing, unsupported or ambiguous ranges fail closed.
Geometry, core colour and rim texture come from the data. The material-pass
interpretation, glow and obstruction behaviour need further retail captures.

[inferred] Placement follows car heading so a turning route cannot move the
entire marker outside the view. Orientation independently follows route bearing.
[data] Current race, pause/results and front-end state gate the previous frame's
navigation flag. The marker is suppressed during active races, pause, results
and every front-end screen, including World Map. It draws with the scene camera
before render_target_present, so scene scaling applies. Zone arrival clears it.

## Straight-view calibration

[data] The authored Focus primary FAR entry has forward -5.8 m, up 1.7 m,
lateral 0, and pitch 0 BAU.
[observed] Engine FAR tuning uses FOV 13020 BAU and default Hor+ wide projection.
At 1280x720 its effective horizontal/vertical angles are approximately
95.07 / 67.85 degrees. The camera option remains default FAR; no chase override
is used. The engine capture is parked; the owner reference is at 173 km/h.
[inferred] Steady aligned speed does not alter this engine camera entry/FOV,
but motion, suspension and the retail camera matrix remain sources of uncertainty.

[inferred] One still cannot identify metric distance and scale separately.
Ahead is fixed at 30 m as a calibration gauge. The initial level-camera CPU fit
uses zero acceleration, aligned bearings, a 0.35 m body/frame offset and bob
phase zero. The actual chase view has a small body/suspension tilt; its visible
rim is also smaller than the full geometric bounds. Refinement uses the logged
view and visible bounds after moving past a foreground traffic light.

[observed] The view reconstruction reproduces logged geometric bounds within
0.004 px, using an effective eye height of 1.87005 m and the recorded look vector.
The runtime pose flag verifies the result before the constants are adopted.
Final compiled-default bounds, measured by a blue/rim threshold in a bounded ROI:

| Capture | Centre, pixels | Visible size, pixels |
|---|---|---|
|Owner still [observed]|637.5, 201.5|115 x 79|
|Engine default [observed]|640.0, 200.5|116 x 79|

[observed] Full frames are compared at native size in
reports/worldmap/s3/gps-straight-side-by-side.png; boxes and captions annotate
the comparison. Raw captures remain unchanged. JPEG edges, threshold choice
and bob phase introduce a few pixels of uncertainty.

| Parameter | Final engine default |
|---|---|
|Ahead|30 m [inferred metric gauge]|
|Height|12.919865 m [observed screen fit; inferred metric placement]|
|Pitch|+0.45 degrees [observed fit for this view; projects as a downward chevron]|
|Scale|3.905761 [observed screen fit; inferred physical scale]|

[data] Existing bob remains 0.15 m amplitude / 1600 ms period, both inferred.
[inferred] Turning placement fidelity, slope handling, material interpretation
and animation remain uncalibrated. The fit establishes straight-view framing.

## Checks and evidence

[observed] All commands use the memory gate, shared heavy lock, nice priority,
at most two build jobs, offscreen video, dummy audio, isolated storage and timeouts.
Reports and all retail-derived images/data stay outside the repository under
reports/worldmap/s3. Final results:

| Required check | Result |
|---|---|
|1. nfsu2 / gles / ios-host|PASS: 24 / 29 / 29 warnings; normalized lists identical to clean baselines|
|2. Nine requested test targets|PASS on final source; GPS 104 checks in desktop and strict GLES configurations, career host 107 checks|
|3. Historical gint reference PNGs|FAIL: L4RA 137,738 pixels differ; L4RB 68,200 differ|
|3. Current S2 controls|PASS: both PNG files byte-identical, zero differing pixels|
|4. Live race/shop guidance|PASS: 124 / 35-node routes, both markers visible, both arrivals clear guidance|
|4. Presentation and visibility|PASS: no normal 2D arrow/distance; pause/map hide, Back restores; scene scaling verified in GLES|

[observed] The nine targets are hud-race-test, hud-minimap-test,
hud-host-map-test, gps-marker-test, fe-worldmap-test, fe-screen-test,
game-flow-test, career-test and career-host-check. The default career-host-check
invocation fails to start LeakSanitizer under the execution environment's ptrace
restrictions. Only after that failure, ASAN_OPTIONS=detect_leaks=0 is used:
ASan/UBSan then pass 107 checks with zero failures. Leak detection remains NOT RUN.
An initial negative-W test inverted both model and view signs, cancelling its
fixture; the corrected synthetic test passes. Original failed logs are retained.

[observed] Historical reference failures are the same as S2: this environment
has no GPU device directory and uses software GL. Both current control images
are byte-identical. Reference PNGs were not changed. The historical gate is
still FAIL and requires the original host/GPU execution context.

[observed] Live tracing proves guidance survives pause/map with draw=0, restores
on Back, and clears at each canonical zone. Top-centre blue-arrow pixel counts
are 0 for both normal live legs and 1341 in the explicit debug capture.
Race event4013 renders without a GPS marker. Its manual GPS request solves zero
nodes; positive-guidance/race exclusion is covered by the complete synthetic
visibility cases and the host's current race condition, rather than that image.
GLES scale0.5 logs a 640x360 scene at a 1280x720 drawable; the marker scales with
3D while HUD text remains sharp. Arrival uses the existing diagnostic XY teleport;
physical driving fidelity is not established.

[observed] Final proof captures were reviewed by eye:

- gps-straight-side-by-side.png, calib-final-default.png, calib-gles-scale05.png.
- inv-STREAML4RA.png, inv-STREAML4RB.png, race-gps-hidden.png.
- live-roam.png, live-pause.png, live-confirm.png, live-map-race.png, live-race-selected.png.
- live-race-route.png, live-guidance-paused.png, live-guidance-worldmap.png, live-guidance-resumed.png, live-race-arrived.png.
- live-map-shop.png, live-shop-selected.png, live-shop-route.png, live-shop-arrived.png.

[observed] Tuning/debug/override captures were also reviewed: calib-first.png,
calib-straight.png, calib-debug-2d.png, calib-override.png, calib-scale05.png,
calib-refined-first.png, calib-refined.png and calib-fit-proof.png. They precede
adoption of the final defaults. Superseded experiments are kept in labelled
initial/intermediate directories and do not support final PASS claims.

[observed] Evidence summaries: builds.status, warnings.status, final-tests.json,
pose-checks.status, final-captures.status, comparison-metrics.json,
live-assert.json and invariance.status. Private run scripts retain exact commands.
No retail executable, decompiled material, real user save or system setting is used.

## Reproduction and remaining captures

[data] The existing game target omits gps_marker.h from HDRS. A header-only
change can leave a previous backend binary marked up-to-date. Validation touches
main.c before make -j2 nfsu2 and archives that GL binary before make gles overwrites
it. The build owner should add `HDRS += src/gps_marker.h`; this task does not edit
the Makefile. No game SRC addition or new shader is needed.

[data] The CPU probe builds from the worktree under the shared heavy wrapper:

```sh
cc -O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Isrc tools/gps_marker_pose_check.c src/camera.c -o build/gps_marker_pose_check -lz -lm
./build/gps_marker_pose_check "$OPENUG2_DATA"
```

[data] An optional second argument supplies a pose. Without it, the probe fits
the initial level-camera model; the final defaults include subsequent viewport
refinement. Omitting the data-root argument exits77. No substitute geometry is used.

[observed] The source video is https://m.youtube.com/watch?v=W5LGI6tr2GY.
The browser could not open it and the downloader failed DNS resolution.
Straight calibration uses the owner-supplied still. Turn/bob calibration and
device checks are NOT RUN.
[inferred] Further references needed: unscaled 1280x720 recordings at known
30/60 fps, default FAR chase and identified car, with HUD visible; 6–10 seconds
on a straight road at steady speed and 6–10 seconds through left/right turns.
Keep the arrow, car roof and horizon visible without camera switching. Several
full bob cycles and GPS/pause/map/race transitions would constrain animation,
obstruction and visibility rules.
