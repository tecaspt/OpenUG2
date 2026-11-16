# World Map host integration

## Applied host follow-up

[data] The host integration is now applied to the integration tree based on 760f585.
The patches below retain the original task-S scratch proposal for reference; the
production implementation in main.c and Makefile is authoritative.

[data] The destination marker uses the existing HudMapIcon/HMI_GPS contract, so
hud.c and hud.h require no edits. With a solved route, nav_target_x/y carry the
exact zone centre while the bearing still follows the next road node. Without a
route, the host appends the exact GPS icon to the career icon list. Arrival checks
the canonical zone polygon independently of HUD visibility, then clears both the
route and destination. The icon array reserves space for all 288 career icons and
one destination. Scale-target/frame presentation order is preserved.

[data] The explicit --world-map-dev diagnostic enables F8 to move the player to a
revalidated selected destination; it has no effect without that flag and a live
career target. It resets drive state and emits typed arrival diagnostics. It is
for host arrival checks; ordinary selection never teleports. Device checks remain
NOT RUN. Host validation results are recorded in WORLD-MAP.md.

## Original scratch proposal

[data] The following exact patches were generated against base2037712; the real Main/Make/HUD
files were not edited. [inferred] This implements navigation selection only, with no teleport,
race launch or shop engagement. It uses the existing GPS route solver and guidance arrow.
At that original handoff gameplay was NOT RUN. The applied follow-up above and
current results in WORLD-MAP.md supersede that status. Include fe_worldmap.c in every SRC-based build, including iOS.

## Request and lifetime contract

[data] fem_open_world_map(menu,icons,n,playerX,playerY,heading) copies a bounded snapshot. WorldXY
uses host coordinates, heading radians. New enum values are appended; existing values stay stable.
FEM_REQ_SET_DESTINATION returns frq.destination (x,y,kind,id), and Back returns FEM_REQ_CAREER_ROAM.
Event IDs are CdRace.name_hash; shop IDs are CdShop.hash. The host pairs ch_icons with its original
unfiltered metadata enumeration, checks canonical positions and rejects ambiguous hashes. It
never identifies a target by nearest XY or by an index after UI filtering.

[inferred] The host revalidates availability on Accept, then queues the canonical zone centre into
g_dbg.gps_want_x/y +gps_request. A copied identity stays active until arrival inside that exact
polygon, replacement/cancel, race/mode/track/Main/profile transition or loss of availability.
Arrival runs independently of HUD visibility and speed. It does not trigger engagement.
The existing pause Enter/Back/Start/confirmation behavior is reused; free-roam Map is default-off
unless the host explicitly enables it for active career free roam. Garage never maps to World Map.

## Exact Main patch

[inferred] Apply the complete patch below. It includes declarations, pause context, action handling,
request handling, every relevant lifecycle clear and the HUD fields. The fresh-map snapshot's empty
list is valid; a negative result means inconsistent data and resumes rather than presenting guessed
identities. The0.01m position-consistency tolerance is an inferred numeric check, not a retail rule.
Label hashes for Car Lot/Garage are present in the language table; race labels reuse QR keys.
Manual/debug requests are detected before the solver consumes them, so a later untyped request
cannot retain an earlier typed destination icon. Back retains an existing destination.

```diff
--- a/src/main.c
+++ b/src/main.c
@@ -67,6 +67,7 @@
 #include "touch_controls.h"
 #include "ui_flat.h"
 #include "fe_menu.h"
+#include "fe_worldmap.h"
 #include "career_host.h"   /* M6 career */
 #include "postrace.h"
 #include "hud_host_map.h"
@@ -1617,6 +1618,82 @@
 /* M6 career: one career host; a one-shot focus override lets a career return load the city around the
  * pre-race pose instead of the authored focus (consumed by the next prepare_map_switch). */
 static CareerHost g_career;
+
+static int wm_resolve(const CareerHost *ch, int kind, uint32_t id,
+                      FeMapDestination *canonical, int *zone_out)
+{
+    int matches=0, zone=-1;
+    if (!ch || !ch->active || ch->race>=0) return 0;
+    if (kind==FWM_ICON_EVENT) {
+        for (int i=0;i<ch->cd.nrace;i++) {
+            if (ch->cd.race[i].name_hash!=id || !cr_available(&ch->st,&ch->cd,i)) continue;
+            int z=cd_find_zone(&ch->cd,ch->cd.race[i].locator_hash);
+            if (z>=0) { matches++; zone=z; }
+        }
+    } else if (kind==FWM_ICON_SHOP) {
+        for (int i=0;i<ch->cd.nshop;i++) {
+            if (ch->cd.shop[i].hash!=id || !cr_shop_open(&ch->st,&ch->cd,i)) continue;
+            int z=cd_find_zone(&ch->cd,ch->cd.shop[i].zone_hash);
+            if (z>=0) { matches++; zone=z; }
+        }
+    }
+    if (matches!=1) return 0;
+    const CdZone *z=&ch->cd.zone[zone];
+    float x=0, y=0;
+    for (int j=0;j<4;j++) { x+=z->poly[j][0]*0.25f; y+=z->poly[j][1]*0.25f; }
+    if (!isfinite(x) || !isfinite(y)) return 0;
+    if (canonical) *canonical=(FeMapDestination){x,y,kind,id};
+    if (zone_out) *zone_out=zone;
+    return 1;
+}
+
+static uint32_t wm_event_label(int kind)
+{
+    static const char *const keys[]={"", "QR_CIRCUIT", "QR_SPRINT", "QR_DRAG", "QR_DRIFT", "QR_STREET_CROSS", "QR_URL"};
+    return kind>0 && kind<7 ? ug_hash_upper(keys[kind]) : 0;
+}
+
+static int wm_snapshot(const CareerHost *ch, FeMapIcon out[FWM_MAX_ICONS])
+{
+    HudMapIcon visual[FWM_MAX_ICONS];
+    if (!ch || !ch->loaded || !ch->active || ch->race>=0) return -1;
+    int n=ch_icons(ch,visual,FWM_MAX_ICONS), k=0;
+    int idx[CD_MAX_RACES];
+    int nr=cr_list(&ch->st,&ch->cd,idx,CD_MAX_RACES);
+    for (int j=0;j<nr && k<n;j++) {
+        int i=idx[j], z=cd_find_zone(&ch->cd,ch->cd.race[i].locator_hash);
+        if (z<0) continue;
+        if (visual[k].kind!=HMI_EVENT) return -1;
+        out[k]=(FeMapIcon){visual[k].x,visual[k].y,FWM_ICON_EVENT,
+                          visual[k].variant,ch->cd.race[i].name_hash,wm_event_label(ch->cd.race[i].kind)};
+        k++;
+    }
+    for (int i=0;i<ch->cd.nshop && k<n;i++) {
+        if (!cr_shop_open(&ch->st,&ch->cd,i) || cd_find_zone(&ch->cd,ch->cd.shop[i].zone_hash)<0) continue;
+        if (visual[k].kind!=HMI_SHOP) return -1;
+        out[k]=(FeMapIcon){visual[k].x,visual[k].y,FWM_ICON_SHOP,
+                          visual[k].variant,ch->cd.shop[i].hash,
+            ch->cd.shop[i].type==CD_SHOP_CAR_LOT ? 0x7edc9715u :
+            ch->cd.shop[i].type==CD_SHOP_CRIB ? 0x7ce21662u : 0};
+        k++;
+    }
+    if (k!=n) return -1;
+    /* A duplicate hash in one namespace is ambiguous; don't offer either. */
+    for (int i=0;i<n;i++) {
+        FeMapDestination c;
+        if (!wm_resolve(ch,out[i].kind,out[i].id,&c,NULL) ||
+            fabsf(c.x-out[i].x)>.01f || fabsf(c.y-out[i].y)>.01f) return -1;
+    }
+    return n;
+}
+
+static void wm_clear_destination(int *valid,int *owned_request,int *marker)
+{
+    *valid=0; *owned_request=0;
+    if (marker) *marker=0;
+    g_dbg.gps_n=0; g_dbg.gps_request=0;
+}
+
 static int g_focus_override_set;
 static float g_focus_override[2];
 
@@ -6695,6 +6772,8 @@
     tm_init(&g_tm,128);memset(&g_tm_player,0,sizeof g_tm_player);g_tm_frame=0;
     tm_load_textures(dataroot);
     GpsMarker *gps_marker = gps_marker_load(dataroot);   /* M9: 3D GPS arrow; NULL keeps the 2D arrow only */
+    FeMapDestination wm_destination={0};
+    int wm_destination_valid=0, wm_from_pause=0, wm_owned_request=0;
     int gps_marker_on = 0; float gps_marker_bearing = 0.0f;   /* last frame's HUD guidance */
     {   /* streamed vertex/colour buffers; static up-normals and quad indices */
         static float normals[TM_MAX_VERTICES*3];static uint16_t indices[TM_MAX_VERTICES/4*6];
@@ -6930,10 +7009,15 @@
                 if (!pause_menu) pause_menu = pause_menu_build(dataroot, &opt_gfx, &opt_caps, &opt_store, &opt_store_open);
                 if (pause_menu) {
                     pause_menu_set_mouse_look(pause_menu,&gc.settings.mouse_look);
-                    if (fe_enabled) {   /* non-career pause entries (Return to Free Run / Restart / Quit to Main Menu) */
+                    if (fe_enabled || g_career.active) {
                         PauseContext pctx; gf_pause_context(&gflow, world.city.race.active, &pctx);
-                        pause_menu_set_context(pause_menu, &pctx);
+                        if (g_career.active) pctx.mode=PE_MODE_CAREER;
+                        pctx.from_map=0; /* no map-launched race origin is claimed by this flow */
+                        pause_menu_set_context(pause_menu,&pctx);
                     }
+                    pause_menu_set_world_map_enabled(pause_menu,
+                        g_career.active && g_career.race<0 && gflow.state==GF_RUNNING &&
+                        gflow.launch.free_run && !world.city.race.active);
                     pause_menu_open(pause_menu);
                     pause_cursor = SDL_ShowCursor(SDL_QUERY);
                     SDL_ShowCursor(SDL_ENABLE);
@@ -7016,6 +7100,7 @@
                 float ny = 1.0f - (float)e.button.y / (wh > 0 ? (float)wh : 1.0f) * 2.0f;
                 float px = 0, py = 0;
                 if (hud_pick(hud, nx, ny, &px, &py)) {
+                    wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                     g_dbg.gps_want_x = px; g_dbg.gps_want_y = py;
                     g_dbg.gps_request = 1;
                     printf("HUD: destination picked at (%.1f, %.1f)\n", px, py);
@@ -7048,7 +7133,7 @@
                 SDL_Keycode k = e.key.keysym.sym;
                 int kbound = cx_key_bound(&g_cx, (int)e.key.keysym.scancode);   /* developer keys stand back for bindings */
                 if (k == SDLK_g && hud && !e.key.repeat && !kbound) {
-                    g_dbg.gps_n = 0; g_dbg.gps_request = 0;   /* cancel guidance */
+                    wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on); /* cancel guidance */
                     printf("HUD: destination cleared\n");
                 }
                 else if (cx_key_is(&g_cx, (int)e.key.keysym.scancode, CB_ACT_ENTER_RACE) && !e.key.repeat &&
@@ -7234,6 +7319,19 @@
                 fprintf(stderr, "options: lighting, car reflection/shadow and road reflection updated\n");
             }
         }
+        if (pause_action==FE_ACTION_WORLD_MAP && fem && g_career.active && g_career.race<0 &&
+            gflow.state==GF_RUNNING && gflow.launch.free_run && !world.city.race.active) {
+            FeMapIcon icons[FWM_MAX_ICONS];
+            int n=wm_snapshot(&g_career,icons);
+            if (n>=0) {
+                fem_open_world_map(fem,icons,n,carpos[0],carpos[1],heading);
+                wm_from_pause=1;
+                pause_action=FE_ACTION_NONE; /* consume; keep audio paused and cursor visible */
+            } else {
+                fprintf(stderr,"world map: inconsistent career icon snapshot\n");
+                pause_action=FE_ACTION_RESUME;
+            }
+        }
         int gf_handled = pause_action != FE_ACTION_NONE ? gf_pause_action(&gflow, pause_action) : 0;
         if (pause_action == FE_ACTION_QUIT && !gf_handled) running = 0;
         else if (pause_action != FE_ACTION_NONE) {
@@ -7261,6 +7359,26 @@
                 garage_current.rim_style=wheel_style;
                 fem_garage_current(fem,carname,&garage_current);}
             FemRequest frq = fem_update(fem, (float)fmin(frame_seconds, .1));
+            if (wm_from_pause && fem_screen(fem)==FEM_SCR_WORLD_MAP &&
+                (frq.kind==FEM_REQ_CAREER_ROAM || frq.kind==FEM_REQ_SET_DESTINATION)) {
+                if (frq.kind==FEM_REQ_SET_DESTINATION) {
+                    FeMapDestination canonical;
+                    if (isfinite(frq.destination.x) && isfinite(frq.destination.y) &&
+                        wm_resolve(&g_career,frq.destination.kind,frq.destination.id,&canonical,NULL)) {
+                        wm_destination=canonical; wm_destination_valid=1; wm_owned_request=1;
+                        g_dbg.gps_want_x=canonical.x; g_dbg.gps_want_y=canonical.y; g_dbg.gps_request=1;
+                    } else {
+                        wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
+                        fprintf(stderr,"world map: destination no longer available\n");
+                    }
+                }
+                wm_from_pause=0; fem_close(fem);
+                SDL_ShowCursor(pause_cursor);
+                SDL_SetRelativeMouseMode(g_dbg.freecam && race_state!=3 ? SDL_TRUE : SDL_FALSE);
+                if (adev) SDL_PauseAudioDevice(adev,0);
+                sim_reset=1;
+                frq.kind=FEM_REQ_NONE; /* never run lot/crib resume side effects */
+            }
             /* M2: front-end Options/Profile apply requests (consume-once mask). */
             unsigned fe_apply = fem_take_apply(fem);
             if (fe_apply & (FEM_APPLY_GRAPHICS | FEM_APPLY_REINIT)) {
@@ -7274,6 +7392,7 @@
                     fprintf(stderr, "front end: display re-init requested; renderer reset hook not wired\n");
             }
             if (fe_apply & FEM_APPLY_PROFILE) {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                 garage_reload_pending = 1; garage_load_failed = 0;
                 const StProfile *fp = fem_current_profile(fem);
                 if (fp) {
@@ -7330,6 +7449,7 @@
                 }
             }
             else if (frq.kind == FEM_REQ_CAREER) {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                 const StProfile *fp = fem_current_profile(fem);
                 const char *root = fem_profile_root(fem);
                 GfLaunch gl;
@@ -7581,6 +7701,7 @@
                 if (fem) fem_set_loading(fem, gflow.launch.free_run ? -1 : gflow.launch.event_id);
                 break;
             case GF_CMD_SWITCH_TRACK: {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                 int idx = -1;
                 for (int k = 0; k < ntrack; k++) if (!strcmp(tracklist[k], gcmd.name)) idx = k;
                 if (idx < 0) fprintf(stderr, "fe: track %s is not available\n", gcmd.name);
@@ -7593,6 +7714,7 @@
                 else { vel[0] = vel[1] = 0; speed = 0; stage1_drive_reset(&g_stage1, vel, heading); pending_car = idx; }
                 break; }
             case GF_CMD_ARM_EVENT: {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                 int evi = -1;
                 for (int k = 0; k < world.city.nev; k++) if (world.city.ev[k].id == gcmd.event_id) evi = k;
                 g_dbg.race_error[0] = 0;
@@ -7623,6 +7745,7 @@
                 sim_reset = 1;
                 break;
             case GF_CMD_OPEN_MAIN:
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on); wm_from_pause=0;
                 if (g_career.active) {   /* M6: keep the free-roam pose; an unfinished race counts as aborted */
                     ch_suspend(&g_career, carpos[0], carpos[1], carpos[2], heading);
                     printf("career: suspended (stage %d bank %lld)\n", g_career.st.stage, (long long)g_career.st.bank);
@@ -11938,6 +12061,7 @@
         g_dbg.navopen = world.city.mode == MODE_RACE_EVENT ? world.city.navopen : NULL;
         g_dbg.race = &world.city.race;
         if (g_dbg.mode_request) {
+            wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
             g_dbg.mode_request = 0;g_dbg.race_ai_choice=-1;
             world_race_stop(&world);
             world_set_mode(&world, g_dbg.want_mode, g_dbg.want_event);
@@ -11958,6 +12082,7 @@
             int armed=evi>=0 && world_race_start_direction(&world,troot,evi,g_dbg.race_maxlaps_want,
                 race_options.reverse,g_dbg.race_error,sizeof g_dbg.race_error)>0;
             if (armed) {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
                 race_place_on_grid(&world,&scene,carpos,&heading);
                 race_prepare_pending=1;ai_race=1;nai=0;
                 race_state=0;racetimer=0;speed=vel[0]=vel[1]=0;g_ride_ready=0;
@@ -11979,6 +12104,8 @@
         /* GPS: solve a fresh route whenever the panel asks for a destination */
         {   static int gpath[8192];
             if (g_dbg.gps_request) {
+                if (!wm_owned_request) wm_destination_valid=0; /* any manual/debug replacement */
+                wm_owned_request=0;
                 g_dbg.gps_request = 0;
                 int s0 = world_nav_nearest(&world, carpos[0], carpos[1]);
                 int g0 = world_nav_nearest(&world, g_dbg.gps_want_x, g_dbg.gps_want_y);
@@ -11987,6 +12114,16 @@
                 g_dbg.gps_ms = (int)(SDL_GetTicks() - ta);
                 g_dbg.gps_path = gpath; g_dbg.gps_n = gn; g_dbg.gps_dist = gd;
                 printf("GPS route: %d nodes, %.0f m, %d ms\n", gn, gd, g_dbg.gps_ms);
+            }
+        }
+        if (wm_destination_valid) {
+            FeMapDestination canonical; int zone=-1;
+            if (!g_career.active || g_career.race>=0 || world.city.race.active || !gflow.launch.free_run ||
+                !wm_resolve(&g_career,wm_destination.kind,wm_destination.id,&canonical,&zone)) {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
+            } else if (fwm_zone_contains((const float (*)[2])g_career.cd.zone[zone].poly,carpos[0],carpos[1])) {
+                wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
+                printf("world map: destination zone reached\n");
             }
         }
         for (int i = 0; i < 4; i++) g_dbg.navbb[i] = world.city.navbb[i];
@@ -12010,6 +12147,7 @@
 
         if (hud && hud_dest_set) {      /* one-shot, same request a click makes */
             hud_dest_set = 0;
+            wm_clear_destination(&wm_destination_valid,&wm_owned_request,&gps_marker_on);
             g_dbg.gps_want_x = hud_dest[0]; g_dbg.gps_want_y = hud_dest[1];
             g_dbg.gps_request = 1;
         }
@@ -12019,6 +12157,8 @@
            source, so it can draw an empty slot instead of a plausible lie. */
         if (hud_active) {
             HudState hs; memset(&hs, 0, sizeof hs);
+            hs.destination_active=wm_destination_valid;
+            hs.destination_x=wm_destination.x; hs.destination_y=wm_destination.y;
             hs.speed_kmh = PHYS_KMH(speed);
             hs.imperial_units = hud_imperial;
             hs.skin_index = hud_skin;
@@ -12219,7 +12359,7 @@
                     /* reached: clear guidance rather than spinning in place */
                     float ddx = hs.nav_target_x - carpos[0];
                     float ddy = hs.nav_target_y - carpos[1];
-                    if (ddx*ddx + ddy*ddy < 25.0f * 25.0f) {
+                    if (!wm_destination_valid && ddx*ddx + ddy*ddy < 25.0f * 25.0f) {
                         g_dbg.gps_n = 0; hs.nav_active = 0;
                         printf("HUD: destination reached\n");
                     }
@@ -13159,9 +13299,9 @@
             }
             if (sf % 30 == 0) {
                 const char *scr_names[] = {"NONE","MAIN","QR_MODE","QR_TRACK","QR_OPTIONS","QR_CAR","LOADING",
-                                           "OPTIONS","PROFILE","CAREER","GARAGE","CAREER_LOT"};
+                                           "OPTIONS","PROFILE","CAREER","GARAGE","CAREER_LOT","WORLD_MAP"};
                 int scr = fem && fem_is_open(fem) ? (int)fem_screen(fem) : 0;
-                if (scr < 0 || scr > FEM_SCR_CAREER_LOT) scr = 0;
+                if (scr < 0 || scr > FEM_SCR_WORLD_MAP) scr = 0;
                 printf("SCRIPT f=%ld screen=%s rs=%d ract=%d ev=%d trk=%s car=%s spd=%.1f held=0x%x\n", sf,
                        pause_menu_is_open(pause_menu) ? "PAUSE" : scr_names[scr], race_state, world.city.race.active,
                        world.city.race.active && world.city.race.ev >= 0 ? world.city.ev[world.city.race.ev].id : 0,
```

[data] Main already freezes simulation while FE is open and consumes its input before driving;
no new global event branch or pause_frame field is needed. fem_preview_car returns an empty string
on World Map, so no preview/velocity guard is needed. The new request branch runs immediately after
fem_update, before the existing lot/crib handlers, preserving velocity, speed and drivetrain on Back.
It consumes the original pause action before generic resume, keeping audio paused until actual exit.

## Exact minimap destination marker patch

[inferred] This adds a separate exact-target marker that survives a route-solver failure. It uses
the already loaded genuine HMI_GPS sprite; guidance remains conditional on a solved route. No
additional native icon or route polyline is invented. Only the owner edits these HUD files.

```diff
--- a/src/hud.h
+++ b/src/hud.h
@@ -83,6 +83,7 @@
     int   route_next;         /* index of the armed gate, -1 = none */
 
     /* --- navigation --- */
+ int destination_active; float destination_x,destination_y; /* exact World Map target */
     int   nav_active;         /* a destination is set AND a route was solved */
     float nav_target_x, nav_target_y;
     float nav_bearing;        /* radians, world bearing to the next route node */
--- a/src/hud.c
+++ b/src/hud.c
@@ -758,7 +758,8 @@
  for(int i=0;s->map_icons&&i<s->map_icon_n&&i<HA_LIMIT;i++){const HudMapIcon*m=s->map_icons+i;
   double angle=m->kind==HMI_RIVAL?hud_race_icon_angle(h->pick_frame.heading,m->heading):0;
   map_icon(h,m->kind,m->variant,m->x,m->y,angle,ring,width,height);}
- if(s->nav_active)map_icon(h,HMI_GPS,0,s->nav_target_x,s->nav_target_y,0,ring,width,height);
+ if(s->destination_active)map_icon(h,HMI_GPS,0,s->destination_x,s->destination_y,0,ring,width,height);
+ else if(s->nav_active)map_icon(h,HMI_GPS,0,s->nav_target_x,s->nav_target_y,0,ring,width,height);
  int rival_art=s->opponent_heading&&h->icon_node[HMI_RIVAL]>=0&&node_texture(h,h->icon_node[HMI_RIVAL]).id.id;
  for(int i=0;rival_art&&i<s->opponent_n&&i<64&&s->opponent_xy;i++){
   map_icon(h,HMI_RIVAL,1,s->opponent_xy[i*2],s->opponent_xy[i*2+1],hud_race_icon_angle(h->pick_frame.heading,s->opponent_heading[i]),ring,width,height);}
```

## Optional race-return and filters

[data] UI_CareerWorldMap.fng has region arrows and example event details; no events/shops/all
filter rows were established from its static package. Those controls are not implemented.
[inferred] The optional race-return path is not implemented by the patches above: pctx.from_map
stays0. If the owner later tracks a genuine race-from-map origin, expose the existing confirmed
Return to World Map only for that context, call ch_return (which already records an unresolved
race as aborted), gf_start its free-roam launch, and open a fresh snapshot only after GF_RUNNING,
placement/reload completion and no lot/crib/other FE screen. Cancel pending opening on failure,
Main/profile change or a different launch. Do not auto-engage or teleport.

## Build patch and verification

[data] Apply the prepared Makefile patch below against base `2037712`; it adds the module to the normal sources, screen tests and a standalone pure-module target. [observed] These exact patches compile in both external scratch games. The synthetic and existing FE targets, including real-data GL/GLES screen checks, PASS; desktop warnings remain24 and GLES warnings remain30, matching their baselines. At that original handoff live host gameplay and device checks were **NOT RUN**; see the applied follow-up and WORLD-MAP.md for current host results.

```diff
--- a/Makefile
+++ b/Makefile
@@ -61,6 +61,8 @@
 SRC += src/fe_lot.c
 SRC += src/career_data.c src/career.c src/career_host.c   # M6 career
 SRC += src/race_rules.c   # M7 race rules (task F)
+SRC += src/fe_worldmap.c   # Task S
+HDRS += src/fe_worldmap.h
 SRC += src/garage.c src/fe_garage.c   # Garage: Customize hub, kits, rims, saves (task J)
 HDRS += src/hud_race.h src/dir_scan.h
 HDRS += src/game_catalog.h src/game_flow.h src/input_script.h src/touch_controls.h src/ui_flat.h src/fe_entries.h src/fe_screen.h src/fe_menu.h
@@ -966,6 +968,7 @@
 	./build/fe_screen_test
 # M1 task B screens (docs/parity/M1-SCREENS-INTEGRATION.md)
 SCREENS_TEST_SRC = src/garage.c src/fe_garage.c src/fe_menu.c src/fe_lot.c src/race_rules.c src/fe_track.c src/fe_car.c src/postrace.c src/fe_profile.c src/fe_entries.c src/fe_screen.c src/gui_view.c $(GUI_ART_SRC) src/options_menu.c src/settings.c src/controls_store.c src/controls_sdl.c $(UGUI_SRC) src/ugui/ugui_gl.c src/rhi_gl.c src/rhi_gl_state.c
+SCREENS_TEST_SRC += src/fe_worldmap.c src/career_data.c src/pause_menu.c
 fe-track-test: tools/fe_track_test.c src/fe_track.c src/fe_car.c $(HDRS)
 	@mkdir -p build
 	$(CC) -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections $(SDL_CFLAGS) -Isrc tools/fe_track_test.c src/fe_track.c src/fe_car.c src/race_rules.c -lm -Wl,--gc-sections -o build/fe_track_test
@@ -1141,3 +1144,9 @@
 	@mkdir -p build
 	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections $(SDL_CFLAGS) -Isrc tools/garage_check.c src/game_catalog.c src/world.c src/resource.c src/ugui/ugui_util.c -Wl,--gc-sections $(SDL_LIBS) -lGL -lz -lm -o build/garage_check
 	./build/garage_check "$(OPENUG2_DATA)"
+
+.PHONY: fe-worldmap-test
+fe-worldmap-test: tools/fe_worldmap_test.c src/fe_worldmap.c src/fe_worldmap.h
+	@mkdir -p build
+	$(CC) -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -ffunction-sections -fdata-sections $(SDL_CFLAGS) -Isrc tools/fe_worldmap_test.c src/fe_worldmap.c -Wl,--gc-sections -lm -o build/fe_worldmap_test
+	./build/fe_worldmap_test
```

[inferred] After applying the patch in an owner-controlled scratch checkout, run each build/test through the shared memory-checking heavy wrapper (source `/home/nike/openug2-dev/env.sh` first; at most `-j2`; no forced software GL). The test recipe is included in the patch above. Use fresh storage directories for every run.
Example commands, from the scratch checkout:

```bash
. /home/nike/openug2-dev/env.sh
FWM_RUN_DIR=$(mktemp -d /home/nike/openug2-dev/reports/worldmap/owner-check.XXXXXX)
export XDG_CONFIG_HOME="$FWM_RUN_DIR/config" XDG_DATA_HOME="$FWM_RUN_DIR/data"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
unset LIBGL_ALWAYS_SOFTWARE
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh make -j2 fe-worldmap-test
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh make -j2 fe-screen-test fe-polish-test gui-art-test fe-garage-test fe-lot-test pause-menu-test fe-binding-test
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy make -j2 fe-screens-check
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh make -j2 nfsu2
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy make -j2 CFLAGS="-O2 -std=c99 -Wall -Wextra -Wno-unused-function -D_DEFAULT_SOURCE -DN2_GLES $(pkg-config --cflags libpng)" GL_LIBS=-lGLESv2 fe-screens-check
bash /home/nike/openug2-dev/reports/worldmap/run-heavy.sh make -j2 gles
```

[inferred] Verify host gameplay with an available career event and shop, stable identities even for co-located icons, Accept, Back, solver failure, exact marker, entry into the named polygon with HUD disabled, manual replacement/cancel, mode/race/profile transitions, and normal explicit race/shop confirmation. Capture before/after in GL and GLES; verify position, speed, audio and cursor survive map Back. Include unavailable assets and empty snapshot behavior. No device or retail parity result follows from the standalone tests.
