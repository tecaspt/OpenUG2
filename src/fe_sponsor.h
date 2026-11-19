#ifndef OPENUG2_FE_SPONSOR_H
#define OPENUG2_FE_SPONSOR_H
#include "career.h"
#include "gui_view.h"
typedef struct FeSponsor {
    CdSponsor offer[5]; int index[5], eligible[5], n, sel, page;
    long long bank;
    char signing[32], bonus[32], purse[32], balance[48], name[96], body[1024], races[256];
} FeSponsor;
enum { FSP_NONE, FSP_PAGE, FSP_SIGN, FSP_LEAVE };
void fsp_open(FeSponsor *, const CareerData *, const CareerState *);
int fsp_input(FeSponsor *, int input);
const char *fsp_package(const FeSponsor *);
int fsp_bind(FeSponsor *, GvScreen *, const UgAssets *);
int fsp_sync(FeSponsor *, GvScreen *);
void fsp_draw(FeSponsor *, GvScreen *, UgCanvas *);
/* Post-race reward hook: separate credited URL bonus from the total cash. */
struct PostRace;
void pr_sponsor_reward(struct PostRace *, int32_t bonus);
#endif
