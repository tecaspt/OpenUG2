# Career vinyls: the retail data and what OpenUG2 does with it

Retail paints a car's vinyls from three places: a per-car `CARS/<CAR>/VINYLS.BIN` catalogue,
the career's unlock flags and prices, and the `UI_ChooseVinylLayer.fng` screen. Each is
described below with the measurement that supports it, tagged `[data]` for something read out
of the shipped files and `[inferred]` for something the data implies but does not state.

## The catalogue file

`[data]` `CARS/<CAR>/VINYLS.BIN` is an EA TPK. Its header block carries magic `0xb3310000`,
and inside it a slot table with magic `0x33310003` of 24-byte records:

```
1113 hidden  compositing parts of another design (NAMEA/NAMEB/NAMEC, NAME_MASK)
 660 sellable base designs, of which the screen can hold 512 (FEV_MAX): 148 unreachable
  10 hidden  named slots with no retail family: GOLF_AD_BESTBUY, GOLF_AD_OLDSPICE,
             GOLF_AD_CINGULAR, GOLF_AD_EDGE, GOLF_AD_D3, GOLF_AD_SHINESTREET_MAS,
             GOLF_AD_SHINESTREET, GOLF_DEBUG_MASK, GOLF_NFSU2 -- billboards and a debug
             slot, not shop stock
```

SKYLINE has 1786 slots, 1783 of which HUFF-decode. Every slot in the file decodes to exactly
`dec = 350748` bytes, and every one of them is a 512x512 image, so the catalogue ships a fixed
canvas per slot rather than one size per design.

`[data]` Each payload ends with a 144-byte record:

```
offset  field
0x00    name[24]   the design token, EA-truncated to 24 characters
0x18    key        uint32   matches the slot's key, which is what validates a slot
0x38    w          uint16
0x3a    h          uint16
0x84    format     uint32   a 0x29/0x22-style code; real textures spell "DXT1"/"DXT3"
```

The 24-character truncation is visible in the names themselves: a token that retail spells
`..._MASK` arrives as `..._MAS`. Anything that matches on these names has to remember it;
`src/nfsu2.h` reads the record and `src/main.c` hashes the token, so both work from the name as
stored.

`[inferred]` The stored mip chain is **not** a full 8-bit pyramid. For the 512x512 case the
payload leaves `350748 - 144 - 262144 = 88460` bytes for the chain, while a full 8bpp pyramid
would need 349525. No square size satisfies `w*w + (mip8(w,w) - w*w) + 144 == 350748`, and the
DXT-chain hypotheses (43704, 10936) do not fit either. The chain length is therefore a constant
of the format, not a function of the base size - so `n2_car_slot_header()` bounds-checks the
record (`n + 144 <= dec`, dimensions in 8..2048, key match, name starts A-Z) rather than
recomputing a chain size. An earlier draft of that check demanded an exact pyramid and rejected
every slot in the game.

## What one design is

`[data]` A design is a token plus its colour-region slots. For a design `NAME` the catalogue
carries `NAMEA`, `NAMEB` and `NAMEC` beside the base `NAME`, and a soft-alpha plane
`NAME_MASK`. Measured over SKYLINE's catalogue, every `NAME<ABC>` slot sits fully inside the
footprint of its base (inside-ratio 1.000), which is what makes the three channels regions of
one decal rather than separate decals.

`[data]` The design's own palette ships all-zero. Colouring therefore cannot come from the
artwork; it happens when the layers are composited, which is why the screen asks for a colour
per channel and why the composite is a two-pass job in `build_car_vinyl()`: the A/B/C regions
are resolved first, then the base is filled in, skipping the pixels the regions already cover.

`[data]` Channel count comes from the token, not the art. `ART_F_<n>` and
`<CAR>_ART_F_<n>` - the artFactory house styles - name one colour; every other design names
three. `cr_vinyl_design_colors()` implements that and the host records the answer in
`FeVinylDesign.colors`, so the screen module never has to ask the career layer.

`[data]` **Every** design token carries the car's own folder name as its first segment:
`GOLF_TEAR_002`, `SKYLINE_WILD_059`, `BENZ300SL_VINYL_HOOD_ART`. Measured with
`tools/vinyl_rows` over GOLF's 1783 named slots, not one is spelled as a bare family token.
That makes the car folder a prefix to skip rather than part of the category, and it is why
`cr_vinyl_category()` takes the car as an argument: with the prefix stripped by whole segment
(`GOLF_`), a token resolves the same as the bare spelling a test or a language key would use.
No design family token collides with a car folder name, so the strip cannot misread a family -
the check is `make vinyl-rows`, whose output is only meaningful against the 64 folders in `CARS/`.

`[data]` What one car actually sells (GOLF, `tools/vinyl_rows $OPENUG2_DATA GOLF`):

```
1783 of VINYLS.BIN's 1786 slots are named. Of those, for GOLF:

```
1113 hidden  compositing parts of another design (NAMEA/NAMEB/NAMEC, NAME_MASK)
 660 sellable base designs - 148 of them beyond the screen's 512-row cap (FEV_MAX)
  10 hidden  named slots with no retail family: GOLF_AD_BESTBUY, GOLF_AD_OLDSPICE,
             GOLF_AD_CINGULAR, GOLF_AD_EDGE, GOLF_AD_D3, GOLF_AD_SHINESTREET_MAS,
             GOLF_AD_SHINESTREET, GOLF_DEBUG_MASK, GOLF_NFSU2 -- billboards and a debug
             slot, not shop stock
```

`make vinyl-catalog-check` runs `tools/vinyl_catalog_check` over all 29 installed cars and fails
the build on a regression; its GOLF line is
`slots=1786 named=1783 sell=512 three_colour=66 parts=1113 no_category=10 CAPPED`, where `sell`
saturates at the 512-row cap, so read it as "the screen is full", not as a count. Parts are
detected by the shop's own test (strip `_MASK` or a trailing A/B/C and find the base in the same
file); an earlier draft of this table said 1384 parts, from a check that compared 24-byte name
fragments over a truncated list, and that number was wrong.

The two figures the game logs for the same car, `vinyl layers opened designs 390 colours 60 hidden
1393`, are not catalogue counts and must not be read as one. `career_vinyl_shop()` stops its loop
once the snapshot is full (`o->ndesign < FEV_MAX`), so it never walks all 1783 slots and the pair
describes a screen state rather than a car: the line fires on the paint screen's VINYL row, which
sits one page above the design list, so `designs` is the layer page's list and `hidden` whatever
the previous call happened to examine. They come from different snapshots, which is why they do not
sum to the census. This task has not measured the walk slot-by-slot, so no split of the 1783 into
"listed / rejected / never examined" is claimed here.

What *is* measured is the whole catalogue, from `make vinyl-shopcensus`, which runs the identical
filter chain without the cap: GOLF gives `parts=1113 no_category=10 narrowed_by_channels=560
sell=512 capped=148`, and 1113 + 10 + 512 + 148 = 1783 accounts for every named slot. Note that the
sellable set is cap-order-dependent - the loop fills in `n2_car_tex_keys()` order, so which 512
designs a car lists is decided by slot order, not by any property of a design - and that
`tools/vinyl_catalog_check` counts 660 designs by a set-based pass over the same file. The census
table above is the account to reconcile against; the game's log answers "what did this visit show".

`[data]` `n2_car_tex_name_by_key()` requires `cap >= 25` and writes its NUL at `out[24]`, so any
caller whose buffer is exactly 24 bytes gets zero names and no error - the failure is silent and
looks like an empty catalogue. The game's own table is 32 bytes per name
(`g_vinyl_names[4096][32]`). Note also that the EA 24-character truncation lands on the `_MASK`
suffix itself: `GOLF_CAPONE_MASK` arrives as `GOLF_CAPONE_MAS`, which is why the part test above
matches a five-character tail rather than six.

`[inferred]` Identity is the upper-cased hash of the token (`cd_hash` upper-cases), and no two
cars' key sets overlap, which is why one hash table per car is enough and why the catalogue is
cached per car (`g_vinyl_car`): the shop snapshot and the renderer must be reading the same
car's file.

## Unlock rules and prices

`[data]` The career gates vinyls through three unlock ids in the 0x34A1C table:

```
id    gates                                            price shown
76    vinyl colours at all                              -
80    the tier-1 categories a shopper may buy from      the gate's `b`
83    the tier-2 exclusive art                           the gate's `b`
```

The price a design or colour costs is the `b` field of the gate that unlocks it, so a category
that is not open has no price to display and the screen refuses it before showing an invoice.

`[data]` Categories are named by `VINYL_CATEGORY_*` language keys. Two spellings are traps:
`VINYL_CATEGORY_MANUFACTURER` renders as "Sponsor", and the top plate's key is
`VINYL_CATEGORY_TOP_LAYER` - plain `_HOOD` and `_TOP` do not exist. `cat_keys[]` in
`src/fe_vinyl.c` carries all 23 verified spellings.

## The layer screen

`[data]` `UI_ChooseVinylLayer.fng` is 70 nodes, all 70 of which are bound by name in
`src/fe_binding.h` (`fe_UI_ChooseVinylLayer`), verified by `make fe-binding-check`. Language
keys used are all present in the shipped English table:

```
0x2ac7d02d CHOOSE_VINYL_LAYER       0xc034ce3d PAINT_CHOOSE_VINYL_COLORS
0x70b11cf1 CHOOSE_VINYL             0x31fbc167 ADD_TO_SETUP
0x06d7902c CHANGE_OR_REMOVE         0xb9ac83eb REORDER_LAYERS
0x3298ccdb BANK                     0x420aa71a TOTAL_COST
0xed56be1f INSTALLED                0xd1ac60be NO_FUNDS
0xc9f7f7e5 LOCKED                  0xda15d7e5 ALREADY_INSTALLED
```

`[data]` Node ids the screen drives: the four layer thumbnails are the art slots
`0x4b4e3888`..`0x4b4e388b`, authored stacked ten pixels up and left of each other, so the stack
is numbered from underneath (Bottom Layer, Middle Layer 1, Middle Layer 2, Top Layer) and the
selected layer is the one that ends up on top. The layer names are `0x27b8f0dd + layer`. Icon
plate `0xb04669e3`, lock badge `0xa2f11b13`, invoice lines `0x9ff84a88` / `0x1c663aa6` /
`0x2f5e7da3`, option title `0x5e7b09c9`, reorder hint `0xd999b6e7`.

## Flow, as implemented

`[inferred]` The retail screen is one screen with the choice nested inside it: a layer row, a
category carousel that stays on screen while you browse designs, and a colour page. OpenUG2
models that as three pages in `fe_vinyl.c` - `FVT_PAGE_LAYER`, `FVT_PAGE_DESIGN`,
`FVT_PAGE_COLOR` - because the design list and colour list are carousels the retail package
drives from one place.

The entry point is the graphics shop's own VINYL row, not a separate shop: retail leaves the
paint colour list open when that row is chosen, and `fem_paint_wants_vinyl()` reproduces that,
so CC_GRAPHICS still opens on paint categories and only that row walks into the layer screen.

Two Accepts buy anything, matching retail's invoice: the first arms the line, the second pays.
Back unwinds the invoice, then the colour page, then the design list; Back on the layer row
leaves to roam. A layer that already wears a design answers Accept with retail's own
"change or remove" question, and the second Accept takes the vinyl off for nothing.

Colour channels are addressed by catalogue index (`colour_sel`), never by row position, so the
invoice and the live preview cannot disagree. A one-channel design never offers channels B and
C, and a request never carries a colour the catalogue cannot name.

The category tab survives a close/re-open (`g_vinyl_cat` in `src/main.c`), because retail leaves
the carousel where the shopper left it. It is deliberately not part of the save file - a save
records what a car wears, not where the shopper was browsing.

## Save

`[data]` The layer state rides the existing `CareerState` TLV chain as tag 9, 16 bytes per
layer across four layers, so 64 bytes per owned car, and only for a car that actually wears
something: the tag is emitted only when some owned car has a non-empty design. That keeps the
schema at 5 and keeps a no-vinyl save byte-identical to one written before vinyls existed.

## Rendering and its limits

`[data]` Compositing needs no shader change. The engine already has a single-texture decal
path (`mix(uColor, t.rgb, t.a)` in `src/shaders/ug2.frag`), reached from `src/main.c` for
`N2_CAR_BODY`/`MISC` meshes that carry no texture of their own. A composed decal is uploaded
with `upload_tex_keyed()` and clamped with `render_texture_clamp()`.

Known limitations, stated plainly:

- **One decal texture, so one layer at a time.** `g_car_vinyl_tex` is a single slot and the save
  state keeps four layers. The screen lets you shop all four layers and remembers all four, but
  the renderer composites the one layer the screen is showing. Showing several at once needs
  either a multi-texture decal path or one composited texture that walks the layers, and both
  are bigger changes than this piece.
- **The `_MASK` planes are not applied, and they are not one thing.** The name suggests a
  soft-alpha plane, and compositing alpha by it is what a first attempt did — but measured on
  GOLF the plane is not a single kind, so it was reverted rather than shipped on a guess
  `[data]`: `GOLF_AEM_SCORPION_MASK` is a second low-index *shape* (15 distinct levels, 12901
  of its pixels inside the design), `GOLF_TEARS_804_MASK` is a 256-level ramp in which only
  340 of 13096 pixels reach >=224, and `GOLF_AEM_AUDIOBAHN_MASK` holds only 3606
  non-background pixels against a design that covers 10812. Scaling alpha by that last plane
  would erase most of the design instead of feathering its edge. Retail must therefore choose
  per design by a rule this data does not settle. **Consequence: edges are hard where retail
  fades, and some designs may be missing a second shape layer.** The measurements are in
  `reports/parity/vinyls/` (`vmask.py`, `valpha.py`).
- **Design colour is a synthesised ramp, not authored shading.** A vinyl palette *ships
  all-zero* and the game recolours from the player's colours, so the composite writes a dark
  cut-vinyl ramp over the shape and tints the bought regions (see the reader note at
  `src/nfsu2.h` "palette indices (vinyls)"). The index stream backs this up: every covered
  pixel of the sampled designs sits at index <= 64, so there is no authored gradient to lose.
- **Design icons stay hidden for most designs.** The design token carries no icon art, and the
  screen hides the icon plate (`0xb04669e3`) rather than inventing artwork for it.

## Verification

`tools/fe_vinyl_test.c` covers the flow without retail data (62 checks): the tab list is only
the categories the catalogue can fill; locked and unaffordable designs are refused before an
invoice; the two-step buy carries the design, the layer and every chosen colour; Back unwinds
without leaving a preview behind; removal carries no design and costs nothing; house styles
offer one colour; `fvt_refresh` keeps page, row and layer; and the `FeMenu` host returns
`FEM_REQ_VINYL_BUY` and stops previewing once the transaction settles.

`tools/career_test.c` covers the rules and the save format without data (1319 checks), including
the retail spellings the catalogue actually contains (`GOLF_TEAR_002` categorises as TEAR while
`FLAME_005` on car `FL` stays FLAMES, and a design whose car segment is empty categorises as
nothing), and a maximal save whose 4088-byte unknown TLV must survive a round trip. The unknown-
TLV copy in `cr_serialize()` is bounded and refuses rather than walk past the buffer; without the
bound ASan reports a stack-buffer-overflow on that path.

`make vinyl-catalog-check` runs over all 29 installed cars and fails on a regression; it SKIPs
with 77 when `OPENUG2_DATA` is unset rather than pretending to have checked.

The render claim is measured, not inferred. `g_dbg.vinyl_binds` / `vinyl_paints` count, per decal,
how many body/misc meshes fell back to the decal texture and how many were painted at all; the
SCRIPT line prints them as `vinyl=binds/paints`. `reports/parity/vinyls/render-ab2.sh` runs one
profile that owns a bought design twice, once normal and once with `--no-vinyl`, each into its own
directory:

```
f=420  on  vinyl=6392/14664      off  vinyl=0/14664
boot frame  AE(on,off) = 0      garage frame AE(on,off) = 476 px, all inside 29x21 at +844+462
```

The boot frame is bit-identical, so the two arms are otherwise the same run; the garage frame
differs only in a small block on the car body, which is where a cut-vinyl decal belongs. `binds`
non-zero with `paints` non-zero is the condition for the decal to be able to show at all - if
binds were 0 while paints were not, every panel would carry its own texture and the decal could
never appear.

The roam frames of the same profile differ by ~17000 pixels between arms *and* between two frames
of one arm, because free-roam traffic animates; whole-frame AE there measures the traffic, not the
decal, and no render claim is made from it. The tree's existing frozen-capture contract (`--shot`
with `--heading`, which sets `WorldCapturePolicy.freeze_motion`) applies to a world2 run, and
`freeze_motion` gates only initial placement, not the per-frame traffic step, so it cannot freeze
a scripted career run; making the roam frame reproducible is open work.
