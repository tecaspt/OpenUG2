# M8 — wiring the sound-effect player into the build and the host

Companion to [M8-SFX-PLAYER.md](M8-SFX-PLAYER.md). The Makefile lines below are the deliverable:
`src/sfx_play.*`, `tools/sfx_play_*` and the two docs are all this task owns, so nothing here was
edited into the Makefile — the snippet was extracted from this file and built as-is to prove it
works.

## Makefile

Matches the existing tool-target shape (`vinyl-census:`, `camera-test:`): `@mkdir -p build`, one
`$(CC) $(CFLAGS) -Isrc …` line, `-lm` last, `.PHONY` after. `CFLAGS` already carries
`-O2 -std=c99 -Wall -Wextra -Wno-unused-function -D_DEFAULT_SOURCE`, and `DATA ?= .` already
exists, so nothing needs adding. No SDL headers or libraries are involved — `src/sfx_play.c` does
not include SDL — and no `-lz` either.

```make
sfx-play-test: tools/sfx_play_test.c src/sfx_play.c src/sfx_bank.c src/sfx_play.h src/sfx_bank.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tools/sfx_play_test.c src/sfx_play.c src/sfx_bank.c -o build/sfx_play_test -lm
	./build/sfx_play_test
.PHONY: sfx-play-test

sfx-play-demo: tools/sfx_play_demo.c src/sfx_play.c src/sfx_bank.c src/sfx_play.h src/sfx_bank.h src/sfx_groups.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tools/sfx_play_demo.c src/sfx_play.c src/sfx_bank.c -o build/sfx_play_demo -lm
.PHONY: sfx-play-demo
```

Run:

```
make sfx-play-test
make sfx-play-demo
./build/sfx_play_demo "$DATA" <reports-dir>/sfx/demo.wav        # DATA ?= . is the data root
```

There is no aggregate `test:` / `check:` target in this Makefile, so `sfx-play-test` stands alone
like the other `*-test` targets. `make clean` already removes `build/`, which is gitignored, so the
demo binary needs nothing special.

Both targets were built by extracting the fenced block above verbatim into a scratch makefile
behind the project's own preamble (`CC`, the two `CFLAGS` lines, `-D_DEFAULT_SOURCE`) and running
`make -f <scratch> sfx-play-test sfx-play-demo` — recipe lines, tabs included. The preamble's
`SDL_CFLAGS := $(shell sdl2-config --cflags)` prints
`make: sdl2-config: No such file or directory` on a host without SDL2 installed; that is inherited
noise from the existing Makefile, unrelated to these two targets, which never touch SDL.

The demo **refuses** an output path inside a git work tree: the WAV is decoded from retail data and
must never be committable. Write it under the reports tree, outside the repository.

To repeat the verification runs the sanitizer form is:

```
cc -O1 -g -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -fsanitize=address,undefined \
   -fno-omit-frame-pointer -Isrc tools/sfx_play_test.c src/sfx_play.c src/sfx_bank.c \
   -o build/sfx_play_test_asan -lm
cc -O1 -g -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -fsanitize=address,undefined \
   -fno-omit-frame-pointer -Isrc tools/sfx_play_demo.c src/sfx_play.c src/sfx_bank.c \
   -o build/sfx_play_demo_asan -lm
```

## How the host would use it

Order of operations, all of it outside the mixer's own file:

1. **At audio init.** `sfx_player_create(44100)` next to `audio_init()` (`src/audio.c:611`), store
   the pointer with the other audio state, `sfx_player_destroy` at shutdown. Pass the rate the
   device was actually opened at, not a literal, if that ever stops being 44100.
2. **At content load.** For each row of `src/sfx_groups.h` the host needs, join it to the data root
   and `sfx_bank_open_file`, then `sfx_clip_from_bank` for the samples it wants. Keep the banks and
   clips alive for as long as any voice might be playing them: a voice borrows `clip->pcm`, it does
   not copy it. Freeing a clip under a live voice is a use-after-free, and `sfx_stop_all` does not
   discharge it.
3. **In the audio callback.** Call `sfx_mix_s16(adev_player, out, n)` once, at the end of
   `audio_cb`, after the per-sample loop that fills `out` (`src/audio.c:391-396` defines
   `out`/`n`). It adds into whatever is already there, so the engine keeps its output; if the
   engine produced samples that already reach the rails, effects will clip against them — that is
   the host's mix balance to manage, not the mixer's.
4. **From the game thread.** Wrap every `sfx_play` / `sfx_set` / `sfx_stop` / `sfx_stop_all` call in
   `SDL_LockAudioDevice(adev)` / `SDL_UnlockAudioDevice(adev)`. The mixer has no internal locking,
   and the existing `volatile float` global pattern is only safe for a single scalar. Holding the
   lock across a whole frame of effect updates is fine — those calls do no allocation and touch at
   most 24 slots.
5. **Handles.** Keep the handle returned by `sfx_play` for as long as the sound is conceptually
   alive (a held skid, a turbo loop), then `sfx_set` it as parameters change and `sfx_stop` it when
   the event ends. A handle that has died — finished, stopped, or stolen — is inert forever, so
   clearing your own copy to 0 after stopping is good hygiene but not required for safety.
6. **What `sfx_active` is for.** Polling whether a held sound is still going, e.g. to notice that a
   one-shot ended and its handle is dead. It takes a `const SfxPlayer *` and is safe to call
   wherever a read is safe.

Cost: one interpolated read plus one saturating add per live voice per output frame — 24 voices
worst case, 1024 frames per callback. Linear interpolation has no low-pass, so heavy pitch-up
(e.g. a wind loop swept to 1.5) will alias a little; that is audible only on tonal material, and
the retail groups that matter here — impacts, shifts, menu clicks — are broadband.

## Known adjacent issue, not touched here

`src/audio.c`'s own `eaxa_decode` uses the 4-bit filter index from a frame header straight into a
4-entry table without a bounds check. It is latent because no retail stream area byte has that
nibble above 3 `[data]`, and `src/sfx_bank.c` guards the same case. `src/audio.c` is read-only for
this task, so the note is left for whoever owns it.
