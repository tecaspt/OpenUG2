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
