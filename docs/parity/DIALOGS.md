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

The current authored view binds two-button nodes. Help packages lack those nodes and the three-button state is not implemented; selecting their metadata does not yet provide a working Help or three-button dialog. Actual rendered text may come from the language table after this state-layer estimate, so selection based on fallback text does not guarantee a fit for translated text.
