/* route_guide.c -- see route_guide.h. Pure arithmetic over borrowed arrays. */
#include "route_guide.h"
#include <math.h>
#include <stddef.h>

static int edge_count(const RouteGuideIndex *ix) { return ix->loop ? ix->n : ix->n - 1; }

static void edge_points(const RouteGuideRoute *r, int i, float a[2], float b[2]) {
    int j = (i + 1) % r->n;
    a[0] = r->xy[2*i]; a[1] = r->xy[2*i+1];
    b[0] = r->xy[2*j]; b[1] = r->xy[2*j+1];
}

/* Raw along -> race-order metres (loops rebased on gate 0, wrapped to [0,L)). */
static float rebase(const RouteGuideIndex *ix, float raw) {
    if (!ix->loop) return raw;
    float v = fmodf(raw - ix->origin, ix->length);
    if (v < 0) v += ix->length;
    return v;
}

/* Projection of p onto edge i: parameter t in [0,1], distance, raw along. */
static float project(const RouteGuideRoute *r, const RouteGuideIndex *ix, int i,
                     float px, float py, float *t_out, float *raw_out) {
    float a[2], b[2]; edge_points(r, i, a, b);
    float dx = b[0]-a[0], dy = b[1]-a[1], len2 = dx*dx + dy*dy, t = 0;
    if (len2 > 1e-8f) t = ((px-a[0])*dx + (py-a[1])*dy) / len2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float qx = a[0] + t*dx, qy = a[1] + t*dy;
    if (t_out) *t_out = t;
    if (raw_out) *raw_out = ix->cum[i] + t * (ix->cum[i+1] - ix->cum[i]);
    return hypotf(px-qx, py-qy);
}

/* Course point at race-order metres s (wrapped for loops, clamped otherwise). */
static void point_at(const RouteGuideRoute *r, const RouteGuideIndex *ix, float s, float out[2]) {
    float raw = s;
    if (ix->loop) { raw = fmodf(s + ix->origin, ix->length); if (raw < 0) raw += ix->length; }
    else raw = raw < 0 ? 0 : raw > ix->length ? ix->length : raw;
    int lo = 0, hi = edge_count(ix) - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (ix->cum[mid] <= raw) lo = mid; else hi = mid - 1; }
    float a[2], b[2]; edge_points(r, lo, a, b);
    float span = ix->cum[lo+1] - ix->cum[lo];
    float t = span > 1e-6f ? (raw - ix->cum[lo]) / span : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    out[0] = a[0] + t*(b[0]-a[0]); out[1] = a[1] + t*(b[1]-a[1]);
}

int route_guide_index(RouteGuideIndex *ix, const RouteGuideRoute *r) {
    if (!ix || !r || !r->xy || r->n < 2 || r->n > ROUTE_GUIDE_MAXNODE ||
        r->ngate < 0 || r->ngate > ROUTE_GUIDE_MAXGATE || (r->ngate && !r->gate)) return 0;
    ix->n = r->n; ix->loop = r->loop != 0; ix->ngate = r->ngate;
    ix->cum[0] = 0; ix->origin = 0;
    int edges = edge_count(ix);
    for (int i = 0; i < edges; i++) {
        float a[2], b[2]; edge_points(r, i, a, b);
        if (!isfinite(a[0]) || !isfinite(a[1]) || !isfinite(b[0]) || !isfinite(b[1])) return 0;
        ix->cum[i+1] = ix->cum[i] + hypotf(b[0]-a[0], b[1]-a[1]);
    }
    ix->length = ix->cum[edges];
    if (!(ix->length > 1.0f) || !isfinite(ix->length)) return 0;
    /* Gates are ordered; project each at or after its predecessor so a course
       that passes the same street twice cannot place a gate on the other pass. */
    float prev = -INFINITY;
    for (int k = 0; k < r->ngate; k++) {
        const RouteGuideGate *g = r->gate + k;
        if (!isfinite(g->x) || !isfinite(g->y)) return 0;
        float best = INFINITY, along = 0;
        for (int i = 0; i < edges; i++) {
            float raw, d = project(r, ix, i, g->x, g->y, NULL, &raw);
            float s = k ? rebase(ix, raw) : raw;
            if (k && s < prev - 1.0f) continue;
            if (d < best) { best = d; along = s; }
        }
        if (!isfinite(best)) return 0;
        if (k == 0) { if (ix->loop) { ix->origin = along; along = 0; } }
        ix->gate_along[k] = along; prev = along;
    }
    return 1;
}

/* Candidate window [lo,hi] in race-order metres: everything up to (and a
   little past) the armed gate -- a whole lap back for a circuit, the whole
   course before it for a sprint -- because the start grid need not lie next
   to gate 0 (event 4012's grid is ~1 km round the lap from it), and a player
   may drive a long way before reaching the armed gate. [sub,hi] is the
   stretch since the previous gate: without a hint, a candidate there wins a
   tie, which keeps parallel passes of the same street apart. aim is the
   armed gate's along: the arrow never points past it. */
static void window(const RouteGuideRoute *r, const RouteGuideIndex *ix,
                   float *lo, float *hi, float *sub, float *aim) {
    float L = ix->length;
    if (r->ngate <= 0) {
        *lo = *sub = ix->loop ? -L : 0; *hi = ix->loop ? 2*L : L;
        *aim = ix->loop ? INFINITY : L;
        return;
    }
    const float *A = ix->gate_along;
    /* Before the gun, gate 1 is what the start arms (world_race_begin);
       driving forward to it crosses gate 0 on the way if the grid is behind. */
    int g = r->lap <= 0 ? (r->ngate > 1 ? 1 : 0) : r->next_gate;
    float target = g > 0 ? A[g] : (ix->loop ? L : A[0]);
    float prev = r->lap <= 0 ? A[0] - 4*ROUTE_GUIDE_SLACK
               : g > 0 ? A[g-1] : A[r->ngate-1];
    *aim = target;
    /* A circuit's window is exactly the lap ending at the armed gate: a pose
       just past that gate (a grid a few metres beyond gate 1, as events 4605
       and 4708 have, or projection jitter on the line) is a lap short of it
       and is guided forward round the lap, never back toward the gate. Only
       an open course keeps the slack past the gate, where turning back is
       the one way to cross it. */
    *hi = ix->loop ? target : target + ROUTE_GUIDE_SLACK;
    *lo = ix->loop ? target - L : -ROUTE_GUIDE_SLACK;
    *sub = prev - ROUTE_GUIDE_SLACK;
    if (*sub < *lo) *sub = *lo;
}

int route_guide_eval(const RouteGuideRoute *r, const RouteGuideIndex *ix,
                     float px, float py, float hint_along, RouteGuide *out) {
    if (!out) return 0;
    out->state = ROUTE_GUIDE_INVALID; out->dir[0] = out->dir[1] = 0;
    out->edge = -1; out->along = out->lateral = 0;
    out->target[0] = out->target[1] = 0; out->target_gate = 0; out->window_edges = 0;
    if (!r || !r->selected) { out->state = ROUTE_GUIDE_UNSELECTED; return 0; }
    if (r->finished) { out->state = ROUTE_GUIDE_CLEARED; return 0; }
    if (!ix || !r->xy || r->n != ix->n || r->n < 2 || (r->loop != 0) != ix->loop ||
        r->ngate != ix->ngate || !isfinite(px) || !isfinite(py) ||
        (r->ngate > 0 && (r->next_gate < 0 || r->next_gate >= r->ngate))) return 0;

    float lo, hi, sub, aim, L = ix->length;
    window(r, ix, &lo, &hi, &sub, &aim);
    int edges = edge_count(ix);
    /* Pass 1: nearest distance among in-window edges. */
    float best = INFINITY;
    for (int i = 0; i < edges; i++) {
        float raw, d = project(r, ix, i, px, py, NULL, &raw);
        float s = rebase(ix, raw);
        for (int w = -1; w <= 1; w++) {
            if (!ix->loop && w) continue;
            float u = s + w*L;
            if (u <= lo || u > hi) continue;
            out->window_edges++;
            if (d < best) best = d;
        }
    }
    if (!isfinite(best)) { out->state = ROUTE_GUIDE_OFF_ROUTE; return 0; }
    if (best > ROUTE_GUIDE_OFFROUTE) { out->state = ROUTE_GUIDE_OFF_ROUTE; out->lateral = best; return 0; }
    /* Pass 2: among (near-)ties pick the next ORDERED edge: the nearest one
       ahead of the previous along (progress runs forward); without a hint,
       the earliest since the previous gate, else the latest before the
       armed gate. */
    float hint = isfinite(hint_along) ? hint_along : NAN;
    float tie = isfinite(hint) ? ROUTE_GUIDE_TIE_HINT : ROUTE_GUIDE_TIE;
    int pick = -1; float pick_s = 0, pick_d = 0, pick_cost = INFINITY;
    for (int i = 0; i < edges; i++) {
        float raw, d = project(r, ix, i, px, py, NULL, &raw);
        if (d > best + tie) continue;
        float s = rebase(ix, raw);
        for (int w = -1; w <= 1; w++) {
            if (!ix->loop && w) continue;
            float u = s + w*L;
            if (u <= lo || u > hi) continue;
            float cost;
            if (isfinite(hint)) {
                float dh = u - hint;
                if (ix->loop) {           /* the hint's representation nearest u */
                    if (fabsf(dh - L) < fabsf(dh)) dh -= L;
                    if (fabsf(dh + L) < fabsf(dh)) dh += L;
                }
                cost = dh >= -ROUTE_GUIDE_BACKTRACK ? fabsf(dh) : 1e6f - dh;
            } else cost = u >= sub ? u - sub : 1e6f + (hi - u);
            if (cost < pick_cost) { pick_cost = cost; pick = i; pick_s = u; pick_d = d; }
        }
    }
    if (pick < 0) { out->state = ROUTE_GUIDE_OFF_ROUTE; return 0; }
    float st = pick_s + ROUTE_GUIDE_LOOKAHEAD;
    int at_gate = 0;
    if (st >= aim) { st = aim; at_gate = r->ngate > 0; }
    float target[2]; point_at(r, ix, st, target);
    float vx = target[0] - px, vy = target[1] - py, vl = hypotf(vx, vy);
    if (vl < 0.5f) {                 /* standing on the aim point: edge tangent */
        float a[2], b[2]; edge_points(r, pick, a, b);
        vx = b[0]-a[0]; vy = b[1]-a[1]; vl = hypotf(vx, vy);
        if (vl < 1e-6f) return 0;
    }
    out->state = ROUTE_GUIDE_ON;
    out->dir[0] = vx / vl; out->dir[1] = vy / vl;
    out->edge = pick; out->along = pick_s; out->lateral = pick_d;
    out->target[0] = target[0]; out->target[1] = target[1]; out->target_gate = at_gate;
    return 1;
}

float route_guide_screen_angle(const float dir[2], float view_heading) {
    float a = atan2f(dir[1], dir[0]) - view_heading;
    a = fmodf(a, 2.0f * (float)M_PI);
    if (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    if (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

const char *route_guide_state_name(int state) {
    switch (state) {
    case ROUTE_GUIDE_ON: return "on";
    case ROUTE_GUIDE_UNSELECTED: return "unselected";
    case ROUTE_GUIDE_CLEARED: return "cleared";
    case ROUTE_GUIDE_OFF_ROUTE: return "off-route";
    default: return "invalid";
    }
}
