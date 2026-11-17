# Rear-view mirror

## Evidence

[data] `tools/fng_dump.py --list` finds 227 packages in this installation.
`--find-sample mirror` finds zero matching nodes. Enumerating every node (including
image aliases and groups) from every InGame bundle and Options package finds no
`mirror`, `rearview`, or `rear_view` name/alias. Examined packages include
HUD_SingleRace (both Race and FreeRoam copies), HUD_Short_Track, HUD_Drag,
HUD_Drift and both split-screen variants. No named mirror rectangle or frame
was identified. A negative name search does not rule out an unnamed image.

[data] LANGUAGES/English.bin contains these keys, read through the string-table
reader used by fng_dump (no executable inspection):

| Key | Text |
| --- | --- |
| 85a6ce05 | Rearview Mirror |
| d02c18bc | Rearview Mirror: |
| e4215d4e | Rearview mirror |
| e32ee46c | To turn the rearview mirror on or off while in-game, select Options and then select Gameplay from the Pause Menu. |

[inferred] Reuse the existing Gameplay option and profile field
`StGameplay.rearview_mirror`. The existing settings implementation defaults it
to 1. This is implementation evidence, not a newly established retail data
encoding: previous settings documents include static-analysis provenance and
are not used here as clean-room data evidence. Default on follows the supplied
retail still and the existing profile policy. An explicit `--rear-mirror on|off`
overrides the profile for reproducible comparisons.

[observed] The supplied retail still is 1170 by 643; its bevelled panel spans
approximately x=450..715, y=75..120. [inferred] Until an authored frame is
identified, use a centred 265 by 45 panel scaled by drawable height / 643,
with a procedural dark bevel. This is an approximation, not recovered retail
art. The scene is horizontally reflected during composition.

[inferred] Show during free roam and races; hide for front-end, pause/world map,
post-race, race presentation, orbit/free-camera and diagnostic passes. These
visibility rules are conservative runtime policy; the language table establishes
the toggle, not the complete visibility state machine.
