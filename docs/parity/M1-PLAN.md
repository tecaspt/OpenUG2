# Milestone 1 design contract (retail Quick Race loop)

Written 2026-11-14 by a read-only design pass over branch `ios/main` (d935687). Line numbers refer to that commit.
Signatures in section (c) are frozen for the M1 workers. Status of each unit: see [ROADMAP.md](ROADMAP.md).

## OpenUG2 play plan: findings, milestones, M1 contract (scope: retail parity)

I worked read-only on wt-ios. Experiments ran in a scratch copy with a verification-only key, finger and capture harness patched into main.c (not for commit). That copy, the run scripts and logs are in (local scratch) (`tree/`, `run1.sh`–`run7.sh`, `run*/log.txt` with PNGs, `labels.tsv` (Labels.bin names joined to English.bin text), `probe/carousel_probe.c`).

**What I ran:**
- PASS: `make menu`, `make ios-host`, `make ugui-demo` in the copy.
- Seven headless offscreen runs (V3D EGL), described below.
- The ugui_demo FNG captures and a carousel-binding probe.
- **NOT RUN:** the full test suite, `race_events_check`, `ios/tools/check_ios.sh`, a real iOS device.

### Headline findings
1. **The retail front end is not blocked.**
   - The production pause-menu carousel binding (`src/gui_view.c:210-249`, `gv_carousel_bind`) binds the retail `UI_Main.fng`, `UI_QRModeSelect.fng`, `UI_OptionsMain.fng`, `UI_StartCareer.fng`, `UI_CareerCrib.fng` and `UI_ProfileManager.fng` unchanged (BOUND, 9–10 slots; `probe/carousel_probe.c`).
   - `UI_QRTrackOptions.fng` and `UI_QRModeOptions.fng` carry `TITLE_MASTER`/`DATA_MASTER`, the option-list engine of `UI_Options`.
   - `UI_QRTrackSelect`, `UI_QRCarSelect` and the post-race screens are custom layouts. They need new bindings, which is research work, not blocked.
2. **The retail language table never loads.**
   - `src/ugui/ugui_strtab.c:70` requires header size `0x20`. The PC `LANGUAGES/English.bin` has `0x10` (count 3925, tab 6164, area 37564, code-map length at p+16).
   - Result: `0 strings`; every retail caption is a built-in fallback.
   - A prototype decode of that layout gave 3920 non-empty strings. They include `UI_MAIN_CAREER`="Career", the `QR_*` mode and option labels (Laps, Opponents, Difficulty, Traffic Level, Catch Up, Lap KO), `POST_RACE_*` titles, and `TRACK_NAME_<event>` for 102 of 105 events (no name for 4341, 4708, 4709).
3. **Starting, stopping and restarting events in-process works today**, but only through debug-request fields that nothing in a plain build sets (`src/main.c:11037-11070`).
   - Verified in a plain build: free roam → arm event 4083 (L4RA) → stop → switch to L4RB → 4201 drag → finish (engine blown) → restart from the finished race → stop back to free roam (`run2`, `run3`, `run6`).
   - Car switch works for all 44 car folders (`run4`).
   - Track switch works for L4RA, L4RB, L4RC, L4RD, L4RF and L4RG; it fails for L4RH and L4RR (`run5`).
   - **No process restart or top-level relaunch loop is needed.** A relaunch loop is not advised: main() has many function-static and global states, and SDL re-init on iOS is fragile.
4. **iOS touch conflict.** Any two simultaneous fingers open the pause menu (`ios/platform_ios.c:46-48`, `src/main.c:6685`; reproduced on the GLES ios-host build, `run7`). Two-thumb driving is impossible until this is gated.
5. **Retail post-race and in-game screens exist in the data:** `UI_postrace.fng`, `Results.fng`, `RaceConfirm.fng`, `UI_PostRaceWorldMap.fng`, `UI_SponsorPopup.fng` (InGameRace/Drag/Drift.bun); `UI_EngageRaceDialog.fng`, `UI_EngageShopDialog.fng`, `HUD_World_Loading.fng` (InGameCommon); `PC_Loading.fng`, `Loading_Tips.fng` (GLOBALB). The in-game asset loader only reads InGameCommon (`src/ugui/ugui_assets.c:219-222`).
6. **No music in this install.** It has 176 `.abk`, 84 `.gin` and no `.mus`/`.asf`/`.mpf` streams, so EA Trax has no data here. MOVIES are VP6 and there is no decoder.

### (a) Current state of the play path

| piece | status | evidence |
|---|---|---|
| Bare launch | free roam, no menu | `main.c:6440-6443` race_state=1; README |
| Retail FE (UI_Main, QR) | MISSING in game | only `tools/ugui_demo_orig.c` (viewer); probe binds |
| Language strings | BROKEN (0 loaded) | `ugui_strtab.c:70`; ugui_demo: "bad header size" |
| `make menu` front end | PARTIAL, not in iOS | `main.c:6452-6466`. FREE ROAM works (`run3`). RACE SELECT leaves an invisible state (overlay-only draw, `main.c:10955-10960`). Its car/track keys do nothing: `selcar`/`seltrack` are updated before the request (`main.c:6896-6900`), so `requested != selcar` (`:7055`, `:7158`) is false (`run1`, `run6`). Enter starts a legacy Paths circuit, not a retail event (`:6919-6977`; `run1` ract=0). The first confirm after the title is swallowed (`frontend/frontend.c:66-70`) |
| Event start/stop in-process | WORKS, no production caller | `main.c:11037-11070`; prep/reject `:7699-7745`; event scenery rebuilt in countdown `:7385-7400` |
| Track switch | WORKS for 6 regions; FAILS L4RH, L4RR | `main.c:7154-7276`; `run5` |
| Car switch | WORKS (44 folders; 29 player cars have non-empty TEXTURES.BIN) | `main.c:7051-7152`, `:5333-5351`; `run4` |
| Race end | TERMINAL (race_state 2 forever, HUD banner only) | `main.c:8955-8986`, `hud_race.c:96-97`; `run3` rs=2 for 650+ frames |
| Pause | retail art WORKS; context always free roam | `main.c:2280-2282`. Quit exits the process (`:7005`); RESTART/EXPLORE are never offered (would just resume, `:7006-7013`) |
| Resident build fails during countdown | exits the app | `main.c:7476` |
| iOS input | pad/keys OK; no touch overlay; two-finger pause; touch-emulated taps set the GPS destination | `main.c:6777-6795` |
| iOS host check | runs headless GLES2 | `make ios-host` + fake HOME (`run7`) |
| Catalogue | 105 events / 6 regions; 104 with a mode; 4341 has no mode | drive-all.txt; `tools/race_events_check.c:160-163` (`world_load_events` over 6 regions) |

### (b) Milestones and parity roadmap (dependency order)

The survey statuses come from a delegated read-only code survey. I spot-checked the pause context, race-end, two-finger pause, minimap window (`hud.c:730`; `map_window_world` is never set in main.c) and money (`main.c:11166`) items; all confirmed.

- **M0, string table** (prerequisite; tiny). Accept the PC `0x10` header layout. Unlocks every retail caption plus `TRACK_NAME_*` and `QR_*`.
  - Verify: `make ugui-test`; `build/ugui_demo --data $D --list` reports ≥3900 strings.
- **M1, retail Quick Race loop.** UI_Main → UI_QRModeSelect (Circuit, Sprint, Drag, Drift, Street X, URL, Free Run) → UI_QRTrackSelect → UI_QRTrackOptions/ModeOptions (only Laps and Difficulty enabled) → UI_QRCarSelect → PC_Loading → race → retail pause with race context (Resume Race, Restart, Options, Quit to Main Menu) → UI_postrace/Results → back to QR or Main. Free Run → free roam.
  - Plus an iOS touch overlay. This is a platform adaptation, not retail, and needs your sign-off (question 5).
  - Verify headless: scripted runs, below.
- **M2, FE options/profile/controls.** UI_OptionsMain in the front-end context. `OM_CTX_FRONTEND` exists (`options_menu.h:62`) but is unused; the Audio and Player pages are missing (`options_menu.c:515`). UI_ProfileManager on top of the profile container (`settings.c:57-279`, only used for pad bindings).
- **M3, boot/attract.** EA logo, THX, intro FMV (needs a new VP6 decoder for MOVIES/*.vp6), UG_LS_Splash, LS_LangSelect, Loading_Tips. MISSING.
- **M4, FE 3D scenes.** Showroom/platforms (`FRONTEND/PLATFORMS/*.BIN`, ENVMAPS) behind Main and Car Select; car rotate. MISSING.
- **M5, Customize Car and tuning.**
  - Retail screens UI_ChooseCustomizeCategory, the parts/rims/paint/vinyl/neon browsers, UI_BuyPerformanceParts, dyno.
  - Engine side today: kits/rims PARTIAL (K/F6 keys; debug panel). Paint, vinyl and neon are debug-only. Spinners, hydraulics, ICE and dyno are MISSING.
  - Performance: power and transmission are debug-only. Tyres, brakes, suspension, weight and NOS are undecoded.
- **M6, career core.**
  - New/load career (UI_StartCareer, UI_CareerSelect).
  - Race entry markers via UI_EngageRaceDialog. MISSING: Enter Race only triggers outruns (`main.c:6831`).
  - SMS (action never read), money (`have_money=0`), reputation, stages/unlocks, sponsors (UI_RewardsSponsor, UI_SponsorPopup), world map (UI_CareerWorldMap, UI_InGame_WorldMap; action never read), shops/crib/car lot (UI_EngageShopDialog), magazines/DVD (UI_Magazine*, UI_Status_DVD), photo ops, hidden races. All MISSING.
  - Career save payload is kept as opaque bytes (`settings.h:127-129`).
  - Outruns: WORKS (`ai.c:961-1003`), but with no reward.
- **M7, race-mode parity.**
  - All six types run with gates, grid, AI and HUD. They are PARTIAL only because they are reachable just by `--event`.
  - Missing: URL tournaments/points; drift equation (provisional, `race.h:22-37`); drag shift-quality feedback; Lap KO; the opponents, traffic and catch-up options; reverse direction; rewards.
  - Laps come from `--laps`, not from data.
- **M8, audio.** Engine WORKS (`audio.c:271-385`). SFX are procedural, not retail (PARTIAL). Music/EA Trax: MISSING, and no data in this install. FE music and speech: MISSING.
- **M9, HUD parity.**
  - WORKS: speedo/tach, position/laps/time, drag and drift HUDs, race map, free-roam GPS arrow.
  - MISSING: minimap zoom with speed (window fixed at 500, `hud.c:730`), reputation.
  - PARTIAL: cash and SMS icon (`--hud-preview` only); NOS gauge (drag or `--no-drivetrain` only); race route arrow (diag flag only).
  - Loading screens: MISSING.
- **M10, replay.** MISSING; the pause Replay entry is always disabled (`pause_entries.c:47`).

### (c) M1 implementation contract

Rules for every unit:
- One owner per file.
- New `src/*.c` files must also be added to Makefile `SRC`, because `ios/tools/check_ios.sh:55-65` checks that list against the `ios/project.yml` glob. Only the integration owner touches the Makefile.
- Tests live in `tools/`.
- No new shaders. 2D drawing reuses the `"frontend"` shader label: GL, GLES and Vulkan variants already exist (`rhi_vk.c:2596`).

**Phase 0** (W0, before everything)
- Owns `src/ugui/ugui_strtab.c` and `tools/ugui_test_assets.inc`.
- Accept `hdr ∈ {0x10, 0x20}`. Code-map length is at `p+hdr`, the map at `p+hdr+4`. The name exists only when `hdr == 0x20`.
- Add a synthetic `0x10` vector.
- Verify: `make ugui-test`; ugui_demo string count ≥3900; pause tests are unchanged.

**Phase 1** (parallel; headers are frozen exactly as below)

- **U1. FE carousel core.** Owns `src/fe_entries.{c,h}`, `src/fe_screen.{c,h}`, `src/fe_menu.{c,h}`, `tools/fe_screen_test.c`.
  ```c
  typedef struct FeEntry { int id; uint32_t icon_key,label_key; const char *icon_name,*label; int disabled; } FeEntry;
  enum { FE_MAIN_CAREER,FE_MAIN_QUICK_RACE,FE_MAIN_GO_ONLINE,FE_MAIN_LAN,FE_MAIN_CUSTOMIZE,FE_MAIN_PROFILE,FE_MAIN_OPTIONS };
  enum { FE_MODE_CIRCUIT,FE_MODE_SPRINT,FE_MODE_DRAG,FE_MODE_DRIFT,FE_MODE_STREET_X,FE_MODE_URL,FE_MODE_FREE_RUN };
  int fe_main_entries(FeEntry out[GC_MAX], unsigned enabled_mask);
  int fe_mode_entries(FeEntry out[GC_MAX]);
  int fe_mode_race_kind(int mode);
  ```
  - Main menu: order and labels from `tools/ugui_demo_orig.c:335-368`. `icon_key = ug_hash_upper("MAIN_ICON_*")`, `label_key = ug_hash_upper("UI_MAIN_*")`.
  - Mode select: `TRACK_ICON_{CIRCUIT,SPRINT,DRAG,DRIFT,SHORT_TRACK,U_RACING_LEA,FREE_RUN}` (all 7 present in FrontB) with `QR_{CIRCUIT,SPRINT,DRAG,DRIFT,STREET_CROSS,URL,FREE_RUN}`. The order is [inferred] until it is observed.
  - `fe_mode_race_kind` returns `N2_RACE_*`, or -1 for Free Run.
  ```c
  typedef enum { FES_IN_LEFT=1,FES_IN_RIGHT,FES_IN_ACCEPT,FES_IN_BACK } FesInput;
  typedef enum { FES_EV_NONE=0,FES_EV_CHOSEN,FES_EV_BACK } FesEvent;
  typedef struct FeCarouselScreen { GuiCarousel car; FeEntry e[GC_MAX]; int n,exiting,reason,chosen; FesEvent pending; } FeCarouselScreen;
  void fes_open(FeCarouselScreen*,const FeEntry*,int n,int focus_id,float rail_w);
  void fes_input(FeCarouselScreen*,FesInput);
  void fes_pointer_press(FeCarouselScreen*,float x,float y);
  void fes_pointer_release(FeCarouselScreen*,float x,float y);
  void fes_exit_complete(FeCarouselScreen*);
  void fes_update(FeCarouselScreen*,float s);
  FesEvent fes_poll(FeCarouselScreen*,int *entry_id);
  const char *fes_caption(const FeCarouselScreen*);
  ```
  - Semantics mirror `pause_screen.c`: Accept or Back starts the exit; the event fires at `fes_exit_complete`.
  - Pointer handling: `gc_arrow_at` and `gc_slot_hit` (`gui_carousel.h:73-74`).
  - Read `rail_w` from the package's `ScrollBar_Backing` size, not from `GC_RAIL_PAUSE`.
  ```c
  typedef enum { FEM_REQ_NONE=0,FEM_REQ_QUICK_RACE,FEM_REQ_FREE_RUN,FEM_REQ_QUIT } FemReqKind;
  typedef struct FemRequest { FemReqKind kind; int event_id; char car[32],track[16]; int laps,difficulty; } FemRequest;
  typedef enum { FEM_SCR_NONE=0,FEM_SCR_MAIN,FEM_SCR_QR_MODE,FEM_SCR_QR_TRACK,FEM_SCR_QR_OPTIONS,FEM_SCR_QR_CAR,FEM_SCR_LOADING } FemScreen;
  typedef struct FeMenu FeMenu;
  FeMenu *fem_create(const char *dataroot,const struct GameCatalog *cat,int allow_quit);
  void fem_destroy(FeMenu*);
  void fem_open(FeMenu*,FemScreen);
  void fem_close(FeMenu*);
  int fem_is_open(const FeMenu*);
  FemScreen fem_screen(const FeMenu*);
  void fem_set_current(FeMenu*,const char *car,const char *track);
  void fem_set_loading(FeMenu*,int event_id);
  void fem_event(FeMenu*,const SDL_Event*,SDL_Window*);
  FemRequest fem_update(FeMenu*,float seconds);
  void fem_draw(FeMenu*,int w,int h);
  const char *fem_preview_car(const FeMenu*);
  int fem_visual_available(const FeMenu*);
  const char *fem_status(const FeMenu*);
  ```
  - Modelled on `pause_menu.c`: its own `ug_assets_open` (FE), UgGL, canvas; GvScreen + GvCarousel.
  - Fail closed: no native fallback art.
  - U1 implements MAIN, QR_MODE and LOADING (`PC_Loading.fng`) now. QR_TRACK, QR_OPTIONS and QR_CAR plug in from Phase 2 through the internal function table `fe_track_*` / `fe_car_*`.
  - `fem_draw` clears the main target to black first (`rhi_cmd_clear_main`), except in QR_CAR.
  - Without a TTY or keyboard (iOS), taps arrive as SDL emulated mouse events, the same path as `pause_menu_event`.
  - Main entries enabled in M1: Quick Race (Options in M2). The rest are shown disabled (alpha 150) — your call in question 2.

- **U2. Catalogue.** Owns `src/game_catalog.{c,h}`, `tools/game_catalog_check.c`.
  ```c
  #define GC_MAX_EVENTS 128
  typedef struct GcEvent { int id; char region[8]; int kind,circuit,downhill,length_m; uint32_t name_key; } GcEvent;
  typedef struct GcCar { char name[32]; int traffic; } GcCar;
  typedef struct GcRegion { char track[16],stem[8]; int nevents; } GcRegion;
  typedef struct GameCatalog { GcEvent ev[GC_MAX_EVENTS]; int nev; GcCar car[64]; int ncar; GcRegion region[8]; int nregion; } GameCatalog;
  int gcat_build(GameCatalog*,const char *dataroot);
  ```
  - Events: a facade `World` with one region per STREAM bundle, then `world_load_events`. Drop `N2_RACE_UNKNOWN`. Sort by (kind, id). `name_key = ug_hash_upper("TRACK_NAME_<id>")`.
  - Cars: `res_list_cars`; `traffic` = TEXTURES.BIN has size 0.
  - Regions: those with at least one event.
  - Do not name this prefix `gc_`: `gc_*` is taken by `gui_carousel`.
  - Check tool prints `CATALOG events=104 regions=6 cars=44 player=29`, plus kinds Circuit 31, Sprint 23, Drag 13, Drift 17, Street 9, URL 11, plus regions RA 60, RB 9, RC 11, RD 3, RF 8, RG 13.

- **U3. Flow and input script.** Owns `src/game_flow.{c,h}`, `src/input_script.{c,h}`, `tools/game_flow_test.c`, `tools/input_script_test.c`.
  ```c
  typedef struct GfLaunch { int free_run,event_id; char track[16],car[32]; int laps,difficulty; } GfLaunch;
  typedef struct GfHost { const char *track,*car; int race_state,race_active,race_event_id,race_failed,finish_place,cars; double race_seconds; const char *race_error; } GfHost;
  typedef enum { GF_CMD_NONE=0,GF_CMD_SHOW_LOADING,GF_CMD_SWITCH_TRACK,GF_CMD_SWITCH_CAR,GF_CMD_ARM_EVENT,GF_CMD_STOP_RACE,GF_CMD_CLOSE_MENU,GF_CMD_OPEN_MAIN,GF_CMD_OPEN_RESULTS,GF_CMD_ERROR } GfCmdKind;
  typedef struct GfCommand { GfCmdKind kind; char name[32]; int event_id,laps,difficulty; char message[96]; } GfCommand;
  typedef struct GameFlow { int enabled,state; GfLaunch launch,last; float timer; int frames; } GameFlow;
  void gf_init(GameFlow*,int enabled);
  void gf_start(GameFlow*,const GfLaunch*);
  GfCommand gf_step(GameFlow*,const GfHost*,float dt);
  void gf_pause_context(const GameFlow*,int race_active,PauseContext*);
  int gf_pause_action(GameFlow*,FeAction);
  ```
  - Launch sequence, one command per frame: SHOW_LOADING (one frame drawn) → SWITCH_TRACK if different (wait until `h->track` matches; give up after 3 frames → ERROR) → SWITCH_CAR likewise → for an event: ARM_EVENT, then CLOSE_MENU, then wait for `race_active`. If instead race_state is 1 with `!race_active` and a non-empty `race_error`, return to QR_TRACK with ERROR. For Free Run: STOP_RACE if a race is active, then CLOSE_MENU.
  - On race end: race_state==2 for 3.0 s → OPEN_RESULTS.
  - Pause context: `mode = 0` (non-career: retail Resume Race, Restart, Options, Replay, Quit to Main Menu; free roam shows "Return to Free Run", label key `0xC44A6515` present in data); `race = race_active ? 1 : 0`.
  - `gf_pause_action` returns 1 when handled: QUIT → OPEN_MAIN + STOP_RACE; RESTART → re-ARM the last launch; RESUME → 1. Everything else, and every call while disabled, returns 0.
  ```c
  int is_parse(InputScript*,const char *spec_or_at_file,char *err,size_t n);
  int is_events(InputScript*,long frame,SDL_Event *out,int max,int win_w,int win_h);
  const char *is_shot(const InputScript*,long frame);
  int is_quit(const InputScript*,long frame);
  ```
  - Commands are `F:key=NAME`, `F:pad=BUTTON`, `F:click=X:Y` (mouse with `which=SDL_TOUCH_MOUSEID`), `F:fdown=ID:X:Y`, `F:fup=ID`, `F:tdown=BTN` / `F:tup=BTN` (resolved by main through `tc_layout`), `F:shot=PATH`, `F:quit`.
  - Note: pushed keys do not update `SDL_GetKeyboardState`, so scripted driving must go through touch.

- **U4. Touch overlay** (non-retail platform adaptation). Owns `src/touch_controls.{c,h}`, `src/ui_flat.{c,h}`, `tools/touch_controls_test.c`, `src/controls_sdl.{c,h}`.
  - In controls_sdl: add `uint32_t virt_held;` to `CxInput`. `cx_action_down` returns 1 when `action < 32 && (virt_held >> action & 1)`. `cx_init` already memsets the struct, so nothing else changes.
  ```c
  typedef enum { TC_STEER_L,TC_STEER_R,TC_GAS,TC_BRAKE,TC_NOS,TC_HANDBRAKE,TC_CAMERA,TC_PAUSE,TC_SHIFT_UP,TC_SHIFT_DOWN,TC_ENTER_RACE,TC_COUNT } TcButton;
  enum { TC_CTX_DRAG=1,TC_CTX_OUTRUN=2 };
  void tc_init(TouchControls*,int enabled,int start_active,int accept_mouse_touch);
  void tc_set_frame(TouchControls*,int suppressed,unsigned ctx,float aspect);
  uint32_t tc_event(TouchControls*,const SDL_Event*);
  uint32_t tc_held(const TouchControls*);
  int tc_visible(const TouchControls*);
  void tc_layout(const TouchControls*,int btn,float *cx,float *cy,float *r);
  void tc_draw(const TouchControls*,UiFlat*,int w,int h);
  ```
  - `tc_event` returns the press-edge CB bits; `tc_held` the held CB bits.
  - Mapping: GAS→`CB_ACT_THROTTLE`, BRAKE→`CB_ACT_BRAKE`, STEER→`CB_ACT_STEER_LEFT`/`_RIGHT`, NOS, HANDBRAKE.
  - Press edges: CAMERA; STEER (drag lane change); SHIFT_UP/DOWN (drag context only); ENTER_RACE (outrun context only); PAUSE fires on finger *up* inside the button.
  - Show and hide: a finger down shows the overlay. Keyboard down, pad button or hat, or an axis beyond 50% hides it and releases all buttons. Backgrounding or focus loss also releases all buttons.
  - Start state: active when iOS has no joysticks.
  - Layout: in height units h with a side inset of 5% of width; circles; translucent at alpha 0.35 (0.55 pressed).
    - Steering: (inset+0.15, 0.80) and (inset+0.45, 0.80), r 0.13.
    - GAS (W−inset−0.15, 0.80) r 0.14; BRAKE (W−inset−0.45, 0.83) r 0.11; NOS (W−inset−0.15, 0.50) r 0.09; HANDBRAKE (W−inset−0.42, 0.55) r 0.08.
    - CAMERA (inset+0.10, 0.12) r 0.07; PAUSE (W/2, 0.07) r 0.06.
    - SHIFT_UP/DOWN (W−inset−0.15 / −0.35, 0.30) r 0.08; ENTER_RACE (W/2, 0.70) r 0.09.
    - These overlap the HUD minimap and tach corners (`run7` capture) — question 5.
  - UiFlat:
    ```c
    UiFlat *uf_create(void);
    void uf_destroy(UiFlat*);
    void uf_begin(UiFlat*,int w,int h);
    void uf_end(UiFlat*);
    void uf_color(UiFlat*,float r,float g,float b,float a);
    void uf_rect(UiFlat*,float x,float y,float w,float h);
    void uf_circle(UiFlat*,float cx,float cy,float r);
    void uf_triangle(UiFlat*,float,float,float,float,float,float);
    ```
    It uses pixel coordinates, the same param layout as `frontend_draw.c:26-35` (`"FeParams"`), and save/restore of rhi state.

- **R1. Research** (deep-reasoner, read-only; a fast-worker transcribes the output).
  - Produce reviewed identity tables in the `pause_menu_binding.h` format (hash, guid, parent, type, resource) plus the application-message semantics for: `UI_QRTrackSelect` (including what its 40 unresolved images are; the TRACKMAP<id>.BIN preview is a hypothesis), `UI_QRTrackOptions`/`UI_QRModeOptions` rows, `UI_QRCarSelect` (name, stats bars, filter), and `UI_postrace`/`Results` (InGameRace/Drag/Drift).
  - Output files: `docs/cleanroom-settings/SPEC_GUI_FE.md` (with [data]/[inferred]/[observed] tags) and `src/fe_binding.h`.
  - "Blocked" (and so a fallback) is allowed only with this evidence.

**Phase 2** (after R1, parallel)
- U5 owns `src/fe_track.c` and `src/gui_view.{c,h}`: Track Select, plus a generalized list (`gv_list_bind_rows` / `gv_list_sync_rows`, which keeps the pause API intact).
- U6 owns `src/fe_car.c`: Car Select. The preview is the frozen world with an in-process car switch, debounced to 0.3 s.
- U7 owns `src/postrace.{c,h}` and `src/ugui/ugui_assets.c`, which gains an in-game race package set:
  ```c
  PostRace *pr_create(const char*);
  void pr_open(PostRace*,const PostRaceInfo*);
  void pr_event(PostRace*,const SDL_Event*,SDL_Window*);
  int pr_update(PostRace*,float);   /* returns PR_ACT_RESTART, PR_ACT_CONTINUE or PR_ACT_QUIT_MAIN */
  void pr_draw(PostRace*,int,int);
  int pr_is_open(const PostRace*);
  void pr_destroy(PostRace*);
  ```

**Phase 3** (single owner: `src/main.c`, `Makefile`, README, `docs/PLAYING.md`, `ios/README.md`). Line anchors are at d935687.
1. `:63-66` — replace the `OPENUG2_MENU` includes with the new headers.
2. Arg loop near `:2519`/`:2523` — set `car_explicit` / `track_explicit`.
3. New options: `--menu`, `--no-menu`, `--touch` (also sets `SDL_HINT_MOUSE_TOUCH_EVENTS=1`), `--no-touch`, `--input-script SPEC`; add them to the usage text.
4. `fe_enabled = !no_menu && (force_menu || !(shot||sshot||raudit||daudit||want_event_id||track_explicit||car_explicit||explicit_circuit||resident_route_audit||resident_drive_audit||ai_drive_audit||poseshot||follow_roam_ai||pause_shot||instance_audit))`. This keeps every test and CLI path unchanged.
5. `:6609` — `deterministic_sim |= script != NULL`.
6. `:6437-6466` — race_state is always 1. Delete the `OPENUG2_MENU` blocks at `:6439-6466`, `:6681-6683`, `:6760-6773`, `:6818-6826`, `:7023-7044`, `:12211-12216`, `:12314`.
7. Pre-loop: `gcat_build`, `fem_create`, `gf_init`, `tc_init`, `uf_create`, `pr_create`; `fem_open(MAIN)` when `fe_enabled`. `make menu` becomes an alias of the plain build.
8. `:6640` — `pause_frame |= fem_is_open || pr_is_open`.
9. Before `:6676` — call `tc_set_frame`, then push the script's events for this frame.
10. `:6679` — `uint32_t touch_edge = tc_event(&g_touch, &e); pad_edge |= touch_edge;`
11. `:6681` — `pause_allowed = !fem_is_open && !pr_is_open`.
12. `:6685` — `int ios_p = N2_IOS_PAUSE_EVENT(&e);` then `pause_start = (ios_p && !tc_visible(&g_touch)) || (touch_edge >> CB_ACT_PAUSE & 1) || …existing`.
13. `:6693` — when enabled, call `pause_menu_set_context(pause_menu, gf_pause_context(...))` before `pause_menu_open`.
14. After `:6713` — when fem or pr is open, dispatch the event there and `continue`.
15. `:6777` — skip the HUD pick when `e.button.which == SDL_TOUCH_MOUSEID && tc_visible(&g_touch)`.
16. `:7005-7020` — `if (a == FE_ACTION_QUIT && !gf_pause_action(&gflow, a)) running = 0;`. For other actions call `gf_pause_action` and keep the existing resume lines; print the "not supported" message only when it returns 0 and the action is not RESUME.
17. Before `:7051` — collect `fem_update` and `pr_update` requests into `gf_start`, then call `gf_step` and execute the command:
    - SWITCH_TRACK / SWITCH_CAR: zero `vel`/`speed`, `stage1_drive_reset`, then set `pending_track` / `pending_car` to the list index.
    - ARM_EVENT: find the event index by id; set `g_dbg.race_error[0] = 0` and `want_mode = MODE_RACE_EVENT`, `want_event`, `mode_request = 1`, `race_maxlaps_want = laps`, `race_difficulty` (= `g_dbg.race_ai_difficulty`), `race_start_request = 1`. These run at `:11037-11060`; the path is proven in `run2`, `run3` and `run6`.
    - STOP_RACE: `g_dbg.race_stop_request = 1`.
    - OPEN_*: call the fem/pr functions.
18. `:7476` — when enabled, STOP plus an error back to the menu instead of `running = 0`.
19. Before `:7857` — `g_cx.virt_held = tc_held(&g_touch)`.
20. Before `:12221` — call `tc_draw`, `fem_draw`, `pr_draw`; the script capture goes right before `:12224` `rhi_frame_end`.
21. Every 30 frames while a script is active, print one state line:
    `SCRIPT f= screen= rs= ract= ev= trk= car= spd= held=`

**Verify** (the orchestrator runs these, offscreen EGL, under the heavy lock)
- Builds: `make`, `make gles`, `make ios-host`, `make check-gl`, `bash ios/tools/check_ios.sh`, `make nfsu2-vk` (if in the baseline).
- Full `run-tests.sh` suite, plus the new targets: fe-screen-test, game-flow-test, touch-controls-test, input-script-test, ugui-test.
- `build/game_catalog_check $OPENUG2_DATA` matches the counts above.
- Scenario S1: bare launch with `--resolution 1280x720 --input-script '90:shot=s1.png,91:quit'` → log shows `fem: screen MAIN`; the PNG differs from a no-menu frame.
- Scenario S2:
  - Script: navigate to QR, drag mode, event 4201, car MIATA.
  - Expect in the log: `track switched in process: STREAML4RB`, `car switched in process: MIATA`, `race armed: event 4201`.
  - Hold `tdown=GAS` → SCRIPT shows `spd > 0` → race over → results opened → RESTART → re-armed.
  - `tdown`/`tup` PAUSE → `pause: opened`, race context.
  - Quit to Main Menu → `fem: screen MAIN` and the process is still alive.
  - Free Run → free roam; Quit → exit 0.
- S3: the S2 flow on `build/nfsu2-ios-host` with `HOME` faked (`Documents/NFSU2` → data), which exercises GLES2 plus iOS hooks. Also check: two `fdown` events with the overlay visible do NOT open pause; after a `key` they do.
- Invariance:
  - L4RA/L4RB `--shot` PNGs byte-identical to `bin/nfsu2-integration7b`.
  - `--pause-shot` captures: any change must be explained by the M0 strings.
  - `race_events_check TRACKS --drive` byte-identical to drive-all.txt (expected, since `ai.c` and `world.c` are untouched).

**Risks**
- Track switches are synchronous, a multi-second hitch, which is why the loading screen is drawn first.
- FE assets add an unmeasured RSS cost (budget +150 MB).
- Per-glyph and per-draw UI cost on GLES is unmeasured.
- Event 4341, and the L4RH/L4RR regions, must stay out of the catalogue.
- `finish_place` is not reset between races (`run6`).
- The R1 bindings are the schedule risk.

### (d) Open questions only you can answer
1. **Clean-room evidence for front-end behaviour.** The screen order, the entries the game inserts and the post-race actions are not in the data. Can you capture the retail game on your PC (screenshots or video of Main → Quick Race → each screen → results)? Otherwise those parts get marked [inferred] from the manual.
2. **Main-menu entries not built yet** (Career, Customize, Profile, Options; Online/LAN never): show them disabled with retail art, or hide them?
3. **Quick Race cars:** all 29 player cars available, or the retail unlock rules (which depend on a career)?
4. **Free Run region:** retail drives the whole city, but the engine loads one region at a time. Use L4RA (60 events), or offer a region choice (non-retail)?
5. **Touch overlay:** it is non-retail and overlaps the HUD corners. Accept as is, switch the HUD to the 4:3 fit while touch is active, or defer touch if you play with a controller?
6. **Boot:** M1 starts at UI_Main. The attract sequence needs a VP6 decoder (M3). OK?
7. **Music:** the install has no EA Trax streams. Do you have them elsewhere?
8. **Launch behaviour:** a bare launch would open the retail front end; any CLI start option keeps today's direct start, and `--no-menu` restores free roam. OK?
