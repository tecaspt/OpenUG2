/* touch_controls.c -- see touch_controls.h. */
#include "touch_controls.h"

#include <math.h>
#include <string.h>

#define TC_MOUSE_ID ((int64_t)-1)
#define TC_AXIS_HIDE 16383          /* more than 50% of full deflection */

/* Layout table: x anchor (0 = left inset, 1 = right inset, 2 = centre), x offset, y, radius. */
typedef struct { int anchor; float dx, y, r; int action; } TcSpec;
static const TcSpec TC_SPEC[TC_COUNT] = {
    [TC_STEER_L]    = { 0, 0.15f, 0.80f, 0.13f, CB_ACT_STEER_LEFT },
    [TC_STEER_R]    = { 0, 0.45f, 0.80f, 0.13f, CB_ACT_STEER_RIGHT },
    [TC_GAS]        = { 1, 0.15f, 0.80f, 0.14f, CB_ACT_THROTTLE },
    [TC_BRAKE]      = { 1, 0.45f, 0.83f, 0.11f, CB_ACT_BRAKE },
    [TC_NOS]        = { 1, 0.15f, 0.50f, 0.09f, CB_ACT_NITROUS },
    [TC_HANDBRAKE]  = { 1, 0.42f, 0.55f, 0.08f, CB_ACT_HANDBRAKE },
    [TC_CAMERA]     = { 2, 0.16f, 0.07f, 0.06f, CB_ACT_CAMERA },
    [TC_PAUSE]      = { 2, 0.00f, 0.07f, 0.06f, CB_ACT_PAUSE },
    [TC_SHIFT_UP]   = { 1, 0.15f, 0.30f, 0.08f, CB_ACT_SHIFT_UP },
    [TC_SHIFT_DOWN] = { 1, 0.35f, 0.30f, 0.08f, CB_ACT_SHIFT_DOWN },
    [TC_ENTER_RACE] = { 2, 0.00f, 0.70f, 0.09f, CB_ACT_ENTER_RACE },
};

void tc_init(TouchControls *t, int enabled, int start_active, int accept_mouse_touch) {
    memset(t, 0, sizeof *t);
    t->enabled = enabled ? 1 : 0;
    t->active = (t->enabled && start_active) ? 1 : 0;
    t->accept_mouse = accept_mouse_touch ? 1 : 0;
    t->aspect = 16.0f / 9.0f;
}

void tc_set_window_size(TouchControls *t, int w, int h) { t->win_w = w; t->win_h = h; }

void tc_layout(const TouchControls *t, int btn, float *cx, float *cy, float *r) {
    float W = (t && t->aspect > 0.0f) ? t->aspect : 16.0f / 9.0f;
    float inset = 0.05f * W;
    if (btn < 0 || btn >= TC_COUNT) { if (cx) *cx = 0; if (cy) *cy = 0; if (r) *r = 0; return; }
    const TcSpec *s = &TC_SPEC[btn];
    float x = s->anchor == 0 ? inset + s->dx : s->anchor == 1 ? W - inset - s->dx : W * 0.5f + s->dx;
    if (cx) *cx = x;
    if (cy) *cy = s->y;
    if (r) *r = s->r;
}

static int tc_btn_enabled(const TouchControls *t, int b) {
    if (b == TC_SHIFT_UP || b == TC_SHIFT_DOWN) return (t->ctx & TC_CTX_DRAG) != 0;
    if (b == TC_ENTER_RACE) return (t->ctx & TC_CTX_OUTRUN) != 0;
    return 1;
}

int tc_visible(const TouchControls *t) { return t->enabled && t->active && !t->suppressed; }

static void tc_release_all(TouchControls *t) { memset(t->touch, 0, sizeof t->touch); t->tap_max = 0; t->tap_spoiled = 1; }

/* Number of touches over a button. */
static int tc_count(const TouchControls *t, int b) {
    int n = 0;
    for (int i = 0; i < TC_MAX_TOUCHES; i++) if (t->touch[i].used && t->touch[i].btn == b) n++;
    return n;
}

void tc_set_frame(TouchControls *t, int suppressed, unsigned ctx, float aspect) {
    if (suppressed && !t->suppressed) tc_release_all(t);
    t->suppressed = suppressed ? 1 : 0;
    if (ctx != t->ctx) {
        t->ctx = ctx;
        for (int i = 0; i < TC_MAX_TOUCHES; i++)
            if (t->touch[i].used && t->touch[i].btn >= 0 && !tc_btn_enabled(t, t->touch[i].btn)) t->touch[i].btn = -1;
    }
    if (aspect > 0.0f) t->aspect = aspect;
}

/* The enabled button whose circle holds the point (height units); nearest centre wins. */
static int tc_hit(const TouchControls *t, float x, float y) {
    int best = -1;
    float bd = 1e9f;
    for (int b = 0; b < TC_COUNT; b++) {
        if (!tc_btn_enabled(t, b)) continue;
        float cx, cy, r;
        tc_layout(t, b, &cx, &cy, &r);
        float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (d <= r * r && d < bd) { bd = d; best = b; }
    }
    return best;
}

static TcTouch *tc_find(TouchControls *t, int64_t id) {
    for (int i = 0; i < TC_MAX_TOUCHES; i++) if (t->touch[i].used && t->touch[i].id == id) return &t->touch[i];
    return NULL;
}

/* Press-edge bit for a button that just gained its first touch. */
static uint32_t tc_edge_bit(int b) {
    switch (b) {
    case TC_CAMERA: case TC_STEER_L: case TC_STEER_R: case TC_SHIFT_UP: case TC_SHIFT_DOWN: case TC_ENTER_RACE:
        return 1u << TC_SPEC[b].action;
    default: return 0;
    }
}

/* Moves a touch onto button `nb`; returns the press edge, if any. */
static uint32_t tc_move_to(TouchControls *t, TcTouch *tt, int nb) {
    if (tt->btn == nb) return 0;
    int first = nb >= 0 && tc_count(t, nb) == 0;
    tt->btn = nb;
    return first ? tc_edge_bit(nb) : 0;
}

static int tc_touches(const TouchControls *t) {
    int n = 0;
    for (int i = 0; i < TC_MAX_TOUCHES; i++) n += t->touch[i].used != 0;
    return n;
}

static uint32_t tc_down(TouchControls *t, int64_t id, float nx, float ny, uint32_t ms) {
    int was_visible = tc_visible(t);
    t->active = 1;
    if (t->suppressed) return 0;
    TcTouch *tt = tc_find(t, id);
    if (!tt) {
        for (int i = 0; i < TC_MAX_TOUCHES; i++) if (!t->touch[i].used) { tt = &t->touch[i]; break; }
        if (!tt) return 0;
        if (tc_touches(t) == 0) { t->tap_t0 = ms; t->tap_max = 0; t->tap_spoiled = 0; }
        tt->used = 1; tt->id = id; tt->btn = -1;
        tt->sx = nx * t->aspect; tt->sy = ny;
    }
    int hit = tc_hit(t, nx * t->aspect, ny);
    int n = tc_touches(t);
    if (n > t->tap_max) t->tap_max = n;
    if ((was_visible && hit >= 0) || id == TC_MOUSE_ID || n > 2 || ms - t->tap_t0 > TC_TAP_MS) t->tap_spoiled = 1;
    return tc_move_to(t, tt, hit);
}

static uint32_t tc_motion(TouchControls *t, int64_t id, float nx, float ny) {
    TcTouch *tt = tc_find(t, id);
    if (!tt || t->suppressed) return 0;
    float dx = nx * t->aspect - tt->sx, dy = ny - tt->sy;
    if (dx * dx + dy * dy > TC_TAP_SLOP * TC_TAP_SLOP) t->tap_spoiled = 1;
    return tc_move_to(t, tt, tc_hit(t, nx * t->aspect, ny));
}

static uint32_t tc_up(TouchControls *t, int64_t id, float nx, float ny, uint32_t ms) {
    TcTouch *tt = tc_find(t, id);
    if (!tt) return 0;
    uint32_t edge = 0;
    int b = tt->btn;
    if (b == TC_PAUSE && tc_hit(t, nx * t->aspect, ny) == TC_PAUSE && tc_count(t, TC_PAUSE) == 1)
        edge = 1u << CB_ACT_PAUSE;
    memset(tt, 0, sizeof *tt);
    if (tc_touches(t) == 0) {
        if (!t->suppressed && !t->tap_spoiled && t->tap_max == 2 && ms - t->tap_t0 <= TC_TAP_MS)
            edge |= 1u << CB_ACT_PAUSE;
        t->tap_max = 0; t->tap_spoiled = 1;
    }
    return edge;
}

/* Mouse position to 0..1; 0 when the window size is unknown. */
static int tc_mouse_norm(const TouchControls *t, Uint32 window_id, int mx, int my, float *nx, float *ny) {
    int w = 0, h = 0;
    SDL_Window *win = window_id ? SDL_GetWindowFromID(window_id) : NULL;
    if (!win) win = SDL_GetMouseFocus();
    if (win) SDL_GetWindowSize(win, &w, &h);
    if (w <= 0 || h <= 0) { w = t->win_w; h = t->win_h; }
    if (w <= 0 || h <= 0) return 0;
    *nx = (float)mx / (float)w;
    *ny = (float)my / (float)h;
    return 1;
}

uint32_t tc_event(TouchControls *t, const SDL_Event *e) {
    if (!t->enabled) return 0;
    float nx, ny;
    switch (e->type) {
    case SDL_FINGERDOWN: return tc_down(t, (int64_t)e->tfinger.fingerId, e->tfinger.x, e->tfinger.y, e->tfinger.timestamp);
    case SDL_FINGERMOTION: return tc_motion(t, (int64_t)e->tfinger.fingerId, e->tfinger.x, e->tfinger.y);
    case SDL_FINGERUP: return tc_up(t, (int64_t)e->tfinger.fingerId, e->tfinger.x, e->tfinger.y, e->tfinger.timestamp);
    case SDL_MOUSEBUTTONDOWN:
        if (!t->accept_mouse || e->button.button != SDL_BUTTON_LEFT) return 0;
        if (!tc_mouse_norm(t, e->button.windowID, e->button.x, e->button.y, &nx, &ny)) { t->active = 1; return 0; }
        return tc_down(t, TC_MOUSE_ID, nx, ny, e->button.timestamp);
    case SDL_MOUSEMOTION:
        if (!t->accept_mouse || !(e->motion.state & SDL_BUTTON_LMASK)) return 0;
        if (!tc_mouse_norm(t, e->motion.windowID, e->motion.x, e->motion.y, &nx, &ny)) return 0;
        return tc_motion(t, TC_MOUSE_ID, nx, ny);
    case SDL_MOUSEBUTTONUP:
        if (!t->accept_mouse || e->button.button != SDL_BUTTON_LEFT) return 0;
        if (!tc_mouse_norm(t, e->button.windowID, e->button.x, e->button.y, &nx, &ny)) { tc_release_all(t); return 0; }
        return tc_up(t, TC_MOUSE_ID, nx, ny, e->button.timestamp);
    case SDL_KEYDOWN:
    case SDL_JOYBUTTONDOWN:
    case SDL_CONTROLLERBUTTONDOWN:
        t->active = 0; tc_release_all(t); return 0;
    case SDL_JOYHATMOTION:
        if (e->jhat.value != SDL_HAT_CENTERED) { t->active = 0; tc_release_all(t); }
        return 0;
    case SDL_JOYAXISMOTION:
        if (e->jaxis.value > TC_AXIS_HIDE || e->jaxis.value < -TC_AXIS_HIDE) { t->active = 0; tc_release_all(t); }
        return 0;
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.value > TC_AXIS_HIDE || e->caxis.value < -TC_AXIS_HIDE) { t->active = 0; tc_release_all(t); }
        return 0;
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
        tc_release_all(t); return 0;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) tc_release_all(t);
        return 0;
    default: return 0;
    }
}

uint32_t tc_held(const TouchControls *t) {
    uint32_t m = 0;
    if (!tc_visible(t)) return 0;
    for (int b = 0; b < TC_COUNT; b++)
        if (b != TC_PAUSE && tc_count(t, b) > 0) m |= 1u << TC_SPEC[b].action;
    return m;
}

/* ---- drawing ---- */
static void tc_icon(UiFlat *uf, int b, float cx, float cy, float r) {
    float k = r * 0.45f;
    switch (b) {
    case TC_STEER_L: uf_triangle(uf, cx - k, cy, cx + k * 0.6f, cy - k, cx + k * 0.6f, cy + k); break;
    case TC_STEER_R: uf_triangle(uf, cx + k, cy, cx - k * 0.6f, cy - k, cx - k * 0.6f, cy + k); break;
    case TC_GAS: case TC_SHIFT_UP: uf_triangle(uf, cx, cy - k, cx - k, cy + k * 0.7f, cx + k, cy + k * 0.7f); break;
    case TC_BRAKE: case TC_SHIFT_DOWN: uf_triangle(uf, cx, cy + k, cx - k, cy - k * 0.7f, cx + k, cy - k * 0.7f); break;
    case TC_NOS: uf_triangle(uf, cx + k * 0.3f, cy - k, cx - k * 0.6f, cy + k * 0.1f, cx + k * 0.1f, cy + k * 0.1f);
                 uf_triangle(uf, cx - k * 0.1f, cy - k * 0.1f, cx + k * 0.6f, cy - k * 0.1f, cx - k * 0.3f, cy + k); break;
    case TC_HANDBRAKE: uf_rect(uf, cx - k * 0.2f, cy - k, k * 0.4f, k * 1.3f); uf_rect(uf, cx - k * 0.2f, cy + k * 0.5f, k * 0.4f, k * 0.4f); break;
    case TC_CAMERA: uf_rect(uf, cx - k, cy - k * 0.6f, k * 2.0f, k * 1.3f); break;
    case TC_PAUSE: uf_rect(uf, cx - k * 0.7f, cy - k, k * 0.5f, k * 2.0f); uf_rect(uf, cx + k * 0.2f, cy - k, k * 0.5f, k * 2.0f); break;
    case TC_ENTER_RACE: uf_triangle(uf, cx + k, cy, cx - k * 0.7f, cy - k, cx - k * 0.7f, cy + k); break;
    default: break;
    }
}

void tc_draw(const TouchControls *t, UiFlat *uf, int w, int h) {
    if (!t || !uf || w <= 0 || h <= 0 || !tc_visible(t)) return;
    uf_begin(uf, w, h);
    for (int b = 0; b < TC_COUNT; b++) {
        if (!tc_btn_enabled(t, b)) continue;
        float cx, cy, r;
        tc_layout(t, b, &cx, &cy, &r);
        float px = cx * (float)h, py = cy * (float)h, pr = r * (float)h;
        int held = tc_count(t, b) > 0;
        /* Faint fill, solid rim and a light glyph: the HUD gauges under the thumbs stay readable. */
        uf_color(uf, 1.0f, 1.0f, 1.0f, held ? 0.30f : 0.08f);
        uf_circle(uf, px, py, pr);
        uf_color(uf, 1.0f, 1.0f, 1.0f, held ? 0.85f : 0.55f);
        uf_ring(uf, px, py, pr, pr * 0.07f + 1.0f);
        uf_color(uf, 1.0f, 1.0f, 1.0f, held ? 0.90f : 0.60f);
        tc_icon(uf, b, px, py, pr);
    }
    uf_end(uf);
}
