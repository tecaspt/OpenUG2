/* fe_menu.h -- host adapter of the retail front end (UI_Main, UI_QRModeSelect, PC_Loading and, from
 * Phase 2, the track / options / car screens). Modelled on pause_menu.c: its own UgAssets (front-end
 * package set), UgGL and canvas; every screen is the authored retail package evaluated through
 * gui_view.h. Fail closed: if a package, art or font cannot be bound the screen is not drawn
 * (fem_visual_available() == 0, fem_status() says why); no native fallback art.
 *
 * Input: keyboard, controller, mouse and SDL's emulated mouse events for touch (iOS taps arrive as
 * mouse events, the same path as pause_menu_event). */
#ifndef OPENUG2_FE_MENU_H
#define OPENUG2_FE_MENU_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GameCatalog;                /* game_catalog.h */

typedef enum { FEM_REQ_NONE = 0, FEM_REQ_QUICK_RACE, FEM_REQ_FREE_RUN, FEM_REQ_QUIT } FemReqKind;
typedef struct FemRequest { FemReqKind kind; int event_id; char car[32], track[16]; int laps, difficulty; } FemRequest;
typedef enum { FEM_SCR_NONE = 0, FEM_SCR_MAIN, FEM_SCR_QR_MODE, FEM_SCR_QR_TRACK, FEM_SCR_QR_OPTIONS, FEM_SCR_QR_CAR, FEM_SCR_LOADING } FemScreen;
typedef struct FeMenu FeMenu;

/* `cat` is borrowed (must outlive the menu); NULL is allowed (race modes then report an error and stay).
 * `allow_quit`: Back on the main menu requests FEM_REQ_QUIT (off on iOS). Needs a current GL context. */
FeMenu *fem_create(const char *dataroot, const struct GameCatalog *cat, int allow_quit);
void fem_destroy(FeMenu *m);
/* Opens (or switches to) a screen; FEM_SCR_NONE closes. */
void fem_open(FeMenu *m, FemScreen screen);
void fem_close(FeMenu *m);
/* Reopens QR_TRACK and exposes the launch failure through fem_status(). */
void fem_show_error(FeMenu *m, const char *message);
int fem_is_open(const FeMenu *m);
FemScreen fem_screen(const FeMenu *m);
/* The car and track the world currently shows (NUL-terminated names; NULL leaves one unchanged). */
void fem_set_current(FeMenu *m, const char *car, const char *track);
/* Opens the loading screen (PC_Loading.fng) for a launch; event_id < 0 for Free Run. */
void fem_set_loading(FeMenu *m, int event_id);
void fem_event(FeMenu *m, const SDL_Event *e, SDL_Window *win);
/* Advances UI time; returns the request produced by a completed choice (kind NONE otherwise). */
FemRequest fem_update(FeMenu *m, float seconds);
/* Clears the main target to black (except in QR_CAR) and draws the screen. Inside a frame, outside a pass. */
void fem_draw(FeMenu *m, int w, int h);
/* Car shown in the 3D preview behind the screen ("" when the screen has none). */
const char *fem_preview_car(const FeMenu *m);
int fem_visual_available(const FeMenu *m);
const char *fem_status(const FeMenu *m);

/* Shared Phase-2 model and renderer adapters (no host/world ownership). */
#include "game_catalog.h"
#include "gui_view.h"
typedef struct FeChoice {
    const GameCatalog *cat;
    int kind, event_index, car_index, laps, difficulty, option, options_page;
    int event_indices[GC_MAX_EVENTS], nevents;
    int cars[64], ncars, selected;
    float preview_wait;
    char preview[32], label[96], laps_text[16], distance[32];
    GvList list;
} FeChoice;
int fet_open(FeChoice *, GvScreen *, const UgAssets *, int options);
void fet_sync(FeChoice *, GvScreen *, float ms, int options);
void fet_move(FeChoice *, int direction, int vertical, int options);
int fec_open(FeChoice *, GvScreen *, const UgAssets *);
void fec_sync(FeChoice *, GvScreen *, float seconds);
void fec_move(FeChoice *, int direction);
#ifdef __cplusplus
}
#endif
#endif
