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
    for (i = 0; i < a->nrec; i++)
        if (a->rec[i].hash != b->rec[i].hash || a->rec[i].best_place != b->rec[i].best_place ||
            a->rec[i].flags != b->rec[i].flags || a->rec[i].attempts != b->rec[i].attempts) return 0;
    return 1;
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
            memcpy(a2, v.a, 8); a2[4] = 2;
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

    printf("career_test: %d checks, %d failures\n", checks, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails != 0;
}
