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
scale expectation 1846.313 (−0.031 dB), so it does not explain the large loss of
upper harmonics in the hashed fallback.

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
