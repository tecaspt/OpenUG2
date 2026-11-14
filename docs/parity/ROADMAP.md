# Retail parity roadmap

Goal: OpenUG2 should play like the original PC Need for Speed: Underground 2, driven by the player's own retail
data, with the original front end and game systems. This file is the shared plan and status board. Anyone (human
or agent) resuming the work should read it first, then [CHANGELOG.md](CHANGELOG.md) and the active milestone's
plan ([M1-PLAN.md](M1-PLAN.md)).

## Ground rules
- Clean room. Parse the retail **data** files; never read, copy or translate the retail executable or any
  decompiled/recompiled code derived from it. External reverse-engineering notes may inform behaviour and must be
  tagged `[external-doc]`. Code may be reused only from licence-compatible (MIT/BSD/Apache/zlib), non-decompiled
  projects, with attribution.
- Every behavioural claim in specs is tagged `[data]` (read from the files), `[observed]` (seen in the retail game or
  a run), `[external-doc]`, or `[inferred]`.
- No retail bytes in the repository: tests use synthetic vectors; checks that need retail data read it at run time
  from `$OPENUG2_DATA`.
- Default behaviour of existing command-line paths must not change unless that is the point of the change. Invariance
  checks: reference `--shot` images on STREAML4RA/STREAML4RB, and the 105-event `race_events_check --drive` summary.

## Status board
Legend: DONE · ACTIVE · NEXT · LATER. "Engine today" summarises what exists before the milestone.

| # | Milestone | Status | Engine today / scope |
|---|---|---|---|
| M0 | Retail language table | DONE (364a0f5) | PC `LANGUAGES/*.bin` (16-byte header) now loads: 3925 English strings. Unlocks every retail caption, `TRACK_NAME_*`, `QR_*`. |
| M1 | Retail Quick Race loop | ACTIVE | UI_Main → QR mode → track → options → car → loading → race → pause (restart / quit to menu) → post-race results → menu; Free Run; touch overlay for phones/tablets (platform adaptation, not retail). Plan: [M1-PLAN.md](M1-PLAN.md). |
| M2 | Front-end options, profiles, controls | NEXT | `UI_OptionsMain` in front-end context; Audio and Player pages missing; `UI_ProfileManager` over the existing profile container. |
| M3 | Boot / attract | LATER | EA/THX logos, intro FMV (needs a VP6 decoder for `MOVIES/*.vp6`), splash, language select, loading tips. |
| M4 | Front-end 3D scenes | LATER | Showroom platforms (`FRONTEND/PLATFORMS`), car rotate behind Main/Car Select. |
| M5 | Customise car and performance tuning | LATER | Retail customise screens; engine has kits/rims partially; paint/vinyl/neon debug-only; spinners, hydraulics, ICE, dyno missing; most performance parts undecoded. |
| M6 | Career core | LATER | New/load career, race entry markers, SMS, money, reputation, stages/unlocks, sponsors, world map, shops/crib/car lot, magazines/DVD, hidden races, career save. Outruns work but give no reward. |
| M7 | Race-mode parity | LATER | All six race types run; missing URL tournaments/points, final drift scoring, drag shift-quality feedback, Lap KO, opponents/traffic/catch-up options, reverse direction, rewards; laps from data. |
| M8 | Audio | LATER | Engine audio works; SFX procedural (not retail); music/EA Trax, front-end music and speech missing. |
| M9 | HUD parity | ACTIVE (branch `parity/m9-hud`) | Speedo/tach, position/laps/time, drag/drift HUDs, race map, GPS arrow work; minimap zoom-with-speed, reputation, cash/SMS icons, NOS gauge (general), loading screens missing or partial. |
| M10 | Replay | LATER | Missing; pause "Replay" entry always disabled. |

Evidence (file:line) for every row is in [M1-PLAN.md](M1-PLAN.md) §(a) and §(b).

## M1 work units
One owner per file; units integrate only through the frozen API in M1-PLAN.md §(c).

| unit | files | status |
|---|---|---|
| W0 string table | `src/ugui/ugui_strtab.c`, `tools/ugui_test_assets.inc` | DONE 364a0f5 — `make ugui-test` 10034/0 |
| U2 catalogue | `src/game_catalog.{c,h}`, `tools/game_catalog_check.c` | DONE 696308b — 104 events / 6 regions / 44 cars (29 player) |
| U3 game flow + input script | `src/game_flow.{c,h}`, `src/input_script.{c,h}`, tests | DONE 4818617 — 252/0, 48/0 |
| U4 touch overlay | `src/touch_controls.{c,h}`, `src/ui_flat.{c,h}`, `src/controls_sdl.{c,h}`, test | DONE 9069f0a — 291/0 (drawing not yet rendered) |
| U1 front-end carousel + menu | `src/fe_entries.*`, `src/fe_screen.*`, `src/fe_menu.*`, test | DONE c832017 — fe_screen_test PASS; retail UI_Main/QR mode/loading render |
| R1 screen-binding research | `docs/cleanroom-settings/SPEC_GUI_FE.md`, `src/fe_binding.h` | DONE (task B, merged 7d7fdba) |
| U5 track select | `src/fe_track.c`, `src/gui_view.{c,h}` | DONE (task B) — track name shows the event number when no name string is found |
| U6 car select | `src/fe_car.c` | DONE (task B) — thumbnails blank (pool assignment unknown) |
| U7 post-race | `src/postrace.{c,h}`, `src/ugui/ugui_assets.c` | DONE (task B) |
| Integration (Phase-1 part) | `src/main.c`, `Makefile` | DONE af10545 — menu on bare launch, Quick Race→race, pause Restart/Quit to menu, Free Run, touch overlay, `--input-script`; gate passed (see CHANGELOG) |
| Integration (Phase-2 part) | `src/main.c`, `Makefile` | DONE a6b5c81 — gate: 91/91 tests, xvfb 4/4, drive + shots identical, S1/S2/S3 + S4 (finish -> results -> restart) pass |

M1 gate (all must pass): full test suite; new unit tests wired into the Makefile; invariance checks above;
scripted end-to-end scenarios S1–S3 from M1-PLAN.md (menu on bare launch; Quick Race drag 4201 with track and car
switch, finish, results, restart, pause, quit to menu, Free Run, quit; same flow on the GLES/iOS host build).

## Decisions taken (may be revisited by the project owner)
- Front end must be the retail FNG flow; an invented menu only where the retail path is shown to be blocked.
- Main-menu entries not yet implemented are shown disabled with retail art.
- Quick Race offers all player cars until career unlock rules exist.
- Free Run uses region STREAML4RA until the whole city can be streamed.
- Boot goes straight to the main menu until M3.
- Touch overlay is a platform adaptation: shown on first touch, hidden when a pad or keyboard is used.

## Open questions
- Retail captures of the front-end flow (screen order, inserted entries, post-race actions) to replace `[inferred]`.
- EA Trax music streams are not present in the reference install.
