/* career_data.h -- retail career database reader (OpenUG2 milestone M6, slice S1).
 *
 * Parses, from the player's own retail data:
 *   - GLOBAL/GLOBALB.BUN, chunk 0x80034A10 (the FIRST one; a second, smaller copy is a demo career and is ignored):
 *       0x34A11 races (136-byte records), 0x34A18 stages (80-byte records), 0x34A1D string blob,
 *       0x34A12 shops (160-byte records, optional), 0x34A1F car unlocks (12-byte records, optional).
 *   - TRACKS/ROUTESL4RA/PathsFreeRoam.bin, chunk 0x3414A: race-locator zones (bbox, name hash, 4 XY points).
 *   - TRACKS/ROUTESL4RA/TrackPosMarkersFreeRoam.bin, chunk 0x34146: named position markers (car lot, crib, ...).
 * Field meanings are tagged in docs/parity/M6-CAREER.md ([data] / [inferred]). Every record keeps its raw bytes so a
 * later decode never needs a re-parse. Pure C99: no SDL, no GL, no globals. Buffers in, structs out; every read is
 * bounds-checked, so corrupt input returns 0 and never crashes.
 * Clean room: derived from data files only, never from the game executable. */
#ifndef OPENUG2_CAREER_DATA_H
#define OPENUG2_CAREER_DATA_H
#include <stddef.h>
#include <stdint.h>

#define CD_MAX_RACES   256
#define CD_MAX_STAGES  8
#define CD_MAX_ZONES   192
#define CD_MAX_MARKERS 64
#define CD_RACE_BYTES  136
#define CD_STAGE_BYTES 80
#define CD_MAX_SHOPS   32
#define CD_MAX_UNLOCKS 48
#define CD_MAX_SMS     192
#define CD_SHOP_BYTES  160
#define CD_UNLOCK_BYTES 12
#define CD_SMS_BYTES   20
#define CD_RACE_ZONE_MAX 20.0f   /* [data] race locator zones are 8x8 m */
#define CD_SHOP_ZONE_MAX 32.0f   /* [data] shop zones are 15..21.1 m wide (CRIB_1) */

/* unlock_mode (+0x0C) [inferred names] */
enum { CD_UNLOCK_SCRIPTED = 0, CD_UNLOCK_STAGE = 1, CD_UNLOCK_SPONSOR = 2, CD_UNLOCK_URL = 3, CD_UNLOCK_URL_FINAL = 4 };
/* category (+0x34) [inferred names] */
enum { CD_CAT_INTRO = 0, CD_CAT_SPONSOR = 1, CD_CAT_URL = 2, CD_CAT_WORLD = 3 };
/* race type (+0x0F) 3 = scripted story event (DDAY_EVENT_A/B): no locator zone, resolved by a shop visit */
#define CD_TYPE_SCRIPTED 3
/* shop type (+0x50) [inferred names] */
enum { CD_SHOP_CRIB = 0, CD_SHOP_PAINT = 1, CD_SHOP_BODY = 2, CD_SHOP_PERF = 3, CD_SHOP_CAR_LOT = 4,
       CD_SHOP_SPECIALTY = 5, CD_SHOP_UNIQUES = 6 };

typedef struct CdHeat { uint16_t track; uint8_t dir, laps; } CdHeat;          /* track = event id, e.g. 4101 */
typedef struct CdRace {
    char     name[32], locator[24];      /* strings from the 0x34A1D blob (+0 / +6 offsets) */
    uint32_t name_hash, locator_hash;    /* +0x08 / +0x28 (ug_hash of the names) */
    uint32_t unlock_arg;                 /* +0x10 */
    uint32_t purse;                      /* +0x30 cash */
    uint32_t rep_base;                   /* +0x14 [inferred] reputation base */
    uint32_t preset_hash;                /* +0x2C ug_hash of a preset car (DDAY_EVENT_A: DDAY_PLAYER_CAR), else 0 */
    uint32_t shop_hash;                  /* +0x3C ug_hash of the shop that resolves a scripted event, else 0 */
    uint8_t  unlock_mode, suv, type, category, stage, nai, hidden, nheat;   /* +0x0C +0x0D +0x0F +0x34 +0x37 +0x7C +0x7F */
    CdHeat   heat[4];                    /* +0x18, 4 bytes each; nheat = entries with track != 0 */
    int      kind;                       /* N2_RACE_* from race.h, mapped from `type` */
    uint8_t  raw[CD_RACE_BYTES];
} CdRace;
typedef struct CdStage {
    uint8_t  index, nsponsors, req_total, req_sponsor, req_url, req_dvd;     /* +0 +1 +0x42 ? +0x2E +0x2F */
    uint32_t sponsor[5];                                                       /* +0x08 hashes */
    uint32_t final_race;                 /* +0x28 race hash [inferred] the stage's final event (0 = none) */
    uint8_t  raw[CD_STAGE_BYTES];
} CdStage;
typedef struct CdShop {
    char     name[32];                   /* +0x00 (char[0x38], truncated) */
    uint32_t hash, zone_hash;            /* +0x38 ug_hash(name) = marker name; +0x3C zone hash in PathsFreeRoam */
    char     platform[16];               /* +0x40 platform file, e.g. CARLOT01.BIN */
    uint8_t  type, known, open_stage;    /* +0x50 CD_SHOP_*; +0x9C [inferred] known at start; +0x9D [inferred] stage */
} CdShop;
typedef struct CdCarUnlock { uint32_t car_hash, race[2]; } CdCarUnlock;   /* ug_hash(car folder), race A/B (0 = none) */
/* SMS/message table (+0x05) [inferred names]; +0x03 is in 1:1 correspondence with it over all 167 retail records. */
enum { CD_SMS_MSG = 1, CD_SMS_MAG = 3, CD_SMS_UNLOCK = 5, CD_SMS_TIP = 9 };
/* 0x34A17: one retail career message (SMS), 20-byte record. Field meanings and their evidence are in
 * docs/parity/SMS-TABLE.md; only +0x00 and the three constant bytes are certain, the rest are named by what
 * they hold, not by a guessed use. arg keeps all four bytes because the same slot is a self hash in the 42 tip
 * records, another table's name hash in 4 records, and two u16 args in the other 121. */
typedef struct CdSms {
    char     name[40];                   /* +0x00 u16 offset into the 0x34A1D blob (empty if out of range) */
    uint32_t name_hash;                  /* cd_hash(name); the record itself carries no name hash */
    uint8_t  trigger, cls, flag, kind;   /* +0x02 (14 values, unknown) +0x03 +0x04 +0x05 CD_SMS_* */
    uint32_t arg;                        /* +0x08 self hash / entity hash / two u16 args */
    uint16_t weight;                     /* +0x0C 0, 25 or 50 (non-zero only for tips); unknown meaning */
    uint32_t sender;                     /* +0x10 language key of the sender name (Labels.bin SMS_FROM_*) */
    uint8_t  raw[CD_SMS_BYTES];
} CdSms;
#define CD_SMS_ARG_LO(a) ((uint16_t)((a) & 0xFFFFu))      /* the +0x08 u16 half, where the pair reading applies */
#define CD_SMS_ARG_HI(a) ((uint16_t)(((a) >> 16) & 0xFFFFu))
typedef struct CdZone   { uint32_t hash; float cx, cy, poly[4][2]; } CdZone;           /* hash = locator hash */
typedef struct CdMarker { uint32_t hash; float x, y, z, heading; } CdMarker;
typedef struct CareerData {
    CdRace   race[CD_MAX_RACES];   int nrace;
    CdStage  stage[CD_MAX_STAGES]; int nstage;
    CdZone   zone[CD_MAX_ZONES];   int nzone;
    CdMarker marker[CD_MAX_MARKERS]; int nmarker;
    CdShop   shop[CD_MAX_SHOPS];   int nshop;       /* 0 when the table is missing */
    CdCarUnlock unlock[CD_MAX_UNLOCKS]; int nunlock; /* 0 when the table is missing */
    CdSms    sms[CD_MAX_SMS];      int nsms;        /* 0 when the table is missing */
    char     error[96];
} CareerData;

/* Each returns the number of records parsed (>0), or 0 with cd->error set. cd_parse_globalb also reads the shop,
 * car-unlock and SMS tables when present (missing or malformed -> nshop / nunlock / nsms 0, not an error).
 * cd_parse_zones keeps race
 * locator zones up to CD_RACE_ZONE_MAX and shop zones (cd->shop[].zone_hash) up to CD_SHOP_ZONE_MAX; when no races
 * and no shops are loaded it keeps every zone up to CD_RACE_ZONE_MAX. */
int cd_parse_globalb(CareerData *cd, const uint8_t *buf, size_t len);
int cd_parse_zones(CareerData *cd, const uint8_t *paths_freeroam, size_t len);
int cd_parse_markers(CareerData *cd, const uint8_t *trackpos, size_t len);
/* Loads all three files under dataroot (career city L4RA). 1 ok (races, stages and zones all > 0), 0 error. */
int cd_load(CareerData *cd, const char *dataroot);
int cd_find(const CareerData *cd, uint32_t name_hash);          /* race index or -1 */
int cd_find_zone(const CareerData *cd, uint32_t locator_hash);  /* zone index or -1 */
int cd_find_marker(const CareerData *cd, uint32_t hash);        /* marker index or -1 */
int cd_find_shop(const CareerData *cd, uint32_t hash);          /* shop index (by name hash) or -1 */
int cd_find_sms(const CareerData *cd, uint32_t name_hash);      /* sms index (by cd_hash(name)) or -1; 0 never matches */
uint32_t cd_hash(const char *s);                                /* ug_hash of the upper-cased string */
#endif
