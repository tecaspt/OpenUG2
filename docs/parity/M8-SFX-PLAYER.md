# M8 — the sound-effect player

The second step of milestone M8. Task D produced `src/sfx_bank.{c,h}`, which opens the retail
Ginsu `.abk` banks and decodes their samples to mono 16-bit PCM (176 banks, 1513 samples, all
decodable). This step adds `src/sfx_play.{c,h}`: a small mixer that plays those decoded samples —
start, retune, loop, stop — and `src/sfx_groups.h`, which says which bank backs which effect
group. Nothing here is wired into the game yet; the host wiring is in
[M8-SFX-PLAYER-INTEGRATION.md](M8-SFX-PLAYER-INTEGRATION.md).

Claims are tagged as elsewhere in `docs/parity/`:

- `[data]` — read off the retail data or the project's own source, reproducible.
- `[inferred]` — a reasonable guess, stated as a guess.
- `[design]` — a decision made here, not a fact about the retail files.

## 1 Output model

`[data]` The game's device is one SDL audio output at 44100 Hz, `AUDIO_S16SYS`, **mono**, 1024
samples per callback (`audio_init`, `src/audio.c:611-616`), and its callback treats the buffer as
`int16_t *` with `n = len / 2` (`audio_cb`, `src/audio.c:391-396`). The mixer is written for
exactly that: mono s16 in, mono s16 out.

`[design]` `sfx_mix_s16(p, out, n)` **adds** into the caller's buffer and never overwrites it, so
the host calls it last and the effects land on top of whatever else filled the buffer. When no
voice is live the buffer is untouched, byte for byte.

`[design]` Threading: no locks, no threads inside the mixer. `sfx_mix_s16` runs on the audio
thread; `sfx_play` / `sfx_set` / `sfx_stop` / `sfx_stop_all` are mutators and the caller must
serialise them against the callback — `SDL_LockAudioDevice` around each call, or call them from
inside the callback. `[data]` Worth being precise: the project does **not** lock the device today.
Game-thread parameters reach the callback through `volatile float` globals (`g_road_vol`,
`g_skid`, `src/audio.c:23-25`) and `src/main.c` only ever pauses or resumes the device. A single
`float` survives that; a voice table does not, which is why the new API asks for a lock instead of
assuming the existing pattern is safe.

`[design]` Nothing allocates except `sfx_player_create` (one `calloc` of a fixed struct) and
`sfx_clip_from_bank` (one buffer per clip). The play and mix paths never call `malloc`, so the
callback never blocks on the allocator.

## 2 Voices and handles

`[design]` 24 slots (`SFX_MAX_VOICES`), fixed at create time — no grow, no shrink, no free list.

`[design]` A handle is `(generation << 5) | (slot + 1)`. Each slot carries a generation counter
that **keeps counting across allocations**: it is bumped when a voice ends, is stopped, or is
stolen, and the new voice takes the new value. A handle therefore dies the moment its own voice
dies, and a later sound in the same slot can never be reached through the old handle. The
generation is 26 bits, so aliasing an old handle would need 2^26 allocations of that one slot.
Slot numbers never repeat immediately for the same reason: the reused slot is one generation
older than every live handle pointing at it.

`[design]` Handle 0 is never returned. `sfx_set`, `sfx_stop` and `sfx_active` on a stale or
foreign handle do nothing at all — no crash, no effect on whoever owns the slot now.

## 3 Resampling

`[design]` Each voice has a `double` read position and a step of `(clip_rate / out_rate) * pitch`
source frames per output frame. Output sample = linear interpolation of `pcm[floor(pos)]` and
`pcm[floor(pos)+1]`, scaled by the voice gain, rounded with `floorf(x + 0.5f)`.

Two properties worth having, both covered by the test:

- `[design]` **Exactness.** With `pitch = 1` and `clip_rate == out_rate` the step is exactly 1.0,
  every position is an integer, the interpolation weight is exactly 0, and the rounding of an
  integer is itself — so a 44100 Hz clip mixes back out byte for byte. That is why the position is
  a `double` stepped by `+= step` rather than an accumulator that gets scaled or rounded.
- `[design]` **Produced length.** A one-shot emits `ceil(frames / step)` output frames: it stops as
  soon as the position passes the last source sample. So 22050 → 44100 doubles the frame count,
  and pitch 2 halves it. No fuzz factor: the frame count is exact and asserted exactly.

`[design]` The upper tap differs by mode. In a **one-shot** it clamps at `frames - 1`, so the last
output frame repeats the last source sample instead of reading one element past the buffer. In a
**loop** it wraps to sample 0, so the seam is a crossfade between the last and first sample rather
than a step to silence. The test distinguishes these: at the seam of a looped 22050 clip the
output must be the mean of the last and first sample, which is neither of them.

## 4 Gain ramp

`[design]` `sfx_set` ramps the gain linearly to its new value over `out_rate * 5 / 1000` output
frames (220 at 44100, i.e. 5.0 ms). The slew limit is therefore `|Δgain| / 220` per frame; the
constant is public (`SFX_GAIN_RAMP_MS`) so a caller can predict it.

`[design]` Two details that make the ramp behave:

- It starts from the gain **in force right now**, not from the previous target, so retargeting
  mid-ramp cannot jump. Repeated calls for the same target do not restart (and so lengthen) it.
- `sfx_play` does **not** ramp: the requested gain is in force from the first sample. A 5 ms fade-in
  on every impact would blunt the transient the effect exists for, and it would make the exact-mix
  property above unreachable. A voice that should fade in is started with `sfx_play(gain 0)`
  followed by `sfx_set(gain want)`.

`[design]` `sfx_set` with a new pitch takes effect on the next frame, unsmoothed — only gain is
ramped, because only gain was specified to be. A `pitch` that is not `> 0` is *ignored* by
`sfx_set` (the voice keeps its current pitch), whereas `sfx_play` clamps a non-positive or NaN
pitch to the 0.25 floor. The difference is deliberate: `sfx_play` must land on a valid speed, while
`sfx_set` is called continuously from a game loop where a stray 0 should not retune a held sound
an octave down.

## 5 The mix

`[design]` Voices are summed slot by slot into the caller's buffer with a saturating add: each
`int` sum is clamped to `[−32768, +32767]` before it is stored, so any number of stacked voices
sits on a rail instead of wrapping. The rails are deliberately asymmetric, because s16 is. The
test checks both rails, and that a loud voice plus a quiet voice of opposite sign still cancels
exactly — i.e. the clamp only bites at the ends.

## 6 Voice stealing

`[design]` `sfx_play` takes the first free slot. If all 24 are busy it steals the **quietest
one-shot**, where quiet means the gain actually in force at that moment (not the requested gain),
with ties going to the lowest slot number. Loops are never stolen: a loop is a held sound the host
believes is still playing, and killing it silently would be worse than refusing the new effect —
so if every slot holds a loop, `sfx_play` returns 0. The test covers all three cases, including
the uncomfortable one where a loop is quieter than the one-shot that gets stolen instead.

## 7 Clip ownership and lifetime

`[design]` `sfx_clip_from_bank` decodes a sample and owns the buffer; `sfx_clip_free` releases it.
A voice only **borrows** `clip->pcm` — nothing is copied at play time, which is what keeps the play
path allocation-free. The consequence is a caller contract, stated in the header: every clip (and
the bank behind it) must outlive every voice started from it. There is no refcount, and
`sfx_stop_all` does not discharge the obligation, since the caller frees when it likes.

## 8 Effect-group table

`src/sfx_groups.h` is a static `{ group, path }` table, paths relative to the data root. Every path
is copied **verbatim** from disk, misspellings included — the collision bank is `MB_Stich_Collision`
(one "t") while the whoosh bank next to it is `MB_Stitch_Whoosh`; "correcting" either makes it
unopenable.

`[data]` Sample counts and rates below come from the task-D census over all 176 banks
(`docs/parity/M8-SFX-BANKS.md`); every bank listed decodes 100% of its samples. `[inferred]` is
what the bank is believed to contain — the banks carry **no** sample names, so the meaning of a
group and the meaning of a sample index inside it are both guesses from retail naming.

| group | bank | samples | rate [data] | reading [inferred] |
|---|---|---|---|---|
| menu | `SOUND/FE/FE_MB.abk` | 68 | 44100 | menu moves/accepts/backs; the only group needing no resampling at 44100 |
| menu | `SOUND/GLOBAL/FE_COMMON_MB.abk` | 18 | 44100 | shared front-end clicks |
| collision | `SOUND/IG_GLOBAL/MB_Stich_Collision.abk` | 94 | 36000 | the impact set: bodywork, scrapes, wall hits |
| skid | `SOUND/SKIDS/SKID_PAV_MB.abk` (+3 more) | 5 each | 32000/36000 | PAV/PAV2 × DRIFT/DRIFT2 read as two surfaces, two variants |
| nos | `SOUND/NOS/Nitrous_00_MB.abk`, `Nitrous_01_MB.abk` | 6 each | 28000 / 22050 | two mixes of the same event |
| nos | `SOUND/NOS/Hydraulics_00_MB.abk` | 7 | 22050 | valve/solenoid chatter |
| shifting | `SOUND/SHIFTING/GEAR_{SML,MED,LRG,TK}_{Base,Lev1..3}.abk` | 2 each | 32000 | size class × upgrade stage; 2 samples ≈ shift-up and shift-down |
| turbo | `SOUND/TURBO/TURBO_{SML1,SML2,MED,BIG,TRUCK}_{0,1,2}_MB.abk` | 5 each | 22050 | engine size × boost stage; a loop candidate, not a one-shot |
| wind | `SOUND/IG_GLOBAL/WIND_00_MB.abk`, `WIND_01_MB.abk` | 4 / 3 | 28000 / 22050 | two speeds or environments, not a stereo pair (every retail sample is mono `[data]`) |
| rain | `SOUND/IG_GLOBAL/FX_Rain_MB.abk` | 2 | 32000, 24000 | one loop, one variant |
| roadnoise | `SOUND/IG_GLOBAL/ROADNOISE_00_MB.abk` | 14 | 22050 | per-surface road noise |
| whoosh | `SOUND/IG_GLOBAL/MB_Stitch_Whoosh.abk` | 16 | 36000 | pass-by (extra, not requested) |
| traffic | `SOUND/IG_GLOBAL/TRAFFIC_MB.abk` | 2 | 22050 | traffic (extra) |
| environment | `SOUND/IG_GLOBAL/ENV_COMMON_MB.abk` | 8 | 28000/16000/18000 | general ambient (extra) |
| misc / fx | `SOUND/IG_GLOBAL/FX_Misc_MB.abk`, `FX_MAIN_MEM_MB.abk` | 2 / 4 | 48000+24000 / 28000 | misc foley (extra) |

`[inferred]` The `_0`/`_1`/`_2` and `Base`/`Lev1`/`Lev2`/`Lev3` suffixes most plausibly index upgrade
levels rather than left/right or surface variants — nothing in the bank confirms it, and the
sample order inside every bank is unverified.

## 9 Verification

See [M8-SFX-PLAYER-INTEGRATION.md](M8-SFX-PLAYER-INTEGRATION.md) for the exact build lines. Every
run below is on this machine, and the claims are quoted from the captured output, not paraphrased.

- `sfx_play_test` — synthetic clips only, no retail bytes: **`sfx_play_test: 134 checks, 0
  failures` / `PASS`**, exit 0, built with `-O2 -std=c99 -Wall -Wextra -Werror` and **no warnings**.
  The same source built with `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` prints
  the identical line, exits 0, and produces no AddressSanitizer, LeakSanitizer or UBsan report.
  The suite is what pins the properties claimed above: sample-for-sample identity at matched rate,
  `ceil(frames/step)` produced lengths, loop wrap versus one-shot clamp, both saturation rails,
  steal-the-quietest-one-shot, inert stale handles, and the gain-ramp slew bound.
- `sfx_play_demo` — renders through the same mixer against the real banks. All 40 group-table banks
  opened, **0 failures**, 14 groups used, 23 events / 8 retunes / 3 stops:

  | | |
  |---|---|
  | length | 463050 frames = 10.50 s, 44100 Hz, mono, s16, 926144 bytes |
  | peak | reaches both rails: minimum sample −32768, maximum +32767 |
  | clipping | 483 frames on a rail = **0.104 %**, longest consecutive run **28 frames (0.6 ms)** |

  So yes, parts of it clip — but only in sub-millisecond transients where several impacts overlap,
  not as sustained distortion. The header and these numbers were re-checked independently by
  reading the file back with Python's `wave` module, which agrees frame for frame, and the ASan
  render is **byte-identical** to the normal one (`cmp`), so the output does not depend on the
  build. An earlier version of the script, with a louder ambient bed, measured 0.597 % on a rail
  and a 64-frame run; the bed was turned down to get the headroom, which is why the retail samples
  clip so easily: many of them already peak within a hair of full scale (`[data]`, task-D census).
- Guard behaviour, exercised: called with the wrong argument count it prints usage and exits 2;
  pointed at a path inside a git work tree it refuses, exits 2 and writes nothing.

## 10 Limits, and what is not done here

- `[design]` Mono only, because every retail sample is mono `[data]`. No panning, no reverb, no
  per-voice filters — this is a volume/pitch/loop mixer and nothing more.
- `[design]` No per-voice send to a music bus, and no master gain: the host has one, and the
  engine mix in `src/audio.c` already has its own master.
- `[design]` `sfx_stop` is immediate, not faded. A held loop that should die gracefully should be
  ramped with `sfx_set(gain 0)` and then stopped; `sfx_stop` is the hard cut.
- `[inferred]` Nothing maps a game event id to a bank/sample pair. The `.csi` files under
  `SOUND/EVT_SYS/` are the retail candidate for that (see the task-D doc, section 8), but their
  meaning is not decoded, so the group table above is naming evidence only.
- `[design]` The demo's script is a listening check, not a mixdown anyone should ship: gains were
  picked to let the owner hear each group, and it deliberately overlaps sounds.

## 11 Files

- `src/sfx_play.h`, `src/sfx_play.c` — the mixer.
- `src/sfx_groups.h` — group → bank table.
- `tools/sfx_play_test.c` — unit test (synthetic data only).
- `tools/sfx_play_demo.c` — renders a scripted sequence to a WAV (output stays out of the repo).
- This document and `M8-SFX-PLAYER-INTEGRATION.md`.
