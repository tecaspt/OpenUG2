# U2PK v1 — the PSP asset container

U2PK is the on-cartridge container for every preprocessed game asset. It is
written once by `psp_convert` on a Linux box and read (never parsed) by the PSP
runtime, which points the GE straight at the bytes: no decode, no realloc, no
per-frame work. A file is a 32-byte header, a section table, and 16-byte-aligned
sections. Everything is little-endian, which the PSP and the converter host both
are, so no field is ever byteswapped.

This document is the contract. `shared/u2pk.h` mirrors it with packed structs and
`_Static_assert`s; `shared/u2pk_read.c` enforces it on every byte it trusts.
Both the runtime and the host tests use the same reader.

## Verified format semantics (measured, not assumed)

The vertex encoding is only meaningful if the GE agrees with it. Every row below
was measured on the emulator we test with (PPSSPP v1.20.4, software renderer) by
`tests/psp/vfmt.sh` before this file was frozen; the position and UV divisors
were then cross-read out of the emulator's own vertex decoder
(`GPU/Common/VertexDecoderCommon.cpp:865` for positions, `:338` for UVs, both
`value * (1.0f/32768.0f)`). Re-run the probe whenever pspsdk or PPSSPP changes.

| Assumption | Evidence | Result |
|---|---|---|
| `GU_VERTEX_16BIT` position = s16/32768 | probe Q1: a batch of local ±0.5 m with `pos_scale` 2.0 measures 59 px at 4 m = exactly 1.00 m | confirmed; s16/16 would be 2048x off screen |
| `GU_TEXTURE_16BIT` UV = u16/32768, so **32768 is one repeat** | probe Q3 with UV 0..65535 and TexScale 1.0 tiles the texture exactly twice, horizontally and vertically | confirmed; 65535 is uv 2.0, not 1.0 |
| TexScale/TexOffset apply as `uv = u16/32768*scale + offset` | decoder `:397` `uv = uvdata/32768 * uScale + uOff` | the batch decode is exactly the GE's own arithmetic |
| v=0 samples the FIRST row of the texture data | probe: reading up from v=0, the first marker row hit is texture row 0 | the converter must NOT flip rows (GL uploads the same top-left-origin rows unflipped, so both builds agree) |
| `GU_COLOR_5650` bit order | probe Q1/Q2: vertex colour `0xF800` renders **blue**, `0x07E0` renders green | bits 11..15 = B, 5..10 = G, **0..4 = R** — same channel order as the `0xAABBGGRR` CLUT. Pack as `(r>>3) \| ((g>>2) << 5) \| ((b>>3) << 11)` |
| 12-byte vertex stride with `GU_INDEX_16BIT`, from a base that is 4- but not 16-byte aligned | probe Q3 vs Q4: identical bounding boxes | PPSSPP is indifferent; the converter still pads (below) because it is not |

Three consequences that are easy to get wrong:

* Positions and UVs are **normalised** 16-bit values, so `pos_scale` is in metres
  and `uv_scale` is in texels-per-repeat — no further division happens anywhere.
  `GU_VERTEX_16BIT` is *not* 16.16 fixed point and *not* 1/16 units: a runtime
  that assumed either would be wrong by 2048x or 2047x.
* `u16 = 65535` is uv **2.0**, not 1.0. A batch that wants exactly one repeat
  spans 0..32768. Getting this backwards samples the texture twice across every
  panel and looks like a plausible but wrong car.
* A 5650 vertex colour has R in the *low* bits. The naive PC-RGB565 packing
  (`(r>>3)<<11 | b>>3`) swaps red and blue on every baked-lighting value and on
  the whole car-paint tint, while still rendering a recognisable car.

## Header — 32 bytes

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | char[4] | magic | `"U2PK"` |
| 4 | u16 | version | 1 |
| 6 | u16 | kind | 1 = car, 2 = region index, 3 = cell |
| 8 | u32 | section_count | entries in the section table |
| 12 | u32 | total_size | whole file, including padding |
| 16 | u32 | flags | 0 |
| 20 | u8[12] | reserved | 0 |

## Section table — section_count x 16 bytes

Starts at offset 32. Entries are in section order; the reader must not assume an
entry's offset is greater than the previous entry's, and validates each one.

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | type |
| 4 | u32 | offset (absolute, multiple of 16) |
| 8 | u32 | size (bytes actually used by this section) |
| 12 | u32 | count (elements; meaning depends on type) |

Every section starts on a 16-byte boundary, so the runtime can hand `offset` to
`sceGuTexImage` directly: the Cars pool it reads the file into is 16-byte aligned,
and aligned + aligned stays aligned.

### Section types

| Type | Name | count | size |
|---|---|---|---|
| 1 | BATCHES | number of `Batch` | count x 64 |
| 2 | VERTS | number of `Vertex` | count x 12 |
| 3 | INDICES | number of u16 | count x 2 |
| 4 | TEXTURES | number of `TexHeader` | count x 32 |
| 5 | CLUTS | number of palettes | count x 1024 |
| 6 | CARINFO | 1 | 112 |
| 7 | COLLIDE | (M4) | |
| 8 | CELLTAB | (M4) | |

Type 4 is the one special case: `TexHeader.data_offset` is an absolute file
offset pointing at bytes that sit *after* the TEXTURES headers and *before* the
next section, and belong to no section table entry. They are still validated
(`data_offset + data_size <= total_size`) and still 16-byte aligned.

## Vertex — 12 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | u16 | u |
| 2 | u16 | v |
| 4 | u16 | colour (5650) |
| 6 | s16 | x |
| 8 | s16 | y |
| 10 | s16 | z |

Field order is the GE's required order (texture, colour, position). Draws use
`GU_TEXTURE_16BIT | GU_COLOR_5650 | GU_VERTEX_16BIT | GU_TRANSFORM_3D`, so the
12-byte stride is what the GE itself steps through.

## Batch — exactly 64 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | first_vertex |
| 4 | u32 | vertex_count |
| 8 | u32 | first_index |
| 12 | u32 | index_count |
| 16 | s32 | texture (-1 = none) |
| 20 | u32 | mode (0 opaque, 1 cutout alpha>=128, 2 blend) |
| 24 | f32[3] | pos_scale |
| 36 | f32[3] | pos_offset |
| 48 | f32[2] | uv_scale |
| 56 | f32[2] | uv_offset |

Decoding, per axis and per UV axis:

```
metres = s16 / 32768 * pos_scale + pos_offset
uv     = u16 / 32768 * uv_scale  + uv_offset
```

The UV half is what the GE computes itself from `sceGuTexScale`/`sceGuTexOffset`,
so those two floats are passed through unchanged. Note that on real hardware the
GE holds TexScale/TexOffset in a 12.4 fixed-point register, so the value it uses
is this float rounded to a multiple of 1/16 and clamped to <= 4095.9375. The
converter keeps `uv_scale` at or below `range/1.99` (normally <= 2), so that
rounding is at most a 3% error on a fraction of one texel; it is recorded here
because it is a hardware-only effect that PPSSPP's software path does not show.

`pos_offset` is the batch bounding-box centre and `pos_scale` its half-extent
(minimum 0.001), so the s16 range is used fully and the values stay O(1) metres.
Because `pos_scale` is the half-extent, `|(p - offset)/scale| <= 1` for every
vertex of the batch: the s16 field only reaches ±1.0 m of scaled distance and
anything outside saturates. A batch that skipped its own bounding box would
silently clamp its own corners onto the centre line.

### Batch run padding

`first_vertex` is multiplied by 12 and `first_index` by 2 to form the addresses
handed to `sceGumDrawArray`, and a 12-byte element size shares only a factor of 4
with the GE's 16-byte fetch granularity. The converter therefore pads each
batch's runs so both pointers stay 16-byte aligned:

* each batch's `vertex_count` run is padded up to a multiple of **4 vertices**
  (48 bytes, so the next `first_vertex * 12` is a multiple of 16);
* each batch's `index_count` run is padded up to a multiple of **8 indices**
  (16 bytes).

Padding bytes are zero, are never referenced by a batch, and count towards
`VERTS`/`INDICES` section sizes but not towards any `vertex_count`/`index_count`.
The cost is at most 3 vertices and 7 indices per batch. PPSSPP was measured
indifferent (probe Q3 vs Q4); the real GE is documented to be less so, and the
runtime is the one place a hardware-only failure would show up as a garbage car.

> **Resolved contradiction.** The milestone brief lists BATCHES as "count x Batch
> (48 bytes)" and CARINFO as "one CarInfo (64 bytes)", then defines Batch as
> "exactly 64 bytes" and CarInfo as "exactly 112 bytes". The two summary numbers
> are the sizes *before* the trailing UV / wheel fields were added to the layout;
> the field-by-field layouts are self-consistent and end exactly where their own
> "exactly N bytes" label says, so this file uses 64 and 112. `shared/u2pk.h`
> asserts both.

## TexHeader — exactly 32 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | u16 | w |
| 2 | u16 | h |
| 4 | u16 | psm (`GU_PSM_T8` = 5, `GU_PSM_DXT1` = 8) |
| 6 | u16 | swizzled (1 for T8, 0 for DXT1) |
| 8 | u32 | data_offset (absolute, multiple of 16) |
| 12 | u32 | data_size |
| 16 | s32 | clut (index into CLUTS, -1 = none) |
| 20 | u32[3] | reserved = 0 |

v1 stores T8 + CLUT only. DXT1 is defined so a later milestone can add it without
a version bump. T8 data is swizzled, so `w` and `h` must satisfy the GE: power of
two, `w >= 16`, `h >= 8`.

## CarInfo — exactly 112 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | lod_count = 2 |
| 4 | u32[2] | batch_first (LOD0 = player, LOD1 = AI) |
| 12 | u32[2] | batch_count |
| 20 | u32 | wheel_batch_first |
| 24 | u32 | wheel_batch_count |
| 28 | f32[4][3] | wheel_pos (FL, FR, RL, RR hub centres, car model space, metres) |
| 76 | f32 | wheel_radius |
| 80 | f32[6] | body_box (x0, x1, y0, y1, z0, z1) |
| 104 | u32[2] | reserved = 0 |

LOD0 and LOD1 batch runs are disjoint and both come before the wheel run; the
reader checks all three ranges against the BATCHES count.

## Reading rules (`u2pk_open`)

`u2pk_open(buf, len, &view)` returns 1 when every rule below holds and 0
otherwise, with `view.err` pointing at a static string naming the first failure.
Order matters: each rule may only look at bytes an earlier rule proved in range.

1. `len >= 32`, magic is `U2PK`, version == 1.
2. `total_size == len`.
3. `section_count` is such that the table itself fits in `len`.
4. For each section: `offset % 16 == 0`, `offset + size <= len` (no overflow —
   both are checked as 64-bit), `size >= count * struct_size` for its type.
5. Exactly one CARINFO, at least one BATCHES/VERTS/INDICES for kind 1.
6. CARINFO fits in its section, `lod_count == 2`, and the LOD0/LOD1/wheel batch
   runs are disjoint and inside the BATCHES count.
7. Every batch: `first_vertex + vertex_count <= VERTS count`, the same for
   indices, `vertex_count <= 65536`, `texture` is -1 or a valid TEXTURES index,
   `mode <= 2`, and every `pos_scale`/`uv_scale` component is finite and > 0.
8. Every texture: `data_offset % 16 == 0`, `data_offset + data_size <= len`,
   `clut` is -1 or `< CLUTS count`.

It never reads a byte it has not proved in range, and it does not copy: the view
holds borrowed pointers into `buf`.
