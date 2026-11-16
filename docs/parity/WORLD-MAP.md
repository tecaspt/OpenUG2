# Career World Map — Task S

## Status and clean-room scope

[data] Research is complete against base2037712. Implementation and checks are not yet run.
Only installed data and existing OpenUG2 readers were inspected. No game executable or external
recompiled/decompiled code was used. Raw dumps, calibration JSON and future PNGs stay outside Git,
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
|0xe6af6|Select Event, label0x813f5021|bottom selection hint|

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

[data/derived] World bounds are X[-3425.429932,2207.730225],Y[-2345.900146,3287.260010].
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
[inferred] Correct visual registration still needs the required rendered four-point check.
Existing hand-authored district-name/geography comments are not independent retail calibration.

## Icon snapshot, selection and input contract

[inferred] FeMapIcon will carry finite worldXY, explicit event/shop kind and stable data identifier,
plus presentation category metadata. The host supplies only available races/open shops through
fem_open_world_map(menu,icons,n,playerX,playerY,heading); the menu copies a bounded snapshot.
Filtering/selection must preserve identity even for co-located items. HudMapIcon has no ID;
conversion must carry identity during the career enumeration, never guess it from nearest position.
[inferred] Arrow keys/controller D-pad move the cursor and snap to a directional nearby icon.
Touch release on the same icon selects it and requests that destination. Accept emits the appended
FEM_REQ_SET_DESTINATION request carrying worldXY,kind,id; Back emits FEM_REQ_CAREER_ROAM.
Every screen retains the existing tap-reachable Back adaptation. No teleport or automatic
race/shop engagement is implied. Product controls are inferred adaptations, not observed retail input.
[inferred] Fail closed on a missing/mismatched package, map metadata, texture, required icon or font;
fem_status reports the reason. No replacement native map or invented icon art is drawn.

## Pause and GPS integration requirements

[data] Existing free-roam pause has no World Map entry; Return to World Map is offered by the
race/from_map context. The adapter currently also maps Garage to FE_ACTION_WORLD_MAP.
[inferred] Add an opt-in career-free-roam entry using the existing retail row and confirmation
via pause_menu.c/h, retaining default lists and Back/Continue behavior. Split Garage from Map.
The reuse of race-return wording while exploring is an adaptation; it is not proven retail behavior.

[data] The current host accepts GPS worldXY through g_dbg.gps_want_x/y +gps_request. The HUD already
has a GPS selector icon but currently gets a snapped navigation endpoint; the old25m arrival check
also lies inside HUD drawing and does not test career zones.
[inferred] Preserve destination kind/id in host state, revalidate availability, feed the chosen
point to the route request/minimap, clear on entry into the selected zone or race start, and keep
arrival separate from explicit engagement. Exact owner edits belong in WORLD-MAP-INTEGRATION.md.

## Checks

All implementation checks1–5: NOT RUN. Baseline desktop build/warning count is queued under the
shared heavy lock. The next unit adds the bound screen and synthetic tests, then renders and audits
through an external scratch Makefile; production Main/Make/HUD/career sources remain untouched.
