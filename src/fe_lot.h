/* fe_lot.h -- career Car Lot screen (UI_CareerCarLot.fng, FRONTEND/FrontB.lzc), M6 W4.
 *
 * Two layers, like fe_car.c: a pure state machine (fel_open / fel_input / fel_advance: browsing,
 * the two-step choose confirmation, the must-buy Back block) and the binding of that state into the
 * authored package (fel_bind / fel_sync). The host (fe_menu.c) turns FEL_EV_* into FemRequests.
 * Decoupled from career.h: FeLotCar has the same field layout as the career unit's CrLotCar. */
#ifndef OPENUG2_FE_LOT_H
#define OPENUG2_FE_LOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FeLotCar { char name[32]; int32_t price; uint8_t owned, starter; } FeLotCar;

#define FEL_MAX 48

/* Caption shown in the screen header (0xddf04): every one is a retail language key. */
typedef enum {
    FEL_MSG_NONE = 0,        /* CAREER_CAR_LOT_TITLE 0x7edc9715 "Car Lot" */
    FEL_MSG_CONFIRM,         /* CAR_SELECT_CONFIRM_CHOOSE_CAR 0xda8f3e96, %s = car display name */
    FEL_MSG_MUST_BUY,        /* CAR_LOT_MUST_BUY 0x272ebeba */
    FEL_MSG_SAME_CAR         /* CAR_SELECT_SAME_CAR 0x7ee84e4e, %s = car display name */
} FelMsg;
typedef enum { FEL_EV_NONE = 0, FEL_EV_BUY, FEL_EV_LEAVE } FelEvent;

typedef struct FeLot {
    FeLotCar car[FEL_MAX];
    int n, sel;
    int must_buy;            /* Back is refused (CAR_LOT_MUST_BUY) until a car is bought */
    int confirm;             /* the first Accept asked CHOOSE_CAR; the next Accept buys */
    FelMsg msg;
    long long bank;
    float preview_wait;
    char preview[32];        /* car for the 3D preview, after the same 0.3 s debounce as QR Car Select */
    float header_scale;      /* view: scale the header carries (1 = authored size) */
    char caption[192], cash[48], status[160];
} FeLot;

/* Copies at most FEL_MAX cars; selection starts on the first car not owned (else the first). */
void fel_open(FeLot *l, const FeLotCar *cars, int n, int must_buy, long long bank);
/* `in` is an FesInput (FES_IN_LEFT/RIGHT/ACCEPT/BACK); other values are ignored.
 * Left/Right: browse without wrap (clears any caption). Accept: owned car -> SAME_CAR caption;
 * otherwise the first Accept asks CONFIRM, the second returns FEL_EV_BUY. Back: cancels a pending
 * confirmation; else MUST_BUY caption while must_buy; else FEL_EV_LEAVE. */
FelEvent fel_input(FeLot *l, int in);
void fel_advance(FeLot *l, float seconds);
/* Name of the browsed car ("" when the lot is empty). */
const char *fel_selected(const FeLot *l);

/* ---- package binding (gui_view.h) ---- */
struct GvScreen;
struct UgAssets;
/* Loads UI_CareerCarLot.fng and verifies its identity set (fe_binding.h). 0 + s->status on failure. */
int fel_bind(FeLot *l, struct GvScreen *s, const struct UgAssets *a);
/* Writes car name, logo, cash and caption into the scene and hides the unimplemented stats. */
void fel_sync(FeLot *l, struct GvScreen *s);

#ifdef __cplusplus
}
#endif
#endif
