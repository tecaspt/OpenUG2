/* hud_test — the HUD's pure maths, with no GL context.
 *
 * These are the parts that are wrong SILENTLY: an arrow that unwinds the long
 * way round a seam still animates, a map transform that is not its own inverse
 * still draws dots, and a layout that scales with width still fills the screen
 * at 16:9. Each case below fails under the specific mistake it guards.
 */
#include "../src/hud.h"
#include "../src/hud_asset.h"
#include "../src/hud_host.h"
#include "../src/hud_baseline.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifdef NDEBUG
#error "hud_test requires assertions; remove -DNDEBUG"
#endif

#include "../src/audio.h"
#define ENG_UPSHIFT_RPM_FOR_TEST ENG_UPSHIFT_RPM
#define PI 3.14159265358979323846f
static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void test_speed_readout(void) {
    assert(hud_speed_value(137.9f, 0) == 137);
    assert(hud_speed_value(-137.9f, 0) == 137);
    assert(hud_speed_value(.99f, 0) == 0);
    assert(hud_speed_value(80.4f, 1) == 49);
    assert(hud_speed_value(-80.4f, 1) == 49);
    assert(hud_speed_value(0, 1) == 0);
    for (int frame = 0; frame < 1000; ++frame) {
        assert(hud_speed_value(137.9f, 0) == 137);
        assert(hud_speed_value(1, 0) == 1); /* no history from the last sample */
    }
    assert(hud_speed_value(NAN, 0) == 0);
    assert(hud_speed_value(INFINITY, 1) == 0);
    assert(hud_speed_value(1e30f, 0) == INT_MAX);
    assert(hud_speed_value(-1e30f, 1) == INT_MAX);
    puts("  speed uses current magnitude, units and truncation     PASS");
}

static void test_tachometer_uses_current_bounded_sample(void) {
    HudState s = {0};
    s.have_rpm = 1; s.rpm_redline = 6500; s.rpm_maximum = 7000;
    s.rpm = 3500;
    assert(near(hud_tach_fraction(&s, 7000), .5f, 1e-6f));
    s.rpm = s.rpm_redline;
    assert(hud_tach_fraction(&s, 7000) < 1); /* redline is not the maximum */
    s.rpm = 9000;
    assert(hud_tach_fraction(&s, 7000) == 1);
    assert(near(hud_tach_fraction(&s, 10000), .7f, 1e-6f));
    s.rpm = -100;
    assert(hud_tach_fraction(&s, 7000) == 0);
    for (int frame = 0; frame < 1000; ++frame) {
        s.rpm = 5600;
        assert(near(hud_tach_fraction(&s, 7000), .8f, 1e-6f));
        s.rpm = 1400;
        assert(near(hud_tach_fraction(&s, 7000), .2f, 1e-6f));
    }
    s.rpm_maximum = 0; s.rpm = 3250;
    assert(near(hud_tach_fraction(&s, 6500), .5f, 1e-6f));
    s.have_rpm = 0;
    assert(hud_tach_fraction(&s, 7000) == 0);
    s.have_rpm = 1; s.rpm = NAN;
    assert(hud_tach_fraction(&s, 7000) == 0);
    s.rpm = 3000; s.rpm_maximum = INFINITY;
    assert(hud_tach_fraction(&s, 7000) == 0);
    s.rpm_maximum = NAN;
    assert(hud_tach_fraction(&s, 7000) == 0);
    assert(hud_tach_fraction(&s, 0) == 0);
    assert(hud_tach_fraction(NULL, 7000) == 0);
    puts("  tachometer uses current RPM and a separate upper bound PASS");
}

/* ---- 1. arrow angle: the wrap is the whole point ---- */
static void test_angle_delta_takes_the_short_way(void) {
    assert(near(hud_angle_delta(0.0f, 0.5f), 0.5f, 1e-5f));
    assert(near(hud_angle_delta(0.5f, 0.0f), -0.5f, 1e-5f));

    /* Crossing the seam: heading just under +pi, target just over -pi. The
       true turn is a hair to the LEFT (+), not almost a full turn right. */
    float from = PI - 0.05f, to = -PI + 0.05f;
    float d = hud_angle_delta(from, to);
    assert(d > 0.0f && d < 0.2f);
    /* the naive to-from would report about -6.18 rad here */
    assert(fabsf(to - from) > 6.0f);

    /* and the mirror case turns the other way */
    assert(hud_angle_delta(-PI + 0.05f, PI - 0.05f) < 0.0f);

    /* every result is inside (-pi, +pi], including many wraps out */
    for (int i = -40; i <= 40; i++) {
        float a = (float)i * 0.7f;
        float w = hud_angle_delta(0.0f, a);
        assert(w > -PI - 1e-4f && w <= PI + 1e-4f);
        /* and it is the same angle modulo a full turn */
        float k = (a - w) / (2.0f * PI);
        assert(near(k, floorf(k + 0.5f), 1e-3f));
    }
    /* exactly opposite resolves to +pi, never -pi, so the arrow is stable */
    assert(near(hud_angle_delta(0.0f, PI), PI, 1e-4f));
    printf("  arrow angle wraps to the short way round                PASS\n");
}

/* ---- 2. map transform: projection and picking must be inverses ---- */
static void test_map_project_and_pick_round_trip(void) {
    HudState s; memset(&s, 0, sizeof s);
    s.car_x = -863.0f; s.car_y = -290.0f; s.car_heading = 0.7f;
    const int W = 1920, H = 1080;

    /* a point 100 m north-east of the car comes back unchanged */
    const float tx = s.car_x + 70.0f, ty = s.car_y + 70.0f;
    float mx, my;
    hud_map_project(tx, ty, s.car_x, s.car_y, 260.0f,
                    PI * 0.5f - s.car_heading, &mx, &my);
    assert(mx * mx + my * my < 1.0f);          /* inside the window */

    /* Drive it through the SAME layout the draw uses by picking the NDC the
       projection lands on. hud_map_pick owns the panel geometry, so this also
       pins the draw and the pick to one definition. */
    float back_x = 0, back_y = 0;
    /* recover the panel centre by picking the exact centre of the panel: the
       car's own position must map to the panel centre, whatever the layout */
    float cx_ndc = 0, cy_ndc = 0;
    {   /* search the NDC that returns the car position (panel centre) */
        int found = 0;
        for (int iy = -200; iy <= 200 && !found; iy++)
            for (int ix = -200; ix <= 200; ix++) {
                float nx = (float)ix / 200.0f, ny = (float)iy / 200.0f;
                float wx, wy;
                if (!hud_map_pick(nx, ny, W, H, &s, &wx, &wy)) continue;
                if (near(wx, s.car_x, 1.5f) && near(wy, s.car_y, 1.5f)) {
                    cx_ndc = nx; cy_ndc = ny; found = 1; break;
                }
            }
        assert(found);
    }
    /* now project a world point, place it in NDC the way hud_draw does, and
       pick it back */
    float asp = (float)W / (float)H, MAP_R = 0.30f;
    float px = cx_ndc + mx * MAP_R / asp, py = cy_ndc + my * MAP_R;
    assert(hud_map_pick(px, py, W, H, &s, &back_x, &back_y));
    assert(near(back_x, tx, 2.0f));
    assert(near(back_y, ty, 2.0f));

    /* outside the round panel the pick must refuse rather than invent a spot */
    float junk_x = 12345.0f, junk_y = 54321.0f;
    assert(!hud_map_pick(0.0f, 0.0f, W, H, &s, &junk_x, &junk_y));
    assert(junk_x == 12345.0f && junk_y == 54321.0f);   /* outputs untouched */

    /* heading-up: the world point straight AHEAD of the car must land on the
       +Y (up) axis of the panel, at any heading. This is what fails if the
       quarter-turn in the rotation is dropped. */
    for (int i = 0; i < 12; i++) {
        float hdg = (float)i * 0.5f;
        float ax = s.car_x + cosf(hdg) * 80.0f, ay = s.car_y + sinf(hdg) * 80.0f;
        float ox, oy;
        hud_map_project(ax, ay, s.car_x, s.car_y, 260.0f, PI * 0.5f - hdg, &ox, &oy);
        assert(near(ox, 0.0f, 1e-4f));
        assert(oy > 0.0f);
    }
    printf("  map projection and picking are exact inverses          PASS\n");
}

/* ---- 3. layout: a unit must be the same size at 16:9 and at 4:3 ---- */
static void test_layout_keeps_proportions_across_aspects(void) {
    const float wide = 1920.0f / 1080.0f, classic = 1024.0f / 768.0f;

    /* A margin quoted in units must come out the same DISTANCE from the edge
       in pixels-of-height at both aspects... */
    float wx, wy, cxx, cyy;
    hud_place(wide,    -1.0f, -1.0f, 0.38f, 0.38f, &wx, &wy);
    hud_place(classic, -1.0f, -1.0f, 0.38f, 0.38f, &cxx, &cyy);
    assert(near(wy, cyy, 1e-6f));                 /* vertical is identical */
    /* ...and horizontally it must shrink in NDC as the screen widens, which is
       exactly what keeps it a fixed number of PIXELS from the edge. */
    assert((wx + 1.0f) < (cxx + 1.0f));
    assert(near((wx + 1.0f) * wide, (cxx + 1.0f) * classic, 1e-5f));

    /* anchors stay on their own edges */
    float rx, ry;
    hud_place(wide, 1.0f, 1.0f, -0.34f, -0.16f, &rx, &ry);
    assert(rx < 1.0f && rx > 0.0f && ry < 1.0f && ry > 0.0f);

    /* top centre is EXACTLY centred at every aspect: the navigation arrow is
       specified to sit there, and x must not drift with the aspect */
    for (int i = 0; i < 4; i++) {
        const float asps[4] = { 16.0f/9.0f, 4.0f/3.0f, 21.0f/9.0f, 1.0f };
        float ax, ay;
        hud_place(asps[i], 0.0f, 1.0f, 0.0f, -0.20f, &ax, &ay);
        assert(ax == 0.0f);
        assert(near(ay, 0.80f, 1e-6f));
    }
    printf("  layout units hold their proportions across aspects     PASS\n");
}

/* ---- 4. a square stays square: the map window must not shear ---- */
static void test_map_window_is_isotropic(void) {
    HudState s; memset(&s, 0, sizeof s);
    s.car_heading = 0.0f;
    float ex, ey, nx, ny;
    hud_map_project(100.0f, 0.0f, 0.0f, 0.0f, 200.0f, 0.0f, &ex, &ey);
    hud_map_project(0.0f, 100.0f, 0.0f, 0.0f, 200.0f, 0.0f, &nx, &ny);
    /* equal world distances give equal panel distances in both axes */
    assert(near(sqrtf(ex*ex + ey*ey), sqrtf(nx*nx + ny*ny), 1e-6f));
    assert(near(sqrtf(ex*ex + ey*ey), 0.5f, 1e-6f));
    /* a degenerate radius must not divide by zero */
    float dx, dy;
    hud_map_project(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &dx, &dy);
    assert(dx == dx && dy == dy);            /* not NaN */
    printf("  map window is isotropic and radius-safe                PASS\n");
}

/* ---- 5. sprint must never borrow a circuit's lap counter ---- */
static void test_sprint_reports_progress_not_fabricated_laps(void) {
    HudState s; memset(&s, 0, sizeof s);
    char buf[64];

    /* free roam: no status line at all */
    hud_status_text(&s, buf, sizeof buf);
    assert(buf[0] == 0);

    /* circuit: real laps */
    s.racing = 1; s.circuit = 1; s.lap = 2; s.laps = 3;
    hud_status_text(&s, buf, sizeof buf);
    assert(!strcmp(buf, "LAP 2/3"));

    /* sprint: progress, and NOTHING that reads as a lap count */
    s.circuit = 0; s.sprint_progress = 0.435f;
    /* lap/laps deliberately left populated: a sprint must ignore them */
    hud_status_text(&s, buf, sizeof buf);
    assert(!strstr(buf, "LAP"));
    assert(!strstr(buf, "/"));
    assert(!strcmp(buf, "44 PCT"));

    /* progress is clamped, so a checkpoint overshoot cannot print 160 PCT */
    s.sprint_progress = 1.8f;  hud_status_text(&s, buf, sizeof buf);
    assert(!strcmp(buf, "100 PCT"));
    s.sprint_progress = -0.3f; hud_status_text(&s, buf, sizeof buf);
    assert(!strcmp(buf, "0 PCT"));

    /* a circuit whose lap counter runs past the total still reads sanely */
    s.circuit = 1; s.lap = 9; s.laps = 2;
    hud_status_text(&s, buf, sizeof buf);
    assert(!strcmp(buf, "LAP 2/2"));
    /* and lap 0 (not yet over the line) shows as the first lap, not "0" */
    s.lap = 0; hud_status_text(&s, buf, sizeof buf);
    assert(!strcmp(buf, "LAP 1/2"));
    s.race_kind=N2_RACE_DRIFT;s.drift_score=1234;s.drift_chain=56;
    hud_status_text(&s,buf,sizeof buf);assert(!strcmp(buf,"SCORE 1234  +56"));
    s.race_kind=N2_RACE_DRAG;s.engine_heat=.5f;s.shift_ready=1;
    hud_status_text(&s,buf,sizeof buf);assert(!strcmp(buf,"HEAT 50 PCT SHIFT"));
    s.engine_failed=1;hud_status_text(&s,buf,sizeof buf);assert(!strcmp(buf,"ENGINE BLOWN"));
    printf("  sprints show progress, never fabricated laps           PASS\n");
}

/* ---- 5b. fitted parts are REMOVED, not greyed out ---- */
static void test_unfitted_parts_are_removed(void) {
    HudState s; memset(&s, 0, sizeof s);
    assert(!hud_shows_nos(&s));              /* nothing fitted -> no N2O bar */
    s.have_nos = 1;
    assert(hud_shows_nos(&s));
    /* an ACTIVE bottle on an UNFITTED car must still show nothing: activation
       is not fitment, and drawing the bar here would be the bug */
    s.have_nos = 0; s.nitro_active = 1; s.nitro_tank = 1.0f;
    assert(!hud_shows_nos(&s));
    /* fitment is independent of whether a tank model exists */
    s.have_nos = 1; s.have_nitro_tank = 0;
    assert(hud_shows_nos(&s));
    assert(!hud_shows_nos(NULL));
    printf("  unfitted parts are removed, not drawn inert          PASS\n");
}

/* ---- 6. money: hidden in a race, and only shown when it exists ---- */
static void test_money_visibility(void) {
    HudState s; memset(&s, 0, sizeof s);
    assert(!hud_shows_money(&s));            /* free roam, no economy -> no */
    s.have_money = 1;
    assert(hud_shows_money(&s));             /* free roam, economy    -> yes */
    s.racing = 1;
    assert(!hud_shows_money(&s));            /* racing hides it even so      */
    s.have_money = 0;
    assert(!hud_shows_money(&s));
    assert(!hud_shows_money(NULL));
    printf("  money hides in races and when no economy exists        PASS\n");
}

/* ---- 7. the art gate: flat UI art passes, decoded noise does not ---- */
static void test_art_gate_separates_ui_from_noise(void) {
    enum { W = 64, H = 64 };
    static unsigned char flat[W*H*3], noise[W*H*3];
    /* UI-like: a few flat blocks with hard edges */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned char v = (x < 20) ? 0 : (x < 42 ? 200 : 90);
            unsigned char *p = flat + ((long)y*W + x)*3;
            p[0] = v; p[1] = v; p[2] = (unsigned char)(v / 2);
        }
    /* mis-decode-like: no horizontal coherence at all */
    unsigned int r = 12345u;
    for (long i = 0; i < (long)W*H*3; i++) { r = r*1103515245u + 12345u; noise[i] = (unsigned char)(r >> 16); }

    float f_flat = hud_art_run_fraction(flat, W, H);
    float f_noise = hud_art_run_fraction(noise, W, H);
    assert(f_flat > HUD_ART_RUN_MIN);
    assert(f_noise < HUD_ART_RUN_MIN);
    /* the two must not merely straddle the threshold, they must be far apart,
       or the gate is a coin flip on the next sprite */
    assert(f_flat - f_noise > 0.5f);
    /* measured reference points from the shipped packs (see hud.h) */
    assert(HUD_ART_RUN_MIN < 0.719f);   /* below the worst real sprite */
    assert(HUD_ART_RUN_MIN > 0.192f);   /* above ARROW_MARKERA's noise */
    /* degenerate inputs must not divide by zero or reject everything */
    assert(hud_art_run_fraction(NULL, W, H) >= HUD_ART_RUN_MIN);
    assert(hud_art_run_fraction(flat, 1, 1) >= HUD_ART_RUN_MIN);
    printf("  art gate separates flat UI art from decoded noise      PASS\n");
}

/* Host wiring (hud_host.h): which spec inputs the engine supplies, with and
 * without --hud-preview, fed through the same pure element contracts hud_draw
 * uses. Guards the "only tach and minimap appear" regression. */
static void host_elements(const HudState *s, int *bank, HaWidget *w, int *nos, int *shift) {
    char buf[64]; double lim;
    *bank = ha_bank(s->career_context, s->have_money, s->suppress_cash, (int64_t)s->money, ',', 1, buf);
    ha_widget(s->career_context, s->message_waiting != 0, 1, 1, w);
    *nos = ha_nos_level(1, s->have_nos, s->have_nitro_tank, s->nitro_tank, 91, 239, &lim);
    *shift = ha_shift(1, s->have_shift_rpm, s->shift_rpm, s->gear, s->rpm, s->rpm_maximum);
}

static void test_host_normal_free_roam_fabricates_nothing(void) {
    HudState s; memset(&s, 0, sizeof s);
    s.have_nos = 1; s.gear = 2; s.rpm = 3000; s.rpm_maximum = 8000;
    hud_host_context(&s);
    assert(s.career_context == 1);
    assert(!s.have_money && !s.message_waiting && !s.have_nitro_tank);
    int bank, nos, shift; HaWidget w;
    host_elements(&s, &bank, &w, &nos, &shift);
    assert(bank == 0);                                /* no balance, no Bank */
    assert(w.visible && w.backing && w.dpad && w.centre);   /* idle widget (A) */
    assert(!w.mail && !w.highlight_right);            /* no fabricated message */
    assert(nos == 0);                                 /* no tank, no level arc */
    /* racing: no career context, widget hidden */
    HudState r = s; r.racing = 1; hud_host_context(&r);
    assert(r.career_context == 0);
    host_elements(&r, &bank, &w, &nos, &shift);
    assert(!w.visible);
    printf("  host: normal free roam sets context, fabricates nothing PASS\n");
}

static void test_host_preview_shows_every_free_roam_element(void) {
    HudState s; memset(&s, 0, sizeof s);
    s.have_nos = 1; s.have_shift_rpm = 1; s.shift_rpm = hud_host_shift_rpm(7000.0f);
    s.gear = 3; s.rpm = 6800; s.rpm_maximum = 8000;
    hud_host_context(&s);
    hud_host_preview_apply(&s, HUD_PREVIEW_TANK_START);
    int bank, nos, shift; HaWidget w;
    host_elements(&s, &bank, &w, &nos, &shift);
    assert(bank == 1);
    assert(w.visible && w.centre && w.mail && w.highlight_right);
    assert(nos == 2);                                 /* partial level arc */
    assert(shift == 1);
    /* a live value is never overwritten by a sample */
    HudState l; memset(&l, 0, sizeof l);
    l.have_money = 1; l.money = 42; l.message_waiting = 1;
    l.have_nitro_tank = 1; l.nitro_tank = 0.1f;
    hud_host_preview_apply(&l, 0.9f);
    assert(l.money == 42 && near(l.nitro_tank, 0.1f, 1e-6f));
    /* Race layouts have no preview banner: no sampled telemetry is admitted. */
    HudState r; memset(&r, 0, sizeof r); r.racing = 1;
    hud_host_context(&r); hud_host_preview_apply(&r, 0.5f);
    assert(!r.career_context && !r.have_money && !r.message_waiting && !r.have_nitro_tank);
    /* NOS not fitted: the sample tank alone does not show the arc */
    HudState n; memset(&n, 0, sizeof n);
    hud_host_context(&n); hud_host_preview_apply(&n, 0.5f);
    host_elements(&n, &bank, &w, &nos, &shift);
    assert(nos == 0);
    printf("  host: preview samples every free-roam readout          PASS\n");
}

static void test_host_preview_tank_drains_and_refills(void) {
    float t = 1.0f;
    for (int i = 0; i < 60; i++) t = hud_host_preview_tank_step(t, 1, 1.0f / 60.0f);
    assert(near(t, 1.0f - HUD_PREVIEW_TANK_DRAIN_PER_S, 1e-3f));
    float d = t;
    for (int i = 0; i < 60; i++) t = hud_host_preview_tank_step(t, 0, 1.0f / 60.0f);
    assert(t > d && near(t - d, HUD_PREVIEW_TANK_REFILL_PER_S, 1e-3f));
    assert(HUD_PREVIEW_TANK_REFILL_PER_S < HUD_PREVIEW_TANK_DRAIN_PER_S);   /* slow */
    for (int i = 0; i < 600; i++) t = hud_host_preview_tank_step(t, 1, 0.1f);
    assert(t == 0.0f);
    for (int i = 0; i < 6000; i++) t = hud_host_preview_tank_step(t, 0, 0.1f);
    assert(t == 1.0f);
    /* bad inputs stay finite and in range; a stalled frame is bounded */
    assert(near(hud_host_preview_tank_step(NAN, 0, 0.0f), HUD_PREVIEW_TANK_START, 1e-6f));
    assert(hud_host_preview_tank_step(0.5f, 1, NAN) == 0.5f);
    assert(hud_host_preview_tank_step(0.5f, 1, 100.0f) > 0.4f);
    printf("  host: preview tank drains on N and refills slowly      PASS\n");
}

static void test_host_shift_rpm_is_reachable_before_the_automatic_upshift(void) {
    float shift = hud_host_shift_rpm(ENG_UPSHIFT_RPM_FOR_TEST);
    assert(shift > 0 && shift < ENG_UPSHIFT_RPM_FOR_TEST);
    assert(near(shift, ENG_UPSHIFT_RPM_FOR_TEST - HUD_HOST_SHIFT_LEAD_RPM, 1e-3f));
    /* the gearbox upshifts at an RPM strictly above its point and never
       publishes it, so the HUD can only see RPM up to that point */
    HudState s; memset(&s, 0, sizeof s);
    s.have_shift_rpm = 1; s.shift_rpm = shift; s.gear = 2; s.rpm_maximum = 8000;
    s.rpm = ENG_UPSHIFT_RPM_FOR_TEST - 1; assert(ha_shift(1, 1, s.shift_rpm, s.gear, s.rpm, s.rpm_maximum) == 1);
    s.rpm = shift - 1;                     assert(ha_shift(1, 1, s.shift_rpm, s.gear, s.rpm, s.rpm_maximum) == 0);
    s.gear = 0; s.rpm = 7000;              assert(ha_shift(1, 1, s.shift_rpm, s.gear, s.rpm, s.rpm_maximum) == 0);
    assert(hud_host_shift_rpm(NAN) == 0.0f && hud_host_shift_rpm(-1) == 0.0f);
    assert(hud_host_shift_rpm(800.0f) == 800.0f);
    printf("  host: shift RPM sits below the automatic upshift       PASS\n");
}

/* Set 7 H: the shift icon is default on, flashes, defaults to the authored
 * lit colour, and the host supplies its RPM and transmission mode. */
static void test_shift_icon_defaults_and_host_inputs(void) {
    HudSettings d; hud_default_settings(&d);
    assert(d.shift_indicator == 1 && d.shift_flash == 1 && d.shift_icon_automatic == 1);
    assert(d.shift_colour[0] == 202 && d.shift_colour[1] == 244 && d.shift_colour[2] == 138);
    assert(d.inferred_redline == 0 && d.inferred_nos_level == 0 && d.nos_glow == 0);
    HudState s; memset(&s, 0, sizeof s);
    s.gear = 3; s.rpm = 6800; s.rpm_maximum = 8000;
    hud_host_shift(&s, ENG_UPSHIFT_RPM_FOR_TEST, 0);
    assert(s.have_shift_rpm && s.transmission == HA_TRANSMISSION_AUTOMATIC);
    assert(ha_shift_mode(d.shift_indicator, s.have_shift_rpm, s.shift_rpm, s.gear, s.rpm,
                         s.rpm_maximum, s.transmission, d.shift_icon_automatic) == 1);
    s.gear = 6;                                   /* no top-gear exclusion (H3 item 4) */
    assert(ha_shift_mode(1, s.have_shift_rpm, s.shift_rpm, s.gear, s.rpm, s.rpm_maximum, s.transmission, 1) == 1);
    assert(ha_shift_mode(1, s.have_shift_rpm, s.shift_rpm, s.gear, s.rpm, s.rpm_maximum, s.transmission, 0) == 0);
    hud_host_shift(&s, ENG_UPSHIFT_RPM_FOR_TEST, 1);
    assert(s.transmission == HA_TRANSMISSION_MANUAL && s.shift_rpm==ENG_UPSHIFT_RPM_FOR_TEST);
    assert(!ha_shift_mode(1,s.have_shift_rpm,s.shift_rpm,3,ENG_UPSHIFT_RPM_FOR_TEST-1,8000,s.transmission,1));
    hud_host_shift(&s, NAN, 0);                   /* withheld, not supplied as 0 */
    assert(!s.have_shift_rpm && ha_shift_mode(1, s.have_shift_rpm, s.shift_rpm, 3, 7000, 8000, s.transmission, 1) == 0);
    printf("  shift icon: default on, authored colour, host inputs   PASS\n");
}

/* Set 8 G6: glyphs are sampled edge to edge with no inset by default; the
 * inset is a labelled compatibility setting. The repacked atlas gives every
 * cell a transparent one-texel ring, so bilinear sampling fades into alpha 0
 * even where the source atlas has opaque neighbours or the cell touches the
 * atlas edge. */
static void test_glyph_sampling_defaults_and_gutter(void) {
    HudSettings d; hud_default_settings(&d);
    assert(d.glyph_half_texel_inset == 0);
    unsigned char src[4 * 4 * 4]; memset(src, 255, sizeof src);   /* fully opaque source */
    HaGlyph g = {48, 0, 0, 4, 4, 4, 0, 0};
    HaGlyphAtlas a;
    assert(ha_glyph_atlas(src, 4, 4, &g, 1, &a) == 1 && a.cells[0].ok);
    HaGlyph p = g; p.x = a.cells[0].x; p.y = a.cells[0].y;
    HaGlyphQuad q;
    assert(ha_glyph_quad(&p, 1.0f / a.width, 1.0f / a.height, 0, 0, 4, 4, d.glyph_half_texel_inset, &q));
    double s[4];
    ha_sample_linear(a.rgba, a.width, a.height, q.u0 + (q.u1 - q.u0) * 0.5 / 16, q.v0 + (q.v1 - q.v0) * 0.5, s);
    assert(fabs(s[3] - 0.625) < 1e-6);            /* first pixel fades into the ring */
    assert(ha_glyph_quad(&p, 1.0f / a.width, 1.0f / a.height, 0, 0, 4, 4, 1, &q));
    ha_sample_linear(a.rgba, a.width, a.height, q.u0 + (q.u1 - q.u0) * 0.5 / 16, q.v0 + (q.v1 - q.v0) * 0.5, s);
    assert(fabs(s[3] - 1.0) < 1e-6);              /* inset: the ring never contributes */
    ha_glyph_atlas_free(&a);
    assert(fabs(ha_glyph_over(1.0, 136 / 255.0, 255, 255, 100) - 182.6667) < 1e-3);
    printf("  glyphs: no inset by default, transparent ring, linear  PASS\n");
}

/* Project policy: widescreen HUD fitment (not original behaviour). The engine-drawn
 * items follow the same rule as the package groups, and the minimap pick inverts the
 * final placement under both policies. Spec vectors: docs/cleanroom-hud/test-vectors/wide-policy.json
 * (checked in tools/hud_spec_test.py); these are the engine-side counterparts. */
static void test_widescreen_policy_engine_items_and_pick(void) {
    HudSettings d; hud_default_settings(&d);
    assert(d.fit_policy == HA_FIT_WIDE);                       /* wide is the default */
    assert(ha_fit_parse("wide") == HA_FIT_WIDE && ha_fit_parse("WIDE") == HA_FIT_WIDE);
    assert(ha_fit_parse("4:3") == HA_FIT_43 && ha_fit_parse("4x3") == HA_FIT_43 && ha_fit_parse("43") == HA_FIT_43);
    assert(ha_fit_parse("") == -1 && ha_fit_parse("16:9") == -1 && ha_fit_parse(NULL) == -1 && ha_fit_parse("widescreen") == -1);

    /* 1920x1080: a left-anchored item sits 0.38 HUD units (0.38*540 px) from its reference edge:
       the screen edge under wide, the centred 4:3 frame's edge under 4:3. */
    float x, y;
    hud_baseline_place_fit(HA_FIT_WIDE, 1920, 1080, -1, -1, 0.38f, 0.38f, &x, &y);
    assert(near((x + 1) * 960, 0.38f * 540, 1e-2f) && near(y, -0.62f, 1e-6f));
    hud_baseline_place_fit(HA_FIT_43, 1920, 1080, -1, -1, 0.38f, 0.38f, &x, &y);
    assert(near((x + 1) * 960, 240 + 0.38f * 540, 1e-2f));
    hud_baseline_place_fit(HA_FIT_WIDE, 1920, 1080, 1, 1, -0.34f, -0.16f, &x, &y);   /* right group */
    assert(near((1 - x) * 960, 0.34f * 540, 1e-2f));
    hud_baseline_place_fit(HA_FIT_43, 1920, 1080, 1, 1, -0.34f, -0.16f, &x, &y);
    assert(near((1 - x) * 960, 240 + 0.34f * 540, 1e-2f));
    for (int p = 0; p < 2; p++) {                              /* centred items never move */
        hud_baseline_place_fit(p, 1920, 1080, 0, 1, 0, -0.20f, &x, &y);
        assert(x == 0.0f && near(y, 0.80f, 1e-6f));
        hud_baseline_place_fit(p, 2560, 1080, 0, -1, 0, 0.045f, &x, &y);
        assert(x == 0.0f);
    }
    /* At 4:3 and narrower both policies are the same as the unfitted helper. */
    const int sizes[][2] = { {640, 480}, {1440, 1080}, {1024, 768}, {800, 1000} };
    for (int k = 0; k < 4; k++)
        for (int p = 0; p < 2; p++) {
            float ax, ay, bx, by;
            hud_baseline_place_fit(p, sizes[k][0], sizes[k][1], -1, -1, 0.38f, 0.38f, &ax, &ay);
            hud_baseline_place((float)sizes[k][0] / sizes[k][1], -1, -1, 0.38f, 0.38f, &bx, &by);
            assert(near(ax, bx, 1e-6f) && near(ay, by, 1e-6f));
        }
    /* Wide is exactly the existing edge-anchored layout at every wider aspect. */
    for (int k = 0; k < 3; k++) {
        const int w[3] = { 1920, 2560, 1366 }, h[3] = { 1080, 1080, 768 };
        float ax, ay, bx, by;
        hud_baseline_place_fit(HA_FIT_WIDE, w[k], h[k], 1, -1, -0.52f, 0.56f, &ax, &ay);
        hud_baseline_place((float)w[k] / h[k], 1, -1, -0.52f, 0.56f, &bx, &by);
        assert(near(ax, bx, 1e-5f) && near(ay, by, 1e-6f));
    }

    /* Minimap picking inverts the placement: clicking the drawn panel centre returns the
       car's position, a projected world point comes back, and a click at the other policy's
       centre is off the pick point. Includes a race-sized map radius, which the draw uses. */
    HudState s; memset(&s, 0, sizeof s);
    s.car_x = -863.0f; s.car_y = -290.0f; s.car_heading = 0.7f;
    for (int radius = 0; radius < 2; radius++)
        for (int k = 0; k < 3; k++)
            for (int p = 0; p < 2; p++) {
                const int w[3] = { 1920, 2560, 1440 }, h[3] = { 1080, 1080, 1080 };
                s.map_radius_m = radius ? 400.0f : 0.0f;
                float cx, cy, wx, wy;
                hud_baseline_place_fit(p, w[k], h[k], -1, -1, 0.38f, 0.38f, &cx, &cy);
                assert(hud_baseline_map_pick_fit(p, cx, cy, w[k], h[k], &s, &wx, &wy));
                assert(near(wx, s.car_x, 1e-2f) && near(wy, s.car_y, 1e-2f));
                const float view = radius ? 400.0f : 260.0f, tx = s.car_x + 70, ty = s.car_y + 70;
                float mx, my;
                hud_map_project(tx, ty, s.car_x, s.car_y, view, PI * 0.5f - s.car_heading, &mx, &my);
                const float asp = (float)w[k] / h[k];
                assert(hud_baseline_map_pick_fit(p, cx + mx * 0.30f / asp, cy + my * 0.30f, w[k], h[k], &s, &wx, &wy));
                assert(near(wx, tx, 0.5f) && near(wy, ty, 0.5f));
                if (w[k] != 1440) {
                    float ox, oy;
                    hud_baseline_place_fit(!p, w[k], h[k], -1, -1, 0.38f, 0.38f, &ox, &oy);
                    assert(!hud_baseline_map_pick_fit(p, ox, oy, w[k], h[k], &s, &wx, &wy));
                }
            }
    /* the legacy helper keeps the default (wide) placement */
    float lx, ly, wx, wy;
    hud_baseline_place_fit(HA_FIT_WIDE, 1920, 1080, -1, -1, 0.38f, 0.38f, &lx, &ly);
    s.map_radius_m = 0;
    assert(hud_map_pick(lx, ly, 1920, 1080, &s, &wx, &wy) && near(wx, s.car_x, 1e-2f));
    printf("  widescreen policy: engine items, default, pick inverts PASS\n");
}

/* M9: the new settings default to today's output (500 m flat window, no
 * table) and every new HudState input defaults to "absent". */
static void test_m9_defaults_keep_output(void) {
    HudSettings d; hud_default_settings(&d);
    assert(d.map_window_m == 500 && d.map_window_points == 0 && d.icon_ring == 70);
    assert(hud_minimap_window(d.map_window_speed_mps, d.map_window_world_m, d.map_window_points, 40) == 500);
    HudState s; memset(&s, 0, sizeof s);
    assert(!s.map_icons && !s.map_icon_n && !s.opponent_heading && !s.wrong_way && !s.have_outrun);
    /* a two-point table interpolates; the clamp holds at both ends */
    d.map_window_points = 2; d.map_window_speed_mps[0] = 0; d.map_window_speed_mps[1] = 50;
    d.map_window_world_m[0] = 500; d.map_window_world_m[1] = 1500;
    assert(hud_minimap_window(d.map_window_speed_mps, d.map_window_world_m, d.map_window_points, 25) == 1000);
    assert(hud_minimap_window(d.map_window_speed_mps, d.map_window_world_m, d.map_window_points, 99) == 1500);
    printf("  M9 defaults: 500 m flat window, ring 70, inputs absent PASS\n");
}

int main(void) {
    printf("hud_test:\n");
    test_speed_readout();

    test_tachometer_uses_current_bounded_sample();
    test_angle_delta_takes_the_short_way();
    test_map_project_and_pick_round_trip();
    test_layout_keeps_proportions_across_aspects();
    test_map_window_is_isotropic();
    test_sprint_reports_progress_not_fabricated_laps();
    test_unfitted_parts_are_removed();
    test_money_visibility();
    test_art_gate_separates_ui_from_noise();
    test_host_normal_free_roam_fabricates_nothing();
    test_host_preview_shows_every_free_roam_element();
    test_host_preview_tank_drains_and_refills();
    test_host_shift_rpm_is_reachable_before_the_automatic_upshift();
    test_shift_icon_defaults_and_host_inputs();
    test_glyph_sampling_defaults_and_gutter();
    test_widescreen_policy_engine_items_and_pick();
    test_m9_defaults_keep_output();
    printf("hud_test: PASS\n");
    return 0;
}
