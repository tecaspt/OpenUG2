# Task E integration — priority 1

Baseline: f507ba2. No main.c/Makefile changes are required for the Quick Race polish.
The existing fem_create/fem_update/fem_draw lifecycle and preview accessor remain unchanged.
Map loading uses runtime assets.dir and TRACKS/TRACKMAP<resolved-layout>.BIN; retain those install files.

Priority 1 alone needs no host changes. The complete Options/Profile extension follows below.

New synthetic test target (from the repository root):

```make
fe-polish-test:
	$(CC) -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections $(SDL_CFLAGS) -Isrc tools/fe_polish_test.c src/fe_track.c -Wl,--gc-sections -o build/fe_polish_test
	./build/fe_polish_test
```

Repeat compilation with -DN2_GLES for the GLES configuration. Existing fe-track-test and postrace-test
recipes already compile the modified source files. No new SRC entry is needed.

Run all builds/checks under the owner's heavy lock at nice priority 10, after checking available RAM.
The private log records concrete runtime render commands and before/after artifacts.

Runtime art/name check (requires installed data; exit 77 means SKIP):

```make
fe-polish-check:
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections $(UGUI_FLAGS) $(SDL_CFLAGS) -Isrc tools/fe_polish_check.c src/fe_track.c src/fe_car.c src/gui_view.c src/options_menu.c src/settings.c $(GUI_ART_SRC) src/controls_store.c src/controls_sdl.c $(UGUI_SRC) -Wl,--gc-sections -o build/fe_polish_check $(SDL_LIBS) $(UGUI_IMAGE_LIBS) $(SETTINGS_LIBS) -lm
	./build/fe_polish_check
```

[inferred] TRACKMAP DXT3 surfaces whose entire decoded alpha plane is zero are presented as opaque RGB;
other alpha planes are preserved. This is local to the FE map import, not a global decoder change.
[data] Both inspected TRACKMAP4201 and TRACKMAP4001 have populated RGB and all-zero alpha.

## Full Task E extension (priorities 2 and 3)

[inferred] Append `src/fe_profile.c` to both Makefile SRC and SCREENS_TEST_SRC. Existing FemScreen
values 0–6 are unchanged; FEM_SCR_OPTIONS and FEM_SCR_PROFILE are appended. Main's entries now reach
these screens without new launch-request kinds. Profile roots/config paths use the existing settings
backend defaults; fem_set_storage is an optional override for tests or host-selected locations.

### Host application hooks, anchored to f507ba2

[observed] The existing pause apply block is at main.c:7075–7085. Add this line after each of its
lighting/detail-policy updates so FE Display reflects later pause changes:

```c
fem_set_graphics(fem, &opt_gfx);
```

[observed] At the graphics declarations next to main.c:6702 add persistent audio gains:

```c
float fe_master_gain = 1.0f, fe_engine_gain = 1.0f, fe_tire_gain = 1.0f;
```

[observed] After `FemRequest frq = fem_update(...)` at main.c:7109, add:

```c
unsigned fe_apply = fem_take_apply(fem);
if (fe_apply & (FEM_APPLY_GRAPHICS | FEM_APPLY_REINIT)) {
    const StGraphics *fg = fem_graphics(fem);
    if (fg) {
        opt_gfx = *fg;
        lighting = lighting_policy(fg);
        apply_detail_policy(detail_policy(fg), vehicle_quality_explicit, road_reflections_explicit);
    }
    if (fe_apply & FEM_APPLY_REINIT)
        fprintf(stderr, "front end: display re-init requested; renderer reset hook required\n");
}
if (fe_apply & FEM_APPLY_PROFILE) {
    const StProfile *fp = fem_current_profile(fem);
    if (fp) {
        fe_master_gain = fminf(1.0f, fmaxf(0.0f, fp->audio.master));
        fe_engine_gain = fminf(1.0f, fmaxf(0.0f, fp->audio.engine));
        fe_tire_gain = fminf(1.0f, fmaxf(0.0f, fp->audio.tire));
    }
}
```

[inferred] Graphics edits are saved through st_gfx_save on Back from Display, matching the existing
options policy. FE profile-page edits save the active container immediately and request application.
The mask is consume-once; call it even when the FE has just returned to Main. A persisted active
profile also raises PROFILE at creation/storage reconfiguration, allowing startup application.

[observed] main.c:8384 overwrites g_engine.master_volume every simulation frame. Replace its two
engine/road assignments with these expressions; setting master_volume only in the poll block would
be overwritten and would not persist as an audible gain:

```c
g_engine.master_volume = (race_state==0 ? 0.16f : 0.16f + sp*0.5f) * fe_master_gain * fe_engine_gain;
g_road_vol = sp*sp*0.35f * fe_master_gain * fe_tire_gain;
```

[observed] The baseline exposes engine master_volume and road gain, but no general speech/menu/game/
SFX bus setters. Those typed profile fields are persisted and available through fem_current_profile;
remaining bus application belongs to the audio owner. These expressions apply supported engine/road
gains, not a complete audio mixer or stereo/mono conversion. No Task E engine application is claimed.
[observed] Actual resolution/device reset is already unwired in the baseline pause path. REINIT is
exposed explicitly; the orchestrator must connect the same device-reset implementation for both paths.
[inferred] Player/gameplay fields are likewise available in the borrowed active-profile snapshot.
The HUD/camera/drivetrain owners must map supported fields to their runtime policy. No HUD/audio or
physics globals are changed by Task E. The getter's pointer must not be retained across storage reset
or destruction. Existing pad persistence still uses its OPENUG2 carrier; this task does not rewrite
opaque input records or silently replace the pause Controls path.

### Profile UI and storage

[inferred] UI_ProfileManager shows the current name in its heading and offers Create/Rename/Load/Delete.
UI_VirtualKeyboard supplies letters, Space, Back and Done; a native touch Back remains available.
Touch letters/Done work without an iOS native keyboard. Desktop text input, Backspace/Enter and pad
navigation/A/Start are also handled. Names use the existing 1–15-byte filename validation. Delete
requires two Done activations and refuses the active profile. Name collisions are refused.
[inferred] ActiveProfile is an additive machine-store string; no existing graphics/profile bytes
change format. Existing OPENUG2 profiles are loaded as the initial fallback. New profiles use the
existing clean-room defaults; rename preserves the existing payload, including unknown bytes.
Profile file writes retain the settings backend's existing non-atomic save behavior. Activation errors
are reported and rollback is attempted; no atomic multi-file transaction is claimed.

### Additional exact test recipes

```make
fe-profile-test:
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tools/fe_profile_test.c src/fe_profile.c src/settings.c -o build/fe_profile_test $(SETTINGS_LIBS) -lm
	mkdir -p build/profile-fixtures
	./build/fe_profile_test "$$(mktemp -d build/profile-fixtures/run.XXXXXX)"

fe-options-test:
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc tools/fe_options_test.c src/options_menu.c src/settings.c src/gui_timing.c src/gui_carousel.c src/gui_dialog.c src/gui_layout.c src/controls_menu.c src/controls_bind.c src/ugui/ugui_util.c -o build/fe_options_test $(SETTINGS_LIBS) -lm
	./build/fe_options_test

fe-m2-check:
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc tools/fe_m2_check.c $(SCREENS_TEST_SRC) -o build/fe_m2_check $(SDL_LIBS) $(GL_LIBS) $(UGUI_IMAGE_LIBS) $(SETTINGS_LIBS) -lm
	SDL_VIDEODRIVER=offscreen ./build/fe_m2_check "$(OPENUG2_DATA)" "$(M2_REPORT_DIR)"
```

[inferred] SCREENS_TEST_SRC must include fe_profile.c before using these recipes. M2_REPORT_DIR must
be an existing directory outside the worktree for retail-derived PNGs. The runtime helper creates
isolated profile/INI fixtures there and never changes the user's original settings. For GLES force
rebuild with CC including -DN2_GLES and GL_LIBS=-lGLESv2; cached desktop executables are not GLES evidence.

[inferred] FE defaults use the same st_gfx_caps_full(..., 1) baseline as pause_menu_build. If the host
adds actual renderer capabilities, call fem_set_caps(fem, &actual_caps) before entering Options.
Do not read opt_caps before pause_menu_build initializes it. fem_set_graphics may also be called
with saved_gfx after fem_create to adopt the host startup graphics selection. The machine store and persisted profile data
are reloaded on Main → Options/Profile entry, preserving intervening pause Controls keys/carrier updates.
