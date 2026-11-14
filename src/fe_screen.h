/* fe_screen.h -- state machine of a front-end icon-carousel screen (UI_Main.fng, UI_QRModeSelect.fng).
 *
 * GL-free and SDL-free; draws nothing. Mirrors pause_screen.c: Accept or Back starts the exit, the
 * result (CHOSEN / BACK) is delivered only after the host reports fes_exit_complete (the authored exit
 * script has finished). Navigation, disabled entries and the pointer rules are those of gui_carousel.h
 * (no wrap, disabled entries are skipped and cannot be chosen; hit tests: gc_slot_hit and the arrow boxes). Pointer coordinates are centred 640x480. */
#ifndef OPENUG2_FE_SCREEN_H
#define OPENUG2_FE_SCREEN_H

#include "fe_entries.h"
#include "gui_carousel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FES_IN_LEFT = 1, FES_IN_RIGHT, FES_IN_ACCEPT, FES_IN_BACK } FesInput;
typedef enum { FES_EV_NONE = 0, FES_EV_CHOSEN, FES_EV_BACK } FesEvent;

typedef struct FeCarouselScreen {
    GuiCarousel car;
    FeEntry e[GC_MAX];
    int n, exiting, reason, chosen;   /* reason: FES_IN_ACCEPT or FES_IN_BACK while exiting; chosen: entry id */
    FesEvent pending;
    /* appended to the frozen layout */
    GcPointer ptr;                    /* pointer press armed on the selection slot */
    float arrow_x[2], arrow_y;        /* centres of ScrollBar_Arrow_1 / _2 (default: the pause-menu geometry) */
} FeCarouselScreen;

/* (Re)builds the screen over `n` entries: focus on `focus_id` when it is an enabled entry, else on the
 * first enabled one; scroll snapped. `rail_w` is the width of the package's ScrollBar_Backing. */
void fes_open(FeCarouselScreen *s, const FeEntry *entries, int n, int focus_id, float rail_w);
/* Centres of the package's navigation arrows (centred 640x480 units); the 26x26 boxes differ per package. */
void fes_set_arrows(FeCarouselScreen *s, float left_x, float right_x, float y);
void fes_input(FeCarouselScreen *s, FesInput in);
void fes_pointer_press(FeCarouselScreen *s, float x, float y);
void fes_pointer_release(FeCarouselScreen *s, float x, float y);
/* The authored exit finished: the pending Accept / Back becomes the event. No-op when not exiting. */
void fes_exit_complete(FeCarouselScreen *s);
void fes_update(FeCarouselScreen *s, float seconds);
/* Exactly-once result. On CHOSEN `*entry_id` (when non-NULL) receives the entry id. */
FesEvent fes_poll(FeCarouselScreen *s, int *entry_id);
/* Label of the focused entry ("" when none). */
const char *fes_caption(const FeCarouselScreen *s);

#ifdef __cplusplus
}
#endif
#endif
