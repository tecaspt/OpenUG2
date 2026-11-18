/* fe_perf_test -- synthetic checks of the Performance Shop screens (fe_perf.c) and their FeMenu wiring (fe_menu.c):
 * category / package / single-part navigation, locked level, owned package and part, funds caption, the two-step
 * purchase, refresh after a purchase, roam request. No retail data: the menu runs logic-only (no package). */
#include "fe_perf.h"
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
static void level(FePerfShop *s, int cat, int lvl, int open, int n, int32_t part_price, int first_comp) {
    FePerfLevel *L = &s->cat[cat].lvl[lvl - 1];
    L->n = (uint8_t)n; L->open = (uint8_t)open; L->full = 0;
    for (int i = 0; i < n; i++) {
        L->part[i].comp = (int16_t)(first_comp + i); L->part[i].price = part_price; L->part[i].name_key = 0x1000u + (uint32_t)i;
        L->full += part_price;
    }
    L->price = L->full * 9 / 10;
}

int main(void) {
    static FePerfShop shop;
    memset(&shop, 0, sizeof shop);
    shop.bank = 1000;
    level(&shop, 1, 1, 1, 2, 300, 0);          /* transmission: Street open */
    level(&shop, 3, 1, 1, 3, 200, 10);         /* engine: Street open ($540), Pro locked */
    level(&shop, 3, 2, 0, 2, 900, 13);
    level(&shop, 8, 1, 1, 1, 5000, 20);        /* tires: Street open, too expensive */

    /* ---- state machine ---- */
    static FePerf p;
    fpr_open(&p, &shop);
    CK(p.page == FEP_PAGE_CAT && p.ncats == 3 && fpr_cat(&p) == 1, "categories with packages only, first focused");
    CK(fpr_input(&p, FES_IN_LEFT) == FEP_EV_NONE && fpr_cat(&p) == 1, "left at the first category does not wrap");
    CK(fpr_input(&p, 6) == FEP_EV_NONE && fpr_cat(&p) == 1, "down ignored on a carousel");
    fpr_input(&p, FES_IN_RIGHT);
    CK(fpr_cat(&p) == 3, "right browses to engine");
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_PAGE && p.page == FEP_PAGE_PKG, "Accept opens the packages");
    CK(p.npkgs == 3 && fpr_level(&p) == 1, "Street, Pro, Choose Individual Parts; Street focused");
    CK(!strcmp(fpr_package(&p), "UI_ChoosePerformancePackage.fng"), "package page package");
    fpr_input(&p, FES_IN_RIGHT);
    CK(fpr_level(&p) == 2 && fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_LOCKED && !p.confirm, "locked level: LOCKED");
    fpr_input(&p, FES_IN_LEFT);
    CK(p.msg == FEP_MSG_NONE, "browsing clears the caption");
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.confirm && p.msg == FEP_MSG_CONFIRM, "first Accept asks");
    CK(fpr_input(&p, FES_IN_BACK) == FEP_EV_NONE && !p.confirm && p.page == FEP_PAGE_PKG, "Back cancels the question");
    fpr_input(&p, FES_IN_ACCEPT);
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_BUY && p.buy_cat == 3 && p.buy_lvl == 1 && p.buy_comp == -1, "second Accept buys the package");

    /* refresh after the purchase: package owned, bank debited, position kept */
    shop.bank -= shop.cat[3].lvl[0].price;
    for (int i = 0; i < 3; i++) shop.cat[3].lvl[0].part[i].owned = 1;
    shop.cat[3].lvl[0].owned_all = 1;
    fpr_refresh(&p, &shop, FEP_MSG_NONE);
    CK(p.page == FEP_PAGE_PKG && fpr_cat(&p) == 3 && fpr_level(&p) == 1 && p.shop.bank == 460, "refresh keeps the page and selection");
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_OWNED && !p.confirm, "owned package: OWNED");
    fpr_refresh(&p, &shop, FEP_MSG_NO_FUNDS);
    CK(p.msg == FEP_MSG_NO_FUNDS, "host refusal caption");

    /* single parts */
    fpr_input(&p, FES_IN_RIGHT); fpr_input(&p, FES_IN_RIGHT);
    CK(fpr_level(&p) == FEP_SPECIFIC, "Choose Individual Parts focused");
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_PAGE && p.page == FEP_PAGE_PARTS && p.nrows == 5, "parts page lists every level's parts");
    CK(!strcmp(fpr_package(&p), "UI_BuyPerformanceParts.fng"), "parts page package");
    CK(p.row_sel == 0, "every open part owned: first row focused");
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_OWNED, "owned part: OWNED");
    fpr_input(&p, 6); fpr_input(&p, 6); fpr_input(&p, 6);
    CK(p.row_sel == 3 && fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_LOCKED, "part of a locked level: LOCKED");
    fpr_input(&p, 6); fpr_input(&p, 6);
    CK(p.row_sel == 4, "down stops at the last row");
    CK(fpr_input(&p, FES_IN_BACK) == FEP_EV_PAGE && p.page == FEP_PAGE_PKG, "Back: parts -> packages");
    CK(fpr_input(&p, FES_IN_BACK) == FEP_EV_PAGE && p.page == FEP_PAGE_CAT && fpr_cat(&p) == 3, "Back: packages -> categories");
    fpr_input(&p, FES_IN_RIGHT);
    fpr_input(&p, FES_IN_ACCEPT);
    CK(fpr_cat(&p) == 8 && fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_NO_FUNDS && !p.confirm, "price above bank: NO_FUNDS");
    fpr_input(&p, FES_IN_RIGHT); fpr_input(&p, FES_IN_ACCEPT);
    CK(p.page == FEP_PAGE_PARTS && fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && p.msg == FEP_MSG_NO_FUNDS, "single part above bank: NO_FUNDS");
    shop.bank = 6000; fpr_refresh(&p, &shop, FEP_MSG_NONE);
    fpr_input(&p, FES_IN_ACCEPT);
    CK(fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_BUY && p.buy_cat == 8 && p.buy_lvl == 1 && p.buy_comp == 20, "second Accept buys the part");
    fpr_input(&p, FES_IN_BACK); fpr_input(&p, FES_IN_BACK);
    CK(fpr_input(&p, FES_IN_BACK) == FEP_EV_LEAVE, "Back from the categories leaves");
    fpr_open(&p, NULL);
    CK(p.ncats == 0 && fpr_input(&p, FES_IN_ACCEPT) == FEP_EV_NONE && fpr_input(&p, FES_IN_BACK) == FEP_EV_LEAVE, "empty shop");

    /* ---- FeMenu wiring (logic only: no data root) ---- */
    shop.bank = 1000;
    FeMenu *m = fem_create("/nonexistent-openug2-data", NULL, 0);
    CK(m != NULL, "menu");
    if (!m) { printf("fe_perf_test: FAIL\n"); return 1; }
    fem_open_perf(m, &shop);
    CK(fem_is_open(m) && fem_screen(m) == FEM_SCR_CAREER_PERF && !fem_visual_available(m), "perf shop opens (logic only)");
    key(m, SDLK_RETURN); step(m, 2);
    key(m, SDLK_RETURN); FemRequest r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE, "first Accept on Street only asks");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_PERF_BUY && r.perf_cat == 1 && r.perf_lvl == 1 && r.perf_comp == -1, "second Accept requests the package");
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE && fem_screen(m) == FEM_SCR_CAREER_PERF, "locked after the request");
    fem_perf_refresh(m, &shop, FEP_MSG_NO_FUNDS);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_CAREER_ROAM, "refresh unlocks; leaving the shop requests roam");
    fem_close(m);
    fem_destroy(m);

    printf("fe_perf_test: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
