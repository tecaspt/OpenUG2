# Task E — Quick Race polish, Options and Profile

Status: all three Task E priorities are implemented and verified in desktop/GLES. Host wiring and
end-to-end game verification remain with the orchestrator, as assigned. The user authorized all priorities, superseding the original stop-after-priority instruction.

[data] Labels.bin and English.bin contain TRACK_NAME_4202, but its text is the placeholder "4202".
TRACK_NAME_4201 is "Bayview Bridge". The event language-key hash is correct; this case was not a
missing string-table entry. No region-name substitution has been established from the data.
[data] The complete 0x34148 path-node payloads of 4201/4202/4203 match byte for byte in the same region.
[inferred] A numeric/missing caption can use a named same-region/same-mode layout only if its complete
node payload matches and all named matches agree. Conflicting matches, blank/missing names and numeric
placeholders fail closed: no invented caption. The launch event id remains the actual choice.

[data] TRACKS/TRACKMAP4201.BIN supplies texture TRACKMAP4201 (512x512), successfully decoded by a fresh
bundle-parser probe. FrontB contains TRACK4000_MAP, but does not contain this per-track map.
[inferred] The resolved layout map is bound to the selected parked thumbnail and large map preview.
Other parked pool objects remain hidden; full retail scrolling/pool assignment is not claimed.
[data] FRONTEND/FrontB.lzc contains SECONDARY_LOGO_MIATA and SECONDARY_LOGO_RX7 (256x64); the MIATA
texture was independently decoded. The car screen's model-image placeholder is 256x64.
[inferred] SECONDARY_LOGO_<catalogue car> replaces that placeholder. These are model badges, not car
photographs. The host's frozen 3D preview is unchanged; manufacturer/stats remain unimplemented.

[inferred] The texture loader retains runtime map buffers until the menu is destroyed, and appends textures
transactionally. No retail buffers, dump text or screenshots belong in the repository. Missing names
or art are reported through fem_status; no guessed city name, numeric caption or unrelated map is used.

## Verification and artifacts

[observed] New synthetic matching/name/alpha tests: 16/0 in desktop and N2_GLES. Modified sources and
runtime-check source compile with -Werror in both configurations. Runtime fe_polish_check passes in
both configurations: drag variant name association, retained map/thumbnail, model badge, preview.
[observed] Required suite passes in desktop and in a forced N2_GLES rebuild: ugui 10034/0; pause vectors/authored/options/binding/behavior
PASS; controls 1929/0 and SDL 107/0; fe-track 11/0; postrace 9/0; fe-binding 8/0; check-gl OK;
options-menu 1448/0; settings 2304/0; settings-boundary PASS. Existing backend and fixture warnings
remain; none are in modified/new Task E source files. Tests use -O0 to reduce shared-machine build load. The full GLES run forced recompilation of all
prerequisite binaries; an earlier cached-binary run was not used as proof of a GLES rebuild.
[observed] Current-settings-copy check: SKIP because no current file exists at the default configured
location and no XDG_CONFIG_HOME override is set. Settings code/format is unchanged; synthetic legacy
load/round-trip and boundary tests pass. No original user settings were modified.

[observed] Before/after screenshots were inspected by eye. Relative paths beneath the private M2 report
root (the full root is recorded in TASK-E-LOG.md):

- Before: before-desktop/track.png, before-desktop/car.png.
- After desktop: after-alpha-desktop/track.png, after-alpha-desktop/car.png.
- After GLES: after-alpha-gles/track.png, after-alpha-gles/car.png.

[observed] After captures show the real layout caption, selected thumbnail/large map and model badge.
The rendering helper uses a synthetic catalogue with installed FNGs/textures; race-kind/distance fixture
values are not evidence of retail race metadata. The GL-free runtime check uses the drag kind for the
4201/4202 association. Actual main.c end-to-end game checks remain with the orchestrator.

## Scope and remaining work

[inferred] Layout alias naming, map alpha interpretation and assignment to the selected pool image
still benefit from owner retail captures. No region-name or numeric-id fallback is invented. Exact
multi-thumbnail scrolling, manufacturer badges, stats and unlock/filter behavior are not completed.
Options/Profile (priorities 2/3) were initially deferred; see the completion work below for the resumed scope.

[observed] Early compiler/probe attempts failed to link or timed out waiting for the shared lock.
Those attempts were not counted as PASS; later corrected checks are the passing evidence above.

[data] Manufacturer art is also in FrontB: the full key CARSELECT_MANUFACTURER_MAZDA matches texture
0xC332AE6D (128x64). The left manufacturer field is still hidden because no generic car-to-manufacturer
association was decoded; this change does not introduce a hardcoded brand table. The right model-badge
field is bound and visible. This distinguishes missing association data from a missing art package.

## Front-end Options and Profile

[inferred] Front-end Main now offers Options and Profile. Options uses UI_OptionsMain and the existing
Display/Customize/defaults model, plus typed-profile Audio/Player/Gameplay pages in UI_Options. The
new pages are enabled only with an explicit profile; existing pause callers retain their prior model.
[inferred] Profile uses UI_ProfileManager and UI_VirtualKeyboard for touch name entry; the current
name appears in the retail heading. New/rename/load and confirmed non-active deletion use the existing
profile container. Active-profile deletion is refused. No career fields or unknown payload bytes are
reinterpreted. The settings/profile binary format is unchanged; ActiveProfile is an additive INI key.
[inferred] Audio/Player/Gameplay edits request host application and save the active profile. Graphics
requests use separate apply/reinit bits; no renderer/audio globals are touched by the FE controller.
[observed] Final lifecycle/model tests pass in desktop/GLES, including missing/failed load retaining
the active snapshot and new/rename activation-failure rollback. New/modified sources and runtime
test helpers compile to objects with -Werror in both configurations. Full required regressions pass
again in desktop and a forced GLES rebuild: ugui 10034/0, controls 1929/0 + SDL 107/0, options 1448/0,
gui-art 1860/0, settings 2304/0, settings-boundary PASS, fe-track 11/0, postrace 9/0, fe-binding 8/0,
pause vectors/authored/binding/options/behavior PASS and check-gl OK. Existing backend/fixture
warnings remain; no warnings originate in Task E files. Settings.c/h and gui_view.c/h are unchanged.
[observed] Final installed-asset runs pass in both backends: Main→Options and all pages, Customize/
defaults, tap Back, touch keyboard A/Done, create/rename/load/confirmed-delete, saved audio reload,
and missing-art no-mutation. A second INI handle writes a pause-owned fixture key after FE creation;
that key survives FE saves, exercising the store-refresh behavior. No original settings are changed.
[observed] Final evidence logs relative to the private M2 report root: m2-models-complete.log,
m2-regression-desktop.log, m2-regression-gles.log and m2-render-clean.log. Earlier attempts with a
profile-name formatting warning are superseded by the corrected clean compile/run.
[observed] Inspected all 13 final PNGs in each of m2-desktop/ and m2-gles/:

- m2-main.png, options-hub.png, audio.png, display.png, customize.png, defaults-dialog.png.
- gameplay.png, player.png, profile-manager.png, profile-keyboard.png.
- profile-created.png, profile-renamed.png, profile-delete-confirm.png.

[inferred] Profile action order/rename caption, keyboard/pad navigation, row limits/order and
immediate profile-save policy need owner retail-PC captures for behavioral parity. Load/Delete use
typed names; there is no claimed retail profile-list implementation. File writes retain the existing
backend's non-atomic behavior. Audio mixer buses/stereo conversion, device reset and player/gameplay
application are host work; exact available hooks and limitations are in M2-OPTIONS-INTEGRATION.md.
[observed] Current-settings-copy validation remains SKIP: no current default settings.ini exists.
Synthetic legacy/container/INI tests pass, and the existing file formats are unchanged.
