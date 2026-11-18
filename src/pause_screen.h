/* pause_screen.h -- state machine of the pause screen (UI_pause.fng): entry list, icon carousel,
 * activation timing and confirmation dialogs (SPEC_GUI_ART 2, tvg07/tvg08/tvg10/tvg11).
 *
 * GL-free and SDL-free; draws nothing. The host adapter (pause_menu.c) maps devices to PsInput,
 * advances time, polls PsEvent and presents the state through the authored package (gui_view.h).
 * "After exit" entries run the screen's 320 ms exit script first and act at exit-complete;
 * "immediate" entries act at once or open a confirmation dialog. */
#ifndef OPENUG2_PAUSE_SCREEN_H
#define OPENUG2_PAUSE_SCREEN_H

#include "gui_carousel.h"
#include "gui_dialog.h"
#include "pause_entries.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Up/Down step the rows of a three-button dialog; the pause carousel itself ignores them (the
 * retail carousel is horizontal). */
typedef enum { PS_IN_NONE = 0, PS_IN_LEFT, PS_IN_RIGHT, PS_IN_UP, PS_IN_DOWN, PS_IN_ACCEPT, PS_IN_BACK, PS_IN_START } PsInput;

/* Exactly-once results for the host / adapter. */
typedef enum {
    PS_EV_NONE = 0,
    PS_EV_RESUME,        /* resume gameplay (Return to Explore Mode / Resume Race / Back / Start) */
    PS_EV_OPTIONS,       /* open UI_PauseOptionsMain.fng */
    PS_EV_REPLAY,        /* open ui_ReplayControl.fng (only reachable with replay data) */
    PS_EV_QUIT,          /* quit to the front end (confirmed) */
    PS_EV_RESTART,       /* restart the race (confirmed) */
    PS_EV_EXPLORE,       /* leave the race to explore mode (confirmed) */
    PS_EV_WORLD_MAP,     /* leave the race to the career world map (confirmed) */
    PS_EV_GARAGE         /* jump to the garage (confirmed; never offered by the retail PC game) */
} PsEvent;

typedef struct PauseScreen {
    PauseContext ctx;
    PauseEntry entries[PE_MAX];
    int n;
    GuiCarousel car;
    GcPointer ptr;
    GuiDialog dialog;
    int dialog_entry;            /* entry the open dialog belongs to */
    int exiting;
    PsEvent exit_event;
    float exit_left_ms;
    int fresh;                   /* the screen was created in this frame: activation/Back/Start ignored (2.4) */
    PsEvent events[4];
    int nevents;
    /* presentation state polled by the view */
    unsigned build_serial;       /* bumped by ps_open */
    unsigned move_serial;        /* bumped by every focus move */
    int move_dir;
    unsigned exit_serial;        /* bumped when the exit script starts */
    unsigned dialog_serial;      /* bumped when a dialog opens or closes (DIM / UNDIM) */
} PauseScreen;

/* (Re)builds the screen: entries from the context, focus on entry 1, scroll snapped, fade off. */
void ps_open(PauseScreen *p, const PauseContext *ctx);
/* Test seam: builds the screen over an explicit entry list instead of the catalogue of the context. */
void ps_open_entries(PauseScreen *p, const PauseContext *ctx, const PauseEntry *entries, int n);
void ps_input(PauseScreen *p, PsInput in);
/* Pointer in centred virtual coordinates. A press on a visible arrow steps; the selection slot
 * activates on release when the press began on it. A dialog takes the click for its buttons. */
void ps_pointer_press(PauseScreen *p, float x, float y);
void ps_pointer_release(PauseScreen *p, float x, float y);
void ps_pointer_click(PauseScreen *p, float x, float y);
void ps_update(PauseScreen *p, float seconds);
PsEvent ps_poll(PauseScreen *p);

int ps_count(const PauseScreen *p);
int ps_selected(const PauseScreen *p);                   /* 0-based */
const PauseEntry *ps_entry(const PauseScreen *p, int i);
const char *ps_caption(const PauseScreen *p);            /* label of the focused entry ("" when none) */
int ps_dialog_open(const PauseScreen *p);
int ps_busy(const PauseScreen *p);                       /* exit running: input is ignored */

#ifdef __cplusplus
}
#endif
#endif
