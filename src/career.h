/* career.h -- career state, rules and save (OpenUG2 milestone M6, slice S1).
 * Pure C99 over career_data.h: no SDL, no GL, no globals. Every rule that is not settled by data is one small
 * function marked [inferred], so owner captures of the real game can replace it in one place.
 * Win counters are always recomputed from rec[] + the data, never stored, so they cannot drift. */
#ifndef OPENUG2_CAREER_H
#define OPENUG2_CAREER_H
#include "career_data.h"

#define CR_SCHEMA       1u
#define CR_MAGIC        0x5243554Fu   /* 'OUCR' little-endian */
#define CR_FILE_NAME    "openug2_career.bin"
#define CR_HIDE_WON     1             /* [inferred] won races disappear from the map */
enum { CR_REC_WON = 1, CR_REC_PAID = 2 };

typedef struct CrRaceRec { uint32_t hash; uint8_t best_place, flags; uint16_t attempts; } CrRaceRec;
typedef struct CareerState {
    uint8_t  started, stage, intro_done;
    int64_t  bank;
    uint32_t rep, entered, won;          /* entered/won: lifetime attempt and win counts (statistics only) */
    CrRaceRec rec[CD_MAX_RACES]; int nrec;
    char     car[32];
    float    pose[4];                    /* x, y, z, heading of the last free-roam position */
    uint8_t  extra[4096]; uint32_t nextra;   /* unknown TLVs from a newer writer, written back verbatim */
} CareerState;

typedef struct CrResult { uint32_t race_hash; int place, ncars, failed, aborted; double seconds; } CrResult;
typedef struct CrReward { int32_t cash, rep; } CrReward;
/* Reward policy hook. The race-rules work (M7) will supply the real one. */
typedef CrReward (*CrRewardFn)(void *user, const CdRace *race, const CrResult *res, int first_win);
/* [inferred] place 1, not failed, not aborted, first win -> {purse, rep_base}; otherwise {0, 0}. */
CrReward cr_reward_placeholder(void *user, const CdRace *race, const CrResult *res, int first_win);

typedef struct CrOutcome { CrReward reward; int first_win, stage_before, stage_after; } CrOutcome;

void cr_new(CareerState *s, const CareerData *cd, const char *car);   /* started=1, stage=1, intro_done=1 (Stage 0 skipped) */
/* [inferred] available = race.stage == s->stage && !(won && CR_HIDE_WON) && !suv && !hidden &&
 *   (mode STAGE || (mode SCRIPTED && stage == 1) || (mode URL && stage_wins >= ((arg >> 16) & 0xFF))
 *    || (mode URL_FINAL && url_wins >= ((arg >> 24) & 0xFF))). SPONSOR races are not available in S1. */
int  cr_available(const CareerState *s, const CareerData *cd, int race);
int  cr_list(const CareerState *s, const CareerData *cd, int *out, int cap);          /* available race indices */
/* Race whose locator zone contains (x, y) and that is available, or -1. Inside = point in the zone's 4-point polygon. */
int  cr_zone_hit(const CareerState *s, const CareerData *cd, float x, float y);
/* Applies one finished attempt. Unknown race -> 0. Always bumps attempts/entered; a win sets CR_REC_WON, best_place,
 * won++; the reward (from fn, or the placeholder if fn is NULL) is added to bank/rep only once per race
 * (CR_REC_PAID). Advances s->stage while cr_stage_complete holds and the next stage exists. Returns 1. */
int  cr_apply(CareerState *s, const CareerData *cd, const CrResult *r, CrRewardFn fn, void *user, CrOutcome *out);
/* [inferred] first wins (world + sponsor + URL) in the current stage >= req_total && URL wins >= req_url &&
 * (nsponsors == 0 || sponsor wins >= req_sponsor). DVD covers are not tracked in S1 and count as met. */
int  cr_stage_complete(const CareerState *s, const CareerData *cd);
int  cr_stage_wins(const CareerState *s, const CareerData *cd, int stage);           /* first wins in that stage */

/* Save: st_container (settings.h) with block A = {u32 CR_MAGIC, u32 schema} and block B = TLV list
 * {u16 tag, u16 0, u32 len, data}: 1 CORE, 2 RACES, 3 CAR, 4 POSE; unknown tags kept in extra[] and rewritten. */
size_t cr_serialize(const CareerState *s, uint8_t *out, size_t cap);   /* 0 if cap too small */
int  cr_parse(CareerState *s, const uint8_t *buf, size_t len);         /* ST_OK / ST_E_CORRUPT / ST_E_INCOMPATIBLE */
/* Files live at <root>/<profile>/CR_FILE_NAME. Save writes a temp file then renames it (atomic replace). */
int  cr_save_file(const char *root, const char *profile, const CareerState *s);   /* ST_OK or ST_E_* */
int  cr_load_file(const char *root, const char *profile, CareerState *s);         /* ST_E_NOTFOUND = no career yet */
int  cr_copy_file(const char *root, const char *from, const char *to);            /* for profile rename; NOTFOUND ok */
#endif
