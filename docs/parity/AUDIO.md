# Engine and impact audio

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

Full-throttle callback output (RMS in s16 units; >2 kHz energy from Hann FFT):

| RPM | Old RMS | New RMS | Old >2 kHz % | New >2 kHz % |
|---:|---:|---:|---:|---:|
|1000|6209.0|2973.5|0.03|0.03|
|3000|7913.0|2728.2|0.01|1.86|
|6000|5645.4|3026.0|0.89|10.60|
|8000|6088.6|2877.1|2.07|9.80|

The brighter result is the selected recording, not added equalization or a gain
boost. This establishes a GOLF routing fault; it does not establish that every
car's audio is retail-identical. Cars without matching sweeps retain the existing
fallback. Authored per-car event routing is not fully decoded.
