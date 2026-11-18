/* career_data_check DATAROOT -- loads the retail career database and verifies the expected totals. */
#include "career_data.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t hs(const char *s) { uint32_t h = 0xFFFFFFFFu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }

/* Installed drivable cars: CARS/<name>/GEOMETRY.BIN present, TEXTURES.BIN non-empty (empty = traffic) and not a
 * shared parts folder (same list as res_list_cars). */
static int installed_cars(const char *root, char (*out)[32], int cap) {
    static const char *const parts[] = { "AUDIO", "BRAKES", "EXHAUST", "PLATES", "ROOF", "WHEELS", "SPOILER",
        "SPOILER_HATCH", "SPOILER_SUV", "MIRRORS_BODY", "MIRRORS_HUMMER", "MIRRORS_POST", "MIRRORS_SUV" };
    char p[1100];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(p, sizeof p, "%s/CARS", root);
    if (!(d = opendir(p))) return 0;
    while ((e = readdir(d)) && n < cap) {
        struct stat g, t;
        char q[1400];
        unsigned k;
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31) continue;
        for (k = 0; k < sizeof parts / sizeof parts[0]; k++) if (!strcmp(e->d_name, parts[k])) break;
        if (k < sizeof parts / sizeof parts[0]) continue;
        snprintf(q, sizeof q, "%s/%s/GEOMETRY.BIN", p, e->d_name);
        if (stat(q, &g) != 0) continue;
        snprintf(q, sizeof q, "%s/%s/TEXTURES.BIN", p, e->d_name);
        if (stat(q, &t) != 0 || t.st_size == 0) continue;
        memcpy(out[n], e->d_name, strlen(e->d_name) + 1); n++;   /* length checked above */
    }
    closedir(d);
    return n;
}
static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* Independent re-statement of the career.c region / starter rules (cr_region_pick): a column qualifies when every
 * car it unlocks (non-zero race) is installed; tie -> A. Starters = installed cars with no unlock in that column. */
static void check_lot(const CareerData *cd, const char *root) {
    static char inst[64][32];
    int n = installed_cars(root, inst, 64), col, ok[2], i, j, ns = 0;
    char line[512] = "";
    qsort(inst, (size_t)n, sizeof inst[0], cmp_name);
    for (col = 0; col < 2; col++) {
        ok[col] = cd->nunlock > 0;
        for (i = 0; i < cd->nunlock; i++) {
            if (!cd->unlock[i].race[col]) continue;
            for (j = 0; j < n; j++) if (hs(inst[j]) == cd->unlock[i].car_hash) break;
            if (j == n) ok[col] = 0;
        }
    }
    col = ok[0] || !ok[1] ? 0 : 1;
    printf("installed cars %d, region %c (A %s, B %s)\n", n, 'A' + col, ok[0] ? "ok" : "missing cars",
           ok[1] ? "ok" : "missing cars");
    EXPECT(col == 1, "region %c, expected B", 'A' + col);
    for (j = 0; j < n; j++) {
        for (i = 0; i < cd->nunlock; i++) if (cd->unlock[i].car_hash == hs(inst[j]) && cd->unlock[i].race[col]) break;
        if (i < cd->nunlock) continue;
        ns++;
        strncat(line, " ", sizeof line - strlen(line) - 1);
        strncat(line, inst[j], sizeof line - strlen(line) - 1);
    }
    printf("starters %d:%s\n", ns, line);
    EXPECT(ns == 6 && strstr(line, " PEUGOT ") && strstr(line, " FOCUS") && strstr(line, " COROLLA") &&
           strstr(line, " 240SX") && strstr(line, " PEUGOT106") && strstr(line, " CORSA"), "starters%s", line);
    for (i = 0; i < cd->nunlock; i++) {   /* every unlock race exists and is a URL race */
        int k, c;
        for (c = 0; c < 2; c++) {
            if (!cd->unlock[i].race[c]) continue;
            k = cd_find(cd, cd->unlock[i].race[c]);
            EXPECT(k >= 0 && cd->race[k].category == CD_CAT_URL, "unlock %d col %d race %08x", i, c, cd->unlock[i].race[c]);
        }
    }
}

/* `--sms FILE` writes the parsed 0x34A17 table as a JSON array, one object per record, for diffing against
 * career_dump.py --section sms --json. Every value is a field the parser fills in, nothing recomputed here. */
static void dump_sms(const char *path, const CareerData *cd) {
    FILE *f = fopen(path, "w");
    int i;
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fputs("[\n", f);
    for (i = 0; i < cd->nsms; i++) {
        const CdSms *q = &cd->sms[i];
        fprintf(f, "  {\"idx\": %d, \"name\": \"%s\", \"name_off\": %u, \"trigger\": %u, \"cls\": %u,"
                  " \"flag\": %u, \"kind\": %u, \"arg\": %u, \"weight\": %u, \"sender\": %u}%s\n",
                i, q->name, (unsigned)(q->raw[0] | (q->raw[1] << 8)), q->trigger, q->cls, q->flag, q->kind,
                q->arg, q->weight, q->sender, i + 1 < cd->nsms ? "," : "");
    }
    fputs("]\n", f);
    fclose(f);
}

/* [data]: +0x03 and +0x05 are in exact 1:1 correspondence over the 167 retail records, so one mapping table is
 * enough to state the pairing: tips 02<->09, blurbs 03<->03, messages 04<->01, part notices 05<->05. */
static int cls_matches_kind(const CdSms *q) {
    static const unsigned char pair[6] = { 0, 0, 9, 3, 1, 5 };   /* +0x03 value -> +0x05 value */
    return q->cls <= 5 && q->cls >= 2 && q->kind == pair[q->cls];
}

/* docs/parity/PERF-PAINT.md: performance packages, brands and the unlock table. */
static void check_perf(const CareerData *cd) {
    int cat, lvl, priced = 0;
    EXPECT(cd->nperf_comp == 66, "perf comps %d != 66", cd->nperf_comp);
    EXPECT(cd->nbrand == 35, "brands %d != 35", cd->nbrand);
    EXPECT(cd->nulk == 86, "unlock records %d != 86", cd->nulk);
    for (cat = 0; cat < CD_PERF_CATS; cat++) for (lvl = 0; lvl < CD_PERF_LEVELS; lvl++)
        EXPECT(cd->perf_pkg[cat][lvl].n >= 1 && cd->perf_pkg[cat][lvl].n <= 4, "pkg %d L%d n %d", cat, lvl + 1, cd->perf_pkg[cat][lvl].n);
    EXPECT(!cd->perf_comp[33].valid && !cd->perf_comp[34].valid, "comps 33/34 must be unused");
    EXPECT(cd->perf_comp[28].cat == 4 && cd->perf_comp[28].lvl == 3 && cd->perf_comp[28].price == 8000, "turbo L3 $8000");
    EXPECT(cd->perf_comp[11].cat == 1 && cd->perf_comp[11].price == 5000, "transmission L3 $5000");
    for (int c = 0; c < CD_PERF_COMPS; c++) if (cd->perf_comp[c].valid && cd->perf_comp[c].price > 0) priced++;
    EXPECT(priced == 54, "priced comps %d != 54 (aero cat 9 is free/hidden)", priced);
    {
        int n[CD_PAINT_GROUPS][4] = {{0}};
        EXPECT(cd->npaint == 450, "paints %d != 450", cd->npaint);
        for (int i = 0; i < cd->npaint; i++) n[cd->paint[i].group][cd->paint[i].level & 3]++;
        EXPECT(n[CD_PAINT_GLOSS][1] == 60 && n[CD_PAINT_METAL][2] == 60 && n[CD_PAINT_PEARL][3] == 30, "body paints");
        for (int g = CD_PAINT_RIM; g < CD_PAINT_GROUPS; g++)
            EXPECT(n[g][1] == 10 && n[g][2] == 20 && n[g][3] == 30, "paint group %d tiers", g);
        int k = cd_find_paint(cd, cd_hash("GLOSS_L1_COLOR03"));
        EXPECT(k >= 0 && cd->paint[k].rgb[0] == 8 && cd->paint[k].rgb[1] == 60 && cd->paint[k].rgb[2] == 161, "GLOSS_L1_COLOR03 rgb");
    }
    for (int b = 0; b < cd->nbrand; b++) EXPECT(cd->brand[b].hash == cd_hash(cd->brand[b].key), "brand %d hash", b);
    {
        int u = cd_find_ulk(cd, 1);
        EXPECT(u >= 0 && cd->ulk[u].l[0].type == CD_ULK_START && cd->ulk[u].l[1].type == CD_ULK_WINS &&
               cd->ulk[u].l[1].value == ((3u << 16) | 13u) && cd->ulk[u].l[2].type == CD_ULK_SPONSOR_WINS, "engine unlocks");
        u = cd_find_ulk(cd, 10);
        EXPECT(u >= 0 && cd->ulk[u].l[0].type == CD_ULK_START && cd->ulk[u].l[2].type == CD_ULK_START, "aero always open");
    }
}

/* docs/parity/SMS-TABLE.md: the 167 records of the retail career table, decoded. */
static void check_sms(const CareerData *cd) {
    check_perf(cd);
    int k, kind[4] = { 0, 0, 0, 0 }, self = 0, flagged = 0, weighted = 0, i;
    EXPECT(cd->nsms == 167, "nsms %d != 167", cd->nsms);       /* the 280-byte demo table must not be read */
    for (i = 0; i < cd->nsms; i++) {
        const CdSms *q = &cd->sms[i];
        EXPECT(q->name[0] && q->name_hash == cd_hash(q->name), "sms %d name '%s'", i, q->name);
        EXPECT(q->raw[6] == 0x88 && q->raw[7] == 0 && q->raw[0x0E] == 0 && q->raw[0x0F] == 0, "sms %d constants", i);
        EXPECT(q->sender, "sms %d %s sender 0", i, q->name);
        EXPECT(q->trigger, "sms %d %s trigger 0", i, q->name);
        if (q->kind == CD_SMS_TIP) kind[3]++;
        else if (q->kind == CD_SMS_UNLOCK) kind[2]++;
        else if (q->kind == CD_SMS_MAG) kind[1]++;
        else if (q->kind == CD_SMS_MSG) kind[0]++;
        if (q->arg == cd_hash(q->name)) self++;
        flagged += q->flag;
        weighted += q->weight != 0;
        EXPECT(cls_matches_kind(q), "sms %d %s class %u kind %u", i, q->name, q->cls, q->kind);
        /* [data]: only tips (kind 9) carry a weight, and only 0 / 25 / 50 occur */
        EXPECT(q->weight == 0 || (q->kind == 9 && (q->weight == 25 || q->weight == 50)),
               "sms %d %s weight %u kind %u", i, q->name, q->weight, q->kind);
    }
    EXPECT(self == 42, "self-hash args %d != 42", self);       /* the 42 tips name themselves */
    EXPECT(flagged == 31, "flagged %d != 31", flagged);
    EXPECT(weighted == 42, "weighted %d != 42", weighted);
    EXPECT(kind[0] == 34 && kind[1] == 38 && kind[2] == 53 && kind[3] == 42,
           "kinds msg %d mag %d unlock %d tip %d", kind[0], kind[1], kind[2], kind[3]);
    /* [data]: the first tip is ENGAGE_TIPS_AUDIO (idx 125), the last is a career-progress tip (idx 166) */
    EXPECT(!strcmp(cd->sms[125].name, "ENGAGE_TIPS_AUDIO") && cd->sms[125].kind == CD_SMS_TIP, "sms[125] %s",
           cd->sms[125].name);
    EXPECT(cd->sms[166].kind == CD_SMS_TIP, "sms[166] kind %u", cd->sms[166].kind);
    {   /* [data]: the 4 records whose u32 arg names an entity in another table rather than a pair of u16 args */
        static const struct { const char *sms, *ent; } X[4] = { { "SMS_GPS_INTRO", "CAR_LOT_1" },
            { "SMS_LOC_GO_TO_GARAGE", "CAR_LOT_1" }, { "SMS_LOC_EVENTS_OUT_THERE", "CRIB_1" },
            { "SMS_END_OF_GAME_MESSAGE", "S5_CIRCUIT_36" } };
        for (i = 0; i < 4; i++) {
            k = cd_find_sms(cd, hs(X[i].sms));
            EXPECT(k >= 0 && cd->sms[k].arg == hs(X[i].ent), "%s arg %08x want %s %08x", X[i].sms,
                   k >= 0 ? cd->sms[k].arg : 0, X[i].ent, hs(X[i].ent));
        }
    }
    k = cd_find_sms(cd, hs("SMS_GPS_INTRO"));                  /* the only record flagged in the docs' example */
    EXPECT(k == 7 && cd->sms[k].trigger == 2 && cd->sms[k].flag == 1 && cd->sms[k].kind == CD_SMS_MSG &&
           cd->sms[k].sender == hs("RACHEL"), "SMS_GPS_INTRO fields");
    EXPECT(!strcmp(cd->sms[k].name, "SMS_GPS_INTRO"), "cd_find_sms name");
    EXPECT(cd_find_sms(cd, hs("NOT_A_MESSAGE")) == -1, "cd_find_sms miss");
    EXPECT(cd_find_sms(cd, cd_hash("")) == -1 && cd_find_sms(cd, 0) == -1, "cd_find_sms degenerate");
    {   /* [data]: the low u16 counts unlocks (1..30) and the high u16 names the stage it happened in (2..5 for the
         * car notices, 1..4 for the part notices). No record has a zero half except the 4 entity-hash ones above. */
        int n = 0, lo, hi;
        for (i = 0; i < cd->nsms; i++) {
            const CdSms *q = &cd->sms[i];
            if (q->kind != CD_SMS_UNLOCK) continue;
            lo = CD_SMS_ARG_LO(q->arg); hi = CD_SMS_ARG_HI(q->arg);
            if (lo && hi) n++;
            EXPECT(lo >= 1 && lo <= 30 && hi >= 1 && hi <= 5, "unlock %s args %d/%d", q->name, lo, hi);
        }
        EXPECT(n == 53, "unlock paired args %d != 53", n);
    }
}

int main(int argc, char **argv) {
    static CareerData cd;
    static const int want_req[6] = { 1, 5, 16, 28, 40, 47 };
    static const int want_spon[6] = { 0, 0, 5, 5, 5, 5 };
    struct stat st;
    int i, s, nz = 0, cnt[CD_MAX_STAGES] = { 0 };
    if (argc < 2 || argc > 4) { fprintf(stderr, "usage: career_data_check DATAROOT [--sms FILE]\n"); return 2; }
    {
        char p[1024];
        snprintf(p, sizeof p, "%s/GLOBAL", argv[1]);
        if (stat(p, &st) != 0) { printf("data dir missing: %s\n", argv[1]); return 77; }
    }
    if (!cd_load(&cd, argv[1])) { printf("cd_load failed: %s\n", cd.error); return 1; }
    if (argc == 4 && !strcmp(argv[2], "--sms")) {   /* the agreement dump: JSON only, no other output */
        check_sms(&cd);
        dump_sms(argv[3], &cd);
        printf(fails ? "career_data_check: %d failures\n" : "career_data_check: OK\n", fails);
        return fails ? 1 : 0;
    }
    printf("races %d stages %d zones %d markers %d\n", cd.nrace, cd.nstage, cd.nzone, cd.nmarker);
    EXPECT(cd.nrace == 224, "nrace %d != 224", cd.nrace);
    EXPECT(cd.nstage == 6, "nstage %d != 6", cd.nstage);
    for (i = 0; i < cd.nrace; i++) {
        const CdRace *r = &cd.race[i];
        if (r->stage < CD_MAX_STAGES) cnt[r->stage]++;
        if (r->name_hash != hs(r->name)) EXPECT(0, "name hash mismatch %s", r->name);
        if (r->locator_hash != hs(r->locator)) EXPECT(0, "locator hash mismatch %s", r->locator);
        if (cd_find_zone(&cd, r->locator_hash) >= 0) nz++;
    }
    {   /* distinct race locators with a zone */
        int dist = 0, withz = 0, j;
        for (i = 0; i < cd.nrace; i++) {
            for (j = 0; j < i; j++) if (cd.race[j].locator_hash == cd.race[i].locator_hash) break;
            if (j < i) continue;
            dist++;
            if (cd_find_zone(&cd, cd.race[i].locator_hash) >= 0) withz++;
        }
        printf("distinct locators %d, with zone %d (races with zone %d)\n", dist, withz, nz);
        EXPECT(withz == 116, "zone locators %d != 116", withz);
    }
    for (s = 0; s < cd.nstage && s < 6; s++) {
        const CdStage *g = &cd.stage[s];
        printf("stage %d: index %u sponsors %u req_total %u req_url %u req_dvd %u races %d\n", s, g->index,
               g->nsponsors, g->req_total, g->req_url, g->req_dvd, cnt[s]);
        EXPECT(g->req_total == want_req[s], "stage %d req_total %u", s, g->req_total);
        EXPECT(g->nsponsors == want_spon[s], "stage %d sponsors %u", s, g->nsponsors);
    }
    {
        static const struct { const char *n; int track; } W[5] = { { "STAGE_1_CIRCUIT_1", 4013 },
            { "STAGE_1_SPRINT_1", 4101 }, { "STAGE_1_DRAG_1", 4201 }, { "STAGE_1_DRIFT_1", 4311 }, { "STAGE_1_SHORT_1", 4602 } };
        for (i = 0; i < 5; i++) {
            int k = cd_find(&cd, hs(W[i].n));
            EXPECT(k >= 0 && cd.race[k].heat[0].track == W[i].track, "%s heat0 expected %d", W[i].n, W[i].track);
            if (k >= 0) printf("%s track %u kind %d purse %u stage %u\n", W[i].n, cd.race[k].heat[0].track,
                               cd.race[k].kind, cd.race[k].purse, cd.race[k].stage);
        }
    }
    EXPECT(cd.nmarker > 0, "no markers");
    EXPECT(cd_find_marker(&cd, hs("CRIB_1")) >= 0, "CRIB_1 marker missing");
    {   /* M6 car lot: shops, unlocks, stage 0 story events */
        int lots = 0, a = cd_find(&cd, hs("DDAY_EVENT_A")), bb = cd_find(&cd, hs("DDAY_EVENT_B"));
        printf("shops %d unlocks %d\n", cd.nshop, cd.nunlock);
        EXPECT(cd.nshop == 24, "nshop %d != 24", cd.nshop);
        EXPECT(cd.nunlock == 24, "nunlock %d != 24", cd.nunlock);
        for (i = 0; i < cd.nshop; i++) {
            const CdShop *p = &cd.shop[i];
            int z = cd_find_zone(&cd, p->zone_hash), m = cd_find_marker(&cd, p->hash);
            EXPECT(p->hash == hs(p->name), "shop hash %s", p->name);
            if (p->type == CD_SHOP_CAR_LOT) lots++;
            if (p->type == CD_SHOP_CAR_LOT || p->type == CD_SHOP_CRIB) {
                int in = 0;
                if (z >= 0 && m >= 0) {   /* marker inside the zone bbox */
                    const CdZone *q = &cd.zone[z];
                    float x0 = q->poly[0][0], x1 = x0, y0 = q->poly[0][1], y1 = y0;
                    int k;
                    for (k = 1; k < 4; k++) {
                        if (q->poly[k][0] < x0) x0 = q->poly[k][0];
                        if (q->poly[k][0] > x1) x1 = q->poly[k][0];
                        if (q->poly[k][1] < y0) y0 = q->poly[k][1];
                        if (q->poly[k][1] > y1) y1 = q->poly[k][1];
                    }
                    in = cd.marker[m].x >= x0 && cd.marker[m].x <= x1 && cd.marker[m].y >= y0 && cd.marker[m].y <= y1;
                }
                printf("shop %-14s type %u stage %u known %u %s zone %s marker %s%s\n", p->name, p->type, p->open_stage,
                       p->known, p->platform, z >= 0 ? "yes" : "NO", m >= 0 ? "yes" : "NO", in ? " (inside)" : "");
                EXPECT(z >= 0 && m >= 0 && in, "shop %s zone %d marker %d inside %d", p->name, z, m, in);
            }
        }
        EXPECT(lots == 3, "car lots %d != 3", lots);
        EXPECT(cd.stage[0].final_race == hs("DDAY_EVENT_B"), "stage 0 final %08x", cd.stage[0].final_race);
        EXPECT(a >= 0 && bb >= 0, "DDAY events missing");
        if (a >= 0 && bb >= 0) {
            int sa = cd_find_shop(&cd, cd.race[a].shop_hash), sb = cd_find_shop(&cd, cd.race[bb].shop_hash);
            printf("DDAY_A type %u preset %08x shop %s; DDAY_B unlock %08x shop %s\n", cd.race[a].type,
                   cd.race[a].preset_hash, sa >= 0 ? cd.shop[sa].name : "?", cd.race[bb].unlock_arg,
                   sb >= 0 ? cd.shop[sb].name : "?");
            EXPECT(sa >= 0 && !strcmp(cd.shop[sa].name, "CC_CAR_LOT_1"), "DDAY_A shop");
            EXPECT(sb >= 0 && !strcmp(cd.shop[sb].name, "CRIB_1"), "DDAY_B shop");
            EXPECT(cd.race[a].type == CD_TYPE_SCRIPTED && cd.race[bb].type == CD_TYPE_SCRIPTED, "DDAY types");
            EXPECT(cd.race[a].preset_hash == hs("DDAY_PLAYER_CAR"), "DDAY_A preset");
            EXPECT(cd.race[bb].unlock_arg == cd.race[a].name_hash, "DDAY_B unlock arg");
        }
        check_lot(&cd, argv[1]);
    }
    check_sms(&cd);
    printf(fails ? "career_data_check: %d failures\n" : "career_data_check: OK\n", fails);
    return fails ? 1 : 0;
}
