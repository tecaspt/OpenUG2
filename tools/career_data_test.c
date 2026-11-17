/* career_data_test -- synthetic unit test for career_data.c (all buffers built here; no retail bytes). */
#include "career_data.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "race.h"
#pragma GCC diagnostic pop
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void put(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = (uint8_t *)realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void u8(Buf *b, unsigned v) { uint8_t c = (uint8_t)v; put(b, &c, 1); }
static void u16(Buf *b, unsigned v) { u8(b, v & 255); u8(b, v >> 8); }
static void u32(Buf *b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
static void f32(Buf *b, float f) { uint32_t u; memcpy(&u, &f, 4); u32(b, u); }
static void pad(Buf *b, size_t n) { while (n--) u8(b, 0); }
static size_t begin(Buf *b, uint32_t id) { u32(b, id); u32(b, 0); return b->n; }
static void end(Buf *b, size_t start) { uint32_t sz = (uint32_t)(b->n - start); memcpy(b->p + start - 4, &sz, 4); }  /* LE host */
static uint32_t hs(const char *s) { uint32_t h = 0xFFFFFFFFu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }
static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* string blob: offset 0 reserved NUL, then names */
static const char blob[] = "\0RACE_A\0LOC_A\0RACE_B\0LOC_B\0RACE_C\0LOC_C\0SMS_A\0SMS_B\0"
                           "A_LONG_SMS_NAME_PAST_FORTY_CHARACTERS_AND_THEN_SOME\0";
enum { OFF_RA = 1, OFF_LA = 8, OFF_RB = 14, OFF_LB = 21, OFF_RC = 27, OFF_LC = 34,
       OFF_SA = 40, OFF_SB = 46, OFF_SL = 52 };   /* OFF_SL: 50 chars, truncates to name[39] */

static uint32_t g_preset, g_shop;   /* +0x2C / +0x3C of the next race() */
static void race(Buf *b, unsigned name, const char *nm, unsigned loc, const char *lc, unsigned mode, unsigned type,
                 unsigned cat, unsigned stage, uint32_t purse, unsigned t0, unsigned laps0, unsigned t1) {
    size_t s = b->n;
    u16(b, name); pad(b, 4); u16(b, loc); u32(b, hs(nm));
    u8(b, mode); u8(b, 1); u8(b, 0); u8(b, type);
    u32(b, 0x01020304u); u32(b, 100);
    u16(b, t0); u8(b, 1); u8(b, laps0); u16(b, t1); u8(b, 0); u8(b, 2); pad(b, 8);
    u32(b, hs(lc)); u32(b, g_preset); u32(b, purse); u8(b, cat); pad(b, 2); u8(b, stage);
    pad(b, 4); u32(b, g_shop);
    pad(b, 0x7C - (b->n - s)); u8(b, 3); pad(b, 2); u8(b, 1); pad(b, CD_RACE_BYTES - (b->n - s));
    CHECK(b->n - s == CD_RACE_BYTES);
}
static void stage(Buf *b, unsigned idx, unsigned nspon, uint32_t sp0, unsigned url, unsigned dvd, unsigned total,
                  uint32_t final) {
    size_t s = b->n;
    u8(b, idx); u8(b, nspon); pad(b, 6); u32(b, sp0); u32(b, sp0 + 1); pad(b, 12);
    pad(b, 0x28 - (b->n - s)); u32(b, final);
    pad(b, 0x2D - (b->n - s)); u8(b, 3); u8(b, url); u8(b, dvd);
    pad(b, 0x42 - (b->n - s)); u8(b, total); pad(b, CD_STAGE_BYTES - (b->n - s));
    CHECK(b->n - s == CD_STAGE_BYTES);
}
static void shop(Buf *b, const char *name, const char *zone, const char *plat, unsigned type, unsigned known,
                 unsigned st) {
    size_t s = b->n, l = strlen(name);
    put(b, name, l); pad(b, 0x38 - l);
    u32(b, hs(name)); u32(b, hs(zone));
    l = strlen(plat); put(b, plat, l); pad(b, 16 - l);
    u8(b, type); put(b, "GARBAGE_STRING", 14); pad(b, 0x9C - (b->n - s));
    u8(b, known); u8(b, st); pad(b, CD_SHOP_BYTES - (b->n - s));
    CHECK(b->n - s == CD_SHOP_BYTES);
}
static int g_tables = 1;   /* 0: build without the shop / unlock / sms tables; 2: malformed table sizes */
/* one 20-byte 0x34A17 record; `bad` writes an out-of-range name offset (0xFFFF) instead of `name` */
static void sms(Buf *b, unsigned name, unsigned trig, unsigned cls, unsigned flag, unsigned kind,
                uint32_t arg, unsigned weight, uint32_t sender, int bad) {
    size_t s = b->n;
    u16(b, bad ? 0xFFFFu : name); u8(b, trig); u8(b, cls); u8(b, flag); u8(b, kind); u8(b, 0x88); u8(b, 0);
    u32(b, bad ? 0u : arg); u16(b, weight); pad(b, 2); u32(b, sender);
    CHECK(b->n - s == CD_SMS_BYTES);
}
static void build_career(Buf *b, int with_decoy_first) {
    size_t c, s;
    if (with_decoy_first) {   /* an unrelated chunk, then a bogus 0x80034A10-less region */
        s = begin(b, 0x00011111u); pad(b, 12); end(b, s);
    }
    c = begin(b, 0x80034A10u);
    s = begin(b, 0x34A1Du); put(b, blob, sizeof blob); end(b, s);
    s = begin(b, 0x34A11u);
    race(b, OFF_RA, "RACE_A", OFF_LA, "LOC_A", 1, 0, 0, 1, 250, 4013, 3, 0);
    race(b, OFF_RB, "RACE_B", OFF_LB, "LOC_B", 2, 4, 1, 2, 750, 4201, 1, 4202);
    g_preset = hs("PRESET_CAR"); g_shop = hs("LOT_X");
    race(b, OFF_RC, "RACE_C", OFF_LC, "LOC_C", 3, 3, 3, 5, 10000, 4311, 2, 0);
    g_preset = g_shop = 0;
    end(b, s);
    if (g_tables) {
        s = begin(b, 0x34A12u);
        shop(b, "LOT_X", "LOT_X_ZONE", "CARLOT01.BIN", 4, 0, 0);
        shop(b, "CRIB_X", "CRIB_X_ZONE", "CRIB01.BIN", 0, 1, 0);
        shop(b, "A_SHOP_WITH_A_VERY_LONG_NAME_OVER_32_CHARS", "Z3", "PERFSHOP01.BIN", 3, 1, 2);
        if (g_tables == 2) pad(b, 7);
        end(b, s);
        s = begin(b, 0x34A1Fu);
        u32(b, hs("CAR_A")); u32(b, hs("RACE_B")); u32(b, 0);
        u32(b, hs("CAR_B")); u32(b, 0); u32(b, hs("RACE_C"));
        if (g_tables == 2) pad(b, 5);
        end(b, s);
        s = begin(b, 0x34A17u);
        /* retail-shaped: a message, a tip whose arg is its own name hash, an unlock pair (count 2, stage 3),
         * a name too long for name[40], and a record whose name offset is out of range */
        sms(b, OFF_SA, 3, 4, 0, 1, 10, 0, hs("RACHEL"), 0);
        sms(b, OFF_SB, 15, 2, 0, 9, hs("SMS_B"), 25, hs("RACHEL"), 0);
        sms(b, OFF_SA, 12, 5, 0, 5, 0x00030002u, 0, hs("CHUCK"), 0);
        sms(b, OFF_SL, 2, 4, 1, 1, 0x008ba2d3u, 0, hs("SERENA"), 0);
        sms(b, 0, 9, 4, 0, 1, 0, 0, hs("STEFAN"), 1);
        if (g_tables == 2) pad(b, 6);
        end(b, s);
    }
    s = begin(b, 0x34A18u);
    stage(b, 0, 0, 0, 0, 0, 1, hs("RACE_C"));
    stage(b, 1, 2, 0xAABB0000u, 7, 2, 5, 0);
    end(b, s);
    end(b, c);
    /* a second, demo career chunk that must be ignored */
    c = begin(b, 0x80034A10u);
    s = begin(b, 0x34A1Du); put(b, blob, sizeof blob); end(b, s);
    s = begin(b, 0x34A11u); pad(b, CD_RACE_BYTES); end(b, s);
    s = begin(b, 0x34A18u); pad(b, CD_STAGE_BYTES); end(b, s);
    end(b, c);
}
static void zone(Buf *b, uint32_t h, float x0, float y0, float x1, float y1, unsigned n) {
    f32(b, x0); f32(b, y0); f32(b, x1); f32(b, y1); u32(b, h); u32(b, 7); u32(b, 8); u32(b, 9); u16(b, n); u16(b, 100);
    f32(b, x0); f32(b, y0); f32(b, x1); f32(b, y0); f32(b, x1); f32(b, y1); f32(b, x0); f32(b, y1);
}
static void build_zones(Buf *b) {
    size_t c = begin(b, 0x80034147u), s;
    s = begin(b, 0x3414Du); pad(b, 40); end(b, s);
    s = begin(b, 0x3414Au);
    pad(b, 8);                                   /* unrelated header words */
    zone(b, hs("LOC_A"), -632, 228, -624, 236, 4);
    pad(b, 12);                                  /* other record kind in between */
    zone(b, hs("LOC_B"), 100, 200, 108, 208, 4);
    zone(b, 0, 1, 2, 9, 10, 4);                  /* hash 0: ignored */
    zone(b, hs("LOC_C"), 0, 0, 500, 500, 4);     /* too large: ignored */
    zone(b, hs("OTHER"), 5, 5, 13, 13, 4);       /* not a race locator: dropped only when races are loaded */
    zone(b, hs("LOC_A"), 300, 300, 308, 308, 4); /* duplicate hash: first wins */
    zone(b, hs("CRIB_X_ZONE"), 640, -110, 661, -90, 4);   /* 21 m shop zone: kept (shop limit 32 m) */
    zone(b, hs("Z3"), 0, 0, 40, 40, 4);                   /* 40 m shop zone: too large */
    zone(b, hs("LOC_B2"), 0, 0, 21, 21, 4);               /* 21 m non-shop zone: too large */
    end(b, s); end(b, c);
}
static void marker(Buf *b, uint32_t h, unsigned slot, float x, float y, float z, unsigned head) {
    u32(b, 11); u32(b, 11); u32(b, h); u32(b, slot); f32(b, x); f32(b, y); f32(b, z); f32(b, 0); u16(b, head); u16(b, 5);
    u32(b, 4000); pad(b, 8);
}
static void build_markers(Buf *b) {
    size_t c = begin(b, 0x34146u);
    u32(b, 0x11111111u); u32(b, 0x11111111u);
    marker(b, hs("CRIB_X"), 0, 648.5f, -96.25f, 15.f, 16384);
    marker(b, hs("GRID_X"), 0, 1, 2, 3, 0);
    marker(b, hs("GRID_X"), 1, 4, 5, 6, 0);
    end(b, c);
}

static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* Header offset of a chunk with `id` inside the first 0x80034A10 container of a buffer built by begin()/end()
 * above, mirroring how find_chunk descends: career -> inner chunks. Inner chunks are not 8-aligned against the
 * buffer start, so the walk must stay inside the container rather than scan the whole buffer. Returns 0 absent. */
static size_t find_career_chunk_off(const uint8_t *p, size_t n, uint32_t id) {
    size_t o = 0;
    while (o + 8 <= n) {   /* top level: find the first career container */
        uint32_t cid = rd32le(p + o), sz = rd32le(p + o + 4);
        size_t q;
        if (cid != 0x80034A10u) {
            if ((size_t)sz > n - o - 8) break;
            o += 8 + (size_t)sz;
            continue;
        }
        for (q = o + 8; q + 8 <= o + 8 + sz; ) {   /* inside it: walk the inner chunks */
            uint32_t iid = rd32le(p + q), isz = rd32le(p + q + 4);
            if (iid == id) return q;
            if ((size_t)isz > o + 8 + sz - q - 8) break;
            q += 8 + (size_t)isz;
        }
        return 0;
    }
    return 0;
}

static void test_good(void) {
    Buf b = { 0, 0, 0 };
    static CareerData cd;
    build_career(&b, 1);
    CHECK(cd_parse_globalb(&cd, b.p, b.n) == 3);
    CHECK(cd.nrace == 3 && cd.nstage == 2);
    CHECK(strcmp(cd.race[0].name, "RACE_A") == 0 && strcmp(cd.race[0].locator, "LOC_A") == 0);
    CHECK(cd.race[0].name_hash == hs("RACE_A") && cd.race[0].locator_hash == hs("LOC_A"));
    CHECK(cd.race[0].unlock_mode == 1 && cd.race[0].suv == 1 && cd.race[0].type == 0);
    CHECK(cd.race[0].unlock_arg == 0x01020304u && cd.race[0].rep_base == 100);
    CHECK(cd.race[0].purse == 250 && cd.race[0].category == 0 && cd.race[0].stage == 1);
    CHECK(cd.race[0].nai == 3 && cd.race[0].hidden == 1);
    CHECK(cd.race[0].nheat == 1 && cd.race[0].heat[0].track == 4013 && cd.race[0].heat[0].dir == 1 &&
          cd.race[0].heat[0].laps == 3);
    CHECK(cd.race[0].kind == N2_RACE_CIRCUIT);
    CHECK(cd.race[1].nheat == 2 && cd.race[1].heat[1].track == 4202 && cd.race[1].heat[1].laps == 2);
    CHECK(cd.race[1].kind == N2_RACE_DRAG && cd.race[1].purse == 750 && cd.race[1].category == CD_CAT_SPONSOR);
    CHECK(cd.race[2].kind == N2_RACE_UNKNOWN && cd.race[2].stage == 5 && cd.race[2].unlock_mode == CD_UNLOCK_URL);
    CHECK(cd.race[0].raw[0x0F] == 0 && cd.race[1].raw[0x0F] == 4);
    CHECK(cd.stage[0].index == 0 && cd.stage[0].nsponsors == 0 && cd.stage[0].req_total == 1);
    CHECK(cd.stage[1].index == 1 && cd.stage[1].nsponsors == 2 && cd.stage[1].sponsor[0] == 0xAABB0000u &&
          cd.stage[1].sponsor[1] == 0xAABB0001u && cd.stage[1].sponsor[2] == 0);
    CHECK(cd.stage[1].req_url == 7 && cd.stage[1].req_dvd == 2 && cd.stage[1].req_sponsor == 3 &&
          cd.stage[1].req_total == 5);
    CHECK(cd_find(&cd, hs("RACE_B")) == 1 && cd_find(&cd, 12345) == -1);
    CHECK(cd.race[2].preset_hash == hs("PRESET_CAR") && cd.race[2].shop_hash == hs("LOT_X"));
    CHECK(cd.race[0].preset_hash == 0 && cd.race[0].shop_hash == 0);
    CHECK(cd.stage[0].final_race == hs("RACE_C") && cd.stage[1].final_race == 0);
    CHECK(cd.nshop == 3 && cd.nunlock == 2);
    CHECK(!strcmp(cd.shop[0].name, "LOT_X") && cd.shop[0].hash == hs("LOT_X") && cd.shop[0].zone_hash == hs("LOT_X_ZONE"));
    CHECK(!strcmp(cd.shop[0].platform, "CARLOT01.BIN") && cd.shop[0].type == CD_SHOP_CAR_LOT);
    CHECK(cd.shop[0].known == 0 && cd.shop[0].open_stage == 0 && cd.shop[1].known == 1 && cd.shop[1].type == CD_SHOP_CRIB);
    CHECK(strlen(cd.shop[2].name) == 31 && !strncmp(cd.shop[2].name, "A_SHOP_WITH", 11) && cd.shop[2].open_stage == 2);
    CHECK(!strcmp(cd.shop[2].platform, "PERFSHOP01.BIN"));
    CHECK(cd_find_shop(&cd, hs("CRIB_X")) == 1 && cd_find_shop(&cd, 99) == -1);
    CHECK(cd.unlock[0].car_hash == hs("CAR_A") && cd.unlock[0].race[0] == hs("RACE_B") && cd.unlock[0].race[1] == 0);
    CHECK(cd.unlock[1].car_hash == hs("CAR_B") && cd.unlock[1].race[1] == hs("RACE_C"));
    CHECK(cd_hash("focus") == hs("FOCUS") && cd_hash("FOCUS") == hs("FOCUS") && cd_hash("") == 0xFFFFFFFFu);
    /* 0x34A17: five records, the last with an out-of-range name offset (kept, name empty) */
    CHECK(cd.nsms == 5);
    CHECK(!strcmp(cd.sms[0].name, "SMS_A") && cd.sms[0].name_hash == hs("SMS_A"));
    CHECK(cd.sms[0].trigger == 3 && cd.sms[0].cls == 4 && cd.sms[0].flag == 0 && cd.sms[0].kind == CD_SMS_MSG);
    CHECK(cd.sms[0].arg == 10 && cd.sms[0].weight == 0 && cd.sms[0].sender == hs("RACHEL"));
    CHECK(cd.sms[0].raw[6] == 0x88 && cd.sms[0].raw[0] == OFF_SA && cd.sms[0].raw[1] == 0);
    CHECK(cd.sms[1].kind == CD_SMS_TIP && cd.sms[1].arg == hs("SMS_B") && cd.sms[1].weight == 25);
    CHECK(cd.sms[2].kind == CD_SMS_UNLOCK && CD_SMS_ARG_LO(cd.sms[2].arg) == 2 &&
          CD_SMS_ARG_HI(cd.sms[2].arg) == 3);
    CHECK(strlen(cd.sms[3].name) == 39 && !strncmp(cd.sms[3].name, "A_LONG_SMS_NAME", 15) &&
          cd.sms[3].name_hash == cd_hash(cd.sms[3].name) && cd.sms[3].flag == 1 &&
          cd.sms[3].arg == 0x008ba2d3u);                          /* the entity-hash reading */
    /* out-of-range name offset: the record survives, name empty, other fields intact */
    CHECK(cd.sms[4].name[0] == 0 && cd.sms[4].name_hash == cd_hash("") && cd.sms[4].sender == hs("STEFAN") &&
          cd.sms[4].trigger == 9 && cd.sms[4].cls == 4 && cd.sms[4].kind == CD_SMS_MSG &&
          cd.sms[4].raw[0] == 0xFF && cd.sms[4].raw[1] == 0xFF && CD_SMS_ARG_LO(cd.sms[4].arg) == 0);
    CHECK(cd_find_sms(&cd, hs("SMS_A")) == 0 && cd_find_sms(&cd, hs("SMS_B")) == 1);
    CHECK(cd_find_sms(&cd, hs("NOPE")) == -1 && cd_find_sms(&cd, 0) == -1 && cd_find_sms(&cd, cd_hash("")) == -1);
    free(b.p);
    {   /* tables missing / malformed -> count 0, races still parse */
        int t;
        for (t = 0; t <= 2; t += 2) {
            static CareerData m;
            Buf c = { 0, 0, 0 };
            g_tables = t;
            build_career(&c, 0);
            CHECK(cd_parse_globalb(&m, c.p, c.n) == 3 && m.nshop == 0 && m.nunlock == 0 && m.nsms == 0);
            free(c.p);
        }
        g_tables = 1;
    }
    {   /* Truncated table. A chunk's size is validated against its *container*, never against the buffer, so the
         * only reachable truncation is a container whose declared size runs past the buffer - and there the shared
         * find_chunk() behaviour is to reject the container outright (an error, nsms 0) rather than read any table.
         * The size-word cases (odd size, empty table) are covered by the g_tables == 2 probe above, which appends
         * padding: shrinking a size word in place would desynchronise the sibling walk that find_chunk() does on the
         * *declared* sizes. */
        static CareerData m;
        Buf c = { 0, 0, 0 };
        size_t o;
        build_career(&c, 0);
        CHECK(cd_parse_globalb(&m, c.p, c.n) == 3 && m.nsms == 5);
        o = find_career_chunk_off(c.p, c.n, 0x34A17u);           /* the chunk's size word */
        CHECK(o && c.p[o + 4] == 5 * CD_SMS_BYTES && c.p[o + 5] == 0);
        c.p[8 + 4] = 0xFF; c.p[8 + 5] = 0xFF;                   /* the container claims far more than the buffer */
        CHECK(cd_parse_globalb(&m, c.p, c.n) == 0 && m.nsms == 0 && m.nrace == 0 && m.error[0]);
        free(c.p);
    }

    /* zones: with races loaded the OTHER zone is dropped */
    b.p = NULL; b.n = b.cap = 0;
    build_zones(&b);
    CHECK(cd_parse_zones(&cd, b.p, b.n) == 3);
    CHECK(cd.nzone == 3 && cd.zone[0].hash == hs("LOC_A") && cd.zone[1].hash == hs("LOC_B"));
    CHECK(cd_find_zone(&cd, hs("CRIB_X_ZONE")) == 2 && cd_find_zone(&cd, hs("Z3")) == -1 &&
          cd_find_zone(&cd, hs("LOC_B2")) == -1);
    CHECK(near(cd.zone[0].cx, -628) && near(cd.zone[0].cy, 232));
    CHECK(near(cd.zone[0].poly[0][0], -632) && near(cd.zone[0].poly[2][1], 236) && near(cd.zone[0].poly[1][0], -624));
    CHECK(cd_find_zone(&cd, hs("LOC_B")) == 1 && cd_find_zone(&cd, hs("OTHER")) == -1 &&
          cd_find_zone(&cd, hs("LOC_C")) == -1);
    {   /* without races all small zones are kept */
        static CareerData z;
        memset(&z, 0, sizeof z);
        CHECK(cd_parse_zones(&z, b.p, b.n) == 3);   /* LOC_A, LOC_B, OTHER; no shop zones without shops */
        CHECK(cd_find_zone(&z, hs("OTHER")) >= 0 && near(z.zone[0].cx, -628));
    }
    free(b.p);

    b.p = NULL; b.n = b.cap = 0;
    build_markers(&b);
    CHECK(cd_parse_markers(&cd, b.p, b.n) == 2);
    CHECK(cd_find_marker(&cd, hs("CRIB_X")) == 0 && cd_find_marker(&cd, hs("GRID_X")) == 1 && cd_find_marker(&cd, 1) == -1);
    CHECK(near(cd.marker[0].x, 648.5f) && near(cd.marker[0].y, -96.25f) && near(cd.marker[0].z, 15.f));
    CHECK(near(cd.marker[0].heading, 1.5707963f) && near(cd.marker[1].x, 1));
    free(b.p);
}

static void test_errors(void) {
    static CareerData cd;
    Buf b = { 0, 0, 0 };
    uint8_t one = 0;
    memset(&cd, 0, sizeof cd);
    CHECK(cd_parse_globalb(&cd, NULL, 0) == 0 && cd.error[0]);
    CHECK(cd_parse_globalb(&cd, &one, 1) == 0 && cd.error[0]);
    CHECK(cd_parse_zones(&cd, &one, 1) == 0 && cd_parse_markers(&cd, &one, 1) == 0);
    CHECK(cd_load(&cd, "/nonexistent-career-root") == 0 && cd.error[0]);
    /* bad string offset */
    build_career(&b, 0);
    b.p[24 + sizeof blob] = 0xFF; b.p[25 + sizeof blob] = 0xFF;   /* race 0 name offset */
    CHECK(cd_parse_globalb(&cd, b.p, b.n) == 0);
    free(b.p);
}

/* every truncation and random mutations: never crash; result must be 0 or a sane count */
static void fuzz(const Buf *b, int which) {
    static CareerData cd;
    size_t n, i;
    uint8_t *m = (uint8_t *)malloc(b->n ? b->n : 1);
    unsigned seed = 12345u + (unsigned)which;
    for (n = 0; n <= b->n; n++) {
        uint8_t *exact = (uint8_t *)malloc(n ? n : 1);   /* exact-size heap copy so ASan sees overreads */
        int r;
        memcpy(exact, b->p, n);
        r = which == 0 ? cd_parse_globalb(&cd, exact, n) : which == 1 ? cd_parse_zones(&cd, exact, n)
                                                                      : cd_parse_markers(&cd, exact, n);
        CHECK(r >= 0 && r <= 256);
        free(exact);
    }
    for (i = 0; i < 4000; i++) {
        int k, r;
        memcpy(m, b->p, b->n);
        for (k = 0; k < 1 + (int)(i % 6); k++) {
            seed = seed * 1664525u + 1013904223u;
            m[(seed >> 8) % b->n] = (uint8_t)(seed >> 24);
        }
        r = which == 0 ? cd_parse_globalb(&cd, m, b->n) : which == 1 ? cd_parse_zones(&cd, m, b->n)
                                                                    : cd_parse_markers(&cd, m, b->n);
        CHECK(r >= 0 && r <= 256);
    }
    free(m);
}

int main(void) {
    Buf b = { 0, 0, 0 };
    test_good();
    test_errors();
    build_career(&b, 0); fuzz(&b, 0); free(b.p);
    b.p = NULL; b.n = b.cap = 0; build_zones(&b); fuzz(&b, 1); free(b.p);
    b.p = NULL; b.n = b.cap = 0; build_markers(&b); fuzz(&b, 2); free(b.p);
    printf("career_data_test: %d checks, %d failures\n", checks, failures);
    if (!failures) printf("PASS\n");
    return failures ? 1 : 0;
}
