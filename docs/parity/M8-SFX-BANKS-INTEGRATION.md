# M8 SFX banks — Makefile lines for the orchestrator

`src/sfx_bank.c` is a standalone reader: nothing in the game calls it yet, so it is deliberately
**not** in `SRC` and `src/sfx_bank.h` is **not** in `HDRS`. Add the targets by hand — the branch
author may not edit `Makefile`.

## 1. Add to `Makefile`

Insert after the existing `vinyl-census:` block (the other data-census tool, same shape: pure C,
no SDL, build-only). Recipe lines are tab-indented, as everywhere else in this file:

```make
# Retail sound-effect banks (M8): standalone .abk reader, decodes every sample to s16 PCM.
# Not linked into the game yet — the engine bank path in src/audio.c is still the only
# consumer of retail audio. See docs/parity/M8-SFX-BANKS.md.
sfx-bank-test: tools/sfx_bank_test.c src/sfx_bank.c src/sfx_bank.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tools/sfx_bank_test.c src/sfx_bank.c -o build/sfx_bank_test
	./build/sfx_bank_test
.PHONY: sfx-bank-test

# Census over every *.abk under DATAROOT/SOUND (needs retail data, like vinyl-census):
#   make sfx-bank-census && ./build/sfx_bank_census "$(DATA)"
# Add `--wav DIR` to write each decoded sample as a mono s16 WAV for listening.
sfx-bank-census: tools/sfx_bank_census.c src/sfx_bank.c src/sfx_bank.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tools/sfx_bank_census.c src/sfx_bank.c -o build/sfx_bank_census
.PHONY: sfx-bank-census
```

`build/` is gitignored and `make clean` already removes it, so neither target needs anything else.

Notes on the lines above:

- `$(CFLAGS)` is enough. Neither tool needs `SDL_CFLAGS`/`SDL_LIBS`, `-lz`, or `-lm` (no `math.h`
  in either file), so the link line is plain `$(CC) $(CFLAGS) -Isrc ... -o build/...`.
- `DATA ?= .` already exists, which is why the census run line can say `"$(DATA)"`.
- There is no aggregate `test:`/`check:` target in this Makefile, so nothing else needs updating to
  register `sfx-bank-test`. If one is added later, `sfx-bank-test` belongs in it: it is synthetic
  only and needs no data files.
- The census target deliberately does not run the binary, matching `vinyl-census:` and
  `route-coverage-audit:` — those need a data directory, and `DATA` defaults to `.`.

## 2. Run

```
make sfx-bank-test                                    # prints "sfx_bank_test: N checks, 0 failures" + PASS
make sfx-bank-census
./build/sfx_bank_census "$(DATA)"                     # one line per bank + a total block
./build/sfx_bank_census "$(DATA)" --wav DIR           # same, plus a WAV per decoded sample
```

`DIR` must be outside the worktree (`build/` would do) — WAV exports are never committed.
Exit status: `0` clean, `1` something failed to open or decode, `2` bad usage.

Sanitizer pass, the same shape as the flags above but without `make`:

```
cc -O1 -g -std=c99 -Wall -Wextra -Wno-unused-function -D_DEFAULT_SOURCE -fsanitize=address,undefined \
   -Isrc tools/sfx_bank_test.c src/sfx_bank.c -o build/sfx_bank_test_asan
cc -O1 -g -std=c99 -Wall -Wextra -Wno-unused-function -D_DEFAULT_SOURCE -fsanitize=address,undefined \
   -Isrc tools/sfx_bank_census.c src/sfx_bank.c -o build/sfx_bank_census_asan
```

Both were run over all 176 retail banks with no compiler warnings and no sanitizer report.

## 3. Optional: register the diagnostic in the developer guide

`docs/DEVELOPER_GUIDE.md` §11 lists one row per census tool; a matching row would be:

```markdown
| sound-effect banks (`.abk`) | `make sfx-bank-census && ./build/sfx_bank_census DATAROOT [--wav DIR]` |
```

## 4. For whoever wires the sounds into the game

- Use `sfx_bank_open_file` → `sfx_bank_index_count` / `b->count` → `sfx_bank_decode`. Allocate
  `frames` `int16_t`s per sample; `frames` is the sample count, one frame is one sample, mono.
- `decodable == 0` means "this library cannot" (stereo, a codec other than EA-XA, zero frames, or a
  stream that does not fit the bank). Retail SFX data has none of those; the engine bank that
  `src/audio.c` already loads does not go through this code and should not.
- 15 records alias another record's stream offset, so a stream length must never be computed as
  "next record's offset − mine" without allowing a zero difference.
- `src/audio.c` still has the unbounded filter-index read described in §4 of
  `docs/parity/M8-SFX-BANKS.md`; `src/sfx_bank.c` guards it. Fixing `audio.c` is a separate change.
