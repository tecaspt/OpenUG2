/* fe_perf.h -- career Performance Shop screens (FRONTEND/FrontB.lzc): UI_ChoosePerformanceCategory.fng ->
 * UI_ChoosePerformancePackage.fng (Street / Pro / Extreme Package = levels 1..3, plus "Choose Individual Parts")
 * -> UI_BuyPerformanceParts.fng (single parts). The brand select (UI_PerformanceBrandSelect.fng) is cosmetic in
 * retail and is not shown.
 *
 * Two layers, like fe_lot.c: a pure state machine (fpr_open / fpr_input / fpr_refresh: pages, the two-step
 * purchase confirmation, lock / owned / funds captions) and the binding of that state into the authored
 * packages (fpr_bind / fpr_sync). Decoupled from career.h: the host copies a FePerfShop snapshot of the career
 * (career_data perf_pkg / perf_comp, cr_perf_level_open / cr_perf_owned / cr_perf_price) into the screen. */
#ifndef OPENUG2_FE_PERF_H
#define OPENUG2_FE_PERF_H

#include <stdint.h>
#include "gui_carousel.h"
#include "gui_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FEP_CATS   9      /* 0 weight 1 transmission 2 nitrous 3 engine 4 turbo 5 suspension 6 brakes 7 ECU 8 tires (9 aero: never shown) */
#define FEP_LEVELS 3
#define FEP_PARTS  4      /* components per package */

typedef struct FePerfPart { uint32_t name_key; int32_t price; int16_t comp; uint8_t owned; } FePerfPart;
typedef struct FePerfLevel {
    uint8_t n;            /* components; 0 = no such package in the data */
    uint8_t open;         /* cr_perf_level_open */
    uint8_t owned_all;    /* every component installed on the active car */
    int32_t price;        /* package price (cr_perf_price comp -1) */
    int32_t full;         /* sum of the component prices not owned yet (the discount is full - price) */
    FePerfPart part[FEP_PARTS];
} FePerfLevel;
typedef struct FePerfCat { FePerfLevel lvl[FEP_LEVELS]; } FePerfCat;
typedef struct FePerfShop { FePerfCat cat[FEP_CATS]; long long bank; } FePerfShop;

typedef enum { FEP_PAGE_CAT = 0, FEP_PAGE_PKG, FEP_PAGE_PARTS } FepPage;
/* Caption in the screen header; every one is a retail language key (fe_perf.c). */
typedef enum { FEP_MSG_NONE = 0, FEP_MSG_CONFIRM, FEP_MSG_NO_FUNDS, FEP_MSG_LOCKED, FEP_MSG_OWNED } FepMsg;
typedef enum { FEP_EV_NONE = 0, FEP_EV_BUY, FEP_EV_LEAVE, FEP_EV_PAGE /* the page changed: rebind the package */ } FepEvent;

#define FEP_SPECIFIC 0    /* package entry level 0 = "Choose Individual Parts" */
#define FEP_MAX_ROWS (FEP_LEVELS * FEP_PARTS)

typedef struct FePerf {
    FePerfShop shop;
    FepPage page;
    int cats[FEP_CATS], ncats, cat_sel;           /* categories with at least one package */
    int pkgs[FEP_LEVELS + 1], npkgs, pkg_sel;     /* levels 1..3 present, then FEP_SPECIFIC */
    int row_lvl[FEP_MAX_ROWS], row_part[FEP_MAX_ROWS], nrows, row_sel;   /* parts page */
    int confirm;                                  /* the first Accept asked; the next Accept buys */
    FepMsg msg;
    int buy_cat, buy_lvl, buy_comp;               /* FEP_EV_BUY: comp -1 = the whole package */
    /* view */
    GuiCarousel carousel; GvCarousel view; GvList list; GvEntry entry[GC_MAX];
    float header_scale;
    char caption[192], cash[48], total[24], discount[24], prices[FEP_PARTS][24], data[FEP_MAX_ROWS][24];
    char status[160];
} FePerf;

/* Fresh shop: category page, first category. */
void fpr_open(FePerf *p, const FePerfShop *shop);
/* New data after a purchase attempt: keeps page and selections (clamped), shows `msg`. */
void fpr_refresh(FePerf *p, const FePerfShop *shop, FepMsg msg);
/* `in` is FES_IN_LEFT/RIGHT/ACCEPT/BACK or 5/6 (up/down: the parts list). Left/Right/Up/Down browse (no wrap)
 * and cancel a pending question. Accept: category -> its packages; "Choose Individual Parts" -> the parts page;
 * a package or part: locked -> LOCKED, owned -> OWNED, price > bank -> NO_FUNDS, else the first Accept asks
 * CONFIRM and the second returns FEP_EV_BUY (buy_cat/buy_lvl/buy_comp). Back: cancels a question, else parts ->
 * packages -> categories -> FEP_EV_LEAVE (a refusal caption is cleared on the way). */
FepEvent fpr_input(FePerf *p, int in);
int fpr_cat(const FePerf *p);                     /* focused category, -1 none */
int fpr_level(const FePerf *p);                   /* focused package level 1..3, FEP_SPECIFIC, or -1 */
const char *fpr_package(const FePerf *p);         /* package of the current page */

/* ---- package binding (gui_view.h) ---- */
/* Loads the current page's package and verifies its identity set (fe_binding.h). 0 + s->status on failure. */
int fpr_bind(FePerf *p, GvScreen *s, const UgAssets *a);
/* Writes entries, captions, prices, bank and lock / owned marks into the scene. 0 when art is missing. */
int fpr_sync(FePerf *p, GvScreen *s, float ms);

#ifdef __cplusplus
}
#endif
#endif
