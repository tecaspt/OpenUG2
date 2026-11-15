# Task E integration — priority 1

Baseline: f507ba2. No main.c/Makefile changes are required for the Quick Race polish.
The existing fem_create/fem_update/fem_draw lifecycle and preview accessor remain unchanged.
Map loading uses runtime assets.dir and TRACKS/TRACKMAP<resolved-layout>.BIN; retain those install files.

No Options/Profile screens or settings-format changes are included in this priority.

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
