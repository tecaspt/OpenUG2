/* Host adapter for the pause menu. Game state and audio stay with the host. */
#ifndef OPENUG2_PAUSE_MENU_H
#define OPENUG2_PAUSE_MENU_H
#include <SDL.h>
#include "frontend/frontend.h"
#include "options_menu.h"
#include "pause_entries.h"
#include "pause_screen.h"
/* Verification captures are bounded to 6,000 ten-millisecond UI substeps. */
#define PAUSE_MENU_CAPTURE_MAX_MS 60000.0f
int pause_menu_capture_duration_valid(float elapsed_ms);

typedef struct PauseMenu PauseMenu;
PauseMenu *pause_menu_create(const char *dataroot);
void pause_menu_destroy(PauseMenu *p);
void pause_menu_open(PauseMenu *p);
int pause_menu_is_open(const PauseMenu *p);
void pause_menu_event(PauseMenu *p, const SDL_Event *e, SDL_Window *win);
/* Window-point input; dimensions are logical window units, as in SDL events. */
void pause_menu_pointer(PauseMenu *p, float x, float y, int width, int height);
/* Describes the situation the pause is opened in (game mode, race activity, ...); applied at the next
 * pause_menu_open. Default: career free roam (Return to Explore Mode, Options, Replay, Quit to Main
 * Menu, SPEC_GUI_ART 2.2). NULL restores the default. */
void pause_menu_set_context(PauseMenu *p, const PauseContext *ctx);
/* Opt-in host adaptation: add the retail World Map entry/confirmation in career
 * free roam at next open. Default off preserves the authored context lists. */
void pause_menu_set_world_map_enabled(PauseMenu *p, int enabled);
/* Options entry (SPEC_GUI): the entry exists once the host lends the graphics settings it edits (the
 * Options availability condition also needs PauseContext.options_ok). Options opens the hub
 * (UI_PauseOptionsMain.fng) -> Display (page 1) -> Customize. The lent objects must outlive the menu;
 * the store may be NULL. */
void pause_menu_set_options(PauseMenu *p, StGraphics *gfx, const StGfxCaps *caps, StRegStore *store);
void pause_menu_set_mouse_look(PauseMenu *p, int *enabled); /* host-owned OpenUG2 camera setting */
/* Controls screen (CONTROLS_SPEC): lends the bindings and joysticks and verifies the original resources the
 * screen needs; the hub gets its fifth entry only when they are all present. Call after pause_menu_set_options. */
struct CxInput;
void pause_menu_set_controls(PauseMenu *p, struct CxInput *cx);
int pause_menu_options_open(const PauseMenu *p);
/* The options state machine, for the apply/re-init hook (om_take_reinit_request ...). NULL if p is NULL. */
OptionsMenu *pause_menu_options(PauseMenu *p);
const char *pause_menu_caption(const PauseMenu *p);
/* The pause screen state machine (entries, focus), for inspection. */
const PauseScreen *pause_menu_screen(const PauseMenu *p);
/* Resources may be unavailable while state/input remains usable. */
int pause_menu_visual_available(const PauseMenu *p);
const char *pause_menu_status(const PauseMenu *p);
/* M9: the in-game language table entry for `key` (retail string), or `fallback` when the key or table is missing. */
const char *pause_menu_text(const PauseMenu *p, uint32_t key, const char *fallback);
/* M6: show one two-button dialog (GdKind, e.g. GD_ENGAGE_RACE) on its own over the running game. While it is
 * up pause_menu_is_open() is true, so the host routes input and draws as for the pause screen; it closes with
 * FE_ACTION_RESUME from pause_menu_update, and pause_menu_take_confirm() then returns the answer once
 * (GdResult: 1 first button / Yes, 2 second / No / Back, 0 none). Returns 0 if the menu is already open. */
int pause_menu_confirm(PauseMenu *p, int kind);
int pause_menu_take_confirm(PauseMenu *p);
/* Confirmation dialog over the pause screen or the options screens (UI_InGameDialog.fng OK/Cancel,
 * GenericDialog_SMALL.fng Yes/No): the authored dialog, or logic only when its resources are unavailable. */
int pause_menu_dialog_open(const PauseMenu *p);
int pause_menu_dialog_visual_available(const PauseMenu *p);
const char *pause_menu_dialog_status(const PauseMenu *p);
/* Empty unless the language table is unavailable (English fallback captions are used). */
const char *pause_menu_caption_status(const PauseMenu *p);
FeAction pause_menu_update(PauseMenu *p, float seconds);
void pause_menu_draw(PauseMenu *p, int width, int height);
/* Deterministic verification-only UI replay and populated authored reference. Replay events are
 * absolute-time `ms:Name[:x:y]` items: open Left Right Up Down Enter Continue Back Escape Start right_click
 * pointer press release hover. */
int pause_menu_capture_replay(PauseMenu *p,const char *events,float elapsed_ms,int width,int height);
int pause_menu_draw_reference(PauseMenu *p,int width,int height,float elapsed_ms,const char *events);
#endif
