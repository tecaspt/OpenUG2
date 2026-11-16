/* career_host.c -- see career_host.h. */
#include "career_host.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Same legend mapping as hud_host_event_variant (hud_host_map.h). race.h is not included because its static
 * helpers trip -Wunused-function here; the kind values are N2_RACE_* (race.h): CIRCUIT 1, SPRINT 2, DRAG 3, DRIFT 4,
 * STREETX 5, URL 6. */
static unsigned char ch_variant(int kind)
{
    if (kind == 1) return 0;                 /* circuit */
    if (kind >= 2 && kind <= 6) return (unsigned char)kind;
    return 7;
}

static const GcEvent *ch_event(const CareerHost *ch, int id)
{
    if (!ch->cat) return NULL;
    for (int i = 0; i < ch->cat->nev; i++) if (ch->cat->ev[i].id == id) return &ch->cat->ev[i];
    return NULL;
}

/* Installed cars: the catalogue's non-traffic cars (read at call time so catalogue build order does not matter). */
static int inst_list(const CareerHost *ch, const char **p)
{
    int n = 0;
    if (!ch->cat) return 0;
    for (int i = 0; i < ch->cat->ncar && n < CH_MAX_INST; i++)
        if (!ch->cat->car[i].traffic && ch->cat->car[i].name[0]) p[n++] = ch->cat->car[i].name;
    return n;
}

/* [inferred] minimap legend variant of a shop type (hud_minimap.h HMI_SHOP 0 car lot .. 4 interiors) */
static unsigned char shop_variant(int type)
{
    switch (type) {
    case CD_SHOP_PAINT: return 1;
    case CD_SHOP_PERF: return 2;
    case CD_SHOP_BODY: return 3;
    case CD_SHOP_SPECIALTY: case CD_SHOP_UNIQUES: return 4;
    default: return 0;   /* car lot, crib */
    }
}

static void zone_centre(const CdZone *z, float *x, float *y)
{
    *x = 0.25f * (z->poly[0][0] + z->poly[1][0] + z->poly[2][0] + z->poly[3][0]);
    *y = 0.25f * (z->poly[0][1] + z->poly[1][1] + z->poly[2][1] + z->poly[3][1]);
}

int ch_init(CareerHost *ch, const char *dataroot, const GameCatalog *cat)
{
    if (!ch) return 0;
    memset(ch, 0, sizeof *ch);
    ch->race = -1; ch->cat = cat; ch->at_shop = -1;
    if (!dataroot || !cd_load(&ch->cd, dataroot)) {
        snprintf(ch->last, sizeof ch->last, "career data unavailable: %s", ch->cd.error[0] ? ch->cd.error : "no data root");
        return 0;
    }
    ch->loaded = 1;
    snprintf(ch->last, sizeof ch->last, "career data: %d races, %d stages, %d zones, %d shops, %d car unlocks",
             ch->cd.nrace, ch->cd.nstage, ch->cd.nzone, ch->cd.nshop, ch->cd.nunlock);
    return 1;
}

static int ch_save(CareerHost *ch)
{
    int rc = cr_save_file(ch->root, ch->profile, &ch->st);
    if (rc != ST_OK) {
        snprintf(ch->last, sizeof ch->last, "career: save failed (%d) for profile %s", rc, ch->profile);
        fprintf(stderr, "%s\n", ch->last);
    }
    return rc == ST_OK;
}

static void free_roam_launch(const char *car, GfLaunch *out)
{
    memset(out, 0, sizeof *out);
    out->free_run = 1;
    snprintf(out->track, sizeof out->track, "%s", CH_CITY_TRACK);
    snprintf(out->car, sizeof out->car, "%s", car ? car : "");
}

/* Car lot the player is at: at_shop when it is a car lot; else the lot that resolves the current objective (Stage 0:
 * CC_CAR_LOT_1); else the first open car lot. -1 when none. */
static int lot_shop(const CareerHost *ch)
{
    const CareerData *cd = &ch->cd;
    if (ch->at_shop >= 0 && ch->at_shop < cd->nshop && cd->shop[ch->at_shop].type == CD_SHOP_CAR_LOT) return ch->at_shop;
    int ob = cr_objective(&ch->st, cd);
    if (ob >= 0) {
        int sh = cd_find_shop(cd, cd->race[ob].shop_hash);
        if (sh >= 0 && cd->shop[sh].type == CD_SHOP_CAR_LOT) return sh;
    }
    for (int i = 0; i < cd->nshop; i++) if (cd->shop[i].type == CD_SHOP_CAR_LOT && cr_shop_open(&ch->st, cd, i)) return i;
    return -1;
}

static int crib_shop(const CareerHost *ch)
{
    const CareerData *cd = &ch->cd;
    if (ch->at_shop >= 0 && ch->at_shop < cd->nshop && cd->shop[ch->at_shop].type == CD_SHOP_CRIB) return ch->at_shop;
    for (int i = 0; i < cd->nshop; i++) if (cd->shop[i].type == CD_SHOP_CRIB) return i;
    return -1;
}

/* Pending spawn + saved pose at the shop's marker (falls back to its zone centre with z/heading 0). 1 when set. */
static int spawn_at_shop(CareerHost *ch, int shop)
{
    if (shop < 0 || shop >= ch->cd.nshop) return 0;
    int m = cd_find_marker(&ch->cd, ch->cd.shop[shop].hash);
    if (m >= 0) {
        const CdMarker *k = &ch->cd.marker[m];
        ch->spawn[0] = k->x; ch->spawn[1] = k->y; ch->spawn[2] = k->z; ch->spawn[3] = k->heading;
    } else {
        int z = cd_find_zone(&ch->cd, ch->cd.shop[shop].zone_hash);
        if (z < 0) return 0;
        zone_centre(&ch->cd.zone[z], &ch->spawn[0], &ch->spawn[1]);
        ch->spawn[2] = ch->spawn[3] = 0.0f;
    }
    memcpy(ch->st.pose, ch->spawn, sizeof ch->st.pose);
    ch->spawn_valid = 1;
    return 1;
}

int ch_begin(CareerHost *ch, int new_career, const char *root, const char *profile, const char *car, GfLaunch *out)
{
    if (!ch || !ch->loaded || !root || !profile || !out) return CH_BEGIN_FAIL;
    snprintf(ch->root, sizeof ch->root, "%s", root);
    snprintf(ch->profile, sizeof ch->profile, "%s", profile);
    ch->race = -1; ch->resolved = 0; ch->spawn_valid = 0; ch->at_shop = -1;
    int ret = CH_BEGIN_ROAM;
    if (new_career) {
        const char *inst[CH_MAX_INST];
        int n = inst_list(ch, inst);
        cr_new_stage0(&ch->st, &ch->cd, cr_region_pick(&ch->cd, inst, n));
        if (!ch_save(ch)) return CH_BEGIN_FAIL;
        ret = CH_BEGIN_LOT;
    } else {
        int rc = cr_load_file(root, profile, &ch->st);
        if (rc != ST_OK || !ch->st.started) {
            snprintf(ch->last, sizeof ch->last, "career: no saved career to continue (%d)", rc);
            return CH_BEGIN_FAIL;
        }
        if (ch->st.pose[0] != 0.0f || ch->st.pose[1] != 0.0f) {
            memcpy(ch->spawn, ch->st.pose, sizeof ch->spawn); ch->spawn_valid = 1;
        }
        if (ch->st.car[0]) car = ch->st.car;   /* [inferred] continue in the career car */
        if (ch->st.stage == 0 && ch->st.nowned == 0) ret = CH_BEGIN_LOT;   /* Stage 0 car not chosen yet */
    }
    ch->active = 1;
    free_roam_launch(car, out);
    if (ret == CH_BEGIN_LOT) {   /* the lot screen opens over the lot marker */
        ch->at_shop = lot_shop(ch);
        spawn_at_shop(ch, ch->at_shop);
    }
    int nav[CD_MAX_RACES];
    snprintf(ch->last, sizeof ch->last, "career: %s stage %d bank %lld rep %u, %d events available, region %c%s",
             new_career ? "new" : "continue", ch->st.stage, (long long)ch->st.bank, ch->st.rep,
             cr_list(&ch->st, &ch->cd, nav, CD_MAX_RACES), ch->st.region < 2 ? 'A' + ch->st.region : '?',
             ret == CH_BEGIN_LOT ? ", car lot" : "");
    return ret;
}

int ch_zone(const CareerHost *ch, float x, float y, float kmh)
{
    if (!ch || !ch->active || ch->race >= 0 || fabsf(kmh) > CH_ENGAGE_KMH) return -1;
    return cr_zone_hit(&ch->st, &ch->cd, x, y);
}

int ch_engage(CareerHost *ch, float x, float y, float heading, float kmh, const char *car, GfLaunch *out)
{
    int r = ch_zone(ch, x, y, kmh);
    if (r < 0 || !out) return -1;
    const CdRace *race = &ch->cd.race[r];
    if (race->nheat < 1) return -1;
    const GcEvent *ev = ch_event(ch, race->heat[0].track);
    if (!ev) {
        snprintf(ch->last, sizeof ch->last, "career: event %u of %s is not in the catalogue", race->heat[0].track, race->name);
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->event_id = ev->id;
    snprintf(out->track, sizeof out->track, "STREAM%s", ev->region);
    snprintf(out->car, sizeof out->car, "%s", car ? car : "");
    out->laps = race->heat[0].laps;   /* 0 lets the host keep the event default */
    out->difficulty = 1;              /* [inferred] career difficulty: medium */
    ch->st.pose[0] = x; ch->st.pose[1] = y; ch->st.pose[2] = 0.0f; ch->st.pose[3] = heading;
    if (car && car[0]) snprintf(ch->st.car, sizeof ch->st.car, "%s", car);
    ch->race = r; ch->resolved = 0;
    snprintf(ch->last, sizeof ch->last, "career: engage %s (event %d, %s, %d heat(s))",
             race->name, ev->id, out->track, race->nheat);
    return r;
}

int ch_on_result(CareerHost *ch, int finish_place, int ncars, int failed, int aborted, double seconds)
{
    if (!ch || !ch->active || ch->race < 0 || ch->resolved) return 0;
    CrResult res; memset(&res, 0, sizeof res);
    res.race_hash = ch->cd.race[ch->race].name_hash;
    res.place = finish_place; res.ncars = ncars; res.failed = failed; res.aborted = aborted; res.seconds = seconds;
    memset(&ch->outcome, 0, sizeof ch->outcome);
    if (!cr_apply(&ch->st, &ch->cd, &res, NULL, NULL, &ch->outcome)) return 0;
    ch->resolved = 1;
    ch_save(ch);
    snprintf(ch->last, sizeof ch->last, "career: %s place %d%s +%d cash +%d rep -> bank %lld rep %u stage %d%s",
             ch->cd.race[ch->race].name, finish_place, failed ? " (failed)" : aborted ? " (aborted)" : "",
             ch->outcome.reward.cash, ch->outcome.reward.rep, (long long)ch->st.bank, ch->st.rep, ch->st.stage,
             ch->outcome.stage_after != ch->outcome.stage_before ? " (stage complete)" : "");
    return 1;
}

int ch_return(CareerHost *ch, const char *car, GfLaunch *out)
{
    if (!ch || !ch->active || !out) return 0;
    if (ch->race >= 0 && !ch->resolved) ch_on_result(ch, 0, 0, 0, 1, 0.0);   /* left without a result: aborted */
    ch->race = -1;
    free_roam_launch(car, out);
    memcpy(ch->spawn, ch->st.pose, sizeof ch->spawn);
    ch->spawn_valid = ch->st.pose[0] != 0.0f || ch->st.pose[1] != 0.0f;
    return 1;
}

void ch_suspend(CareerHost *ch, float x, float y, float z, float heading)
{
    if (!ch || !ch->active) return;
    if (ch->race >= 0) {
        if (!ch->resolved) ch_on_result(ch, 0, 0, 0, 1, 0.0);
    } else {
        ch->st.pose[0] = x; ch->st.pose[1] = y; ch->st.pose[2] = z; ch->st.pose[3] = heading;
    }
    ch_save(ch);
    ch->active = 0; ch->race = -1; ch->spawn_valid = 0;
}

int ch_icons(const CareerHost *ch, HudMapIcon *out, int cap)
{
    if (!ch || !ch->active || ch->race >= 0 || !out || cap <= 0) return 0;
    int idx[CD_MAX_RACES], n = cr_list(&ch->st, &ch->cd, idx, CD_MAX_RACES), k = 0;
    for (int i = 0; i < n && k < cap; i++) {
        const CdRace *r = &ch->cd.race[idx[i]];
        int z = cd_find_zone(&ch->cd, r->locator_hash);
        if (z < 0) continue;
        HudMapIcon *o = &out[k++];
        zone_centre(&ch->cd.zone[z], &o->x, &o->y);
        o->heading = 0.0f; o->kind = HMI_EVENT; o->variant = ch_variant(r->kind);
    }
    for (int i = 0; i < ch->cd.nshop && k < cap; i++) {
        if (!cr_shop_open(&ch->st, &ch->cd, i)) continue;
        int z = cd_find_zone(&ch->cd, ch->cd.shop[i].zone_hash);
        if (z < 0) continue;
        HudMapIcon *o = &out[k++];
        zone_centre(&ch->cd.zone[z], &o->x, &o->y);
        o->heading = 0.0f; o->kind = HMI_SHOP; o->variant = shop_variant(ch->cd.shop[i].type);
    }
    return k;
}

const char *ch_prompt(const CareerHost *ch, float x, float y, float kmh, const char *label, char *buf, int cap)
{
    int r = ch_zone(ch, x, y, kmh);
    if (!buf || cap <= 0) return NULL;
    if (r < 0) {
        int sh = ch_shop_zone(ch, x, y, kmh);
        if (sh < 0) return NULL;
        snprintf(buf, (size_t)cap, "%s: %s", label && *label ? label : "Enter Shop", ch->cd.shop[sh].name);
        return buf;
    }
    const CdRace *race = &ch->cd.race[r];
    /* label: the host's retail "Enter Race / Shop" caption; the name/purse layout is [inferred] */
    snprintf(buf, (size_t)cap, "%s: %s  $%u", label && *label ? label : "Enter Race", race->name, race->purse);
    return buf;
}

int ch_take_spawn(CareerHost *ch, float spawn[4])
{
    if (!ch || !ch->spawn_valid || !spawn) return 0;
    memcpy(spawn, ch->spawn, sizeof ch->spawn);
    ch->spawn_valid = 0;
    return 1;
}

int ch_shop_zone(const CareerHost *ch, float x, float y, float kmh)
{
    if (!ch || !ch->active || ch->race >= 0 || fabsf(kmh) > CH_ENGAGE_KMH) return -1;
    return cr_shop_hit(&ch->st, &ch->cd, x, y);
}

int ch_lot_list(const CareerHost *ch, CrLotCar *out, int cap)
{
    if (!ch || !ch->loaded || !out || cap <= 0) return 0;
    const char *inst[CH_MAX_INST];
    int ni = inst_list(ch, inst);
    return cr_lot_list(&ch->st, &ch->cd, inst, ni, out, cap);
}

const char *ch_lot_line(const CrLotCar *c, int n, char *buf, int cap)
{
    if (!buf || cap <= 0) return NULL;
    int k = snprintf(buf, (size_t)cap, "career: lot %d cars:", n);
    for (int i = 0; c && i < n && k >= 0 && k < cap; i++)
        k += snprintf(buf + k, (size_t)(cap - k), " %s%s", c[i].name, c[i].owned ? "*" : "");
    return buf;
}

static const char *lot_err(int rc)
{
    switch (rc) {
    case CR_LOT_E_SAME: return "same car";
    case CR_LOT_E_LOCKED: return "locked";
    case CR_LOT_E_FUNDS: return "not enough money";
    default: return "not installed";
    }
}

int ch_lot_buy(CareerHost *ch, const char *car, GfLaunch *out)
{
    if (!ch || !ch->loaded || !ch->active || !out) return 0;
    const char *inst[CH_MAX_INST];
    int ni = inst_list(ch, inst), lot = lot_shop(ch), before = ch->st.stage;
    memset(&ch->outcome, 0, sizeof ch->outcome);
    ch->lot_rc = cr_lot_buy(&ch->st, &ch->cd, inst, ni, car, &ch->outcome);
    if (ch->lot_rc != CR_LOT_OK) {
        snprintf(ch->last, sizeof ch->last, "career: lot refused %s (%s)", car ? car : "(null)", lot_err(ch->lot_rc));
        return 0;
    }
    ch->at_shop = lot;
    free_roam_launch(ch->st.car, out);
    spawn_at_shop(ch, lot);
    ch_save(ch);
    snprintf(ch->last, sizeof ch->last, "career: bought %s for $%d at %s, bank %lld stage %d%s", ch->st.car,
             cr_rule_car_price(&ch->cd, ch->st.car), lot >= 0 ? ch->cd.shop[lot].name : "?", (long long)ch->st.bank,
             ch->st.stage, ch->outcome.first_win ? (before == 0 ? " (DDAY_A done)" : " (objective done)") : "");
    return 1;
}

int ch_crib_done(CareerHost *ch, GfLaunch *out)
{
    if (!ch || !ch->loaded || !ch->active || !out) return 0;
    int crib = crib_shop(ch), before = ch->st.stage;
    memset(&ch->outcome, 0, sizeof ch->outcome);
    if (crib >= 0) cr_objective_done(&ch->st, &ch->cd, crib, &ch->outcome);
    ch->at_shop = crib;
    free_roam_launch(ch->st.car, out);
    spawn_at_shop(ch, crib);
    ch_save(ch);
    if (ch->st.stage != before)
        snprintf(ch->last, sizeof ch->last, "career: stage %d -> %d", before, ch->st.stage);
    else
        snprintf(ch->last, sizeof ch->last, "career: garage left, stage %d", ch->st.stage);
    return 1;
}
