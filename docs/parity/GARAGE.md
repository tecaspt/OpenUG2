# Customize / Garage

[observed] Task J starts at 486ce08. Worker owns only the scoped garage/FE/config files; host and build
integration is supplied separately. Retail assets/dumps/captures remain outside the repository.
[inferred] Preview debounce is0.3 seconds. The host acknowledges each serialized request. Preview
never changes the kept configuration. Accept keeps only after host success; Back requests the entry
snapshot and waits for successful restoration. Failed loads retain the last displayed config and
report a diagnostic. Inputs cannot change an in-flight final Keep/Restore operation.
[data] Local KIT/STYLE mesh variants and supported shared library sockets supply options.
[inferred] Only fully assembled CPU scenes with finite vertices, valid indices, positive triangles,
and a matching selected part/kit are exposed. GPU texture/upload proof is separate.
[observed] Priority1 is complete: Main Customize opens the category package; visual categories and
part options use the current car catalogue and host-acknowledged scene changes. Priorities2–5 remain.

## Retail presentation and scope

[data] UI_ChooseCustomizeCategory has55 objects. UI_GenericParts_Browser has202; its application
image pool repeats hash87257585, masterGUIDe2958/hashef8bb088, and titlehash6c9e933e. A separate
binding validates explicit pool identities instead of treating duplicate names as unique.
[data] SHOP_ICON_BODY/PERFORMANCE/GRAPHIC/INTERIORS and per-part textures are present in the installed
asset catalogue. Language and texture keys differ for headlights, taillights and muffler tips;
these are bound independently. Prices, total cost, placeholder player/bank and unavailable locks
are hidden because this slice supplies no purchase/unlock data.
[inferred] Visual options are grouped into kit presets followed by every slot with alternatives.
Kit presets follow the existing KITnn host behavior; the English caption distinguishes them from
unsupported KITW/wide/carbon variants. Per-option tiles reuse the genuine category icon; the world
car supplies the actual geometry preview. Socket compatibility is not recovered retail shop eligibility.
[inferred] Catalogue filtering builds each choice over the stock base. Mixed configurations are
validated again by the host before acknowledgement. Rejected combinations retain the previous scene.
The current-world front-quarter camera and HUD suppression are preview adaptations; no retail garage
world/camera behavior or original category order is claimed. Performance, paint and interiors hub
entries remain disabled; renderable engine/audio parts are available within Visual as requested.

## Priority1 checks

[observed] garage_test passes desktop, N2_GLES and ASan/UBSan: option bounds, debounce, stale serials,
coalescing, failed loads, final-operation input lock, Keep and restoration. LeakSanitizer is NOT RUN
under this ptrace environment; the initial sanitizer attempt failed there and was rerun with leak
scanning disabled while address/undefined instrumentation remained enabled. Save/rename cases await3.
[observed] fe_garage_test passes desktop/GLES: navigation,20-option catalogue, restore handshake,
invalid catalogues and synthetic parts-binding identity rejection. Existing fe-screen, fe-binding,
fe-profile, fe-polish, fe-options, game-flow, postrace(9) and pause-menu vectors pass. No new warnings
originate in owned code; untouched baseline host/backend warnings remain.
[observed] garage_check passes29 player cars and10363 catalogue entries, each independently rebuilt
into valid CPU geometry with its selected kit/part. This does not claim GPU/texture/pose validation
of every entry; the representative full game captures below provide separate GPU proof.
[observed] The integration patches were applied only to an external scratch copy and built for
OpenGL desktop and GLES. Both game runs exited0 with an explicit scripted quit. Host logs show
KIT01 preview/Keep, KIT02 preview, then touch Back restoring KIT01. Initial attempts uncovered
placeholder cost/HUD, an incorrect camera API argument and direct-launch CLI flags; those failed or
superseded runs are not the final PASS. Final proof uses --car GOLF --menu and isolated user storage.
[observed] Reviewed640x480 PNGs under the private report root are
reports/garage/desktop/{hub,slots,parts-before,parts-after,kept,before-cancel,restored}.png and the
same names under reports/garage/gles. Desktop/GLES contact sheets and the kept-frame review confirm
visible stock→KIT01→KIT02→KIT01 geometry and tap-reachable Back without gameplay gauges.
[observed] The production host is not wired by this worker; GARAGE-INTEGRATION.md supplies exact
source/build changes. Persistence, rims, paint/vinyl and performance research are not yet delivered.
