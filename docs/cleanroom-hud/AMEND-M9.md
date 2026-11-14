# Amendment M9: minimap icons, GPS selector and data-backed in-race nodes

Status: implementation amendment for milestone M9 (unit U2), pending review. It
amends SPEC.md sections "Minimap metadata and bounded behavior" (the sentence
"No fictional GPS path, police icon, event icon or career marker is drawn")
and C1-10. Every claim is tagged:

* **[data]** read directly from the user's own retail package data (node
  dumps of the free-roam HUD package `HUD_SingleRace.fng` in
  `GLOBAL/InGameFreeRoam.bun` and of `Demo_MiniMap_Legend.fng` in
  `GLOBAL/InGameCommon.bun`).
* **[observed]** seen in user-taken retail PC captures.
* **[inferred]** an implementation choice consistent with the above but not
  proven by it; each one is a candidate for an owner capture.

No executable, decompiled code or recovered implementation was consulted. No
retail bytes are stored in tests; the probe and pixel check read only the
engine's own output.

## 1. What is drawn and when

All new elements are gated on host inputs that default to zero/NULL, and on the
new settings whose defaults reproduce the earlier output. With the defaults and
no new host inputs the frame is unchanged (checked by a byte comparison of a
full-game screenshot before and after).

### 1.1 Pooled minimap icon art [data]

The free-roam HUD package holds pools of off-screen image nodes using the art
`Minimap_Icon_Car.tga`, `Minimap_Icon_Circuit.tga`,
`Minimap_Icon_Safe_House.tga`, `Minimap_Icon_Cop_star.tga`,
`Minimap_Icon_Start_Line.tga` and `Minimap_GPS_Selector.tga` [data]. The
implementation binds, per icon kind, the first node in the package that uses
that art, and draws its texture, UVs, authored alpha and composed authored
size (the selector's parent scale included: about 28.7 logical units) [data].
Pool slots and their dormant positions are not used; the position comes from
the host [inferred: pooled nodes are moved by the game at run time].

Kind to art: rival/player car -> Car; event -> Circuit; shop -> Safe_House;
showcase -> Cop_star; start -> Start_Line; GPS -> GPS_Selector [inferred from
the legend package, which labels each art with these meanings, data].

### 1.2 Tints [data] and fail-closed kinds

Tints come only from the legend package (icon nodes matched to their label
rows) and the free-roam HUD package [data]:

| kind / variant | tint |
| --- | --- |
| player car | 48,207,48 |
| opponent car | 207,128,48 |
| circuit race | 165,60,233 |
| short track | 130,190,230 |
| car lot / graphics / performance / body shop / interiors | 60,185,185 / 120,10,25 / 43,100,164 / 0,128,0 / 128,128,0 |
| showcase | 210,195,25 |
| start line | 192,192,192 |
| GPS selector | 0,128,255 |

Fail closed: an icon whose kind/variant has no legend tint is **not drawn at
all** (no default colour, no substitute art). This covers sprint, drag, drift,
street X, URL and any other event kind [inferred: the legend shows only the
two circuit-style rows]. The free-roam HUD package's circuit-icon pool carries
seven distinct authored tints, two of which equal the legend rows above; the
other five (207,128,48; 190,180,45; 14,156,14; 202,244,138; 50,130,200) are
not assigned to event kinds by any data seen so far [data: tints exist;
assignment unknown]. **Owner captures needed:** free-roam map showing sprint,
drag, drift, URL and outrun/other event icons, so each kind can be matched to a
tint.

### 1.3 Placement and the ring

Icons are placed by the inverse of the map pick transform (same frame as the
road layer, rotation, window and widescreen group shift) [inferred: same
projection as the roads]. A point farther from the map centre than the ring
radius is moved onto the ring along its bearing [observed: a POI icon with a
concentric blue selector ring on the border ring at about 70 logical units,
capture set 1 C1-10; the bearing rule itself is inferred]. Ring radius is the
setting `icon_ring`, default 70 logical 640x480 units, scaled by
min(W/640, H/480) to output pixels [observed radius, inferred scaling]. Icons
are not clipped by the map mask, so a clamped icon stays fully visible on the
ring [inferred].

Icons are upright except car icons, which turn with their world heading
relative to the map heading (art points up = heading along screen up)
[inferred]. Draw order inside the map: route, host icons, GPS target, rivals
[inferred].

### 1.4 Rivals

When the host supplies opponent headings and the Car art is bound, each rival
is the Car art tinted opponent orange (207,128,48) [data], rotated by its
heading and ring-clamped. Without headings, or without the art, the earlier
procedural markers (race diamonds, free-roam triangles) are kept as the
fallback [inferred: a car icon without a heading would be a fabricated
orientation].

### 1.5 GPS target

While navigation is active, the selector art is drawn at the navigation
destination in its authored blue (0,128,255), ring-clamped [observed: blue
selector ring on the map border, C1-10; data: tint]. The retail world-space
GPS arrow (`MARKER_GPS_AID` solids) is a separate unit and not part of this
amendment [data: asset exists].

### 1.6 Wrong way

`icon_Wrongway.tga` is drawn at its authored place and RGB while the host
reports wrong way [data: node and art in the free-roam package]. Its authored
alpha is 0 and it carries `Fadein`, `Fadeout` and blink scripts [data]; it is
drawn at full alpha, steady, with no fade or blink [inferred]. **Owner capture
needed:** wrong-way state, to confirm alpha and blink.

### 1.7 Lap box

`3rdperson_BoxLaps.tga` (authored black, under the lap texts' group) is drawn
in lap races only: racing, circuit, laps > 0, not drag [data: node and its
group with the lap texts; inferred: the gate].

### 1.8 Outrun (Getaway) bar

While the host reports a live outrun with a finite lead and a positive goal,
the `Getaway_Distance_Backing`, `_Lines` and `_Bar` nodes are drawn with
authored placement and colours (bar 48,207,48) [data]. The bar is cut from the
left to lead/goal clamped to 0..1 [inferred: direction and scale]. The label
text node shows the whitelisted display string `Meters`, which equals its
authored label [data]; the stored sample text is never displayed. The value
node shows "+ N" / "- N" in whole metres [inferred format, after the node's
authored sample shape]. Units do not follow the imperial setting [inferred]. An invalid lead or goal hides the group with a
diagnostic. **Owner capture needed:** outrun in progress (lead near 0, mid,
near goal) to confirm fill direction, the value's meaning and format.

### 1.9 Tach NOS label

`Tach_NOS.tga` under the gauge group is drawn exactly when the NOS backing is
drawn, i.e. only with a real host NOS state (fitted NOS, and the existing
opt-in level setting or the drag gauge) [data: node; inferred: shares the
backing's visibility]. No NOS value is fabricated; with the defaults it is not
drawn.

### 1.10 Whitelist addition

* Display string `Meters` (outrun label, 1.8). Package image aliases
  `Minimap_Icon_Circuit.tga`, `Minimap_Icon_Safe_House.tga`,
  `Minimap_Icon_Cop_star.tga`, `Minimap_Icon_Start_Line.tga`,
  `Minimap_GPS_Selector.tga`, `icon_Wrongway.tga`, `3rdperson_BoxLaps.tga`,
  `Tach_NOS.tga`; object lookup names `Getaway_Distance_Backing`,
  `Getaway_Distance_Lines`, `Getaway_Distance_Bar`. The two outrun text nodes
  are resolved by opaque name ids.

## 2. Map window

The map window (world metres across the authored map width) is chosen as: the
host's explicit window if set; else a speed table (`map_window_points` > 0,
piecewise linear over m/s, clamped at both ends); else `map_window_m`
(default 500, the provisional SPEC window). The default output is unchanged.
Retail draws visibly thinner roads than a 500 m window on the 4000 map
[observed], suggesting a wider window around 1.4 km [inferred]; the value and
any speed dependence await calibration from owner captures (stationary and
120 mph free roam).

## 3. Verification

* Unit tests: lap-box gate, outrun fill/text/invalid inputs, icon rotation
  agreeing with the placement transform over many headings, default settings.
* Probe `--icons`: a synthetic icon set (probe inputs only) on the free-roam
  map; a pixel check finds each expected tint at its expected point within
  2 px, the clamped icon on the ring within 2 px, the rotated rival wider than
  tall, and proves a fail-closed icon leaves the frame byte-identical.
* Default-output invariance: a full-game screenshot with default settings is
  byte-identical before and after this amendment.

## 4. Open questions for owner captures

1. Event-kind tints for sprint, drag, drift, street X, URL, outrun (five
   unassigned pooled tints, section 1.2).
2. Map window and speed dependence (section 2).
3. Wrong-way alpha/blink (1.6); outrun bar direction and value text (1.8).
4. Whether rivals and the start line rotate, and whether rivals clamp to the
   ring like POIs (1.3, 1.4).

## 5. GPS world marker (U4, `src/gps_marker.h`)

* Retail guides with a solid arrow floating over the road ahead of the car
  [observed]. The solid is the object `MARKER_GPS_AID` in
  `GLOBAL/InGameCommon.bun` [data]: two material ranges over one vertex pool
  (a dark-blue core and a light-blue shell, 82 triangles in all), bounds about
  1.9 m x 0.7 m x 3.2 m [data]. `MARKER_DIRECTION_AID` is the same geometry
  laid out elsewhere in the bundle [data]; it is not used.
* Both ranges wear one 64x64 texture, a horizontal light band, whose record
  is authored additive (usage 2, blend 2) [data]. The blue lives in the
  vertex prelight [data]. Drawn as MODULATE2X of texel and vertex colour,
  added to the frame, depth tested without depth writes, fogged like the
  world [inferred from the record's draw mode]. If the texture fails to
  decode, a flat mid-grey texel passes the vertex colour through unchanged.
* `ARROW_MARKERA` (the planner's candidate) is not this solid's texture. In
  `InGameCommon` it decodes to a coherent grey painted arrow with no alpha
  plane [data]; the HUD art loader rejects it by its UI noise test, which is a
  property of that test, not of the record. It is not used here.
* The solid is authored tip-down (tip on its own -Z, flat faces along +-Y)
  [data]. The engine lays it flat with the tip leading along the guidance
  bearing (`HudState.nav_bearing`: atan2 in world XY) [inferred]. Placement
  6 m ahead of the car, 2.5 m above the road, a 0.15 m / 1.6 s bob, authored
  scale [inferred]; all are named constants awaiting calibration from owner
  captures at three distances. Viewed from the chase camera the flat arrow
  reads nearly edge-on; a nose-down tilt or a higher placement may be what
  retail does [inferred, open].
* Shown only while HUD guidance is active; it uses the previous frame's
  bearing because the HUD state is sampled after the world pass.
