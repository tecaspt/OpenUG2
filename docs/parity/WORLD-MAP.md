# Career World Map — Task S

## Status and clean-room scope

[observed] Research, the screen module, FE/pause adapters and synthetic tests are complete.
Desktop GL and GLES screen checks and full scratch game builds pass against base2037712.
All ten map captures were reviewed by eye. Exact host patches are supplied separately; live
GPS/arrival gameplay and device validation remain NOT RUN for the orchestrator to perform.
Only installed data and existing OpenUG2 readers were inspected. No game executable or external
recompiled/decompiled code was used. Raw dumps, calibration JSON and PNGs stay outside Git,
in the private reports/worldmap directory. This document records format/identity facts, not assets.

## Package identities and authored layout

[data] FRONTEND/FrontB.lzc contains UI_CareerWorldMap.fng:190 nodes (160 images,17 text,13 groups),
23 resource slots. The package dump has no parse diagnostics. Important node GUIDs and roles:

| GUID | Resource / authored sample | Placement or role |
|---|---|---|
|0xe252e|Track4000_map.tga|centre(-91,22),309×309,zero pivot,identity quaternion,UV(0,0,1,1)|
|0xe5555|U2_Menu_Box_Black.tga|same centre/size; map backing|
|0xdee85|World Map, label0xa0c6ae56|screen title|
|0xdb8e9|Region 1|top region caption, with arrows0xdf163/0xdf164|
|0xe3117|U2_mini_icon.tga|16×16, full UV; authored at(-264,24)|
|0xe3116/0xe3118|U2_Menu_Arrow_layer1.tga|vertical selector arrows|
|0xe3115|U2_SliderBar_Tab_layer1.tga|selector slider tab|
|0xe3114|U2_mini_icon_highlight.tga|32×32 selector highlight|
|0xe3112|U2_mini_icon_backing.tga|32×32 selector backing|
|0xe4eaa and other pool GUIDs|U2_mini_icon.tga|81 full-UV placeholders, mostly authored below the viewport|
|0xe25de/0xe336a|Race Type / Sprint|right-side example event details|
|0xe36de/0xe36dd|Race Status / Completed|right-side example event status|
|0xe25dd/0xe33c5|Distance / XXXXX Miles|right-side example distance|
|0xe25dc/0xe33c9|Previous Time /01:30:04|right-side example time|
|0xe6af6|Select Event, label0xdaac95f9|bottom selection hint|

[data] The map corners are(-245.5,-132.5)..(63.5,176.5) in centred640×480 virtual coordinates.
The static package supplies generic mini-icon placeholders, not class-specific atlas crops:
representative pool/selector images have zero pivots, identity rotations and full UVs.
[data] No events/shops/all filter rows or icon-kind legend are identified in this package. Its
region arrows and event-details panel must not be represented as proven filter controls.
[inferred] Runtime placement of available events/shops, selection, category art replacement,
zoom/pan behavior and icon classification cannot be established from the static package alone.
Unimplemented authored example values must be hidden or replaced with actual values.

## Map art and world projection

[data] Track4000_map.tga resolves to TRACKMAP4000 (texture key0xf7600936) in TRACKS/TRACKMAP4000.BIN,
512×512. The alias is explicit in the existing data reader (hud_asset.c). GLOBAL/GLOBALB.BUN
chunk0x34201 has296-byte TrackInfo records: event ID at+140; float originX,originY,squareExtent
at+172,+176,+180. The unique4000 row supplies:

```
originX = -3425.429931640625
originY =  3287.260009765625
extent  =  5633.16015625
u = (worldX-originX)/extent
v = (originY-worldY)/extent
virtualX = -245.5 +309*u
virtualY = -132.5 +309*v
```

[data] Derived: World bounds are X[-3425.429932,2207.730225],Y[-2345.900146,3287.260010].
Positive worldY goes toward texture top; positive worldX goes right. The model must invert the
same transform for cursor/touch tests, reject nonfinite/degenerate inputs, and bind to the live
map node's authored transform rather than guessing screen offsets. TrackInfo and map art are
loaded at runtime, never embedded. Numeric constants above document calibration only.

[data] Independent controls come from career shops/races plus PathsFreeRoam zone polygons:
UC_CAR_LOT zone centre(-974.037964,412.114166),CRIB_1(654.167542,-101.491867),
EV_68(-888,636),EV_30(1814,-832). Their normalized coordinates are respectively
(0.435172,0.510397),(0.724211,0.601572),(0.450445,0.470652),(0.930105,0.731252).
[data] Spawn-marker positions and zone centres are distinct (e.g. UC_CAR_LOT spawn(-976.587646,
405.059601)); navigation/arrival must use the selected zone rather than confuse it with a spawn.
[data] The rendered shop controls use the actual spawn-marker positions, independently of the
zone-centre destinations used by ch_icons; the two event controls use the locator-zone centres.
[observed] Four rendered controls align with the corresponding map road locations in GL and GLES;
projection/inverse checks recover their world positions within0.001m. This validates registration,
not independent retail input behavior or exact retail category colors.
Existing hand-authored district-name/geography comments are not independent retail calibration.

## Icon snapshot, selection and input contract

[inferred] FeMapIcon carries finite worldXY, explicit event/shop kind and stable data identifier,
plus presentation category metadata. The host supplies only available races/open shops through
fem_open_world_map(menu,icons,n,playerX,playerY,heading); the menu copies a bounded snapshot.
Filtering/selection must preserve identity even for co-located items. HudMapIcon has no ID;
conversion must carry identity during the career enumeration, never guess it from nearest position.
[inferred] Arrow keys/controller D-pad move the cursor and snap to a directional nearby icon.
Touch release on an icon selects it; repeated taps cycle co-located identities. The authored
Select Event caption is a tap-reachable Accept target. Tab/Shift-Tab and pad shoulders cycle the
visible snapshot as an additional selection path. Accept emits the appended
FEM_REQ_SET_DESTINATION request carrying worldXY,kind,id; Back emits FEM_REQ_CAREER_ROAM.
Every screen retains the existing tap-reachable Back adaptation. No teleport or automatic
race/shop engagement is implied. Product controls are inferred adaptations, not observed retail input.
[inferred] Fail closed on a missing/mismatched package, map metadata, texture, required icon or font;
fem_status reports the reason. No replacement native map or invented icon art is drawn.

## Pause and GPS integration requirements

[data] Existing free-roam pause has no World Map entry; Return to World Map is offered by the
race/from_map context. The base adapter also maps Garage to FE_ACTION_WORLD_MAP; this task splits those actions.
[inferred] Add an opt-in career-free-roam entry using the existing retail row and confirmation
via pause_menu.c/h, retaining default lists and Back/Continue behavior. Split Garage from Map.
The reuse of race-return wording while exploring is an adaptation; it is not proven retail behavior.

[data] The current host accepts GPS worldXY through g_dbg.gps_want_x/y +gps_request. The HUD already
has a GPS selector icon but currently gets a snapped navigation endpoint; the old25m arrival check
also lies inside HUD drawing and does not test career zones.
[inferred] Preserve destination kind/id in host state, revalidate availability, feed the chosen
point to the route request/minimap, clear on entry into the selected zone or race start, and keep
arrival separate from explicit engagement. Exact owner edits belong in WORLD-MAP-INTEGRATION.md.

## Implemented adapter behavior

[inferred] Available events and shops reuse the genuine generic U2 mini-icon pool style, with the
existing minimap legend tints. Unsupported event tint variants use the generic circuit-event tint;
this is not evidence of retail World Map category colors. The player uses the genuine directional
Minimap_Icon_Car art from GLOBAL/InGameCommon.lzc (with a .bun fallback).
[data] The final real-data check confirms that InGameCommon supplies the player texture;
InGameFreeRoam alone does not.
[inferred] Its heading is derived by projecting
the world heading vector, accounting for the reversed mapY axis. Selection uses the authored
highlight sprite's size/color, and textured triangles are clipped to the affine map rectangle.

[inferred] Directional snapping favors nearby icons aligned with the requested direction. Tab/
Shift-Tab and pad shoulders cycle visible choices; repeated taps cycle co-located identities.
The existing Select Event caption is a touch Accept target. Its authored text node is drawn
explicitly because the shared PC view hides console hint strips. No replacement caption art is used.
A lost/hidden window cancels a pending
tap. Accept locks the menu until the host reacts; repeated updates cannot emit another request.
Unbound player/cash, help, region-selector and example status/distance/time widgets are hidden.

[data] The identity set includes all190 objects and is appended after the existing binding sets,
preserving the numeric indexes currently used by Quick Race Car and PostRace. The pause opt-in is
restricted to career free roam and is default-off. It copies the existing retail World Map entry
and confirmation. The unsupported Garage action resumes; it no longer aliases World Map.

## Checks and evidence

| Requested check | Result |
|---|---|
|1. Synthetic World Map and existing FE tests|PASS: all requested targets, plus fe-binding-test; GL/GLES real-data screen checks|
|2. Four-point real-data projection PNG|PASS: car-lot/crib spawn markers and two event zones, round-trip within0.001m; captures reviewed|
|3. GL/GLES screen/destination PNGs|PASS: five captures per backend, fresh isolated storage; reviewed by eye|
|4. Every added language key|PASS for both new lookups:0xa0c6ae56 World Map,0xdaac95f9 Select Event; whole audit retains baseline findings|
|5. Full build and unchanged24 warnings|PASS: desktop baseline24 / modified24, exact normalized warning list unchanged; GLES baseline30 / modified30, also unchanged|

[observed] fe-worldmap-test passes with -Wall -Wextra -Werror. Existing fe-screen-test,
fe-polish-test, gui-art-test, fe-garage-test, fe-lot-test, pause-menu-test and fe-binding-test pass.
fe-screens-check passes in desktop GL and GLES. Both full scratch games build with the exact
Main/HUD/Makefile patches in WORLD-MAP-INTEGRATION.md; no production host files were edited.
GLES has six additional pre-existing warnings in unowned renderer/backend code; no new warning
appears in either backend. Baseline and modified warning messages match after line-number removal.

[observed] The full language audit command exits1 on both baseline and modified source: baseline has
31OK,10MISSING,3LABEL-MISMATCH; modified has33OK with the same10MISSING/3LABEL-MISMATCH. No new
missing/mismatched lookup is introduced. Existing findings include non-language asset hashes, so
the global command is not reported PASS. Reports are language-audit-before.log and language-audit.log.

[data] A bounded independent scan found all139 parsed career zones inside the4000 frame.
[observed] The PNG harness reads real UC_CAR_LOT/CRIB_1 spawn markers and two event locator zones
at runtime, projects and inverse-checks them, tests typed request identity/no repeat emission,
renders four controls plus a touch-selected destination, and checks keyboard/touch Back, focus
cancellation and the opt-in pause confirmation. A fresh standalone menu repeats those checks
without a preceding Quick Race visit, proving the screen loads its own map and player art.
The diagnostic controls are independent of career unlock stage; their presence in this test is
not a claim that those later-stage events are available in a new career.

[observed] The following ten final PNGs were reviewed by eye. Paths are relative to the private
project reports/worldmap directory; screenshots are not committed:

- shots/gl/worldmap-control0.png — UC_CAR_LOT spawn marker.
- shots/gl/worldmap-control1.png — CRIB_1 spawn marker.
- shots/gl/worldmap-control2.png — EV_68 locator zone.
- shots/gl/worldmap-control3.png — EV_30 locator zone.
- shots/gl/worldmap-destination.png — touch-selected event, with player at car lot.
- shots/gles/worldmap-control0.png
- shots/gles/worldmap-control1.png
- shots/gles/worldmap-control2.png
- shots/gles/worldmap-control3.png
- shots/gles/worldmap-destination.png

[observed] Resumable artifacts under the private reports/worldmap directory:

- baseline-source, baseline-build.log, baseline-gles-build.log: immutable base2037712 and measured warning baselines.
- build-gl.log/build-gles.log: final full scratch builds.
- prepare-scratch.py: copies project source into external GL/GLES scratch trees and patches Makefile.
- prepare-host.py/main-helpers.c: generates exact Main/HUD preview patches and applies only to scratch.
- run-checks.sh/checks.status/checks.exit: full validation run, final exit0.
- fresh-checks.status: standalone fresh-menu checks PASS in both backends.
- final-captures.sh/final-captures.status: final GL/GLES captures, separate fresh XDG storage per backend, both PASS.
- worldmap.json/fng-map-geometry.json/map-calibration.json/frame-coverage.json: local-only research evidence.

[observed] Initial validation failures exposed the missing player texture import and test timing/
disabled-row traversal assumptions. The final source loads InGameCommon; pause checks now wait
for dialog transitions and navigate by actual selected row. Failed earlier logs remain retained;
only the final successful runs above support the PASS claims.

[inferred] Host gameplay hooks (GPS, exact minimap marker and zone arrival) still need the owner to
apply/review the integration document and run gameplay checks. Optional race-return and unproven
legend/filter controls are not implemented. Generic icon tints, directional selection and touch
adaptations remain inferred; retail captures would help refine them. Decorative authored left
rails remain inactive. No retail executable, native map fallback, production host-file edit,
publishing or real user-config modification is part of this task.

## Host integration follow-up

[data] The production host now opens the map from career free-roam pause, validates
stable destination identities, feeds the existing GPS solver and minimap icon
contract, and clears guidance inside the selected zone. The frame/presentation
order from the scene render-scale integration is preserved.

[observed] Desktop build passes with24 warnings, identical to the prior normalized
baseline list. GLES builds with29 warnings; the clean current integration iOS-host
baseline also has29 and the same warning list. Direct current GLES baseline and
modified iOS-host comparison remain pending at this checkpoint.

[observed] A live isolated test career opens the map with18 icons. Selecting race
41837a8a solves124 nodes/3295m; selecting Car Lot be4383fa solves35 nodes/833m.
Both selections show the blue minimap destination marker and road guidance. Moving
into each canonical zone clears active destination, route and marker. The eleven
live captures were reviewed. The explicit --world-map-dev/F8 diagnostic moves XY;
it does not establish physical driving fidelity, and existing fall recovery reset
the test pose between the two legs. Ordinary map selection never moves the car.

[observed] Reports are under reports/worldmap/s2 outside the repository: live.log,
live-assert.status and live.txt; live-roam.png, live-pause.png, live-confirm.png,
live-map-race.png, live-race-selected.png, live-race-route.png,
live-race-arrived.png, live-map-shop.png, live-shop-selected.png,
live-shop-route.png and live-shop-arrived.png. Four probe captures were also
reviewed. Remaining build/tests, invariance and S7/S13 checks are still pending
at this checkpoint. Device checks remain NOT RUN. Optional filters and race-return
remain unimplemented; the earlier inferred input/colour choices remain inferred.
