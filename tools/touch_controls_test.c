/* touch_controls_test.c -- touch overlay logic against synthesised SDL events; no window, no GL. */
#include "touch_controls.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ui_flat stand-ins: the test links no GL; tc_draw is exercised against these. */
static int uf_calls;
UiFlat *uf_create(void) { return NULL; }
void uf_destroy(UiFlat *u) { (void)u; }
void uf_begin(UiFlat *u, int w, int h) { (void)u; (void)w; (void)h; uf_calls++; }
void uf_end(UiFlat *u) { (void)u; }
void uf_color(UiFlat *u, float r, float g, float b, float a) { (void)u; (void)r; (void)g; (void)b; (void)a; }
void uf_rect(UiFlat *u, float x, float y, float w, float h) { (void)u; (void)x; (void)y; (void)w; (void)h; uf_calls++; }
void uf_circle(UiFlat *u, float cx, float cy, float r) { (void)u; (void)cx; (void)cy; (void)r; uf_calls++; }
void uf_ring(UiFlat *u, float cx, float cy, float r, float w) { (void)u; (void)cx; (void)cy; (void)r; (void)w; uf_calls++; }
void uf_triangle(UiFlat *u, float a, float b, float c, float d, float e, float f) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; uf_calls++; }

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define BIT(a) (1u << (a))

static SDL_Event finger(Uint32 type, int id, float x, float y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = type; e.tfinger.fingerId = id; e.tfinger.x = x; e.tfinger.y = y;
    return e;
}
static SDL_Event mouse(Uint32 type, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = type; e.button.which = SDL_TOUCH_MOUSEID; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    if (type == SDL_MOUSEMOTION) { e.motion.state = SDL_BUTTON_LMASK; }
    return e;
}
/* Normalised finger position of a button centre (offset in height units). */
static void at(const TouchControls *t, int b, float *nx, float *ny, float dx) {
    float cx, cy, r; tc_layout(t, b, &cx, &cy, &r);
    *nx = (cx + dx) / t->aspect; *ny = cy;
}
static uint32_t down(TouchControls *t, int id, int b) {
    float x, y; at(t, b, &x, &y, 0); SDL_Event e = finger(SDL_FINGERDOWN, id, x, y); return tc_event(t, &e);
}
static uint32_t up(TouchControls *t, int id, int b, float dx) {
    float x, y; at(t, b, &x, &y, dx); SDL_Event e = finger(SDL_FINGERUP, id, x, y); return tc_event(t, &e);
}
static uint32_t move(TouchControls *t, int id, float x, float y) {
    SDL_Event e = finger(SDL_FINGERMOTION, id, x, y); return tc_event(t, &e);
}
static uint32_t tf(TouchControls *t, Uint32 type, int id, float x, float y, Uint32 ms) {
    SDL_Event e = finger(type, id, x, y); e.tfinger.timestamp = ms; return tc_event(t, &e);
}
static void fresh(TouchControls *t, unsigned ctx) {
    tc_init(t, 1, 0, 0); tc_set_frame(t, 0, ctx, 16.0f / 9.0f);
}

int main(void) {
    TouchControls t;

    /* held bits per button */
    static const struct { int b, act; unsigned ctx; } map[] = {
        { TC_GAS, CB_ACT_THROTTLE, 0 }, { TC_BRAKE, CB_ACT_BRAKE, 0 }, { TC_STEER_L, CB_ACT_STEER_LEFT, 0 },
        { TC_STEER_R, CB_ACT_STEER_RIGHT, 0 }, { TC_NOS, CB_ACT_NITROUS, 0 }, { TC_HANDBRAKE, CB_ACT_HANDBRAKE, 0 },
        { TC_CAMERA, CB_ACT_CAMERA, 0 }, { TC_SHIFT_UP, CB_ACT_SHIFT_UP, TC_CTX_DRAG },
        { TC_SHIFT_DOWN, CB_ACT_SHIFT_DOWN, TC_CTX_DRAG }, { TC_ENTER_RACE, CB_ACT_ENTER_RACE, TC_CTX_OUTRUN },
    };
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
        fresh(&t, map[i].ctx);
        CHECK(tc_held(&t) == 0);
        down(&t, 1, map[i].b);
        CHECK(tc_visible(&t));
        CHECK(tc_held(&t) == BIT(map[i].act));
        up(&t, 1, map[i].b, 0);
        CHECK(tc_held(&t) == 0);
    }

    /* press edges */
    fresh(&t, 0);
    CHECK(down(&t, 1, TC_CAMERA) == BIT(CB_ACT_CAMERA));
    up(&t, 1, TC_CAMERA, 0);
    CHECK(down(&t, 1, TC_STEER_L) == BIT(CB_ACT_STEER_LEFT));
    CHECK(down(&t, 2, TC_STEER_L) == 0);                /* second finger on a held button: no new edge */
    up(&t, 1, TC_STEER_L, 0); up(&t, 2, TC_STEER_L, 0);
    CHECK(down(&t, 1, TC_GAS) == 0);                    /* held-only button has no edge */
    up(&t, 1, TC_GAS, 0);
    /* shift: drag context only */
    CHECK(down(&t, 1, TC_SHIFT_UP) == 0 && tc_held(&t) == 0);
    fresh(&t, TC_CTX_DRAG);
    CHECK(down(&t, 1, TC_SHIFT_UP) == BIT(CB_ACT_SHIFT_UP));
    up(&t, 1, TC_SHIFT_UP, 0);
    CHECK(down(&t, 1, TC_SHIFT_DOWN) == BIT(CB_ACT_SHIFT_DOWN));
    up(&t, 1, TC_SHIFT_DOWN, 0);
    /* enter race: outrun context only */
    fresh(&t, 0);
    CHECK(down(&t, 1, TC_ENTER_RACE) == 0 && tc_held(&t) == 0);
    fresh(&t, TC_CTX_OUTRUN);
    CHECK(down(&t, 1, TC_ENTER_RACE) == BIT(CB_ACT_ENTER_RACE));
    up(&t, 1, TC_ENTER_RACE, 0);
    CHECK(down(&t, 1, TC_SHIFT_UP) == 0);               /* shift not in the outrun context */
    /* pause: fires on finger up inside */
    fresh(&t, 0);
    CHECK(down(&t, 1, TC_PAUSE) == 0);
    CHECK(tc_held(&t) == 0);
    CHECK(up(&t, 1, TC_PAUSE, 0) == BIT(CB_ACT_PAUSE));
    CHECK(down(&t, 1, TC_PAUSE) == 0);
    CHECK(up(&t, 1, TC_PAUSE, 0.5f) == 0);              /* lifted outside */
    {   /* slid off, lifted away */
        float x, y; at(&t, TC_PAUSE, &x, &y, 0);
        down(&t, 1, TC_PAUSE);
        CHECK(move(&t, 1, x, 0.5f) == 0);
        CHECK(up(&t, 1, TC_PAUSE, 0.0f) == 0);
    }

    /* multi-finger independence */
    fresh(&t, 0);
    down(&t, 1, TC_GAS); down(&t, 2, TC_STEER_R);
    CHECK(tc_held(&t) == (BIT(CB_ACT_THROTTLE) | BIT(CB_ACT_STEER_RIGHT)));
    up(&t, 2, TC_STEER_R, 0);
    CHECK(tc_held(&t) == BIT(CB_ACT_THROTTLE));
    down(&t, 3, TC_NOS);
    up(&t, 1, TC_GAS, 0);
    CHECK(tc_held(&t) == BIT(CB_ACT_NITROUS));
    up(&t, 3, TC_NOS, 0);
    CHECK(tc_held(&t) == 0);

    /* sliding off releases, sliding onto another presses */
    fresh(&t, 0);
    down(&t, 1, TC_GAS);
    CHECK(tc_held(&t) == BIT(CB_ACT_THROTTLE));
    move(&t, 1, 0.5f, 0.2f);
    CHECK(tc_held(&t) == 0);
    {
        float x, y; at(&t, TC_BRAKE, &x, &y, 0);
        move(&t, 1, x, y);
        CHECK(tc_held(&t) == BIT(CB_ACT_BRAKE));
    }
    up(&t, 1, TC_BRAKE, 0);
    CHECK(tc_held(&t) == 0);

    /* a touch outside every button shows the overlay and holds nothing */
    tc_init(&t, 1, 0, 0); tc_set_frame(&t, 0, 0, 16.0f / 9.0f);
    CHECK(!tc_visible(&t));
    { SDL_Event e = finger(SDL_FINGERDOWN, 1, 0.5f, 0.3f); CHECK(tc_event(&t, &e) == 0); }
    CHECK(tc_visible(&t) && tc_held(&t) == 0);

    /* hide on keyboard / pad / axis > 50%, releasing everything */
    for (int k = 0; k < 7; k++) {
        fresh(&t, 0);
        down(&t, 1, TC_GAS);
        SDL_Event e; memset(&e, 0, sizeof e);
        int hides = 1;
        switch (k) {
        case 0: e.type = SDL_KEYDOWN; break;
        case 1: e.type = SDL_JOYBUTTONDOWN; break;
        case 2: e.type = SDL_JOYHATMOTION; e.jhat.value = SDL_HAT_UP; break;
        case 3: e.type = SDL_JOYAXISMOTION; e.jaxis.value = 20000; break;
        case 4: e.type = SDL_JOYAXISMOTION; e.jaxis.value = -20000; break;
        case 5: e.type = SDL_CONTROLLERBUTTONDOWN; break;
        case 6: e.type = SDL_JOYAXISMOTION; e.jaxis.value = 10000; hides = 0; break;   /* under 50% */
        }
        tc_event(&t, &e);
        if (hides) { CHECK(!tc_visible(&t)); CHECK(tc_held(&t) == 0); }
        else { CHECK(tc_visible(&t)); CHECK(tc_held(&t) == BIT(CB_ACT_THROTTLE)); }
    }
    fresh(&t, 0);
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_JOYHATMOTION; e.jhat.value = SDL_HAT_CENTERED; down(&t, 1, TC_GAS);
      tc_event(&t, &e); CHECK(tc_visible(&t)); }
    /* a later touch shows it again */
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_KEYDOWN; tc_event(&t, &e); CHECK(!tc_visible(&t)); }
    down(&t, 2, TC_BRAKE);
    CHECK(tc_visible(&t) && tc_held(&t) == BIT(CB_ACT_BRAKE));

    /* backgrounding and focus loss release but keep the overlay */
    fresh(&t, 0);
    down(&t, 1, TC_GAS);
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_APP_WILLENTERBACKGROUND; tc_event(&t, &e); }
    CHECK(tc_visible(&t) && tc_held(&t) == 0);
    down(&t, 1, TC_GAS);
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_FOCUS_LOST; tc_event(&t, &e); }
    CHECK(tc_visible(&t) && tc_held(&t) == 0);

    /* start-active rule and disabled */
    tc_init(&t, 1, 1, 0); CHECK(tc_visible(&t));
    tc_init(&t, 1, 0, 0); CHECK(!tc_visible(&t));
    tc_init(&t, 0, 1, 0); CHECK(!tc_visible(&t));
    tc_set_frame(&t, 0, 0, 16.0f / 9.0f);
    CHECK(down(&t, 1, TC_CAMERA) == 0 && tc_held(&t) == 0 && !tc_visible(&t));

    /* suppressed: hidden, drops touches, ignores new ones */
    fresh(&t, 0);
    down(&t, 1, TC_GAS);
    tc_set_frame(&t, 1, 0, 16.0f / 9.0f);
    CHECK(!tc_visible(&t) && tc_held(&t) == 0);
    CHECK(down(&t, 2, TC_CAMERA) == 0);
    tc_set_frame(&t, 0, 0, 16.0f / 9.0f);
    CHECK(tc_visible(&t) && tc_held(&t) == 0);

    /* mouse-as-touch needs the flag; pixel size from tc_set_window_size */
    {
        tc_init(&t, 1, 0, 1); tc_set_frame(&t, 0, 0, 16.0f / 9.0f); tc_set_window_size(&t, 1280, 720);
        float cx, cy, r; tc_layout(&t, TC_GAS, &cx, &cy, &r);
        SDL_Event e = mouse(SDL_MOUSEBUTTONDOWN, (int)(cx * 720), (int)(cy * 720));
        tc_event(&t, &e);
        CHECK(tc_held(&t) == BIT(CB_ACT_THROTTLE));
        e = mouse(SDL_MOUSEMOTION, 640, 100); tc_event(&t, &e);
        CHECK(tc_held(&t) == 0);
        e = mouse(SDL_MOUSEBUTTONUP, 640, 100); tc_event(&t, &e);
        tc_layout(&t, TC_CAMERA, &cx, &cy, &r);
        e = mouse(SDL_MOUSEBUTTONDOWN, (int)(cx * 720), (int)(cy * 720));
        CHECK(tc_event(&t, &e) == BIT(CB_ACT_CAMERA));
        tc_init(&t, 1, 0, 0); tc_set_frame(&t, 0, 0, 16.0f / 9.0f); tc_set_window_size(&t, 1280, 720);
        e = mouse(SDL_MOUSEBUTTONDOWN, (int)(cx * 720), (int)(cy * 720));
        CHECK(tc_event(&t, &e) == 0 && !tc_visible(&t));
    }

    /* layout: every circle inside the frame, for 4:3, 16:9, 19.5:9; and the spec anchors */
    {
        const float asp[3] = { 4.0f / 3.0f, 16.0f / 9.0f, 19.5f / 9.0f };
        for (int a = 0; a < 3; a++) {
            tc_init(&t, 1, 1, 0); tc_set_frame(&t, 0, 0, asp[a]);
            for (int b = 0; b < TC_COUNT; b++) {
                float cx, cy, r; tc_layout(&t, b, &cx, &cy, &r);
                CHECK(cx - r >= 0 && cx + r <= asp[a] && cy - r >= 0 && cy + r <= 1.0f);
                for (int c = b + 1; c < TC_COUNT; c++) {
                    float x2, y2, r2; tc_layout(&t, c, &x2, &y2, &r2);
                    /* the frozen numbers let ENTER_RACE overlap the steering/brake circles at 4:3 only
                     * (hit test: nearest centre wins); nothing else overlaps at any aspect */
                    int er43 = a == 0 && (b == TC_ENTER_RACE || c == TC_ENTER_RACE);
                    if (!er43) CHECK(hypotf(cx - x2, cy - y2) >= r + r2 - 1e-4f);
                }
            }
        }
        tc_set_frame(&t, 0, 0, 16.0f / 9.0f);
        float cx, cy, r, in = 0.05f * 16.0f / 9.0f;
        tc_layout(&t, TC_GAS, &cx, &cy, &r);
        CHECK(fabsf(cx - (16.0f / 9.0f - in - 0.15f)) < 1e-5f && cy == 0.80f && r == 0.14f);
        tc_layout(&t, TC_STEER_R, &cx, &cy, &r);
        CHECK(fabsf(cx - (in + 0.45f)) < 1e-5f && cy == 0.80f && r == 0.13f);
        tc_layout(&t, TC_PAUSE, &cx, &cy, &r);
        CHECK(fabsf(cx - 16.0f / 18.0f) < 1e-5f && cy == 0.07f);
    }

    /* two-finger tap = pause: off the buttons, fingers in different frames, quick, still */
    {
        const float X1 = 0.45f, X2 = 0.55f, Y = 0.40f;   /* mid-screen: no button there */
        TouchControls t2; fresh(&t2, 0); t2.active = 1;
        CHECK(tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 1000) == 0);
        CHECK(tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 1080) == 0);           /* lands a few frames later */
        CHECK(tf(&t2, SDL_FINGERUP, 1, X1, Y, 1150) == 0);
        CHECK(tf(&t2, SDL_FINGERUP, 2, X2, Y, 1200) == BIT(CB_ACT_PAUSE));
        /* too slow */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 2000); tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 2050);
        tf(&t2, SDL_FINGERUP, 1, X1, Y, 2300); CHECK(tf(&t2, SDL_FINGERUP, 2, X2, Y, 2000 + TC_TAP_MS + 1) == 0);
        /* one finger only */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 3000); CHECK(tf(&t2, SDL_FINGERUP, 1, X1, Y, 3050) == 0);
        /* three fingers */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 4000); tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 4010); tf(&t2, SDL_FINGERDOWN, 3, 0.5f, 0.3f, 4020);
        tf(&t2, SDL_FINGERUP, 1, X1, Y, 4100); tf(&t2, SDL_FINGERUP, 2, X2, Y, 4100);
        CHECK(tf(&t2, SDL_FINGERUP, 3, 0.5f, 0.3f, 4110) == 0);
        /* a finger that moves (a swipe) */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 5000); tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 5010);
        tf(&t2, SDL_FINGERMOTION, 2, X2 + 0.1f, Y, 5050);
        tf(&t2, SDL_FINGERUP, 1, X1, Y, 5100); CHECK(tf(&t2, SDL_FINGERUP, 2, X2 + 0.1f, Y, 5120) == 0);
        /* driving: two thumbs quickly on gas and steer while the overlay shows */
        float gx, gy, sx, sy; at(&t2, TC_GAS, &gx, &gy, 0); at(&t2, TC_STEER_L, &sx, &sy, 0);
        tf(&t2, SDL_FINGERDOWN, 1, gx, gy, 6000); tf(&t2, SDL_FINGERDOWN, 2, sx, sy, 6010);
        tf(&t2, SDL_FINGERUP, 1, gx, gy, 6100); CHECK(tf(&t2, SDL_FINGERUP, 2, sx, sy, 6120) == 0);
        /* a menu opening in the middle cancels it */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 7000); tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 7010);
        tc_set_frame(&t2, 1, 0, 16.0f / 9.0f); tc_set_frame(&t2, 0, 0, 16.0f / 9.0f);
        tf(&t2, SDL_FINGERUP, 1, X1, Y, 7100); CHECK(tf(&t2, SDL_FINGERUP, 2, X2, Y, 7120) == 0);
        /* and it works again afterwards */
        tf(&t2, SDL_FINGERDOWN, 1, X1, Y, 8000); tf(&t2, SDL_FINGERDOWN, 2, X2, Y, 8010);
        tf(&t2, SDL_FINGERUP, 2, X2, Y, 8100); CHECK(tf(&t2, SDL_FINGERUP, 1, X1, Y, 8120) == BIT(CB_ACT_PAUSE));
    }

    /* tc_draw draws only while visible (the stand-in needs a non-NULL pointer) */
    {
        UiFlat *fake = (UiFlat *)&t;
        tc_init(&t, 1, 0, 0); uf_calls = 0; tc_draw(&t, fake, 1280, 720); CHECK(uf_calls == 0);
        tc_init(&t, 1, 1, 0); tc_draw(&t, fake, 1280, 720); CHECK(uf_calls > 0);
    }

    printf("touch_controls_test: %d checks, %d failures\n", checks, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
