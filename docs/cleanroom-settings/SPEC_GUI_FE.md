# Quick Race front-end bindings (R1, partial)

[data] Scope: parsed PC retail data only. No executable or derived executable code was inspected.
[data] `src/fe_binding.h` records hash, GUID, parent GUID, type and resource index for candidate application
objects; `tools/fe_binding_check.c` reparses the user's bundles and verifies every tuple independently.
[observed] 2026-11-14: 278 identity occurrences matched, zero failures; this is metadata verification,
not a rendered-screen or retail gameplay comparison.

## Track selection

[data] FrontB contains UI_QRTrackSelect.fng (161 objects, 19 resources). Forty images share hash
0xF5FF3DBC and resource 17 (UI_Icon_Map.tga), with distinct GUIDs; these are a parked pool, not forty
independently missing resource names. The pool spans authored rows below the normal screen bounds.
[data] Resource 18 names Track4000_map.tga; GUID 0xE2305 references it with hash 0x90FE80F3.
[data] GUID 0xDF142 is the track caption (hash 0xF601F2D4), and 0xDF12B the header.
[inferred] The pool receives event-dependent map textures and the separate large image shows the selected
track; the exact pool scrolling and event-to-map association still need validation. TRACKMAP*.BIN is
not established as the source of these images.

## Track and mode options

[data] UI_QRTrackOptions.fng has 181 objects; UI_QRModeOptions.fng has 201. Both contain title/data
master anchors and repeated text rows. Their text identities are included in the table.
[inferred] The application supplies labels and values; authored animations alone do not establish
which race kinds use which page. M1 enables Laps and Difficulty only, as an engine scope decision.
[inferred] Opponents, traffic, catch-up, reverse and Lap KO must remain unavailable until implemented.

## Car selection

[data] UI_QRCarSelect.fng has 84 objects and 17 resources. GUIDs 0xE12B8, 0xE12B9 and 0xE12BA are
three distinct stat labels sharing hash 0xF601F2D4. Hash-only lookup is therefore unsafe.
[data] GUID 0xE694B contains a filter placeholder; 0xDBA69 is another filter text object with console
flags. GUID 0xE1E52 is the race-mode value; 0xDDF04 is the screen heading.
[inferred] Stats must not display fabricated performance values. Player-car filtering uses the existing
catalogue traffic flag. A frozen-world preview with a 0.3-second debounce is a platform/engine adaptation.

## Post-race

[data] InGameRace.bun, InGameDrag.bun and InGameDrift.bun each contain matching selected identities for
UI_postrace.fng (63 objects), UI_PostRaceResults.fng (102), and MU_PostRaceConfirm.fng (34).
[data] UI_PostRaceResults has repeated player, ranking, car and time fields, plus authored restart and
next captions. UI_postrace has a presentation group and icon caption, rather than the result table.
[inferred] Application actions Restart, Continue and Quit to Main map to the M1 flow contract; their
ordering and navigation are not verified against a retail capture. Opponent result rows cannot be
populated from PostRaceInfo unless the host supplies their results; placeholders must be hidden.

## Messages and remaining evidence

[data] Object responses contain script-start commands and package fade/dim responses. The interrupted
research's human-readable message names are guesses and are not treated as proof of application semantics.
[inferred] Screen order remains mode → track → options → car → race → results pending owner captures.
[observed] No new screens have been rendered by this R1 pass. Binding identities alone do not establish
that resource decoding, application writes, or authored animation states produce a complete screen.

## Verification

[observed] From the worktree, after sourcing env.sh and checking available memory, under the heavy lock:

```sh
cc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Isrc tools/fe_binding_check.c \
 src/ugui/ugui_bundle.c src/ugui/ugui_codec.c src/ugui/ugui_fng.c \
 src/ugui/ugui_util.c src/ugui/ugui_anim.c -lm -o /tmp/fe_binding_check
/tmp/fe_binding_check
```

[observed] The checker reads OPENUG2_DATA at runtime, returns 77 when it is unset, and returns failure
for missing packages or mismatched identities. No bundle dumps or derived images belong in the repository.
