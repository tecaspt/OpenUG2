/* fe_lot_test -- synthetic checks of the M6 Car Lot (fe_lot.c) and its FeMenu wiring (fe_menu.c):
 * browsing + preview debounce, must_buy Back block, the two-step buy, owned car, roam requests from
 * the lot and from the career Garage. No retail data: the menu runs logic-only (no package). */
#include "fe_lot.h"
#include "fe_menu.h"
#include "fe_screen.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static FemRequest step(FeMenu *m, int frames) {
    FemRequest r; memset(&r, 0, sizeof r);
    for (int k = 0; k < frames; k++) { FemRequest q = fem_update(m, 1.0f / 30); if (q.kind) r = q; }
    return r;
}
static void key(FeMenu *m, SDL_Keycode k) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    fem_event(m, &e, NULL);
}

int main(void) {
    FeLotCar cars[4]; memset(cars, 0, sizeof cars);
    const char *names[4] = { "PEUGOT", "FOCUS", "COROLLA", "240SX" };
    for (int i = 0; i < 4; i++) { snprintf(cars[i].name, sizeof cars[i].name, "%s", names[i]); cars[i].starter = 1; }

    /* ---- state machine ---- */
    FeLot l;
    fel_open(&l, cars, 4, 1, 0);
    CK(l.n == 4 && l.sel == 0 && !strcmp(fel_selected(&l), "PEUGOT"), "opens on the first car");
    CK(fel_input(&l, FES_IN_LEFT) == FEL_EV_NONE && l.sel == 0, "left at the first car does not wrap");
    fel_input(&l, FES_IN_RIGHT); fel_input(&l, FES_IN_RIGHT);
    CK(!strcmp(fel_selected(&l), "COROLLA"), "right browses");
    fel_advance(&l, 0.2f); CK(!l.preview[0], "preview debounced");
    fel_advance(&l, 0.2f); CK(!strcmp(l.preview, "COROLLA"), "preview after 0.3 s");
    fel_input(&l, FES_IN_RIGHT); fel_input(&l, FES_IN_RIGHT);
    CK(l.sel == 3 && !l.preview[0], "right stops at the last car, preview reset");
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_NONE && l.msg == FEL_MSG_MUST_BUY, "must_buy blocks Back");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && l.confirm && l.msg == FEL_MSG_CONFIRM, "first Accept asks");
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_NONE && !l.confirm && l.msg == FEL_MSG_NONE, "Back cancels the question");
    fel_input(&l, FES_IN_ACCEPT);
    CK(fel_input(&l, FES_IN_LEFT) == FEL_EV_NONE && !l.confirm && l.sel == 2, "browsing cancels the question");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE, "ask again");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_BUY && !strcmp(fel_selected(&l), "COROLLA"), "second Accept buys");
    fel_open(&l, cars, 4, 0, 1234);
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_LEAVE && l.bank == 1234, "Back leaves when not must_buy");
    cars[0].owned = 1;
    fel_open(&l, cars, 4, 0, 0);
    CK(l.sel == 1, "opens on the first car not owned");
    fel_input(&l, FES_IN_LEFT);
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && l.msg == FEL_MSG_SAME_CAR && !l.confirm, "owned car: SAME_CAR");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE, "owned car never buys");
    cars[0].owned = 0;
    fel_open(&l, NULL, 0, 1, 0);
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && !fel_selected(&l)[0], "empty lot");

    /* ---- FeMenu wiring (logic only: no data root) ---- */
    FeMenu *m = fem_create("/nonexistent-openug2-data", NULL, 0);
    CK(m != NULL, "menu");
    if (!m) { printf("fe_lot_test: FAIL\n"); return 1; }
    fem_open_lot(m, cars, 4, 1, 0);
    CK(fem_is_open(m) && fem_screen(m) == FEM_SCR_CAREER_LOT && !fem_visual_available(m), "lot opens (logic only)");
    key(m, SDLK_RIGHT); step(m, 15);
    CK(!strcmp(fem_preview_car(m), "FOCUS"), "lot preview follows the browsed car");
    key(m, SDLK_ESCAPE); FemRequest r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE && fem_screen(m) == FEM_SCR_CAREER_LOT, "must_buy: Back stays on the lot");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE, "first Accept only asks");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_LOT_BUY && !strcmp(r.car, "FOCUS"), "second Accept requests the buy");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE, "locked after the request");
    fem_open_lot(m, cars, 4, 0, 500);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_CAREER_ROAM, "leaving the lot requests roam");
    fem_close(m);
    CK(!fem_preview_car(m)[0], "no preview once closed");
    fem_open_career_garage(m);
    CK(fem_screen(m) == FEM_SCR_GARAGE, "career garage opens");
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_CAREER_ROAM, "leaving the career garage requests roam");
    fem_close(m);
    fem_open(m, FEM_SCR_GARAGE);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE && fem_screen(m) == FEM_SCR_MAIN, "Customize garage still returns to Main");
    fem_destroy(m);

    printf("fe_lot_test: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
