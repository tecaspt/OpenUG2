/* fe_screen_test.c -- pure logic tests of the front-end carousel: entry tables (fe_entries.c) and the
 * carousel screen state machine (fe_screen.c). Synthetic data only, no install, no GL/SDL. */
#include <stdio.h>
#include <string.h>

#include "../src/fe_entries.h"
#include "../src/fe_screen.h"
#include "../src/race.h"
#include "../src/ugui/ugui_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void run_exit(FeCarouselScreen *s) {          /* the host reports the authored exit finished */
    fes_update(s, 0.4f);
    fes_exit_complete(s);
}

static void test_entries(void) {
    static const char *const icon[] = { "MAIN_ICON_CAREER", "MAIN_ICON_QUICK_RACE", "MAIN_ICON_GO_ONLINE", "MAIN_ICON_LAN",
                                        "MAIN_ICON_CUSTOMIZE_CAR", "MAIN_ICON_PROFILE_MANAG", "MAIN_ICON_OPTIONS" };
    static const char *const label[] = { "UI_MAIN_CAREER", "UI_MAIN_QUICK_RACE", "UI_MAIN_ONLINE", "UI_MAIN_ONLINE_LAN",
                                         "UI_MAIN_CUSTOMIZE", "UI_MAIN_PROFILE", "UI_MAIN_OPTIONS" };
    FeEntry e[GC_MAX];
    int n = fe_main_entries(e, 0xFFFFu);
    CHECK(n == 7);
    for (int i = 0; i < 7; i++) {
        CHECK(e[i].id == i);
        CHECK(e[i].icon_key == ug_hash_upper(icon[i]));
        CHECK(e[i].label_key == ug_hash_upper(label[i]));
        CHECK(!strcmp(e[i].icon_name, icon[i]));
        CHECK(e[i].label && e[i].label[0]);
    }
    CHECK(e[FE_MAIN_GO_ONLINE].disabled && e[FE_MAIN_LAN].disabled);       /* never enabled, even by the mask */
    CHECK(!e[FE_MAIN_CAREER].disabled && !e[FE_MAIN_QUICK_RACE].disabled && !e[FE_MAIN_OPTIONS].disabled);
    n = fe_main_entries(e, 1u << FE_MAIN_QUICK_RACE);
    CHECK(n == 7);
    for (int i = 0; i < 7; i++) CHECK(e[i].disabled == (i != FE_MAIN_QUICK_RACE));
    fe_main_entries(e, 0);
    for (int i = 0; i < 7; i++) CHECK(e[i].disabled);
    CHECK(fe_main_entries(NULL, 0) == 0);

    static const char *const micon[] = { "TRACK_ICON_CIRCUIT", "TRACK_ICON_SPRINT", "TRACK_ICON_DRAG", "TRACK_ICON_DRIFT",
                                         "TRACK_ICON_SHORT_TRACK", "TRACK_ICON_U_RACING_LEA", "TRACK_ICON_FREE_RUN" };
    static const char *const mlabel[] = { "QR_CIRCUIT", "QR_SPRINT", "QR_DRAG", "QR_DRIFT", "QR_STREET_CROSS", "QR_URL", "QR_FREE_RUN" };
    n = fe_mode_entries(e);
    CHECK(n == 7);
    for (int i = 0; i < 7; i++) {
        CHECK(e[i].id == i && !e[i].disabled);
        CHECK(e[i].icon_key == ug_hash_upper(micon[i]));
        CHECK(e[i].label_key == ug_hash_upper(mlabel[i]));
    }
    CHECK(fe_mode_race_kind(FE_MODE_CIRCUIT) == N2_RACE_CIRCUIT);
    CHECK(fe_mode_race_kind(FE_MODE_SPRINT) == N2_RACE_SPRINT);
    CHECK(fe_mode_race_kind(FE_MODE_DRAG) == N2_RACE_DRAG);
    CHECK(fe_mode_race_kind(FE_MODE_DRIFT) == N2_RACE_DRIFT);
    CHECK(fe_mode_race_kind(FE_MODE_STREET_X) == N2_RACE_STREETX);
    CHECK(fe_mode_race_kind(FE_MODE_URL) == N2_RACE_URL);
    CHECK(fe_mode_race_kind(FE_MODE_FREE_RUN) == -1);
    CHECK(fe_mode_race_kind(99) == -1);
}

static void test_navigation(void) {
    FeEntry e[GC_MAX];
    FeCarouselScreen s;
    int n = fe_mode_entries(e), id = -1;
    fes_open(&s, e, n, FE_MODE_CIRCUIT, 562.0f);
    CHECK(s.n == 7 && s.car.cur == 0 && s.car.rail_w == 562.0f);
    CHECK(!strcmp(fes_caption(&s), "Circuit"));
    fes_input(&s, FES_IN_LEFT);                       /* no wrap at the left end (gui_carousel rule) */
    CHECK(s.car.cur == 0);
    for (int i = 0; i < 6; i++) fes_input(&s, FES_IN_RIGHT);
    CHECK(s.car.cur == 6 && !strcmp(fes_caption(&s), "Free Run"));
    fes_input(&s, FES_IN_RIGHT);                      /* nor at the right end */
    CHECK(s.car.cur == 6);
    fes_input(&s, FES_IN_LEFT);
    CHECK(s.car.cur == 5);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);

    fes_open(&s, e, n, FE_MODE_DRAG, 562.0f);         /* initial focus by id */
    CHECK(s.car.cur == FE_MODE_DRAG);
    fes_open(&s, e, n, 99, 562.0f);                   /* unknown id: first entry */
    CHECK(s.car.cur == 0);
}

static void test_exit(void) {
    FeEntry e[GC_MAX];
    FeCarouselScreen s;
    int n = fe_mode_entries(e), id = -1;
    fes_open(&s, e, n, FE_MODE_CIRCUIT, 562.0f);
    fes_input(&s, FES_IN_RIGHT);
    fes_input(&s, FES_IN_ACCEPT);
    CHECK(s.exiting);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);          /* nothing before the exit completes */
    fes_input(&s, FES_IN_RIGHT);                      /* input is ignored while exiting */
    fes_input(&s, FES_IN_BACK);
    CHECK(s.car.cur == 1 && s.reason == FES_IN_ACCEPT);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);
    fes_exit_complete(&s);
    CHECK(!s.exiting);
    CHECK(fes_poll(&s, &id) == FES_EV_CHOSEN && id == FE_MODE_SPRINT);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);          /* exactly once */

    fes_open(&s, e, n, FE_MODE_CIRCUIT, 562.0f);
    fes_exit_complete(&s);                            /* not exiting: no-op */
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);
    fes_input(&s, FES_IN_BACK);
    CHECK(s.exiting && fes_poll(&s, &id) == FES_EV_NONE);
    run_exit(&s);
    CHECK(fes_poll(&s, &id) == FES_EV_BACK);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);
}

static void test_disabled(void) {
    FeEntry e[GC_MAX];
    FeCarouselScreen s;
    int n = fe_main_entries(e, 1u << FE_MAIN_QUICK_RACE), id = -1;
    fes_open(&s, e, n, FE_MAIN_CAREER, 562.0f);       /* the requested focus is disabled: first enabled entry */
    CHECK(s.car.cur == FE_MAIN_QUICK_RACE);
    fes_input(&s, FES_IN_LEFT);                       /* disabled entries are skipped */
    CHECK(s.car.cur == FE_MAIN_QUICK_RACE);
    fes_input(&s, FES_IN_RIGHT);
    CHECK(s.car.cur == FE_MAIN_QUICK_RACE);
    /* every entry disabled: accept does nothing at all */
    n = fe_main_entries(e, 0);
    fes_open(&s, e, n, FE_MAIN_CAREER, 562.0f);
    fes_input(&s, FES_IN_ACCEPT);
    CHECK(!s.exiting);
    run_exit(&s);
    CHECK(fes_poll(&s, &id) == FES_EV_NONE);
    fes_input(&s, FES_IN_BACK);                       /* Back still works */
    run_exit(&s);
    CHECK(fes_poll(&s, &id) == FES_EV_BACK);
    /* pointer activation of a disabled focused entry */
    fes_open(&s, e, n, FE_MAIN_CAREER, 562.0f);
    float x = s.car.master_x, y = s.car.master_y;
    fes_pointer_press(&s, x, y);
    fes_pointer_release(&s, x, y);
    CHECK(!s.exiting);
}

static void test_pointer(void) {
    FeEntry e[GC_MAX];
    FeCarouselScreen s;
    int n = fe_mode_entries(e), id = -1;
    fes_open(&s, e, n, FE_MODE_CIRCUIT, 562.0f);
    gc_set_master(&s.car, 0.388f, -14.296f);
    CHECK(gc_arrow_at(GC_ARROW_R_X, GC_ARROW_Y) == 1);
    fes_pointer_press(&s, GC_ARROW_L_X, GC_ARROW_Y);  /* the left arrow is hidden on the first entry: inert */
    CHECK(s.car.cur == 0);
    fes_pointer_press(&s, GC_ARROW_R_X, GC_ARROW_Y);  /* steps at press-down */
    CHECK(s.car.cur == 1);
    fes_pointer_release(&s, GC_ARROW_R_X, GC_ARROW_Y);
    CHECK(!s.exiting);
    fes_pointer_press(&s, GC_ARROW_R_X, GC_ARROW_Y);
    CHECK(s.car.cur == 2);
    fes_pointer_press(&s, GC_ARROW_L_X, GC_ARROW_Y);
    CHECK(s.car.cur == 1);
    /* the selection slot activates on release when the press began on it */
    float x = s.car.master_x, y = s.car.master_y;
    fes_pointer_press(&s, x, y);
    CHECK(!s.exiting);
    fes_pointer_release(&s, x, y);
    CHECK(s.exiting && fes_poll(&s, &id) == FES_EV_NONE);
    fes_pointer_press(&s, GC_ARROW_R_X, GC_ARROW_Y);  /* ignored while exiting */
    CHECK(s.car.cur == 1);
    run_exit(&s);
    CHECK(fes_poll(&s, &id) == FES_EV_CHOSEN && id == FE_MODE_SPRINT);
    /* package-specific arrow boxes (UI_Main: -271 / 269 at y -133) replace the pause geometry */
    fes_open(&s, e, n, FE_MODE_SPRINT, 564.0f);
    fes_set_arrows(&s, -271.0f, 269.0f, -133.0f);
    fes_pointer_press(&s, GC_ARROW_R_X, GC_ARROW_Y);  /* the old position is now inert */
    CHECK(s.car.cur == 1);
    fes_pointer_press(&s, 269.0f + 13.0f, -133.0f - 13.0f);   /* box corner, inclusive */
    CHECK(s.car.cur == 2);
    fes_pointer_press(&s, 269.0f + 13.5f, -133.0f);
    CHECK(s.car.cur == 2);
    fes_pointer_press(&s, -271.0f, -133.0f);
    CHECK(s.car.cur == 1);
    /* press on the slot, release elsewhere: no activation */
    fes_open(&s, e, n, FE_MODE_CIRCUIT, 562.0f);
    gc_set_master(&s.car, 0.388f, -14.296f);
    fes_pointer_press(&s, s.car.master_x, s.car.master_y);
    fes_pointer_release(&s, s.car.master_x + 200, s.car.master_y);
    CHECK(!s.exiting);
    /* release without a press: no activation */
    fes_pointer_release(&s, s.car.master_x, s.car.master_y);
    CHECK(!s.exiting);
    /* press elsewhere (rail) arms nothing */
    fes_pointer_press(&s, 0, 100);
    fes_pointer_release(&s, s.car.master_x, s.car.master_y);
    CHECK(!s.exiting);
}

int main(void) {
    test_entries();
    test_navigation();
    test_exit();
    test_disabled();
    test_pointer();
    if (fails) { printf("fe_screen_test: FAIL (%d)\n", fails); return 1; }
    puts("fe_screen_test: PASS");
    return 0;
}
