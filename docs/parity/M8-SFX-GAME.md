# M8 — Retail sound effects in the game

Status: wired on `parity/m8-sfx-game` (2026-11-15). Owner: orchestrator. Builds on task D (`sfx_bank`, the .abk reader)
and task G (`sfx_play`, the mixer).

Tags: **[data]** from the player's own retail files; **[inferred]** a choice made here that owner listening or captures
must confirm. Clean room: only data files, never the executable.

## What changed for the player

| Effect | Before | Now |
|---|---|---|
| Collisions (cars, walls) | synthesized noise burst | a retail impact one-shot, light / medium / heavy by strength |
| Tyre slide | synthesized hiss + 920 Hz square | a retail pavement skid loop, level and pitch follow the slide |
| Road / wind bed | low-passed white noise | a retail road-noise loop, level with speed², pitch with speed |
| Gear shift | none | a retail shift one-shot (up / down) |
| Wall scrape, engine | unchanged | unchanged (engine keeps its Gnsu20 / bank path) |

Any group whose bank does not load keeps the synthesized sound, so a missing or partial install never goes silent.
The effects follow the FE master volume.

## Design

`src/sfx_game.{c,h}` (pure C99, no SDL) owns the four banks, the decoded clips and one `sfx_play` mixer. It runs entirely
on the audio thread: `audio_cb` reads the values the game thread already publishes (`g_hit` collision latch, `g_skid`,
`g_road_vol`, plus the new `g_sfx_gear` and `g_sfx_master`), builds an `SfgInput` and calls `sfg_render` once per
callback, after the engine has written the buffer; the mixer adds on top with saturation. No locks are needed on the play
path; the pointer `g_sfx_game` is set once under `SDL_LockAudioDevice` after `audio_init` and cleared after the device
closes, before `sfg_destroy`.

## Sample choices ([inferred]; the banks carry no names)

| Group | Bank [data] | Choice | Why |
|---|---|---|---|
| impact | `IG_GLOBAL/MB_Stich_Collision.abk` (94 one-shots) | light < 0.20 s, heavy > 0.45 s, medium between; random within the set, pitch ±8 % | length tracks weight |
| skid | `SKIDS/SKID_PAV_MB.abk` | sample 0, looped | start and end loudness match (a loop) |
| road | `IG_GLOBAL/ROADNOISE_00_MB.abk` | sample 4, looped | the quietest steady loop; 7..13 are bumps |
| shift | `SHIFTING/GEAR_MED_Base.abk` | 0 = up, 1 = down | two one-shots per bank; medium engine class for every car |

Levels: impact 0.25 + 0.55 × strength, skid 0.7 × slide, road 0.5 × speed², shift 0.45, all × master.
Impacts are at least 80 ms apart; the skid loop is ramped to silence and stopped one callback later (no click).

## Checks

- `make sfx-play-test`: task G's mixer test (134 checks).
- `OPENUG2_DATA=... make sfx-game-check`: a scripted 12 s drive (stop, accelerate through 4 gears, crash, slide, stop) on
  the real banks under ASan/UBSan: all four groups load, silence when stopped, road bed audible at speed, crash louder than
  the bed, slide audible and released, effects alone stay below the rails. Missing data returns NULL (host keeps synth).

## Next

Front-end menu sounds (`FE/FE_MB.abk`, 68 samples at 44100 Hz), surface-dependent skid and road loops, gear bank per
engine class and upgrade level, turbo / NOS once those systems exist, traffic and environment beds, countdown.
