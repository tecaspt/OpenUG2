# Parity work changelog

Newest first. One entry per change that affects behaviour, data handling or the plan. Commit hashes refer to the
branch the work was done on (`parity/m1` unless stated).

## 2026-11-14
- **a6b5c81** Retail Quick Race screens wired in (task B, branch `parity/m1-screens`, merged 7d7fdba): Track Select, Track/Mode Options, Car Select with in-place car preview, Event Results and Post Race (Restart / Continue / Quit to Main); failed launch returns to Track Select with the reason. Scripted end-to-end run: menu -> drag -> finish (4th, 1:54.48) -> results -> post race -> restart.
- **a11e3b4** Follow-ups: car stops on Quit to Main; two-finger tap pause (off the buttons, any frames) replaces the iOS "second finger down" rule that fired while driving; see-through touch buttons, camera button moved to top centre; test/audit options no longer open the menu.
- **af10545** Front end wired into the game: bare launch opens the retail main menu; Quick Race → mode starts the race in process; pause menu Restart / Quit to Main Menu; Free Run; touch overlay; `--menu`, `--no-menu`, `--touch`, `--no-touch`, `--input-script`. Existing command-line starts unchanged (shots and 105-event drive identical). Warning baseline 259 → 269 (explained in the commit).
- Work split: retail Track/Options/Car Select and post-race screens moved to a separate task on branch `parity/m1-screens`.
- **Plan.** Retail-parity roadmap M0–M10 and the M1 contract written ([ROADMAP.md](ROADMAP.md), [M1-PLAN.md](M1-PLAN.md)).
  Findings: retail front-end screens bind with the existing carousel code; in-process event start/stop/restart and
  car/track switching already work but had no production caller; race end was terminal; two-finger pause on iOS
  conflicts with two-thumb driving.
- **364a0f5** ugui: load the PC language table (16-byte header). 3925 English strings now resolve.
- **696308b** Quick Race catalogue: 104 events in 6 regions, 44 cars (29 player). Event 4341 (no mode) and regions
  L4RH/L4RR (not switchable in process) are excluded.
- **4818617** Front-end game flow state machine and scripted input player (headless end-to-end testing).
- **c832017** Retail front-end carousel screens (UI_Main, Quick Race mode select, loading) and the menu controller; not wired yet.
- **9069f0a** On-screen touch overlay and flat-shape drawing helper; not wired into the game yet.

## Before parity work (branch `ios/main`, summary)
- f6a59c6 iOS: render at full pixel resolution.
- d935687 iOS/iPadOS port (GLES2, UIKit framebuffer, data discovery in Documents, lifecycle, hot-plug, Codemagic).
- 85c7b44 PlayStation 2 style default layout for recognised game controllers.
- 3e1299a … 78384e2 race-course decoder fix and route/minimap check tools (see branch history).
