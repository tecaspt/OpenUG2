/* touch_controls.h -- the on-screen touch overlay (a platform adaptation, not a retail feature).
 * Pure logic over SDL events plus a draw through ui_flat. Held and edge masks are CbAction bit sets
 * (bit `a` = action a), the form CxInput.virt_held and the host's pad-edge mask use.
 *
 * Geometry is in "height units": y in 0..1 down the frame, x in 0..aspect, so a unit is the frame height in
 * pixels. Finger events (normalised 0..1) are scaled by the aspect; mouse events (pixels, accepted only with
 * accept_mouse_touch) are normalised by the window size (looked up from the event's window, then the mouse
 * focus, then tc_set_window_size). */
#ifndef OPENUG2_TOUCH_CONTROLS_H
#define OPENUG2_TOUCH_CONTROLS_H

#include <SDL.h>
#include <stdint.h>

#include "controls_bind.h"
#include "ui_flat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TC_STEER_L, TC_STEER_R, TC_GAS, TC_BRAKE, TC_NOS, TC_HANDBRAKE, TC_CAMERA, TC_PAUSE,
               TC_SHIFT_UP, TC_SHIFT_DOWN, TC_ENTER_RACE, TC_COUNT } TcButton;
enum { TC_CTX_DRAG = 1, TC_CTX_OUTRUN = 2 };

#define TC_MAX_TOUCHES 10

typedef struct TcTouch {
    int used;
    int64_t id;      /* SDL finger id, or -1 for the mouse */
    int btn;         /* the button the touch is over now, -1 for none */
    float sx, sy;    /* where it landed (height units), for the two-finger tap */
} TcTouch;

/* Two-finger tap = pause: exactly two fingers, both landing off the visible buttons, none moving more than
 * TC_TAP_SLOP, all lifted within TC_TAP_MS of the first landing. The fingers need not land in the same frame. */
#define TC_TAP_MS   350u
#define TC_TAP_SLOP 0.04f

typedef struct TouchControls {
    int enabled, active, accept_mouse;
    int suppressed;
    unsigned ctx;
    float aspect;
    int win_w, win_h;                /* fallback pixel size for mouse events */
    TcTouch touch[TC_MAX_TOUCHES];
    uint32_t tap_t0;                 /* two-finger tap gesture in progress (see TC_TAP_MS) */
    int tap_max, tap_spoiled;
} TouchControls;

/* enabled 0 = never shows, never reports. start_active: shown before the first touch. */
void tc_init(TouchControls *t, int enabled, int start_active, int accept_mouse_touch);
/* Per frame. suppressed (a menu is open): the overlay is hidden and drops its touches. ctx: TC_CTX_*. */
void tc_set_frame(TouchControls *t, int suppressed, unsigned ctx, float aspect);
/* Pixel size used to normalise mouse events that carry no usable window. */
void tc_set_window_size(TouchControls *t, int w, int h);
/* Feeds one event; returns the press-edge CbAction bits (CAMERA, STEER_L/R, SHIFT_UP/DOWN in the drag context,
 * ENTER_RACE in the outrun context; PAUSE on a finger lifting inside the button, or at the end of a two-finger tap). */
uint32_t tc_event(TouchControls *t, const SDL_Event *e);
/* CbAction bits of the buttons held now (PAUSE is never held). */
uint32_t tc_held(const TouchControls *t);
int tc_visible(const TouchControls *t);
/* Circle of a button in height units (whatever the context). */
void tc_layout(const TouchControls *t, int btn, float *cx, float *cy, float *r);
/* Draws the overlay in a w x h pixel target (nothing when not visible). */
void tc_draw(const TouchControls *t, UiFlat *uf, int w, int h);

#ifdef __cplusplus
}
#endif
#endif
