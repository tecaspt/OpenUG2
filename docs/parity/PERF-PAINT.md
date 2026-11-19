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

## Effects on the car (src/drive_stage1.c, src/drive_contact.c, src/drivetrain.c; per-car record [data])

The car table is one 0x890-byte record per vehicle (46 = 31 player cars + 15 traffic, whose fields are shared
template constants). NULL or an all-zero DrvUpgrade reproduces every stock reader (Stage 1..5 and body) byte
for byte (memcmp-tested on GOLF, 240SX, SUPRA, 350Z, TAXI). Task AK found no proof from data alone for six
categories; task AR lifted that restriction and takes the facts below from external sources, tagged:
[recomp-GC] github.com/Octoblimp/NFSU2-3DS (GPL-3.0; docs/retail_car_physics.md, ridephys.c package builders,
perfdb.c level function, carsim.c nitrous; facts only, no code copied), [GlobalLib] github.com/NFSTools/GlobalLib
(CarTypeInfo field names), [upstream] docs/cleanroom-driving-physics/SPEC.md. Offsets are record offsets.

Fraction per category = sum of installed parts' axis share (components: transmission and suspension 3, others 1).
Level = (int)(clamp(u,0,1)*3+0.5) [recomp-GC] (not cr_perf_level_of). Build order: weight, transmission, nitrous,
engine, turbo, suspension, brakes, ECU, tires. lerp(v,t,x)=(t-v)*x+v, so x=0 gives exactly v.

| Category | Effect | Evidence |
|---|---|---|
| Weight 0/0 | mass x ((0x450-1)u+1); centre.x += 0x454 u; yaw scale lerp 0x270 -> 0x458; inertia rebuilt from the new mass | [recomp-GC] builder; 0x450 = .90/.90/.90/.91 (GOLF/240SX/SUPRA/350Z), 0x454 = +.02 on SUPRA, 0x458 = 2.15/2.10/2.00/2.00 [data]. 0x778/0x7D8 are hood camera heights, not weight. No effect on records without a kit (TAXI: 0x450 = 1) |
| Transmission 1/2 | gearbox block of the level (0x460/0x4A0/0x4E0), level from the retail rounding of axis 2 | [data] same layout as stock 0x2C0; L3 adds a 6th gear on 5-speed cars |
| Nitrous 2/0 | bottle seconds = lerp(0x3B0 = 0, 0x690 = 6.0, u) (2/4/6 s at .333/.667/1.0); NOS torque = 0x69C = 0.5 (set) | [recomp-GC] carsim; [data] values. Only a bought kit gives a bottle in career |
| Engine 3/0 | primary torque += u x 0x530 (9 samples); engine brake += u x 0x554; NO rpm change | [data] + [recomp-GC]; the rpm header belongs to the ECU (correction of task AK) |
| Turbo 4/0 | secondary torque lerp to the 0x620 top curve by u, always applied | [recomp-GC]; 0x620 >= stock secondary on all player cars |
| Suspension 5/0 | c0 (springs/shocks, coilover): axle progression, stiffness, both dampers, travel limits, reference lerp to the 0x6D0/0x6F0 blocks; wheel z += 0x714 (front) / 0x718 (rear); centre z -= 0x710. c1 (tower bar, sways): axle anti-roll lerp. c2 (camber plates, coilover): steering gain lerp 0x380 -> 0x6CC | [recomp-GC] builder; 0x6D0/0x6F0 have the stock 0x280/0x2A0 layout [data]. 0x6A0..0x6BC are aero-kit add triples of the hidden category 9 |
| Brakes 6/0 | service, handbrake, bias each lerp to 0x6C0 / 0x6C4 / 0x6C8 | [recomp-GC]; [data] |
| ECU 7/0 | rpm block by level (0x560/0x5A0/0x5E0): idle, shift, max rpm adds; primary torque += u x 0x5F0; engine brake += u x 0x614; owning part 45, 46 or 51 sets the speed governor 0x3A0 to 0 | [recomp-GC]; [data] (0x5F0 == 0x530, 0x614 == 0x554) |
| Tires 8/0 | axle grip lerps stock 0x1E0/0x200 -> max 0x650/0x670 by u | max >= stock on all 31 player cars; refused where it is not (traffic rear blocks) |

Nitrous at run time (src/drivetrain.c, src/drive_stage1.c) [recomp-GC]: while the burn level lambda (1..3) is
on, Ton += kappa x nos_torque x shape(rpm) x throttle, kappa = 1/1.4/1.7, shape = nine samples
{.3,.4,.45,.5,.55,.6,.7,1.0,.7} over [idle, shift rpm], zero on the limiter, added before the negative-torque
scaling and not multiplied by G or the 1.05 bonus; every wheel grip multiplier is also scaled by kappa. The
bottle is in seconds, filled when the car is built and when a race starts (respawn keeps it); a press starts a
burn when shut and fuel is left, it burns 1 s/s while held and stops on release or empty. Levels 2 and 3 need
overfill from style points, which OpenUG2 lacks, so the level is 1. Input: CB_ACT_NITROUS / touch TC_NOS; the
legacy (non-Stage-1) top-speed boost is unchanged. Race kinds without NOS (Street X, drift) still block it.
HUD: have_nos = bottle > 0 and allowed, nitro tank gauge = remaining fraction, have_turbo only with a bought turbo kit.

Stock vs fully upgraded (every category 1.0 / L3, governor off), from `make drive-stage1-test`: a 90 s full
throttle straight-line run on the Stage 1 drive adapter (flat road, no contact chassis, drag from the record),
so times are comparable between rows, not retail measurements. NOS = the same run holding nitrous.

| Car | mass | 0-100 km/h | 0-100 with NOS | top speed | brake service | steering gain | NOS bottle |
|---|---|---|---|---|---|---|---|
| GOLF | 1.330 -> 1.197 | 7.48 s -> 3.52 s | 3.52 s | 214 -> 326 km/h | 0.450 -> 0.675 | 1.120 -> 1.150 | 6.0 s |
| 240SX | 1.220 -> 1.098 | 8.77 s -> 4.30 s | 4.03 s | 212 -> 349 km/h | 0.485 -> 0.616 | 1.100 -> 1.100 | 6.0 s |
| SUPRA | 1.550 -> 1.395 | 6.95 s -> 4.12 s | 3.83 s | 245 -> 341 km/h | 0.560 -> 0.670 | 1.050 -> 1.120 | 6.0 s |
| 350Z | 1.450 -> 1.320 | 5.78 s -> 4.17 s | 3.93 s | 253 -> 351 km/h | 0.495 -> 0.600 | 1.010 -> 1.075 | 6.0 s |

Stock top speed is the record's governor (130-155 mph); the upgraded runs have it removed, so they are drag limited.

TAXI (traffic) control: weight, turbo and ECU change nothing (no kit data); engine, tires keep stock where the
max is zero or below stock.

Ruled out: 0x778/0x7D8 as weight (hood camera heights); 0x6A0..0x6BC as suspension (aero-kit adds, category 9);
the AK reading of the 0x6A0-0x6CC block as an unexplained per-axle layout is superseded by the table above.

## Still unknown
No PC-binary verification: the formulas are GC (recomp-GC) facts plus PC data plus the upstream SPEC. Free-roam
nitrous refill and extra NOS start conditions, the 0x3B4/0x694 bottles field, the transmission follow-up (clutch
lerp to 0x4F4/0x4FC by c0, engine inertia += 0x52C x c1) and tyre details are not applied. Retail unlock record
`a`/`b` meaning, graphics ids 37-68 and 80-84, vinyl colour channel count, exact discount formula.

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
