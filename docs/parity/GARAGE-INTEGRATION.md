# Garage host/build integration

[observed] Base486ce08. The worker edits no host/build files. The same patches are exercised only in
an external scratch source copy for rendered proof. The real host owner applies these changes.

## Visual preview transaction

[inferred] Add `src/garage.c src/fe_garage.c` to SRC and to SCREENS_TEST_SRC. Both new src/*.c files
must appear in SRC for the iOS source-completeness check. No new external library is required.
[inferred] After `fem_set_current(fem,carname,trackname)` at initial FE setup (near6568) and in the
per-frame FE block (near7167), supply the actual host config before fem_update:
```c
GarageConfig garage_current={.car=carcfg};
snprintf(garage_current.rim_brand,sizeof garage_current.rim_brand,"%s",wheel_brands[wheel_brand]);
garage_current.rim_style=wheel_style;
fem_garage_current(fem,carname,&garage_current);
```
[inferred] The menu copies this outside Garage only, so preview changes cannot overwrite its rollback
snapshot. Keep polling requests even when the menu closes: a pending restore must finish.

[inferred] Immediately before the existing body-kit/mod request block near7632 add:
```c
GarageRequest garage_request={0};
int garage_apply=fem_garage_take(fem,&garage_request);
```
Change its guard to:
```c
if(garage_apply || g_dbg.body_kit_request>=0 || g_dbg.mod_request_slot>=0) {
```
After its existing `BodyKitCandidate kit={0};N2CarConfig next=carcfg;` and before `if(part>=0)`:
```c
if(garage_apply) {
    next=garage_request.config.car;requested=next.body_kit;part=-1;
    for(int i=0;i<nkit_ids;i++)if(kit_ids[i]==requested)cursor=i;
} else if(part>=0) {
```
The last `else if` replaces the original `if(part>=0)`; its existing body and kit branch are retained.
[inferred] Change the stopped-car guard in this block to accept unchanged restore/keep requests:
```c
if(memcmp(&next,&carcfg,sizeof next) && fabsf(PHYS_KMH(speed))>1.0f)
    error="Stop the car before changing parts.";
```
[inferred] Retain the complete prepare_body_kit, clearance, GPU/texture transaction and body metrics
commit path. After its existing `printf("body kit: %s\n",g_dbg.body_kit_status)` add:
```c
if(garage_apply)fem_garage_ack(fem,garage_request.serial,!error,error);
```
[inferred] Exactly one host acknowledgement is required for each taken serial. Success means the
complete config is now displayed; failure means the host retained its previous complete scene.
Preview uses0.3 seconds; Keep/Restore force a request without debounce and wait for acknowledgement.
No host acknowledgement means no kept configuration. The host must not separately mutate the same
car while Garage is editing. The existing frozen-world FE path and menu camera are reused.

## Checks

[inferred] Synthetic model command, repeat with -DN2_GLES and with
-fsanitize=address,undefined -fno-omit-frame-pointer:
```sh
cc -g -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Isrc tools/garage_test.c src/garage.c -lm -o "$report/garage-test"
"$report/garage-test"
```
[observed] This environment prevents LeakSanitizer under ptrace. ASan/UBSan checks use
ASAN_OPTIONS=detect_leaks=0; leak scanning is NOT RUN, not PASS.
[inferred] FE flow check:
```sh
cc -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections $(sdl2-config --cflags) -Isrc tools/fe_garage_test.c src/fe_garage.c src/garage.c src/gui_view.c src/gui_carousel.c src/ugui/ugui_scene.c src/ugui/ugui_util.c -Wl,--gc-sections -lm -o "$report/fe-garage-test"
"$report/fe-garage-test"
```
[inferred] CPU installed-data check, no output artifacts in the worktree:
```sh
cc -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections $(sdl2-config --cflags) -Isrc tools/garage_check.c src/game_catalog.c src/world.c src/resource.c src/ugui/ugui_util.c -Wl,--gc-sections $(sdl2-config --libs) -lm -o "$report/garage-check"
"$report/garage-check" "$OPENUG2_DATA"
```
[inferred] All compile/run commands require the owner's memory check, flock and nice envelope. Tests
exit77 without data; they prove CPU geometry/selection assembly, while GPU/texture proof is separate.

[inferred] At the HUD draw call near11731, suppress driving gauges/route cues only while Garage is open:
```c
if(!(fem && fem_screen(fem)==FEM_SCR_GARAGE))hud_draw(hud,&rp,&quad,&hs,W,H);
```
[inferred] Immediately before `RhiFrameInfo frame` in the camera block, provide a fixed front/side
preview, clipped against the existing world. This is a preview adaptation, not a recovered retail
Garage camera or new garage world. It changes no physical pose:
```c
if(fem && fem_screen(fem)==FEM_SCR_GARAGE) {
    float angle=render_player.head+.75f,distance=fmaxf(6.0f,chase*.8f);
    float focus[3]={render_player.pos[0],render_player.pos[1],render_player.pos[2]+1.0f};
    cam[0]=focus[0]+cosf(angle)*distance;cam[1]=focus[1]+sinf(angle)*distance;
    cam[2]=render_player.pos[2]+2.8f;
    world_camera_clip(&scene,(const float (*)[4])world.neighborhood.mbb,focus,cam,.25f);
    for(int a=0;a<3;a++)look[a]=focus[a]-cam[a];
}
```
[inferred] Extend the scripted telemetry screen-name array near12608 with OPTIONS, PROFILE, CAREER,
GARAGE in enum order, and replace its hardcoded scr>6 guard with the actual array element count.
Otherwise correct Garage captures are misleadingly logged as NONE.

## Rim preview transaction (priority 2)

[inferred] Keep the original wheel_brands indices; append these archive brands to that array near6250:
```c
"DAVIN","DONZ","FOXX","GIANELLE","GIOVANNA","KAIZER","OASIS","SPINNER","WELDWHEEL"
```
[inferred] In the Garage/body-kit transaction above, immediately after declaring `error` and
`BodyKitCandidate kit={0};N2CarConfig next=carcfg;`, prepare a detached rim candidate.
The existing `load_rim_style` helper receives empty outputs, so it cannot destroy active rims.
```c
N2Scene garage_rims={0};GpuMesh *garage_rim_gpu=NULL;
            RhiTexture garage_rim_tex={0};int garage_rim_count=0,garage_rim_mode=0;
            unsigned char *garage_rim_data=NULL;long garage_rim_len=0;
            int garage_rim_brand=-1,garage_rim_changed=0;
            if(garage_apply && (strcmp(garage_request.config.rim_brand,wheel_brands[wheel_brand]) ||
                               garage_request.config.rim_style!=wheel_style)) {
                garage_rim_changed=1;
                for(int b=0;b<n_wheel_brands;b++)if(!strcmp(garage_request.config.rim_brand,wheel_brands[b]))garage_rim_brand=b;
                if(garage_rim_brand<0)error="Rim brand is unavailable. Current config kept.";
                else {
                    char path[1024];snprintf(path,sizeof path,"%s/CARS/WHEELS/GEOMETRY_%s.BIN",dataroot,wheel_brands[garage_rim_brand]);
                    garage_rim_data=n2_read_file(path,&garage_rim_len);
                    if(!load_rim_style(garage_rim_data,garage_rim_len,wkeys,nwkeys,garage_request.config.rim_style,
                                      &garage_rims,&garage_rim_gpu,&garage_rim_count,wtdata,wtlen,
                                      &garage_rim_tex,&garage_rim_mode,carWheelR))
                        error="Rim could not be loaded. Current config kept.";
                }
            }
```
[inferred] Keep the complete existing body preparation/clearance/commit block. Immediately before
its `body_kit_release(&kit);`, swap the prepared rims only if the entire transaction succeeded;
otherwise discard them. There are no fallible operations after body commit.
```c
if(!error && garage_rim_changed) {
                free_scene_gpu(wheelgm,nwheelgm);n2_free_scene(&wheellib);
                if(rimtex.id)rhi_texture_destroy(rimtex);
                free(wldata);wldata=garage_rim_data;wllen=garage_rim_len;garage_rim_data=NULL;
                wheellib=garage_rims;memset(&garage_rims,0,sizeof garage_rims);
                wheelgm=garage_rim_gpu;garage_rim_gpu=NULL;nwheelgm=garage_rim_count;
                rimtex=garage_rim_tex;garage_rim_tex=(RhiTexture){0};rimmode=garage_rim_mode;
                wheel_brand=garage_rim_brand;wheel_style=garage_request.config.rim_style;presentation_snap=1;
                printf("garage rims: %s STYLE%02d action=%d\n",wheel_brands[wheel_brand],wheel_style,garage_request.action);
            }
            free(garage_rim_data);free_scene_gpu(garage_rim_gpu,garage_rim_count);n2_free_scene(&garage_rims);
            if(garage_rim_tex.id)rhi_texture_destroy(garage_rim_tex);
```
[inferred] Retain the final fem_garage_ack from priority1. Rim previews, Keep and Back carry the
whole config through the same serial/ack contract. Never acknowledge before both body and rims
are ready. The host must continue polling while a Restore is pending.

## Saved configurations (priority 3, scratch compile pending)

[inferred] `fem_garage_saved(menu,car,&cfg)` reads the selected profile's Garage document.
ST_E_NOTFOUND means stock for this profile/car; every other error blocks loading/launch.
The host normalizes an empty stock rim identity to NFSU STYLE01. The menu saves a Keep itself
only after a matching successful acknowledgement, lazily persisting a new profile first.
No additional host save call is needed. Existing settings/profile bytes keep their format.

[inferred] Apply the following exact host patch after the visual/rim transaction above.
Base486ce08 anchors: prepare_car_switch469; opponent visual caller550; pending_car6606;
FEM_APPLY_PROFILE7181; GfHost7306; active player-car transaction7393. The hunks include
the preceding priority1/2 additions, so their line numbers include those insertions.
A profile change reloads the same car; every player-car switch resolves a fresh per-car config
before committing body/rim/tyre GPU resources. Failed loads keep the current car and cancel
launch. These hooks serve Car Select previews, Quick Race, Free Run and career's existing
shared car-switch path. The real orchestrator still owns production integration/end-to-end gates.

```diff
--- a/src/main.c
+++ b/src/main.c
@@ -467,7 +467,7 @@
 }

 static int prepare_car_switch(CarSwitchCandidate *candidate,
-                              const char *dataroot, const char *name) {
+                              const char *dataroot, const char *name, const N2CarConfig *custom) {
     memset(candidate, 0, sizeof *candidate);
     char path[1024];
     snprintf(path, sizeof path, "%s/CARS/%s/GEOMETRY.BIN", dataroot, name);
@@ -485,11 +485,13 @@
         candidate->nkeys+=n2_tpk_keys(candidate->globaldata,candidate->globaltpk,
                                       candidate->keys+candidate->nkeys,2048-candidate->nkeys);
     candidate->config.body_kit = 0;
+    if (custom) candidate->config = *custom;
     n2_car_skin_use(name, candidate->texdata, candidate->texlen);
     if (!candidate->data || candidate->len <= 0 ||
         n2_load_car_colored(candidate->data, candidate->len, &candidate->scene,
                     candidate->keys, candidate->nkeys, &candidate->config,
                     candidate->prelight) <= 0 ||
+        (custom && !n2_mod_assemble(dataroot,candidate->data,candidate->len,custom,&candidate->scene)) ||
         n2_mod_add_plate(dataroot,candidate->data,candidate->len,&candidate->scene)<0 ||
         !car_body_metrics(&candidate->scene, candidate->bb, candidate->bloom)) {
         car_switch_release(candidate); return 0;
@@ -547,7 +549,7 @@
                                  int nvisual, const char *kind) {
     for(int k=0;k<nvisual && count;k++) {
         const char *name=names[k*count/nvisual];
-        if(!prepare_car_switch(&visual[k],dataroot,name))continue;
+        if(!prepare_car_switch(&visual[k],dataroot,name,NULL))continue;
         visual[k].body_floor=car_body_floor(&visual[k].scene,visual[k].bb[2]);
         if(visual[k].stock_wheel<0)
             wheels[k]=make_wheel(visual[k].profile.wheel_r,
@@ -6609,6 +6611,7 @@
     float menuspin = 0.0f;   /* orbit-camera angle on the menu screen */
     int running = 1, shotframe = 0, final_status = 0;
     int pending_car = -1, pending_track = -1;
+    int garage_reload_pending = fem != NULL, garage_load_failed = 0;
     float nitro_fx = 0.0f;                 /* eased visual/boost state, 0..1 */
     uint32_t t0 = SDL_GetTicks();   /* --shot prints avg ms/frame at exit */
     /* tyre skid marks: a ring buffer of oriented ground SEGMENTS (ax,ay,az,
@@ -7188,6 +7191,7 @@
                     fprintf(stderr, "front end: display re-init requested; renderer reset hook not wired\n");
             }
             if (fe_apply & FEM_APPLY_PROFILE) {
+                garage_reload_pending = 1; garage_load_failed = 0;
                 const StProfile *fp = fem_current_profile(fem);
                 if (fp) {
                     fe_master_gain = fminf(1.0f, fmaxf(0.0f, fp->audio.master));
@@ -7204,6 +7208,11 @@
                 for (int k = 0; k < ncars; k++) if (!strcmp(carlist[k], preview)) idx = k;
                 if (idx >= 0) { vel[0] = vel[1] = 0; speed = 0; stage1_drive_reset(&g_stage1, vel, heading); pending_car = idx; }
                 else fprintf(stderr, "fe: preview car %s unavailable\n", preview);
+            }
+            if ((garage_reload_pending || garage_load_failed) &&
+                (frq.kind == FEM_REQ_QUICK_RACE || frq.kind == FEM_REQ_FREE_RUN || frq.kind == FEM_REQ_CAREER)) {
+                fem_show_error(fem, "Garage configuration is not ready. Select a profile or retry the car.");
+                frq.kind = FEM_REQ_NONE;
             }
             if (frq.kind == FEM_REQ_QUIT) running = 0;
             else if (frq.kind == FEM_REQ_QUICK_RACE || frq.kind == FEM_REQ_FREE_RUN) {
@@ -7312,7 +7321,9 @@
             default: break;
             }
         }
-        if (gflow.enabled) {
+        if (garage_reload_pending && fem && fem_current_profile(fem) && pending_car < 0)
+            pending_car = selcar;
+        if (gflow.enabled && !garage_load_failed && !garage_reload_pending) {
             GfHost gh; memset(&gh, 0, sizeof gh);
             gh.track = trackname; gh.car = carname; gh.race_state = race_state;
             gh.race_active = world.city.race.active;
@@ -7400,14 +7411,40 @@
             sim_reset=1;
             int requested = pending_car;
             pending_car = -1;
-            if (requested >= 0 && requested < ncars && requested != selcar) {
+            if (requested >= 0 && requested < ncars && (requested != selcar || garage_reload_pending || garage_load_failed)) {
                 const char *next_car = carlist[requested];
                 if (fabsf(PHYS_KMH(speed)) > 1.0f) {
                     printf("car switch deferred: stop the car before selecting %s\n", next_car);
                 } else {
-                    CarSwitchCandidate candidate;
+                    CarSwitchCandidate candidate={0};
+                    GarageConfig saved={0};
+                    int saved_rc = fem ? fem_garage_saved(fem,next_car,&saved) : ST_E_NOTFOUND;
+                    if (saved_rc == ST_E_NOTFOUND) {
+                        memset(&saved,0,sizeof saved);
+                        snprintf(saved.rim_brand,sizeof saved.rim_brand,"NFSU"); saved.rim_style=1;
+                    } else if (saved_rc == ST_OK && !saved.rim_brand[0] && saved.rim_style==0) {
+                        snprintf(saved.rim_brand,sizeof saved.rim_brand,"NFSU"); saved.rim_style=1;
+                    }
+                    N2Scene saved_rims={0};GpuMesh *saved_gpu=NULL;int saved_count=0,saved_mode=0;
+                    RhiTexture saved_tex={0};unsigned char *saved_data=NULL;long saved_len=0;
+                    int saved_brand=-1, saved_ready=0;
+                    if (saved_rc==ST_OK || saved_rc==ST_E_NOTFOUND) {
+                        for (int b=0;b<n_wheel_brands;b++) if (!strcmp(saved.rim_brand,wheel_brands[b])) saved_brand=b;
+                        if (saved_brand>=0 && prepare_car_switch(&candidate,dataroot,next_car,&saved.car)) {
+                            char path[1024];
+                            snprintf(path,sizeof path,"%s/CARS/WHEELS/GEOMETRY_%s.BIN",dataroot,wheel_brands[saved_brand]);
+                            saved_data=n2_read_file(path,&saved_len);
+                            saved_ready=load_rim_style(saved_data,saved_len,wkeys,nwkeys,saved.rim_style,
+                                &saved_rims,&saved_gpu,&saved_count,wtdata,wtlen,&saved_tex,&saved_mode,candidate.profile.wheel_r);
+                        }
+                    }
+                    GpuMesh saved_wheel={0};
+                    if (saved_ready && have_wheel) {
+                        saved_wheel=make_wheel(candidate.profile.wheel_r,fmaxf(0.04f,0.5f*candidate.profile.wheel_w));
+                        if (!saved_wheel.vbo.id || !saved_wheel.nbo.id || !saved_wheel.ibo.id) saved_ready=0;
+                    }
                     DrvCar next_stage1;ResCar next_resistance;BrkCar next_brakes;LatCar next_lateral;CtCar next_contacts;BodyCar next_body;
-                    if (!prepare_car_switch(&candidate, dataroot, next_car) ||
+                    if (!saved_ready ||
                         (g_stage1_requested && (!stage1_car_read(candidate.globaldata,candidate.globallen,next_car,&next_stage1) ||
                          !stage2_car_read(candidate.globaldata,candidate.globallen,next_car,&next_resistance) ||
                          !stage3_car_read(candidate.globaldata,candidate.globallen,next_car,&next_brakes) ||
@@ -7415,6 +7452,9 @@
                          !stage5_car_read(candidate.globaldata,candidate.globallen,next_car,&next_contacts) ||
                          !stage5_body_car_read(candidate.globaldata,candidate.globallen,next_car,&next_body)))) {
                         car_switch_release(&candidate);
+                        garage_reload_pending=0; garage_load_failed=1;
+                        gf_init(&gflow,gflow.enabled);
+                        if (fem) fem_show_error(fem,"Garage configuration could not be loaded. Current car kept.");
                         fprintf(stderr, "car switch failed for %s; keeping %s\n",
                                 next_car, carname);
                     } else {
@@ -7464,13 +7504,15 @@
                             rhi_buffer_destroy(wheelmesh.vbo);
                             rhi_buffer_destroy(wheelmesh.nbo);
                             rhi_buffer_destroy(wheelmesh.ibo);
-                            wheelmesh = make_wheel(carprof.wheel_r,
-                                                   fmaxf(0.04f, 0.5f*carprof.wheel_w));
+                            wheelmesh = saved_wheel; saved_wheel=(GpuMesh){0};
                         }
-                        /* Refit the currently selected rim against the new car. */
-                        load_rim_style(wldata, wllen, wkeys, nwkeys, wheel_style,
-                                       &wheellib, &wheelgm, &nwheelgm,
-                                       wtdata, wtlen, &rimtex, &rimmode, carWheelR);
+                        free_scene_gpu(wheelgm,nwheelgm);n2_free_scene(&wheellib);
+                        if(rimtex.id)rhi_texture_destroy(rimtex);
+                        free(wldata);wldata=saved_data;wllen=saved_len;saved_data=NULL;
+                        wheellib=saved_rims;memset(&saved_rims,0,sizeof saved_rims);
+                        wheelgm=saved_gpu;saved_gpu=NULL;nwheelgm=saved_count;
+                        rimtex=saved_tex;saved_tex=(RhiTexture){0};rimmode=saved_mode;
+                        wheel_brand=saved_brand;wheel_style=saved.rim_style;
                         nkit_ids = n2_car_variant_numbers(cdata, clen, 1, kit_ids, 100);
                         if (nkit_ids == 0 || kit_ids[0] != 0) {
                             if (nkit_ids < 100) nkit_ids++;
@@ -7482,6 +7524,7 @@
                            swap starts the new car without that decal. */
                         clear_car_vinyl();
                         carname = next_car; selcar = requested; kit_cursor = 0;
+                        for(int i=0;i<nkit_ids;i++)if(kit_ids[i]==carcfg.body_kit)kit_cursor=i;
                         if(g_stage1_requested) {
                             stage1_drive_init(&g_stage1,&next_stage1,&next_resistance,0);
                             stage3_drive_brakes(&g_stage1,&next_brakes,0,0);
@@ -7491,11 +7534,16 @@
                             tm_car_read(candidate.globaldata,candidate.globallen,next_car,g_tm_width);
                             vel[0]=vel[1]=0;speed=0;sim_reset=1;
                         }
-                        g_dbg.body_kit_current = 0; g_dbg.mod_current = carcfg;
+                        g_dbg.body_kit_current = carcfg.body_kit; g_dbg.mod_current = carcfg;
+                        garage_reload_pending=0;garage_load_failed=0;
+                        printf("garage saved: %s body=%d rim=%s STYLE%02d\n",carname,carcfg.body_kit,wheel_brands[wheel_brand],wheel_style);
                         g_ride_ready = 0;
                         printf("car switched in process: %s (position and world preserved)\n", carname);
                         car_switch_release(&candidate);
                     }
+                    rhi_buffer_destroy(saved_wheel.vbo);rhi_buffer_destroy(saved_wheel.nbo);rhi_buffer_destroy(saved_wheel.ibo);
+                    free(saved_data);free_scene_gpu(saved_gpu,saved_count);n2_free_scene(&saved_rims);
+                    if(saved_tex.id)rhi_texture_destroy(saved_tex);
                 }
             }
         }
```

[observed] All patch anchors were checked against a separate copy of the scratch main.
The resulting C and rendered persistence proof are still pending; the patch is not yet a PASS.
