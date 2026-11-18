/* career_test.c -- synthetic unit test for career.c (no retail data). */
#include "career.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static CareerData cd;
enum { H_A = 0x100, H_B, H_C, H_D, H_E, H_F, H_G, H_H, H_I };
static void mk(int i, const char *name, uint32_t hash, int mode, int cat, int stage, uint32_t arg, uint32_t purse, uint32_t rep)
{
    CdRace *r = &cd.race[i];
    memset(r, 0, sizeof *r);
    strcpy(r->name, name); r->name_hash = hash; r->locator_hash = hash + 0x1000;
    r->unlock_mode = (uint8_t)mode; r->category = (uint8_t)cat; r->stage = (uint8_t)stage;
    r->unlock_arg = arg; r->purse = purse; r->rep_base = rep;
}
static void mkzone(int i, uint32_t hash, float x0, float y0, float x1, float y1)
{
    cd.zone[i].hash = hash;
    cd.zone[i].poly[0][0] = x0; cd.zone[i].poly[0][1] = y0; cd.zone[i].poly[1][0] = x1; cd.zone[i].poly[1][1] = y0;
    cd.zone[i].poly[2][0] = x1; cd.zone[i].poly[2][1] = y1; cd.zone[i].poly[3][0] = x0; cd.zone[i].poly[3][1] = y1;
}
static void build_data(void)
{
    memset(&cd, 0, sizeof cd);
    mk(0, "S1_A", H_A, CD_UNLOCK_STAGE,    CD_CAT_WORLD,   1, 0, 250, 10);
    mk(1, "S1_B", H_B, CD_UNLOCK_STAGE,    CD_CAT_WORLD,   1, 0, 300, 12);
    mk(2, "S1_C", H_C, CD_UNLOCK_SCRIPTED, CD_CAT_INTRO,   1, 0, 100, 5);
    mk(3, "S1_U", H_D, CD_UNLOCK_URL,      CD_CAT_URL,     1, 1u << 16, 500, 20);   /* needs 1 stage win */
    mk(4, "S1_SPON", H_E, CD_UNLOCK_SPONSOR, CD_CAT_SPONSOR, 1, 0, 400, 15);
    mk(5, "S1_SUV", H_F, CD_UNLOCK_STAGE,  CD_CAT_WORLD,   1, 0, 999, 99);
    cd.race[5].suv = 1;
    mk(6, "S1_HID", H_G, CD_UNLOCK_STAGE,  CD_CAT_WORLD,   1, 0, 999, 99);
    cd.race[6].hidden = 1;
    mk(7, "S2_A", H_H, CD_UNLOCK_STAGE,    CD_CAT_WORLD,   2, 0, 600, 30);
    mk(8, "S2_B", H_I, CD_UNLOCK_STAGE,    CD_CAT_WORLD,   2, 0, 700, 35);
    cd.nrace = 9;
    cd.stage[0].index = 1; cd.stage[0].req_total = 2; cd.stage[0].req_url = 0;
    cd.stage[1].index = 2; cd.stage[1].req_total = 2; cd.stage[1].req_url = 1; cd.stage[1].nsponsors = 0;
    cd.nstage = 2;
    mkzone(0, H_A + 0x1000, 0, 0, 10, 10);
    mkzone(1, H_B + 0x1000, 100, 100, 110, 110);
    mkzone(2, H_H + 0x1000, 200, 200, 210, 210);
    cd.nzone = 3;
}
static CrResult res(uint32_t h, int place, int failed, int aborted);
static int state_eq(const CareerState *a, const CareerState *b);

/* ---- M6 stage 0 / car lot data ---- */
enum { H_OPT = 0x200, H_DA, H_DB, H_S1, H_URL, H_LOT1 = 0x300, H_CRIB, H_LOT2, H_PERF };
static uint32_t nh(const char *s) { uint32_t h = 0xFFFFFFFFu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }
static void mkshop(int i, const char *name, uint32_t hash, int type, int stage)
{
    CdShop *p = &cd.shop[i];
    memset(p, 0, sizeof *p);
    strcpy(p->name, name); p->hash = hash; p->zone_hash = hash + 0x1000; p->type = (uint8_t)type; p->open_stage = (uint8_t)stage;
}
static void build_lot_data(void)
{
    memset(&cd, 0, sizeof cd);
    mk(0, "S0_OPT", H_OPT, CD_UNLOCK_SCRIPTED, CD_CAT_WORLD, 0, 0, 200, 4);
    mk(1, "DDAY_A", H_DA, CD_UNLOCK_SCRIPTED, CD_CAT_INTRO, 0, 0, 0, 0);
    cd.race[1].type = CD_TYPE_SCRIPTED; cd.race[1].shop_hash = H_LOT1; cd.race[1].preset_hash = 0x777;
    mk(2, "DDAY_B", H_DB, CD_UNLOCK_SCRIPTED, CD_CAT_INTRO, 0, H_DA, 0, 0);
    cd.race[2].type = CD_TYPE_SCRIPTED; cd.race[2].shop_hash = H_CRIB;
    mk(3, "S1_R", H_S1, CD_UNLOCK_SCRIPTED, CD_CAT_WORLD, 1, H_DB, 300, 5);
    mk(4, "S2_URL", H_URL, CD_UNLOCK_STAGE, CD_CAT_URL, 2, 0, 900, 9);
    cd.nrace = 5;
    cd.stage[0].index = 0; cd.stage[0].req_total = 1; cd.stage[0].final_race = H_DB;
    cd.stage[1].index = 1; cd.stage[1].req_total = 1;
    cd.stage[2].index = 2; cd.stage[2].req_total = 9;
    cd.nstage = 3;
    mkshop(0, "LOT1", H_LOT1, CD_SHOP_CAR_LOT, 0);
    mkshop(1, "CRIB", H_CRIB, CD_SHOP_CRIB, 0);
    mkshop(2, "LOT2", H_LOT2, CD_SHOP_CAR_LOT, 2);
    mkshop(3, "PERF", H_PERF, CD_SHOP_PERF, 0);
    cd.nshop = 4;
    mkzone(0, H_OPT + 0x1000, 0, 0, 10, 10);
    mkzone(1, H_LOT1 + 0x1000, 100, 0, 115, 15);
    mkzone(2, H_CRIB + 0x1000, 200, 0, 221, 21);
    mkzone(3, H_LOT2 + 0x1000, 300, 0, 315, 15);
    mkzone(4, H_PERF + 0x1000, 400, 0, 415, 15);
    cd.nzone = 5;
    cd.unlock[0].car_hash = nh("CAR_U"); cd.unlock[0].race[0] = H_URL;
    cd.unlock[1].car_hash = nh("CAR_V"); cd.unlock[1].race[1] = H_URL;
    cd.unlock[2].car_hash = nh("CAR_W"); cd.unlock[2].race[0] = H_URL; cd.unlock[2].race[1] = H_URL;
    cd.nunlock = 3;
}
static int lot_has(const CrLotCar *c, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(c[i].name, name)) return i;
    return -1;
}

static void test_lot(void)
{
    static CareerState s, t;
    static const char *const inst[] = { "STARTER1", "CAR_U", "CAR_V", "CAR_W" };
    static const char *const inst_b[] = { "CAR_V", "CAR_W" };
    static const char *const inst_none[] = { "STARTER1" };
    CrLotCar lot[16];
    CrOutcome o;
    int n, list[CD_MAX_RACES];
    CrResult r;
    build_lot_data();
    CHECK(cr_region_pick(&cd, inst, 4) == 0);        /* both columns complete: tie -> A */
    CHECK(cr_region_pick(&cd, inst_b, 2) == 1);      /* A lacks CAR_U -> B */
    CHECK(cr_region_pick(&cd, inst_none, 1) == 0);   /* neither -> A */
    CHECK(cr_region_pick(&cd, NULL, 0) == 0);
    cr_new_stage0(&s, &cd, 0);
    CHECK(s.started && s.stage == 0 && !s.intro_done && s.bank == 0 && s.nowned == 0 && s.region == 0 && !s.car[0]);
    cr_new_stage0(&t, &cd, 7);
    CHECK(t.region == CR_REGION_UNKNOWN);
    /* objectives and map */
    CHECK(cr_objective(&s, &cd) == 1);
    n = cr_list(&s, &cd, list, CD_MAX_RACES);
    CHECK(n == 1 && list[0] == 0);                   /* SCRIPTED optional race in stage 0; DDAY events never listed */
    CHECK(cr_shop_open(&s, &cd, 0) && cr_shop_open(&s, &cd, 1) && !cr_shop_open(&s, &cd, 2) && cr_shop_open(&s, &cd, 3));
    CHECK(!cr_shop_open(&s, &cd, -1) && !cr_shop_open(&s, &cd, 9));
    CHECK(cr_shop_hit(&s, &cd, 105, 5) == 0 && cr_shop_hit(&s, &cd, 220, 20) == 1);
    CHECK(cr_shop_hit(&s, &cd, 305, 5) == -1 && cr_shop_hit(&s, &cd, 405, 5) == 3 && cr_shop_hit(&s, &cd, 50, 50) == -1);
    /* lot inventory, region A */
    n = cr_lot_list(&s, &cd, inst, 4, lot, 16);
    CHECK(n == 2 && !strcmp(lot[0].name, "STARTER1") && !strcmp(lot[1].name, "CAR_V"));
    CHECK(lot[0].starter && lot[1].starter && !lot[0].owned && lot[0].price == 0 && cr_rule_car_price(&cd, "CAR_V") == 0);
    CHECK(cr_lot_list(&s, &cd, inst, 4, lot, 1) == 1);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "CAR_U", &o) == CR_LOT_E_LOCKED);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "NOPE", &o) == CR_LOT_E_ARG);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, NULL, &o) == CR_LOT_E_ARG);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "", &o) == CR_LOT_E_ARG);
    /* an optional stage-0 win must not end stage 0 (req_total 1 is not the stage-0 rule) */
    r = res(H_OPT, 1, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1 && s.stage == 0 && s.bank == 200 && !cr_stage_complete(&s, &cd));
    /* buy: DDAY_A resolved by the choice */
    memset(&o, 0x55, sizeof o);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "STARTER1", &o) == CR_LOT_OK);
    CHECK(o.first_win == 1 && o.stage_before == 0 && o.stage_after == 0 && o.reward.cash == 0);
    CHECK(!strcmp(s.car, "STARTER1") && s.nowned == 1 && !strcmp(s.owned[0], "STARTER1") && s.active == 0);
    CHECK(cr_owns(&s, "STARTER1") && !cr_owns(&s, "CAR_V") && !cr_owns(&s, NULL) && s.bank == 200);
    CHECK(cr_objective(&s, &cd) == 2 && s.stage == 0 && !s.intro_done);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "STARTER1", &o) == CR_LOT_E_SAME);
    n = cr_lot_list(&s, &cd, inst, 4, lot, 16);
    CHECK(n == 2 && lot[0].owned && !lot[1].owned);
    /* the crib resolves DDAY_B: stage 0 -> 1 only there */
    CHECK(cr_objective_done(&s, &cd, 0, &o) == 0 && s.stage == 0);   /* wrong shop */
    CHECK(cr_objective_done(&s, &cd, 1, &o) == 1);
    CHECK(o.first_win && o.stage_before == 0 && o.stage_after == 1 && s.stage == 1 && s.intro_done);
    CHECK(cr_objective(&s, &cd) == -1 && cr_objective_done(&s, &cd, -1, &o) == 0 && o.stage_after == 1);
    CHECK(s.won == 1 && s.entered == 1);             /* story events are not counted as race wins */
    n = cr_list(&s, &cd, list, CD_MAX_RACES);
    CHECK(n == 1 && list[0] == 3);
    /* second car: one slot, replaced */
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "CAR_V", &o) == CR_LOT_OK && o.first_win == 0 && o.stage_after == 1);
    CHECK(s.nowned == 1 && !strcmp(s.owned[0], "CAR_V") && !strcmp(s.car, "CAR_V"));
    /* unlock race won -> CAR_U and CAR_W on sale (region A) */
    s.stage = 2;
    r = res(H_URL, 1, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    n = cr_lot_list(&s, &cd, inst, 4, lot, 16);
    CHECK(n == 4 && lot_has(lot, n, "CAR_U") >= 0 && !lot[lot_has(lot, n, "CAR_U")].starter);
    CHECK(cr_lot_buy(&s, &cd, inst, 4, "CAR_W", &o) == CR_LOT_OK && !strcmp(s.car, "CAR_W"));
    CHECK(cr_shop_open(&s, &cd, 2));
    /* region B: CAR_V locked, CAR_U a starter */
    cr_new_stage0(&t, &cd, 1);
    n = cr_lot_list(&t, &cd, inst, 4, lot, 16);
    CHECK(n == 2 && lot_has(lot, n, "CAR_U") >= 0 && lot_has(lot, n, "CAR_V") < 0);
    /* unknown region: picked from the installed cars and fixed on the first buy */
    cr_new_stage0(&t, &cd, -1);
    n = cr_lot_list(&t, &cd, inst_b, 2, lot, 16);
    CHECK(n == 0);                                   /* region B by pick; CAR_V/CAR_W locked */
    CHECK(cr_lot_buy(&t, &cd, inst_b, 2, "CAR_V", &o) == CR_LOT_E_LOCKED && t.region == CR_REGION_UNKNOWN);
    CHECK(cr_lot_buy(&t, &cd, inst, 4, "STARTER1", &o) == CR_LOT_OK && t.region == 0);
}

/* schema 2 OWNED TLV, schema 1 migration, schema 3 rejection, truncation */
static void test_save2(void)
{
    static CareerState s, t, u;
    static uint8_t buf[16384], blk[512], tmp[1024];
    uint8_t a[8];
    size_t len, bl, tl, i;
    StContainerView v;
    cr_new(&s, &cd, "FIRST");
    CHECK(s.nowned == 1 && !strcmp(s.owned[0], "FIRST") && s.region == CR_REGION_UNKNOWN);
    strcpy(s.owned[1], "SECOND_CAR_WITH_A_31_CHAR_NAME_"); s.nowned = 2; s.active = 1; s.region = 1;
    strcpy(s.car, "SECOND_CAR_WITH_A_31_CHAR_NAME_");
    s.stage = 0; s.bank = 12345;
    len = cr_serialize(&s, buf, sizeof buf);
    CHECK(len > 0);
    CHECK(st_container_parse(buf, len, &v) == ST_OK && v.size_a == 8 && v.a[4] == CR_SCHEMA);
    memset(&t, 0x33, sizeof t);
    CHECK(cr_parse(&t, buf, len) == ST_OK && state_eq(&s, &t));
    for (i = 0; i < len; i++) CHECK(cr_parse(&u, buf, i) != ST_OK);
    s.nowned = 6;
    CHECK(cr_serialize(&s, buf, sizeof buf) == 0);
    s.nowned = 2;
    /* schema 1 blob written by hand: CORE, CAR, POSE */
    a[0] = 0x4F; a[1] = 0x55; a[2] = 0x43; a[3] = 0x52; a[4] = 1; a[5] = a[6] = a[7] = 0;
    memset(blk, 0, sizeof blk);
    bl = 0;
    blk[bl] = 1; blk[bl + 4] = 24; bl += 8;
    blk[bl] = 1; blk[bl + 1] = 3; blk[bl + 2] = 1; blk[bl + 4] = 0x10; blk[bl + 5] = 0x27; bl += 24;   /* stage 3, bank 10000 */
    blk[bl] = 3; blk[bl + 4] = 6; bl += 8; memcpy(blk + bl, "OLDCAR", 6); bl += 6;
    blk[bl] = 4; blk[bl + 4] = 16; bl += 8 + 16;
    blk[bl] = 5; blk[bl + 4] = 3; bl += 8 + 3;      /* a tag-5 TLV is not defined in schema 1: dropped */
    tl = st_container_build(tmp, sizeof tmp, a, 8, blk, (uint32_t)bl);
    CHECK(tl > 0);
    memset(&t, 0x44, sizeof t);
    CHECK(cr_parse(&t, tmp, tl) == ST_OK);
    CHECK(t.stage == 3 && t.bank == 10000 && t.started == 1 && !strcmp(t.car, "OLDCAR") && t.nextra == 0);
    CHECK(t.nowned == 1 && !strcmp(t.owned[0], "OLDCAR") && t.active == 0 && t.region == CR_REGION_UNKNOWN);
    len = cr_serialize(&t, buf, sizeof buf);       /* re-saved as schema 2 */
    CHECK(len > 0 && cr_parse(&u, buf, len) == ST_OK && state_eq(&t, &u));
    CHECK(st_container_parse(buf, len, &v) == ST_OK && v.a[4] == CR_SCHEMA);
    /* schema 1 without a car: owns nothing */
    tl = st_container_build(tmp, sizeof tmp, a, 8, blk, 32);
    CHECK(cr_parse(&t, tmp, tl) == ST_OK && t.nowned == 0 && t.region == CR_REGION_UNKNOWN);
    /* schema 3 refused */
    a[4] = CR_SCHEMA + 1;
    tl = st_container_build(tmp, sizeof tmp, a, 8, blk, 32);
    CHECK(cr_parse(&t, tmp, tl) == ST_E_INCOMPATIBLE);
    /* malformed OWNED TLVs (schema 2) */
    a[4] = 2;
    {
        static const uint8_t bad5[4][4] = { { 6, 0, 0, 0 }, { 1, 1, 0, 0 }, { 0, 1, 0, 0 }, { 1, 0, 0, 0 } };
        static const uint32_t badlen[4] = { 4 + 32 * 6, 36, 4, 35 };   /* last: length short of n x 32 */
        int k;
        for (k = 0; k < 4; k++) {
            memset(blk, 0, sizeof blk);
            blk[0] = 5; blk[4] = (uint8_t)badlen[k]; blk[5] = (uint8_t)(badlen[k] >> 8);
            memcpy(blk + 8, bad5[k], 4);
            tl = st_container_build(tmp, sizeof tmp, a, 8, blk, 8 + badlen[k]);
            CHECK(tl > 0 && cr_parse(&t, tmp, tl) == ST_E_CORRUPT);
        }
        /* well-formed empty OWNED */
        memset(blk, 0, sizeof blk);
        blk[0] = 5; blk[4] = 4; blk[10] = 1;
        tl = st_container_build(tmp, sizeof tmp, a, 8, blk, 12);
        CHECK(cr_parse(&t, tmp, tl) == ST_OK && t.nowned == 0 && t.region == 1);
    }
}

static void test_save3(void)
{
    static CareerState s,t;static uint8_t bytes[16384],block[8192],out[16384];
    StContainerView v;uint8_t header[8];size_t n;
    cr_new(&s,&cd,"GOLF");s.config[0].car.parts[N2_PART_AUDIO]=1001;
    strcpy(s.config[0].rim_brand,"NFSU");s.config[0].rim_style=2;
    n=cr_serialize(&s,bytes,sizeof bytes);
    CHECK(n&&st_container_parse(bytes,n,&v)==ST_OK);
    memcpy(header,v.a,8);memcpy(block,v.b,v.size_b);
    size_t at=0;
    while(at+8<=v.size_b&&block[at]!=6) {
        uint32_t size=(uint32_t)block[at+4]|(uint32_t)block[at+5]<<8;
        at+=8+size;
    }
    CHECK(at+96<=v.size_b);
    /* Schema 2 migration keeps ownership but installs stock. */
    header[4]=2;n=st_container_build(out,sizeof out,header,8,block,v.size_b);
    CHECK(cr_parse(&t,out,n)==ST_OK&&t.nowned==1&&t.config[0].car.parts[N2_PART_AUDIO]==0);
    header[4]=3;
    /* Count mismatch, invalid value, duplicate config record, no output mutation. */
    uint8_t saved=block[at+4];block[at+4]=0;
    n=st_container_build(out,sizeof out,header,8,block,(uint32_t)at+8);
    CHECK(cr_parse(&t,out,n)==ST_E_CORRUPT);block[at+4]=saved;
    block[at+8]=255;block[at+9]=255;
    n=st_container_build(out,sizeof out,header,8,block,v.size_b);
    CareerState before=t;
    CHECK(cr_parse(&t,out,n)==ST_E_CORRUPT&&state_eq(&t,&before));
    memcpy(block,v.b,v.size_b);memcpy(block+v.size_b,block+at,96);
    n=st_container_build(out,sizeof out,header,8,block,v.size_b+96);
    CHECK(cr_parse(&t,out,n)==ST_E_CORRUPT);
}

static CrResult res(uint32_t h, int place, int failed, int aborted)
{
    CrResult r; memset(&r, 0, sizeof r);
    r.race_hash = h; r.place = place; r.ncars = 4; r.failed = failed; r.aborted = aborted; r.seconds = 60.0;
    return r;
}
static int listed(const CareerState *s, int race)
{
    int o[CD_MAX_RACES], n = cr_list(s, &cd, o, CD_MAX_RACES), i;
    for (i = 0; i < n; i++) if (o[i] == race) return 1;
    return 0;
}
static CrReward custom(void *user, const CdRace *race, const CrResult *r, int first)
{
    CrReward rw = {1234, 56};
    (void)race; (void)r; (void)first;
    (*(int *)user)++;
    return rw;
}
static int state_eq(const CareerState *a, const CareerState *b)
{
    int i;
    if (a->started != b->started || a->stage != b->stage || a->intro_done != b->intro_done || a->bank != b->bank ||
        a->rep != b->rep || a->entered != b->entered || a->won != b->won || a->nrec != b->nrec ||
        strcmp(a->car, b->car) || memcmp(a->pose, b->pose, sizeof a->pose) || a->nextra != b->nextra ||
        memcmp(a->extra, b->extra, a->nextra)) return 0;
    if (a->nowned != b->nowned || a->active != b->active || a->region != b->region || memcmp(a->config,b->config,sizeof a->config)) return 0;
    for (i = 0; i < a->nowned; i++) if (strcmp(a->owned[i], b->owned[i])) return 0;
    for (i = 0; i < a->nrec; i++)
        if (a->rec[i].hash != b->rec[i].hash || a->rec[i].best_place != b->rec[i].best_place ||
            a->rec[i].flags != b->rec[i].flags || a->rec[i].attempts != b->rec[i].attempts) return 0;
    return 1;
}

/* ---- performance shop (synthetic data) ---- */
static void test_perf(void)
{
    static CareerData pd;
    static CareerState s, t;
    static uint8_t buf[16384];
    int32_t paid;
    float u[CD_PERF_CATS][4];
    size_t len;
    memset(&pd, 0, sizeof pd);
    /* one race in stage 3 for the win counter, one perf shop open from stage 0, one gated shop open at stage 4 */
    pd.race[0].name_hash = 0x500; pd.race[0].stage = 3; pd.race[0].category = CD_CAT_WORLD; pd.race[0].unlock_mode = CD_UNLOCK_STAGE;
    pd.nrace = 1; pd.nstage = 0;
    pd.shop[0].hash = 0x9001; pd.shop[0].type = CD_SHOP_PERF; pd.shop[0].open_stage = 0;
    pd.shop[1].hash = 0x9002; pd.shop[1].type = CD_SHOP_PERF; pd.shop[1].open_stage = 4;
    pd.shop[2].hash = 0x9003; pd.shop[2].type = CD_SHOP_BODY; pd.shop[2].open_stage = 0;
    pd.nshop = 3;
    /* engine (cat 3): L1 = comps 15 ($500, 0.1) + 16 ($250, 0.2); L2 = comp 19 ($1000, 0.7) exclusive with 15 */
    pd.perf_comp[15] = (CdPerfComp){ .valid = 1, .cat = 3, .lvl = 1, .price = 500,  .axis = { 0.1f, -1, -1, -1 }, .excl = { -1, -1, -1, -1, -1 } };
    pd.perf_comp[16] = (CdPerfComp){ .valid = 1, .cat = 3, .lvl = 1, .price = 250,  .axis = { 0.2f, -1, -1, -1 }, .excl = { -1, -1, -1, -1, -1 } };
    pd.perf_comp[19] = (CdPerfComp){ .valid = 1, .cat = 3, .lvl = 2, .price = 1000, .axis = { 0.9f, -1, -1, -1 }, .excl = { 15, -1, -1, -1, -1 } };
    pd.perf_pkg[3][0] = (CdPerfPkg){ 2, { 15, 16 } };
    pd.perf_pkg[3][1] = (CdPerfPkg){ 1, { 19 } };
    pd.nperf_comp = 3;
    /* unlock id 1 (engine): L1 start, L2 = 1 win in stage 3 (type 2 {count 1, stage 3}), L3 shop 0x9002 */
    pd.ulk[0].id = 1;
    pd.ulk[0].l[0] = (CdUnlockLvl){ 0xFFFF, 0xFFFF, CD_ULK_START, 1, 0 };
    pd.ulk[0].l[1] = (CdUnlockLvl){ 0xFFFF, 0xFFFF, CD_ULK_WINS, 2, (3u << 16) | 1u };
    pd.ulk[0].l[2] = (CdUnlockLvl){ 0xFFFF, 0xFFFF, CD_ULK_SHOP, 3, 0x9002 };
    pd.nulk = 1;

    memset(&s, 0, sizeof s);
    s.started = 1; s.stage = 3; s.nowned = 1; strcpy(s.owned[0], "CAR_P"); strcpy(s.car, "CAR_P");
    CHECK(cr_perf_unlock_id(3) == 1 && cr_perf_unlock_id(-1) == -1 && cr_perf_unlock_id(CD_PERF_CATS) == -1);
    CHECK(cr_unlock_ok(&s, &pd, 1, 1));
    CHECK(!cr_unlock_ok(&s, &pd, 1, 2));                 /* stage 3, 0 wins */
    CHECK(!cr_unlock_ok(&s, &pd, 1, 3));                 /* gated shop opens at stage 4 */
    CHECK(!cr_unlock_ok(&s, &pd, 77, 1) && !cr_unlock_ok(&s, &pd, 1, 0) && !cr_unlock_ok(&s, &pd, 1, 4));
    CHECK(cr_perf_price(&s, &pd, 3, 1, -1) == 675);     /* (500 + 250) * 0.9 */
    CHECK(cr_perf_price(&s, &pd, 3, 1, 16) == 250 && cr_perf_price(&s, &pd, 3, 1, 19) == -1);
    s.bank = 600;
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, -1, &paid) == CR_PERF_E_FUNDS && paid == 0 && s.bank == 600);
    CHECK(cr_perf_buy(&s, &pd, 2, 3, 1, 16, &paid) == CR_PERF_E_ARG);    /* body shop does not sell engines */
    CHECK(cr_perf_buy(&s, &pd, 1, 3, 1, 16, &paid) == CR_PERF_E_ARG);    /* shop not open yet */
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, 16, &paid) == CR_PERF_OK && paid == 250 && s.bank == 350 && cr_perf_owned(&s, 16));
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, 16, &paid) == CR_PERF_E_OWNED);
    CHECK(cr_perf_price(&s, &pd, 3, 1, -1) == 450);     /* only comp 15 left: 500 * 0.9 */
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, -1, &paid) == CR_PERF_E_FUNDS && s.bank == 350);
    s.bank = 450;
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, -1, &paid) == CR_PERF_OK && paid == 450 && s.bank == 0);
    CHECK(cr_perf_owned(&s, 15) && cr_perf_owned(&s, 16));
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 1, -1, &paid) == CR_PERF_E_OWNED);
    cr_perf_fraction(&s, &pd, u);
    CHECK(u[3][0] > 0.299f && u[3][0] < 0.301f && u[3][1] == 0.0f && cr_perf_level_of(u[3][0]) == 0);
    s.bank = 5000;
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 2, 19, &paid) == CR_PERF_E_LOCKED);
    s.rec[0].hash = 0x500; s.rec[0].best_place = 1; s.rec[0].flags = CR_REC_WON; s.nrec = 1;   /* 1 win in stage 3 */
    CHECK(cr_unlock_ok(&s, &pd, 1, 2));
    CHECK(cr_perf_buy(&s, &pd, 0, 3, 2, 19, &paid) == CR_PERF_OK && paid == 1000 && s.bank == 4000);
    CHECK(cr_perf_owned(&s, 19) && !cr_perf_owned(&s, 15) && cr_perf_owned(&s, 16));   /* 19 replaces 15 */
    cr_perf_fraction(&s, &pd, u);
    CHECK(u[3][0] == 1.0f && cr_perf_level_of(u[3][0]) == 3);   /* 0.2 + 0.9 clamped */
    s.stage = 4;
    CHECK(cr_unlock_ok(&s, &pd, 1, 2) && cr_unlock_ok(&s, &pd, 1, 3));   /* past stage 3; gated shop open */
    CHECK(cr_perf_level_of(0.33f) == 1 && cr_perf_level_of(0.66f) == 2 && cr_perf_level_of(0.1f) == 0);

    /* paint: two gloss colours (L1, open from start via id 76), one pearl (L3, gated on shop 0x9002) */
    pd.shop[3].hash = 0x9004; pd.shop[3].type = CD_SHOP_PAINT; pd.shop[3].open_stage = 0; pd.nshop = 4;
    pd.paint[0] = (CdPaint){ .key = 0xA1, .group = CD_PAINT_GLOSS, .level = 1, .rgb = { 10, 20, 30 } };
    pd.paint[1] = (CdPaint){ .key = 0xA2, .group = CD_PAINT_GLOSS, .level = 1, .rgb = { 40, 50, 60 } };
    pd.paint[2] = (CdPaint){ .key = 0xA3, .group = CD_PAINT_PEARL, .level = 3 };
    pd.paint[3] = (CdPaint){ .key = 0xA4, .group = CD_PAINT_RIM, .level = 1 };
    pd.npaint = 4;
    pd.ulk[1].id = 76;
    pd.ulk[1].l[0] = (CdUnlockLvl){ 0, 25, CD_ULK_START, 1, 0 };
    pd.ulk[1].l[1] = (CdUnlockLvl){ 0, 60, CD_ULK_START, 2, 0 };
    pd.ulk[1].l[2] = (CdUnlockLvl){ 0, 100, CD_ULK_SHOP, 3, 0x9009 };   /* shop that does not exist: never */
    pd.nulk = 2;
    CHECK(cr_paint_slot(CD_PAINT_METAL) == 0 && cr_paint_slot(CD_PAINT_RIM) == 1 && cr_paint_slot(99) == -1);
    CHECK(cr_paint_unlock_id(CD_PAINT_CALIPER) == 79 && cr_paint_unlock_id(CD_PAINT_PEARL) == 76);
    CHECK(cr_paint_open(&s, &pd, 0) && !cr_paint_open(&s, &pd, 2) && !cr_paint_open(&s, &pd, 3));   /* id 77 missing */
    CHECK(cr_paint_price(&pd, 0) == 25 && cr_paint_price(&pd, 2) == 100 && cr_paint_price(&pd, 3) == 0);
    s.bank = 30;
    CHECK(cr_paint_buy(&s, &pd, 0, 0, &paid) == CR_PERF_E_ARG);          /* a perf shop does not paint */
    CHECK(cr_paint_buy(&s, &pd, 3, 0, &paid) == CR_PERF_OK && paid == 25 && s.bank == 5 && cr_paint_installed(&s, 0) == 0xA1);
    CHECK(cr_paint_buy(&s, &pd, 3, 0, &paid) == CR_PERF_E_OWNED);
    CHECK(cr_paint_buy(&s, &pd, 3, 1, &paid) == CR_PERF_E_FUNDS && cr_paint_installed(&s, 0) == 0xA1);
    CHECK(cr_paint_buy(&s, &pd, 3, 2, &paid) == CR_PERF_E_LOCKED);
    s.bank = 4000;
    /* save round trip (schema 4) and corrupt TLV 7 */
    len = cr_serialize(&s, buf, sizeof buf);
    CHECK(len > 0);
    CHECK(cr_parse(&t, buf, len) == ST_OK && t.perf[0][0] == s.perf[0][0] && t.perf[0][1] == s.perf[0][1] && t.paint[0][0] == 0xA1u);
    CHECK(t.bank == s.bank && cr_perf_owned(&t, 19) && cr_perf_owned(&t, 16) && !cr_perf_owned(&t, 15));
    /* a new lot car starts stock */
    {
        static CareerState n2; n2 = s;
        memset(n2.perf, 0, sizeof n2.perf);
        CHECK(!cr_perf_owned(&n2, 19));
    }
}

int main(void)
{
    static CareerState s, s2, s3;
    static uint8_t buf[16384], bad[16384];
    CrOutcome o;
    CrResult r;
    int cnt = 0, list[CD_MAX_RACES], n, i;
    size_t len;
    char root[256], prof[300], path[400];

    test_perf();
    build_data();
    cr_new(&s, &cd, "CAR_X");
    CHECK(s.started && s.stage == 1 && s.intro_done && s.bank == 0);
    CHECK(strcmp(s.car, "CAR_X") == 0);
    n = cr_list(&s, &cd, list, CD_MAX_RACES);
    CHECK(n == 3 && list[0] == 0 && list[1] == 1 && list[2] == 2);
    CHECK(cr_list(&s, &cd, list, 2) == 2);
    CHECK(!cr_available(&s, &cd, 3) && !cr_available(&s, &cd, 4) && !cr_available(&s, &cd, 5) && !cr_available(&s, &cd, 6));
    CHECK(!cr_available(&s, &cd, 7) && !cr_available(&s, &cd, -1) && !cr_available(&s, &cd, 99));

    /* zones */
    CHECK(cr_zone_hit(&s, &cd, 5, 5) == 0);
    CHECK(cr_zone_hit(&s, &cd, 105, 105) == 1);
    CHECK(cr_zone_hit(&s, &cd, 50, 50) == -1);
    CHECK(cr_zone_hit(&s, &cd, 11, 5) == -1);
    CHECK(cr_zone_hit(&s, &cd, 205, 205) == -1);   /* stage 2 race unavailable */

    /* win */
    r = res(H_A, 1, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    CHECK(o.first_win == 1 && o.reward.cash == 250 && o.reward.rep == 10 && o.stage_before == 1 && o.stage_after == 1);
    CHECK(s.bank == 250 && s.rep == 10 && s.won == 1 && s.entered == 1 && s.nrec == 1);
    CHECK(s.rec[0].hash == H_A && s.rec[0].best_place == 1 && s.rec[0].attempts == 1);
    CHECK(s.rec[0].flags == (CR_REC_WON | CR_REC_PAID));
    CHECK(!listed(&s, 0) && listed(&s, 1));
    CHECK(cr_zone_hit(&s, &cd, 5, 5) == -1);            /* won race hidden */
    CHECK(cr_available(&s, &cd, 3) == 1);               /* URL race unlocked by 1 win */
    CHECK(cr_stage_wins(&s, &cd, 1) == 1 && !cr_stage_complete(&s, &cd));
    /* repeat win pays nothing */
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    CHECK(o.first_win == 0 && o.reward.cash == 0 && s.bank == 250 && s.rep == 10 && s.won == 1 && s.rec[0].attempts == 2);
    /* fail / abort */
    r = res(H_B, 3, 1, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    r = res(H_B, 1, 0, 1);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    r = res(H_B, 2, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    CHECK(s.bank == 250 && s.won == 1 && s.rec[1].attempts == 3 && !(s.rec[1].flags & CR_REC_WON));
    CHECK(s.rec[1].best_place == 2 && s.entered == 5 && listed(&s, 1));
    /* unknown race */
    r = res(0xDEAD, 1, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 0 && s.nrec == 2);

    /* serialize round trip mid-way, with an unknown TLV */
    s.pose[0] = 1.5f; s.pose[1] = -2.25f; s.pose[2] = 3.f; s.pose[3] = 0.5f;
    s.bank = -77;
    len = cr_serialize(&s, buf, sizeof buf);
    CHECK(len > 0 && cr_serialize(&s, buf, 10) == 0);
    memset(&s2, 0xAA, sizeof s2);
    CHECK(cr_parse(&s2, buf, len) == ST_OK);
    CHECK(state_eq(&s, &s2));
    s.bank = 250;

    /* unknown TLV: build from a re-wrapped container */
    {
        StContainerView v;
        uint8_t blk[8192], tmp[8192];
        size_t bl, tl;
        CHECK(cr_serialize(&s, buf, sizeof buf) > 0);
        CHECK(st_container_parse(buf, cr_serialize(&s, buf, sizeof buf), &v) == ST_OK);
        bl = v.size_b; memcpy(blk, v.b, bl);
        blk[bl + 0] = 0x99; blk[bl + 1] = 0; blk[bl + 2] = 0; blk[bl + 3] = 0;
        blk[bl + 4] = 3; blk[bl + 5] = blk[bl + 6] = blk[bl + 7] = 0;
        blk[bl + 8] = 7; blk[bl + 9] = 8; blk[bl + 10] = 9;
        bl += 11;
        tl = st_container_build(tmp, sizeof tmp, v.a, v.size_a, blk, (uint32_t)bl);
        CHECK(tl > 0);
        memset(&s2, 0, sizeof s2);
        CHECK(cr_parse(&s2, tmp, tl) == ST_OK);
        CHECK(s2.nextra == 11 && s2.extra[0] == 0x99 && s2.extra[10] == 9);
        len = cr_serialize(&s2, bad, sizeof bad);
        memset(&s3, 0, sizeof s3);
        CHECK(len == tl && memcmp(bad, tmp, tl) == 0);
        CHECK(cr_parse(&s3, bad, len) == ST_OK && state_eq(&s2, &s3));
        /* schema 2 */
        {
            uint8_t a2[8];
            memcpy(a2, v.a, 8); a2[4] = CR_SCHEMA + 1;   /* schema 3: newer than this build */
            tl = st_container_build(tmp, sizeof tmp, a2, 8, v.b, v.size_b);
            CHECK(cr_parse(&s3, tmp, tl) == ST_E_INCOMPATIBLE);
            a2[4] = 0;
            tl = st_container_build(tmp, sizeof tmp, a2, 8, v.b, v.size_b);
            CHECK(cr_parse(&s3, tmp, tl) == ST_E_CORRUPT);
            a2[4] = 1; a2[0] ^= 1;
            tl = st_container_build(tmp, sizeof tmp, a2, 8, v.b, v.size_b);
            CHECK(cr_parse(&s3, tmp, tl) == ST_E_CORRUPT);
            /* TLV length overrun */
            memcpy(blk, v.b, v.size_b); blk[4] = 0xFF; blk[5] = 0xFF;
            tl = st_container_build(tmp, sizeof tmp, v.a, 8, blk, v.size_b);
            CHECK(cr_parse(&s3, tmp, tl) == ST_E_CORRUPT);
        }
    }
    /* flipped byte and truncation */
    len = cr_serialize(&s, buf, sizeof buf);
    memcpy(bad, buf, len); bad[len - 3] ^= 0x40;
    CHECK(cr_parse(&s3, bad, len) == ST_E_CORRUPT);
    for (i = 0; i < (int)len; i++) CHECK(cr_parse(&s3, buf, (size_t)i) != ST_OK);
    CHECK(cr_parse(&s3, buf, len) == ST_OK);

    /* custom reward */
    cr_new(&s3, &cd, "C");
    r = res(H_B, 1, 0, 0);
    CHECK(cr_apply(&s3, &cd, &r, custom, &cnt, &o) == 1);
    CHECK(cnt == 1 && s3.bank == 1234 && s3.rep == 56 && o.reward.cash == 1234);
    CHECK(cr_apply(&s3, &cd, &r, custom, &cnt, &o) == 1 && s3.bank == 1234);

    /* stage advance: s has won H_A; win H_B -> req_total 2 met -> stage 2 */
    r = res(H_B, 1, 0, 0);
    CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    CHECK(o.stage_before == 1 && o.stage_after == 2 && s.stage == 2);
    CHECK(listed(&s, 7) && listed(&s, 8) && !listed(&s, 0) && !listed(&s, 1) && !listed(&s, 3));
    CHECK(cr_zone_hit(&s, &cd, 205, 205) == 7);
    /* stage 2 needs a URL win and total 2; no stage 3 exists -> stays */
    r = res(H_H, 1, 0, 0); CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    r = res(H_I, 1, 0, 0); CHECK(cr_apply(&s, &cd, &r, NULL, NULL, &o) == 1);
    CHECK(s.stage == 2 && cr_stage_complete(&s, &cd) == 0);   /* req_url 1 unmet */

    /* files */
    {
        const char *tmpbase = "build/career_test_XXXXXX";
        char tpl[64];
        strcpy(tpl, tmpbase);
        CHECK(mkdtemp(tpl) != NULL);
        snprintf(root, sizeof root, "%s", tpl);
        snprintf(prof, sizeof prof, "%s/ALICE", root);
        CHECK(mkdir(prof, 0700) == 0);
        snprintf(prof, sizeof prof, "%s/BOB", root);
        CHECK(mkdir(prof, 0700) == 0);
        CHECK(cr_load_file(root, "ALICE", &s3) == ST_E_NOTFOUND);
        CHECK(cr_save_file(root, "NOPE", &s) == ST_E_NOTFOUND);
        CHECK(cr_copy_file(root, "ALICE", "BOB") == ST_E_NOTFOUND);
        CHECK(cr_save_file(root, "ALICE", &s) == ST_OK);
        memset(&s3, 0, sizeof s3);
        CHECK(cr_load_file(root, "ALICE", &s3) == ST_OK && state_eq(&s, &s3));
        snprintf(path, sizeof path, "%s/ALICE/%s.tmp", root, CR_FILE_NAME);
        CHECK(access(path, F_OK) != 0);
        s.bank += 5;
        CHECK(cr_save_file(root, "ALICE", &s) == ST_OK);      /* replace */
        CHECK(cr_copy_file(root, "ALICE", "BOB") == ST_OK);
        memset(&s2, 0, sizeof s2);
        CHECK(cr_load_file(root, "BOB", &s2) == ST_OK && state_eq(&s, &s2));
        CHECK(cr_save_file(root, "a/b", &s) == ST_E_ARG);
        /* corrupt file on disk */
        snprintf(path, sizeof path, "%s/BOB/%s", root, CR_FILE_NAME);
        {
            FILE *f = fopen(path, "r+b");
            CHECK(f != NULL);
            if (f) { fseek(f, 40, SEEK_SET); fputc(0x5A, f); fputc(0xA5, f); fclose(f); }
        }
        CHECK(cr_load_file(root, "BOB", &s2) == ST_E_CORRUPT);
        /* cleanup */
        snprintf(path, sizeof path, "%s/ALICE/%s", root, CR_FILE_NAME); remove(path);
        snprintf(path, sizeof path, "%s/BOB/%s", root, CR_FILE_NAME); remove(path);
        snprintf(path, sizeof path, "%s/ALICE", root); rmdir(path);
        snprintf(path, sizeof path, "%s/BOB", root); rmdir(path);
        rmdir(root);
    }

    test_lot();
    test_save2();
    test_save3();
    build_lot_data();
    cr_new(&s, &cd, "STARTER1");
    cd.shop[3].type = CD_SHOP_BODY;
    GarageConfig cfg = {0}; cfg.car.body_kit = 1;
    CHECK(cr_shop_install(&s, &cd, 3, &cfg) == ST_OK);
    CHECK(cr_car_config(&s)->car.body_kit == 1 && s.bank == 0);
    cfg.car.parts[N2_PART_AUDIO] = 101;
    CHECK(cr_shop_install(&s, &cd, 3, &cfg) == ST_E_ARG);
    cd.shop[3].type = CD_SHOP_SPECIALTY;
    CHECK(cr_shop_install(&s, &cd, 3, &cfg) == ST_OK);
    cfg.car.body_kit = 2;
    CHECK(cr_shop_install(&s, &cd, 3, &cfg) == ST_E_ARG);
    CHECK(!cr_shop_slot(CD_SHOP_PERF, N2_PART_ENGINE));
    CHECK(!cr_shop_slot(CD_SHOP_PAINT, GARAGE_KIT));
    CHECK(!cr_shop_slot(CD_SHOP_BODY, -1));
    CHECK(cr_rule_part_price(&cd, 3, N2_PART_AUDIO, 101) == 0);
    len = cr_serialize(&s, buf, sizeof buf);
    CHECK(len && cr_parse(&s2, buf, len) == ST_OK);
    CHECK(s2.config[0].car.body_kit == 1 && s2.config[0].car.parts[N2_PART_AUDIO] == 101);
    cd.shop[3].open_stage = 2;
    CHECK(cr_shop_install(&s, &cd, 3, &cfg) == ST_E_ARG);
    printf("career_test: %d checks, %d failures\n", checks, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails != 0;
}
