/* career_data.c -- retail career database reader (M6-S1). See career_data.h. Clean room: data files only. */
#include "career_data.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "race.h"
#pragma GCC diagnostic pop
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CD_ID_CAREER   0x80034A10u
#define CD_ID_RACES    0x00034A11u
#define CD_ID_STAGES   0x00034A18u
#define CD_ID_STRINGS  0x00034A1Du
#define CD_ID_SHOPS    0x00034A12u
#define CD_ID_UNLOCKS  0x00034A1Fu
#define CD_ID_ZONES    0x0003414Au
#define CD_ID_MARKERS  0x00034146u
#define CD_MAX_FILE    (256u * 1024u * 1024u)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static int cd_fail(CareerData *cd, const char *msg) {
    snprintf(cd->error, sizeof cd->error, "%s", msg);
    return 0;
}

/* Finds the first chunk with `id` in [buf+off, buf+end); sets the data offset and size. Sizes are checked against `end`. */
static int find_chunk(const uint8_t *buf, size_t off, size_t end, uint32_t id, size_t *data, size_t *size) {
    while (off <= end && end - off >= 8) {
        uint32_t cid = rd32(buf + off), sz = rd32(buf + off + 4);
        if ((size_t)sz > end - off - 8) return 0;
        if (cid == id) { *data = off + 8; *size = sz; return 1; }
        off += 8 + (size_t)sz;
    }
    return 0;
}

/* String at blob offset `o`; copies into out (truncating). Returns 0 if out of range / unterminated. */
static int blob_str(const uint8_t *blob, size_t blen, unsigned o, char *out, size_t cap) {
    size_t i = 0;
    out[0] = 0;
    if (o >= blen) return 0;
    for (; o + i < blen; i++) {
        uint8_t c = blob[o + i];
        if (!c) { out[i < cap - 1 ? i : cap - 1] = 0; return 1; }
        if (i < cap - 1) out[i] = (char)c;
    }
    out[i < cap - 1 ? i : cap - 1] = 0;
    return 0;
}

/* type (+0x0F) -> N2_RACE_*. [data] agrees with the 0x34201 event-catalog flag word of each race's first heat for
 * 224/224 races: 0 = 0x71/0x2010 (circuit), 1 = 0x34 (sprint), 2 = 0x800 (Street X), 4 = 0x02 (drag), 5 = 0x08
 * (drift); 3 = the two scripted DDAY intro races (catalog flag 0x10) -> UNKNOWN. */
static int kind_from_type(unsigned t) {
    switch (t) {
    case 0: return N2_RACE_CIRCUIT;
    case 1: return N2_RACE_SPRINT;
    case 2: return N2_RACE_STREETX;
    case 4: return N2_RACE_DRAG;
    case 5: return N2_RACE_DRIFT;
    default: return N2_RACE_UNKNOWN;
    }
}

/* Fixed-size NUL-padded string field: copies up to the first NUL (or `n` bytes), truncating to cap-1. */
static void fixed_str(const uint8_t *p, size_t n, char *out, size_t cap) {
    size_t i;
    for (i = 0; i < n && i < cap - 1 && p[i]; i++) out[i] = (char)p[i];
    out[i] = 0;
}

/* 0x34A12: 160-byte shop records. Missing or malformed table -> nshop 0. */
static void parse_shops(CareerData *cd, const uint8_t *buf, size_t co, size_t cs) {
    size_t o, sz;
    unsigned i, n;
    cd->nshop = 0;
    if (!find_chunk(buf, co, co + cs, CD_ID_SHOPS, &o, &sz) || sz % CD_SHOP_BYTES) return;
    n = (unsigned)(sz / CD_SHOP_BYTES);
    if (n > CD_MAX_SHOPS) n = CD_MAX_SHOPS;
    for (i = 0; i < n; i++) {
        const uint8_t *r = buf + o + (size_t)i * CD_SHOP_BYTES;
        CdShop *d = &cd->shop[i];
        memset(d, 0, sizeof *d);
        fixed_str(r, 0x38, d->name, sizeof d->name);
        d->hash = rd32(r + 0x38); d->zone_hash = rd32(r + 0x3C);
        fixed_str(r + 0x40, 0x10, d->platform, sizeof d->platform);
        d->type = r[0x50]; d->known = r[0x9C]; d->open_stage = r[0x9D];
    }
    cd->nshop = (int)n;
}

/* 0x34A1F: {u32 car hash, u32 race A, u32 race B}. Missing or malformed table -> nunlock 0. */
static void parse_unlocks(CareerData *cd, const uint8_t *buf, size_t co, size_t cs) {
    size_t o, sz;
    unsigned i, n;
    cd->nunlock = 0;
    if (!find_chunk(buf, co, co + cs, CD_ID_UNLOCKS, &o, &sz) || sz % CD_UNLOCK_BYTES) return;
    n = (unsigned)(sz / CD_UNLOCK_BYTES);
    if (n > CD_MAX_UNLOCKS) n = CD_MAX_UNLOCKS;
    for (i = 0; i < n; i++) {
        const uint8_t *r = buf + o + (size_t)i * CD_UNLOCK_BYTES;
        cd->unlock[i].car_hash = rd32(r);
        cd->unlock[i].race[0] = rd32(r + 4);
        cd->unlock[i].race[1] = rd32(r + 8);
    }
    cd->nunlock = (int)n;
}

int cd_parse_globalb(CareerData *cd, const uint8_t *buf, size_t len) {
    size_t co, cs, ro, rs, so, ss, bo, bs;
    unsigned i, nr, ns;
    cd->nrace = 0; cd->nstage = 0; cd->nshop = 0; cd->nunlock = 0; cd->error[0] = 0;
    if (!buf) return cd_fail(cd, "null buffer");
    if (!find_chunk(buf, 0, len, CD_ID_CAREER, &co, &cs)) return cd_fail(cd, "no 0x80034A10 chunk");
    if (!find_chunk(buf, co, co + cs, CD_ID_STRINGS, &bo, &bs)) return cd_fail(cd, "no 0x34A1D string blob");
    if (!find_chunk(buf, co, co + cs, CD_ID_RACES, &ro, &rs)) return cd_fail(cd, "no 0x34A11 races");
    if (!find_chunk(buf, co, co + cs, CD_ID_STAGES, &so, &ss)) return cd_fail(cd, "no 0x34A18 stages");
    nr = (unsigned)(rs / CD_RACE_BYTES); ns = (unsigned)(ss / CD_STAGE_BYTES);
    if (!nr || rs % CD_RACE_BYTES) return cd_fail(cd, "bad race table size");
    if (!ns || ss % CD_STAGE_BYTES) return cd_fail(cd, "bad stage table size");
    if (nr > CD_MAX_RACES) return cd_fail(cd, "too many races");
    if (ns > CD_MAX_STAGES) return cd_fail(cd, "too many stages");
    for (i = 0; i < nr; i++) {
        const uint8_t *r = buf + ro + (size_t)i * CD_RACE_BYTES;
        CdRace *d = &cd->race[i];
        unsigned k;
        memset(d, 0, sizeof *d);
        memcpy(d->raw, r, CD_RACE_BYTES);
        if (!blob_str(buf + bo, bs, rd16(r + 0), d->name, sizeof d->name)) return cd_fail(cd, "bad race name offset");
        if (!blob_str(buf + bo, bs, rd16(r + 6), d->locator, sizeof d->locator)) return cd_fail(cd, "bad locator offset");
        d->name_hash = rd32(r + 0x08);
        d->unlock_mode = r[0x0C]; d->suv = r[0x0D]; d->type = r[0x0F];
        d->unlock_arg = rd32(r + 0x10);
        d->rep_base = rd32(r + 0x14);
        for (k = 0; k < 4; k++) {
            d->heat[k].track = rd16(r + 0x18 + 4 * k);
            d->heat[k].dir = r[0x1A + 4 * k];
            d->heat[k].laps = r[0x1B + 4 * k];
            if (d->heat[k].track) d->nheat++;
        }
        d->locator_hash = rd32(r + 0x28);
        d->preset_hash = rd32(r + 0x2C);
        d->shop_hash = rd32(r + 0x3C);
        d->purse = rd32(r + 0x30);
        d->category = r[0x34]; d->stage = r[0x37];
        d->nai = r[0x7C]; d->hidden = r[0x7F];
        d->kind = kind_from_type(d->type);
    }
    for (i = 0; i < ns; i++) {
        const uint8_t *r = buf + so + (size_t)i * CD_STAGE_BYTES;
        CdStage *d = &cd->stage[i];
        unsigned k;
        memset(d, 0, sizeof *d);
        memcpy(d->raw, r, CD_STAGE_BYTES);
        d->index = r[0];
        d->nsponsors = r[1] > 5 ? 5 : r[1];
        for (k = 0; k < d->nsponsors; k++) d->sponsor[k] = rd32(r + 0x08 + 4 * k);
        d->req_url = r[0x2E]; d->req_dvd = r[0x2F];
        d->req_sponsor = r[0x2D];   /* [inferred] 3 in every stage */
        d->final_race = rd32(r + 0x28);
        d->req_total = r[0x42];
    }
    cd->nrace = (int)nr; cd->nstage = (int)ns;
    parse_shops(cd, buf, co, cs);
    parse_unlocks(cd, buf, co, cs);
    return (int)nr;
}

static int finite_f(float f) { return f == f && fabsf(f) < 1e9f; }

/* The 0x3414A payload is a stream of mixed variable-size records, so locator zones are found structurally:
 * bbox f32[4], u32 hash(!=0), 3 words, u16 n=4, u16 100, 4 x (x,y), with the bbox equal to the point extent and
 * at most CD_RACE_ZONE_MAX wide ([data]: 8x8 m for every race locator), or CD_SHOP_ZONE_MAX for a shop zone
 * ([data] 15..21.1 m). 4-byte aligned scan. When races or shops are already loaded only zones whose hash is a race
 * locator or a shop zone are kept (the file also holds ~170 non-career zones). */
int cd_parse_zones(CareerData *cd, const uint8_t *buf, size_t len) {
    size_t co, cs, p;
    int n = 0, j, filter = cd->nrace > 0 || cd->nshop > 0;
    cd->nzone = 0; cd->error[0] = 0;
    if (!buf) return cd_fail(cd, "null buffer");
    if (!find_chunk(buf, 0, len, CD_ID_ZONES, &co, &cs)) {
        /* chunk is nested inside a 0x80034147 container */
        size_t xo, xs;
        if (!find_chunk(buf, 0, len, 0x80034147u, &xo, &xs) || !find_chunk(buf, xo, xo + xs, CD_ID_ZONES, &co, &cs))
            return cd_fail(cd, "no 0x3414A chunk");
    }
    for (p = co; cs >= 68 && p + 68 <= co + cs; p += 4) {
        const uint8_t *r = buf + p;
        float bb[4], pt[8], x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        uint32_t h = rd32(r + 16);
        int k, dup = 0, is_shop = 0, is_race = 0;
        if (!h || rd16(r + 32) != 4 || rd16(r + 34) != 100) continue;
        for (k = 0; k < 4; k++) bb[k] = rdf(r + 4 * k);
        for (k = 0; k < 8; k++) pt[k] = rdf(r + 36 + 4 * k);
        for (k = 0; k < 4; k++) if (!finite_f(bb[k])) break;
        if (k < 4) continue;
        for (k = 0; k < 8; k++) if (!finite_f(pt[k])) break;
        if (k < 8) continue;
        for (k = 0; k < 4; k++) {
            if (pt[2 * k] < x0) x0 = pt[2 * k];
            if (pt[2 * k] > x1) x1 = pt[2 * k];
            if (pt[2 * k + 1] < y0) y0 = pt[2 * k + 1];
            if (pt[2 * k + 1] > y1) y1 = pt[2 * k + 1];
        }
        if (fabsf(x0 - bb[0]) > 0.01f || fabsf(y0 - bb[1]) > 0.01f || fabsf(x1 - bb[2]) > 0.01f ||
            fabsf(y1 - bb[3]) > 0.01f || x1 - x0 > CD_SHOP_ZONE_MAX || y1 - y0 > CD_SHOP_ZONE_MAX) continue;
        for (j = 0; j < cd->nshop; j++) if (cd->shop[j].zone_hash == h) { is_shop = 1; break; }
        for (j = 0; j < cd->nrace; j++) if (cd->race[j].locator_hash == h) { is_race = 1; break; }
        if (!is_shop && (x1 - x0 > CD_RACE_ZONE_MAX || y1 - y0 > CD_RACE_ZONE_MAX)) continue;
        if (filter && !is_race && !is_shop) continue;
        for (j = 0; j < n; j++) if (cd->zone[j].hash == h) { dup = 1; break; }
        if (dup) continue;
        if (n >= CD_MAX_ZONES) break;
        cd->zone[n].hash = h;
        cd->zone[n].cx = (bb[0] + bb[2]) * 0.5f;
        cd->zone[n].cy = (bb[1] + bb[3]) * 0.5f;
        for (k = 0; k < 4; k++) { cd->zone[n].poly[k][0] = pt[2 * k]; cd->zone[n].poly[k][1] = pt[2 * k + 1]; }
        n++;
    }
    cd->nzone = n;
    if (!n) return cd_fail(cd, "no zones found");
    return n;
}

/* 0x34146: 8-byte filler then 48-byte records [+8 hash, +12 slot, +16 xyz, +32 u16 heading in 1/65536 turn].
 * Only slot 0 of each name is kept (multi-slot names are start grids). heading is in radians. */
int cd_parse_markers(CareerData *cd, const uint8_t *buf, size_t len) {
    size_t co, cs, p;
    int n = 0, j;
    cd->nmarker = 0; cd->error[0] = 0;
    if (!buf) return cd_fail(cd, "null buffer");
    if (!find_chunk(buf, 0, len, CD_ID_MARKERS, &co, &cs)) return cd_fail(cd, "no 0x34146 chunk");
    for (p = co + 8; cs >= 8 && p + 48 <= co + cs; p += 48) {
        const uint8_t *r = buf + p;
        uint32_t h = rd32(r + 8);
        float x = rdf(r + 16), y = rdf(r + 20), z = rdf(r + 24);
        if (rd32(r + 12) != 0) continue;
        if (!finite_f(x) || !finite_f(y) || !finite_f(z)) continue;
        for (j = 0; j < n; j++) if (cd->marker[j].hash == h) break;
        if (j < n) continue;
        if (n >= CD_MAX_MARKERS) break;
        cd->marker[n].hash = h; cd->marker[n].x = x; cd->marker[n].y = y; cd->marker[n].z = z;
        cd->marker[n].heading = (float)rd16(r + 32) * (6.2831853f / 65536.0f);
        n++;
    }
    cd->nmarker = n;
    if (!n) return cd_fail(cd, "no markers found");
    return n;
}

static uint8_t *read_file(const char *root, const char *rel, size_t *len) {
    char path[1024];
    FILE *f;
    long sz;
    uint8_t *b;
    if (snprintf(path, sizeof path, "%s/%s", root, rel) >= (int)sizeof path) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (sz = ftell(f)) <= 0 || (unsigned long)sz > CD_MAX_FILE || fseek(f, 0, SEEK_SET)) {
        fclose(f); return NULL;
    }
    b = (uint8_t *)malloc((size_t)sz);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)sz;
    return b;
}

int cd_load(CareerData *cd, const char *dataroot) {
    static const char *const F[3] = { "GLOBAL/GLOBALB.BUN", "TRACKS/ROUTESL4RA/PathsFreeRoam.bin",
                                      "TRACKS/ROUTESL4RA/TrackPosMarkersFreeRoam.bin" };
    uint8_t *b;
    size_t len = 0;
    int ok;
    memset(cd, 0, sizeof *cd);
    if (!dataroot) return cd_fail(cd, "null dataroot");
    if (!(b = read_file(dataroot, F[0], &len))) return cd_fail(cd, "cannot read GLOBAL/GLOBALB.BUN");
    ok = cd_parse_globalb(cd, b, len); free(b);
    if (!ok) return 0;
    if (!(b = read_file(dataroot, F[1], &len))) return cd_fail(cd, "cannot read PathsFreeRoam.bin");
    ok = cd_parse_zones(cd, b, len); free(b);
    if (!ok) return 0;
    if ((b = read_file(dataroot, F[2], &len))) {   /* markers are optional for the load result */
        cd_parse_markers(cd, b, len); free(b);
        cd->error[0] = 0;
    }
    return 1;
}

int cd_find(const CareerData *cd, uint32_t h) {
    int i;
    for (i = 0; i < cd->nrace; i++) if (cd->race[i].name_hash == h) return i;
    return -1;
}
int cd_find_zone(const CareerData *cd, uint32_t h) {
    int i;
    for (i = 0; i < cd->nzone; i++) if (cd->zone[i].hash == h) return i;
    return -1;
}
int cd_find_marker(const CareerData *cd, uint32_t h) {
    int i;
    for (i = 0; i < cd->nmarker; i++) if (cd->marker[i].hash == h) return i;
    return -1;
}
int cd_find_shop(const CareerData *cd, uint32_t h) {
    int i;
    for (i = 0; i < cd->nshop; i++) if (cd->shop[i].hash == h) return i;
    return -1;
}
uint32_t cd_hash(const char *s) {
    uint32_t h = 0xFFFFFFFFu;
    if (!s) return h;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        h = h * 33u + c;
    }
    return h;
}
