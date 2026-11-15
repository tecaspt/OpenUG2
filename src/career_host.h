/* career_host.h -- glue between the career rules (career.h) and the game loop (OpenUG2 M6, slice S1).
 * Pure C99: no SDL, no GL, no globals. main.c owns one CareerHost and calls these at fixed hook points:
 *   startup ch_init; Main->Career ch_begin; Enter Race in free roam ch_engage; results ch_on_result;
 *   post-race Continue ch_return; HUD ch_icons / ch_prompt; quit to Main ch_suspend.
 * Policies not settled by data are marked [inferred] (see docs/parity/M6-CAREER.md). */
#ifndef OPENUG2_CAREER_HOST_H
#define OPENUG2_CAREER_HOST_H
#include "career.h"
#include "game_catalog.h"
#include "game_flow.h"
#include "hud_minimap.h"

#define CH_CITY_TRACK   "STREAML4RA"   /* [data] career free roam city (race locators live in ROUTESL4RA) */
#define CH_ENGAGE_KMH   30.0f          /* [inferred] Enter Race only accepted below this speed */

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
    CrOutcome outcome;    /* outcome of the last applied result */
} CareerHost;

/* Loads the career data. 1 ok, 0 (host keeps Career disabled; ch->last says why). */
int  ch_init(CareerHost *ch, const char *dataroot, const GameCatalog *cat);
/* Starts a new career (new_career=1) or continues the saved one for profile under root. Fills a free-roam launch
 * into the career city. 1 ok; 0 when there is no saved career to continue or the save fails/refuses. */
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
/* Map icons for available career events. Returns count. */
int  ch_icons(const CareerHost *ch, HudMapIcon *out, int cap);
/* Notice text when the player is in an engageable zone ("Press Enter Race: <name>"), else NULL. */
const char *ch_prompt(const CareerHost *ch, float x, float y, float kmh, char *buf, int cap);
/* Takes a pending spawn (1) and clears it. */
int  ch_take_spawn(CareerHost *ch, float spawn[4]);
#endif
