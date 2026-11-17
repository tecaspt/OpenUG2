# Engage presentation

Career zone and outrun offer text prompts are not shown on the normal HUD. The
enter controls and career logging are unchanged. Timed outrun status messages
remain.

The free-roam HUD package contains the d-pad, its upper highlight, and a
`Mode_Icon_Circuit.tga` icon slot. While a career zone is active, the slot uses
the matching `SHOP_ICON_*` or `MODE_ICON_*` texture from the global catalog,
and the upper highlight is enabled. The authored d-pad centre texture is used
for this state.

Nearby open shops and available races have camera-facing world signs using the
same catalog textures. They use the named marker position where available,
falling back to the zone centre. Height, scale and camera-facing orientation
are inferred; this does not reconstruct an authored three-dimensional solid.
The retail shop still shows a floating wrench; the signs need comparison at
matching locations and poses before claiming visual parity.

Shop entry displays `HUD_World_Loading.fng` over the world for 1.2 seconds,
with the five authored loading circles pulsed in sequence. The duration and
pulse timing are inferred. Career race entry overlays the same animation on the
existing frontend loading screen. The retail shop-entry cinematic is not
implemented: playback requires a VP6 decoder.
