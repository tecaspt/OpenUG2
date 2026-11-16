/* career.c -- career state, rules and save (OpenUG2 M6-S1). See career.h. Pure C99 + stdio. */
#include "career.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

#define CR_BUF_MAX 16384u
#define CR_B_MAX   (8u + 24u + 8u + 4u * CD_MAX_RACES * 2u + 8u + 32u + 8u + 16u + 8u + 4u + 32u * CR_MAX_OWNED + 4096u)

/* ---- lookups (static; no dependency on career_data.c) ---- */
static int find_race(const CareerData *cd, uint32_t hash)
{
    int i;
    for (i = 0; i < cd->nrace; i++) if (cd->race[i].name_hash == hash) return i;
    return -1;
}
static int find_zone(const CareerData *cd, uint32_t hash)
{
    int i;
    for (i = 0; i < cd->nzone; i++) if (cd->zone[i].hash == hash) return i;
    return -1;
}
static int find_rec(const CareerState *s, uint32_t hash)
{
    int i;
    for (i = 0; i < s->nrec; i++) if (s->rec[i].hash == hash) return i;
    return -1;
}
static const CdStage *find_stage(const CareerData *cd, int index)
{
    int i;
    for (i = 0; i < cd->nstage; i++) if (cd->stage[i].index == index) return &cd->stage[i];
    return NULL;
}
static int find_shop(const CareerData *cd, uint32_t hash)
{
    int i;
    for (i = 0; i < cd->nshop; i++) if (cd->shop[i].hash == hash) return i;
    return -1;
}
/* ug_hash of the upper-cased name (same as cd_hash; kept local like the lookups above) */
static uint32_t name_hash(const char *s)
{
    uint32_t h = 0xFFFFFFFFu;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        h = h * 33u + c;
    }
    return h;
}
static int race_done(const CareerState *s, uint32_t hash)
{
    int k = find_rec(s, hash);
    return k >= 0 && (s->rec[k].flags & CR_REC_WON);
}
static void set_name(char dst[32], const char *src)
{
    size_t i;
    for (i = 0; i < 31 && src && src[i]; i++) dst[i] = src[i];
    for (; i < 32; i++) dst[i] = 0;
}

/* ---- [inferred] rules, one small function each ---- */
CrReward cr_reward_placeholder(void *user, const CdRace *race, const CrResult *res, int first_win)
{
    CrReward r = {0, 0};
    (void)user;
    if (race && res && first_win && res->place == 1 && !res->failed && !res->aborted) {
        r.cash = (int32_t)race->purse;
        r.rep = (int32_t)race->rep_base;
    }
    return r;
}
static int rule_is_win(const CrResult *r) { return r->place == 1 && !r->failed && !r->aborted; }
/* [inferred] which categories count toward stage completion: everything except the intro/scripted category. */
static int rule_counts_for_stage(const CdRace *r) { return r->category != CD_CAT_INTRO; }
/* [inferred] visibility of a won race on the map. */
static int rule_hidden_by_win(const CrRaceRec *rec) { return CR_HIDE_WON && rec && (rec->flags & CR_REC_WON); }

/* first wins in `stage`, optionally restricted to a category (cat < 0 = all counted categories) */
static int count_wins(const CareerState *s, const CareerData *cd, int stage, int cat)
{
    int i, n = 0;
    for (i = 0; i < s->nrec; i++) {
        int ri;
        const CdRace *r;
        if (!(s->rec[i].flags & CR_REC_WON)) continue;
        ri = find_race(cd, s->rec[i].hash);
        if (ri < 0) continue;
        r = &cd->race[ri];
        if (r->stage != stage || !rule_counts_for_stage(r)) continue;
        if (cat >= 0 && r->category != cat) continue;
        n++;
    }
    return n;
}
int cr_stage_wins(const CareerState *s, const CareerData *cd, int stage) { return count_wins(s, cd, stage, -1); }

int cr_stage_complete(const CareerState *s, const CareerData *cd)
{
    const CdStage *st = find_stage(cd, s->stage);
    if (!st) return 0;
    if (st->final_race) {   /* [inferred] story stage: done when its final scripted event is done */
        int f = find_race(cd, st->final_race);
        if (f >= 0 && cd->race[f].type == CD_TYPE_SCRIPTED) return race_done(s, st->final_race);
    }
    if (count_wins(s, cd, s->stage, -1) < st->req_total) return 0;
    if (count_wins(s, cd, s->stage, CD_CAT_URL) < st->req_url) return 0;
    if (st->nsponsors != 0 && count_wins(s, cd, s->stage, CD_CAT_SPONSOR) < st->req_sponsor) return 0;
    return 1; /* DVD covers not tracked in S1: met */
}

void cr_new(CareerState *s, const CareerData *cd, const char *car)
{
    (void)cd;
    memset(s, 0, sizeof *s);
    s->started = 1; s->stage = 1; s->intro_done = 1;
    s->region = CR_REGION_UNKNOWN;
    if (car) { strncpy(s->car, car, sizeof s->car - 1); s->car[sizeof s->car - 1] = 0; }
    if (s->car[0]) { set_name(s->owned[0], s->car); s->nowned = 1; }
}

void cr_new_stage0(CareerState *s, const CareerData *cd, int region)
{
    (void)cd;
    memset(s, 0, sizeof *s);
    s->started = 1; s->stage = 0; s->intro_done = 0;
    s->region = (region == 0 || region == 1) ? (uint8_t)region : (uint8_t)CR_REGION_UNKNOWN;
}

static int installed(const char *const *inst, int n, uint32_t hash)
{
    int i;
    for (i = 0; i < n; i++) if (inst && inst[i] && name_hash(inst[i]) == hash) return i;
    return -1;
}

int cr_region_pick(const CareerData *cd, const char *const *inst, int n)
{
    int ok[2], col, i;
    for (col = 0; col < 2; col++) {
        ok[col] = cd->nunlock > 0;
        for (i = 0; i < cd->nunlock; i++)
            if (cd->unlock[i].race[col] && installed(inst, n, cd->unlock[i].car_hash) < 0) ok[col] = 0;
    }
    return (ok[1] && !ok[0]) ? 1 : 0;   /* tie -> A */
}

int cr_available(const CareerState *s, const CareerData *cd, int race)
{
    const CdRace *r;
    int ri;
    if (race < 0 || race >= cd->nrace) return 0;
    r = &cd->race[race];
    if (r->stage != s->stage || r->suv || r->hidden || r->type == CD_TYPE_SCRIPTED) return 0;
    ri = find_rec(s, r->name_hash);
    if (rule_hidden_by_win(ri >= 0 ? &s->rec[ri] : NULL)) return 0;
    switch (r->unlock_mode) {
    case CD_UNLOCK_STAGE:     return 1;
    case CD_UNLOCK_SCRIPTED:  return 1;   /* [inferred] stage already matched above */
    case CD_UNLOCK_URL:       return count_wins(s, cd, s->stage, -1) >= (int)((r->unlock_arg >> 16) & 0xFF);
    case CD_UNLOCK_URL_FINAL: return count_wins(s, cd, s->stage, CD_CAT_URL) >= (int)((r->unlock_arg >> 24) & 0xFF);
    default:                  return 0;
    }
}

int cr_list(const CareerState *s, const CareerData *cd, int *out, int cap)
{
    int i, n = 0;
    for (i = 0; i < cd->nrace && n < cap; i++) if (cr_available(s, cd, i)) out[n++] = i;
    return n;
}

static int point_in_quad(const float p[4][2], float x, float y)
{
    int i, j, in = 0;
    for (i = 0, j = 3; i < 4; j = i++) {
        float xi = p[i][0], yi = p[i][1], xj = p[j][0], yj = p[j][1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

int cr_zone_hit(const CareerState *s, const CareerData *cd, float x, float y)
{
    int i;
    for (i = 0; i < cd->nrace; i++) {
        int z;
        if (!cr_available(s, cd, i)) continue;
        z = find_zone(cd, cd->race[i].locator_hash);
        if (z >= 0 && point_in_quad(cd->zone[z].poly, x, y)) return i;
    }
    return -1;
}

static void advance(CareerState *s, const CareerData *cd)
{
    int guard;
    for (guard = 0; guard < CD_MAX_STAGES + 1; guard++) {
        if (!cr_stage_complete(s, cd) || !find_stage(cd, s->stage + 1)) break;
        s->stage++;
    }
}

int cr_apply(CareerState *s, const CareerData *cd, const CrResult *r, CrRewardFn fn, void *user, CrOutcome *out)
{
    int ri = find_race(cd, r->race_hash), k, win, first;
    CrRaceRec *rec;
    CrOutcome o;
    memset(&o, 0, sizeof o);
    if (ri < 0) return 0;
    k = find_rec(s, r->race_hash);
    if (k < 0) {
        if (s->nrec >= CD_MAX_RACES) return 0;
        k = s->nrec++;
        memset(&s->rec[k], 0, sizeof s->rec[k]);
        s->rec[k].hash = r->race_hash;
    }
    rec = &s->rec[k];
    o.stage_before = s->stage;
    if (rec->attempts < 65535u) rec->attempts++;
    s->entered++;
    win = rule_is_win(r);
    first = win && !(rec->flags & CR_REC_WON);
    if (!r->failed && !r->aborted && r->place >= 1) {
        uint8_t pl = (uint8_t)(r->place > 255 ? 255 : r->place);
        if (rec->best_place == 0 || pl < rec->best_place) rec->best_place = pl;
    }
    if (win) { rec->flags |= CR_REC_WON; if (first) s->won++; }
    o.first_win = first;
    if (first && !(rec->flags & CR_REC_PAID)) {
        CrReward rw = (fn ? fn : cr_reward_placeholder)(user, &cd->race[ri], r, 1);
        s->bank += rw.cash;
        if (rw.rep >= 0) s->rep += (uint32_t)rw.rep;
        else s->rep = ((uint32_t)-rw.rep > s->rep) ? 0u : s->rep - (uint32_t)-rw.rep;
        rec->flags |= CR_REC_PAID;
        o.reward = rw;
    }
    advance(s, cd);
    o.stage_after = s->stage;
    if (out) *out = o;
    return 1;
}

/* ---- story objectives and shops ---- */
int cr_objective(const CareerState *s, const CareerData *cd)
{
    int i;
    for (i = 0; i < cd->nrace; i++) {
        const CdRace *r = &cd->race[i];
        if (r->stage != s->stage || r->type != CD_TYPE_SCRIPTED || race_done(s, r->name_hash)) continue;
        if (r->unlock_arg && find_race(cd, r->unlock_arg) >= 0 && !race_done(s, r->unlock_arg)) continue;
        return i;
    }
    return -1;
}

static int mark_done(CareerState *s, uint32_t hash)
{
    int k = find_rec(s, hash);
    if (k < 0) {
        if (s->nrec >= CD_MAX_RACES) return 0;
        k = s->nrec++;
        memset(&s->rec[k], 0, sizeof s->rec[k]);
        s->rec[k].hash = hash;
    }
    s->rec[k].flags |= CR_REC_WON | CR_REC_PAID;
    s->rec[k].best_place = 1;
    if (s->rec[k].attempts < 65535u) s->rec[k].attempts++;
    return 1;
}

int cr_objective_done(CareerState *s, const CareerData *cd, int shop, CrOutcome *out)
{
    int r = cr_objective(s, cd);
    CrOutcome o;
    memset(&o, 0, sizeof o);
    o.stage_before = o.stage_after = s->stage;
    if (r < 0) { if (out) *out = o; return 0; }
    if (shop >= 0 && (shop >= cd->nshop || cd->race[r].shop_hash != cd->shop[shop].hash)) { if (out) *out = o; return 0; }
    if (!mark_done(s, cd->race[r].name_hash)) { if (out) *out = o; return 0; }
    if (cd->race[r].stage == 0) s->intro_done = cr_objective(s, cd) < 0;
    advance(s, cd);
    o.first_win = 1; o.stage_after = s->stage;
    if (out) *out = o;
    return 1;
}

/* [inferred] M6 scope: only car lots and the crib (Garage hub) have a working screen. */
static int rule_shop_works(const CdShop *p) { return p->type == CD_SHOP_CAR_LOT || p->type == CD_SHOP_CRIB; }

int cr_shop_open(const CareerState *s, const CareerData *cd, int shop)
{
    if (shop < 0 || shop >= cd->nshop) return 0;
    return cd->shop[shop].open_stage <= s->stage && rule_shop_works(&cd->shop[shop]);
}

int cr_shop_hit(const CareerState *s, const CareerData *cd, float x, float y)
{
    int i;
    for (i = 0; i < cd->nshop; i++) {
        int z;
        if (!cr_shop_open(s, cd, i)) continue;
        z = find_zone(cd, cd->shop[i].zone_hash);
        if (z >= 0 && point_in_quad(cd->zone[z].poly, x, y)) return i;
    }
    return -1;
}

/* ---- car lot ---- */
int32_t cr_rule_car_price(const CareerData *cd, const char *car) { (void)cd; (void)car; return 0; }   /* [inferred] */

int cr_owns(const CareerState *s, const char *car)
{
    int i;
    if (!car || !car[0]) return 0;
    for (i = 0; i < s->nowned && i < CR_MAX_OWNED; i++) if (!strcmp(s->owned[i], car)) return 1;
    return 0;
}

static int lot_region(const CareerState *s, const CareerData *cd, const char *const *inst, int n)
{
    return s->region < 2 ? s->region : cr_region_pick(cd, inst, n);
}
/* 1 starter, 2 unlocked by a won race, 0 locked */
static int car_status(const CareerState *s, const CareerData *cd, int col, const char *car)
{
    uint32_t h = name_hash(car);
    int i;
    for (i = 0; i < cd->nunlock; i++) {
        uint32_t race = cd->unlock[i].race[col];
        if (cd->unlock[i].car_hash != h || !race) continue;
        return race_done(s, race) ? 2 : 0;
    }
    return 1;
}

int cr_lot_list(const CareerState *s, const CareerData *cd, const char *const *inst, int n, CrLotCar *out, int cap)
{
    int col = lot_region(s, cd, inst, n), i, k = 0;
    if (!inst || !out) return 0;
    for (i = 0; i < n && k < cap; i++) {
        int st;
        if (!inst[i] || !inst[i][0]) continue;
        st = car_status(s, cd, col, inst[i]);
        if (!st) continue;
        memset(&out[k], 0, sizeof out[k]);
        set_name(out[k].name, inst[i]);
        out[k].price = cr_rule_car_price(cd, inst[i]);
        out[k].owned = (uint8_t)cr_owns(s, out[k].name);
        out[k].starter = st == 1;
        k++;
    }
    return k;
}

int cr_lot_buy(CareerState *s, const CareerData *cd, const char *const *inst, int n, const char *car, CrOutcome *out)
{
    int col, ob;
    int32_t price;
    CrOutcome o;
    if (!s || !cd || !car || !car[0] || strlen(car) > 31 || installed(inst, n, name_hash(car)) < 0) return CR_LOT_E_ARG;
    col = lot_region(s, cd, inst, n);
    if (!car_status(s, cd, col, car)) return CR_LOT_E_LOCKED;
    if (!strcmp(s->car, car) && cr_owns(s, car)) return CR_LOT_E_SAME;
    price = cr_rule_car_price(cd, car);
    if (price > 0 && s->bank < price) return CR_LOT_E_FUNDS;
    s->bank -= price;
    memset(s->owned, 0, sizeof s->owned);
    set_name(s->owned[0], car); s->nowned = 1; s->active = 0;   /* Defaults: 1 car slot */
    set_name(s->car, car);
    if (s->region >= 2) s->region = (uint8_t)col;
    memset(&o, 0, sizeof o);
    o.stage_before = o.stage_after = s->stage;
    ob = cr_objective(s, cd);
    if (ob >= 0) {   /* a story event resolved by a car lot (Stage 0: DDAY_EVENT_A) */
        int sh = find_shop(cd, cd->race[ob].shop_hash);
        if (sh >= 0 && cd->shop[sh].type == CD_SHOP_CAR_LOT) cr_objective_done(s, cd, sh, &o);
    }
    if (out) *out = o;
    return CR_LOT_OK;
}

/* ---- little-endian packing ---- */
static void p16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, v); p16(p + 2, v >> 16); }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t g16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return g16(p) | (g16(p + 2) << 16); }
static uint64_t g64(const uint8_t *p) { return (uint64_t)g32(p) | ((uint64_t)g32(p + 4) << 32); }
static void pf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); p32(p, u); }
static float gf(const uint8_t *p) { uint32_t u = g32(p); float f; memcpy(&f, &u, 4); return f; }
static uint8_t *tlv(uint8_t *p, unsigned tag, uint32_t len) { p16(p, tag); p16(p + 2, 0); p32(p + 4, len); return p + 8; }

size_t cr_serialize(const CareerState *s, uint8_t *out, size_t cap)
{
    uint8_t a[8], b[CR_B_MAX], *p = b;
    size_t carlen, n;
    int i;
    if (s->nrec < 0 || s->nrec > CD_MAX_RACES || s->nextra > sizeof s->extra || s->nowned > CR_MAX_OWNED) return 0;
    p32(a, CR_MAGIC); p32(a + 4, CR_SCHEMA);
    p = tlv(p, 1, 24);
    p[0] = s->started; p[1] = s->stage; p[2] = s->intro_done; p[3] = 0;
    p64(p + 4, (uint64_t)s->bank); p32(p + 12, s->rep); p32(p + 16, s->entered); p32(p + 20, s->won);
    p += 24;
    p = tlv(p, 2, 8u * (uint32_t)s->nrec);
    for (i = 0; i < s->nrec; i++, p += 8) {
        p32(p, s->rec[i].hash); p[4] = s->rec[i].best_place; p[5] = s->rec[i].flags; p16(p + 6, s->rec[i].attempts);
    }
    for (carlen = 0; carlen < 31 && s->car[carlen]; carlen++) {}
    p = tlv(p, 3, (uint32_t)carlen);
    memcpy(p, s->car, carlen); p += carlen;
    p = tlv(p, 4, 16);
    for (i = 0; i < 4; i++) pf(p + 4 * i, s->pose[i]);
    p += 16;
    p = tlv(p, 5, 4u + 32u * s->nowned);
    p[0] = s->nowned; p[1] = s->active; p[2] = s->region; p[3] = 0;
    p += 4;
    for (i = 0; i < s->nowned; i++, p += 32) {
        size_t l;
        for (l = 0; l < 31 && s->owned[i][l]; l++) {}
        memset(p, 0, 32); memcpy(p, s->owned[i], l);
    }
    memcpy(p, s->extra, s->nextra); p += s->nextra;
    n = st_container_build(out, cap, a, 8, b, (uint32_t)(p - b));
    return n;
}

int cr_parse(CareerState *dst, const uint8_t *buf, size_t len)
{
    StContainerView v;
    CareerState t;
    uint32_t schema;
    size_t pos = 0;
    int have_owned = 0;
    int rc = st_container_parse(buf, len, &v);
    if (rc != ST_OK) return ST_E_CORRUPT;
    if (v.size_a < 8 || g32(v.a) != CR_MAGIC) return ST_E_CORRUPT;
    schema = g32(v.a + 4);
    if (schema == 0) return ST_E_CORRUPT;
    if (schema > CR_SCHEMA) return ST_E_INCOMPATIBLE;
    memset(&t, 0, sizeof t);
    while (pos < v.size_b) {
        unsigned tag; uint32_t l; const uint8_t *d;
        if (v.size_b - pos < 8) return ST_E_CORRUPT;
        tag = g16(v.b + pos); l = g32(v.b + pos + 4);
        if (l > v.size_b - pos - 8) return ST_E_CORRUPT;
        d = v.b + pos + 8;
        switch (tag) {
        case 1:
            if (l < 24) return ST_E_CORRUPT;
            t.started = d[0]; t.stage = d[1]; t.intro_done = d[2];
            t.bank = (int64_t)g64(d + 4); t.rep = g32(d + 12); t.entered = g32(d + 16); t.won = g32(d + 20);
            break;
        case 2: {
            uint32_t i, n;
            if (l % 8 != 0 || l / 8 > CD_MAX_RACES) return ST_E_CORRUPT;
            n = l / 8;
            for (i = 0; i < n; i++) {
                const uint8_t *q = d + 8 * i;
                t.rec[i].hash = g32(q); t.rec[i].best_place = q[4]; t.rec[i].flags = q[5];
                t.rec[i].attempts = (uint16_t)g16(q + 6);
            }
            t.nrec = (int)n;
            break; }
        case 3:
            if (l > 31) return ST_E_CORRUPT;
            memset(t.car, 0, sizeof t.car); memcpy(t.car, d, l);
            break;
        case 4: {
            int i;
            if (l < 16) return ST_E_CORRUPT;
            for (i = 0; i < 4; i++) t.pose[i] = gf(d + 4 * i);
            break; }
        case 5:
            if (schema < 2) break;   /* not defined in schema 1: dropped, rebuilt by the migration below */
            {
                uint32_t i, n;
                if (l < 4 || d[0] > CR_MAX_OWNED || l != 4u + 32u * d[0]) return ST_E_CORRUPT;
                n = d[0];
                if (n ? d[1] >= n : d[1] != 0) return ST_E_CORRUPT;
                t.nowned = (uint8_t)n; t.active = d[1]; t.region = d[2];
                for (i = 0; i < n; i++) { memcpy(t.owned[i], d + 4 + 32 * i, 31); t.owned[i][31] = 0; }
                have_owned = 1;
            }
            break;
        default:
            if (8u + l > sizeof t.extra - t.nextra) return ST_E_CORRUPT;
            memcpy(t.extra + t.nextra, v.b + pos, 8u + l);
            t.nextra += 8u + l;
            break;
        }
        pos += 8u + l;
    }
    if (!have_owned) {   /* schema 1 migration: the one car driven so far is the one owned */
        t.region = CR_REGION_UNKNOWN; t.active = 0; t.nowned = 0;
        if (t.car[0]) { set_name(t.owned[0], t.car); t.nowned = 1; }
    }
    *dst = t;
    return ST_OK;
}

/* ---- files ---- */
static int cr_path(const char *root, const char *profile, const char *suffix, char *out, size_t cap)
{
    int n;
    if (!root || !profile || !st_profile_name_valid(profile)) return ST_E_ARG;
    n = snprintf(out, cap, "%s/%s/%s%s", root, profile, CR_FILE_NAME, suffix);
    return (n < 0 || (size_t)n >= cap) ? ST_E_ARG : ST_OK;
}
static int write_atomic(const char *path, const char *tmp, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(tmp, "wb");
    int ok;
    if (!f) return ST_E_NOTFOUND;
    ok = fwrite(buf, 1, n, f) == n;
    if (fflush(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return ST_E_IO; }
    if (rename(tmp, path) != 0) {
        remove(path);                       /* Windows rename does not replace */
        if (rename(tmp, path) != 0) { remove(tmp); return ST_E_IO; }
    }
    return ST_OK;
}
static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ST_E_NOTFOUND;
    *n = fread(buf, 1, cap, f);
    if (ferror(f)) { fclose(f); return ST_E_IO; }
    if (*n == cap && fgetc(f) != EOF) { fclose(f); return ST_E_CORRUPT; }   /* too large */
    fclose(f);
    return ST_OK;
}

int cr_save_file(const char *root, const char *profile, const CareerState *s)
{
    char path[1024], tmp[1032];
    uint8_t buf[CR_BUF_MAX];
    size_t n;
    int rc = cr_path(root, profile, "", path, sizeof path);
    if (rc == ST_OK) rc = cr_path(root, profile, ".tmp", tmp, sizeof tmp);
    if (rc != ST_OK) return rc;
    n = cr_serialize(s, buf, sizeof buf);
    if (n == 0) return ST_E_ARG;
    return write_atomic(path, tmp, buf, n);
}

int cr_load_file(const char *root, const char *profile, CareerState *s)
{
    char path[1024];
    uint8_t buf[CR_BUF_MAX];
    size_t n = 0;
    int rc = cr_path(root, profile, "", path, sizeof path);
    if (rc != ST_OK) return rc;
    rc = read_file(path, buf, sizeof buf, &n);
    if (rc != ST_OK) return rc;
    return cr_parse(s, buf, n);
}

int cr_copy_file(const char *root, const char *from, const char *to)
{
    char src[1024], dst[1024], tmp[1032];
    uint8_t buf[CR_BUF_MAX];
    size_t n = 0;
    int rc = cr_path(root, from, "", src, sizeof src);
    if (rc == ST_OK) rc = cr_path(root, to, "", dst, sizeof dst);
    if (rc == ST_OK) rc = cr_path(root, to, ".tmp", tmp, sizeof tmp);
    if (rc != ST_OK) return rc;
    rc = read_file(src, buf, sizeof buf, &n);
    if (rc != ST_OK) return rc;
    return write_atomic(dst, tmp, buf, n);
}
