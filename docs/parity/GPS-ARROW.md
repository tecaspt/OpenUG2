# GPS guidance presentation

[observed] The owner supplied a 1280x720 straight-road Focus/default-chase capture
at 173 km/h: reports/retail-gps/retail-gps-straight-1280x720.jpg. The arrow is near
(635,200), approximately 115x80 pixels, with a dark-blue core/light-blue rim and
a downward-chevron projection. No top-centre 2D arrow or distance is present.

[data] Normal HUD rendering omits the old 2D navigation arrow and its distance.
`--hud-nav-arrow-2d` explicitly enables that diagnostic in the current process via
OPENUG2_HUD_NAV_ARROW_2D=1. The minimap GPS selector and route code are unchanged.
The flag does not persist a setting.

[data] `--gps-marker "ahead,height,pitch_deg,scale"` changes the 3D pose. The first
two values are metres; positive local-right-axis pitch raises the authored tip,
negative pitch lowers it. Scale is uniform. Parsing is atomic and rejects missing,
extra, non-finite or overflowing fields. [inferred] Calibration safeguards allow
ahead 0..1000, height -1000..1000, pitch -180..180 and scale greater than 0 through
100. They are operational bounds. No iOS environment override is added.

[data] The genuine MARKER_GPS_AID geometry contains 32 core triangles and 50 rim
triangles, with all 158 source vertices carrying alpha255. Referenced mean RGB is
approximately (9,84,161) for the core and (58,163,196) for the rim. Both ranges use
the same band texture; its source record retains additive usage/blend tags.
[observed] The initial additive draw accumulates front/back faces and washes the
core bright cyan; the supplied retail still has a solid dark-blue core.
[inferred] The solid uses the existing depth-writing, unculled opaque pipeline
with the existing textured MODULATE2X/prelight shader law. The texture tag alone
does not settle the solid's material-level blending. This interpretation is being
checked against final rendered captures; no substitute geometry, tint or texture is added.

## Straight-view fit

[data] The engine's authored Focus primary FAR camera entry has forward -5.8m,
up 1.7m, lateral0 and pitch0 BAU. Its FAR tuning uses FOV13020 BAU. The default
wide projection applies horizontal/vertical policy scales; at1280x720 the
corresponding full angles are approximately78.67/50.89 degrees.
[inferred] The CPU fit assumes level ground, steady173km/h with zero acceleration,
aligned car/route bearings, a0.35m body/frame offset and bob phase0. It uses the
same camera module and genuine marker geometry as the game. Speed does not change
the steady, aligned FAR entry/FOV; acceleration, slope and body pose can still matter.

[inferred] A single still cannot identify physical distance and scale separately.
Fixing ahead at30m provides a calibration gauge. Height, tilt and scale are then
fit to the owner-observed screen rectangle, using tools/gps_marker_pose_check.c.
[observed] The CPU fit produces bounds(582.50,159.97)..(697.50,240.03), centre
(640.00,200.00), size115.00x80.06 pixels with:

| Parameter | Fitted engine default |
|---|---|
|Ahead|30m [inferred metric gauge]|
|Height|12.285345m [observed screen fit; inferred metric placement]|
|Pitch|-2.75 degrees [observed fit for the stated camera assumptions]|
|Scale|3.540327 [observed screen fit; inferred physical scale]|

[inferred] The five-pixel horizontal difference from the owner's approximate
centre does not justify adding an unsupported lateral offset. The car-heading placement rule keeps turning-route guidance in view [inferred].
Turn placement fidelity, height relative to a sloping road and animation remain uncalibrated.
[data] Existing bob remains0.15m amplitude /1600ms period, both inferred.

## Visibility and scene placement

[data] The host tests current race, pause/results and front-end state before
using the previous frame's navigation flag. The marker is suppressed during
active races, pause, results and every front-end screen, including World Map.
Placement follows the car heading and orientation follows the route bearing.
It draws with the scene camera/target before render_target_present, so render
scale applies. Arrival clears its guidance flag through the existing zone check.
[inferred] Broader race-mode exceptions await moving retail references.

## Validation status

[observed] Validation is in progress. The final desktop build passes with24
warnings and the GPS tests pass98 checks. Final GLES/iOS builds, the remaining
requested checks, reference shots and final live captures remain pending. An initial synthetic
behind-camera test inverted both model and view homogeneous signs, cancelling
its own negative-W fixture. The corrected test changes only the view matrix;
initial failed logs are retained. Initial live captures already confirm the
2D removal, pause/map suppression and arrival clearing; final captures supersede them.

[observed] The source video is https://m.youtube.com/watch?v=W5LGI6tr2GY.
The browser could not open it and the local downloader failed DNS resolution.
Turn/bob calibration is NOT RUN; calibration uses the owner-supplied still only.

[inferred] Additional references needed: unscaled1280x720 recordings at known
30/60fps, default FAR chase, identified car, HUD visible;6–10seconds on a straight
road at steady speed and6–10seconds through left/right turns. Keep the entire
arrow, car roof and horizon visible, without camera switching. Several full bob
cycles and GPS/pause/map/race transitions would constrain animation and visibility.
Device checks remain NOT RUN.

[data] The existing game target omits gps_marker.h from HDRS. A header-only edit
can leave a previous backend binary marked up-to-date. The validation build
explicitly touches main.c before make -j2 nfsu2, then archives that binary before
make gles overwrites it. The build owner should add `HDRS += src/gps_marker.h`;
this task does not edit the Makefile.

[data] The CPU calibration probe builds from the worktree with the following
command, under the shared heavy lock/nice wrapper and memory gate:

```sh
cc -O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Isrc tools/gps_marker_pose_check.c src/camera.c -o build/gps_marker_pose_check -lz -lm
./build/gps_marker_pose_check "$OPENUG2_DATA"
```

[data] An optional second argument supplies a pose instead of fitting. Omitting
the data-root argument exits77; it never substitutes invented marker geometry.
The same GPS synthetic/data tests build with -DN2_GLES; no extra game source or
shader is required.
