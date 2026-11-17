/* career_host_check -- drives a new career through Stage 0 (car lot, crib) and Stage 1 via career_host.h on the
 * installed data (M6). Usage: career_host_check DATAROOT TMPDIR. Exit 0 ok, 1 failure, 77 data missing (SKIP).
 * The game catalogue is a small stand-in (event id -> region; cars scanned from DATAROOT/CARS like gcat_build) so this
 * check does not link the world code; the in-game scenario covers the real catalogue. Prints no retail bytes beyond
 * names and counts. */
#include "career_host.h"
#include "settings.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void add_ev(GameCatalog *c, int id, const char *reg)
{
    GcEvent *e = &c->ev[c->nev++];
    memset(e, 0, sizeof *e); e->id = id; snprintf(e->region, sizeof e->region, "%s", reg);
}

/* gcat_build's car list: CARS/<name>/GEOMETRY.BIN, not a parts folder; traffic = empty TEXTURES.BIN */
static void add_cars(GameCatalog *c, const char *root)
{
    static const char *const parts[] = { "AUDIO", "BRAKES", "EXHAUST", "PLATES", "ROOF", "WHEELS", "SPOILER",
        "SPOILER_HATCH", "SPOILER_SUV", "MIRRORS_BODY", "MIRRORS_HUMMER", "MIRRORS_POST", "MIRRORS_SUV" };
    char p[1100];
    snprintf(p, sizeof p, "%s/CARS", root);
    DIR *d = opendir(p);
    struct dirent *e;
    while (d && (e = readdir(d)) && c->ncar < 64) {
        struct stat g, t;
        char q[1400];
        unsigned k;
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31) continue;
        for (k = 0; k < sizeof parts / sizeof parts[0]; k++) if (!strcmp(e->d_name, parts[k])) break;
        if (k < sizeof parts / sizeof parts[0]) continue;
        snprintf(q, sizeof q, "%s/%s/GEOMETRY.BIN", p, e->d_name);
        if (stat(q, &g) != 0) continue;
        snprintf(q, sizeof q, "%s/%s/TEXTURES.BIN", p, e->d_name);
        GcCar *car = &c->car[c->ncar++];
        memcpy(car->name, e->d_name, strlen(e->d_name) + 1);
        car->traffic = stat(q, &t) == 0 && t.st_size == 0;
    }
    if (d) closedir(d);
}

static int marker_of(const CareerHost *ch, const char *name, float *x, float *y)
{
    uint32_t h = 0xFFFFFFFFu;
    for (const char *q = name; *q; q++) h = h * 33u + (uint8_t)*q;
    int m = cd_find_marker(&ch->cd, h);
    if (m < 0) return 0;
    *x = ch->cd.marker[m].x; *y = ch->cd.marker[m].y;
    return 1;
}

static int zone_of(const CareerHost *ch, int race, float *x, float *y)
{
    int z = cd_find_zone(&ch->cd, ch->cd.race[race].locator_hash);
    if (z < 0) return 0;
    const CdZone *q = &ch->cd.zone[z];
    *x = 0.25f * (q->poly[0][0] + q->poly[1][0] + q->poly[2][0] + q->poly[3][0]);
    *y = 0.25f * (q->poly[0][1] + q->poly[1][1] + q->poly[2][1] + q->poly[3][1]);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: career_host_check DATAROOT TMPDIR\n"); return 2; }
    static CareerHost ch;
    static GameCatalog cat;
    add_ev(&cat, 4013, "L4RA"); add_ev(&cat, 4101, "L4RA"); add_ev(&cat, 4201, "L4RB");
    add_ev(&cat, 4311, "L4RC"); add_ev(&cat, 4602, "L4RF");
    add_cars(&cat, argv[1]);
    if (!ch_init(&ch, argv[1], &cat)) { printf("SKIP: %s\n", ch.last); return 77; }
    printf("%s\n", ch.last);
    CHECK(ch.cd.nshop == 24 && ch.cd.nunlock == 24, "shops %d unlocks %d", ch.cd.nshop, ch.cd.nunlock);
    char prof[512]; snprintf(prof, sizeof prof, "%s/CAREERT", argv[2]); mkdir(prof, 0700);
    { char f[640]; snprintf(f, sizeof f, "%s/%s", prof, CR_FILE_NAME); remove(f); }   /* start without a save */

    GfLaunch gl;
    CHECK(!ch_begin(&ch, 0, argv[2], "CAREERT", "GOLF", &gl), "continue without a save must fail");
    CHECK(ch_begin(&ch, 1, argv[2], "CAREERT", "GOLF", &gl) == CH_BEGIN_LOT, "new career: %s", ch.last);
    printf("%s\n", ch.last);
    CHECK(gl.free_run == 1 && !strcmp(gl.track, CH_CITY_TRACK), "free roam launch %d %s", gl.free_run, gl.track);
    CHECK(ch.st.stage == 0 && ch.st.region == 1 && ch.st.nowned == 0 && ch.st.bank == 0, "stage 0 region B, no car");
    CHECK(ch.at_shop >= 0 && !strcmp(ch.cd.shop[ch.at_shop].name, "CC_CAR_LOT_1"), "lot opens at CC_CAR_LOT_1");
    HudMapIcon ic[64];
    float lx, ly, cx, cy, sp[4];
    CHECK(marker_of(&ch, "CC_CAR_LOT_1", &lx, &ly) && marker_of(&ch, "CRIB_1", &cx, &cy), "lot / crib markers");
    CHECK(ch_take_spawn(&ch, sp) && sp[0] == lx && sp[1] == ly, "new career spawns at the lot marker");

    /* Stage 0: the car lot (must buy), free choice of the 6 starters at $0 */
    CrLotCar lot[64];
    int nlot = ch_lot_list(&ch, lot, 64);
    char line[512];
    printf("%s\n", ch_lot_line(lot, nlot, line, sizeof line));
    CHECK(nlot == 6, "lot cars %d (want 6)", nlot);
    {
        static const char *const want[6] = { "PEUGOT", "FOCUS", "COROLLA", "240SX", "PEUGOT106", "CORSA" };
        for (int w = 0; w < 6; w++) {
            int f = 0;
            for (int k = 0; k < nlot; k++) if (!strcmp(lot[k].name, want[w]) && lot[k].starter && lot[k].price == 0) f = 1;
            CHECK(f, "starter %s on the lot", want[w]);
        }
    }
    CHECK(!ch_lot_buy(&ch, "SKYLINE", &gl) && ch.lot_rc == CR_LOT_E_LOCKED, "locked car refused (%s)", ch.last);
    CHECK(!ch_lot_buy(&ch, "BUS", &gl) && ch.lot_rc == CR_LOT_E_ARG, "traffic car refused");
    CHECK(ch_lot_buy(&ch, "FOCUS", &gl), "buy FOCUS: %s", ch.last);
    printf("%s\n", ch.last);
    CHECK(!strncmp(ch.last, "career: bought FOCUS", 20), "bought log");
    CHECK(gl.free_run && !strcmp(gl.car, "FOCUS") && !strcmp(ch.st.car, "FOCUS") && cr_owns(&ch.st, "FOCUS"), "now FOCUS");
    CHECK(ch_take_spawn(&ch, sp) && sp[0] == lx && sp[1] == ly, "spawn at the lot marker after buying");
    CHECK(ch.st.stage == 0, "still stage 0 after the lot (stage %d)", ch.st.stage);
    CHECK(!ch_lot_buy(&ch, "FOCUS", &gl) && ch.lot_rc == CR_LOT_E_SAME, "same car refused");
    nlot = ch_lot_list(&ch, lot, 64);
    printf("%s\n", ch_lot_line(lot, nlot, line, sizeof line));
    int nicon = ch_icons(&ch, ic, 64), nshopicon = 0;
    for (int k = 0; k < nicon; k++) nshopicon += ic[k].kind == HMI_SHOP;
    printf("stage 0: %d icons (%d shops)\n", nicon, nshopicon);
    CHECK(nshopicon == 2, "stage 0 shop icons %d (lot + crib)", nshopicon);
    /* [data] the CC_CAR_LOT_1 marker lies on its zone's edge (inside the bbox, see career_data_check), so the zone is
     * probed at its centre; the CRIB_1 marker is well inside its zone. */
    int lz = cd_find_zone(&ch.cd, ch.cd.shop[ch.at_shop].zone_hash);
    float zx = 0.0f, zy = 0.0f;
    if (lz >= 0)
        for (int k = 0; k < 4; k++) { zx += 0.25f * ch.cd.zone[lz].poly[k][0]; zy += 0.25f * ch.cd.zone[lz].poly[k][1]; }
    int lotidx = ch_shop_zone(&ch, zx, zy, 0.0f), cribidx = ch_shop_zone(&ch, cx, cy, 0.0f);
    CHECK(lz >= 0 && lotidx >= 0 && !strcmp(ch.cd.shop[lotidx].name, "CC_CAR_LOT_1"), "lot zone hit");
    CHECK(fabsf(zx - lx) < 10.0f && fabsf(zy - ly) < 10.0f, "lot marker near its zone (%.1f %.1f vs %.1f %.1f)", lx, ly, zx, zy);
    CHECK(cribidx >= 0 && !strcmp(ch.cd.shop[cribidx].name, "CRIB_1"), "crib marker inside its zone");
    CHECK(ch_shop_zone(&ch, cx, cy, 80.0f) < 0, "too fast for a shop");
    {
        char buf[96];
        CHECK(ch_prompt(&ch, cx, cy, 0.0f, NULL, buf, sizeof buf) && strstr(buf, "CRIB_1"), "crib prompt");
    }
    /* optional stage-0 races must not end stage 0 */
    {
        int l0[CD_MAX_RACES], n0 = cr_list(&ch.st, &ch.cd, l0, CD_MAX_RACES);
        printf("stage 0 optional races: %d\n", n0);
        for (int k = 0; k < n0; k++) {
            CrResult rr; CrOutcome oo; memset(&rr, 0, sizeof rr);
            rr.race_hash = ch.cd.race[l0[k]].name_hash; rr.place = 1; rr.ncars = 4;
            CHECK(cr_apply(&ch.st, &ch.cd, &rr, NULL, NULL, &oo) && ch.st.stage == 0, "optional win keeps stage 0");
        }
    }
    ch.at_shop = cribidx;
    CHECK(ch_crib_done(&ch, &gl), "crib");
    printf("%s\n", ch.last);
    CHECK(!strcmp(ch.last, "career: stage 0 -> 1") && ch.st.stage == 1 && ch.st.intro_done, "stage 0 -> 1 at the crib");
    CHECK(gl.free_run && !strcmp(gl.car, "FOCUS") && ch_take_spawn(&ch, sp) && sp[0] == cx && sp[1] == cy, "crib spawn");
    CHECK(ch_crib_done(&ch, &gl) && ch.st.stage == 1, "second crib visit changes nothing");
    nicon = ch_icons(&ch, ic, 64);
    nshopicon = 0;
    for (int k = 0; k < nicon; k++) nshopicon += ic[k].kind == HMI_SHOP;
    CHECK(nicon - nshopicon == 5, "stage 1 event icons %d (want 5)", nicon - nshopicon);

    CHECK(nshopicon == 8, "stage 1 open shop icons %d",nshopicon);
    /* All retail stage boundaries, including the normally unreachable uniques shop. */
    {
        const int counts[]={2,8,13,15,20,23};
        HudMapIcon all[CD_MAX_RACES+CD_MAX_SHOPS];
        int saved_stage=ch.st.stage;
        for(int stage=0;stage<6;stage++) {
            ch.st.stage=(uint8_t)stage;
            int total=ch_icons(&ch,all,CD_MAX_RACES+CD_MAX_SHOPS),shops=0;
            for(int i=0;i<total;i++)if(all[i].kind==HMI_SHOP)shops++;
            CHECK(shops==counts[stage],"stage %d shop icons %d expected %d",stage,shops,counts[stage]);
        }
        ch.st.stage=(uint8_t)saved_stage;
    }
    int body = cd_find_shop(&ch.cd,cd_hash("CC_BODYSHOP_1"));
    ch.at_shop = body;
    GarageConfig installed = {0};installed.car.body_kit=1;
    CHECK(ch_shop_install(&ch,"FOCUS",&installed)==ST_OK,"body installation saved");
    CHECK(ch_shop_install(&ch,"OTHER",&installed)==ST_E_ARG,"wrong car refused");
    installed.car.parts[N2_PART_AUDIO]=1;
    CHECK(ch_shop_install(&ch,"FOCUS",&installed)==ST_E_ARG,"body cannot install specialty part");
    CareerState loaded;
    CHECK(cr_load_file(argv[2],"CAREERT",&loaded)==ST_OK&&loaded.config[0].car.body_kit==1,"installation survives load");
    char saved_root[256];snprintf(saved_root,sizeof saved_root,"%s",ch.root);
    snprintf(ch.root,sizeof ch.root,"%s/nonexistent",argv[2]);
    installed=ch.st.config[0];installed.car.body_kit=2;
    CHECK(ch_shop_install(&ch,"FOCUS",&installed)!=ST_OK&&ch.st.config[0].car.body_kit==1,"save failure rolls back state");
    snprintf(ch.root,sizeof ch.root,"%s",saved_root);

    int list[CD_MAX_RACES], n = cr_list(&ch.st, &ch.cd, list, CD_MAX_RACES), done = 0;
    CHECK(n == 5, "stage 1 available %d", n);
    for (int k = 0; k < n; k++) {
        int r = list[k]; float x, y; char buf[96];
        CHECK(zone_of(&ch, r, &x, &y), "zone for %s", ch.cd.race[r].name);
        CHECK(ch_zone(&ch, x, y, 50.0f) < 0, "too fast must not engage");
        CHECK(ch_prompt(&ch, x, y, 0.0f, NULL, buf, sizeof buf) != NULL, "prompt in zone %s", ch.cd.race[r].name);
        CHECK(ch_prompt(&ch, x + 500.0f, y + 500.0f, 0.0f, NULL, buf, sizeof buf) == NULL, "no prompt far away");
        int e = ch_engage(&ch, x, y, 1.0f, 0.0f, "GOLF", &gl);
        CHECK(e == r, "engage %s -> %d (%s)", ch.cd.race[r].name, e, ch.last);
        printf("%s\n", ch.last);
        CHECK(!gl.free_run && gl.event_id == ch.cd.race[r].heat[0].track, "launch event %d", gl.event_id);
        CHECK(ch_icons(&ch, ic, 64) == 0, "no icons while racing");
        int64_t bank = ch.st.bank;
        if (k == 0) {   /* first attempt lost: no pay, still available */
            CHECK(ch_on_result(&ch, 3, 4, 0, 0, 100.0), "apply loss");
            CHECK(ch.st.bank == bank, "loss pays nothing");
            CHECK(!ch_on_result(&ch, 1, 4, 0, 0, 100.0), "second apply ignored");
            CHECK(ch_return(&ch, "GOLF", &gl) && gl.free_run, "return");
            float sp[4]; CHECK(ch_take_spawn(&ch, sp) && sp[0] == x && sp[1] == y, "spawn at engage pose");
            e = ch_engage(&ch, x, y, 1.0f, 0.0f, "GOLF", &gl);
            CHECK(e == r, "re-engage after loss");
        }
        CHECK(ch_on_result(&ch, 1, 4, 0, 0, 90.0), "apply win");
        printf("%s\n", ch.last);
        CHECK(ch.st.bank == bank + (int64_t)ch.cd.race[r].purse, "win pays purse %u", ch.cd.race[r].purse);
        CHECK(ch_return(&ch, "GOLF", &gl), "return");
        done++;
    }
    CHECK(done == 5, "five races");
    CHECK(ch.st.stage == 2, "stage 2 after five wins (stage %d)", ch.st.stage);
    nicon = ch_icons(&ch, ic, 64);
    printf("stage %d: %d icons, bank %lld rep %u\n", ch.st.stage, nicon, (long long)ch.st.bank, ch.st.rep);
    CHECK(nicon > 0, "stage 2 has events");
    ch_suspend(&ch, 10.0f, 20.0f, 0.0f, 0.5f);

    static CareerHost again;
    CHECK(ch_init(&again, argv[1], &cat), "reinit");
    CHECK(ch_begin(&again, 0, argv[2], "CAREERT", "COROLLA", &gl), "continue: %s", again.last);
    printf("%s\n", again.last);
    CHECK(again.st.stage == 2 && again.st.bank == ch.st.bank, "continue restores stage/bank");
    CHECK(!strcmp(gl.car, "GOLF"), "continue uses career car (%s)", gl.car);
    CHECK(again.st.nowned == 1 && !strcmp(again.st.owned[0], "FOCUS") && again.st.region == 1, "owned car kept");
    CHECK(ch_take_spawn(&again, sp) && sp[0] == 10.0f && sp[1] == 20.0f, "continue spawns at saved pose");

    /* a Stage 0 career saved before the lot choice continues into the lot */
    { char f[640]; snprintf(f, sizeof f, "%s/CAREER0", argv[2]); mkdir(f, 0700);
      snprintf(f, sizeof f, "%s/CAREER0/%s", argv[2], CR_FILE_NAME); remove(f); }
    static CareerHost z;
    CHECK(ch_init(&z, argv[1], &cat), "reinit 0");
    CHECK(ch_begin(&z, 1, argv[2], "CAREER0", "GOLF", &gl) == CH_BEGIN_LOT, "new 0");
    ch_suspend(&z, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(ch_begin(&z, 0, argv[2], "CAREER0", "GOLF", &gl) == CH_BEGIN_LOT, "continue stage 0 without a car -> lot (%s)", z.last);
    CHECK(ch_lot_buy(&z, "PEUGOT", &gl), "buy PEUGOT");
    ch_suspend(&z, 1.0f, 2.0f, 0.0f, 0.0f);
    CHECK(ch_begin(&z, 0, argv[2], "CAREER0", "GOLF", &gl) == CH_BEGIN_ROAM && !strcmp(gl.car, "PEUGOT") && z.st.stage == 0,
          "continue stage 0 with a car -> roam in it");

    printf("career_host_check: %d checks, %d failures\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
