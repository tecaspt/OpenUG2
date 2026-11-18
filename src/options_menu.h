/* options_menu.h -- state machine of the graphics option screens: the pause options hub, Display and
 * Customize, with the Default Settings confirmation.
 *
 * Implements docs/cleanroom-settings/SPEC_GUI.md and SPEC_GUI_ART.md (section numbers in comments
 * refer to them) on top of the settings backend of SPEC.md (src/settings.h). This module is GL-free,
 * SDL-free and draws nothing: the screens are the retail authored packages, evaluated by the view
 * (gui_view.h) from the state published here. The host feeds semantic inputs and a frame time, polls
 * events, and reads/writes the settings it lent to the menu.
 *
 * Screens (SPEC_GUI 1.1, SPEC_GUI_ART 2.5): the in-game hub (UI_PauseOptionsMain.fng, an icon
 * carousel: Audio, Display, Gameplay, Player), Display (4 rows) and Customize (up to 22 rows plus an
 * optional spacer). Leaving a screen runs its exit script (gui_timing.h: 140 ms front end, 320 ms in
 * game) and the transition happens at exit-complete; input is ignored meanwhile.
 *
 * Controls (CONTROLS_SPEC, CONTROLS_DECISIONS D1): a fifth hub entry, present only while the host reports that
 * every original resource the screen needs is available (om_set_controls); it opens the Controls screen
 * (controls_menu.h), whose Back returns to the hub at once.
 *
 * Re-initialisation hook (SPEC_GUI 5.4): the menu never touches the renderer. Changes raise "apply
 * requested" / "display re-init requested" flags immediately; the host consumes them with
 * om_take_apply_request()/om_take_reinit_request() at the start of its next frame.
 * Design log: docs/cleanroom-settings/IMPL_DESIGN_GUI.md. */
#ifndef OPENUG2_OPTIONS_MENU_H
#define OPENUG2_OPTIONS_MENU_H

#include <stdint.h>
#include "settings.h"
#include "controls_menu.h"
#include "gui_carousel.h"
#include "gui_dialog.h"
#include "gui_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OM_VISIBLE_ROWS 8       /* SPEC_GUI 2.3: V = 8 on all three screens */
#define OM_MAX_ROWS 24          /* 22 rows + spacer, with headroom (SPEC_GUI 2.2: <= 23) */
#define OM_ARROW_SIZE 24.0f     /* authored row arrow size (tvg09 numbers are consistent with 24) */

/* ---- list engine (SPEC_GUI 2.5), pure and shared by every screen ---------------------------- */
typedef struct OmList {
    int n;                       /* number of rows */
    int selectable[OM_MAX_ROWS];
    int focus;                   /* 0-based */
    int top;                     /* 0-based first visible row */
} OmList;
/* Initial window: <= V rows -> top 0; else top = focus when focus <= n-V, else n-V (0-based). */
void om_list_init(OmList *l, int n, const int *selectable, int initial_focus);
/* dir < 0 up, dir > 0 down. Non-wrapping; skips non-selectable rows; adjusts the window minimally.
 * Returns 1 when the focus moved. */
int om_list_move(OmList *l, int dir);
int om_first_selectable(const OmList *l);   /* 0-based; 0 when there is none */

/* ---- slider rule (SPEC_GUI 2.7) --------------------------------------------------------------- */
/* Steps *v by `dir` (<0 left, >0 right) within [min,max]. Returns 1 when the value really moved. */
int om_slider_step(int *v, int min, int max, int dir);
/* Fill fraction (v-min)/(max-min), 0 when the range is empty; v is clamped into the range first. */
float om_slider_fill(int v, int min, int max);

/* ---- identifiers and data keys ------------------------------------------------------------------ */
typedef enum { OM_CTX_FRONTEND = 0, OM_CTX_INGAME } OmContext;           /* SPEC_GUI 1.2 */
typedef enum { OM_SCREEN_CLOSED = 0, OM_SCREEN_DISPLAY, OM_SCREEN_CUSTOMIZE, OM_SCREEN_HUB, OM_SCREEN_CONTROLS, OM_SCREEN_GAMEPLAY, OM_SCREEN_AUDIO, OM_SCREEN_PLAYER } OmScreen;
typedef enum { OM_ROW_CHOICE = 0, OM_ROW_SLIDER, OM_ROW_BUTTON, OM_ROW_SPACER } OmRowKind;  /* 2.1 */
typedef enum {                   /* what the row does */
    OM_ID_OPTION = 0,            /* an ordinary graphics option, `gfx` names its slot */
    OM_ID_RESOLUTION, OM_ID_LEVEL, OM_ID_CUSTOMIZE, OM_ID_DEFAULTS, OM_ID_SPACER, OM_ID_MOUSE_LOOK, OM_ID_PROFILE,
    OM_ID_N                      /* count of ids (task AQ: the view keeps one value memo per id) */
} OmRowId;
typedef enum {                   /* how the value text is produced (SPEC_GUI 3.1, 4.1) */
    OM_VT_NONE = 0, OM_VT_RESOLUTION, OM_VT_ONOFF, OM_VT_LOWHIGH, OM_VT_VSYNC
} OmValueText;

typedef struct OmRow {
    OmRowKind kind;
    OmRowId id;
    int slot;                    /* 1-based object slot n (SPEC_GUI 2.2) */
    uint32_t label_key;          /* language label key; 0 for a spacer */
    const char *label;           /* English text of that key */
    int selectable;
    int gfx;                     /* ST_GFX_* slot edited, or -1 */
    int min, max;                /* sliders */
    OmValueText vt;
    int needs_reinit;            /* a change raises the display re-init request (SPEC 4.4) */
} OmRow;

/* Label keys, SPEC_GUI 9 / SPEC_GUI_ART 2.5 */
#define OM_KEY_ON 0x0000063Cu
#define OM_KEY_OFF 0x0000CCFAu
#define OM_KEY_VSYNC_OFF 0x88576CFFu
#define OM_KEY_LOW 0xB55E7678u
#define OM_KEY_HIGH 0x612AF8A6u
#define OM_KEY_TITLE_DISPLAY 0x4204D527u
#define OM_KEY_TITLE_PAUSE 0x1D7BB6C9u
#define OM_KEY_TITLE_PAUSE_DISPLAY 0xEF1BB23Bu
#define OM_KEY_TITLE_CUSTOMIZE 0x381FE1E9u
#define OM_KEY_DEFAULTS_MESSAGE 0x2BBCCA6Bu
#define OM_KEY_YES 0x0000F770u
#define OM_KEY_NO 0x0000061Cu

/* Package lookup names and keys (SPEC_GUI 1.2); NULL/0 when the screen is closed. The hub exists only
 * in game (its front-end counterpart UI_OptionsMain.fng is not modelled). */
const char *om_package_name(OmContext ctx, OmScreen screen);
uint32_t om_package_key(OmContext ctx, OmScreen screen);
const char *om_hub_package_name(OmContext ctx);          /* Back target of Display */
const char *om_dialog_package_name(void);                /* GenericDialog_SMALL.fng */
int om_exit_ms(OmContext ctx, OmScreen screen);          /* exit-complete time of the screen's package */

typedef enum {
    OM_IN_NONE = 0, OM_IN_UP, OM_IN_DOWN, OM_IN_LEFT, OM_IN_RIGHT, OM_IN_ACCEPT, OM_IN_BACK, OM_IN_START,
    OM_IN_RESET                  /* Controls: the Reset Keys To Default request (CONTROLS_SPEC 4.6) */
} OmInput;

typedef enum {                   /* exactly-once events for the host */
    OM_EV_NONE = 0,
    OM_EV_LEAVE_TO_HUB,          /* Display left with Back: values were written (5.1). Front end: the screens close and the
                                    host goes to the options hub. In game the hub screen of this menu becomes current. */
    OM_EV_LEAVE_TO_PAUSE,        /* in-game hub left with Back: the pause screen is next (SPEC_GUI_ART 2.5) */
    OM_EV_RESUME_GAME,           /* Start (in-game hub or Display): leave and resume, nothing written (5.2) */
    OM_EV_CONTROLS_CHANGED       /* Controls left with changed bindings: the host persists them (D4) */
} OmEvent;

typedef struct OmConfig {
    OmContext ctx;
    int row1_label_has_caret;    /* localized "Car Reflection Update Rate" contains '^' (4.1) */
    int cpu_mhz;                 /* inputs of the performance level (SPEC 4.5) */
    double gpu_score;
    int *mouse_look;             /* optional OpenUG2 setting; host owns persistence */
    StProfile *profile;         /* optional FE profile; NULL preserves legacy pause behavior */
} OmConfig;
void om_config_default(OmConfig *c, OmContext ctx);   /* modern hardware: 3000 MHz, GPU score 5 */

/* What a leaving transition does at exit-complete. */
typedef enum { OM_EXIT_NONE = 0, OM_EXIT_TO_DISPLAY, OM_EXIT_TO_CUSTOMIZE, OM_EXIT_LEAVE_DISPLAY,
               OM_EXIT_TO_PAUSE, OM_EXIT_RESUME, OM_EXIT_TO_CONTROLS,
               OM_EXIT_TO_GAMEPLAY, OM_EXIT_LEAVE_GAMEPLAY, OM_EXIT_TO_AUDIO, OM_EXIT_TO_PLAYER } OmExit;

typedef struct OmHubEntry {      /* SPEC_GUI_ART 2.5 */
    uint32_t icon_key;
    uint32_t label_key;
    const char *label;           /* English text of the key; NULL for the Controls entry (language file only) */
    int page;                    /* page index set on activation: Audio 0, Display 1, Gameplay 2, Player 3, Controls 4 */
} OmHubEntry;
#define OM_HUB_ENTRIES 4         /* the baseline entries; Controls is a fifth one (OM_HUB_MAX) */
#define OM_HUB_MAX 5
#define OM_HUB_PAGE_CONTROLS 4

typedef struct OptionsMenu {
    OmConfig cfg;
    StGraphics *gfx;             /* lent by the host; edited in place */
    const StGfxCaps *caps;       /* lent by the host */
    StRegStore *store;           /* lent by the host; NULL = nothing is persisted */
    OmScreen screen;
    OmRow rows[OM_MAX_ROWS];
    int nrows;
    OmList list;
    int res_shown;               /* resolution index whose text is displayed, -1 none (3.1) */
    GuiDialog dialog;            /* Default Settings confirmation (6) */
    int exiting;                 /* exit script running */
    OmExit exit_kind;
    float exit_left_ms;
    int apply_requested, reinit_requested;
    int registry_writes, last_write_status;
    OmEvent events[4];
    int nevents;
    /* hub */
    GuiCarousel hub;
    GcPointer hub_ptr;
    int hub_count;               /* entries shown: OM_HUB_ENTRIES, +1 with Controls */
    /* Controls (CONTROLS_SPEC); the binding set and platform names are lent by the host */
    CbBindings *bindings;
    CmPlatform cm_plat;
    int controls_ok;             /* the host verified every original resource of the screen */
    ControlsMenu cm;
    int hub_remembered;          /* 1-based remembered entry, 0 = none (SPEC_GUI_ART 2.5) */
    int page;                    /* page index chosen in the hub */
    /* presentation state polled by the view */
    unsigned build_serial;       /* bumped whenever a screen is (re)built */
    unsigned blink_serial;       /* bumped on Left/Right on a row or hub entry */
    int blink_dir;               /* direction of the latest blink */
    unsigned activate_serial;    /* bumped when a row or hub entry was activated (ACCEPT/click): an
                                  * attempt, not an outcome -- a locked row activates too. Consumed
                                  * by gui_view's navigation-event emission (task AQ). */
    unsigned exit_serial;        /* bumped when an exit begins */
    int profile_requested;
    unsigned dialog_serial;      /* bumped when the dialog opens or closes */
} OptionsMenu;

/* Opens the Display screen directly (SPEC_GUI 1.2). `remembered_focus` is accepted for
 * compatibility and ignored: Display always opens on its first selectable row (SPEC_GUI_ART 4.7).
 * gfx and caps must outlive the menu. The remembered hub entry survives (see om_reset_session). */
void om_open(OptionsMenu *m, const OmConfig *cfg, StGraphics *gfx, const StGfxCaps *caps,
             StRegStore *store, int remembered_focus);
/* Opens the hub in the supplied context (front end or pause Options). */
void om_open_hub(OptionsMenu *m, const OmConfig *cfg, StGraphics *gfx, const StGfxCaps *caps,
                 StRegStore *store);
int om_take_profile_request(OptionsMenu *m);
void om_close(OptionsMenu *m);                    /* drops everything without writing anything */
/* Lends the bindings and platform names and states whether the Controls screen may be offered. Without
 * `available` (or without bindings) the hub keeps its four baseline entries (CONTROLS_SPEC R3). Survives
 * om_open/om_open_hub. */
void om_set_controls(OptionsMenu *m, CbBindings *b, const CmPlatform *plat, int available);
/* A pause session begins: forgets the remembered hub entry (the pause screen builds with it reset). */
void om_reset_session(OptionsMenu *m);
int om_is_open(const OptionsMenu *m);
OmScreen om_screen(const OptionsMenu *m);

void om_input(OptionsMenu *m, OmInput in);
/* Controls screen only: raw key press (not a repeat) and a joystick frame for the capture, hover. */
void om_key_down(OptionsMenu *m, int scancode);
void om_pad_frame(OptionsMenu *m, const CbPadSample *s);
void om_pointer_move(OptionsMenu *m, float x, float y);
int om_controls_capturing(const OptionsMenu *m);
/* Primary click at centred virtual coordinates (origin at the screen centre, 640x480). */
void om_pointer_click(OptionsMenu *m, float x, float y);
/* Advances the exit animation and the dialog; delivers any completed transition. */
void om_update(OptionsMenu *m, float seconds);
OmEvent om_poll_event(OptionsMenu *m);

/* Hook for the host/renderer (SPEC_GUI 5.4): consume-and-clear. */
int om_take_apply_request(OptionsMenu *m);        /* any graphics value changed */
int om_take_reinit_request(OptionsMenu *m);       /* ... and it needs a display reset */
int om_apply_pending(const OptionsMenu *m);
int om_reinit_pending(const OptionsMenu *m);
/* The host's display reset lowered the resolution (SPEC_GUI 5.5): the frontend Display list is
 * rebuilt so the Resolution row shows the new value; the in-game list is not refreshed. */
void om_notify_resolution_lowered(OptionsMenu *m);

/* Queries for the view and tests. Rows and focus are 0-based here. */
int om_row_count(const OptionsMenu *m);
const OmRow *om_row(const OptionsMenu *m, int i);
int om_focus(const OptionsMenu *m);
/* The value the row's value text shows (task AQ: lets the view tell an authored value from an
 * edited one without the OM_VT_RESOLUTION special case in om_row_value's text path). */
int om_row_display_value(const OptionsMenu *m, int i);
int om_top(const OptionsMenu *m);
int om_row_visible(const OptionsMenu *m, int i);
int om_row_value(const OptionsMenu *m, int i);    /* displayed value (sliders clamped, SPEC_GUI 2.7) */
float om_row_fill(const OptionsMenu *m, int i);
/* Value text: *key = label key when it comes from the language file, else 0 with a literal text.
 * NULL when the row shows none (sliders, buttons, resolution index outside 0..5 before any text). */
const char *om_row_value_text(const OptionsMenu *m, int i, uint32_t *key);
int om_scrollbar_visible(const OptionsMenu *m);   /* more than one row (2.6) */
float om_scroll_tab(const OptionsMenu *m);        /* 0..1 along the track, follows the focused row */
/* Header texts (SPEC_GUI_ART 5): which = 0 Header_Text/Header_text, 1 Header_text2 (in-game Display
 * only). NULL when the screen has none. */
const char *om_header(const OmContext ctx, OmScreen screen, int which, uint32_t *key);

int om_dialog_is_open(const OptionsMenu *m);
int om_dialog_focus(const OptionsMenu *m);        /* 0 = first button (Yes), 1 = second (No) */
uint32_t om_dialog_button_key(int i);

/* Hub (SPEC_GUI_ART 2.5). */
const OmHubEntry *om_hub_entry(int i);            /* the baseline entries 0..3; NULL beyond */
int om_hub_count(const OptionsMenu *m);           /* entries this hub shows (4, or 5 with Controls) */
const OmHubEntry *om_hub_entry_of(const OptionsMenu *m, int i);   /* includes Controls when shown */
int om_hub_focus(const OptionsMenu *m);           /* 0-based focused entry */
int om_hub_remembered(const OptionsMenu *m);      /* 1-based, 0 = none */

/* Grid of a list screen: TITLE_MASTER / DATA_MASTER as top-left + size (SPEC_GUI 2.3 table). The view
 * reads the actual anchors from the package; pointer hit tests use these constants. */
GlAnchors om_anchors(OmContext ctx, OmScreen screen);
/* Centre y of visible line k on that screen (the row spans pitch 36 about it). */
float om_row_center_y(const OptionsMenu *m, int k);

#ifdef __cplusplus
}
#endif
#endif
