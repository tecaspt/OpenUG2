/* career.h -- career state, rules and save (OpenUG2 milestone M6, slice S1).
 * Pure C99 over career_data.h: no SDL, no GL, no globals. Every rule that is not settled by data is one small
 * function marked [inferred], so owner captures of the real game can replace it in one place.
 * Win counters are always recomputed from rec[] + the data, never stored, so they cannot drift. */
#ifndef OPENUG2_CAREER_H
#define OPENUG2_CAREER_H
#include "career_data.h"
#include "garage.h"

#define CR_SCHEMA       5u            /* 5: + TLV 8 ordered SMS receipts and read state */
#define CR_PAINT_SLOTS  9             /* 0 body, 1 rims, 2 calipers, 3 muffler, 4..8 reserved; value = part key, 0 = stock */
#define CR_MAGIC        0x5243554Fu   /* 'OUCR' little-endian */
#define CR_FILE_NAME    "openug2_career.bin"
#define CR_HIDE_WON     1             /* [inferred] won races disappear from the map */
#define CR_MAX_OWNED    5
#define CR_REGION_UNKNOWN 0xFFu       /* car-unlock column not chosen yet (schema 1 saves) */
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
    /* schema 2 */
    uint8_t  nowned, active, region;     /* owned cars, index of the driven one, unlock column 0 = A, 1 = B */
    GarageConfig config[CR_MAX_OWNED]; /* schema 3: career-only installed configurations */
    char     owned[CR_MAX_OWNED][32];    /* car folder names */
    uint16_t nsms; uint8_t sms_ready; /* false: host must migrate after loading CareerData */
    struct { uint32_t hash; uint8_t read; } sms[CD_MAX_SMS];
    /* schema 4 (TLV 7): per owned car, installed performance components (bit i = CareerData.perf_comp[i]) and paint */
    uint32_t perf[CR_MAX_OWNED][3];
    uint32_t paint[CR_MAX_OWNED][CR_PAINT_SLOTS];
} CareerState;

typedef struct CrResult { uint32_t race_hash; int place, ncars, failed, aborted; double seconds; } CrResult;
typedef struct CrReward { int32_t cash, rep; } CrReward;
/* Reward policy hook. The race-rules work (M7) will supply the real one. */
typedef CrReward (*CrRewardFn)(void *user, const CdRace *race, const CrResult *res, int first_win);
/* [inferred] place 1, not failed, not aborted, first win -> {purse, rep_base}; otherwise {0, 0}. */
CrReward cr_reward_placeholder(void *user, const CdRace *race, const CrResult *res, int first_win);

typedef struct CrOutcome { CrReward reward; int first_win, stage_before, stage_after; } CrOutcome;

/* started=1, stage=1, intro_done=1 (Stage 0 skipped); owned = {car}, region unknown. */
void cr_new(CareerState *s, const CareerData *cd, const char *car);
/* New career at Stage 0 (story start): started=1, stage=0, no car owned, bank 0 (Defaults: starting cash 0). region is
 * the car-unlock column (0 A, 1 B; anything else stores CR_REGION_UNKNOWN). */
void cr_new_stage0(CareerState *s, const CareerData *cd, int region);
/* [inferred] car-unlock column for the installed cars (car folder names): the column whose every unlockable car
 * (non-zero race) is installed; both or neither -> 0 (A). */
int  cr_region_pick(const CareerData *cd, const char *const *inst, int n);
/* [inferred] available = race.stage == s->stage && !(won && CR_HIDE_WON) && !suv && !hidden && type != SCRIPTED &&
 *   (mode STAGE || mode SCRIPTED || (mode URL && stage_wins >= ((arg >> 16) & 0xFF))
 *    || (mode URL_FINAL && url_wins >= ((arg >> 24) & 0xFF))). SPONSOR races are not available in S1.
 * Scripted story events (type CD_TYPE_SCRIPTED, DDAY_EVENT_A/B) are objectives (cr_objective), never map races. */
int  cr_available(const CareerState *s, const CareerData *cd, int race);
int  cr_list(const CareerState *s, const CareerData *cd, int *out, int cap);          /* available race indices */
/* Race whose locator zone contains (x, y) and that is available, or -1. Inside = point in the zone's 4-point polygon. */
int  cr_zone_hit(const CareerState *s, const CareerData *cd, float x, float y);
/* Applies one finished attempt. Unknown race -> 0. Always bumps attempts/entered; a win sets CR_REC_WON, best_place,
 * won++; the reward (from fn, or the placeholder if fn is NULL) is added to bank/rep only once per race
 * (CR_REC_PAID). Advances s->stage while cr_stage_complete holds and the next stage exists. Returns 1. */
int  cr_apply(CareerState *s, const CareerData *cd, const CrResult *r, CrRewardFn fn, void *user, CrOutcome *out);
/* [inferred] a stage whose final_race is a scripted event (Stage 0: DDAY_EVENT_B) completes when that event is done.
 * Otherwise first wins (world + sponsor + URL) in the current stage >= req_total && URL wins >= req_url &&
 * (nsponsors == 0 || sponsor wins >= req_sponsor). DVD covers are not tracked in S1 and count as met. */
int  cr_stage_complete(const CareerState *s, const CareerData *cd);
int  cr_stage_wins(const CareerState *s, const CareerData *cd, int stage);           /* first wins in that stage */

/* ---- story objectives and shops (M6 car lot) ---- */
/* Current story objective: the first scripted event of the current stage that is not done and whose unlock_arg
 * (a race hash, 0 = none) is done. Race index or -1. Its shop_hash names the shop that resolves it. */
int  cr_objective(const CareerState *s, const CareerData *cd);
/* Marks the current objective done when shop is -1 or the objective's shop_hash is cd->shop[shop].hash, records it
 * (CR_REC_WON | CR_REC_PAID, no reward, not counted in won/entered), then advances stages like cr_apply.
 * 1 when an objective was completed. out (may be NULL) gets first_win=1 and stage_before/after. */
int  cr_objective_done(CareerState *s, const CareerData *cd, int shop, CrOutcome *out);
/* [inferred] shop open = supported type and open_stage <= s->stage. */
int  cr_shop_open(const CareerState *s, const CareerData *cd, int shop);
/* Open shop whose zone contains (x, y), or -1. */
int  cr_shop_hit(const CareerState *s, const CareerData *cd, float x, float y);

/* Supported geometry categories, independent of yet-undecoded retail unlock tiers. */
int cr_shop_slot(int type, int slot);
int32_t cr_rule_part_price(const CareerData *, int shop, int slot, int value);
/* Validates shop availability and changed categories before updating the active owned car.
 * Returns ST_OK / ST_E_ARG; the host saves transactionally after renderer acknowledgement. */
int cr_shop_install(CareerState *, const CareerData *, int shop, const GarageConfig *);
const GarageConfig *cr_car_config(const CareerState *);

/* ---- performance shop (docs/parity/PERF-PAINT.md) ---- */
enum { CR_PERF_OK = 0, CR_PERF_E_ARG = -1, CR_PERF_E_LOCKED = -2, CR_PERF_E_FUNDS = -3, CR_PERF_E_OWNED = -4 };
/* Unlock record 0x34A1C `id`, level 1..3: type 6 start, 0 never, 1 shop open [inferred: retail unlocks on discovery],
 * 2/3/4 {u16 count, u16 stage}: past that stage, or in it with count wins / sponsor wins / URL wins [inferred]. */
int  cr_unlock_ok(const CareerState *s, const CareerData *cd, uint32_t id, int lvl);
int  cr_perf_unlock_id(int cat);                                           /* category -> 0x34A1C id [inferred] */
int  cr_perf_level_open(const CareerState *s, const CareerData *cd, int cat, int lvl);
int  cr_perf_owned(const CareerState *s, int comp);                        /* on the active car */
/* comp >= 0: one component; comp < 0: the whole cat/lvl package (components not owned yet, 10% off [inferred]). */
int32_t cr_perf_price(const CareerState *s, const CareerData *cd, int cat, int lvl, int comp);
/* Validates shop (an open performance shop), unlock, funds and ownership; installs (exclusive components are
 * replaced), debits the bank. Returns CR_PERF_*; *charged = amount paid. */
int  cr_perf_buy(CareerState *s, const CareerData *cd, int shop, int cat, int lvl, int comp, int32_t *charged);
/* Upgrade fraction per category/axis in [0,1]: clamped sum of the installed components' axis shares [data]. */
void cr_perf_fraction(const CareerState *s, const CareerData *cd, float u[CD_PERF_CATS][4]);
int  cr_perf_level_of(float u);                                            /* 0..3 at 0.325/0.655/0.995 [inferred] */

/* ---- paint (graphics) shop ---- */
/* Slot per paint group: GLOSS/METAL/PEARL -> 0 body, RIM 1, CALIPER 2, MUFFLER 3, HOSE 4, VINYL 5 (vinyl colour). */
int  cr_paint_slot(int group);
/* 0x34A1C unlock id per group [inferred, docs/parity/PERF-PAINT.md]: 76 body (also hose/vinyl), 77 rims,
 * 78 muffler, 79 calipers. Level = the paint's own level. */
int  cr_paint_unlock_id(int group);
int  cr_paint_open(const CareerState *s, const CareerData *cd, int paint);   /* paint = index into cd->paint[] */
int32_t cr_paint_price(const CareerData *cd, int paint);   /* 0x34A1C level `b` field [inferred, low]; 0 when absent */
/* Shop must be an open paint shop; installs the colour in its slot on the active car and debits the bank.
 * Returns CR_PERF_* codes (OWNED = that colour is already in the slot). */
int  cr_paint_buy(CareerState *s, const CareerData *cd, int shop, int paint, int32_t *charged);
uint32_t cr_paint_installed(const CareerState *s, int slot);   /* part key or 0 (stock) on the active car */

/* Ordered, unique receipts keyed by name hash, stable across table reorderings.
 * Receive returns 1 only for a new receipt. Read returns 1 only for a held message. */
int cr_sms_receive(CareerState *, uint32_t hash, int read);
int cr_sms_read(CareerState *, uint32_t hash);
int cr_sms_unread(const CareerState *);
/* Evaluate progress-backed conditions. shop is a discovered shop index, or -1.
 * migrate marks implied receipts read; elapsed is seconds in this roam session.
 * Timing and trigger mappings are [inferred], documented in SMS-MAILBOX.md. */
int cr_sms_deliver(CareerState *, const CareerData *, int shop, double elapsed, int migrate);

/* ---- car lot ---- */
typedef struct CrLotCar { char name[32]; int32_t price; uint8_t owned, starter; } CrLotCar;
enum { CR_LOT_OK = 0, CR_LOT_E_SAME = -1, CR_LOT_E_LOCKED = -2, CR_LOT_E_FUNDS = -3, CR_LOT_E_ARG = -4 };
/* [inferred] lot price of a stock car. No price exists in the data: 0 (free stock-car choice). */
int32_t cr_rule_car_price(const CareerData *cd, const char *car);
int  cr_owns(const CareerState *s, const char *car);
/* Cars on sale (same inventory at every lot): installed cars (inst, car folder names, in that order) that are
 * starters (no unlock race in the region column) or whose unlock race is won. Region: s->region, or
 * cr_region_pick when unknown. Returns the count written (<= cap). */
int  cr_lot_list(const CareerState *s, const CareerData *cd, const char *const *inst, int n, CrLotCar *out, int cap);
/* Buys car: CR_LOT_E_ARG (null / not installed), E_LOCKED (not on sale), E_SAME (already the driven car),
 * E_FUNDS (bank < price); CR_LOT_OK: bank -= price, owned = {car} (Defaults: 1 car slot), active 0, s->car = car,
 * region fixed when unknown, and a current objective resolved by a car lot (Stage 0: DDAY_EVENT_A) is completed.
 * out (may be NULL) is filled on OK (first_win = 1 when an objective was completed). */
int  cr_lot_buy(CareerState *s, const CareerData *cd, const char *const *inst, int n, const char *car, CrOutcome *out);

/* Save: st_container (settings.h) with block A = {u32 CR_MAGIC, u32 schema} and block B = TLV list
 * {u16 tag, u16 0, u32 len, data}: 1 CORE, 2 RACES, 3 CAR, 4 POSE, 5 OWNED {u8 n<=5, u8 active, u8 region, u8 0,
 * n x char[32]} (schema 2); unknown tags kept in extra[] and rewritten. Schema 1 (or a missing TLV 5) migrates to
 * owned = {car} (none when car is empty), active 0, region CR_REGION_UNKNOWN. Schema > CR_SCHEMA -> INCOMPATIBLE. */
size_t cr_serialize(const CareerState *s, uint8_t *out, size_t cap);   /* 0 if cap too small */
int  cr_parse(CareerState *s, const uint8_t *buf, size_t len);         /* ST_OK / ST_E_CORRUPT / ST_E_INCOMPATIBLE */
/* Files live at <root>/<profile>/CR_FILE_NAME. Save writes a temp file then renames it (atomic replace). */
int  cr_save_file(const char *root, const char *profile, const CareerState *s);   /* ST_OK or ST_E_* */
int  cr_load_file(const char *root, const char *profile, CareerState *s);         /* ST_E_NOTFOUND = no career yet */
int  cr_copy_file(const char *root, const char *from, const char *to);            /* for profile rename; NOTFOUND ok */
#endif
