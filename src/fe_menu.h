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
#include "race_rules.h"
#include "fe_lot.h"
#include "fe_perf.h"
#include "fe_worldmap.h"

#ifdef __cplusplus
extern "C" {
#endif

struct GameCatalog;                /* game_catalog.h */

typedef enum { FEM_REQ_NONE = 0, FEM_REQ_QUICK_RACE, FEM_REQ_FREE_RUN, FEM_REQ_QUIT, FEM_REQ_CAREER,
               FEM_REQ_LOT_BUY,       /* M6: buy the lot car named in `car` (second Accept on the Car Lot) */
               FEM_REQ_CAREER_ROAM,   /* M6: leave a career sub-screen back to free roam */
               FEM_REQ_SET_DESTINATION, /* Task S: destination carries world XY and explicit identity */
               FEM_REQ_PERF_BUY       /* performance shop: buy perf_cat / perf_lvl / perf_comp (-1 = the package) */
} FemReqKind;
typedef struct FemRequest { FemReqKind kind; int event_id; char car[32], track[16]; int laps, difficulty; RrOptions rules; FeMapDestination destination;
                            int perf_cat, perf_lvl, perf_comp; } FemRequest;
typedef enum { FEM_SCR_NONE = 0, FEM_SCR_MAIN, FEM_SCR_QR_MODE, FEM_SCR_QR_TRACK, FEM_SCR_QR_OPTIONS, FEM_SCR_QR_CAR, FEM_SCR_LOADING, FEM_SCR_OPTIONS, FEM_SCR_PROFILE, FEM_SCR_CAREER, FEM_SCR_GARAGE, FEM_SCR_CAREER_LOT, FEM_SCR_WORLD_MAP, FEM_SCR_CAREER_PERF } FemScreen;
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
#include "fe_profile.h"
#include "garage.h"
#include "gui_view.h"
enum { FEM_APPLY_GRAPHICS=1, FEM_APPLY_REINIT=2, FEM_APPLY_PROFILE=4 };
/* Optional storage/caps overrides before opening Options/Profile. No writes on creation. */
int fem_set_storage(FeMenu *,const char *settings_path,const char *profile_root);
void fem_set_caps(FeMenu *,const StGfxCaps *);
void fem_set_graphics(FeMenu *,const StGraphics *);
unsigned fem_take_apply(FeMenu *);
const StGraphics *fem_graphics(const FeMenu *);
const StProfile *fem_current_profile(const FeMenu *);
int fem_options_page(const FeMenu *);
/* M6: enables Main > Career (career data loaded) and says whether the current profile has a save.
 * Choosing Career returns FEM_REQ_CAREER with event_id 1 = new career, 0 = continue, car = current car. */
void fem_set_career(FeMenu *, int available, int has_save);
/* M6 Car Lot (UI_CareerCarLot.fng): left/right browse `cars` (previewed through fem_preview_car), Accept asks
 * CAR_SELECT_CONFIRM_CHOOSE_CAR and a second Accept returns FEM_REQ_LOT_BUY (car = name). Back returns
 * FEM_REQ_CAREER_ROAM unless `must_buy` (then the CAR_LOT_MUST_BUY caption). An owned car shows
 * CAR_SELECT_SAME_CAR. `bank` fills PLAYER_CASH. The menu is locked after a request until the host reopens. */
void fem_open_lot(FeMenu *, const FeLotCar *cars, int n, int must_buy, long long bank);
/* Career Performance Shop (fe_perf.h): category -> package -> optional single part, two-step purchase. Accept on a
 * purchase question returns FEM_REQ_PERF_BUY; Back from the categories returns FEM_REQ_CAREER_ROAM. The menu is locked
 * after a request until the host calls fem_perf_refresh (new data, kept position, `msg` caption, FepMsg). */
void fem_open_perf(FeMenu *, const FePerfShop *shop);
void fem_perf_refresh(FeMenu *, const FePerfShop *shop, int msg);
/* M6: the Garage hub opened from career free roam; leaving it returns FEM_REQ_CAREER_ROAM, not Main. */
void fem_open_career_garage(FeMenu *);
void fem_open_career_shop(FeMenu *,int type,uint32_t slots,
                         int (*save)(void *,const char *,const GarageConfig *),void *user);
/* Task S: copied bounded snapshot of available career races/open shops. World XY;
 * heading radians. Accept requests a destination; Back requests CAREER_ROAM. */
void fem_open_world_map(FeMenu *, const FeMapIcon *icons, int n,
                        float player_x, float player_y, float heading);
/* Read-only current model, for inspection; NULL outside World Map. */
const FeWorldMap *fem_world_map_state(const FeMenu *);
/* Directory holding the profile folders (career saves live inside them); NULL before storage opens. */
const char *fem_profile_root(const FeMenu *);
/* Writes the current profile if it only exists in memory (a career save needs its folder). ST_OK or ST_E_*. */
int fem_persist_profile(FeMenu *);
/* Current host config is copied outside Garage; preview requests require a host acknowledgement. */
/* ST_E_NOTFOUND: use stock for this profile/car; other errors refuse launch. */
int fem_garage_saved(const FeMenu *,const char *car,GarageConfig *out);
int fem_garage_current(FeMenu *,const char *car,const GarageConfig *);
int fem_garage_take(FeMenu *,GarageRequest *);
void fem_garage_ack(FeMenu *,uint32_t serial,int success,const char *error);
int fem_garage_page(const FeMenu *);


typedef struct FeChoice {
    const GameCatalog *cat;
    int kind, event_index, car_index, laps, difficulty, option, options_page;
    RrOptions rules;
    int event_indices[GC_MAX_EVENTS], nevents;
    int cars[64], ncars, selected;
    float preview_wait;
    char preview[32], label[96], laps_text[16], distance[32];
    GvList list;
    uint32_t track_name_keys[GC_MAX_EVENTS];
    int track_map_ids[GC_MAX_EVENTS], map_textures[GC_MAX_EVENTS];
    UgBuf map_buffers[GC_MAX_EVENTS]; int nmap_buffers;
    int resolved_kind;
    char track_status[160],car_status[160];

} FeChoice;
int fet_open(FeChoice *, GvScreen *, UgAssets *, int options);
void fet_free(FeChoice *);
int fet_name_is_real(const char *);
int fet_map_opaque(uint8_t *,size_t pixels);
int fet_same_layout(const void *,size_t,const void *,size_t);
void fet_sync(FeChoice *, GvScreen *, float ms, int options);
void fet_move(FeChoice *, int direction, int vertical, int options);
int fet_row_count(const FeChoice *);
int fet_row_enabled(const FeChoice *, int row);
int fec_open(FeChoice *, GvScreen *, const UgAssets *);
void fec_sync(FeChoice *, GvScreen *, float seconds);
void fec_move(FeChoice *, int direction);
#ifdef __cplusplus
}
#endif
#endif
