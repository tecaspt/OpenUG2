# Career shops

## Data evidence

[data] The first GLOBAL/GLOBALB.BUN 0x80034A10 container holds 24 shop records
in 0x34A12, each 160 bytes. Type at +0x50 correlates with platform at +0x40:

| Type | Meaning | Platform | Opening stages / counts |
| --- | --- | --- | --- |
| 0 | Garage / crib | CRIB01.BIN | 0: 1 |
| 1 | Graphics / paint | PAINTSHOP01.BIN | 1: 1, 2: 1, 3: 1, 4: 1 |
| 2 | Body | PARTSSHOP01.BIN | 1: 2, 2: 1, 3: 1, 4: 1, 5: 1 |
| 3 | Performance | PERFSHOP01.BIN | 1: 2, 2: 1, 4: 1, 5: 1 |
| 4 | Car lot | CARLOT01.BIN | 0: 1, 2: 1, 4: 1 |
| 5 | Car specialties | AUDIOSHOP01.BIN | 1: 1, 2: 1, 4: 1, 5: 1 |
| 6 | Uniques (name THE_UNIQUES_SHOP) | PARTSSHOP01.BIN | 10: 1 |

[data] All except THE_UNIQUES_SHOP have a zone and marker. Stage values
are the byte at +0x9D. [inferred] Open means stage >= that value; discoveries
and messages do not add an additional gate. The career has stages 0..5.

[data] tools/fng_dump.py locates these packages in FRONTEND/FrontB.lzc:
UI_ChooseCustomizeCategory, UI_GenericParts_Browser, UI_ChooseRimBrand,
UI_Rims_Browser, UI_ChoosePerformanceCategory, UI_ChoosePerformancePackage,
UI_BuyPerformanceParts, UI_PerformanceBrandSelect, UI_ChoosePaintCategory,
UI_Paint, UI_CustomNeonMain and UI_ChooseUniquePart (all .fng).
The performance category has 56 nodes and the paint category 69; their
retail headers resolve to Performance Shop and Graphics Shop respectively.
Package existence alone does not establish the shop's category population.

[data] English.bin contains questions at 0x0AAB553E (car lot), 0x78DAD861
(body), 0x85D2ED13 (graphics), 0x15D6B438 (generic shop), and 0x72988DBE
(garage). MINIMAP_PERFORMANCE, MINIMAP_BODY and MINIMAP_GRAPHICS resolve
to readable shop labels. Labels.bin identifies the body/graphics question entries as
SHOP_BLURB_PARTS_SHOP_1 and SHOP_BLURB_PAINT_SHOP_1, and the generic
question as ENGAGE_ENTER_SHOP_GENERIC. Their use as entry confirmations
is an application inference, not a recovered control-flow fact.

[data] Labels.bin + English.bin identify these ASK_ENTER keys:

| Type | Header | Message / title |
| --- | --- | --- |
| Crib | ASK_ENTER_CRIB_HEADER | ASK_ENTER_CRIB (garage question) |
| Body | ASK_ENTER_BODY_SHOP_HEADER | ASK_ENTER_BODY_SHOP (Body Shop title) |
| Performance | ASK_ENTER_PERF_SHOP_HEADER | no per-type ASK_ENTER question in Labels.bin |
| Graphics | ASK_ENTER_PAINT_SHOP_HEADER | no per-type ASK_ENTER question in Labels.bin |
| Specialties | ASK_ENTER_ICE_SHOP_HEADER | no per-type ASK_ENTER question in Labels.bin |
| Car lot | ASK_ENTER_CAR_LOT_HEADER | ASK_ENTER_CAR_LOT (car lot question) |

[data] ASK_ENTER_ICE_SHOP_HEADER resolves to Car Specialties Shop. Thus
ICE is a data-backed specialty alias; it is not a performance category.

[data] The 0x34A15 table has 30 records of 380 bytes, with header domains
0..9, 1..3 and 1..4 and four possible 92-byte descriptors. Other undecoded
career leaves include 0x34A1A/1C/1E. These cannot establish the absence of
prices or unlock rules. No decoded part-price or stage-unlock table is
available. [inferred] The price hook returns 0 pending a reliable decode;
no fabricated stage tiers are imposed. Geometry catalogue validity remains
separate from retail unlock eligibility.

## Application policy

[inferred] Career body and specialty configurations are stored with the
career save, independently of openug2_garage.bin. Missing configuration in
older career schemas migrates to stock; no Quick Race setup is imported.
Body shops offer supported exterior meshes, kit presets and rims;
specialty shops offer the supported engine-cover and trunk-audio meshes.
The crib exposes both sets. Compatibility is revalidated by the renderer
before the save callback commits an installation.

[inferred] Existing map legend variants are reused: car lot 0, graphics 1,
performance 2, body 3, specialties 4. No invented unique-shop marker is added.

## Save and entry contract

[inferred] Schema 3 TLV 6 contains one 88-byte configuration per owned car,
in ownership order: four little-endian i32 legacy geometry selectors,
eleven i32 part selectors, a zero-terminated 24-byte rim archive name and
an i32 rim style. Length/count, selector domains and rim names are checked.
Missing configurations migrate to stock. Unknown TLVs remain preserved.
Replacing the career car resets its configuration rather than importing a
Quick Race configuration. The host restores state if persistence fails;
the frontend then requests restoration of the previous rendered config.

[inferred] Body and graphics use their retail question text/hash pairs;
performance and specialties currently use the generic retail shop question.
Every question starts focused on No; Back cancels. A shop's filtered
category page returns directly to free roam on Back. Part selection keeps
the existing preview / acknowledgement / Keep / restoration protocol.

## Remaining fidelity limits

Paint/material palette ownership, vinyl composition and performance
component effects remain undecoded (see GARAGE.md). Their shop entry pages
must state that no installable choices are available; a successful entry
is not a successful paint or performance purchase. There is no claim of
complete retail customization parity until those choices and effects are
implemented and visually verified.

## Touch entry adaptation

[inferred] An engageable career shop sets TC_CTX_SHOP. The enabled overlay
stays visible in that context before the first finger input and after keyboard
or pad input; menus and questions suppress it. Its existing Enter hit circle
uses a dark backing, bright border and explicit ENTER label instead of a
triangle-only icon. This is a platform adaptation, not retail artwork.
The uppercase shop notice and stage-gated minimap/world-map icons identify
the destination. An explicit --no-touch still disables the overlay.

[inferred] Shop-zone detection has no speed gate: the entry action stays
visible while driving through a zone. Choosing entry uses the existing
question/stop flow. Race engagement retains its separate speed restriction.
Before touch input, only the advertised Enter control is drawn in a shop
zone. Once touch driving is active, the normal controls are drawn, with
pause/camera moved to the upper-left to keep the shop notice readable.

## Verification

[observed] Final desktop/GLES/iOS-host builds exit 0 with 24/29/29 warnings.
All nine requested test targets pass: career 816 checks, career-data 14396,
career-host 362, car-lot 29, game-flow 252 and touch-controls 313, all zero
failures; garage, frontend garage and frontend screen targets also pass.
Additional shop-dialog checks pass 35/35 and pause-menu regressions pass.
Host address/undefined sanitizers run with leak scanning disabled because
this environment's ptrace setup prevents LeakSanitizer startup; leak checking
is NOT RUN.

[observed] Every open shop-zone centre across stages 0..5 resolves at both
0 and 80 km/h and is not masked by an available race at that centre.
Shop-icon counts are 2/8/13/15/20/23. Both INV captures, with rear mirror off
and fresh user directories on the host GPU, are byte-identical to references.

[observed] All four new shop types pass touch entry, question/Yes, matching
page and Back to roam. Body saves KIT01; specialties saves engine-cover
STYLE01 (selector 102). Both use schema 3 and create no Quick Race garage
file. Paint and performance have no installable choices: their purchase
checks are NOT IMPLEMENTED, not passes. Overall retail customization parity
therefore remains partial.

[observed] Car-lot re-entry and crib entry/Back pass. S7 completes all 4201
frames with exit 0, including purchase and stage 0 -> 1 at the crib. That
career-start proof precedes the final drawing-only layout adjustment;
final S7S captures cover the revised lot layout.

[observed] iOS-host at 1170x643, scene scale .75 and the normal rear-mirror
setting shows a labelled Enter button before touch input, then active
controls clear of the readable notice. A real touch Enter event opens the
question, Yes opens the body page, Back returns to roam, and the world map
shows 13 icons including all eight stage-1 shops. Physical-device testing
is NOT RUN. Temporary data-discovery links are removed after the host run.

[observed] Reviewed 42 PNGs: 39 final-layout shots and three S7 logic-reference
shots. Local evidence outside the versioned tree: reports/parity/shops/
(build-*-v3.log, tests-v3.log, capture-v3-results.log,
regression-v3-results.log, touch-extra-v3-results.log, save-check.txt,
final-image-manifest.tsv and review-final/). Raw placement is a zone-input
fixture; its height can leave the car above the road. These captures prove
entry/UI/state transitions, not original shop interiors or cameras.

## Follow-up data-only investigation

[data] Cross-checking the 30 records in 0x34A15 against Labels.bin gives
nonempty performance component identities at record offsets 16, 108, 200
and 292. These are descriptor +4 for the four 92-byte descriptors. Thus
the descriptors contain actual component language hashes, rather than
only opaque references. The corresponding descriptor +8 integers span
0..8000; a price interpretation is plausible but remains [inferred].
Descriptor +12 spans 0..8 and is not a proven career-stage gate.

[data] Descriptor +48, +52, +56 and +60 include IEEE floating-point
values such as -1 and positive fractions. In particular, the earlier
blanket interpretation of +64/+68 as small signed reference indices is
incorrect for this installed data: these fields include float bit patterns.
Descriptor +64/+68 at the first descriptor have signed ranges -1..57 and
-1..58. Header category/level pairs are unique across all 30 records.

[observed] This establishes named component composition candidates, but
not their activation, unlock, handling or engine attribute mapping. The
existing power-level implementation and documentation draw partly on
executable research and cannot supply the missing data-only proof.
Paint palette identity, finish mapping and per-stage vinyl eligibility
also remain unresolved. Purchases, save extensions and cold-start proof
for these two shop types remain unimplemented; no purchase pass is claimed.
