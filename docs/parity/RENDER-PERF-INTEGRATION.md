# Render target host integration (base 0778af0)

**Status:** Host changes applied and tested in external scratch builds only. The following patches are the exact externally prepared scratch integration; applying them to `src/main.c` and `Makefile` is the host owner's responsibility. This document does not apply them. Context anchors are against base `0778af0`; check the surrounding code before applying to another revision. The implementation depends on the task-L changes to `rhi.h`, the GL/GLES and Vulkan backends, and `render_target.{c,h}`.

**[observed]** `render_target_parse_scale` accepts only a complete finite number in `[0.5, 1.0]`; whitespace prefix, extra text, infinities, NaNs and range errors fail without updating the output. The host patch returns exit status 2 on invalid or missing input, and puts `--render-scale` into the config-only allowlist so it does not alone force a gameplay start. The default is `1.0`. At 1.0 `render_target_begin` bypasses target operations; unsupported targets and allocation/selection failure fall back to effective scale 1. Vulkan reports the scene target unsupported. The startup line records requested and effective scale, although a first-frame line alone does not prove the target remains available after a later resize.

**[inferred]** Begin after the drawable frame dimensions are known and before the sky clear and world drawing. Present after collision/debug scene geometry and before the race and other HUD drawing. The world, vehicles, scene effects and scene color copies then use the selected scene target; HUD, minimap, touch, front end and pause UI draw after the upscale on the physical drawable. Preserve `W` and `H` as drawable dimensions for camera/projection and UI: target selection sets the scaled viewport and the RHI's main-copy implementation uses the selected target's actual bounds. This avoids changing aspect and UI placement. Target dimensions are `max(1, floor(drawable_dimension * scale))`; the final color is linearly upscaled. The selected target must be unselected before final readback or frame end. Destroy it while the device/context still exists. Verify every host path that begins a scene reaches presentation; RHI frame-end cleanup is recovery, not a replacement for a composite.

## Exact proposed host patches

Apply these two patches together, from the repository root. They add a CLI/config-only setting and build the scene target in every `SRC`-based build. The live code may have moved; do not silently apply at a different location.

```diff
--- a/src/main.c
+++ b/src/main.c
@@ -28,6 +28,7 @@
 #include "nfsu2.h"
 #include "car_mod.h"
 #include "render.h"
+#include "render_target.h"
 #include "rhi.h"
 #include "physics.h"
 #include "drive_stage1.h"
@@ -2329,7 +2330,7 @@
         "--drivetrain", "--no-drivetrain", "--no-hud", "--hud", "--hud-skin", "--hud-mph", "--hud-inferred-redline",
         "--hud-shift-indicator", "--hud-no-shift-indicator", "--hud-no-shift-flash", "--hud-nos-level",
         "--hud-nos-glow", "--hud-map-window", "--hud-fit", "--hud-glyph-half-texel-inset", "--hud-fitted", "--no-map-cache",
-        "--lighting", "--map-detail", "--world2", "--resident-sync", "--resident-realtime", "--perf-csv", "--career-dev", "--debug-sink",
+        "--lighting", "--map-detail", "--world2", "--resident-sync", "--resident-realtime", "--render-scale", "--perf-csv", "--career-dev", "--debug-sink",
     };
     for (int i = 1; i < argc; i++) {
         if (strncmp(argv[i], "--", 2)) continue;
@@ -2383,6 +2384,7 @@
          --pause-off FILE save same-frame frozen scene-only pair before UI
          --pause-reference capture independent populated authored reference with UI replay
          --resolution WxH window/render size (default 1920x1080; also used by audits)
+         --render-scale X scene-only resolution scale, 0.5..1.0 (default 1; UI stays full size)
          --carinfo CAR    dump CAR's part list + texture catalog and exit (GL-free)
          --world2         legacy alias; instance world is automatic for one STREAM
          --scenery-preview free|EVENT  load-time event-only scenery test (world2 capture/audit only)
@@ -2507,6 +2509,8 @@
     const char *ai_drive_audit = NULL; /* input-only route follower; reuses RA evidence */
     int follow_roam_ai = 0; /* ride along with the first simulated traffic car */
     const char *perf_csv = NULL;  /* --perf-csv FILE: per-frame section timings */
+    float render_scale=1.0f;
+    RenderTarget scene_target={0};
     int world_cull_off = 0; /* diagnostic A/B, never changes loading or shadows */
     int perf_still = 0;           /* --perf-still: no --shot auto-throttle */
     int collision_source = WCOLLISION_BUN;
@@ -2768,6 +2772,11 @@
         }
         else if (!strcmp(argv[i], "--follow-roam-ai")) follow_roam_ai = 1;
         else if (!strcmp(argv[i], "--perf-csv") && i+1 < argc) perf_csv = argv[++i];
+        else if (!strcmp(argv[i], "--render-scale")) {
+            if(i+1>=argc || !render_target_parse_scale(argv[++i],&render_scale)) {
+                fprintf(stderr,"--render-scale expects a finite number in [0.5,1.0]\n");return 2;
+            }
+        }
         else if (!strcmp(argv[i], "--perf-still")) perf_still = 1;
         else if (!strcmp(argv[i], "--traffic-cull-off")) g_dbg.traffic_cull_off = 1;
         else if (!strcmp(argv[i], "--perf-orbit") && i+1 < argc) perf_orbit = (float)atof(argv[++i]);
@@ -9806,6 +9815,9 @@
             continue;
         }
         int W = frame.width, H = frame.height;
+        render_target_begin(&scene_target,W,H,render_scale);
+        if(frame.frame_index==0)printf("render scale: requested=%.3f effective=%.3f drawable=%dx%d scene=%dx%d\n",
+            render_scale,scene_target.effective_scale,W,H,scene_target.scene_w,scene_target.scene_h);
         pf_mark(PF_CAMERA); pf_gpu(pf_frame,0);
         /* the sky is cleared to the fog colour: distant geometry dissolves
            into exactly what the horizon shows */
@@ -11382,6 +11394,7 @@
         collision_debug_draw(&rp,&wall_debug_vbo,&scene,
             (const float (*)[4])world.neighborhood.mbb,obstsrc,nobst,
             g_dbg.freecam?cam:render_player.pos,render_player.pos,MVP);
+        render_target_present(&scene_target);
 
         if(!hud && (world.city.race.active || (ai_race && race_state!=3))) {
             hud=hud_init(dataroot);
@@ -12938,6 +12951,7 @@
     free_headlight_shadows(&headlight_shadows);
     clear_car_vinyl();
     tex_override_shutdown();   /* frees the index only; no-op when off */
+    render_target_destroy(&scene_target);
     rhi_device_destroy(); SDL_DestroyWindow(win); SDL_Quit();
     return final_status;
 }
```

```diff
--- a/Makefile
+++ b/Makefile
@@ -60,6 +60,8 @@
 SRC += src/game_catalog.c src/game_flow.c src/input_script.c src/touch_controls.c src/ui_flat.c src/fe_entries.c src/fe_screen.c src/fe_menu.c src/fe_track.c src/fe_car.c src/postrace.c src/fe_profile.c
 SRC += src/career_data.c src/career.c src/career_host.c   # M6 career
 SRC += src/race_rules.c   # M7 race rules (task F)
+SRC += src/render_target.c   # Task L scene target and upscale
+HDRS += src/render_target.h
 HDRS += src/hud_race.h src/dir_scan.h
 HDRS += src/game_catalog.h src/game_flow.h src/input_script.h src/touch_controls.h src/ui_flat.h src/fe_entries.h src/fe_screen.h src/fe_menu.h
 HDRS += src/player_motion.h src/map_bake.h src/graphics_policy.h src/route_guide.h
```

## Proposed Makefile test targets

**[inferred]** Add the following recipes to the Makefile with the host changes. The fake-RHI executable exercises both GL and GLES backend selections within one invocation; the scene and compositor targets compile and run actual GL and GLES variants independently. These recipes were applied to the scratch Makefile and all three targets passed (GL and GLES); see the status report for the separate full-gate limitation.

```make
.PHONY: render-target-test render-scene-test render-target-gl-test

render-target-test:
	@mkdir -p build
	$(CC) -std=c99 -Wall -Wextra -Werror -Isrc src/render_target.c tools/render_target_test.c -lm -o build/render_target_test
	./build/render_target_test

render-scene-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter $(SDL_CFLAGS) -Isrc tools/render_scene_test.c src/rhi_gl.c src/rhi_gl_state.c $(SDL_LIBS) -lGL -lm -o build/render_scene_gl_test
	SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/render_scene_gl_test
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter -DN2_GLES $(SDL_CFLAGS) -Isrc tools/render_scene_test.c src/rhi_gl.c src/rhi_gl_state.c $(SDL_LIBS) -lGLESv2 -lm -o build/render_scene_gles_test
	SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/render_scene_gles_test

render-target-gl-test:
	@mkdir -p build
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -Isrc $(SDL_CFLAGS) tools/render_target_gl_test.c src/render_target.c src/rhi_gl.c src/rhi_gl_state.c -o build/render_target_gl_test $(SDL_LIBS) -lGL -lm
	SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/render_target_gl_test
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -DN2_GLES -Isrc $(SDL_CFLAGS) tools/render_target_gl_test.c src/render_target.c src/rhi_gl.c src/rhi_gl_state.c -o build/render_target_gles_test $(SDL_LIBS) -lGLESv2 -lm
	SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/render_target_gles_test
```

**[inferred]** Once the owner applies these recipes, invoke them under the shared lock; the wrapper sources the environment, checks available memory inside the lock and runs `nice -n 10`:

```bash
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh make -j2 render-target-test
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh make -j2 render-scene-test
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh make -j2 render-target-gl-test
```

## Standalone validation commands

The following are the exact bodies of the external validation scripts. They are standalone tests, not a substitute for the existing full test gate, game captures, or before/after performance measurements. **[observed]** these commands and the three Make targets passed; the separate full-gate limitation is recorded below. Run compilation and each test under the project's shared heavy lock, after sourcing `/home/nike/openug2-dev/env.sh` and checking at least 2500 MiB available inside the lock. The supplied `run-heavy.sh` enforces that check and `nice -n 10`; a script invocation below serializes its tests under one lock.

```bash
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh bash /home/nike/openug2-dev/reports/render-perf/test-present.sh
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh bash /home/nike/openug2-dev/reports/render-perf/task-l-test.sh
bash /home/nike/openug2-dev/reports/render-perf/run-heavy.sh bash /home/nike/openug2-dev/reports/render-perf/task-l-vk.sh
```

`test-present.sh` (pure scale/parser and compositor GL/GLES tests):

```bash
#!/bin/bash
set -eu
cd /home/nike/openug2-dev/wt-render
cc -std=c99 -Wall -Wextra -Werror -Isrc src/render_target.c tools/render_target_test.c -lm -o /tmp/render_target_test
/tmp/render_target_test
cc -std=c99 -O2 -Isrc $(sdl2-config --cflags) tools/render_target_gl_test.c src/render_target.c src/rhi_gl.c src/rhi_gl_state.c -o /tmp/render_target_gl_test $(sdl2-config --libs) -lGL -lm
SDL_VIDEODRIVER=offscreen /tmp/render_target_gl_test
cc -std=c99 -O2 -DN2_GLES -Isrc $(sdl2-config --cflags) tools/render_target_gl_test.c src/render_target.c src/rhi_gl.c src/rhi_gl_state.c -o /tmp/render_target_gles_test $(sdl2-config --libs) -lGLESv2 -lm
SDL_VIDEODRIVER=offscreen /tmp/render_target_gles_test
```

`task-l-test.sh` (scene backend and existing RHI regression tests):

```bash
#!/bin/bash
set -euo pipefail
. /home/nike/openug2-dev/env.sh
cd /home/nike/openug2-dev/wt-render
out=/home/nike/openug2-dev/reports/render-perf
for mode in gl gles; do
 flags=; libs=-lGL
 if [ "$mode" = gles ]; then flags=-DN2_GLES; libs=-lGLESv2; fi
 cc -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter $flags $(sdl2-config --cflags) -Isrc tools/render_scene_test.c src/rhi_gl.c src/rhi_gl_state.c $(sdl2-config --libs) $libs -lm -o "$out/task-l-$mode"
 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$out/task-l-$mode"
done
for test in rhi_pass rhi_readback rhi_pipeline; do
 sources="tools/${test}_test.c src/rhi_gl.c src/rhi_gl_state.c"
 if [ "$test" = rhi_pass ]; then sources="tools/${test}_test.c src/rhi_gl_state.c"; fi
 cc -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter $(sdl2-config --cflags) -Isrc $sources $(sdl2-config --libs) -lGL -lm -o "$out/task-l-$test"
 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$out/task-l-$test"
done
```

`task-l-vk.sh` (shader generation and Vulkan backend compile):

```bash
#!/bin/bash
set -euo pipefail
. /home/nike/openug2-dev/env.sh
cd /home/nike/openug2-dev/wt-render
out=/home/nike/openug2-dev/reports/render-perf
: > "$out/generated_shaders_vk.h"
for shader in src/shaders/vk/*.vert src/shaders/vk/*.frag; do
 symbol=vk_$(basename "$shader" | tr '.' '_')
 glslangValidator -V --target-env vulkan1.1 --vn "$symbol" -o "$out/task-l-shader.tmp" "$shader" > /dev/null
 cat "$out/task-l-shader.tmp" >> "$out/generated_shaders_vk.h"
done
cc -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers $(sdl2-config --cflags) -Isrc -I"$out" -c src/rhi_vk.c -o "$out/task-l-vk.o"
```

**[observed] Verification:** GL/GLES builds, all eight native pixel comparisons, all sixteen scaled captures and the three new Make targets passed on the scratch build. The existing CPU gate has108 passing targets plus a default LeakSanitizer failure in sfx-game-check; its functional/ASan/UBSan rerun passes with leak detection disabled. All four required Xvfb launcher runs FAIL because this sandbox cannot create X11 listening sockets. Rerun those four targets with actual Xvfb support; do not treat offscreen fallback assertion output as an Xvfb PASS. Full evidence is in RENDER-PERF.md. The menu script screenshot must be taken after the front-end draw; `--shot` alone captures before that draw. Compare same scene, command, inputs and frozen frame window for performance. The headless GL context was reported as llvmpipe, so its timings do not establish hardware GPU or iPhone performance.

## Optional iOS owner decision

**[inferred]** Once device measurements show a worthwhile scene-scale setting and visual acceptance, the iOS owner may reconsider the existing drawable backing cap so that UI can benefit from full drawable resolution while 3D is scaled separately. This requires an explicit iOS-side integration and device validation of memory, thermal behavior, frame pacing, legibility and visual effects. No iOS backing change or iPhone result is claimed here; leave the backing policy and default scale unchanged until that evidence exists.


## Recommended iOS device-trial policy (owner only)

[inferred] Trial render_scale=0.75 on iOS, retaining the host's default1.0 elsewhere. Do not change
this until device measurements are available if a production rollout must be performance-proven.
The scene has56.25% of native pixels at0.75; HUD remains sharp only if drawable backing is native.
In ios/platform_ios.c, n2_ios_apply_render_scale currently lowers contentScaleFactor to meet a pixel
budget. The iOS owner should replace that function's budget calculation with native screen.scale
(and retain its layout/update handling), then use the host setting for scene scaling. This is a
separate owner change; no device performance/memory validation or iOS source edit is claimed.
At direct scale1 startup the diagnostic scene=0x0 means no offscreen allocation, not a zero-sized
physical framebuffer; drawable dimensions in the same line are the actual render dimensions.

The following optional two edits are exact owner trial changes, separate from the scratch-tested
host patch above. [inferred] They retain SDL's native high-DPI backing and choose scene0.75 for
the actual Apple app. They are NOT RUN on an iPhone and must be device-validated before rollout.
In src/platform_ios.h, replace its N2_IOS_AFTER_GL macro (base line34):

```c
#define N2_IOS_AFTER_GL(win) ((void)(win))
```

In main.c, replace only the new float initialization from the required patch:

```c
#if defined(N2_IOS) && defined(__APPLE__)
    float render_scale=0.75f;
#else
    float render_scale=1.0f;
#endif
```

The CLI still overrides the default. Keeping SDL_WINDOW_ALLOW_HIGHDPI and bypassing the
post-GL backing cap is the proposed native-UI policy; the old cap helper need not be deleted.
Do not combine native backing with an unreviewed production default1 on previously capped devices.
