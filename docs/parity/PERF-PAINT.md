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

| Category | Effect | Evidence |
|---|---|---|
| Engine | primary torque += u x master add-curve 0x530 (9 samples); shift/max rpm += level block header (0x560/0x5A0/0x5E0 +4/+8) | L1..L3 blocks are 0.34/0.68/1.00 x master; headers are floats (e.g. GOLF 500/1000/1500 rpm) |
| Transmission | gearbox block of the level (0x460/0x4A0/0x4E0, same layout as stock 0x2C0); level from axis 2 | L3 adds a 6th gear on 5-speed cars |
| Tires | axle grip lerps stock 0x1E0/0x200 -> max 0x650/0x670 by u | max >= stock on every car |
| Turbo, suspension, brakes, weight, nitrous, ECU | NOT IMPLEMENTED (no decoded per-car target; 0x620 is not a torque curve) | |

Example, fully upgraded: GOLF +50% peak torque, 5 -> 6 gears, +1500 rpm, front grip 1.62 -> 1.95;
350Z +10%; SUPRA +25%.

## Still unknown
Effect targets for brakes, weight, nitrous, ECU, turbo, suspension axes; whether retail uses u or the package level;
exact discount formula; unlock record `a`/`b` meaning; graphics ids 37-68 and 80-84; vinyl colour channel count;
unlock on discovery vs shop open.
