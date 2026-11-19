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
#define CD_ID_SMS      0x00034A17u
#define CD_ID_PERF     0x00034A15u
#define CD_ID_BRANDS   0x00034A14u
#define CD_ID_ULK      0x00034A1Cu
#define CD_ID_PARTSDB  0x80034602u
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

/* 0x34A17: the career message table, 20-byte records (retail: 167). Missing or malformed table -> nsms 0, never an
 * error, like the shop and unlock tables. A record whose name offset is out of range keeps its raw bytes and other
 * fields with an empty name, so the array index stays equal to the record index in the table. docs/parity/SMS-TABLE.md
 * holds the evidence for every field name. */
static void parse_sms(CareerData *cd, const uint8_t *buf, size_t co, size_t cs, size_t bo, size_t bs) {
    size_t o, sz;
    unsigned i, n;
    cd->nsms = 0;
    if (!find_chunk(buf, co, co + cs, CD_ID_SMS, &o, &sz) || sz % CD_SMS_BYTES) return;
    n = (unsigned)(sz / CD_SMS_BYTES);
    if (n > CD_MAX_SMS) n = CD_MAX_SMS;
    for (i = 0; i < n; i++) {
        const uint8_t *r = buf + o + (size_t)i * CD_SMS_BYTES;
        CdSms *d = &cd->sms[i];
        memset(d, 0, sizeof *d);
        memcpy(d->raw, r, CD_SMS_BYTES);
        blob_str(buf + bo, bs, rd16(r), d->name, sizeof d->name);   /* empty on a bad offset, on purpose */
        d->name_hash = cd_hash(d->name);
        d->trigger = r[0x02]; d->cls = r[0x03]; d->flag = r[0x04]; d->kind = r[0x05];
        d->arg = rd32(r + 0x08);
        d->weight = rd16(r + 0x0C);
        d->sender = rd32(r + 0x10);
    }
    cd->nsms = (int)n;
}


/* 0x34A19: 20 x 16-byte sponsor contracts; see SPONSORS.md. */
static void parse_sponsors(CareerData *cd, const uint8_t *buf, size_t co, size_t cs, size_t bo, size_t bs) {
    size_t o, sz;
    cd->nsponsor = 0;
    if (!find_chunk(buf, co, co + cs, 0x34A19u, &o, &sz) || sz % CD_SPONSOR_BYTES ||
        sz / CD_SPONSOR_BYTES > CD_MAX_SPONSORS) return;
    for (size_t i = 0; i < sz / CD_SPONSOR_BYTES; i++) {
        const uint8_t *r = buf + o + i * CD_SPONSOR_BYTES;
        CdSponsor *d = &cd->sponsor[cd->nsponsor++];
        memset(d, 0, sizeof *d); memcpy(d->raw, r, CD_SPONSOR_BYTES);
        blob_str(buf + bo, bs, rd16(r), d->name, sizeof d->name);
        d->url_bonus = rd16(r + 2); memcpy(d->race, r + 4, 3);
        d->hash = rd32(r + 8); d->signing = rd16(r + 12); d->reputation = rd16(r + 14);
    }
}

/* 0x34A15 performance packages, 0x34A14 brands, 0x34A1C unlock table. Missing or malformed -> counts 0. */
static void parse_perf(CareerData *cd, const uint8_t *buf, size_t co, size_t cs) {
    size_t o, sz;
    unsigned i, k, j, n;
    memset(cd->perf_comp, 0, sizeof cd->perf_comp); memset(cd->perf_pkg, 0, sizeof cd->perf_pkg);
    cd->nperf_comp = 0; cd->nbrand = 0; cd->nulk = 0;
    if (find_chunk(buf, co, co + cs, CD_ID_PERF, &o, &sz) && sz % CD_PERF_PKG_BYTES == 0) {
        n = (unsigned)(sz / CD_PERF_PKG_BYTES);
        for (i = 0; i < n; i++) {
            const uint8_t *r = buf + o + (size_t)i * CD_PERF_PKG_BYTES;
            int32_t cat = (int32_t)rd32(r), lvl = (int32_t)rd32(r + 4), cnt = (int32_t)rd32(r + 8);
            if (cat < 0 || cat >= CD_PERF_CATS || lvl < 1 || lvl > CD_PERF_LEVELS || cnt < 0 || cnt > 4) continue;
            CdPerfPkg *pk = &cd->perf_pkg[cat][lvl - 1];
            pk->n = 0;
            for (k = 0; k < (unsigned)cnt; k++) {
                const uint8_t *q = r + 12 + k * CD_PERF_DESC_BYTES;
                int32_t c = (int32_t)rd32(q);
                if (c < 0 || c >= CD_PERF_COMPS) continue;
                CdPerfComp *d = &cd->perf_comp[c];
                if (!d->valid) cd->nperf_comp++;
                d->valid = 1; d->cat = (uint8_t)cat; d->lvl = (uint8_t)lvl;
                d->name_key = rd32(q + 4); d->price = (int32_t)rd32(q + 8);
                d->nbrand = (uint8_t)(rd32(q + 12) > 8 ? 8 : rd32(q + 12));
                for (j = 0; j < 8; j++) d->brand[j] = rd32(q + 16 + 4 * j);
                for (j = 0; j < 4; j++) d->axis[j] = rdf(q + 48 + 4 * j);
                for (j = 0; j < 5; j++) { int32_t e = (int32_t)rd32(q + 64 + 4 * j); d->excl[j] = (int8_t)(e >= 0 && e < CD_PERF_COMPS ? e : -1); }
                pk->comp[pk->n++] = (int8_t)c;
            }
        }
    }
    if (find_chunk(buf, co, co + cs, CD_ID_BRANDS, &o, &sz) && sz % CD_BRAND_BYTES == 0) {
        n = (unsigned)(sz / CD_BRAND_BYTES); if (n > CD_MAX_BRANDS) n = CD_MAX_BRANDS;
        for (i = 0; i < n; i++) {
            const uint8_t *r = buf + o + (size_t)i * CD_BRAND_BYTES;
            CdBrand *d = &cd->brand[i];
            memcpy(d->key, r, 31); d->key[31] = 0;
            memcpy(d->display, r + 32, 31); d->display[31] = 0;
            d->hash = rd32(r + 64);
        }
        cd->nbrand = (int)n;
    }
    if (find_chunk(buf, co, co + cs, CD_ID_ULK, &o, &sz) && sz % CD_ULK_BYTES == 0) {
        n = (unsigned)(sz / CD_ULK_BYTES); if (n > CD_MAX_ULK) n = CD_MAX_ULK;
        for (i = 0; i < n; i++) {
            const uint8_t *r = buf + o + (size_t)i * CD_ULK_BYTES;
            CdUnlock *d = &cd->ulk[i];
            d->id = rd32(r);
            for (k = 0; k < 3; k++) {
                const uint8_t *q = r + 4 + 12 * k;
                d->l[k].a = rd16(q); d->l[k].b = rd16(q + 2);
                d->l[k].type = q[4]; d->l[k].level = q[5];
                d->l[k].value = rd32(q + 8);
            }
        }
        cd->nulk = (int)n;
    }
}

/* Paint colours from the parts database (see career_data.h). Missing or malformed -> npaint 0. */
static void parse_paint(CareerData *cd, const uint8_t *buf, size_t len) {
    static const char *grp[CD_PAINT_GROUPS] = { "GLOSS", "METAL", "PEARL", "RIM", "CALIPER", "MUFFLER", "HOSE", "VINYL" };
    size_t po, ps, so, ss, lo, ls, ao, as, co, cs;
    uint32_t ghash[CD_PAINT_GROUPS], kr = cd_hash("RED"), kg = cd_hash("GREEN"), kb = cd_hash("BLUE"), kgl = cd_hash("GLOSS");
    unsigned i, np, nl, na;
    cd->npaint = 0;
    if (!find_chunk(buf, 0, len, CD_ID_PARTSDB, &co, &cs)) return;
    if (!find_chunk(buf, co, co + cs, 0x00034604u, &po, &ps) || ps < 14) return;   /* [data] 12167 x 14 + 2 pad bytes */
    if (!find_chunk(buf, co, co + cs, 0x00034606u, &so, &ss)) return;
    if (!find_chunk(buf, co, co + cs, 0x0003460Cu, &lo, &ls) || ls < 2) return;
    if (!find_chunk(buf, co, co + cs, 0x00034605u, &ao, &as) || as < 8) return;
    for (i = 0; i < CD_PAINT_GROUPS; i++) ghash[i] = cd_hash(grp[i]);
    np = (unsigned)(ps / 14); nl = (unsigned)(ls / 2); na = (unsigned)(as / 8);
    for (i = 0; i < np && cd->npaint < CD_MAX_PAINT; i++) {
        const uint8_t *r = buf + po + (size_t)i * 14;
        unsigned list = rd16(r + 10), k, cnt, have = 0;
        uint32_t red = 0, green = 0, blue = 0, gloss = 0, group = 0;
        if (list == 0xFFFFu || list >= nl) continue;
        cnt = rd16(buf + lo + 2 * (size_t)list);
        if (list + cnt >= nl) continue;
        for (k = 0; k < cnt; k++) {
            unsigned ai = rd16(buf + lo + 2 * (size_t)(list + 1 + k));
            uint32_t key, val;
            if (ai >= na) break;
            key = rd32(buf + ao + 8 * (size_t)ai); val = rd32(buf + ao + 8 * (size_t)ai + 4);
            if (key == kr) { red = val; have |= 1; } else if (key == kg) { green = val; have |= 2; }
            else if (key == kb) { blue = val; have |= 4; } else if (key == kgl) gloss = val;
            else if (key == 0xEBB03E66u) { group = val; have |= 8; }
        }
        if (have != 15) continue;
        for (k = 0; k < CD_PAINT_GROUPS && ghash[k] != group; k++) {}
        if (k == CD_PAINT_GROUPS) continue;
        {
            CdPaint *d = &cd->paint[cd->npaint++];
            memset(d, 0, sizeof *d);
            d->key = rd32(r); d->group = (uint8_t)k; d->level = (uint8_t)(r[5] >> 5);
            d->rgb[0] = (uint8_t)(red > 255 ? 255 : red); d->rgb[1] = (uint8_t)(green > 255 ? 255 : green);
            d->rgb[2] = (uint8_t)(blue > 255 ? 255 : blue); d->gloss = (uint8_t)(gloss > 255 ? 255 : gloss);
            {
                size_t off = (size_t)rd16(r + 8) * 4;
                if (off < ss) {
                    const uint8_t *nm = buf + so + off;
                    size_t m = 0;
                    while (m < sizeof d->name - 1 && off + m < ss && nm[m]) { d->name[m] = (char)nm[m]; m++; }
                }
            }
        }
    }
}

int cd_find_paint(const CareerData *cd, uint32_t key) {
    for (int i = 0; i < cd->npaint; i++) if (cd->paint[i].key == key) return i;
    return -1;
}

int cd_find_ulk(const CareerData *cd, uint32_t id) {
    for (int i = 0; i < cd->nulk; i++) if (cd->ulk[i].id == id) return i;
    return -1;
}

int cd_parse_globalb(CareerData *cd, const uint8_t *buf, size_t len) {
    size_t co, cs, ro, rs, so, ss, bo, bs;
    unsigned i, nr, ns;
    cd->nrace = 0; cd->nstage = 0; cd->nshop = 0; cd->nunlock = 0; cd->nsms = 0; cd->error[0] = 0;
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
    parse_sms(cd, buf, co, cs, bo, bs);
    parse_sponsors(cd, buf, co, cs, bo, bs);
    parse_perf(cd, buf, co, cs);
    parse_paint(cd, buf, len);
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
int cd_find_sms(const CareerData *cd, uint32_t h) {
    int i;
    if (h == 0xFFFFFFFFu) return -1;          /* a record whose name failed to decode hashes as the empty name */
    for (i = 0; i < cd->nsms; i++) if (cd->sms[i].name_hash == h) return i;
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
