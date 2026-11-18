# Dialog frames

The package list and node measurements below come from `tools/fng_dump.py` against retail data [data]. Coordinates are centred virtual units; message positions are node positions, not text bounds. `M` is message node `0x1e2640fa`, `T` is title `0x426c7b4d`, `L1/L2/L3` are `0xda473d2f/30/31`, and `B` is help back text `0x426c7b4e`. Text node size is its scale, not a bounding rectangle.

| Package | Nodes (total; text) | Message position (x, y); scale | Other text nodes |
|---|---:|---|---|
| GenericDialog.fng | 55; 6 | 0, -3; 0.75 | T, L1–L3 |
| GenericDialog_SMALL.fng | 55; 6 | 0, -3; 0.75 | T, L1–L3 |
| GenericDialog_MED.fng | 55; 6 | 0, -20; 0.75 | T, L1–L3 |
| GenericDialog_LARGE.fng | 58; 6 | 0, -33; 0.75 | T, L1–L3 |
| GenericDialog_Animate_SMALL.fng | 65; 6 | 0, -3; 0.75 | T, L1–L3 |
| GenericDialog_Animate_MED.fng | 65; 6 | 0, -20; 0.75 | T, L1–L3 |
| GenericDialog_Animate_LARGE.fng | 68; 6 | 0, -33; 0.75 | T, L1–L3 |
| GenericDialog_ThreeButton.fng | 59; 6 | 0, -60; 0.75 | T, L1–L3 |
| IG_GenericDialog_SMALL.fng | 67; 7 | 0, 1; 0.75 | T, L1–L3 |
| IG_GenericDialog_MED.fng | 67; 7 | 0, -10; 0.75 | T, L1–L3 |
| IG_GenericDialog_LARGE.fng | 59; 7 | 0, -26; 0.75 | T, L1–L3 |
| UI_InGameDialog.fng | 67; 7 | 0, 1; 0.75 | T, L1–L3 |
| HelpDialog_SMALL.fng | 51; 5 | -281, -60; 0.9 | T, B |
| HelpDialog_MED.fng | 51; 5 | -278, -107; 0.9 | T, B |
| HelpDialog_LARGE.fng | 51; 5 | -279, -162; 0.9 | T, B |

All packages include additional image and group nodes and an event-handler text node. The in-game packages include an additional text node. Frontend packages are in `FRONTEND/FrontB.lzc`, in-game packages in `GLOBAL/InGameCommon.lzc`; the non-animated Generic packages also occur in `GLOBAL/GlobalB.lzc` [data].

The state layer selects the first preferred size whose estimated capacity holds the message: 4/5/6 lines for generic and in-game, 5/6/7 for help. It counts explicit `^` and newline breaks and estimates wrapping at 50 characters per line (380 units divided by 7.5 units per character). These widths and capacities are heuristics [inferred], not measured font layout; long words, variable-width glyphs, and localization can differ. If no size fits it returns LARGE. An existing frame that fits is retained; explicit package lookup also exposes animated and three-button variants. Existing small defaults are preserved when they fit.

## Authored binding (all three families)

The authored view now binds every dialog family; selecting their metadata provides a working dialog. Two-button sheets keep `button1`/`Button2` with `L1`/`L2` and the two label texts; three-button sheets additionally require `button3` and `L3`, and help sheets require the `B` caption (a sheet missing a required node fails closed and is not drawn). In a help sheet the node `T` carries the title and `M` the body; the console-only glyph `0x35e8f0b2` is hidden on PC, and the sheet has no focusable node (focus value `-1`). Text set by the spec overrides the authored sample; an empty spec text keeps the authored string, and language-table text takes precedence over both when the key exists.

Measured composed geometry [data] (`build/ugui_demo --dump`, `reports/parity/dlghelp/`):

- Three-button row strips are 512 x 42 at composed centres y = 164 (L1, bottom), 120 (L2), 76 (L3, top); `button1` is the bottom row. Navigation steps Up toward L3 and Down toward L1 without wrapping; the initial focus is the middle row.
- Help caption `B` sits at composed centre (220, 87) in SMALL and (220, 137) in MED, extent x 172..268, y ±10; LARGE continues the +50 step to y = 192 [inferred]. The caption row is the only tap target; the body `M` is left-justified at 0.9 scale.
- Help body capacity measured from the authored sheets: SMALL holds 1 message line (30-char sample), MED 2 (46-char), LARGE about 3 (~60) [inferred]. The size picker compares its line estimate against {1, 2, 3}.
- Two-button pills sit at x ±151, y 74 (SMALL), shifted by the frame's button row.

Result codes (`GdResult` in `src/gui_dialog.h`, host-facing values from `pause_menu_take_confirm`): `GD_RES_NONE = 0`, `GD_RES_FIRST = 1` and `GD_RES_SECOND = 2` keep their historic values; `GD_RES_THIRD = 3` (the L3 row of a three-row sheet) and `GD_RES_CANCEL = 4` occur only on three-row and help sheets. Back answers `GD_RES_CANCEL` there but keeps the historic `GD_RES_SECOND` on two-button sheets (the pause undim path and the career shop depend on it); Back or Enter or a caption tap closes a help sheet. The profile-delete dialog (`GD_DELETE_PROFILE`) uses the three-button sheet with rows Delete (`0x56b00632`), No (`0x61c`), Cancel (`0x541389e5`) top-to-bottom L3/L2/L1 [inferred ordering], and its message (`0x2c8ec31d`) keeps the literal `%s` placeholder — the override path does not format it. The help dialog (`GD_HELP`) defaults its title to Help (`0x169268`) and its caption to Back (`0x1335f0`).

Hosts: the pause adapter shows a lone dialog over the running scene with `pause_menu_confirm(p, kind)`, then `pause_menu_dialog_prepare(p)` binds the authored package; the update returns `FE_ACTION_RESUME` when it closes and `pause_menu_take_confirm(p)` reports the answer. Pause-screen entries choose their sheet through the additive `PauseEntry.dialog` field (0 keeps the two-button default). Retail hosts that would surface these dialogs (the options-screen help bar, the profile-delete prompt) live in files this task does not own; wiring them is a host change of the shape above. `make dialog-retail-test` renders each family over the user-supplied install and checks the results end to end.
