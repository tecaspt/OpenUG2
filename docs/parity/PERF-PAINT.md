# Career performance and paint shops: data, rules, effects

Clean-room: everything below comes from the retail data files (GLOBAL/GLOBALB.BUN, language files, FNG packages)
and OpenUG2's own code. Tags: [data] read directly, [inferred] interpretation.

## Tables

| Chunk | Layout | Notes |
|---|---|---|
| 0x34A15 (career container) | 30 package records x 380 B: `i32 cat, i32 lvl, i32 n`, then n x 92 B descriptors [data] | 10 categories x 3 levels |
| 92 B descriptor | +0 `i32 comp` (0..67), +4 language key, +8 `i32 price`, +12 `i32 nbrand`, +16 `u32 brand[8]`, +48 `f32 axis[4]` (-1 unused), +64 `i32 excl[5]`, +84 `i32 req[2]` (always -1) [data] | 66 components (33, 34 unused) |
| 0x34A14 | 35 brands x 68 B: `char key[32], char display[32], u32 ug_hash(key)` [data] | all hashes verified |
| 0x34A1C | 86 unlock records x 40 B: `u32 id`, 3 x {`u16 a, u16 b, u8 type, u8 level, u16, u32 value`} [data] | see rules |
| 0x80034602 (top level) | parts database: 0x34604 parts x 14 B, 0x34606 names, 0x3460C attribute lists, 0x34605 key/value pairs [data] | paint = part with RED/GREEN/BLUE + group key 0xEBB03E66 |

Categories [data, from component name prefixes]: 0 weight, 1 transmission, 2 nitrous, 3 engine, 4 turbo,
5 suspension, 6 brakes, 7 ECU, 8 tires, 9 aero (no names, no prices: hidden [inferred]).
Packages: levels 1/2/3 = Street / Pro / Extreme (PERFORMANCE_PACKAGE_1..3); single parts are sold too
(PERFORMANCE_CHOOSE_SPECIFIC). Prices $100..$8000 = descriptor +8 [inferred: dollars].

Paint [data]: 450 colours. Body: GLOSS 60 (L1), METAL 60 (L2), PEARL 30 (L3, includes CUSTOMPAINT).
RIM, CALIPER, MUFFLER, HOSE, VINYL colours: 10 / 20 / 30 at L1 / L2 / L3. Each has RGB and GLOSS (128 paint,
100 vinyl colour).

## Rules (src/career.c)

- Unlock record level types: 6 = from the start, 0 = never, 1 = shop (value = shop hash; OpenUG2 unlocks when that
  shop is open [inferred: retail says "You have unlocked access" on discovery]), 2/3/4 = {u16 count, u16 stage}:
  unlocked past that stage, or in it with `count` wins / sponsor wins / URL wins [inferred, 3 strong, 2/4 medium].
- Performance category -> unlock id [inferred from SMS_UNLOCK pairs and shop texts]: engine 1, ECU 2, suspension 3,
  nitrous 4, transmission 5, weight/tires/brakes 6/7/8 (identical conditions), turbo 9, aero 10.
- Paint group -> unlock id [inferred]: body 76 (also hose and vinyl colours), rims 77, muffler 78, calipers 79.
- Prices: single part = descriptor price; package = sum of not-owned parts x 0.9 (LABEL_DISCOUNT "10% Discount")
  [inferred formula]. Megalow's 20% / levels 1-2 only (shop texts) is NOT implemented. Paint price = unlock record
  level field `b` [inferred, low confidence; a retail cost capture would settle it].
- Buying a component removes the components listed as exclusive with it [data: exclusions are symmetric].
- Upgrade fraction u[cat][axis] = clamped sum of installed components' axis shares [data: Street+Pro+Extreme sums to
  about 0.33 / 0.66 / 1.00 per category]. Level of a fraction: 3 / 2 / 1 at 0.995 / 0.655 / 0.325 [inferred].
- Save schema 4, TLV 7 per owned car: 68-bit component mask + 9 paint slot keys. Older saves load stock.

## Effects on the car (src/drive_stage1.c stage1_car_read_upgraded, per-car record [data])

The car table is one 0x890-byte record per vehicle (46 = 31 player cars + 15 traffic, whose fields are
shared template constants). NULL or an all-zero DrvUpgrade reproduces the stock reader byte for byte.

| Category | Effect | Evidence |
|---|---|---|
| Engine 3/0 | primary torque += u x master add-curve 0x530 (9 samples); shift/max rpm += level block header (0x560/0x5A0/0x5E0 +4/+8) | L1..L3 blocks are 0.34/0.68/1.00 x master; headers are floats (GOLF +500/+1000/+1500, 240SX +500/+1000/+2000 rpm) |
| Transmission 1/2 | gearbox block of the level (0x460/0x4A0/0x4E0, same layout as stock 0x2C0); level from axis 2 | L3 adds a 6th gear on 5-speed cars; ratios only differ from stock at the last gear |
| Tires 8/0 | axle grip lerps stock 0x1E0/0x200 -> max 0x650/0x670 by u | max >= stock on all 31 player cars; refused where it is not (all 15 traffic rear blocks: 1.60 -> 1.50) |
| Suspension 5/0 | carried, applied to nothing (data-only) | the 0x6A0-0x6CC block is not a stock/max pair, see ruled-out list |
| Weight 0/0, Nitrous 2/0, Turbo 4/0, Brakes 6/0, ECU 7/0 | NOT IMPLEMENTED (no justified per-car target) | see ruled-out list |

Fully upgraded (engine 3/3, gearbox L3, tires 1.0) vs stock, printed by `make drive-stage1-test` on the
retail records - torque is the sum of the nine primary samples, mu the front peak/slide pair:

| Car | gears | shift / max rpm | torque curve | front mu | mass |
|---|---|---|---|---|---|
| GOLF | 5 -> 6 | 6500/7000 -> 8000/8500 | 1.464 -> 2.270 (+55%) | 1.62/1.30 -> 1.95/1.75 | 1.330 (unchanged) |
| 240SX | 5 -> 6 | 6500/7000 -> 8500/9000 | 1.569 -> 1.961 (+25%) | 1.70/1.40 -> 1.88/1.65 | 1.220 |
| SUPRA | 6 -> 6 | 7000/7500 -> 8000/8500 | 2.248 -> 2.810 (+25%) | 1.60/1.35 -> 1.95/1.65 | 1.550 |
| 350Z | 6 -> 6 | 7000/7500 -> 8000/8500 | 2.756 -> 3.032 (+10%) | 1.68/1.58 -> 1.85/1.58 | 1.450 |
| TAXI (traffic) | 5 -> 5 | 6000/7000 (unchanged) | 1.380 (unchanged) | 2.00/1.60 (unchanged) | 1.800 |

Traffic is the interesting control: their engine add-curve and rpm headers are all zero and their tyre max
block is below stock, so an engine/tyre purchase changes nothing - while their gearbox template (final drive
3.00 against stock 3.55) *does* replace the stock block. This cannot reach a player: career_drv_upgrade
returns stock for any car the active career does not own, and the traffic models are not ownable.

Ruled out as effect targets (census over all 46 records; the scan is in handoffs/TASK-AK-LOG.md):
- Suspension 5/0 - 0x6A0..0x6CC reads as four 16-byte groups, and the second group repeats the first's
  spring and damper (0x6B4 == 0x6A4, 0x6B8 == 0x6A8 on 46/46), so it is a per-axle or retune layout, not
  a stock/max pair. Body stiffness even *falls* (0x6C0 -> 0x6C4 on 30 of 31 player cars), and 0x6AC is 0.0
  everywhere. Traffic zeroes the spring/damper slots and shares one stiffness constant set. Nothing in
  Stage1-5 reads 0x6A4..0x6CC: the readers consume mass, gearbox, grip and rpm only [data: stage*_car_read].
- Weight 0/0 - mass 0x220 has no copy and no partner anywhere in the record (searched all 0x890 bytes for
  16-byte exact repeats and for value duplicates). The nearest candidate, 0x778 -> 0x7D8, has the shape of
  a reduction (per-car 2.9% NAVIGATOR to 29.1% PEUGOT106, i.e. inverse to mass, and identical 1.50 on all
  15 traffic cars) but unknown units: it equals mass on GOLF alone and sits below mass on the rest
  (SUPRA 1.267 vs 1.550), and 0x7D4 next to it is a different quantity again (-0.16 MIATA .. 0.75 PEUGOT).
  Applying an unknown-unit value to mass would break the physics parity this branch protects, so it stays
  unapplied [inferred, low confidence].
- Brakes 6/0 - the 0x384 trio (service/handbrake/rear_share, read by stage3_car_read) has no max block and
  no level triplet. 0x370..0x3A0 is the resistance/stage-limit block.
- Turbo 4/0 / ECU 7/0 - 0x454 and 0x450 sit inside the engine domain, 0x604..0x640 pairs are inside the
  torque-curve domain, and 0x620 is not a torque curve (it does not scale with the master add-curve). ECU
  would double-count the engine/gearbox blocks, which are the only level triplets in the record besides the
  gearbox. No third block family exists to hold them.
- Nitrous 2/0 - no per-car field, and no drive-path input: drivetrain.c defers nitrous entirely, so there
  is nothing to feed even if a field were found. The 1.000 fits on axis 1 come from the share-fitting in
  cr_perf_fraction, not from the car record.

## Still unknown
Effect targets for brakes, weight, nitrous, ECU, turbo axes, and the meaning of the 0x6A0-0x6CC suspension
block (per-axle retune vs a stage the port has not modelled yet); units of the 0x778/0x7D8 pair; whether
retail uses u or the package level; exact discount formula; unlock record `a`/`b` meaning; graphics ids
37-68 and 80-84; vinyl colour channel count; unlock on discovery vs shop open.

## Career paint screen and rendering

`fe_paint.c` binds FrontB's `UI_ChoosePaintCategory.fng` -> `UI_Paint.fng`, verifying each
retail object identity. The paint package has 60 mini-icon swatches [data], sorted spatially
into three rows of 20 [inferred navigation]. Left/right browse one, up/down browse a row.
Body gloss/metallic/pearl are separate categories; rims, calipers, muffler, hoses and vinyl
combine their levels into 60 entries [inferred]. Category names use platform English captions. FrontB carries six numbered paint-drop icons
`PAINT_ICON_1..6` [data, inspected]; body finishes share icon 1 and the other groups use 2..6 [inferred].
Swatches use the parsed RGB, dim locked choices and show the authored lock for the selected
locked colour. The authored highlight follows focus, and Installed marks the saved colour.
Total Cost and Bank show the current rules snapshot. Accept asks before purchase; insufficient
funds uses retail key `0xd1ac60be`. Back cancels the question, returns to categories, then roam.

The host validates with `cr_paint_buy`, saves, rolls the state back on save failure, and refreshes
the snapshot. Browsing never changes career state. Body/rim preview ends when leaving the palette,
and saved paint is resolved only for the active career car. Restart reads the same per-car paint keys.

CARSKIN slices receive body RGB; opaque texture detail uses the existing luminance tint shader
path, alpha decals retain their own artwork over the new base. Gloss/metallic/pearl differ only
by a specular multiplier (0.8/1.0/1.1) [inferred]; no decoded metallic-flake/pearl optics are claimed.
Rims use the existing rim tint. Key zero bypasses paint application entirely, preserving the stock
shader and material path. No new shader uniform or shader-source change is required.

Caliper/muffler/hose/vinyl colour purchases persist, but their material effects are not implemented:
brake mount identity does not isolate a paintable caliper from its disc, and no verified muffler,
hose or vinyl colour-channel material mapping is available. Whole assemblies are left untouched.

Validation (Task AH): desktop/GLES/iOS-host builds pass at 24/29/29 warnings. `fe-paint-test`
passes 37 checks with retail data, including 60 visible swatches and visible lock alpha after
animation updates. All eight requested test/check targets pass. The full `m1.json` gate passes
17/17 steps (zero failures/skips/unrun steps); both stock INV PNGs are byte-identical to the
required references. The CC_GRAPHICS script buys GLOSS_L1_COLOR03 for $25 (bank 20000 -> 19975),
previews another colour, restores purchased blue on Back, and retains blue/Installed/bank 19975
after cold restart. All 14 final paint captures and 35 gate captures were visually inspected.
Evidence: `reports/parity/paintfe` in the shared reports directory; final task report in
`handoffs/TASK-AH-LOG.md`.
