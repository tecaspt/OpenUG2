# Engage presentation

Career zone and outrun offer text prompts are not shown on the normal HUD. The
existing enter controls and career logging are unchanged. Timed outrun status
messages remain.

The retail shop capture shows a world-space wrench, a matching HUD icon and an
engage indication in the d-pad. These cues are not yet implemented. The HUD
package contains the engage widget and a `Mode_Icon_Circuit.tga` node; the
global texture catalog also names `SHOP_ICON_BODY`, `SHOP_ICON_CAR_LOT`,
`SHOP_ICON_CRIB`, `SHOP_ICON_GRAPHIC`, `SHOP_ICON_INTERIORS` and
`SHOP_ICON_PERFORMANCE`, alongside `MODE_ICON_*` race textures. Their use,
rendering and mapping for each shop and race type still need verification.

The shop entry cinematic is not implemented: playback requires a VP6 decoder.
`HUD_World_Loading.fng` in the common in-game bundle contains a loading label
and five circle images. Its animation and its relationship to the shop and
race transitions have not yet been implemented or verified. Race launches
already use the frontend loading state; this is not a claim of retail parity.
