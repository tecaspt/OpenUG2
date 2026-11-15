# Customize / Garage

[observed] Task J starts at 486ce08. Worker owns only the scoped garage/FE/config files; host and build
integration is supplied separately. Retail assets/dumps/captures remain outside the repository.
[inferred] Preview debounce is0.3 seconds. The host acknowledges each serialized request. Preview
never changes the kept configuration. Accept keeps only after host success; Back requests the entry
snapshot and waits for successful restoration. Failed loads retain the last displayed config and
report a diagnostic. Inputs cannot change an in-flight final Keep/Restore operation.
[data] Local KIT/STYLE mesh variants and supported shared library sockets supply options.
[inferred] Only fully assembled CPU scenes with finite vertices, valid indices, positive triangles,
and a matching selected part/kit are exposed. GPU texture/upload proof is separate.
[observed] Visual parts and rims are implemented and rendered on desktop/GLES. Per-profile saves
pass synthetic/sanitizer checks and desktop/GLES persistence proof. Priorities1–3 are complete.
Paint/vinyl and performance research limitations are recorded below.

## Retail presentation and scope

[data] UI_ChooseCustomizeCategory has55 objects. UI_GenericParts_Browser has202; its application
image pool repeats hash87257585, masterGUIDe2958/hashef8bb088, and titlehash6c9e933e. A separate
binding validates explicit pool identities instead of treating duplicate names as unique.
[data] SHOP_ICON_BODY/PERFORMANCE/GRAPHIC/INTERIORS and per-part textures are present in the installed
asset catalogue. Language and texture keys differ for headlights, taillights and muffler tips;
these are bound independently. Prices, total cost, placeholder player/bank and unavailable locks
are hidden because this slice supplies no purchase/unlock data.
[inferred] Visual options are grouped into kit presets followed by every slot with alternatives.
Kit presets follow the existing KITnn host behavior; the English caption distinguishes them from
unsupported KITW/wide/carbon variants. Per-option tiles reuse the genuine category icon; the world
car supplies the actual geometry preview. Socket compatibility is not recovered retail shop eligibility.
[inferred] Catalogue filtering builds each choice over the stock base. Mixed configurations are
validated again by the host before acknowledgement. Rejected combinations retain the previous scene.
The current-world front-quarter camera and HUD suppression are preview adaptations; no retail garage
world/camera behavior or original category order is claimed. Performance, paint and interiors hub
entries remain disabled; renderable engine/audio parts are available within Visual as requested.

## Priority1 checks

[observed] garage_test passes desktop, N2_GLES and ASan/UBSan: option bounds, debounce, stale serials,
coalescing, failed loads, final-operation input lock, Keep and restoration. LeakSanitizer is NOT RUN
under this ptrace environment; the initial sanitizer attempt failed there and was rerun with leak
scanning disabled while address/undefined instrumentation remained enabled. Save/rename coverage is recorded under priority3 below.
[observed] fe_garage_test passes desktop/GLES: navigation,20-option catalogue, restore handshake,
invalid catalogues and synthetic parts-binding identity rejection. Existing fe-screen, fe-binding,
fe-profile, fe-polish, fe-options, game-flow, postrace(9) and pause-menu vectors pass. No new warnings
originate in owned code; untouched baseline host/backend warnings remain.
[observed] garage_check passes29 player cars and10363 catalogue entries, each independently rebuilt
into valid CPU geometry with its selected kit/part. This does not claim GPU/texture/pose validation
of every entry; the representative full game captures below provide separate GPU proof.
[observed] The integration patches were applied only to an external scratch copy and built for
OpenGL desktop and GLES. Both game runs exited0 with an explicit scripted quit. Host logs show
KIT01 preview/Keep, KIT02 preview, then touch Back restoring KIT01. Initial attempts uncovered
placeholder cost/HUD, an incorrect camera API argument and direct-launch CLI flags; those failed or
superseded runs are not the final PASS. Final proof uses --car GOLF --menu and isolated user storage.
[observed] Reviewed640x480 PNGs under the private report root are
reports/garage/desktop/{hub,slots,parts-before,parts-after,kept,before-cancel,restored}.png and the
same names under reports/garage/gles. Desktop/GLES contact sheets and the kept-frame review confirm
visible stock→KIT01→KIT02→KIT01 geometry and tap-reachable Back without gameplay gauges.
[observed] The production host is not wired by this worker; GARAGE-INTEGRATION.md supplies exact
source/build changes; later sections record the completed units and remaining limitations.

## Priority 2 — rims

[data] Runtime archive enumeration finds 115 renderable, diffuse-resolvable STYLEnn choices across
24 named wheel archives. The checker rebuilds each candidate independently; all29 player-car
visual catalogues are also rechecked after correcting the finite-vertex validator to the loader's
five-float vertex stride (authored normals occupy a separate tail).

[inferred] Visual > Rim Styles opens UI_ChooseRimBrand, then UI_Rims_Browser. Brand groups use
archive names with RIMS_BRAND language captions where present. Styles display their STYLEnn
identity. Available VISUAL_RIMS_BRAND art is used; other archives use the genuine VISUAL_PART_RIMS
category texture, rather than inventing a brand logo. This is an application mapping, not verified
retail brand ordering or eligibility. Spinner archives are static geometry under the existing rim
renderer; spinning effects, prices, unlocks and wheel-size selection are not implemented.

[inferred] Selecting a style previews after0.3s; Keep commits on host acknowledgement. Back from
styles restores the previous full config and returns to brands; Back from brands returns to parts.
The host stages all rim GPU/texture resources before changing the body and swaps only after all
preparation succeeds.

[observed] Model tests pass desktop/GLES and ASan+UBSan (LSan disabled for the existing ptrace
limitation). FE navigation tests pass desktop/GLES; all seven requested FE regressions and the
mandatory pause-menu regression pass. Runtime CPU checks pass115rims and29cars/10363visualchoices.
Initial desktop screen review caught an icon/selection scroll mismatch; gc_snap_to corrects it.
The corrected desktop/GLES captures were reviewed: brand focus, style preview, Keep and touch Back
restore all PASS. The GLES250:quit run exited0. Desktop was intentionally stopped after its final
restore capture to release memory before the separately gated GLES build; no clean desktop quit
is claimed for this run. Final images: `reports/garage/{desktop,gles}/rim-brands.png`,
`rims-before.png`, `rims-after.png`, `rims-kept.png`, `rims-before-cancel.png`, `rims-restored.png`.

## Priority 3 — per-profile, per-car persistence

[inferred] Garage state uses a separate `openug2_garage.bin` within the selected profile directory.
The existing profile/settings format is unchanged. The file uses the existing20CM container with
an application header: little-endian magic0x5241474f and schema1. A-block extension bytes remain
opaque. B is ordered TLV records: u16 tag, u16 reserved, u32 payload length. Tag1 has a120-byte
prefix: car name[32], four legacy config i32 values, eleven part i32 values, rim brand[24], rim
style i32. Fields are encoded individually. This schema is OpenUG2 policy, not a retail save format.

[inferred] Updates preserve all unknown TLVs, known-record tails, reserved header bytes, A extensions,
other cars and unchanged string padding. A missing car appends a zero-padded record. Every known
record is validated, including records for other cars. Duplicate car keys, invalid fields,
truncation, checksum mismatch, trailing bytes, schema0, and files exceeding1MiB are refused;
newer schemas are incompatible. Failure leaves caller outputs and the existing save unchanged.

[inferred] Keep saves only after a matching successful host acknowledgement. A save failure queues
an explicit restore of the previous config; a failed restore remains editable and reports failure.
Profile/root replacement is refused during a transaction. Serial allocation persists across screen
rebuilds so stale acknowledgements cannot confirm a later Keep. Saves reload the latest document,
write an exclusive temporary file beside it and replace it atomically. Only an actual missing file
or profile means absent; unreadable, linked or special files are refused. Simultaneous writers are
not supported. Profile rename carries Garage and career bytes opaquely before deleting the old
profile, including unknown, corrupt and newer Garage documents. Other regular auxiliary files
are copied opaquely too; directories, special files and destination-name collisions are refused
before mutation. If old-folder cleanup fails after deletion begins, the complete new copy is kept.
A lazy first save reloads a profile created by another menu after FE opened, preserving its bindings
and unknown bytes instead of overwriting it with stale defaults.

[observed] Pure store/model tests PASS on desktop/GLES and ASan+UBSan. FE save-failure recovery,
stale-acknowledgement and profile rename/opaque-copy fixtures PASS desktop/GLES and ASan+UBSan.
All seven required FE regressions and pause-menu tests PASS. LeakSanitizer remains disabled for
the existing ptrace limitation. Scratch host builds and all four persistence game runs PASS
(desktop/GLES first save and cold restart, each exited0). Desktop Quick Race Car Select also
loaded G35 stock and restored the Golf's saved config on return. Windows filesystem runtime
checks are NOT RUN. Production host/build files remain the orchestrator's responsibility.

## Paint and vinyl scope (priority 4)

[observed] The current body draw path accepts an RGB `uColor` (main.c near6490 and10513;
shaders/ug2.frag near215). Body RGB rendering therefore exists. This is not evidence of a
retail paint-choice mapping, finish/material selection or paint-category contract.

[observed] Vinyl loading already reads each car's VINYLS.BIN (main.c near2110), but the current
draw path applies the selected decal only to certain body meshes without their own texture
(main.c near10495). Layer stacking and retail color ownership are unfinished; see
VEHICLE_CUSTOMIZATION.md, Vinyl Colors and Layers, and FORMATS.md near1440. The existing vinyl
palette notes concern vinyl image indices; they do not identify the UI_Paint body-color palette.

[inferred] Paint/vinyl pages remain disabled. No guessed RGB swatches, whole-scene tint or fake
retail material effect is added. The remaining work is to recover the actual paint/category
color records and their material mapping, plus per-layer vinyl colors/composition, before a
retail picker can be bound and persisted. This priority is research-only here; paint/vinyl UI
and rendered selection checks are NOT RUN. Owner captures of category/color/finish transitions
would help distinguish those choices from the existing debug RGB override.

## Performance-part research (priority 5)

[data] A bounded chunk-header probe of the owner's GLOBAL/GLOBALB.BUN finds two0x80034A10
containers. Their0x34A15 children are bytewise equal, each11400bytes, exactly30records of380bytes.
Across those records, u32 fields at offsets0,4,8 respectively have domains0..9,1..3,1..4. Each
record has room for four92-byte descriptors starting at12. For the declared descriptor count,
signed fields at descriptor offset0,64,68,72,76,80,84,88 all lie in-1..67. No out-of-range
header/count/index field was found. These are structural observations, not recovered effects.

[inferred] The repeated domains are consistent with category, level and component-count headers,
followed by component references and exclusion/requirement references. Field roles are provisional;
no activation order, prerequisite algorithm, handling multiplier, price, unlock or gameplay effect
is inferred from these values. The existing provisional350Z documentation is used only to locate
candidate fields; its behavior/composition rules are not adopted as original-game findings.

[data] The0x34A1A and0x34A1C payloads are respectively1488 and3440bytes in both copies. The first
copy contains708bytes of0x34A1E and288bytes of0x34A1F; the second copy has empty children at those
IDs. Their record boundaries and semantics remain undecoded. Identical length or divisibility
alone does not establish a record stride.

[observed] The current career reader consumes0x34A11/0x34A18/0x34A1D, not the remaining tables.
The local structural probe was independently rerun after env.sh. Only aggregate facts and field
layouts are documented here; raw bytes, decoded per-record tables and probe outputs remain local.
No performance category is enabled and no vehicle physics or career logic changes in this task.

## Final verification and artifacts

[observed] The runtime catalogue sweep passed29player cars /10363visual choices and115rim styles.
This is CPU geometry/selection proof plus diffuse decoding for rims, with representative GPU
proof from the reviewed captures; it is not validation of every material or attachment pose.
The final checker also compiled and returned SKIP77 for an empty/unset data root. SKIP is not
an installed-data PASS. The normal asset-walk implementation is unchanged from the full sweep.

[observed] Latest source tests PASS desktop, -DN2_GLES, and ASan/UBSan. They cover store round trips,
unknown TLVs/A extensions/known tails/padding, duplicate/corrupt/newer refusal, atomic writes,
Keep/restore/save failure, stale acknowledgements, per-car separation, opaque profile rename and
profiles created by another menu after FE opened. All required fe-screen, fe-binding, fe-profile,
fe-polish, fe-options, game-flow and postrace tests PASS; pause-menu tests also PASS. Owned code
has no new warnings under the focused -Werror checks. Untouched baseline warnings remain.

[observed] Persistence screenshots were reviewed under both report backend directories:
`reports/garage/{desktop,gles}/{save-stock,save-kit-preview,save-kit-kept,save-rim-preview,save-kit-rim-kept,save-cold-reloaded}.png`.
The desktop additionally has `save-other-car-stock.png` and `save-golf-returned.png`. The first
save contains GOLF KIT01/NFSU STYLE02; cold startup restored that configuration on both renderers.
Desktop host logs record GOLF body1/rim2 → G35 body0/rim1 → GOLF body1/rim2 through real Car Select.
Earlier sections list the category/parts/rim screen images. All captures are640x480 and remain
local; no retail-derived images or dumps are committed.

[observed] Full-game rendered proof used snapshot bb8af06 with the documented scratch host patch.
The later b012d27 lazy-profile guard changes no UI/renderer path and was separately compiled/tested
on desktop/GLES and ASan/UBSan, including the new profile freshness regression. The documented
main.c diff was git-apply checked on a separate P2 source copy and its result exactly matched
the compiled scratch main. No production main.c or Makefile was edited. Production integration
and the orchestrator's end-to-end gates are not claimed as done.

[observed] Unit commits on parity/garage:5774dc0(visual parts),3360159(rims),bb8af06(per-car/profile
store),b012d27(profile freshness and integration commands), followed by final validation notes
and the empty-data checker guard. Every unit was pushed only to the private remote.

[inferred] Remaining retail fidelity work: category/brand ordering and shop eligibility/prices,
original garage environment/camera, animated spinner behavior, paint/material/color mappings and
vinyl layers, and semantics/effects of the remaining performance tables. Owner retail captures
would help resolve these policies; no prices, unlocks, paint swatches or performance effects
are invented by this implementation.
