/* career_host.h -- glue between the career rules (career.h) and the game loop (OpenUG2 M6, slice S1).
 * Pure C99: no SDL, no GL, no globals. main.c owns one CareerHost and calls these at fixed hook points:
 *   startup ch_init; Main->Career ch_begin; Enter Race in free roam ch_engage; results ch_on_result;
 *   post-race Continue ch_return; HUD ch_icons / ch_prompt; quit to Main ch_suspend;
 *   M6 car lot: ch_shop_zone in free roam, ch_lot_list / ch_lot_buy on the lot screen, ch_crib_done leaving the
 *   career Garage (crib). Installed cars = the catalogue's non-traffic cars. Status lines go to ch->last.
 * Policies not settled by data are marked [inferred] (see docs/parity/M6-CAREER.md). */
#ifndef OPENUG2_CAREER_HOST_H
#define OPENUG2_CAREER_HOST_H
#include "career.h"
#include "game_catalog.h"
#include "game_flow.h"
#include "hud_minimap.h"

#define CH_CITY_TRACK   "STREAML4RA"   /* [data] career free roam city (race locators live in ROUTESL4RA) */
#define CH_ENGAGE_KMH   30.0f          /* [inferred] Enter Race only accepted below this speed */
#define CH_MAX_INST     64

/* ch_begin results */
enum { CH_BEGIN_FAIL = 0, CH_BEGIN_ROAM = 1, CH_BEGIN_LOT = 2 };

typedef struct CareerHost {
    CareerData cd;
    CareerState st;
    const GameCatalog *cat;
    int  loaded;          /* career data parsed */
    int  active;          /* a career is being played (free roam or a career race) */
    int  race;            /* race index of the engaged career event, -1 when none */
    int  resolved;        /* ch_on_result already applied for the engaged event */
    int  spawn_valid;     /* ch_return/ch_begin set a spawn the host should use */
    float spawn[4];       /* x, y, z, heading (z/heading 0 = host picks nearest road) */
    char root[256], profile[32];
    char last[128];       /* last human-readable status line (also printed by the host) */
    CrOutcome outcome;    /* outcome of the last applied result / lot buy / crib visit */
    int  at_shop;         /* shop the player is in (host sets it from ch_shop_zone on entering), -1 unknown */
    int  lot_rc;          /* CR_LOT_* of the last ch_lot_buy */
} CareerHost;

/* Loads the career data. 1 ok, 0 (host keeps Career disabled; ch->last says why). */
int  ch_init(CareerHost *ch, const char *dataroot, const GameCatalog *cat);
/* Starts a new career (new_career=1) or continues the saved one for profile under root. Fills a free-roam launch
 * into the career city. CH_BEGIN_FAIL (0) when there is no saved career to continue or the save fails/refuses.
 * New career: Stage 0, region picked from the installed cars, no car -> CH_BEGIN_LOT (open the car lot at once,
 * must buy; out is a free roam at the lot marker with `car` as preview, at_shop = the lot). Continue: CH_BEGIN_LOT
 * when a Stage 0 career owns no car yet, else CH_BEGIN_ROAM. Logs "career: new stage 0 ..." / "career: continue ...". */
int  ch_begin(CareerHost *ch, int new_career, const char *root, const char *profile, const char *car, GfLaunch *out);
/* Race index whose locator zone contains (x, y), available, and engageable at this speed; else -1. */
int  ch_zone(const CareerHost *ch, float x, float y, float kmh);
/* Engage the event at (x, y): fills the race launch (first heat: event id, region track, laps, current car).
 * Remembers the free-roam pose for the return. Race index, or -1. */
int  ch_engage(CareerHost *ch, float x, float y, float heading, float kmh, const char *car, GfLaunch *out);
/* Apply the finished engaged event once (later calls are ignored until the next engage), then save.
 * Returns 1 when applied. ch->outcome / ch->last describe it. */
int  ch_on_result(CareerHost *ch, int finish_place, int ncars, int failed, int aborted, double seconds);
/* Post-race Continue: free-roam launch back to the career city at the pre-race pose (spawn_valid set). */
int  ch_return(CareerHost *ch, const char *car, GfLaunch *out);
/* Free-roam pose update + save (call on quit to Main). Leaves the career inactive. */
void ch_suspend(CareerHost *ch, float x, float y, float z, float heading);
/* Map icons for available career events, then open shops (HMI_SHOP). Returns count. */
int  ch_icons(const CareerHost *ch, HudMapIcon *out, int cap);
/* Notice text when the player is in an engageable race zone ("<label>: <name>  $purse") or open shop zone
 * ("<label>: <shop name>"), else NULL. */
const char *ch_prompt(const CareerHost *ch, float x, float y, float kmh, const char *label, char *buf, int cap);
/* Open shop (index into ch->cd.shop) whose zone contains (x, y), engageable at this speed, not while racing; else -1. */
int  ch_shop_zone(const CareerHost *ch, float x, float y, float kmh);
/* Lot inventory (cr_lot_list over the installed cars). Count. ch is const, so the log line comes from ch_lot_line. */
int  ch_lot_list(const CareerHost *ch, CrLotCar *out, int cap);
/* Formats "career: lot N cars: A B* ..." (* = owned) into buf (truncated to cap) for the host to print. */
const char *ch_lot_line(const CrLotCar *c, int n, char *buf, int cap);
/* Buys car (cr_lot_buy) and saves. 1 ok: out = free roam in the new car at the lot marker (at_shop if it is a car
 * lot, else the Stage 0 lot, else the first open lot; spawn_valid set, pose saved); logs "career: bought X".
 * 0: refused, ch->lot_rc = CR_LOT_E_*, ch->last says why. */
int  ch_lot_buy(CareerHost *ch, const char *car, GfLaunch *out);
/* Leaving the career Garage at the crib: completes a crib objective (Stage 0: DDAY_EVENT_B -> stage 1, logs
 * "career: stage 0 -> 1"), saves, and fills a free roam at the crib marker in the career car. 1 ok, 0 no career. */
int  ch_crib_done(CareerHost *ch, GfLaunch *out);
/* Takes a pending spawn (1) and clears it. */
int  ch_take_spawn(CareerHost *ch, float spawn[4]);
#endif
