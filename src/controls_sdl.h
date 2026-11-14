/* controls_sdl.h -- the SDL side of the Controls feature: the joysticks the game has opened with their
 * default or persisted bindings, platform key/axis names for the screen, per-frame joystick samples for the
 * capture, persistence, and the binding lookups the driving code uses instead of fixed keys
 * (CONTROLS_SPEC 8 "Game input", decisions D2-D6). The pure model is controls_bind.h. */
#ifndef OPENUG2_CONTROLS_SDL_H
#define OPENUG2_CONTROLS_SDL_H

#include <SDL.h>

#include "controls_bind.h"
#include "controls_menu.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CxInput {
    CbBindings b;                      /* what the game queries and the Controls screen edits */
    SDL_Joystick *joy[CB_MAX_PADS];
    SDL_GameController *gc[CB_MAX_PADS];   /* only to ask SDL for axis names */
    StProfile *profile;                /* carries the joystick device records (heap) */
    char profile_root[512];
    int have_root;
    int profile_writable;              /* the profile file is absent (to be created) or was loaded: only then may it be written */
    Uint8 hat_prev[CB_MAX_PADS][CB_BIND_POVS];   /* last hat state per pad, for press edges */
    Uint8 vbtn_prev[CB_MAX_PADS][CB_VBTN_COUNT]; /* last trigger-as-button state per pad, for press edges */
    int ready;
    uint32_t virt_held;                /* on-screen touch buttons held: bit `a` = action a (touch_controls.h) */
} CxInput;

/* Opens the joysticks, applies the defaults, then the persisted keyboard table and device records. The SDL
 * joystick subsystem must be initialised. Never fails: without a store the defaults stay. */
void cx_init(CxInput *c);
void cx_shutdown(CxInput *c);
/* Writes the keyboard table (INI store) and the device records (profile OPENUG2). 0 on success. */
int cx_save(CxInput *c);
/* Platform names for the screen (decision D2). */
void cx_platform(CxInput *c, CmPlatform *out);
/* One frame of pad `i` for the capture. */
void cx_sample(const CxInput *c, int i, CbPadSample *out);

/* ---- driving queries ---- */
/* The action is held on the keyboard (`keys` = SDL_GetKeyboardState) or any pad (a bound button or POV). */
int cx_action_down(const CxInput *c, const Uint8 *keys, int action);
/* A key-down event's scancode belongs to the action. */
int cx_key_is(const CxInput *c, int scancode, int action);
/* The key is bound to one of the actions of the Controls screen; developer hot keys stand back for it. */
int cx_key_bound(const CxInput *c, int scancode);
/* A joystick button-down event belongs to the action. */
int cx_joy_button_is(const CxInput *c, SDL_JoystickID instance, int button, int action);
/* Bit `a` (actions are < 32) is set when this joystick event newly pressed a control bound to action `a`: a
 * button-down, a hat direction that was not held before, or an analog trigger (virtual button 14/15) that crossed its
 * press threshold. Other events, releases and a held hat or trigger give 0. Use it
 * for actions the host performs once per press; call it once per event. */
uint32_t cx_joy_edges(CxInput *c, const SDL_Event *e);
/* Throttle +1 .. brake -1, steering +1 left .. -1 right; the digital actions win, the pad axes fill in. */
float cx_throttle(const CxInput *c, const Uint8 *keys);
float cx_steer(const CxInput *c, const Uint8 *keys);

#ifdef __cplusplus
}
#endif
#endif
