# Engine and impact audio

Two engine faults were established: reversed RPM clamps at descending sweep
endpoints, and GOLF missing its GTi road recordings. Impact playback also ignored
recording level and added random pitch, while sustained walls retriggered crashes.

Evidence uses DATA and project source only. Render with `make audio-ae-capture`,
then `SDL_AUDIODRIVER=dummy build/audio_ae_capture ROOT engine GOLF 3000 OUT.pcm`.
The test pauses the dummy device and calls its production callback synchronously.
PCM is mono signed 16 bit, 44100 Hz, 133120 frames, after a 45056-frame warmup
for engines. Keep recordings outside the repository. `make audio-ae-test` uses
synthetic recordings and measures a 1 kHz sine through the native grain mixer.

## Engine selection

The old filename substring search did not match GOLF to any sweep. It selected
CAR_28 via a name hash, then pitched its three loops against invented centers
1500/4000/7000 RPM. Those centers are not bank metadata. The bank's mono EA-XA
loops default to 22050 Hz. Their measured autocorrelation estimates are
75/50/16.679 Hz; the last estimate is ambiguous, demonstrating why these estimates
cannot establish authored layer RPMs. Loop tags end at frames minus one.

[data] The named GTi road acceleration sweep has rate 32000 Hz, 138358 samples,
RPM limits 2062.20–7349.30, 50 RPM intervals and 171 cycle-aligned grains.
[inferred] GOLF (Golf GTI) uses `GIN_GTi_Pull_01b_shrt.gin` and
`GIN_GTi_01b_DCL.gin`. Match this pair explicitly; a broad GTi substring would also
select the shorter drag recording. Other cars keep their selection, with lexical
tie breaking so filesystem order cannot change an equally short choice.

The native mixer uses the file's RPM position curve and grain points, pitches
by requested/recorded RPM and crossfades over 12 ms. Below/above the recorded
range it pitches the endpoint grain; idle at 1000 and redline at 8000 are thus
extrapolations, not recordings at those speeds. Gains and auxiliary sounds stay
unchanged. There is no engine low-pass stage. Linear 32000→44100 interpolation
measures 1839.644 amplitude for a 1 kHz input of amplitude 10000, versus the bus
scale expectation 1846.313 (−0.031 dB). The frequency audit below quantifies its remaining
rolloff across the upper band.

Full-throttle callback output (RMS in s16 units; >2 kHz energy from DC-removed Hann FFT):

| RPM | Old RMS | New RMS | Old >2 kHz % | New >2 kHz % |
|---:|---:|---:|---:|---:|
|1000|6209.0|2973.5|0.03|0.03|
|3000|7913.0|2728.2|0.01|1.86|
|6000|5645.4|3026.0|0.89|10.62|
|8000|6088.6|2877.1|2.07|9.82|

The brighter result is the selected recording, not added equalization or a gain
boost. This establishes a GOLF routing fault; it does not establish that every
car's audio is retail-identical. Cars without matching sweeps retain the existing
fallback. Authored per-car event routing is not fully decoded.

## Interpolation frequency audit

Synthetic tones at 24/32 kHz input, 44.1 kHz output; wanted-frequency gain
relative to the unchanged engine bus scale. Every tone's measured amplitude
agrees with the first-order-hold `sinc²(pi*f/source_rate)` response within 1%.

| Tone | 32000→44100 loss dB | 24000→44100 loss dB |
|---:|---:|---:|
|1000 Hz|-0.031|-0.053|
|2000 Hz|-0.118|-0.203|
|6000 Hz|-1.036|-1.831|
|10000 Hz|-2.936|-5.293|

Linear interpolation does attenuate the top of the recorded band, especially
near a 24 kHz recording's Nyquist limit. This quality limit remains explicit;
there is no claim of a transparent resampler. No DATA metadata identifies the
retail interpolation kernel. Retain the current algorithm while correcting the
demonstrated recording-selection and RPM faults. There is no added low-pass,
EQ or engine bus gain change. `make audio-ae-test` measures all eight tones,
the coast endpoint tone, collision gain/frequency and contact lifetime.

## Descending sweep endpoint pitch

[data] Deceleration RPM marker curves descend in sample position. Their first
and last cycle-aligned grains extend outside the marker curve in all 37
inspected descending recordings. The old `gin_rpm_at` fallback treated every
curve as ascending, assigning low RPM to the first deceleration grain and high
RPM to its last grain. Interior interpolation was correct.

For `GIN_240SX_Decel.gin`, marker endpoints are positions 81520→360 at
1120.7→7012.4 RPM. The first grain midpoint is 204.5, before the high-RPM
marker. At 8000 RPM that reachable grain was labeled 1120.7 instead of 7012.4,
playing 6.26 times too fast. Its >2 kHz energy falls from 60.58% to 3.11% when
pitched from the correct endpoint RPM. 350Z coast redline similarly changes
from 17.07% to 3.21% above 2 kHz. These changes remove wrong pitch/aliasing;
they do not add a filter.

The outlying final grain is also mislabeled in the old helper, but normal idle
selection in these examples chooses the preceding in-range grain. Thus steady
idle coast for 240SX and 350Z is byte-identical, as are their 3000/6000 RPM coast
renders. A header-only prediction that their idle would play too slowly is not
supported by the callback captures. The Golf recording change independently
addresses its missing road sweep and muffled full-load tone.

Clamp endpoint RPM according to marker direction. This shared processing fault
justifies changing native deceleration playback for other cars too; ascending
sweeps, interior interpolation, gains, filters and decoders stay unchanged.
Synthetic checks cover both marker directions and endpoint clamps. A synthetic
1 kHz endpoint coast grain remains 1 kHz, amplitude 1839.520 versus 1846.313
expected; the old clamp would slow that fixture to 143 Hz. Actual audible
changes depend on which endpoint grains the RPM-to-position map selects.

Closed-throttle callback output (RMS / >2 kHz energy):

| Car | RPM | Before RMS | After RMS | Before >2 kHz % | After >2 kHz % |
|---|---:|---:|---:|---:|---:|
|GOLF|1000|6208.92|3376.11|0.03207|0.00116|
|GOLF|3000|7255.96|3581.93|0.01826|0.01522|
|GOLF|6000|6488.64|3338.21|2.62609|5.87808|
|GOLF|8000|8125.32|2959.62|3.93221|8.85607|
|240SX|1000|662.45|662.45|0.01187|0.01187|
|240SX|3000|4184.52|4184.52|0.00579|0.00579|
|240SX|6000|2983.74|2983.74|1.71219|1.71219|
|240SX|8000|2951.93|2971.82|60.57904|3.11042|
|350Z|1000|2878.00|2878.00|0.00057|0.00057|
|350Z|3000|2775.76|2775.76|0.00504|0.00504|
|350Z|6000|3563.48|3563.48|1.48418|1.48418|
|350Z|8000|3513.85|3511.62|17.06694|3.21098|

Full-load 240SX/350Z/HUMMER captures and coast HUMMER captures remain
byte-identical at all four RPMs. No engine-only capture reaches a clipping rail.

## Native recordings for every playable car

The substring heuristic is gone; `src/audio.c` now carries an explicit model →
recording table. This section records how that table was derived, what it
changed, and where it stops.

### Where the association lives in the data

[data] The retail data contains **no** car → recording table. A case-insensitive
search of the whole install tree for `GIN_` hits exactly three files: the
installer list `filelist.txt` (a flat path list with no car records), the
language label file (`Labels.bin`, unrelated UI keys such as
`OPT_AUDIO_ENGINE_VOL`), and `SPEED2.EXE` — which is out of scope for this
clean-room effort. The `SOUND/EVT_SYS/*.csi` event-system files name engine
*event* strings (`CAR`, `CAR_SputOutput`, `CAR_SputOut`, `CAR_SWTN`) but no
recording names. The `CAR_nn_ENG_MB` banks carry no car identifier either.
Consequently a recording's only link to a car is its filename, exactly as
`docs/FORMATS.md` states, so **every row of the map is [inferred from filename]**.

Two independent record sets *are* data and are used to test each inference:

1. [data] each recording's own `Gnsu20` header (`rpm_min`, `rpm_max`, sample
   rate, and whether the RPM marker curve ascends or descends);
2. [data] each model's GLOBALB powertrain row (idle / redline / rev-limiter RPM),
   read through the project's existing `n2_global_physics_attr` record reader.

A mapping survives only if the recording's RPM envelope covers the model's tacho
band. All twelve mapped models pass: for example RX7 records 1597–7239 RPM
against an 850/8000/8500 row, and HUMMER records 1267–4783 against
800/5500/6000 — a low-revving truck engine, which is why the truck-family RPM
floor is lower than the sports-car floor. Full table in the evidence log.

### The map

| Model | Acceleration | Coast | Basis |
|---|---|---|---|
|240SX|`GIN_Nissan_240SX.gin`|`GIN_240SX_Decel.gin`|[inferred] name match, road take|
|350Z|`GIN_350Z_01d_shrt2.gin`|same recording, aliased|[inferred] name match; see below|
|ECLIPSE|`GIN_MitsEclipse_2000.gin`|`GIN_Eclipse_2000_DCL.gin`|[inferred] road pair replaces an aftermarket take|
|FOCUS|`GIN_FordFocus_Road.gin`|`GIN_Ford_Focus2_DCL.gin`|[inferred] "Road" is the road take|
|G35|`GIN_G35_350ZX_Comp_v2_AB.gin`|`GIN_Infinit_G35_DCL.gin`|[inferred] name match|
|GOLF|`GIN_GTi_Pull_01b_shrt.gin`|`GIN_GTi_01b_DCL.gin`|[inferred] unchanged from the Golf fix|
|HUMMER|`GIN_Hummer.gin`|same recording, aliased|[inferred] name match|
|MIATA|`GIN_Mazda_Miata.gin`|`GIN_Mazda_Miata_DCL.gin`|[inferred] name match|
|RX7|`GIN_Mazda_RX7.gin`|`GIN_Mazda_RX7_DCL.gin`|[inferred] name match|
|RX8|`GIN_RX8_Ramp_v1_CD.gin`|`GIN_Mazda_RX8_DCL.gin`|[inferred] the only RX8 pair|
|SKYLINE|`GIN_Nissan_Skyline.gin`|same recording, aliased|[inferred] name match, no coast twin|
|TT|`GIN_VR6_Jetta_M3.gin`|`GIN_VW_Jetta_M3_DCL.gin`|[inferred] name match, VR6 Jetta pair|

The table is keyed case-insensitively and the recordings are opened in the order
the old scan produced (acceleration first), so a car the old scan already matched
cannot move. `make engine-map-test` asserts exactly that, plus file existence,
parse success, envelope sanity, coast-aliasing and the fallbacks.

The fallback banks carry no usable identifier: the `CAR_nn_ENG_MB_SPU.abk`
prefixes hold executable content, which is out of scope, and their sample tables
expose only `BNKl`/`PT` rate, count and offset tags — no car name and no
recording name. One unrelated engine event string family does exist in the data
(`CAR_10_SWTN_MB.abk`, matching the `CAR_SWTN` string in
`SOUND/EVT_SYS/UG2_ENGINES_AEMS2.csi`), but nothing maps it to a model. So the
filename plus the two RPM record sets above are the whole evidential basis, and
no row is stronger than another in data terms.

### What actually changed

Because the retired rule was a *substring* search, nine of the twelve models were
already matched and stay byte-identical. Only two rows differ, and one of them is
a deliberate brief-mandated preference:

- **ECLIPSE** moved off `GIN_Eclipse_5Zigen.gin` (an aftermarket variant take)
  onto the `MitsEclipse_2000` road pair. The old rule reached it because
  "ECLIPSE" is a substring of `GIN_Eclipse_5Zigen.gin` and that filename is
  shorter than `GIN_MitsEclipse_2000.gin`. The road recordings also track the
  model's 850/7000/7500 row more faithfully. This is the same fault class as
  GOLF's drag-vs-road mistake, and the brief's "prefer road" instruction covers
  it.
- **350Z** no longer borrows `GIN_Nissan_350ZX_Jason_DCL.gin` as its coast
  sweep. That file is the tuned variant's coast take and contains that variant's
  entire acceleration sweep; measured, its last-decile RMS is 15069 against the
  selected recording's 18889, so the old cross-family pairing imposed a ~14.5 dB
  output step every time the driver lifted off. Aliasing to the model's own
  recording (the same mechanism HUMMER and SKYLINE use) removes the step. At
  full throttle the 350Z is unaffected and byte-identical.

`[inferred]` Neither change is confirmed by a data table — none exists — but
ECLIPSE is a variant-preference call and 350Z is a loudness-discontinuity call,
both testable from the files themselves.

### Cars that keep the bank fallback

32 installed models have no plausible native recording and keep the hashed
`CAR_nn_ENG_MB` path unchanged. All 84 recordings are accounted for in the
inventory: 21 used, 63 unused, each with a reason. The unused ones fall into:

- **models not installed** — Ferrari 360, Acura Integra/ITR, Chevrolet Cavalier,
  Ford Bronco, Honda Prelude, Porsche Cayenne, Mazda Protege, Nissan 300ZX,
  Toyota Trueno AE86 / Sprinter GTS, VW Cabriolet and Scirocco. A recording with
  no car to receive it stays unused; inventing an owner for it would be a guess
  with no supporting data at all.
- **the S2000 family (12 recordings; three of them — `HondaS2000_APEX_V1`,
  `S2000_5Zigen_v2`, `S2000_ApexChurch2F2F_v3_SH` — are named without the
  `GIN_` prefix).** The loader keys on the `Gnsu20` magic and the explicit map,
  never on a filename prefix, so the naming is only a cataloguing note, not a
  reason for exclusion. The reason is that there is no
  `CARS/S2000/GEOMETRY.BIN` in this install: the model is not drivable, so
  nothing can receive the recordings. If a S2000 body is ever installed, this is
  the first family to revisit.
- **competition variants of a mapped car** — `Eclip2000_v1_CD` (Eclipse ramp),
  `FordFocus_comp2` (Focus competition), `VW_Jetta_M4` (TT upgrade tier),
  `VW_Drag_GTi` (Golf drag). Road takes win per the brief.
- **no road recording exists** — Mustang (`Mustang_Drag`, `Mustang_Nscr_COMP`,
  `Mustang_Saleen_DCL`, `MustSal_Ramp_V2_CD`) and NASCAR takes only. MUSTANGGT's
  engine revs 800/6000/6500, but every Mustang take is a drag or ovals recording
  and `MustSal_Ramp_V2_CD` tops out at 5122 RPM, below the model's 6000 redline.
  Mapping the car to a wrong-sounding variant was rejected in favour of the
  fallback.
- **IMPREZAWRX against the WRX family.** The closest road take,
  `GIN_WRX_Street.gin`, ends at 6314 RPM against the model's 7000 redline, and
  `GIN_WRX_Street_DCL.gin` starts at 3916 RPM — no idle coverage. The other WRX
  files are aftermarket tunes. Left on the fallback rather than mapped to a
  recording that cannot reach the tacho band.
- **LANCEREVO8** — `GIN_Mits_5zigen_DCL.gin` is the only Mitsubishi-named coast
  take and has no acceleration twin, and "5Zigen" is an aftermarket variant. No
  mapping.

### Measured renders

Full-throttle renders through the production dummy callback, 45056 frames at
44100 Hz, RMS in s16 units and >2 kHz share of a DC-removed Hann power spectrum
(the AE method). "Source" is what the loader picked; `bank` is the fallback. The
before column is the retired selection rule, the after column this map.

| Car | Source | RPM | RMS before → after | >2 kHz % before → after | PCM |
|---|---|---:|---:|---:|---|
| ECLIPSE | sweeps | 1000 | 3692.13 → 3863.55 | 0.02278 → 0.00153 | changed |
| ECLIPSE | sweeps | 3000 | 3672.04 → 3684.97 | 0.85924 → 0.10475 | changed |
| ECLIPSE | sweeps | 6000 | 3655.23 → 3190.51 | 1.00624 → 2.17638 | changed |
| FOCUS | sweeps | 1000 | 3358.28 → 3358.28 | 0.03047 → 0.03047 | identical |
| FOCUS | sweeps | 3000 | 3326.24 → 3326.24 | 0.18441 → 0.18441 | identical |
| FOCUS | sweeps | 6000 | 2675.75 → 2675.75 | 7.52130 → 7.52130 | identical |
| G35 | sweeps | 1000 | 1988.34 → 1988.34 | 0.03307 → 0.03307 | identical |
| G35 | sweeps | 3000 | 3472.20 → 3472.20 | 0.09955 → 0.09955 | identical |
| G35 | sweeps | 6000 | 2882.15 → 2882.15 | 1.98105 → 1.98105 | identical |
| MIATA | sweeps | 1000 | 3625.41 → 3625.41 | 0.00271 → 0.00271 | identical |
| MIATA | sweeps | 3000 | 2989.51 → 2989.51 | 0.59956 → 0.59956 | identical |
| MIATA | sweeps | 6000 | 3133.98 → 3133.98 | 2.33555 → 2.33555 | identical |
| RX7 | sweeps | 1000 | 4177.07 → 4177.07 | 0.02762 → 0.02762 | identical |
| RX7 | sweeps | 3000 | 4161.61 → 4161.61 | 0.38881 → 0.38881 | identical |
| RX7 | sweeps | 6000 | 3146.25 → 3146.25 | 4.03582 → 4.03582 | identical |
| 240SX | sweeps | 1000 | 2607.24 → 2607.24 | 0.02605 → 0.02605 | identical |
| 240SX | sweeps | 3000 | 3047.04 → 3047.04 | 0.07702 → 0.07702 | identical |
| 240SX | sweeps | 6000 | 2668.26 → 2668.26 | 1.81739 → 1.81739 | identical |
| 350Z | sweeps | 1000 | 2516.81 → 2516.81 | 0.02000 → 0.02000 | identical |
| 350Z | sweeps | 3000 | 3243.88 → 3243.88 | 0.10117 → 0.10117 | identical |
| 350Z | sweeps | 6000 | 3588.98 → 3588.98 | 1.02866 → 1.02866 | identical |
| GOLF | sweeps | 1000 | 2970.00 → 2970.00 | 0.03021 → 0.03021 | identical |
| GOLF | sweeps | 3000 | 2725.96 → 2725.96 | 1.87858 → 1.87858 | identical |
| GOLF | sweeps | 6000 | 3026.01 → 3026.01 | 11.27755 → 11.27755 | identical |
| HUMMER | sweeps | 1000 | 1328.26 → 1328.26 | 0.34884 → 0.34884 | identical |
| HUMMER | sweeps | 3000 | 2146.81 → 2146.81 | 3.76371 → 3.76371 | identical |
| HUMMER | sweeps | 6000 | 3037.01 → 3037.01 | 7.59486 → 7.59486 | identical |
| RX8 | sweeps | 1000 | 3185.17 → 3185.17 | 0.03400 → 0.03400 | identical |
| RX8 | sweeps | 3000 | 2956.66 → 2956.66 | 0.59490 → 0.59490 | identical |
| RX8 | sweeps | 6000 | 2920.42 → 2920.42 | 1.83798 → 1.83798 | identical |
| SKYLINE | sweeps | 1000 | 35.25 → 35.25 | 3.77267 → 3.77267 | identical |
| SKYLINE | sweeps | 3000 | 3775.11 → 3775.11 | 0.02395 → 0.02395 | identical |
| SKYLINE | sweeps | 6000 | 3256.66 → 3256.66 | 1.58358 → 1.58358 | identical |
| TT | sweeps | 1000 | 3251.84 → 3251.84 | 0.00853 → 0.00853 | identical |
| TT | sweeps | 3000 | 4099.13 → 4099.13 | 0.07839 → 0.07839 | identical |
| TT | sweeps | 6000 | 2806.98 → 2806.98 | 4.19242 → 4.19242 | identical |
| SUPRA | bank | 1000 | 11207.14 → 11207.14 | 0.01066 → 0.01066 | identical |
| SUPRA | bank | 3000 | 9040.96 → 9040.96 | 0.04514 → 0.04514 | identical |
| SUPRA | bank | 6000 | 7711.59 → 7711.59 | 0.39160 → 0.39160 | identical |
| CELICA | bank | 1000 | 8349.39 → 8349.39 | 0.00161 → 0.00161 | identical |
| CELICA | bank | 3000 | 7681.93 → 7681.93 | 0.00168 → 0.00168 | identical |
| CELICA | bank | 6000 | 8033.76 → 8033.76 | 0.08905 → 0.08905 | identical |
| COROLLA | bank | 1000 | 5042.40 → 5042.40 | 0.17819 → 0.17819 | identical |
| COROLLA | bank | 3000 | 9040.67 → 9040.67 | 0.04524 → 0.04524 | identical |
| COROLLA | bank | 6000 | 7711.47 → 7711.47 | 0.39222 → 0.39222 | identical |
| MUSTANGGT | bank | 1000 | 8809.95 → 8809.95 | 0.00349 → 0.00349 | identical |
| MUSTANGGT | bank | 3000 | 6456.39 → 6456.39 | 0.98237 → 0.98237 | identical |
| MUSTANGGT | bank | 6000 | 7347.58 → 7347.58 | 1.05277 → 1.05277 | identical |

45 of 48 renders are byte-identical.

`make engine-map-test` prints these rows and writes the raw renders; the before
column comes from the same test built against the retired selection source. Every
"identical" row is a control, not a measurement coincidence — the PCM files are
byte-for-byte equal. `SUPRA`, `CELICA`, `COROLLA` and `MUSTANGGT` are included as
fallback controls: their bank renders are unchanged, and their bank RMS
(5042–11207) is roughly double the native-sweep levels, one measured facet of the
reported muffled sound.

`SKYLINE` at 1000 RPM is quiet (RMS 35) because its only recording starts at
1524 RPM and has no coast twin; the callback then extrapolates the endpoint
grain. That is pre-existing behaviour, unchanged here, and is the reason the RPM
envelope check in `engine-map-test` reports rather than hides envelope limits.

### Reproducing this section

`SDL_AUDIODRIVER=dummy ./build/engine_map_test ROOT` runs the checks; add
`--render CAR... [--dump-pcm DIR]` for the render rows. Build the before binary
from the retired `src/audio.c` with `-DEMAP_NO_MAP`, which keeps the GOLF pair
check and the renders but skips the table assertions. See
`reports/parity/engmap/` for `selection_diff.py` (old rule vs map over every
installed model), `rpm_coverage.py` (recording envelope vs GLOBALB tacho row),
`recording_inventory.py` (all 84 recordings with a use or a reason),
`z350_coast_check.py` (the 350Z coast loudness measurement) and
`render_table.py` (this table from the two logs). PCM dumps stay out of git.

## Impacts

[data] `SOUND/EVT_SYS/UG2_STITCH_AEMS.csi` names `AEMS_StichCollision`;
`SOUND/IG_GLOBAL/MB_Stich_Collision.abk` contains 94 mono EA-XA one-shots,
all at 36000 Hz, all carrying PT level tag 0x06 = 50. No loop tags occur.
The game already routed wall and vehicle hit controls to this bank.

[inferred] Interpret the level as a percentage, restricted to impacts. Playback
now applies 0.5 authored level, preserving the existing strength envelope
`0.25 + 0.55 * clamp(hit / 0.45)`. Pitch is 1 instead of random 0.92–1.08;
there is no demonstrated data basis for that modulation. Decoder, channel and
endianness are unchanged. Other effects do not consume the new level field.

[inferred] A wall contact rearms only after 120 ms with no inward contact,
in addition to the existing 180 ms cooldown. The old cooldown alone permitted
11 crash events during 120 ticks (2 s) of sustained high contact; now exactly
one plays. Brief gaps below 120 ms do not rearm. The 2.5 m/s inward-speed
threshold and hit strength `min(0.45, (inward - 2.5) * 0.025)` stay unchanged.
Consequently the light/medium/heavy boundaries correspond to 8.8 and 15.1 m/s
for walls. Separate vehicle contacts still supply their existing physics hit
value. The effects player retains its 80 ms global inter-impact guard.

The existing duration classes (<0.20 / 0.20–0.45 / >0.45 s) contain 28/41/25
clips. These remain **inferred**, as does random selection within each class;
duration does not establish surface or material. The input API carries no
surface identifier, so material-specific routing cannot be claimed. Authored
collision-event graph semantics are unresolved; this change preserves the
recordings' rate/level and fixes repeated-wall bursts, not a complete recreation
of the event graph. Embedded executable content in bank prefixes was excluded
from analysis.

Synthetic impact validation: 1 kHz input amplitude 10000 gives output amplitude
3999.742 at hit 0.45/master 1, expected 4000. Every rendered sample matches
native-rate PCM times 0.4 within one integer step. This verifies frequency,
gain, no random pitch, cooldown suppression and master mute. A 120-tick wall
contact produces one event and a separated contact rearms. Bank/parser and
player tests and the installed-bank sanitizer check also pass.

## Reproducing the evidence

Use the same capture harness for both binaries. The before sources are
`src/audio.c` and `src/sfx_game.c` from commit `cd6400e`; supply their external
copies to the compiler through `AUDIO_AE_ENGINE_SOURCE` and
`AUDIO_AE_GAME_SOURCE` (quoted include filenames). Link `src/sfx_bank.c` and
`src/sfx_play.c`, SDL2 and libm. The after binary is `make audio-ae-capture`.
The parser's added level field does not change decoded PCM, and the baseline
playback source ignores it.

Run `tools/audio_ae_evidence.py ROOT BEFORE_BINARY AFTER_BINARY OUTDIR` with
numpy installed. It writes before/after WAVs, source WAVs, per-case text,
`spectra-rms.csv`, `MEASUREMENTS.md`, engine bank/sweep header inventories and
capture binary hashes. `--reuse` retains completed captures while adding missing
cases and recomputing tables. The script verifies unchanged 240SX, 350Z and
HUMMER full-load engines byte for byte; HUMMER coast is also unchanged. Engine tests cover idle/3000/6000/redline at full and closed throttle; crash
tests cover light/medium/heavy single and repeated hits, plus single/sustained
wall contact. Original-rate source exports cover every collision sample and
the four selected Golf acceleration grains and twelve deceleration grains from
GOLF/240SX/350Z, including exact loop positions and recorded RPM.

Main-thread bus controls remain unchanged: engine master is
`0.16 + speed_fraction * 0.5` while driving, multiplied by the master and engine
options; road control is `speed_fraction² * 0.35` times master and tire options.
Offline engine comparisons use master/load 1 to isolate recording selection.
They do not claim an on-device listening result or identical retail event routing.

Fixed-seed collision callback captures (3.0186 s including the tail; master 1):

| Hit | Pattern | Before RMS | After RMS | Before/after clipped frames |
|---:|---|---:|---:|---:|
|0.05|single|773.75|393.37|0 / 0|
|0.20|single|2175.35|1106.16|0 / 0|
|0.45|single|2714.20|1379.93|0 / 0|
|0.05|every 8192 frames|4443.21|2000.18|0 / 0|
|0.20|every 8192 frames|9201.26|4009.02|27 / 0|
|0.45|every 8192 frames|15474.62|7587.88|6449 / 1|

The repeat test deliberately permits overlapping tails to measure headroom;
it is distinct from the sustained-wall test, which suppresses retriggering.
Removing a pitch RNG draw also changes subsequent random clip choices, so repeat
RMS changes include that variation. Single-hit cases retain the same clip and
compare native pitch/level directly. One clipped frame remains in the strongest
repeat stress case; no claim of a universal clipping-free effects mix is made.

At 20 m/s inward wall speed, the 3.0186 s single-contact capture measures
2662.32→1353.57 RMS (zero clipped frames both). Sustaining that contact measures
15250.28→1353.70 RMS and 6090→0 clipped frames. The retained quiet scrape bed
explains the small RMS difference between the new single and sustained cases.
