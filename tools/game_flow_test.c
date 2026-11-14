/* game_flow_test -- drives gf_step with a fake host. No SDL, no data. */
#include <stdio.h>
#include <string.h>
#include "../src/game_flow.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static GfHost host(const char *track, const char *car) {
    GfHost h;
    memset(&h, 0, sizeof h);
    h.track = track; h.car = car; h.race_state = 1;
    return h;
}
static GfLaunch launch(int fr, int ev, const char *t, const char *c) {
    GfLaunch l;
    memset(&l, 0, sizeof l);
    l.free_run = fr; l.event_id = ev; l.laps = 3; l.difficulty = 1;
    strcpy(l.track, t); strcpy(l.car, c);
    return l;
}

static void test_free_run(void) {
    GameFlow g; GfHost h = host("L4RA", "CARX"); GfLaunch l = launch(1, 0, "L4RA", "CARX"); GfCommand c;
    gf_init(&g, 1);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);
    gf_start(&g, &l);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_SHOW_LOADING);
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_CLOSE_MENU);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);
    /* with a race active, it is stopped first */
    h.race_active = 1;
    gf_start(&g, &l);
    gf_step(&g, &h, 0.016f);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_STOP_RACE);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_CLOSE_MENU);
}

static void test_event_switch(void) {
    GameFlow g; GfHost h = host("L4RA", "OLD"); GfLaunch l = launch(0, 42, "L4RB", "NEW"); GfCommand c;
    gf_init(&g, 1);
    gf_start(&g, &l);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_SHOW_LOADING);
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_SWITCH_TRACK && strcmp(c.name, "L4RB") == 0);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);  /* waiting */
    h.track = "L4RB";
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_SWITCH_CAR && strcmp(c.name, "NEW") == 0);
    h.car = "NEW";
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_ARM_EVENT && c.event_id == 42 && c.laps == 3 && c.difficulty == 1);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_CLOSE_MENU);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);
    CHECK(g.state == GF_WAIT_RACE);
    h.race_active = 1; h.race_event_id = 42;
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);
    CHECK(g.state == GF_RUNNING);
    /* race end held 3.0 s -> results */
    h.race_state = 2;
    {
        int n = 0, got = 0;
        for (n = 0; n < 400 && !got; n++) {
            c = gf_step(&g, &h, 0.01f);
            if (c.kind == GF_CMD_OPEN_RESULTS) got = n + 1;
        }
        CHECK(got >= 299 && got <= 301);
    }
    CHECK(gf_step(&g, &h, 0.01f).kind == GF_CMD_NONE);
}

static void test_race_end_interrupted(void) {
    GameFlow g; GfHost h = host("L4RA", "C"); GfLaunch l = launch(0, 5, "L4RA", "C");
    int i;
    gf_init(&g, 1); gf_start(&g, &l);
    h.race_active = 1;
    for (i = 0; i < 6; i++) gf_step(&g, &h, 0.016f);
    CHECK(g.state == GF_RUNNING);
    h.race_state = 2;
    for (i = 0; i < 100; i++) CHECK(gf_step(&g, &h, 0.02f).kind == GF_CMD_NONE); /* 2.0 s */
    h.race_state = 1;
    gf_step(&g, &h, 0.02f);
    h.race_state = 2;
    for (i = 0; i < 100; i++) CHECK(gf_step(&g, &h, 0.02f).kind == GF_CMD_NONE); /* timer restarted */
}

static void test_switch_never(void) {
    GameFlow g; GfHost h = host("L4RA", "C"); GfLaunch l = launch(0, 7, "L4RB", "C"); GfCommand c;
    int i, err = -1;
    gf_init(&g, 1); gf_start(&g, &l);
    gf_step(&g, &h, 0.016f);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_SWITCH_TRACK);
    for (i = 1; i <= 5; i++) {
        c = gf_step(&g, &h, 0.016f);
        if (c.kind == GF_CMD_ERROR) { err = i; break; }
        CHECK(c.kind == GF_CMD_NONE);
    }
    CHECK(err == 3);
    CHECK(c.message[0] != 0);
    CHECK(g.state == GF_QR_TRACK);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);
    /* car switch that never takes effect */
    h = host("L4RB", "OLD"); l = launch(0, 7, "L4RB", "NEWCAR");
    gf_start(&g, &l);
    gf_step(&g, &h, 0.016f);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_SWITCH_CAR);
    err = -1;
    for (i = 1; i <= 5; i++) { c = gf_step(&g, &h, 0.016f); if (c.kind == GF_CMD_ERROR) { err = i; break; } }
    CHECK(err == 3);
}

static void test_race_rejected(void) {
    GameFlow g; GfHost h = host("L4RA", "C"); GfLaunch l = launch(0, 9, "L4RA", "C"); GfCommand c;
    gf_init(&g, 1); gf_start(&g, &l);
    gf_step(&g, &h, 0.016f);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_ARM_EVENT);
    h.race_error = "no route for event";
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_ERROR && strcmp(c.message, "no route for event") == 0);
    CHECK(g.state == GF_QR_TRACK);
    /* rejection noticed after CLOSE_MENU too */
    h.race_error = NULL;
    gf_start(&g, &l);
    gf_step(&g, &h, 0.016f);
    gf_step(&g, &h, 0.016f);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_CLOSE_MENU);
    h.race_error = "late reject";
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_ERROR);
    CHECK(g.state == GF_QR_TRACK);
}

static void test_pause(void) {
    GameFlow g; GfHost h = host("L4RA", "C"); GfLaunch l = launch(0, 9, "L4RA", "C"); GfCommand c;
    PauseContext pc; PauseEntry pe[PE_MAX]; int n, i, has_resume = 0, has_restart = 0, has_free = 0;
    gf_init(&g, 1); gf_start(&g, &l);
    h.race_active = 1;
    for (i = 0; i < 6; i++) gf_step(&g, &h, 0.016f);

    CHECK(gf_pause_action(&g, FE_ACTION_RESUME) == 1);
    CHECK(gf_pause_action(&g, FE_ACTION_REPLAY) == 0);
    CHECK(gf_pause_action(&g, FE_ACTION_NONE) == 0);
    CHECK(gf_pause_action(&g, FE_ACTION_FREE_ROAM) == 0);

    CHECK(gf_pause_action(&g, FE_ACTION_RESTART) == 1);
    c = gf_step(&g, &h, 0.016f);
    CHECK(c.kind == GF_CMD_ARM_EVENT && c.event_id == 9 && c.laps == 3);

    h.race_error = NULL;
    CHECK(gf_pause_action(&g, FE_ACTION_QUIT) == 1);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_OPEN_MAIN);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_STOP_RACE);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);

    /* disabled */
    gf_init(&g, 0);
    CHECK(gf_pause_action(&g, FE_ACTION_QUIT) == 0);
    CHECK(gf_pause_action(&g, FE_ACTION_RESTART) == 0);
    CHECK(gf_pause_action(&g, FE_ACTION_RESUME) == 0);
    gf_start(&g, &l);
    CHECK(gf_step(&g, &h, 0.016f).kind == GF_CMD_NONE);

    /* contexts */
    gf_init(&g, 1);
    gf_pause_context(&g, 1, &pc);
    CHECK(pc.mode == 0 && pc.race == 1);
    n = pe_build(&pc, pe);
    for (i = 0; i < n; i++) {
        if (pe[i].id == PE_RESUME_RACE) has_resume = 1;
        if (pe[i].id == PE_RESTART) has_restart = 1;
    }
    CHECK(n > 0 && has_resume && has_restart);
    gf_pause_context(&g, 0, &pc);
    CHECK(pc.mode == 0 && pc.race == 0);
    n = pe_build(&pc, pe);
    for (i = 0; i < n; i++) if (pe[i].id == PE_FREE_RUN && pe[i].label_key == 0xC44A6515u) has_free = 1;
    CHECK(has_free);
}

int main(void) {
    test_free_run();
    test_event_switch();
    test_race_end_interrupted();
    test_switch_never();
    test_race_rejected();
    test_pause();
    printf("game_flow_test: %d checks, %d failures\n", checks, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
