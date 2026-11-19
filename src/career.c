/* career.c -- career state, rules and save (OpenUG2 M6-S1). See career.h. Pure C99 + stdio. */
#include "career.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

#define CR_BUF_MAX 16384u
#define CR_B_MAX   (12u + 8u + 4u + 8u * CD_MAX_SMS + 8u + 24u + 8u + 4u * CD_MAX_RACES + 8u + 32u + 8u + 16u + \
                    8u + 4u + 32u * CR_MAX_OWNED + 8u + 88u * CR_MAX_OWNED + 8u + 48u * CR_MAX_OWNED + \
                    8u + 64u * CR_MAX_OWNED + 4096u)

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
    s->sms_ready = 1;
    s->started = 1; s->stage = 1; s->intro_done = 1;
    s->region = CR_REGION_UNKNOWN;
    if (car) { strncpy(s->car, car, sizeof s->car - 1); s->car[sizeof s->car - 1] = 0; }
    if (s->car[0]) { set_name(s->owned[0], s->car); s->nowned = 1; }
}

void cr_new_stage0(CareerState *s, const CareerData *cd, int region)
{
    (void)cd;
    memset(s, 0, sizeof *s);
    s->sms_ready = 1;
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
static int rule_shop_works(const CdShop *p) { return p->type <= CD_SHOP_UNIQUES; }

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

/* Geometry availability is separate from retail stage unlocks, which remain undecoded. */
int cr_shop_slot(int type, int slot)
{
    if (slot < 0 || slot > GARAGE_RIM) return 0;
    if (type == CD_SHOP_CRIB) return 1;
    if (type == CD_SHOP_SPECIALTY) return slot == N2_PART_ENGINE || slot == N2_PART_AUDIO;
    return type == CD_SHOP_BODY && slot != N2_PART_ENGINE && slot != N2_PART_AUDIO;
}
int32_t cr_rule_part_price(const CareerData *cd, int shop, int slot, int value)
{
    (void)cd; (void)shop; (void)slot; (void)value;
    return 0; /* [inferred] no decoded price table */
}
const GarageConfig *cr_car_config(const CareerState *s)
{
    return s && s->nowned && s->active < s->nowned ? &s->config[s->active] : NULL;
}
static int config_valid(const GarageConfig *c)
{
    if (!c || c->car.body_kit < 0 || c->car.body_kit > 99 ||
        c->car.hood_style < 0 || c->car.hood_style > 999 ||
        c->car.spoiler_style < 0 || c->car.spoiler_style > 999 ||
        c->car.wheel_style_id < 0 || c->car.wheel_style_id > 999 ||
        !memchr(c->rim_brand, 0, sizeof c->rim_brand) || c->rim_style < 0 || c->rim_style > 999) return 0;
    if (!c->rim_brand[0] && c->rim_style) return 0;
    for (const char *p = c->rim_brand; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    for (int i = 0; i < N2_PART_COUNT; i++) if (c->car.parts[i] < 0 || c->car.parts[i] > 5999) return 0;
    return 1;
}
int cr_shop_install(CareerState *s, const CareerData *cd, int shop, const GarageConfig *c)
{
    const GarageConfig *old = cr_car_config(s);
    if (!old || !config_valid(c) || !cr_shop_open(s, cd, shop)) return ST_E_ARG;
    int type = cd->shop[shop].type;
    if (!cr_shop_slot(type, GARAGE_KIT) && old->car.body_kit != c->car.body_kit) return ST_E_ARG;
    if (!cr_shop_slot(type, N2_PART_HOOD) && old->car.hood_style != c->car.hood_style) return ST_E_ARG;
    if (!cr_shop_slot(type, N2_PART_SPOILER) && old->car.spoiler_style != c->car.spoiler_style) return ST_E_ARG;
    if (!cr_shop_slot(type, GARAGE_RIM) && (old->car.wheel_style_id != c->car.wheel_style_id ||
        strcmp(old->rim_brand[0]?old->rim_brand:"NFSU", c->rim_brand[0]?c->rim_brand:"NFSU") ||
        (old->rim_brand[0]?old->rim_style:1) != (c->rim_brand[0]?c->rim_style:1))) return ST_E_ARG;
    for (int i = 0; i < N2_PART_COUNT; i++)
        if (!cr_shop_slot(type, i) && old->car.parts[i] != c->car.parts[i]) return ST_E_ARG;
    s->config[s->active] = *c;
    return ST_OK;
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
    set_name(s->owned[0], car); s->nowned = 1; s->active = 0; memset(s->config, 0, sizeof s->config); memset(s->perf, 0, sizeof s->perf); memset(s->paint, 0, sizeof s->paint);   /* Defaults: 1 car slot */
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
/* ---- performance shop ---- */
int cr_perf_unlock_id(int cat)
{
    static const int id[CD_PERF_CATS] = { 6, 5, 4, 1, 9, 3, 8, 2, 7, 10 };   /* [inferred] docs/parity/PERF-PAINT.md */
    return cat >= 0 && cat < CD_PERF_CATS ? id[cat] : -1;
}

int cr_unlock_ok(const CareerState *s, const CareerData *cd, uint32_t id, int lvl)
{
    int u = cd_find_ulk(cd, id), i;
    const CdUnlockLvl *l;
    if (u < 0 || lvl < 1 || lvl > 3) return 0;
    l = &cd->ulk[u].l[lvl - 1];
    switch (l->type) {
    case CD_ULK_START: return 1;
    case CD_ULK_SHOP:
        for (i = 0; i < cd->nshop; i++) if (cd->shop[i].hash == l->value) return cr_shop_open(s, cd, i);
        return 0;
    case CD_ULK_WINS: case CD_ULK_SPONSOR_WINS: case CD_ULK_URL_WINS: {
        int count = (int)(l->value & 0xFFFFu), stage = (int)(l->value >> 16);
        int cat = l->type == CD_ULK_WINS ? -1 : l->type == CD_ULK_SPONSOR_WINS ? CD_CAT_SPONSOR : CD_CAT_URL;
        if (s->stage > stage) return 1;
        return s->stage == stage && count_wins(s, cd, stage, cat) >= count; }
    default: return 0;
    }
}

int cr_perf_level_open(const CareerState *s, const CareerData *cd, int cat, int lvl)
{
    int id = cr_perf_unlock_id(cat);
    return id > 0 && lvl >= 1 && lvl <= CD_PERF_LEVELS && cd->perf_pkg[cat][lvl - 1].n > 0 && cr_unlock_ok(s, cd, (uint32_t)id, lvl);
}

int cr_perf_owned(const CareerState *s, int comp)
{
    if (comp < 0 || comp >= CD_PERF_COMPS || s->nowned == 0 || s->active >= s->nowned) return 0;
    return (s->perf[s->active][comp >> 5] >> (comp & 31)) & 1u;
}

static int pkg_has(const CdPerfPkg *p, int comp) { for (int i = 0; i < p->n; i++) if (p->comp[i] == comp) return 1; return 0; }

int32_t cr_perf_price(const CareerState *s, const CareerData *cd, int cat, int lvl, int comp)
{
    const CdPerfPkg *p;
    int64_t sum = 0;
    if (cat < 0 || cat >= CD_PERF_CATS || lvl < 1 || lvl > CD_PERF_LEVELS) return -1;
    p = &cd->perf_pkg[cat][lvl - 1];
    if (comp >= 0) return pkg_has(p, comp) ? cd->perf_comp[comp].price : -1;
    for (int i = 0; i < p->n; i++) if (!cr_perf_owned(s, p->comp[i])) sum += cd->perf_comp[p->comp[i]].price;
    return (int32_t)((sum * 9 + 5) / 10);   /* package discount 10% ("LABEL_DISCOUNT") [inferred formula] */
}

static int pkg_all_owned(const CareerState *s, const CdPerfPkg *p)
{
    for (int i = 0; i < p->n; i++) if (!cr_perf_owned(s, p->comp[i])) return 0;
    return 1;
}

static void perf_install(CareerState *s, const CareerData *cd, int comp)
{
    uint32_t *m = s->perf[s->active];
    for (int j = 0; j < 5; j++) {
        int e = cd->perf_comp[comp].excl[j];
        if (e >= 0) m[e >> 5] &= ~(1u << (e & 31));
    }
    m[comp >> 5] |= 1u << (comp & 31);
}

int cr_perf_buy(CareerState *s, const CareerData *cd, int shop, int cat, int lvl, int comp, int32_t *charged)
{
    const CdPerfPkg *p;
    int32_t price;
    if (charged) *charged = 0;
    if (!s || !cd || s->nowned == 0 || s->active >= s->nowned) return CR_PERF_E_ARG;
    if (shop < 0 || shop >= cd->nshop || cd->shop[shop].type != CD_SHOP_PERF || !cr_shop_open(s, cd, shop)) return CR_PERF_E_ARG;
    if (cat < 0 || cat >= CD_PERF_CATS || lvl < 1 || lvl > CD_PERF_LEVELS) return CR_PERF_E_ARG;
    p = &cd->perf_pkg[cat][lvl - 1];
    if (!p->n || (comp >= 0 && !pkg_has(p, comp))) return CR_PERF_E_ARG;
    if (!cr_perf_level_open(s, cd, cat, lvl)) return CR_PERF_E_LOCKED;
    if (comp >= 0 ? cr_perf_owned(s, comp) : pkg_all_owned(s, p)) return CR_PERF_E_OWNED;
    price = cr_perf_price(s, cd, cat, lvl, comp);
    if (price < 0) return CR_PERF_E_ARG;
    if (s->bank < price) return CR_PERF_E_FUNDS;
    s->bank -= price;
    if (comp >= 0) perf_install(s, cd, comp);
    else for (int i = 0; i < p->n; i++) perf_install(s, cd, p->comp[i]);
    if (charged) *charged = price;
    return CR_PERF_OK;
}

void cr_perf_fraction(const CareerState *s, const CareerData *cd, float u[CD_PERF_CATS][4])
{
    memset(u, 0, sizeof(float) * CD_PERF_CATS * 4);
    for (int c = 0; c < CD_PERF_COMPS; c++) {
        const CdPerfComp *k = &cd->perf_comp[c];
        if (!k->valid || !cr_perf_owned(s, c)) continue;
        for (int a = 0; a < 4; a++) if (k->axis[a] > 0.0f) u[k->cat][a] += k->axis[a];
    }
    for (int c = 0; c < CD_PERF_CATS; c++) for (int a = 0; a < 4; a++) if (u[c][a] > 1.0f) u[c][a] = 1.0f;
}

int cr_perf_level_of(float u) { return u >= 0.995f ? 3 : u >= 0.655f ? 2 : u >= 0.325f ? 1 : 0; }

/* ---- paint shop ---- */
int cr_paint_slot(int group)
{
    switch (group) {
    case CD_PAINT_GLOSS: case CD_PAINT_METAL: case CD_PAINT_PEARL: return 0;
    case CD_PAINT_RIM: return 1;
    case CD_PAINT_CALIPER: return 2;
    case CD_PAINT_MUFFLER: return 3;
    case CD_PAINT_HOSE: return 4;
    case CD_PAINT_VINYL: return 5;
    default: return -1;
    }
}

int cr_paint_unlock_id(int group)
{
    switch (group) {
    case CD_PAINT_RIM: return 77;
    case CD_PAINT_MUFFLER: return 78;
    case CD_PAINT_CALIPER: return 79;
    case CD_PAINT_GLOSS: case CD_PAINT_METAL: case CD_PAINT_PEARL: case CD_PAINT_HOSE: case CD_PAINT_VINYL: return 76;
    default: return -1;
    }
}

int cr_paint_open(const CareerState *s, const CareerData *cd, int paint)
{
    const CdPaint *p;
    int id;
    if (!s || !cd || paint < 0 || paint >= cd->npaint) return 0;
    p = &cd->paint[paint];
    id = cr_paint_unlock_id(p->group);
    return id > 0 && p->level >= 1 && p->level <= 3 && cr_unlock_ok(s, cd, (uint32_t)id, p->level);
}

int32_t cr_paint_price(const CareerData *cd, int paint)
{
    int u, id;
    if (!cd || paint < 0 || paint >= cd->npaint) return -1;
    id = cr_paint_unlock_id(cd->paint[paint].group);
    u = id > 0 ? cd_find_ulk(cd, (uint32_t)id) : -1;
    if (u < 0 || cd->paint[paint].level < 1 || cd->paint[paint].level > 3) return 0;
    {
        uint16_t b = cd->ulk[u].l[cd->paint[paint].level - 1].b;
        return b == 0xFFFFu ? 0 : (int32_t)b;
    }
}

uint32_t cr_paint_installed(const CareerState *s, int slot)
{
    if (!s || slot < 0 || slot >= CR_PAINT_SLOTS || s->nowned == 0 || s->active >= s->nowned) return 0;
    return s->paint[s->active][slot];
}

int cr_paint_buy(CareerState *s, const CareerData *cd, int shop, int paint, int32_t *charged)
{
    int slot;
    int32_t price;
    if (charged) *charged = 0;
    if (!s || !cd || s->nowned == 0 || s->active >= s->nowned || paint < 0 || paint >= cd->npaint) return CR_PERF_E_ARG;
    if (shop < 0 || shop >= cd->nshop || cd->shop[shop].type != CD_SHOP_PAINT || !cr_shop_open(s, cd, shop)) return CR_PERF_E_ARG;
    slot = cr_paint_slot(cd->paint[paint].group);
    if (slot < 0) return CR_PERF_E_ARG;
    if (s->paint[s->active][slot] == cd->paint[paint].key) return CR_PERF_E_OWNED;
    if (!cr_paint_open(s, cd, paint)) return CR_PERF_E_LOCKED;
    price = cr_paint_price(cd, paint);
    if (price < 0) return CR_PERF_E_ARG;
    if (s->bank < price) return CR_PERF_E_FUNDS;
    s->bank -= price;
    s->paint[s->active][slot] = cd->paint[paint].key;
    if (charged) *charged = price;
    return CR_PERF_OK;
}

/* ---- vinyl shop (graphics): docs/parity/VINYLS.md ----
 * Designs have no global id table (the list lives per car, inside CARS/<CAR>/VINYLS.BIN, and the texture keys do
 * not overlap between cars), so career state stores cd_hash of the upper-cased design token. The token rules are
 * [data] from the slot names of all 29 cars (census in docs/parity/VINYLS.md): FAMILY_NUMBER[SET][STYLE], car art
 * inserts the car folder name, sponsor art carries the sponsor name, the art factory styles are ART_<letter>_<n>. */
uint32_t cr_vinyl_hash(const char *token) { return cd_hash(token); }

const CrVinyl *cr_vinyl_layer(const CareerState *s, int layer)
{
    if (!s || layer < 0 || layer >= CR_VINYL_LAYERS || s->nowned == 0 || s->active >= s->nowned) return NULL;
    return &s->vinyl[s->active][layer];
}

/* Colour channels a design offers: art factory styles share one colour; every other family ships the three
 * colour layers A/B/C [data]. -1 when the name is not a design token at all. */
int cr_vinyl_design_colors(const char *design)
{
    char up[32];
    size_t n;
    if (!design || !design[0] || strlen(design) > 31) return -1;
    for (n = 0; design[n]; n++) {
        char c = design[n];
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return -1;
        up[n] = c;
    }
    up[n] = 0;
    /* the art factory styles ("ART_F_<n>" and the per-car "_ART_F_<n>" variants) ship one shared colour;
     * every other family - including the per-car hood art ("*_VINYL_HOOD_ART_A/B/C", where the colour
     * suffix proves A/B/C are colour layers) - ships the three colour channels [data]. */
    if (strstr(up, "_ART_F_") || !strncmp(up, "ART_F_", 6)) return 1;
    return CR_VINYL_COLORS;
}

int cr_vinyl_color_channel(const char *design, int ch)
{
    int layers = cr_vinyl_design_colors(design);
    if (layers < 0 || ch < 0) return -1;
    if (layers == 1) return ch == 0 ? 0 : -1;
    return ch < layers ? ch : -1;
}

/* Retail category of a design token (docs/parity/VINYLS.md). Returns CR_VINYL_CAT_* or -1 for names no retail
 * category admits (DEBUG, ...). The head token decides; sponsor/unique art is decided by its marker. */
int cr_vinyl_category(const char *design)
{
    static const struct { const char *key; int cat; } head[] = {
        {"FLAME", CR_VINYL_CAT_FLAMES}, {"FLAMES", CR_VINYL_CAT_FLAMES},
        {"STRIPE", CR_VINYL_CAT_STRIPE}, {"STRIPES", CR_VINYL_CAT_STRIPE},
        {"MODERN", CR_VINYL_CAT_MODERN}, {"MODERNS", CR_VINYL_CAT_MODERN},
        {"TEAR", CR_VINYL_CAT_TEAR}, {"TEARS", CR_VINYL_CAT_TEAR},
        {"LIGHTNG", CR_VINYL_CAT_LIGHTNING}, {"LIGHTNING", CR_VINYL_CAT_LIGHTNING},
        {"LIQUID", CR_VINYL_CAT_SPLASH}, {"LIQUIDS", CR_VINYL_CAT_SPLASH},
        {"TRIBAL", CR_VINYL_CAT_TRIBAL}, {"WILD", CR_VINYL_CAT_WILD},
        {"FLAG", CR_VINYL_CAT_FLAG}, {"FLAGS", CR_VINYL_CAT_FLAG},
        {"CONTEST", CR_VINYL_CAT_CONTEST}, {"ART", CR_VINYL_CAT_ARTFACTORY}, {"ARTS", CR_VINYL_CAT_ARTFACTORY},
        {"BODY", CR_VINYL_CAT_BODY}, {"UNIQUE", CR_VINYL_CAT_UNIQUE},
        {"TOP", CR_VINYL_CAT_TOP}, {"HOOD", CR_VINYL_CAT_HOODS},
        {"CHINGY", CR_VINYL_CAT_MANUFACTURER}, {"CAPONE", CR_VINYL_CAT_MANUFACTURER},
        {"KENWOOD", CR_VINYL_CAT_MANUFACTURER}, {"STANZA", CR_VINYL_CAT_MANUFACTURER},
        {"GIANELLE", CR_VINYL_CAT_MANUFACTURER}, {"SCORPION", CR_VINYL_CAT_MANUFACTURER},
        {"TOMMY", CR_VINYL_CAT_AFTERMARKET}, {"GUMUT", CR_VINYL_CAT_AFTERMARKET},
        {"BENSON", CR_VINYL_CAT_AFTERMARKET}, {"ROSSI", CR_VINYL_CAT_AFTERMARKET},
        {"NOSESUCKER", CR_VINYL_CAT_AFTERMARKET}
    };
    char tok[32], head_tok[32];
    size_t i = 0, n;
    if (!design || !design[0] || strlen(design) > 31) return -1;
    for (n = 0; design[n]; n++) tok[n] = (design[n] >= 'a' && design[n] <= 'z') ? (char)(design[n] - 32) : design[n];
    tok[n] = 0;
    /* sponsor-marked art ("AEM_" anywhere, or a bare _HOOD_ART suffix) is manufacturer art; art factory
     * designs are named "<CAR>_ART_F_<n>", so the ART token needs no leading position */
    if (strstr(tok, "ART_F_")) return CR_VINYL_CAT_ARTFACTORY;
    if (strstr(tok, "AEM_") || strstr(tok, "_HOOD_ART")) return CR_VINYL_CAT_MANUFACTURER;
    while (i < sizeof head_tok - 1 && tok[i] && tok[i] != '_') { head_tok[i] = tok[i]; i++; }
    head_tok[i] = 0;
    if (!i) return -1;
    /* aftermarket brands name their tops "TOP_<brand>_..." (TOP_NOSESUCKER_001): check the second token */
    if (!strcmp(head_tok, "TOP")) {
        char sub[32];
        size_t m = 0;
        for (n = 4; tok[n] && tok[n] != '_' && m < sizeof sub - 1; n++) sub[m++] = tok[n];
        sub[m] = 0;
        for (unsigned k = 0; k < sizeof head / sizeof head[0]; k++)
            if (sub[0] && !strcmp(sub, head[k].key)) return head[k].cat;
    }
    for (unsigned k = 0; k < sizeof head / sizeof head[0]; k++)
        if (!strcmp(head_tok, head[k].key)) return head[k].cat;
    return -1;
}

/* Which of the four graphics unlock gates (table 0x34A1C [data], ids 76/80/83/86) admits a design.
 * [inferred, docs/parity/VINYLS.md] gate 80 tier 1 = buyable categories, gate 83 tier 2 = the exclusive-art
 * branch (art factory, hoods/tops, unique, manufacturer). 76 admits buying vinyl colours (paint rule),
 * 85/86 gate a late-race branch with no matching category names. */
static int cr_vinyl_gate(const char *design)
{
    switch (cr_vinyl_category(design)) {
    case CR_VINYL_CAT_ARTFACTORY: case CR_VINYL_CAT_UNIQUE: case CR_VINYL_CAT_HOODS:
    case CR_VINYL_CAT_TOP: case CR_VINYL_CAT_MANUFACTURER:
        return 83;
    default:
        return 80;
    }
}

int cr_vinyl_open(const CareerState *s, const CareerData *cd, const char *design)
{
    int gate;
    if (!s || !cd || cr_vinyl_category(design) < 0) return 0;
    gate = cr_vinyl_gate(design);
    return cr_unlock_ok(s, cd, (uint32_t)gate, gate == 83 ? 2 : 1);
}

int cr_vinyl_shop_open(const CareerState *s, const CareerData *cd, int shop)
{
    if (!s || !cd || shop < 0 || shop >= cd->nshop) return 0;
    return cd->shop[shop].type == CD_SHOP_PAINT && cr_shop_open(s, cd, shop);
}

/* [inferred] one price per design: the `b` field of the gate that admits it - the same price source the paint
 * shop uses (cd_find_ulk + level `b`). Replacing a design charges the new one; recolouring or removing is free. */
int32_t cr_vinyl_price(const CareerState *s, const CareerData *cd, const char *design)
{
    int gate = cr_vinyl_gate(design), u;
    uint16_t b;
    if (!s || !cd || cr_vinyl_category(design) < 0) return -1;
    if (!cr_vinyl_open(s, cd, design)) return -1;
    u = cd_find_ulk(cd, (uint32_t)gate);
    if (u < 0) return 0;
    b = cd->ulk[u].l[gate == 83 ? 1 : 0].b;
    return b == 0xFFFFu ? 0 : (int32_t)b;
}

/* The layer's colours must be open VINYL-group colours of the design's channels; then funds, then install.
 * design NULL/"" removes the layer (free). Returns CR_PERF_* codes (OWNED = the layer already stores this). */
int cr_vinyl_buy(CareerState *s, const CareerData *cd, int shop, int layer, const char *design,
                 const uint32_t colour[CR_VINYL_COLORS], int32_t *charged)
{
    CrVinyl next;
    int32_t price;
    if (charged) *charged = 0;
    if (!s || !cd || !colour || layer < 0 || layer >= CR_VINYL_LAYERS) return CR_PERF_E_ARG;
    if (s->nowned == 0 || s->active >= s->nowned) return CR_PERF_E_ARG;
    if (!cr_vinyl_shop_open(s, cd, shop)) return CR_PERF_E_ARG;
    memset(&next, 0, sizeof next);
    if (design && design[0]) {
        int colors = cr_vinyl_design_colors(design);
        int idx;
        if (cr_vinyl_category(design) < 0 || colors < 0) return CR_PERF_E_ARG;
        /* an installed design carries its channel-0 colour (its own for artFactory, A otherwise); B/C may
         * only be set on designs that ship those channels [data: every design except artFactory ships A/B/C] */
        if (colour[0]) {
            idx = cd_find_paint(cd, colour[0]);
            if (idx < 0 || cd->paint[idx].group != CD_PAINT_VINYL || !cr_paint_open(s, cd, idx))
                return CR_PERF_E_ARG;
            next.colour[0] = colour[0];
        }
        for (int c = 1; c < CR_VINYL_COLORS; c++) {
            if (!colour[c]) continue;
            if (c >= colors) return CR_PERF_E_ARG;                 /* 1-colour designs reject B/C */
            idx = cd_find_paint(cd, colour[c]);
            if (idx < 0 || cd->paint[idx].group != CD_PAINT_VINYL || !cr_paint_open(s, cd, idx))
                return CR_PERF_E_ARG;
            next.colour[c] = colour[c];
        }
        if (!cr_vinyl_open(s, cd, design)) return CR_PERF_E_LOCKED;
        next.design = cr_vinyl_hash(design);
    } else if (colour[0] || colour[1] || colour[2]) return CR_PERF_E_ARG;
    if (!memcmp(&next, &s->vinyl[s->active][layer], sizeof next)) return CR_PERF_E_OWNED;
    price = next.design ? cr_vinyl_price(s, cd, design) : 0;
    if (next.design && s->vinyl[s->active][layer].design == next.design) price = 0;   /* recolour is free */
    if (price < 0) return CR_PERF_E_ARG;
    if (s->bank < price) return CR_PERF_E_FUNDS;
    s->bank -= price;
    s->vinyl[s->active][layer] = next;
    if (charged) *charged = price;
    return CR_PERF_OK;
}

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
    p = tlv(p, 6, 88u * s->nowned);
    for (i = 0; i < s->nowned; i++, p += 88) {
        const GarageConfig *c = &s->config[i];
        if (!config_valid(c)) return 0;
        p32(p, c->car.body_kit); p32(p + 4, c->car.hood_style);
        p32(p + 8, c->car.spoiler_style); p32(p + 12, c->car.wheel_style_id);
        for (int j = 0; j < N2_PART_COUNT; j++) p32(p + 16 + 4*j, c->car.parts[j]);
        memset(p + 60, 0, 24); memcpy(p + 60, c->rim_brand, strlen(c->rim_brand));
        p32(p + 84, c->rim_style);
    }
    p = tlv(p, 7, 48u * s->nowned);
    for (i = 0; i < s->nowned; i++, p += 48) {
        for (int j = 0; j < 3; j++) p32(p + 4*j, s->perf[i][j]);
        for (int j = 0; j < CR_PAINT_SLOTS; j++) p32(p + 12 + 4*j, s->paint[i][j]);
    }
    if (s->nsms > CD_MAX_SMS || s->sms_ready > 1) return 0;
    p = tlv(p, 8, 4u + 8u * s->nsms);
    p16(p, s->nsms); p[2] = s->sms_ready; p[3] = 0; p += 4;
    for (i = 0; i < s->nsms; i++, p += 8) {
        if (!s->sms[i].hash || s->sms[i].read > 1) return 0;
        for (int j = 0; j < i; j++) if (s->sms[j].hash == s->sms[i].hash) return 0;
        p32(p, s->sms[i].hash); p[4] = s->sms[i].read; memset(p + 5, 0, 3);
    }
    /* TLV 9 is omitted while no owned car carries a vinyl, keeping old-format blobs byte-identical
     * (the INV gate shots and every pre-vinyl save); it appears as soon as any layer is non-empty. */
    {
        int any = 0;
        for (i = 0; i < s->nowned && !any; i++)
            for (int l = 0; l < CR_VINYL_LAYERS && !any; l++)
                any = s->vinyl[i][l].design != 0;
        if (any) {
            p = tlv(p, 9, 16u * CR_VINYL_LAYERS * s->nowned);
            for (i = 0; i < s->nowned; i++)
                for (int l = 0; l < CR_VINYL_LAYERS; l++, p += 16) {
                    p32(p, s->vinyl[i][l].design);
                    for (int c = 0; c < CR_VINYL_COLORS; c++) p32(p + 4 + 4 * c, s->vinyl[i][l].colour[c]);
                }
        }
    }
    /* The unknown-TLV buffer is the only path whose length is not derived from a count that is
     * itself bounded above (nrec, nowned, nsms all are). nextra can only be non-zero in a state
     * that was cr_parse()d from a file a newer writer produced, but the bound is still checked
     * here, because exceeding it would walk `p` past b[] and st_container_build would be handed
     * a size computed from a pointer outside the buffer. */
    if (s->nextra > (uint32_t)(sizeof b - (p - b))) return 0;
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
    int have_sms = 0, have_owned = 0, config_count = -1, perf_count = -1, vinyl_count = -1;
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
        case 6:
            if (schema < 3) break;
            if (config_count >= 0 || l % 88 || l / 88 > CR_MAX_OWNED) return ST_E_CORRUPT;
            config_count = (int)(l / 88);
            for (int i = 0; i < config_count; i++) {
                const uint8_t *q = d + 88*i; GarageConfig *c = &t.config[i];
                c->car.body_kit = (int32_t)g32(q); c->car.hood_style = (int32_t)g32(q + 4);
                c->car.spoiler_style = (int32_t)g32(q + 8); c->car.wheel_style_id = (int32_t)g32(q + 12);
                for (int j = 0; j < N2_PART_COUNT; j++) c->car.parts[j] = (int32_t)g32(q + 16 + 4*j);
                memcpy(c->rim_brand, q + 60, 24); c->rim_style = (int32_t)g32(q + 84);
                if (!config_valid(c)) return ST_E_CORRUPT;
            }
            break;
        case 7:
            if (schema < 4) break;
            if (perf_count >= 0 || l % 48 || l / 48 > CR_MAX_OWNED) return ST_E_CORRUPT;
            perf_count = (int)(l / 48);
            for (int i = 0; i < perf_count; i++) {
                const uint8_t *q = d + 48*i;
                for (int j = 0; j < 3; j++) t.perf[i][j] = g32(q + 4*j);
                for (int j = 0; j < CR_PAINT_SLOTS; j++) t.paint[i][j] = g32(q + 12 + 4*j);
                if (t.perf[i][2] >> (CD_PERF_COMPS - 64)) return ST_E_CORRUPT;   /* bits past component 67 */
            }
            break;
        case 8:
            if (schema < 5) break;
            if (have_sms++ || l < 4 || g16(d) > CD_MAX_SMS ||
                l != 4u + 8u * g16(d) || d[2] > 1 || d[3]) return ST_E_CORRUPT;
            t.sms_ready = d[2];
            for (unsigned i = 0; i < g16(d); i++) {
                const uint8_t *q = d + 4 + 8*i;
                if (q[4] > 1 || q[5] || q[6] || q[7] || !cr_sms_receive(&t, g32(q), q[4])) return ST_E_CORRUPT;
            }
            break;
        case 9:
            /* Task AP: the per-car vinyl layers (16 bytes per layer, 4 layers per owned car). A missing TLV
             * (any writer before this change, or one with no vinyl installed) simply means no vinyls. */
            if (vinyl_count >= 0 || l % (16u * CR_VINYL_LAYERS) || l / (16u * CR_VINYL_LAYERS) > CR_MAX_OWNED)
                return ST_E_CORRUPT;
            vinyl_count = (int)(l / (16u * CR_VINYL_LAYERS));
            for (int i = 0; i < vinyl_count; i++)
                for (int j = 0; j < CR_VINYL_LAYERS; j++) {
                    const uint8_t *q = d + 16u * CR_VINYL_LAYERS * i + 16*j;
                    t.vinyl[i][j].design = g32(q);
                    for (int c = 0; c < CR_VINYL_COLORS; c++) t.vinyl[i][j].colour[c] = g32(q + 4 + 4*c);
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
    if (config_count >= 0 && config_count != t.nowned) return ST_E_CORRUPT;
    if (perf_count >= 0 && perf_count != t.nowned) return ST_E_CORRUPT;   /* schema < 4 / missing: all stock (zeroed) */
    if (vinyl_count >= 0 && vinyl_count != t.nowned) return ST_E_CORRUPT;
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


int cr_sms_receive(CareerState *s, uint32_t hash, int read)
{
    if (!s || !hash || s->nsms >= CD_MAX_SMS) return 0;
    for (int i = 0; i < s->nsms; i++) if (s->sms[i].hash == hash) return 0;
    s->sms[s->nsms].hash = hash; s->sms[s->nsms++].read = read != 0;
    return 1;
}
int cr_sms_read(CareerState *s, uint32_t hash)
{
    if (!s) return 0;
    for (int i = 0; i < s->nsms; i++) if (s->sms[i].hash == hash) { s->sms[i].read = 1; return 1; }
    return 0;
}
int cr_sms_unread(const CareerState *s)
{
    int n = 0;
    if (s) for (int i = 0; i < s->nsms; i++) n += !s->sms[i].read;
    return n;
}
/* [inferred] SMS trigger 12 = world wins, 10 = sponsor wins, 9 = URL wins.
 * Match the exact 0x34A1C type/value before consulting the common unlock rule. */
static int sms_condition(const CareerState *s, const CareerData *cd, int i, int shop, double elapsed, int migrate)
{
    const CdSms *m = &cd->sms[i];
    if (m->kind == CD_SMS_UNLOCK) {
        int type = m->trigger == 12 ? CD_ULK_WINS : m->trigger == 10 ? CD_ULK_SPONSOR_WINS :
                   m->trigger == 9 ? CD_ULK_URL_WINS : -1;
        for (int u = 0; u < cd->nulk; u++) for (int l = 0; l < 3; l++)
            if (cd->ulk[u].l[l].type == type && cd->ulk[u].l[l].value == m->arg &&
                cr_unlock_ok(s, cd, cd->ulk[u].id, l+1)) return 1;
        return 0;
    }
    if (!strncmp(m->name, "SMS_CAR_UNLOCK_", 15)) {
        /* [data] Cars reference URL races. [inferred] A notice's per-stage
         * ordinal enumerates distinct regional unlock races in numeric URL order;
         * multiple cars unlocked by one event share one notice. */
        int race[CD_MAX_UNLOCKS], ordinal[CD_MAX_UNLOCKS], n = 0;
        int stage = CD_SMS_ARG_HI(m->arg), which = CD_SMS_ARG_LO(m->arg);
        for (int u = 0; u < cd->nunlock && n < CD_MAX_UNLOCKS; u++) {
            int r = find_race(cd, cd->unlock[u].race[s->region == 1 ? 1 : 0]);
            int named_stage, number, duplicate = 0; char tail;
            if (r < 0 || cd->race[r].stage != stage ||
                sscanf(cd->race[r].name,"S%d_URL_%d%c",&named_stage,&number,&tail) != 2 ||
                named_stage != stage || number < 1) continue;
            for (int k = 0; k < n; k++) if (race[k] == r) duplicate = 1;
            if (!duplicate) {race[n] = r; ordinal[n++] = number;}
        }
        for (int j = 0; j < n; j++) for (int k = j+1; k < n; k++) if (ordinal[k] < ordinal[j]) {
            int tmp = ordinal[j]; ordinal[j] = ordinal[k]; ordinal[k] = tmp;
            tmp = race[j]; race[j] = race[k]; race[k] = tmp;
        }
        return which > 0 && which <= n && race_done(s,cd->race[race[which-1]].name_hash);
    }
    if (m->kind == CD_SMS_TIP) {
        int ordinal = 0;
        for (int k = 0; k < i; k++) if (cd->sms[k].kind == CD_SMS_TIP) ordinal++;
        return s->stage >= 1 && s->won >= (unsigned)ordinal;
    }
    if (m->trigger == 3 && m->kind == CD_SMS_MSG) return migrate || elapsed >= m->arg;
    if (!strcmp(m->name, "SMS_OUTRUN_INTRO")) return s->stage >= 1;
    if (!strcmp(m->name, "SMS_GPS_INTRO")) return s->nowned > 0;
    if (!strcmp(m->name, "SMS_LOC_GO_TO_CAR_LOT_NOW")) return !s->nowned;
    if (!strcmp(m->name, "SMS_LOC_GO_TO_GARAGE")) return s->nowned > 0;
    if (!strcmp(m->name, "SMS_LOC_EVENTS_OUT_THERE")) return s->stage >= 1 &&
        (migrate || (shop >= 0 && shop < cd->nshop && cd->shop[shop].type == CD_SHOP_CRIB));
    if (!strcmp(m->name, "SMS_END_OF_GAME_MESSAGE")) return race_done(s, m->arg);
    return 0;
}
int cr_sms_deliver(CareerState *s, const CareerData *cd, int shop, double elapsed, int migrate)
{
    int n = 0;
    if (!s || !cd || !s->started) return 0;
    for (int i = 0; i < cd->nsms; i++) if (sms_condition(s, cd, i, shop, elapsed, migrate))
        n += cr_sms_receive(s, cd->sms[i].name_hash, migrate);
    s->sms_ready = 1;
    return n;
}
