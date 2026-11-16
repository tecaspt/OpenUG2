# Rendering performance — Task L

## Status and measurement scope

Priority1 measurement is complete; the scaled-target implementation has not started. The branch and baseline binaries
are pinned to private/parity/m1 at0778af0. Production main.c, Makefile and iOS sources are untouched.
CSV files, logs and images stay under the private reports/render-perf directory, outside this repo.

Both offscreen contexts are Mesa llvmpipe (LLVM19.1.7,128-bit), Mesa25.0.7. Desktop reports GL4.5
compatibility and timer queries; GLES reports ES3.2 and no timer queries. These are **software-renderer
measurements on the Pi**, not hardware V3D or iPhone GPU results. Desktop GPU-query values are software
elapsed observations and will not be presented as hardware GPU timings. cpu_swap is reported, but
blocking can also occur during draw submission, so low swap time does not prove spare GPU capacity.

Every build/run uses env.sh, available memory>=2500MiB, the shared flock, nice10 and make-j2 at most.
Offscreen runs never force LIBGL_ALWAYS_SOFTWARE. Perf runs cannot overlap another locked heavy job.
The separate required Xvfb test set uses its prescribed software driver without offscreen SDL.

Input scripts enable main.c's deterministic_sim branch: one physics tick per rendered frame.
The same frozen scripts/frame counts are reused before and after each change. Weather and asynchronous
residency still have wall-clock components; command equality alone does not prove equal scenes.
Validate event/racer counts and script speed/input traces alongside timings/draw counts. Warmups
exclude initial loading; race warmup180 excludes its countdown. Target duration is about60s of
steady measured cpu_total; total process duration also includes startup, locking and warmup.

## District scouting (desktop GL, pilots only)

Values are mean/p95, milliseconds for CPU sections and counts for draws/racers. Each free-roam
pilot quit at frame60 and excluded0..9; race quit at240 and excluded0..179. The initial game-launch
permission error occurred before execution and is excluded. All four successful pilots exited0.

| Scene | Steady frames | CPU total ms | World draw ms | Swap ms | Draws | Racers |
|---|---:|---:|---:|---:|---:|---:|
| l4ra | 51 | 504.18/667.77 | 441.68/599.49 | 0.04/0.06 | 2272.75/2855.00 | 2.00/2.00 |
| l4rb | 51 | 332.36/511.59 | 307.63/491.56 | 0.15/0.07 | 412.29/485.00 | 2.00/2.00 |
| l4rc | 51 | 114.12/150.88 | 90.69/114.99 | 0.04/0.07 | 322.08/474.00 | 2.00/2.00 |
| race | 61 | 625.35/839.46 | 427.49/571.84 | 0.17/0.07 | 1912.16/2413.00 | 3.00/3.00 |

L4RA is the heaviest observed initial view among these three bundles. This is not an exhaustive
district survey. The third long-run case repeats this view at1920x1080 as an explicitly labeled
fill-stress case; it does not invent a separate heavier district. The race uses event4013 onL4RA,
with verified active event, AI and player throttle after countdown.

The world-draw section dominates. All free-roam L4RA pilot frames exceed33.333ms under llvmpipe;
that is a slow steady baseline, not evidence that every frame contains a streaming hitch.
Player simulation/camera are much smaller in this sample. Causal hitch/optimization conclusions
await the frozen full baselines and matched after measurements.

## Completed measurement baselines

Each case below contains about60s of steady profiler time. Process wall time also includes loading,
memory/lock waits and warmup; longer completed runs are sampled using the fixed frame windows shown.
GL race combines two completed runs totaling58.8s; every later comparison must repeat both commands
and aggregate their same windows. Other cases use the first contiguous window reaching60s.
All source runs exited0 and their CSVs contain every frame from0 through their scripted quit frame.

| Case | Resolution | Sample frames | Measured seconds | Source frame windows |
|---|---|---:|---:|---|
| gl-free | 640x480 | 144 | 60.18 | baseline-gl-free-extended: 20–163 (quit 600) |
| gl-race | 640x480 | 157 | 58.85 | pilot-gl-race: 180–240 (quit 240); baseline-gl-race: 180–275 (quit 275) |
| gl-fill | 1920x1080 | 68 | 60.52 | baseline-gl-fill: 20–87 (quit 129) |
| gles-free | 640x480 | 126 | 60.11 | baseline-gles-free: 20–145 (quit 305) |
| gles-race | 640x480 | 134 | 60.25 | baseline-gles-race: 180–313 (quit 467) |
| gles-fill | 1920x1080 | 53 | 60.42 | baseline-gles-fill: 20–72 (quit 110) |

### GL — section means/p95

CPU times are milliseconds; counters are counts. GPU columns are omitted from conclusions because
the context is software llvmpipe (desktop queries are not physical-GPU timings).

| Metric | Free roam | Race4013 | 1080p fill stress |
|---|---:|---:|---:|
| cpu_top | 0.001/0.001 | 0.001/0.001 | 0.000/0.001 |
| cpu_player_sim | 3.224/11.073 | 2.880/13.217 | 1.426/3.644 |
| cpu_ai_follow | 0.009/0.013 | 0.000/0.000 | 0.009/0.015 |
| cpu_ai_step | 1.307/5.557 | 3.161/13.133 | 0.848/2.841 |
| cpu_ai_contact | 0.023/0.008 | 0.031/0.010 | 0.004/0.006 |
| cpu_ai_spawn | 0.002/0.004 | 0.002/0.005 | 0.002/0.003 |
| cpu_camera | 0.032/0.040 | 0.030/0.077 | 0.020/0.038 |
| cpu_world_draw | 365.826/491.063 | 257.813/542.485 | 291.668/474.442 |
| cpu_car_shadow | 0.070/0.011 | 0.007/0.010 | 0.006/0.009 |
| cpu_player_draw | 22.646/42.204 | 20.874/54.489 | 18.401/43.001 |
| cpu_ai_draw | 0.021/0.035 | 68.034/166.924 | 0.061/0.100 |
| cpu_mid_draw | 16.824/31.164 | 15.550/38.651 | 12.452/25.064 |
| cpu_ai_lamps | 0.006/0.010 | 0.170/0.208 | 0.005/0.007 |
| cpu_post_draw | 7.874/17.700 | 6.191/19.195 | 565.004/819.553 |
| cpu_swap | 0.034/0.065 | 0.081/0.056 | 0.046/0.084 |
| cpu_total | 417.899/561.733 | 374.825/757.735 | 889.952/1263.640 |
| draws | 2249.535/2808.000 | 1928.204/2414.000 | 2444.382/3042.000 |
| traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
| racers | 2.000/2.000 | 3.000/3.000 | 2.000/2.000 |
| ai_visible | 0.000/0.000 | 2.592/3.000 | 0.000/0.000 |
| wall_candidates | 126498.000/126498.000 | 148141.000/148141.000 | 126498.000/126498.000 |
| spawn_attempts | 0.035/0.000 | 0.045/0.000 | 0.029/0.000 |
| spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |

| Hitch diagnostic | Free roam | Race4013 | 1080p fill stress |
|---|---:|---:|---:|
| Frames over33.333ms | 144/144 | 157/157 | 68/68 |
| Frames over100ms | 144/144 | 157/157 | 68/68 |
| Frames over2× case median | 0/144 | 54/157 | 1/68 |

### GLES — section means/p95

CPU times are milliseconds; counters are counts. GPU columns are omitted from conclusions because
the context is software llvmpipe (desktop queries are not physical-GPU timings).

| Metric | Free roam | Race4013 | 1080p fill stress |
|---|---:|---:|---:|
| cpu_top | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 |
| cpu_player_sim | 3.087/9.697 | 3.197/13.042 | 2.824/11.203 |
| cpu_ai_follow | 0.010/0.017 | 0.000/0.001 | 0.178/0.012 |
| cpu_ai_step | 1.408/6.173 | 2.838/10.079 | 1.248/7.447 |
| cpu_ai_contact | 0.005/0.008 | 0.006/0.010 | 0.005/0.008 |
| cpu_ai_spawn | 0.002/0.004 | 0.002/0.005 | 0.002/0.004 |
| cpu_camera | 0.024/0.044 | 0.077/0.163 | 0.022/0.038 |
| cpu_world_draw | 420.389/610.677 | 303.865/449.104 | 1085.582/1347.308 |
| cpu_car_shadow | 0.007/0.012 | 0.006/0.011 | 0.006/0.010 |
| cpu_player_draw | 25.678/59.116 | 22.014/42.909 | 24.428/54.118 |
| cpu_ai_draw | 0.004/0.007 | 93.570/174.795 | 0.004/0.006 |
| cpu_mid_draw | 17.158/34.199 | 16.010/35.400 | 16.013/33.478 |
| cpu_ai_lamps | 0.006/0.010 | 0.348/0.418 | 0.007/0.014 |
| cpu_post_draw | 9.293/22.147 | 7.613/20.947 | 9.512/23.368 |
| cpu_swap | 0.021/0.035 | 0.043/0.045 | 0.248/0.043 |
| cpu_total | 477.093/685.555 | 449.590/619.638 | 1140.081/1432.427 |
| draws | 2254.254/2808.000 | 1936.239/2443.000 | 2461.340/3047.000 |
| traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
| racers | 2.000/2.000 | 3.000/3.000 | 2.000/2.000 |
| ai_visible | 0.000/0.000 | 2.761/3.000 | 0.000/0.000 |
| wall_candidates | 126498.000/126498.000 | 148141.000/148141.000 | 126498.000/126498.000 |
| spawn_attempts | 0.032/0.000 | 0.037/0.000 | 0.038/0.000 |
| spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |

| Hitch diagnostic | Free roam | Race4013 | 1080p fill stress |
|---|---:|---:|---:|
| Frames over33.333ms | 126/126 | 134/134 | 53/53 |
| Frames over100ms | 126/126 | 134/134 | 53/53 |
| Frames over2× case median | 2/126 | 0/134 | 0/53 |

### Reproduction and interpretation

The frozen source runs use the following frame counts/warmups. Every command has GOLF, STREAML4RA,
--touch, --input-script and --perf-csv; race adds --event4013. Free/race are640x480; fill is1920x1080.
The frame windows above, rather than a new time cutoff, must be used after a change.

| Backend/case | Quit frame | Warmup |
|---|---:|---:|
| GL free |600|20|
| GL race trial1 |240|180|
| GL race trial2 |275|180|
| GL fill |129|20|
| GLES free |305|20|
| GLES race |467|180|
| GLES fill |110|20|

The exact executable form (replace the uppercase report paths, not the arguments) is:
```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
flock /home/nike/openug2-dev/.heavy.lock nice -n 10 timeout 1800 \
  "$REPORT/baseline-BACKEND" --path "$OPENUG2_DATA" --car GOLF --track STREAML4RA \
  --resolution WIDTHxHEIGHT --touch --input-script "@$REPORT/runs/LABEL/input.txt" \
  --perf-csv "$REPORT/runs/LABEL/perf.csv"
```
Add `--event4013` as two arguments `--event 4013` for the race. Generate the input file with gas down
at0; at20+80k steer-left down,30+80k left up,40+80k right down,50+80k right up (complete groups
ending before quit-1); gas up at quit-1, quit at the listed frame. The game draws the quit frame
and writes CSV rows0..quit inclusive. Use the same script contents and windows for each comparison.
Reports are external; no CSV, raw data or captures are committed. XDG config/data roots are isolated
under the report root and shared across matched runs.

At the exact same frames20..58 with the exact same2279.1 mean draw count, GL pilot CPU total was
507.7ms versus233.0ms in the first long run; GLES was211.4ms versus513.8ms. This exceeds2× variation
without any source change. Therefore subsequent acceptance needs interleaved reference/treatment
runs, not attribution of every timing delta to code. Production CPU load, scheduling, clock and
software-driver variability remain confounders. The later long refinement runs are supplementary,
not replacements silently selected for a favorable result.

World drawing dominates all cases. In the pooled GL-race diagnostic,54 frames exceed2× the pooled
median because the two runs have different timing baselines; this is not54 proven in-game hitches.
Within other windows, two GLES-free outliers are dominated by world draw and one GL-fill outlier by
post_draw. The coarse profiler cannot establish whether that latter event is a HUD upload, driver
stall or scheduling event. Spawn-time means are microseconds and do not establish spawning as the
cause. No measured evidence yet justifies changing streaming or AI-spawn behavior.

Opaque MAIN also clears over an already-rendered world. A separate optional menu baseline is being
collected before considering a P3 change. Such a change must preserve offscreen reflection/shadow
updates and all pre-FE capture/audit paths, not simply skip a large block of frame logic.

## Invariance and final checks

NOT RUN for a treatment build. Required gate:109 CPU targets plus render-resolution-test,
rhi-pipeline-test,rhi-vk-present-test,race-ai-test under Xvfb. All targets exist in the pinned Makefile.
Scale1 must match decoded pixels against the unchanged same-backend baseline for L4RA/L4RB,
race and menu. Scale0.5/0.75 must render scene and full-resolution UI; GLES additionally tests0.5.
The host's ordinary --shot capture precedes FE overlays, so menu proof uses a scripted after-FE shot
while a later --shot keeps weather deterministic. Baseline menu repeatability is checked separately.

## Proposed implementation (not started)

Use a logical scene-main target separate from ordinary reflection/shadow passes. It needs packed
24-bit depth plus8-bit stencil for projected car shadows. Scale1 bypasses all scaling resources
and compositor calls. Upscale after all3D/world effects and before the first diagnostic HUD,
minimap, touch and FE/pause rendering. Vulkan will explicitly report unsupported and use scale1.
The iOS default will be proposed from the matched results, with a device-performance caveat.
