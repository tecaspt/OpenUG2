# Task B status

R1 partial: candidate text/master/map-pool identities independently checked against retail bundles:
278 matching occurrences, zero failures. See SPEC_GUI_FE.md for tagged claims and unresolved semantics.
No new screen renders yet. U5/U6/U7 NOT IMPLEMENTED; full task NOT DONE.

Verification: fe_binding_check compiles without warnings; desktop runtime PASS (278/0).
GLES, pause-menu, controls, ugui and check-gl checks NOT RUN in this unit.
Retail PNGs: none in this unit.

## FE host fixes checkpoint

- Free Run carries STREAML4RA and current car.
- `fem_show_error` opens QR_TRACK and retains host text in status (no error presentation yet).
- Touch Back pill on existing Main/QR mode screens; press/release inside activates Back,
  release outside and focus loss cancel. Uses retail pill kit as the allowed platform adaptation.
  Still needs extending when U5/U6 screens are implemented.
- Synthetic identity tests PASS in desktop and N2_GLES, 8 checks each, with -Werror.
- `fe_menu.c` GLES compile with -Werror PASS; smoke desktop compile: no FE warnings.
- Regression PASS: check-gl OK; ugui 10034/0; pause authored/state/binding/behavior checks PASS;
  controls 1929/0 and SDL controls 107/0. Existing ugui fixture format-truncation warnings remain.
- Runtime smoke PASS: Free Run request, touch Back QR mode→Main, host error status.
  Scratch harness uses a synthetic catalogue, so this does not verify catalogue event selection.
- Visually checked `/home/nike/openug2-dev/reports/parity/task-b/fe_qrmode.png`:
  existing retail mode screen renders with visible bottom-left Back pill.
  Other smoke PNGs in the same directory were generated but not reviewed in this pass.

This remains a partial task. R1 semantics and U5/U6/U7 remain outstanding. Do not mark M1 complete
or remove the first-event shortcut until the real selection flow and results are implemented and tested.
