# M6 — Career core (slice S1 + wiring)

Status: S1 playable loop wired on `parity/m6-career` (2026-11-15). Owner: orchestrator.

Tags: **[data]** read from the player's own retail files; **[inferred]** a policy chosen here that owner captures of
the real game must confirm; **[code]** existing OpenUG2 behaviour. Clean room: only data files, never the executable.

## What works

Main > Career → the retail Career screen (`UI_StartCareer.fng`: Resume Career / Start New Career) → free roam in the career city (STREAML4RA) → drive into an event zone below 30 km/h → the HUD
shows `ENTER RACE: <name> $<purse>` → Enter Race → the event loads in its own region → results → bank cash and
reputation are paid → Continue → back in the city at the pre-race spot → after the stage's wins the next stage's
events appear on the map. Quit to Main saves the free-roam pose; Career then continues the saved career.

Headless proof (`--career-dev 1`): new career, the 5 Stage-1 races won, bank 1250, reputation 500, stage 2 opens,
the first stage-2 sponsor race pays its own purse (1000); a second run continues the saved career.

## Modules (pure C99, no SDL/GL)

| File | Role |
|---|---|
| `src/career_data.{c,h}` | Reads GLOBALB.BUN chunk 0x80034A10 (first copy: 224 races x 136 B, 6 stages x 80 B, string blob), PathsFreeRoam.bin 0x3414A (116 locator zones), TrackPosMarkersFreeRoam.bin 0x34146 (markers) [data] |
| `src/career.{c,h}` | State, availability, zone hit, result apply, stage gate, save file `<profile dir>/openug2_career.bin` |
| `src/career_host.{c,h}` | Glue for main.c: begin / zone / engage / result / return / suspend / icons / prompt |

Tests: `make career-data-test` (synthetic, 13634 checks), `make career-test` (198 checks),
`OPENUG2_DATA=... make career-data-check`, `OPENUG2_DATA=... make career-host-check` (a whole Stage 1 under ASan/UBSan).
Data checks exit 77 (SKIP) without `OPENUG2_DATA`.

## Host wiring (main.c, fe_menu, fe_profile)

| Hook | Where |
|---|---|
| Career data load, Main > Career enabled only when it loads | after `fem_create` (`ch_init`, `fem_set_career`) |
| Career screen: Resume (only with a save, focused), New (with a save: a second choice confirms, label `CONFIRM_OVERWRITE_SAVED_GAME`) | `FEM_SCR_CAREER` in fe_menu, entries `fe_career_entries` |
| Career request: 0 = resume, 1 = new | `FEM_REQ_CAREER` → `fem_persist_profile` → `ch_begin` → `gf_start` |
| Enter Race inside a zone (key, pad, touch `enter_race`) | `career_zone` beats the outrun challenge |
| Result → reward + save | `GF_CMD_OPEN_RESULTS` → `ch_on_result` |
| Continue → city at the pre-race pose | `PR_ACT_CONTINUE` → `ch_return`; focus override in `prepare_map_switch`, same-track reload |
| Restart from results = new attempt | `PR_ACT_RESTART` clears `resolved` |
| Quit to Main → save pose, unfinished race = aborted | `GF_CMD_OPEN_MAIN` → `ch_suspend` |
| Map icons (available career events only) and the notice | HUD free-roam block (`ch_icons`, `ch_prompt`) |
| Profile rename keeps the career save | `fep_rename` copies `openug2_career.bin` before removing the old folder |
| Developer automation | `--career-dev PLACE`: engage the first available event, finish in PLACE, Continue |

## Data facts used

- Race record: name/locator strings, hashes, unlock mode/arg, purse (+0x30), type (+0x0F), category (+0x34),
  stage (+0x37), heats (+0x18: event id, dir, laps), AI count (+0x7C), hidden (+0x7F) [data].
- Stage 1 = 4013 L4RA, 4101 L4RA, 4201 L4RB, 4311 L4RC, 4602 L4RF, purse 250 each [data].
- Zones are 8x8 m squares in L4RA coordinates; the race itself runs in the heat's region [data].

## Inferred policies (replace from owner captures)

1. Stage 0 (intro/DDAY) is skipped: a new career starts at stage 1 with bank 0.
2. Only a first win pays: purse + reputation base (100); losses and repeats pay nothing; won events disappear.
3. Stage complete = first wins in the stage ≥ stage requirement (+URL/sponsor counts); DVD covers count as met.
4. Engage below 30 km/h inside the zone; text `ENTER RACE: <name> $<purse>` (retail ENGAGE_* dialog not yet used).
5. Career screen order Resume, New; New over an existing save needs a second choice; no Load (one career per profile). UI_StartCareer's header text resolves to the Options caption, so the host titles it with `UI_MAIN_CAREER`.
6. Career race difficulty medium; laps from the heat; car = the car in use.
7. Continue returns to the pre-race spot (nearest surface, saved heading).
8. Career needs a saved profile; an in-memory default profile is saved on first Career.

## Not yet (next slices)

Stage 0 intro and car lot, UI_CareerSelect / world map, ENGAGE dialog art, SMS, sponsors choice, multi-heat
URL tournaments and per-race AI count (M7 race rules), DVD/magazine covers, car/part unlocks, shops, retail save import.
