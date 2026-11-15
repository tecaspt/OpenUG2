# M7 host/build integration

[observed] All anchors refer to ea2c9c4. main.c and Makefile are read-only to Task F.

## Priority 1 — opt-in drag longitudinal adapter

[inferred] Next to main.c:1352 (`g_vehicle_source`) add:

```c
static PhysDragConfig g_drag_config;
```

[inferred] Immediately after the initial g_vehicle_source assignment at main.c:5018 add:

```c
g_drag_config = (PhysDragConfig){0};
if (physics_from_global)
    phys_drag_config_read(&g_drag_config, &g_vehicle_source,
        globdata, globlen, carname, 0, 0);
```

[inferred] Immediately after the candidate assignment at main.c:7269 add:

```c
g_drag_config = (PhysDragConfig){0};
if (candidate.has_physics)
    phys_drag_config_read(&g_drag_config, &g_vehicle_source,
        candidate.globaldata, candidate.globallen, next_car, 0, 0);
```

[observed] The raw initial global buffer is freed at main.c:5071; the candidate buffer is temporary.
[inferred] Capture the radius/coefficient at these two locations before freeing buffers. Config's
source pointer borrows the stable global g_vehicle_source, never the temporary candidate/local pa.

[inferred] At GF_CMD_ARM_EVENT, before the evi check at main.c:7179, construct:

```c
PhysDragConfig launch_drag = g_drag_config;
launch_drag.power_level = g_dbg.perf_power_level;
launch_drag.transmission_level = g_dbg.perf_transmission_level;
```

[inferred] After the `evi < 0` error branch, before its existing success `else`, insert:

```c
else if (world.city.ev[evi].info.kind == N2_RACE_DRAG &&
         !phys_drag_config_valid(&launch_drag)) {
    snprintf(g_dbg.race_error, sizeof g_dbg.race_error,
             "Drag wheel/engine/resistance data is unavailable.");
}
```

[inferred] Replace the whole drag_gain block at main.c:8332–8339 with:

```c
PhysVehicle driven = g_vehicle;
```

[inferred] Keep the Stage 1 branch at main.c:8344 unchanged. Between its closing brace and the
existing `else dmag = phys_drive_step(...)` at main.c:8355 add this branch:

```c
} else if (drag_active && race_state == 1) {
    PhysDragConfig drag_config = g_drag_config;
    drag_config.power_level = g_dbg.perf_power_level;
    drag_config.transmission_level = g_dbg.perf_transmission_level;
    dmag = phys_drag_step(&drag_gear, &drag_config, carpos, vel, &heading, &speed,
        throttle, steer_filtered, handbrake, drag_shift,
        &surf_now, &driven, g_ride_ready ? &g_ride : NULL);
```

[inferred] Do not call phys_manual_step again in the host: phys_drag_step advances its shift timer,
RPM, limiter/heat state once per fixed tick. Preserve raw throttle for telemetry. In the existing
drag audio branch at main.c:8389 set the load as follows, so torque cuts remain audible:

```c
g_engine.load = (drag_gear.shift > 0 || drag_gear.failed ||
                 drag_gear.rpm >= g_vehicle_source.limiter_rpm)
    ? 0.0f : fmaxf(0.0f, throttle);
```

[inferred] No new SRC entry is required for priority 1; both new functions are in physics.c. Existing
legacy/ordinary physics APIs remain available. NOS force beyond the existing host behavior is not
implemented by this adapter. The orchestrator must run the full game-after-fix check after wiring.

## Exact new checks

```make
drag-physics-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -Isrc tools/drag_physics_test.c src/physics.c -Wl,--gc-sections -lm -o build/drag_physics_test
	./build/drag_physics_test

drag-speed-check:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -Isrc tools/drag_speed_check.c src/physics.c src/drivetrain.c src/drive_stage1.c src/braking.c src/resistance.c src/lateral.c src/contact.c src/body_response.c src/drive_contact.c -Wl,--gc-sections -lm -o build/drag_speed_check
	./build/drag_speed_check
```

[inferred] Repeat synthetic compilation with -DN2_GLES. The speed check reads OPENUG2_DATA at
runtime (exit 77 if unset); it compares adapters on a synthetic level ground query, with full configured ordinary dynamics,
not world collision or a complete race replay. Run from an external report directory and store output there.
All builds/runs require the owner's memory check, heavy lock and nice priority 10.

## Priority 2 — shift feedback and over-rev

[inferred] Add src/race_rules.c to Makefile SRC. Existing isolated physics/AI test recipes continue
to link without it because canonical grade/heat primitives are inline; new external API tests and
host label lookup link race_rules.c. No SDL/GL or mutable globals are in that module.

[inferred] In the new priority-1 drag branch, immediately before phys_drag_step, optionally supply
stock per-gear centers derived by the existing drivetrain curve solver:

```c
drag_gear.shift_point_rpm = 0.0f;
if (g_stage1.ready && g_dbg.perf_power_level == 0 &&
    g_dbg.perf_transmission_level == 0 && drag_gear.gear >= 1 &&
    drag_gear.gear <= g_stage1.drivetrain.car.gear_count)
    drag_gear.shift_point_rpm = g_stage1.drivetrain.car.up_rpm[drag_gear.gear + 1];
```

[inferred] If the hint is missing/invalid, grading uses g_vehicle_source.redline_rpm. Refresh the
hint before each tick, so it belongs to the pre-shift gear. It does not request an automatic shift.

[inferred] Add these presentation fields to HudState near its current shift_ready/engine_failed:

```c
int shift_quality;                 /* RR_SHIFT_* */
uint32_t shift_quality_key;         /* installed language key, 0 means hide */
float shift_quality_seconds;       /* remaining simulation-time feedback */
```

[inferred] In the existing drag HUD feed at main.c:11388–11391 append:

```c
hs.shift_quality = drag_gear.shift_quality;
hs.shift_quality_seconds = drag_gear.feedback_seconds;
hs.shift_quality_key = drag_gear.feedback_seconds > 0
    ? rr_drag_grade_label(drag_gear.shift_quality) : 0;
```

[inferred] The HUD owner binds the nonzero key through the loaded language table and hides feedback
at zero; do not hardcode English replacement captions. Existing rpm/redline, heat and engine_failed
feeds stay intact. phys_manual_step owns the heat clock/failure latch, and phys_drag_step calls it
once per tick; do not integrate heat again in main. Existing GF failure handling already observes
race_drag_failure(drag_gear.failed, ...). Reset/restart already zero-initializes PhysManual.
[observed] Baseline HUD only exposes heat/readiness and an engine-blown message. Rendering new
grading captions requires the HUD owner; no such rendered result is claimed by Task F.

```make
race-drag-rules-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -Isrc tools/race_drag_rules_test.c src/race_rules.c src/physics.c -Wl,--gc-sections -lm -o build/race_drag_rules_test
	./build/race_drag_rules_test
```

[inferred] Repeat the recipe with -DN2_GLES. The new kernel and test are synthetic/data-free.

## Priority 3 — drift scoring/results

[observed] The existing calls at main.c:9063 and ai.c's race_drift_step call already supply final
contact-corrected poses, ground/collision flags and travel. world.c already banks at finish. No
new physics or world clock hook is needed; race.h keeps the existing wrapper API.

[inferred] Replace the body of the finished-race block at main.c:9145–9156 with:

```c
if (world.city.race.kind == N2_RACE_DRIFT) {
    float totals[RR_MAX_CARS] = {0};
    race_drift_bank(&world.city.race.drift);
    totals[0] = world.city.race.drift.bank;
    for (int k = 0; k < nai; ++k) {
        race_drift_bank(&race_drivers[k].progress.drift);
        totals[k + 1] = race_drivers[k].progress.drift.bank;
    }
    int rank = rr_points_place(totals, nai + 1, 0);
    finish_place = rank > 0 ? rank : nai + 1;
    if (!rank) world.city.race.failed = RR_FAIL_INVALID_RULES;
} else {
    finish_place = 1;
    for (int k = 0; k < nai; ++k)
        if (race_drivers[k].progress.finished) ++finish_place;
}
race_state = 2;
```

[inferred] Keep the enclosing `if (ai_race && race_state == 1 && ...finished)` guard. RR_MAX_CARS is
six, matching the current five-opponent limit; reject future larger fields before filling totals.
The snapshot banks all active chains exactly once; ties share points rank.

[inferred] At GF_CMD_OPEN_RESULTS after info.kind at main.c:7204 add:

```c
if (info.kind == N2_RACE_DRIFT) {
    info.have_drift_points = 1;
    info.drift_points = (uint32_t)fminf(RR_SCORE_LIMIT,
        fmaxf(0.0f, world.city.race.drift.bank));
}
```

[inferred] Existing HudState bank/chain/angle feeds remain valid. Add `float drift_multiplier` and
populate it in the drift feed at main.c:11385:

```c
hs.drift_multiplier = fmaxf(1.0f, world.city.race.drift.multiplier);
```

[inferred] HUD should show banked points, pending chain and ×1…×5. The HUD owner supplies its authored
field binding. The results Points caption and player score are already handled by postrace.c.
No bonus-zone geometry or guessed edge-of-road proximity bonus is generated by this unit.

```make
race-drift-rules-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Isrc tools/race_drift_rules_test.c src/race_rules.c -lm -o build/race_drift_rules_test
	./build/race_drift_rules_test

postrace-rules-check:
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc tools/postrace_rules_check.c $(SCREENS_TEST_SRC) src/race_rules.c -o build/postrace_rules_check $(SDL_LIBS) $(GL_LIBS) $(UGUI_IMAGE_LIBS) $(SETTINGS_LIBS) -lm
	SDL_VIDEODRIVER=offscreen ./build/postrace_rules_check "$(M7_REPORT_DIR)"
```

[inferred] OPENUG2_DATA is read at runtime; unset returns 77. M7_REPORT_DIR must exist outside the
worktree. Repeat synthetic compile with -DN2_GLES; force GL_LIBS=-lGLESv2 for GLES runtime builds.

## Priority 4 — knockout and URL coordination

[inferred] Add these locals beside race_drivers at main.c:5508 (all live for the main-loop lifetime):

```c
RrKnockout knockout = {0}; int knockout_live = 0, race_lap_knockout = 0;
RrUrl url_series = {0}; RrRound url_round = {0}; int url_live = 0;
int url_pending[RR_MAX_URL_RACES], url_pending_count = 0;
```

[inferred] Priority 5 supplies race_lap_knockout from the FE; default zero keeps old launches intact.
Add these helpers above main, after the existing catalogue/game-flow declarations:

```c
static int m7_url_events(const GameCatalog *cat, int first, int out[RR_MAX_URL_RACES]) {
    int ids[GC_MAX_EVENTS], n = 0;
    for (int k = 0; k < cat->nev; ++k)
        if (cat->ev[k].kind == N2_RACE_URL) ids[n++] = cat->ev[k].id;
    return rr_url_select_events(ids, n, first, RR_DEFAULT_URL_ROUNDS, out);
}
static int m7_next_url(GameFlow *flow, const GameCatalog *cat, const RrUrl *series) {
    int id = rr_url_next_event(series); if (!id) return 0;
    GfLaunch next = flow->last; next.event_id = id; next.free_run = 0;
    for (int e = 0; e < cat->nev; ++e) if (cat->ev[e].id == id) {
        for (int r = 0; r < cat->nregion; ++r)
            if (!strcmp(cat->ev[e].region, cat->region[r].stem)) {
                snprintf(next.track, sizeof next.track, "%s", cat->region[r].track);
                gf_start(flow, &next); return 1;
            }
    }
    return -1;
}
```

[inferred] At the fresh FEM_REQ launch branch main.c:7128, before gf_start, clear url_live and
url_pending_count, then inspect the selected catalog event. For URL call
`url_pending_count = m7_url_events(&g_catalog, frq.event_id, url_pending);` and reject a zero result
through fem_show_error. Free Run/non-URL leave count zero. Do not rebuild this sequence on Restart.

[inferred] In the successful prepared-race branch after ai_race_prepare's rejection checks
(main.c:7923), initialize/reset the per-round state before countdown:

```c
knockout_live = race_lap_knockout && event->info.kind == N2_RACE_CIRCUIT;
if (knockout_live) {
    rr_knockout_init(&knockout, nai + 1);
    world.city.race.maxlaps = nai > 0 ? nai : 1;
    for (int k = 0; k < nai; ++k) race_drivers[k].progress.maxlaps = world.city.race.maxlaps;
}
if (event->info.kind == N2_RACE_URL) {
    if (url_pending_count) {
        url_live = rr_url_init(&url_series, nai + 1, url_pending, url_pending_count, NULL);
        url_pending_count = 0;
    }
    if (url_live && url_series.count != nai + 1) {
        snprintf(g_dbg.race_error, sizeof g_dbg.race_error, "URL field size changed.");
        url_live = 0; /* reject through the existing failed-prepare path */
    }
    if (url_live) rr_round_init(&url_round, nai + 1);
} else url_live = 0;
```

[inferred] A field-size mismatch must abort preparation using the same world_race_stop/free-course/
reopen-QR error path immediately above; do not start a resized tournament. Entrant0 is the player;
race_drivers[k] uses ID k+1. The elimination update belongs after all gates/contacts, immediately
before the finished-race block at main.c:9145:

```c
if (knockout_live && race_state == 1) {
    float progress[RR_MAX_CARS] = {0};
    const WRace *pr = &world.city.race;
    int lap = pr->finished ? pr->maxlaps : pr->lap > 0 ? pr->lap - 1 : 0;
    progress[0] = world_race_progress_value(pr, carpos[0], carpos[1]);
    for (int k = 0; k < nai; ++k) {
        const WRace *r = &race_drivers[k].progress;
        progress[k + 1] = world_race_progress_value(r, ais[k].pos[0], ais[k].pos[1]);
        if (!race_drivers[k].eliminated) {
            int completed = r->finished ? r->maxlaps : r->lap > 0 ? r->lap - 1 : 0;
            if (completed > lap) lap = completed;
        }
    }
    int gone = rr_knockout_lap(&knockout, lap, progress);
    if (gone > 0) ai_race_eliminate(ais + gone - 1, race_drivers + gone - 1);
    if (!knockout.alive[0] || rr_knockout_winner(&knockout) >= 0) {
        finish_place = knockout.place[0];
        world.city.race.failed = knockout.alive[0] ? 0 : RR_FAIL_KNOCKOUT;
        world.city.race.finished = 1; race_state = 2;
    }
}
```

[inferred] Replace every main.c ai_actor_present call (e.g.7996,8079,9092,9109,9183,9225,9246 and
minimap/draw feeds) with ai_actor_present_rules using the same four arguments plus race_drivers.
The old API remains for other callers. In the contacts pointer-list loop at main.c:9032 use:

```c
if ((ai_race ? !race_drivers[k].eliminated : traffic_routes[k].present))
    contacts[count++] = &ais[k];
```

[inferred] This removes eliminated cars from collisions, render/minimap/presentation and AI intent.
Keep nai fixed (initial entrants), so stable IDs/results places are not compacted or renumbered.

[inferred] For URL, let the field continue after the player finishes. Change the loop condition at
main.c:8984 and the collision-block guard at9024 to:

```c
race_state == 1 || (url_live && !url_round.ready)
```

[inferred] In the contacts list, include the player only while
`!follow_roam_ai && !(url_live && url_round.done[0])`; finished players do not block remaining cars.
After all actor progress updates, once per fixed tick while URL is live and race_state is 1 or 2:

```c
if (url_live && (race_state == 1 || race_state == 2)) {
    int finished[RR_MAX_CARS] = {0}, failed[RR_MAX_CARS] = {0};
    float progress[RR_MAX_CARS] = {0};
    finished[0] = world.city.race.finished; failed[0] = world.city.race.failed != 0;
    progress[0] = world_race_progress_value(&world.city.race, carpos[0], carpos[1]);
    for (int k = 0; k < nai; ++k) {
        finished[k + 1] = race_drivers[k].progress.finished;
        failed[k + 1] = race_drivers[k].progress.failed ||
            (race_drivers[k].drive.failed && !finished[k + 1]);
        progress[k + 1] = world_race_progress_value(&race_drivers[k].progress,
            ais[k].pos[0], ais[k].pos[1]);
    }
    if (rr_round_step(&url_round, finished, failed, progress, 1.0 / PHYS_TICKRATE) < 0) {
        url_live = 0; world.city.race.failed = RR_FAIL_INVALID_RULES;
    }
}
```

[inferred] At main.c:7150, after filling GfHost, delay the existing results transition while the
field collector is pending: `if (url_live && race_state == 2 && !url_round.ready) {
 gh.race_state = 1; gh.race_failed = 0; }`. Pause performs no fixed ticks, so the collector freezes.
At GF_CMD_OPEN_RESULTS before pr_open, record only the ready round and fill supplied standings:

```c
if (url_live) {
    int places[RR_MAX_CARS] = {0};
    if (!rr_round_places(&url_round, places) ||
        !rr_url_record(&url_series, info.event_id, places)) {
        url_live = 0; info.failed = RR_FAIL_INVALID_RULES;
    } else {
        info.have_url_standings = 1; info.standing_count = url_series.count;
        info.url_round = url_series.completed; info.url_rounds = url_series.races;
        info.finish_place = rr_url_place(&url_series, 0);
        for (int k = 0; k < url_series.count; ++k) {
            if (!k) snprintf(info.standing[k].name, sizeof info.standing[k].name, "Player");
            else snprintf(info.standing[k].name, sizeof info.standing[k].name, "Racer %d", k);
            if (!k) snprintf(info.standing[k].car, sizeof info.standing[k].car, "%s", carname);
            info.standing[k].points = url_series.points[k];
            info.standing[k].place = rr_url_place(&url_series, k);
        }
    }
}
```

[inferred] Player/Racer captions are generated stable identifiers until real names are supplied;
use the profile name/localized Player when available. Unknown NPC car names stay empty/hidden.
At post-result actions main.c:7142–7144:

```c
case PR_ACT_RESTART:
    if (url_live) rr_url_undo_last(&url_series);
    gf_pause_action(&gflow, FE_ACTION_RESTART); break;
case PR_ACT_QUIT_MAIN:
    url_live = knockout_live = 0;
    gf_pause_action(&gflow, FE_ACTION_QUIT); break;
case PR_ACT_CONTINUE: {
    int next = url_live ? m7_next_url(&gflow, &g_catalog, &url_series) : 0;
    if (next <= 0) {
        url_live = knockout_live = 0;
        if (next < 0) fem_show_error(fem, "The next URL course is unavailable.");
        else if (fem) fem_open(fem, FEM_SCR_MAIN);
    }
    break;
}
```

[inferred] Mid-race pause Restart resets the collector through race preparation without undoing
already-completed earlier rounds. Only results Restart undoes that finished round. Clear live flags
on GF_CMD_ERROR/STOP_RACE when abandoning the session, not on the ARM transition between URL rounds.
HUD needs knockout.remaining/alive and URL round/total/points. The knocked-out language keys are
HUD_DNF_KNOCKED_OUT0x1b59940c and HUD_LB_KNOCKED_OUT0x5d82dba2. No HUD implementation is claimed.

```make
race-series-rules-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Isrc tools/race_series_rules_test.c src/race_rules.c -lm -o build/race_series_rules_test
	./build/race_series_rules_test
race-series-ai-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections $(SDL_CFLAGS) -Isrc tools/race_series_ai_test.c src/ai.c -Wl,--gc-sections -lm -o build/race_series_ai_test
	./build/race_series_ai_test
```

[inferred] Repeat synthetic recipes with -DN2_GLES. The existing postrace runtime helper now also
writes url-standings.png. Default URL grouping, points, grace, and tie policies remain inferred;
replace the named policies when actual tournament data/retail captures establish them.

## Priority 5: explicit Quick Race options (base ea2c9c4)

[inferred] These are host patches to apply with priorities1–4. The implementation lives in owned
AI/world/rules files; no front-end/main/Makefile files were edited by this worker. Retain existing
requests/API enum values. Do not advertise a row until these host changes are installed.

[data] Genuine labels in the installed language table: QR_OPT_OPPONENTS 3384a679,
QR_OPT_TRAFFIC_LEVEL eb9dfc09, QR_OPT_CATCH_UP 8b8e913a, TRACK_DIRECTION a88ffeb4,
TRACK_DIRECTION_FORWARD 9e4c4648, TRACK_DIRECTION_REVERSE 22f254af, QR_OPT_LAP_KO 7fd73849.
[inferred] Replace the incorrect QR_OPPONENTS/QR_TRAFFIC/QR_CATCH_UP label keys in fe_track.c.
Use the track-options page for Laps/Direction/Lap KO, mode-options for Difficulty/Opponents/Traffic/
Catch Up. Keep touch Back and page transition behaviour from the current front end. Rows must use
real localized values and reject a missing caption rather than silently displaying a guessed label.
[inferred] In fe_menu.h include race_rules.h and append `RrOptions rules;` to FemRequest and FeChoice.
Initialize `m->choice.rules=rr_options_default()` in fem_create (near192); copy
`r.rules=m->choice.rules` beside laps/difficulty in fem_update (near370). Free Run ignores this payload.
Maintain selected values across track/car navigation; new menu creation uses defaults. Value mapping:

| Row | Field / allowed selection | Host effect |
| --- | --- | --- |
| Opponents | rules.opponents = 0..5 | wanted opponents, constrained by genuine grid availability |
| Traffic | rules.traffic = 0..3 | None/low/moderate/high: 0/4/8/16 traffic targets |
| Catch Up | rules.catch_up = 0 or1 | overrides source per-driver catch-up flag, keeps source skill |
| Direction | rules.reverse = 0 or1 | named Forward/Backward grid and Routes{id}F/B.bin |
| Lap KO | rules.lap_knockout = 0 or1 | enable only Circuit, at least one opponent; count−1 race laps |

[inferred] `-1` on the first three fields preserves the previous authored/host default. Numeric traffic
counts and no-source catch-up curve are provisional named constants. Solo downhill Drift disables opponents; modes without traffic keep the
traffic row disabled; the host's existing `race_has_traffic(kind)` predicate remains authoritative. The exact
retail restrictions on opponents/reverse remain unverified. A selected reverse launch fails when its
backward package/grid cannot be decoded; it must never launch forward with a reverse label.

[inferred] main.c next to race locals5508 (after `traffic_world`5481 is declared):
```c
RrOptions race_options=rr_options_default();
int saved_traffic_target=-1;
traffic_world.race_options=&race_options;
```
[inferred] Before constructing a QR GfLaunch at7129 validate/copy the request, without resetting on
Restart or a URL next-round launch:
```c
if (frq.kind==FEM_REQ_QUICK_RACE) {
    if (!rr_options_valid(&frq.rules)) {
        fem_show_error(fem,"Invalid race options");
        frq.kind=FEM_REQ_NONE;
    } else race_options=frq.rules;
}
```
Guard the existing launch block with the resulting request kind. At GF_CMD_ARM_EVENT7176 reject
KO on a non-Circuit event or with requested zero opponents using g_dbg.race_error/fem_show_error;
set the existing `ko_live` preparation predicate from `race_options.lap_knockout`. Do not infer KO
from the Circuit mode alone. URL rounds retain the same rules/entrant IDs as priority4.

[inferred] Replace race_start_request world_race_start at11238 with the directed entrypoint. Preserve
the existing success body and add an explicit failure branch (a failed launch must reach GF_CMD_ERROR):
```c
int evi=world.city.active_ev;
int armed=evi>=0 && world_race_start_direction(&world,troot,evi,
    g_dbg.race_maxlaps_want,race_options.reverse,
    g_dbg.race_error,sizeof g_dbg.race_error)>0;
if (armed) { /* existing race_place_on_grid/race_prepare_pending body */ }
else {
    if (!g_dbg.race_error[0]) snprintf(g_dbg.race_error,sizeof g_dbg.race_error,
                                     "Race direction/grid unavailable");
    ai_race=0;race_prepare_pending=0;race_state=1;
}
```
Direct CLI/debug launches at4751/7022 keep old forward defaults unless the owner also exposes the
new options there. A deliberate debug launch should reset race_options to rr_options_default to
avoid inheriting a prior QR selection. No existing world_race_start caller is forced to change.

[inferred] At prepare7914, replace hardcoded wanted with:
```c
int authored=event->info.kind==N2_RACE_URL?5:3;
int wanted=event->info.downhill?0:
    rr_opponent_count(&race_options,authored,world.city.race.ngrid);
```
Keep `nai!=wanted` and `aipath.n<2` rejection (also reject wanted<0). Zero opponents is supported by
ai_race_prepare's solo driver/course binding, `ai_race=1` still denotes an active race. Source variant
lookup receives the effective wanted count. KO initialization uses `nai+1`, requires>=2, and replaces
maxlaps on the player AND every AiRace progress copy with `nai` (see priority4 hook). If grid fitting
cannot realize a selected field size the existing launch diagnostic is retained; do not quietly race
a smaller field. rr_opponent_count rejects an insufficient grid with -1; expose that
constraint before launch where grid metadata is known.

[inferred] Preserve/restores the free-roam traffic slider independently from a race selection. Before
first successful preparation7950 save once `if(saved_traffic_target<0)saved_traffic_target=g_dbg.traffic_target;`.
Immediately after the per-frame traffic_world.traffic_target assignment6757:
```c
traffic_world.race_options=&race_options;
if (saved_traffic_target>=0 && world.city.race.active)
    traffic_world.traffic_target=rr_traffic_target(&race_options,saved_traffic_target);
```
Existing race_has_traffic remains the enable predicate; no-traffic modes do not spawn traffic even
with a positive target. On final exit/error/stop (not a next URL round) set saved_traffic_target=-1;
the slider itself was never mutated. Existing live traffic reduction/respawn policy handles targets.
Catch-up reads the borrowed options pointer each tick; explicit Off suppresses only the catch-up
curve, while On uses the decoded source curve when supplied or the named bounded fallback otherwise.

[inferred] New test target recipes (no SRC addition beyond race_rules.c from priority2):
```make
race-options-test:
	$(CC) $(CFLAGS) -Isrc tools/race_options_test.c src/race_rules.c -o build/race_options_test -lm
	./build/race_options_test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -ffunction-sections -fdata-sections -Isrc tools/race_options_ai_test.c src/ai.c src/physics.c src/race_rules.c -Wl,--gc-sections -o build/race_options_ai_test -lm
	./build/race_options_ai_test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -ffunction-sections -fdata-sections -Isrc tools/race_direction_test.c src/world.c -Wl,--gc-sections -o build/race_direction_test -lm
	./build/race_direction_test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -ffunction-sections -fdata-sections -Isrc tools/race_options_field_test.c src/ai.c src/world.c src/physics.c src/race_rules.c -Wl,--gc-sections $(SDL_LIBS) $(GL_LIBS) -o build/race_options_field_test -lm
	./build/race_options_field_test
```
Repeat with CFLAGS+=-DN2_GLES. Header-heavy units use the project's -Wno-unused-function convention.
All builds/runs still use the owner's memory/lock/nice requirements. End-to-end selected options and
reverse play remain the orchestrator's gate after these host patches; they are not claimed by unit tests.
