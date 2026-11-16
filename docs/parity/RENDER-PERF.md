# Rendering performance — Task L

## Status and measurement scope

Priority1 measurement is complete (ca20b35). Priority2 implementation, matched timings and GL/GLES picture checks are complete.
The required Xvfb gate is BLOCKED by unavailable display sockets in this execution environment;
the task is not fully accepted. See final validation below. The branch and baseline binaries
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

Opaque MAIN also clears over an already-rendered world. Completed extra menu baselines measure
436.24 ms total / 393.59 ms world (GL), 309.58 / 282.14 ms (GLES), about2289 logical draws.
A separate P3 experiment is justified, but remains deferred until the mandatory display gate can
run. Its design must preserve ordinary offscreen passes and pre-FE readbacks; see below.

## Priority 2 implementation

[observed] GL/GLES now provide optional RGBA8 + D24S8 scene targets, separate from ordinary
reflection/shadow passes. Main clear and color/depth copies address the selected target;
GLES depth copies remain unsupported. Physical drawable readback requires presentation first.
Allocation preserves bindings, resize replaces resources transactionally, unsupported/failing
sizes fall back to direct rendering, and frame end recovers a forgotten selection.

[observed] The portable compositor uses a bilinear, clamp-to-edge fullscreen triangle. Scale 1
bypasses all RHI calls. Camera dimensions remain drawable dimensions; scene viewport dimensions
are scaled. Presentation restores the physical drawable before HUD, minimap, touch and menus.
Vulkan explicitly reports scene targets unsupported, so its effective scale remains 1.
The exact owner-only Main/Makefile integration is in RENDER-PERF-INTEGRATION.md.

## Final validation

[observed] The exact integration document was applied only to external scratch source copies.
Production main.c, Makefile and iOS files are untouched. Results:

| Check | Result | Evidence under reports/render-perf/ |
|---|---|---|
| New fake-RHI parser/lifecycle test | PASS | present-final.log, new-make-targets.log |
| New real scene/compositor tests, GL and GLES | PASS, no compiler warnings with -Werror | task-l-test-final.log, new-make-targets.log |
| Scratch game builds, GL and GLES | PASS | build-scene-gl.log, build-scene-gles.log |
| Vulkan backend with current generated shaders | PASS compile; scaled target explicitly unsupported | task-l-vk-final.log, task-l-vk.o |
| Existing 109-target CPU gate | 108 PASS; default sfx-game-check FAIL | gate-cpu/summary.tsv |
| sfx-game-check with ASAN_OPTIONS=detect_leaks=0 | PASS: 10 checks, 0 failures; AddressSanitizer/UBSan retained | sfx-game-check-no-lsan.log |
| LeakSanitizer for that sound check | NOT RUN successfully: fatal unsupported-under-ptrace diagnostic | gate-cpu/sfx-game-check.log |
| Four prescribed Xvfb targets | FAIL at launcher/environment; gate remains blocked | gate-xvfb/summary.tsv, xvfb-diagnostic.log |
| Scale1 invariance, 4 scenes × GL/GLES | PASS, all 8 comparisons have 0 differing decoded pixels | pixel-comparisons.json |
| Scale0.5/0.75 captures | PASS, all 16 inspected by eye: visible correctly oriented scene, placed sharp HUD and menu | shots/scene-* |
| Main menu at all scales | PASS, 0 differing pixels against same-backend native menu | shots/scene-*/menu-correct.png |
| Strict CLI invalid-value rejection | PASS, exit2 for NaN/Inf/out-of-range/trailing junk | scale-cli.log |
| Existing check-gl | PASS (part of CPU gate) | gate-cpu/check-gl.log |

[observed] Xvfb cannot establish listening sockets in this sandbox. A diagnostic explicitly sets
SDL_VIDEODRIVER=x11 and records `_XSERVTransSocketCreateListener: failed to bind listener` and
`Cannot establish any listening sockets`; SDL video initialization then fails. Earlier launcher
runs returned failure even when pipeline/resolution/race assertions logged PASS using the automatic
offscreen fallback. Those are **not** passing Xvfb runs. Vulkan presentation additionally needs a
working X11/Vulkan window. No system settings or socket permissions were changed. The owner must
rerun these four exact targets in an environment supporting Xvfb; no acceptance is claimed here.

[observed] The default sound-check failure comes from LeakSanitizer's environment limitation;
the same target passes with only leak detection disabled. Its missing-bank messages are from its
intentional nonexistent-root test; the installed-data run loads all groups and passes all assertions.
The original FAIL is preserved, and leak detection is not claimed as tested.

## Reviewed PNGs

All paths below are relative to the private reports/render-perf/ directory, never tracked files.
For each backend `gl` and `gles` and each scale `1`, `0.75`, `0.5`, the reviewed capture set is:

- `shots/scene-<backend>-<scale>/l4ra.png`
- `shots/scene-<backend>-<scale>/l4rb.png`
- `shots/scene-<backend>-<scale>/race-active.png`
- `shots/scene-<backend>-<scale>/menu-correct.png`

The unchanged counterparts are `shots/base-<backend>/<scene>.png`. Native captures use 640×480,
GOLF, the indicated STREAM region at frame120, or event4013 at frame240. Menus use a scripted
frame120 after-FE screenshot plus the deterministic-weather background-shot option. Commands are
saved beside each image as JSON. Lower scales intentionally soften world geometry; speedometer,
minimap and race-text placement remain at native resolution. Scale0.75 preserves more scene detail
than0.5. The portable GPU test independently verifies a one-pixel-wide full-resolution UI bar.

## Measured outcome and iOS proposal

[observed] At1920×1080 the fresh scale1 /0.75 /0.5 mean CPU totals are1121/860/395 ms (GL)
and1160/960/667 ms (GLES). These are software-renderer measurements; at640×480 race results are
noisy and GLES0.5 can be slower than1.0. Even unchanged fresh-reference versus scale1 timings vary
substantially while all draw counts match exactly. Do not infer an iPhone speedup or zero native
performance overhead from a single timing ratio. Scale1's zero-RHI-call unit test and exact images
establish the bypass independently of noisy wall time.

[inferred] Propose0.75 as the first iOS device-trial default:56.25% scene pixels, with visibly better
world detail than0.5. Offer0.5 for heavier loads, without claiming its timing is universally better.
Keep the checked-in host default1.0 until the owner enables a device policy. To recover native HUD
sharpness on iOS, the owner must replace the backing-resolution cap with native drawable backing
and scene scaling, measuring memory, frame pacing and thermals on device. No iOS edit or device
validation occurred in this task. The integration document gives the exact owner hooks.

## Priority 3 and 4 disposition

[observed] No P3/P4 optimization is included. P3's strongest separate candidate is suppressing
native main-target draws under the opaque Main menu while retaining validation, parameter updates,
ordinary reflection/shadow passes, and scene/cache update logic. The host must restrict eligibility
to open, visually available MAIN and a process without capture/audit launch arguments; re-enable
before compositing/UI and reset on each frame. Track actual submitted/suppressed native draws
separately from existing logical draw counters. FE animation advances with capped wall time, so
compare controlled UI timestamps and the first visible post-menu scene, not only equal frame numbers.
This remains an experiment for after the blocked display gate is resolved, not an accepted change.

[observed] The measured outliers do not establish streaming, uploads or AI spawning as a cause.
No hitch-spreading change is justified by the current coarse evidence. No artificial improvement
claim is made for CPU trace outliers or the pooled two-trial race timing baseline.

## Paired comparison results

Times are mean/p95 ms. Original is the frozen first baseline; ref is a fresh unchanged binary. Fixed frame windows and inputs are identical; measured durations can decrease at lower scales. Draws are logical engine draws, not a new submitted-draw counter.

### gl-free

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 |
ai_visible | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
draws | 2249.535/2808.000 | 2249.535/2808.000 | 2249.535/2808.000 | 2249.535/2808.000 | 2249.535/2808.000 |
spawn_attempts | 0.035/0.000 | 0.035/0.000 | 0.035/0.000 | 0.035/0.000 | 0.035/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.001/0.001 | 0.000/0.001 | 0.001/0.001 | 0.000/0.001 | 0.000/0.001 |
cpu_player_sim | 3.224/11.073 | 2.269/6.839 | 1.343/5.188 | 0.881/1.250 | 0.851/1.215 |
cpu_ai_follow | 0.009/0.013 | 0.007/0.009 | 0.007/0.009 | 0.007/0.008 | 0.007/0.010 |
cpu_ai_step | 1.307/5.557 | 1.016/4.337 | 0.857/4.299 | 0.602/1.617 | 0.670/2.704 |
cpu_ai_contact | 0.023/0.008 | 0.004/0.006 | 0.004/0.005 | 0.003/0.004 | 0.004/0.005 |
cpu_ai_spawn | 0.002/0.004 | 0.002/0.003 | 0.002/0.003 | 0.001/0.002 | 0.002/0.003 |
cpu_camera | 0.032/0.040 | 0.126/0.030 | 0.016/0.024 | 0.024/0.037 | 0.025/0.041 |
cpu_world_draw | 365.826/491.063 | 277.687/365.694 | 215.859/297.907 | 161.632/227.424 | 166.259/237.067 |
cpu_car_shadow | 0.070/0.011 | 0.006/0.008 | 0.005/0.007 | 0.005/0.006 | 0.005/0.008 |
cpu_player_draw | 22.646/42.204 | 13.665/28.721 | 10.894/18.732 | 10.358/18.908 | 9.428/19.047 |
cpu_ai_draw | 0.021/0.035 | 0.018/0.027 | 0.017/0.021 | 0.014/0.017 | 0.013/0.019 |
cpu_mid_draw | 16.824/31.164 | 11.721/26.278 | 8.303/14.518 | 6.958/12.462 | 6.998/12.912 |
cpu_ai_lamps | 0.006/0.010 | 0.004/0.006 | 0.004/0.006 | 0.004/0.006 | 0.005/0.007 |
cpu_post_draw | 7.874/17.700 | 5.781/12.573 | 4.355/8.364 | 37.684/68.041 | 32.662/52.045 |
cpu_swap | 0.034/0.065 | 0.028/0.041 | 0.221/0.037 | 0.020/0.025 | 0.021/0.039 |
cpu_total | 417.899/561.733 | 312.334/412.589 | 241.886/330.604 | 218.193/287.449 | 216.950/286.434 |

Samples: original: 144 frames, 60.18 s, ref: 144 frames, 44.98 s, scale1: 144 frames, 34.83 s, scale075: 144 frames, 31.42 s, scale05: 144 frames, 31.24 s.

### gl-race

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 |
ai_visible | 2.592/3.000 | 2.592/3.000 | 2.592/3.000 | 2.592/3.000 | 2.592/3.000 |
draws | 1928.204/2414.000 | 1928.204/2414.000 | 1928.204/2414.000 | 1928.204/2414.000 | 1928.204/2414.000 |
spawn_attempts | 0.045/0.000 | 0.045/0.000 | 0.045/0.000 | 0.045/0.000 | 0.045/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.001/0.001 | 0.000/0.001 | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 |
cpu_player_sim | 2.880/13.217 | 3.697/11.171 | 3.071/10.783 | 1.263/4.566 | 1.600/6.562 |
cpu_ai_follow | 0.000/0.000 | 0.000/0.001 | 0.000/0.000 | 0.000/0.001 | 0.000/0.000 |
cpu_ai_step | 3.161/13.133 | 2.429/13.101 | 3.296/13.064 | 1.826/10.635 | 1.828/7.410 |
cpu_ai_contact | 0.031/0.010 | 0.005/0.008 | 0.005/0.009 | 0.005/0.009 | 0.005/0.009 |
cpu_ai_spawn | 0.002/0.005 | 0.028/0.004 | 0.002/0.004 | 0.002/0.004 | 0.002/0.004 |
cpu_camera | 0.030/0.077 | 0.113/0.081 | 0.149/0.111 | 0.050/0.115 | 0.049/0.097 |
cpu_world_draw | 257.813/542.485 | 268.855/383.404 | 291.427/442.185 | 184.734/269.758 | 210.615/325.859 |
cpu_car_shadow | 0.007/0.010 | 0.006/0.010 | 0.006/0.010 | 0.067/0.010 | 0.032/0.011 |
cpu_player_draw | 20.874/54.489 | 21.071/38.687 | 21.685/42.438 | 19.487/36.350 | 19.989/41.403 |
cpu_ai_draw | 68.034/166.924 | 75.665/147.870 | 79.188/151.933 | 59.848/115.196 | 52.505/109.469 |
cpu_mid_draw | 15.550/38.651 | 16.202/29.373 | 16.844/33.816 | 13.100/23.922 | 15.090/32.508 |
cpu_ai_lamps | 0.170/0.208 | 0.255/0.223 | 0.130/0.208 | 0.475/1.242 | 0.263/0.340 |
cpu_post_draw | 6.191/19.195 | 7.351/16.674 | 7.519/16.395 | 59.120/130.300 | 46.738/90.572 |
cpu_swap | 0.081/0.056 | 0.112/0.057 | 0.033/0.048 | 0.027/0.053 | 0.029/0.050 |
cpu_total | 374.825/757.735 | 395.790/533.813 | 423.357/642.164 | 340.006/448.564 | 348.746/502.435 |

Samples: original: 157 frames, 58.85 s, ref: 157 frames, 62.14 s, scale1: 157 frames, 66.47 s, scale075: 157 frames, 53.38 s, scale05: 157 frames, 54.75 s.

### gl-fill

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 |
ai_visible | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
draws | 2444.382/3042.000 | 2444.382/3042.000 | 2444.382/3042.000 | 2444.382/3042.000 | 2444.382/3042.000 |
spawn_attempts | 0.029/0.000 | 0.029/0.000 | 0.029/0.000 | 0.029/0.000 | 0.029/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.000/0.001 | 0.000/0.001 | 0.001/0.001 | 0.001/0.001 | 0.000/0.001 |
cpu_player_sim | 1.426/3.644 | 1.736/5.303 | 2.084/8.477 | 1.778/5.589 | 1.257/2.978 |
cpu_ai_follow | 0.009/0.015 | 0.009/0.014 | 0.009/0.013 | 0.010/0.014 | 0.008/0.011 |
cpu_ai_step | 0.848/2.841 | 0.937/4.330 | 0.880/2.844 | 0.929/4.673 | 0.849/4.741 |
cpu_ai_contact | 0.004/0.006 | 0.005/0.008 | 0.004/0.006 | 0.005/0.006 | 0.004/0.007 |
cpu_ai_spawn | 0.002/0.003 | 0.002/0.004 | 0.002/0.003 | 0.002/0.003 | 0.002/0.003 |
cpu_camera | 0.020/0.038 | 0.021/0.033 | 0.078/0.032 | 0.081/0.099 | 0.031/0.051 |
cpu_world_draw | 291.668/474.442 | 332.177/440.362 | 351.274/494.143 | 377.128/495.198 | 254.202/352.962 |
cpu_car_shadow | 0.006/0.009 | 0.006/0.011 | 0.006/0.009 | 0.006/0.011 | 0.005/0.009 |
cpu_player_draw | 18.401/43.001 | 24.786/46.886 | 21.870/41.441 | 24.045/50.349 | 15.890/35.397 |
cpu_ai_draw | 0.061/0.100 | 0.133/0.131 | 0.110/0.112 | 0.042/0.061 | 0.024/0.040 |
cpu_mid_draw | 12.452/25.064 | 16.212/33.601 | 14.474/27.303 | 15.495/28.709 | 9.400/19.045 |
cpu_ai_lamps | 0.005/0.007 | 0.005/0.009 | 0.005/0.009 | 0.005/0.007 | 0.012/0.009 |
cpu_post_draw | 565.004/819.553 | 687.317/835.678 | 730.518/894.890 | 440.615/612.546 | 112.846/170.659 |
cpu_swap | 0.046/0.084 | 0.056/0.109 | 0.051/0.082 | 0.050/0.099 | 0.043/0.080 |
cpu_total | 889.952/1263.640 | 1063.403/1216.618 | 1121.366/1289.277 | 860.190/1005.470 | 394.574/503.830 |

Samples: original: 68 frames, 60.52 s, ref: 68 frames, 72.31 s, scale1: 68 frames, 76.25 s, scale075: 68 frames, 58.49 s, scale05: 68 frames, 26.83 s.

### gles-free

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 |
ai_visible | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
draws | 2254.254/2808.000 | 2254.254/2808.000 | 2254.254/2808.000 | 2254.254/2808.000 | 2254.254/2808.000 |
spawn_attempts | 0.032/0.000 | 0.032/0.000 | 0.032/0.000 | 0.032/0.000 | 0.032/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.001/0.001 | 0.000/0.001 | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 |
cpu_player_sim | 3.087/9.697 | 1.934/6.576 | 3.333/13.007 | 1.513/6.784 | 1.207/2.481 |
cpu_ai_follow | 0.010/0.017 | 0.007/0.009 | 0.009/0.015 | 0.009/0.013 | 0.009/0.014 |
cpu_ai_step | 1.408/6.173 | 0.997/4.522 | 1.599/6.974 | 0.795/2.709 | 0.839/2.814 |
cpu_ai_contact | 0.005/0.008 | 0.004/0.005 | 0.005/0.008 | 0.004/0.007 | 0.005/0.008 |
cpu_ai_spawn | 0.002/0.004 | 0.002/0.003 | 0.002/0.005 | 0.002/0.003 | 0.002/0.004 |
cpu_camera | 0.024/0.044 | 0.018/0.026 | 0.024/0.041 | 0.129/0.063 | 0.035/0.061 |
cpu_world_draw | 420.389/610.677 | 249.624/341.553 | 413.894/581.268 | 227.541/332.755 | 226.103/325.628 |
cpu_car_shadow | 0.007/0.012 | 0.005/0.007 | 0.006/0.010 | 0.006/0.009 | 0.007/0.011 |
cpu_player_draw | 25.678/59.116 | 13.017/22.488 | 24.039/50.090 | 14.034/30.235 | 14.657/26.530 |
cpu_ai_draw | 0.004/0.007 | 0.004/0.005 | 0.004/0.009 | 0.005/0.007 | 0.005/0.008 |
cpu_mid_draw | 17.158/34.199 | 10.366/22.145 | 16.166/36.649 | 10.764/25.018 | 10.040/17.916 |
cpu_ai_lamps | 0.006/0.010 | 0.005/0.007 | 0.006/0.010 | 0.005/0.010 | 0.006/0.010 |
cpu_post_draw | 9.293/22.147 | 4.983/11.655 | 8.589/20.270 | 64.049/120.870 | 39.706/81.863 |
cpu_swap | 0.021/0.035 | 0.036/0.025 | 0.072/0.040 | 0.026/0.035 | 0.019/0.033 |
cpu_total | 477.093/685.555 | 281.001/374.880 | 467.749/646.648 | 318.882/424.101 | 292.639/392.235 |

Samples: original: 126 frames, 60.11 s, ref: 126 frames, 35.41 s, scale1: 126 frames, 58.94 s, scale075: 126 frames, 40.18 s, scale05: 126 frames, 36.87 s.

### gles-race

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 | 3.000/3.000 |
ai_visible | 2.761/3.000 | 2.761/3.000 | 2.761/3.000 | 2.761/3.000 | 2.761/3.000 |
draws | 1936.239/2443.000 | 1936.239/2443.000 | 1936.239/2443.000 | 1936.239/2443.000 | 1936.239/2443.000 |
spawn_attempts | 0.037/0.000 | 0.037/0.000 | 0.037/0.000 | 0.037/0.000 | 0.037/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 | 0.001/0.001 |
cpu_player_sim | 3.197/13.042 | 2.104/7.954 | 2.399/10.267 | 1.267/3.640 | 2.174/7.832 |
cpu_ai_follow | 0.000/0.001 | 0.000/0.001 | 0.000/0.001 | 0.000/0.001 | 0.000/0.001 |
cpu_ai_step | 2.838/10.079 | 2.355/8.927 | 3.069/8.768 | 1.801/5.400 | 2.039/9.453 |
cpu_ai_contact | 0.006/0.010 | 0.005/0.007 | 0.005/0.007 | 0.005/0.008 | 0.006/0.010 |
cpu_ai_spawn | 0.002/0.005 | 0.002/0.004 | 0.002/0.004 | 0.002/0.004 | 0.003/0.005 |
cpu_camera | 0.077/0.163 | 0.032/0.075 | 0.033/0.078 | 0.054/0.166 | 6.023/0.126 |
cpu_world_draw | 303.865/449.104 | 213.982/301.804 | 261.361/431.673 | 170.782/246.134 | 230.873/366.088 |
cpu_car_shadow | 0.006/0.011 | 0.005/0.008 | 0.005/0.010 | 0.006/0.011 | 0.007/0.010 |
cpu_player_draw | 22.014/42.909 | 13.573/26.637 | 16.456/36.204 | 16.709/33.038 | 23.507/47.287 |
cpu_ai_draw | 93.570/174.795 | 59.672/104.977 | 80.208/172.572 | 67.160/124.364 | 80.969/142.539 |
cpu_mid_draw | 16.010/35.400 | 11.268/23.959 | 13.981/32.779 | 12.400/25.008 | 14.891/32.536 |
cpu_ai_lamps | 0.348/0.418 | 0.178/0.263 | 0.184/0.285 | 0.206/0.370 | 0.481/0.806 |
cpu_post_draw | 7.613/20.947 | 5.197/12.148 | 6.933/19.545 | 57.275/117.419 | 50.416/99.457 |
cpu_swap | 0.043/0.045 | 0.020/0.032 | 0.021/0.037 | 0.043/0.038 | 0.023/0.041 |
cpu_total | 449.590/619.638 | 308.393/408.400 | 384.658/684.975 | 327.711/411.218 | 411.413/575.848 |

Samples: original: 134 frames, 60.25 s, ref: 134 frames, 41.32 s, scale1: 134 frames, 51.54 s, scale075: 134 frames, 43.91 s, scale05: 134 frames, 55.13 s.

### gles-fill

Metric | Original | Fresh reference | Scale 1 | Scale 0.75 | Scale 0.5 |
--- | ---: | ---: | ---: | ---: | ---: |
traffic | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 | 4.000/4.000 |
racers | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 | 2.000/2.000 |
ai_visible | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
draws | 2461.340/3047.000 | 2461.340/3047.000 | 2461.340/3047.000 | 2461.340/3047.000 | 2461.340/3047.000 |
spawn_attempts | 0.038/0.000 | 0.038/0.000 | 0.038/0.000 | 0.038/0.000 | 0.038/0.000 |
spawn_checks | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 | 0.000/0.000 |
cpu_top | 0.001/0.001 | 0.000/0.001 | 0.000/0.001 | 0.001/0.001 | 0.001/0.001 |
cpu_player_sim | 2.824/11.203 | 1.927/6.662 | 1.941/5.813 | 1.450/4.089 | 1.560/5.257 |
cpu_ai_follow | 0.178/0.012 | 0.008/0.013 | 0.008/0.012 | 0.008/0.012 | 0.009/0.012 |
cpu_ai_step | 1.248/7.447 | 0.899/4.415 | 1.027/4.408 | 0.974/3.933 | 0.998/4.566 |
cpu_ai_contact | 0.005/0.008 | 0.005/0.009 | 0.005/0.008 | 0.005/0.006 | 0.005/0.008 |
cpu_ai_spawn | 0.002/0.004 | 0.002/0.004 | 0.002/0.003 | 0.002/0.003 | 0.002/0.004 |
cpu_camera | 0.022/0.038 | 0.028/0.045 | 0.020/0.036 | 0.035/0.056 | 0.037/0.059 |
cpu_world_draw | 1085.582/1347.308 | 340.274/488.923 | 343.671/474.931 | 445.942/555.341 | 423.884/576.712 |
cpu_car_shadow | 0.006/0.010 | 0.006/0.009 | 0.006/0.009 | 0.006/0.011 | 0.006/0.009 |
cpu_player_draw | 24.428/54.118 | 24.057/46.161 | 22.890/49.941 | 28.165/43.910 | 23.393/46.889 |
cpu_ai_draw | 0.004/0.006 | 0.004/0.006 | 0.004/0.006 | 0.004/0.007 | 0.004/0.007 |
cpu_mid_draw | 16.013/33.478 | 16.858/41.879 | 14.606/32.111 | 17.861/41.098 | 16.992/30.743 |
cpu_ai_lamps | 0.007/0.014 | 0.005/0.008 | 0.005/0.010 | 0.006/0.009 | 0.006/0.008 |
cpu_post_draw | 9.512/23.368 | 764.562/886.939 | 775.909/943.921 | 465.897/611.674 | 200.282/373.272 |
cpu_swap | 0.248/0.043 | 0.018/0.029 | 0.017/0.029 | 0.021/0.037 | 0.020/0.032 |
cpu_total | 1140.081/1432.427 | 1148.653/1365.397 | 1160.111/1355.989 | 960.376/1064.589 | 667.198/759.455 |

Samples: original: 53 frames, 60.42 s, ref: 53 frames, 60.88 s, scale1: 53 frames, 61.49 s, scale075: 53 frames, 50.90 s, scale05: 53 frames, 35.36 s.
