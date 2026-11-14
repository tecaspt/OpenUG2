# Task B: M1 retail screens

Implementation delivered on parity/m1-screens. Main.c/Makefile wiring remains with task A;
[M1-SCREENS-INTEGRATION.md](M1-SCREENS-INTEGRATION.md) gives concrete code and test recipes.
No main.c, Makefile, roadmap, changelog, handoff or progress files were edited by Task B.

## Units

- R1: full declarative identities for all objects in seven packages; independent runtime check
  matches 1224 occurrences across FrontB and InGameRace/Drag/Drift, zero failures.
  Tagged claims and fidelity limits: [SPEC_GUI_FE.md](../cleanroom-settings/SPEC_GUI_FE.md).
- U5: Track Select, both retail options packages, generalized gv_list_bind_rows/sync_rows;
  original pause/options APIs preserved. Event filtering, real selection, laps/difficulty request.
- U6: non-traffic car selection, 0.3-second preview debounce, complete FemRequest routing.
  Native touch Back target and host error caption implemented. Free Run explicitly targets STREAML4RA.
- U7: player Results, Next → post-race carousel; Restart / Continue / Quit to Main once-only actions.
  Shared retained buffer loads all three race bundles without exceeding UgAssets.files[8].

## Evidence

Logs are local in `/home/nike/openug2-dev/reports/parity/task-b/`:

- `final-tests.log`: synthetic tests desktop/GLES: selection 11/0, identity 8/0, postrace 9/0.
- `verification-final.log` and `verification-last.log`: all owned source files and runtime-check source compile with -Werror
  in desktop and N2_GLES; independent metadata check 1224/0; required regressions.
- Required regressions: check-gl OK; ugui 10034/0; GUI view 194/0; pause authored/state/binding/
  behavior vectors PASS; controls 1929/0 and SDL controls 107/0. Existing ugui fixture truncation
  warnings and unrelated backend/settings warnings are not warnings in Task B's source files.
- `visible-back.log`: desktop and GLES offscreen installed-art checks PASS. The helper uses a
  synthetic catalogue and does not load a world. It verifies selected request fields, delayed
  preview, touch Back (including a visible-pixel assertion on every page), Results → post-race menu → Restart.
- One earlier verification command terminated with status 143 during compilation. Its partial
  output was not counted as PASS; the mandatory suite was rerun successfully.

PNG paths (local retail-derived artifacts, never committed), under each of:

`/home/nike/openug2-dev/reports/parity/task-b/desktop-verified/`
`/home/nike/openug2-dev/reports/parity/task-b/gles-verified/`

Files: `track.png`, `options.png`, `mode_options.png`, `car.png`, `results.png`, `postmenu.png`.
The screens were inspected by eye in both configurations. Car's empty black background is the test
harness: task A supplies the frozen world and handles fem_preview_car's switch request.

## Fidelity limits and integration gate

[inferred] Screen order and action mapping, option limits, thumbnail assignments, unlock/filter
behavior and logo/stat values still benefit from owner retail captures. The parked track thumbnail
pool is omitted; missing event maps are hidden. Car manufacturer/stat placeholders and unknown
opponent results are hidden rather than showing fabricated or authored example values.
Both retail options pages currently expose the same persistent laps/difficulty model.

Whole-game S2/S3, actual preview switching, race-finish/results timing and restart through main are
NOT RUN by Task B: they depend on task A applying the documented main.c/Makefile hooks. These are
integration work, not a claim of complete retail parity or a blocker on delivering these units.
