/* Career paint shop: copied snapshot, pure navigation and retail package binding. */
#ifndef OPENUG2_FE_PAINT_H
#define OPENUG2_FE_PAINT_H
#include "career_data.h"
#include "gui_view.h"
typedef struct FePaintColor { CdPaint paint; int index, price; unsigned open, installed; } FePaintColor;
typedef struct FePaintShop { FePaintColor color[CD_MAX_PAINT]; int n; long long bank; } FePaintShop;
typedef enum { FPT_MSG_NONE, FPT_MSG_CONFIRM, FPT_MSG_NO_FUNDS, FPT_MSG_LOCKED, FPT_MSG_OWNED, FPT_MSG_SAVE } FptMsg;
typedef enum { FPT_EV_NONE, FPT_EV_PAGE, FPT_EV_BUY, FPT_EV_LEAVE } FptEvent;
typedef struct FePaint {
 FePaintShop shop;
 int page, cats[CD_PAINT_GROUPS], ncats, cat_sel, rows[60], nrows, sel, confirm, buy_index;
 FptMsg msg;
 char caption[192], cash[48], total[32], label[96], mark[96], status[160];
} FePaint;
void fpt_open(FePaint *, const FePaintShop *);
void fpt_refresh(FePaint *, const FePaintShop *, FptMsg);
FptEvent fpt_input(FePaint *, int);
const FePaintColor *fpt_selected(const FePaint *);
const char *fpt_package(const FePaint *);
int fpt_bind(FePaint *, GvScreen *, const UgAssets *);
int fpt_sync(FePaint *, GvScreen *, float);
#endif
