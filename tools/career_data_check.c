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

int main(int argc, char **argv) {
    static CareerData cd;
    static const int want_req[6] = { 1, 5, 16, 28, 40, 47 };
    static const int want_spon[6] = { 0, 0, 5, 5, 5, 5 };
    struct stat st;
    int i, s, nz = 0, cnt[CD_MAX_STAGES] = { 0 };
    if (argc < 2) { fprintf(stderr, "usage: career_data_check DATAROOT\n"); return 2; }
    {
        char p[1024];
        snprintf(p, sizeof p, "%s/GLOBAL", argv[1]);
        if (stat(p, &st) != 0) { printf("data dir missing: %s\n", argv[1]); return 77; }
    }
    if (!cd_load(&cd, argv[1])) { printf("cd_load failed: %s\n", cd.error); return 1; }
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
    printf(fails ? "career_data_check: %d failures\n" : "career_data_check: OK\n", fails);
    return fails ? 1 : 0;
}
