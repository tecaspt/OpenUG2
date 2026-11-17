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
with 158 source vertices. Source alpha is 255. Referenced mean RGB is approximately
(9,84,161) for the core and (58,163,196) for the rim. The object's texture-slot
list and two material-range records bind the core to slot 0, an opaque 128x256
arrow atlas (key 985c190c), and the rim to slot 1, a 64x64 horizontal light band
(key 1e232d55, usage 2 / blend 2).
[observed] S3's generic world-loader call supplied no resolvable pack keys;
its last-slot fallback therefore assigned the band to both ranges. S3's statement
that both ranges were authored with one band was incorrect. The neutral-core
workaround suppressed the genuine atlas detail and the cyan inner band.

[data] The marker loader now recovers each exact recorded range/slot binding.
It validates one matching object, contiguous ranges, matching index counts and
nonzero in-range slots, then requires both textures to decode. Missing or
ambiguous material metadata fails closed. Source geometry, UVs, prelight and
texture bytes are never rewritten. A private shader and full RHI state snapshot
isolate the marker material from world and HUD draws.
[inferred] The opaque atlas core draws before the additive band shell. The
MODULATE2X interpretation and calibrated channel gains reconstruct the sampled
contrast; authored prelight retains the lighter faces. No procedural replacement
band, painted texture or invented geometry is used.

[data] Placement follows car heading, while yaw follows route bearing.
Current race, pause/results and front-end state gate the previous frame's
navigation flag. The marker is suppressed during active races, pause, results
and every front-end screen, including World Map. It draws with the scene camera
before render_target_present, so scene scaling applies. Zone arrival clears it.

## Accepted straight-view calibration (S3)

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
[inferred] At S3 close, turning placement, slope handling, material interpretation
and animation remained uncalibrated. S4 below adds the supplied turn still.

## S3 checks and evidence

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

## Earlier S3 reproduction and missing captures

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
Straight calibration used the owner-supplied still. At S3 close, turn/bob
calibration and device checks were NOT RUN. S4 below adds still-based turn work;
moving animation and device checks remain NOT RUN.
[inferred] Further references needed: unscaled 1280x720 recordings at known
30/60 fps, default FAR chase and identified car, with HUD visible; 6–10 seconds
on a straight road at steady speed and 6–10 seconds through left/right turns.
Keep the arrow, car roof and horizon visible without camera switching. Several
full bob cycles and GPS/pause/map/race transitions would constrain animation,
obstruction and visibility rules.

## S4 turn pose and colour correction

[observed] The second owner reference is
reports/retail-gps/retail-gps-turn-right-1170x643.png, a 1170x643 right-turn
view with a 350Z and default chase. Its marker points sideways to the right;
the source atlas has grain, blue faces, cyan highlights and a cyan-to-white rim.
The top-centre rear-view mirror is a separate feature and is out of scope here.

[inferred] Let delta be route bearing minus car heading, s = sin(delta) and
w = s*s. Yaw uses the route bearing. Base pitch is multiplied by 1-w, becoming
flat side-on; scale is multiplied by 1+0.10*w. The centre stays 30 m ahead in the
car frame, moves 4.1*s metres toward the route side and rises 1.1*w metres.
These are screen-fit parameters, not uniquely recoverable retail world units.
The smooth symmetric law between the two observed poses remains inferred.
Straight guidance has w=0 and retains the accepted S3 transform bit-for-bit.
Bob remains unchanged and uncalibrated; moving references are still needed.

[observed] Material gains, applied in the shader after source texture/prelight
multiplication, are core RGB (1.8,0.67,1.18), rim RGB (1.8,1.45,1.0).
They were fitted from the sampled regions, not extracted from executable code.
The opaque atlas and authored light band provide the spatial detail and face
contrast. This supersedes the S3 neutral multiplier and rim-before-core order.

[observed] The final proof uses native-size full frames with region annotations:

- reports/worldmap/s4/gps-straight-side-by-side.png (2560x950).
- reports/worldmap/s4/gps-turn-side-by-side.png (2340x873).

[observed] Both figures were reviewed by eye. Engine views use default FAR chase
and Hor+ projection, without camera overrides, at the reference resolutions.
The Focus is stationary versus the 173 km/h straight reference; both turn views
show a stationary 350Z. Logged engine turn heading is 118 degrees and guidance
bearing about 44.075 degrees: a right-relative bearing of -73.925 degrees.
The fitted pitch is then about +0.0345 degrees, versus +0.45 straight. A thin
foreground cable crosses the upper turn outline; the body and measured regions
remain legible. Obstruction behaviour is unchanged.

[observed] Visible bounds use the same blue/rim threshold as S3 in bounded ROIs:

| View | Retail size / centre, px | Engine size / centre, px |
|---|---|---|
|Straight|115x79 / (637.5,201.5)|115x78 / (639.5,200.0)|
|Right turn|185x61 / (639.5,161.5)|180x57 / (633.0,162.5)|

[observed] Straight framing remains within 2 px horizontally and 1.5 px
vertically; its visible height differs by 1 px. The turn is 5 px narrower,
4 px shorter, 6.5 px left and 1 px lower. This is an approximate still fit,
not a recovered retail matrix. The broader top face and sideways tip are visible.
A later camera-position probe put the car on a different road/suspension tilt,
changing chase inclination and lowering the marker; it is excluded from the fit.

[observed] RGB values below are regional medians of decoded screenshot bytes,
without gamma conversion. Core rectangles and masked bright-rim rectangles are
shown in the figures. The lit-face/band sample selects the three rows around the
highest mean green row inside a bounded feature ROI. Rim samples require
R>100, G>170, B>200. The source atlas grain and the additive background mean
single-pixel values vary. Exact ROIs/counts are retained in comparison-metrics.json.
The before-turn frame is the S3 material rendered with the 350Z; separate visible
regions avoid its foreground tree. No retail texture bytes are altered.

| View / region | S3 before RGB | S4 after RGB | Retail RGB |
|---|---|---|---|
|Straight core|9,73,161|15,45,174|20,44,171|
|Straight lit face/band|9,95,161|166,255,255|116,240,255|
|Straight rim|157,255,255|174,255,255|202,245,252|
|Turn core|9,68,161|15,42,174|23,37,172|
|Turn lit face/band|9,95,161|150,255,252|145,240,254|
|Turn rim|143,255,255|186,255,255|205,243,250|

[observed] Core medians now differ by at most 8 per channel. The straight band
peak remains 50 red and 15 green levels brighter; turn band differences are
+5,+15,-2. Rim medians remain 19..28 red levels lower and 10..12 green levels
higher. Full colour/bloom fidelity is not established. The additive rim includes
scene background, which differs strongly between the retail sky and engine view.
[inferred] Channel gains, material response, bloom/tonemapping, metric offsets,
intermediate turning poses and animation remain assumptions. The two stills
constrain appearance at their views; they do not establish a unique lighting law.

## S4 final checks

| Check | Result |
|---|---|
|1. nfsu2 / gles / ios-host|PASS: 24 / 29 / 29 warnings; normalized lists unchanged from S3|
|2. Requested five test targets|PASS: gps-marker-test, hud-race-test, hud-minimap-test, fe-worldmap-test, game-flow-test|
|2. GPS desktop / strict GLES|PASS: 149 checks, zero failures in each; strict GLES build uses -Werror|
|3. Historical gint reference files|FAIL: L4RA 137,738 pixels differ; L4RB 68,200 differ|
|3. Current S3 controls|PASS: both PNG files byte-identical, zero changed pixels|
|4. Annotated straight / turn figures|PASS: rendered, sampled and reviewed by eye|
|4. GL and GLES screen captures|PASS: straight and turn guidance visible, no normal 2D arrow/distance|
|Moving bob / device checks|NOT RUN|

[observed] Historical-reference failure is unchanged from S2/S3 in software GL;
reference files were never replaced. The original host/GPU context is still
needed for that gate. This does not count as a historical invariance pass.

[observed] The three final build commands each returned zero before the private
runner encountered an epilogue parse error caused by editing its file while it
ran. Their logs, archived binaries and warning comparison independently verify
the builds; the runner's failure is retained. Earlier terminated/cancelled builds,
procedural-palette experiments, stale binaries and obstructed captures are excluded.
Final code checksums match the tested source. No source changed during final capture
review. All compiles/runs used the shared lock, nice priority, memory gate and
bounded isolated headless runs. Memory waits were honoured without lowering the
2500 MB threshold. No leak-sanitizer exception was needed for this five-target set.

[observed] Final reviewed evidence under reports/worldmap/s4:

- straight.png, turn-clear-gl.png, gles-straight.png, turn-clear-gles.png.
- inv-STREAML4RA.png, inv-STREAML4RB.png.
- gps-straight-side-by-side.png, gps-turn-side-by-side.png.
- build-gl.log, build-gles.log, build-ios.log, build-warning-check.json.
- gps-marker-test.log, gps-marker-gles-test.log and the other requested test logs.
- validation.status, turn-clear.status, comparison-metrics.json, invariance.json, source.sha256.

[data] No host wiring, new SRC entry or shader file is required: the marker's
private material compiles through the existing header. The earlier recommendation
to add gps_marker.h to Makefile HDRS still applies to the build owner. This task
changes only the GPS header, its test and this document. The rear-view mirror
remains out of scope. Moving straight/left/right clips are still needed to constrain
bob, temporal turning and obstruction; no retail executable or decompiled code
was consulted.
