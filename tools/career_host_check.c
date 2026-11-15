/* career_host_check -- drives a whole Stage 1 career through career_host.h on the installed data (M6 S1).
 * Usage: career_host_check DATAROOT TMPDIR. Exit 0 ok, 1 failure, 77 data missing (SKIP).
 * The game catalogue is a small hand-made stand-in (event id -> region) so this check does not link the world code;
 * the in-game scenario covers the real catalogue. Prints no retail bytes beyond names and counts. */
#include "career_host.h"
#include "settings.h"
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
    if (!ch_init(&ch, argv[1], &cat)) { printf("SKIP: %s\n", ch.last); return 77; }
    printf("%s\n", ch.last);
    char prof[512]; snprintf(prof, sizeof prof, "%s/CAREERT", argv[2]); mkdir(prof, 0700);
    { char f[640]; snprintf(f, sizeof f, "%s/%s", prof, CR_FILE_NAME); remove(f); }   /* start without a save */

    GfLaunch gl;
    CHECK(!ch_begin(&ch, 0, argv[2], "CAREERT", "GOLF", &gl), "continue without a save must fail");
    CHECK(ch_begin(&ch, 1, argv[2], "CAREERT", "GOLF", &gl), "new career: %s", ch.last);
    printf("%s\n", ch.last);
    CHECK(gl.free_run == 1 && !strcmp(gl.track, CH_CITY_TRACK), "free roam launch %d %s", gl.free_run, gl.track);
    HudMapIcon ic[64];
    int nicon = ch_icons(&ch, ic, 64);
    CHECK(nicon == 5, "stage 1 icons %d (want 5)", nicon);

    int list[CD_MAX_RACES], n = cr_list(&ch.st, &ch.cd, list, CD_MAX_RACES), done = 0;
    CHECK(n == 5, "stage 1 available %d", n);
    for (int k = 0; k < n; k++) {
        int r = list[k]; float x, y; char buf[96];
        CHECK(zone_of(&ch, r, &x, &y), "zone for %s", ch.cd.race[r].name);
        CHECK(ch_zone(&ch, x, y, 50.0f) < 0, "too fast must not engage");
        CHECK(ch_prompt(&ch, x, y, 0.0f, buf, sizeof buf) != NULL, "prompt in zone %s", ch.cd.race[r].name);
        CHECK(ch_prompt(&ch, x + 500.0f, y + 500.0f, 0.0f, buf, sizeof buf) == NULL, "no prompt far away");
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
    float sp[4]; CHECK(ch_take_spawn(&again, sp) && sp[0] == 10.0f && sp[1] == 20.0f, "continue spawns at saved pose");

    printf("career_host_check: %d checks, %d failures\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
