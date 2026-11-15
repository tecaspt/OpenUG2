/* career_data.h -- retail career database reader (OpenUG2 milestone M6, slice S1).
 *
 * Parses, from the player's own retail data:
 *   - GLOBAL/GLOBALB.BUN, chunk 0x80034A10 (the FIRST one; a second, smaller copy is a demo career and is ignored):
 *       0x34A11 races (136-byte records), 0x34A18 stages (80-byte records), 0x34A1D string blob.
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

/* unlock_mode (+0x0C) [inferred names] */
enum { CD_UNLOCK_SCRIPTED = 0, CD_UNLOCK_STAGE = 1, CD_UNLOCK_SPONSOR = 2, CD_UNLOCK_URL = 3, CD_UNLOCK_URL_FINAL = 4 };
/* category (+0x34) [inferred names] */
enum { CD_CAT_INTRO = 0, CD_CAT_SPONSOR = 1, CD_CAT_URL = 2, CD_CAT_WORLD = 3 };

typedef struct CdHeat { uint16_t track; uint8_t dir, laps; } CdHeat;          /* track = event id, e.g. 4101 */
typedef struct CdRace {
    char     name[32], locator[24];      /* strings from the 0x34A1D blob (+0 / +6 offsets) */
    uint32_t name_hash, locator_hash;    /* +0x08 / +0x28 (ug_hash of the names) */
    uint32_t unlock_arg;                 /* +0x10 */
    uint32_t purse;                      /* +0x30 cash */
    uint32_t rep_base;                   /* +0x14 [inferred] reputation base */
    uint8_t  unlock_mode, suv, type, category, stage, nai, hidden, nheat;   /* +0x0C +0x0D +0x0F +0x34 +0x37 +0x7C +0x7F */
    CdHeat   heat[4];                    /* +0x18, 4 bytes each; nheat = entries with track != 0 */
    int      kind;                       /* N2_RACE_* from race.h, mapped from `type` */
    uint8_t  raw[CD_RACE_BYTES];
} CdRace;
typedef struct CdStage {
    uint8_t  index, nsponsors, req_total, req_sponsor, req_url, req_dvd;     /* +0 +1 +0x42 ? +0x2E +0x2F */
    uint32_t sponsor[5];                                                       /* +0x08 hashes */
    uint8_t  raw[CD_STAGE_BYTES];
} CdStage;
typedef struct CdZone   { uint32_t hash; float cx, cy, poly[4][2]; } CdZone;           /* hash = locator hash */
typedef struct CdMarker { uint32_t hash; float x, y, z, heading; } CdMarker;
typedef struct CareerData {
    CdRace   race[CD_MAX_RACES];   int nrace;
    CdStage  stage[CD_MAX_STAGES]; int nstage;
    CdZone   zone[CD_MAX_ZONES];   int nzone;
    CdMarker marker[CD_MAX_MARKERS]; int nmarker;
    char     error[96];
} CareerData;

/* Each returns the number of records parsed (>0), or 0 with cd->error set. */
int cd_parse_globalb(CareerData *cd, const uint8_t *buf, size_t len);
int cd_parse_zones(CareerData *cd, const uint8_t *paths_freeroam, size_t len);
int cd_parse_markers(CareerData *cd, const uint8_t *trackpos, size_t len);
/* Loads all three files under dataroot (career city L4RA). 1 ok (races, stages and zones all > 0), 0 error. */
int cd_load(CareerData *cd, const char *dataroot);
int cd_find(const CareerData *cd, uint32_t name_hash);          /* race index or -1 */
int cd_find_zone(const CareerData *cd, uint32_t locator_hash);  /* zone index or -1 */
int cd_find_marker(const CareerData *cd, uint32_t hash);        /* marker index or -1 */
#endif
