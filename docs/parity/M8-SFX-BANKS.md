# M8 — the retail sound-effect banks (`.abk`)

Scope: a standalone reader (`src/sfx_bank.c`) that opens any `.abk`, lists its samples and decodes
each one to 16-bit PCM, plus a census over every bank in the install. The game is untouched.

Clean-room: every claim below comes from reading the retail `*.abk` **data** files, from this
project's own existing code (`src/audio.c`), or from reasoning. No game executable, and no
decompiled or recompiled game code, was read at any point. `tools/sfx_bank_test.c` contains no
byte that came from a retail file — it builds its own banks.

Claim tags: `[data]` = observed in the retail banks; `[external-doc]` = public format notes;
`[inferred]` = our reading of the evidence, not proven.

## 1. Container: `ABKC`

A `.abk` file starts with `ABKC`. All fields below are `u32` little-endian. `[data]` = true in all
176 banks of the install unless stated.

| Offset | Observed | Note |
|---|---|---|
| +0x00 | `"ABKC"` then 4 version bytes | 171 banks `1,1,1,0`; 4 banks `1,1,0,3`; 1 bank `1,1,0,4` |
| +0x08 | 65536 (128 banks) or 131072 (48 banks) | a power of two, not a sample count. Purpose unknown `[inferred]` |
| +0x14 | equals the file size | |
| +0x18 | bank offset | the `BNKl` image; between 0xC80 and 0x1200 in this data |
| +0x1c | 92 in every bank | a constant, **not** a count |
| +0x20 | same value as +0x18 | duplicated field |
| +0x24 | bank size | |
| +0x30, +0x34, +0x38 | three offsets, all inside the file | point past the bank; contents not examined |

The area between the header and the bank offset is mostly binary: 7–27 % printable bytes, and
splitting it on zero bytes never yields a number of entries equal to the bank's sample count, so it
is **not** a table of sample names. `[data]` Its purpose is unknown. `[inferred]`

## 2. Bank: `BNKl`

The bank image starts with `BNKl`. `[data]`

| Offset | Observed | Note |
|---|---|---|
| +0x00 | `"BNKl"`, then a byte `5` | |
| +0x06 | `u16` = number of sample records **+ 1**, in 176/176 banks | one more than the records we find; a spare/null entry is the plausible reading `[inferred]` |
| +0x08 | `u32` = end of the stream area | always `bank_size - 16*n` with `n` in 0..7, so the area ends 0–112 bytes before the bank does |
| +0x18 | index table, `u32` per sample | see below |

**Index table.** Entry `k` (at bank+0x18+4k) gives the offset of the k-th sample header as
`bank + 0x18 + 4*k + entry[k]`. Verified on 176 of 176 banks, and the number of table entries
equals the number of sample records found in every bank. `[data]`

`src/sfx_bank.c` finds the records the way `src/audio.c` does — scan the bank for `PT\0\0`,
step 0x20 past a hit, keep scanning — because that is the walk the project already trusts. Raw
scanning alone is not enough: `PT\0\0` also occurs by chance inside stream data (in one bank, 23
false hits against 68 real records), so a candidate is only accepted when its tag list terminates
properly and carries a sample count and a stream offset, and the scan stops once it reaches the
lowest stream offset. With that, the count agrees with the bank's own index table on 176/176 banks
(the census prints a note if it ever disagrees; it did not). `[data]`

## 3. Per-sample header: `PT`

`PT\0\0`, then a list of `(tag u8, length u8, value big-endian)`, ended by tag `0xFF`.
Tag `0xFD` is a one-byte entry with no length and no value. The project's code also treats `0xFE`
as an end tag. Record width varies between 0x20 and 0x30 bytes; no tag value is wider than 4 bytes.
`[data]` (`PT` is also listed among EA chunk types in public notes — "EA SCHl Audio", Xentax wiki —
which names the chunk but does not document these tags. `[external-doc]`)

Tags seen across all 1513 sample records:

| Tag | Length | Values | Records | Reading |
|---|---|---|---|---|
| 0x06 | 1 | 50, 60, 70, 80, 90, 100 | 1500 | unknown; a level/gain in steps of 10 `[inferred]` |
| 0x13 | 1 | always 127 | 1513 | unknown; 127 = mid-scale, so possibly pan/pitch `[inferred]` |
| 0x1a | 0 (twice: 1) | 0; the two exceptions carry 61 | 742 | a flag that only appears together with 0x86/0x87 `[inferred]` |
| 0x80 | 1 | always 2 | 1513 | unknown; constant, so not a per-sample property |
| 0x82 | — | never present | 0 | channel count: absent everywhere, so every sample is mono by the API default |
| 0x83 | — | never present | 0 | codec: absent everywhere, so every sample is EA-XA by the API default |
| 0x84 | 1, 2 or 3 | 16000, 18000, 22050, 24000, 28000, 32000, 36000, 44100, 48000 — and twice 100 | 809 | sample rate in Hz; absent means 22050 |
| 0x85 | 2 or 3 | 319 … 60000-ish | 1513 | frame (sample) count |
| 0x86 | 0 (twice: 1) | 0; exceptions 2 and 3 | 742 | as 0x1a |
| 0x87 | 2 or 3 | `0x85 - 1` in 735 of the 742 records that carry it; the other 7 give `0x85 - 2` | 742 | last sample index, i.e. a loop end `[inferred]` |
| 0x88 | 4 | | 1513 | stream offset, relative to the bank start |
| 0x8a | 4 | always 0 | 1513 | unknown |
| 0x9c | 3 | always 57343 (= 0xDFFF, field `00 DF FF`) | 10 | unknown; only in a few banks |

Nothing about 0x9c is understood; only 10 records carry it.

The library reads 0x82, 0x83, 0x84, 0x85 and 0x88 and ignores the rest; it must, because the record
list is a TLV list and unknown tags have to be skipped, not guessed.

## 4. Stream data: EA-XA mono ADPCM

Same codec `src/audio.c` already decodes for the engine banks. `[data]` — it decodes 1513/1513
retail streams with audible results (see §6):

- 15-byte frame: header byte (filter index in the high nibble, `20 - shift` in the low nibble,
  so shift is 5..20) + 14 payload bytes = 28 signed nibbles, high nibble first.
- Filters: `c1 = {0, 240, 460, 392}`, `c2 = {0, 0, -208, -220}`, output
  `(nib * 2^shift + prev*c1 + prev2*c2 + 0x80) >> 8`, clamped to s16.
- Frame whose first byte is `0xEE`: predictor reseed (`2 x s16` big-endian at +1 and +3) followed by
  28 uncompressed `s16` big-endian samples at +5; the frame occupies 61 bytes.
- 28 samples per frame either way, so a stream of `n` frames needs at least `n * 15` bytes.

Hardening note: the filter index must be 0..3. The copied maths in `src/audio.c` indexes a 4-entry
table with a nibble, which is out of bounds for corrupt data; `sfx_bank.c` rejects the frame
instead. (Fixing `src/audio.c` itself is out of scope for this task; it is listed in the handoff.)

## 5. Census totals

`build/sfx_bank_census <DATAROOT>` over every `*.abk` under `SOUND/`:

```
banks              176  (open errors 0)
samples            1513
decodable          1513  (undecodable 0)
decoded ok         1513  (decode failures 0)
all-zero samples   15
implausible rate   2  (outside 8000..192000 Hz)
toc mismatches     0
audio seconds      1095.58
loudest amplitude  32768
```

`toc mismatches` counts banks where our walk found a different number of samples than the bank's own
index table; `implausible rate` counts records whose tag `0x84` value is outside 8000..192000 Hz
(see §6 — 2 records, and their seconds are excluded from the total above).

"Loudest amplitude" is the largest absolute sample value seen, so `32768` means a sample sat on the
negative rail, `-32768` — the one s16 value with no positive counterpart, reached either by the
decoder's clamp or, in principle, straight out of a raw `0xEE` frame.
37 of the 176 banks contain at least one such sample; 17 banks top out at `+32767` instead. Most
banks peak in the 28000–32000 range, which is what makes the all-zero ones below stand out. `[data]`

Rates actually used: 22050 (default where 0x84 is absent), 16000, 18000, 24000, 28000, 32000,
36000, 44100, 48000. `[data]`

## 6. What is not a plain sample

- **Nothing is undecodable in this data set.** No bank carries tag 0x82 or 0x83, so there is no
  multi-channel and no alternative-codec sample to report: the `decodable` flag is 1 for all 1513
  samples. The library still handles those tags (they mark a sample undecodable rather than
  pretending) because the format allows them and the API exposes them, and the census would list
  any bank where they appear — this did not occur, so no codec/channel tag values can be quoted.
- **15 samples decode to all zeros**, one per bank, all in the `SOUND/ENGINE/SWTN_CAR_*_MB.abk`
  family. Silence placeholders, presumably deliberately. `[inferred]`
- **2 records carry an impossible rate**: tag 0x84 stored in **one** byte with value **100**
  (`SOUND/ENGINE/SWTN_CAR_00_MB.abk` and `SWTN_CAR_10_MB.abk`, both at bank+0x8C). Every sibling in
  the same bank says 32000 Hz, and these same two records are the only records in the whole install
  where 0x1a and 0x86 carry a value, so the record is unusual in more than one way. It is not a
  plausible Hz value and the one-byte encoding is unique. The library reports the raw value as the
  API requires; the census excludes those samples from the seconds total and flags the bank.
  What the field means is unknown. `[inferred]`
- **15 records share a stream offset with another record** in the same bank (mostly
  `SOUND/ENGINE/*`). These are aliased entries: the same stream listed twice, sometimes with an
  identical record body. Anything that computes a stream length from "the next record's offset" must
  cope with a difference of zero. `[data]`
- Loop points (0x87 and friends) are recorded but **not verified** against the audio: nothing here
  proves where a sample loops. `[inferred]` Tag 0x87 is present in 742 of the 1513 records and reads
  `0x85 - 1` in 735 of them; the 7 that read `0x85 - 2` are `SOUND/ENGINE/CAR_04_ENG_MB_{EE,SPU}`,
  `CAR_05_ENG_MB_{EE,SPU}`, `CAR_22_ENG_MB_{EE,SPU}` (all three pairs store 21173 against 21171) and
  one record in `SOUND/IG_GLOBAL/ROADNOISE_00_MB.abk` (23126 against 23124). Nothing distinguishes
  those 7 records from the rest. `[data]`

## 7. Files and how they are built

- `src/sfx_bank.h`, `src/sfx_bank.c` — the reader. C99, no SDL, no globals, every read bounded.
- `tools/sfx_bank_test.c` — synthetic-only unit test (2-sample bank, exact `0xEE` frame, ADPCM
  frame checked against the decoder copied from `src/audio.c`, records that declare stereo or an
  unknown codec, every prefix of the file, a bank whose declared size is honest but whose stream is
  short, garbage, 4000 single-byte mutations, a file round trip). Prints
  `sfx_bank_test: N checks, 0 failures` and `PASS`.
- `tools/sfx_bank_census.c` — `sfx_bank_census DATAROOT [--wav OUTDIR]`; prints one line per bank
  with paths relative to `DATAROOT`, so the report carries no local paths.
- Makefile lines for the orchestrator: `docs/parity/M8-SFX-BANKS-INTEGRATION.md`.
- Listening exports (WAV, not committed, live outside the worktree in the task's reports
  directory): 1513 files, 58 MB total, one per decoded sample. Peak amplitudes of the first five
  `FE_MB` samples: 29350, 29917, 29443, 28591, 28794 (full scale 32767).
