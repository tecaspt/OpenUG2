/* career_data_check DATAROOT -- loads the retail career database and verifies the expected totals. */
#include "career_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t hs(const char *s) { uint32_t h = 0xFFFFFFFFu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }

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
    printf(fails ? "career_data_check: %d failures\n" : "career_data_check: OK\n", fails);
    return fails ? 1 : 0;
}
