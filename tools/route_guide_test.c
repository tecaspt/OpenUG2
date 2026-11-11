/* Synthetic, data-free tests for the route-state layer (src/route_guide.c).
 * Real shipped event routes are covered by tools/route_guide_check.c. */
#include "route_guide.h"
#include "camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static float dot2(const float a[2], float x, float y) { return a[0]*x + a[1]*y; }

/* Straight 400 m sprint along +X, gates at 0, 200, 400. */
static void straight(void) {
    float xy[41*2]; for (int i = 0; i <= 40; i++) { xy[2*i] = i*10.f; xy[2*i+1] = 0; }
    RouteGuideGate g[3] = {{0,0,1,0,22},{200,0,1,0,22},{400,0,1,0,22}};
    RouteGuideRoute r = {1,0,xy,41,0,g,3,1,1};
    RouteGuideIndex ix; CHECK(route_guide_index(&ix,&r), "index straight");
    CHECK(fabsf(ix.gate_along[1]-200) < 1e-3f && fabsf(ix.gate_along[2]-400) < 1e-3f, "gate along");
    RouteGuide o;
    CHECK(route_guide_eval(&r,&ix,50,3,NAN,&o) && o.dir[0] > .99f, "forward on a straight");
    CHECK(fabsf(o.lateral-3) < 1e-3f, "lateral %.3f", o.lateral);
    CHECK(route_guide_eval(&r,&ix,190,0,NAN,&o) && o.target_gate && fabsf(o.target[0]-200) < 1e-3f,
          "aim clamps to the armed gate");
    CHECK(!route_guide_eval(&r,&ix,100,ROUTE_GUIDE_OFFROUTE+5,NAN,&o) && o.state == ROUTE_GUIDE_OFF_ROUTE,
          "off-route hides");
    /* Gate 2 armed but the player is back near gate 0 (a grid away from the
       line, or a long detour): still guided forward to it. */
    r.next_gate = 2;
    CHECK(route_guide_eval(&r,&ix,20,0,NAN,&o) && o.dir[0] > .99f, "far behind the armed gate: forward");
    /* Past the armed gate beyond the slack without crossing it: off-route. */
    r.next_gate = 1;
    CHECK(!route_guide_eval(&r,&ix,200+ROUTE_GUIDE_SLACK+ROUTE_GUIDE_OFFROUTE+10,0,NAN,&o) && o.state == ROUTE_GUIDE_OFF_ROUTE,
          "past the armed gate is off-route, state %d", o.state);
    r.next_gate = 2;
    r.finished = 1;
    CHECK(!route_guide_eval(&r,&ix,300,0,NAN,&o) && o.state == ROUTE_GUIDE_CLEARED, "cleared hides");
    r.finished = 0; r.selected = 0;
    CHECK(!route_guide_eval(&r,&ix,300,0,NAN,&o) && o.state == ROUTE_GUIDE_UNSELECTED, "unselected hides");
    r.selected = 1; r.next_gate = 7;
    CHECK(!route_guide_eval(&r,&ix,300,0,NAN,&o) && o.state == ROUTE_GUIDE_INVALID, "bad gate index");
    r.next_gate = 1;
    float bad[4] = {0,0,NAN,1}; RouteGuideRoute rb = {1,0,bad,2,0,NULL,0,0,1};
    CHECK(!route_guide_index(&ix,&rb), "non-finite course rejected");
}

/* Out-and-back sprint: east along y=0, a U-turn, west along y=3 (two lanes of
   one street). The driver drifts toward the other lane, so the nearest node
   often belongs to the wrong pass.
   Gate windows (and, inside one window, the previous edge) choose the order. */
static void out_and_back(int with_gate_at_turn) {
    float xy[200*2]; int n = 0;
    for (int i = 0; i <= 50; i++) { xy[2*n] = i*10.f; xy[2*n+1] = 0; n++; }
    for (int k = 1; k < 8; k++) { float a = -(float)M_PI*.5f + (float)M_PI*k/8;
        xy[2*n] = 500 + 1.5f*cosf(a); xy[2*n+1] = 1.5f + 1.5f*sinf(a); n++; }
    for (int i = 50; i >= 0; i--) { xy[2*n] = i*10.f; xy[2*n+1] = 3; n++; }
    RouteGuideGate g3[3] = {{0,0,1,0,6},{501.5f,1.5f,0,1,6},{0,3,-1,0,6}};
    RouteGuideGate g2[2] = {{0,0,1,0,6},{0,3,-1,0,6}};
    RouteGuideRoute r = {1,0,xy,n,0,with_gate_at_turn?g3:g2,with_gate_at_turn?3:2,1,1};
    RouteGuideIndex ix; CHECK(route_guide_index(&ix,&r), "index out-and-back");
    CHECK(fabsf(ix.gate_along[r.ngate-1]-ix.length) < 1, "final gate at the end (%.1f of %.1f)",
          ix.gate_along[r.ngate-1], ix.length);
    /* Drive the true path in order, carrying the edge hint like the host does. */
    float hint = NAN; int wrong = 0, back = 0, naive_wrong = 0, samples = 0;
    float s_true = 0;
    for (int i = 0; i < n-1; i++) {
        float ax = xy[2*i], ay = xy[2*i+1], bx = xy[2*i+2], by = xy[2*i+3];
        float len = hypotf(bx-ax, by-ay);
        for (float t = 0; t < len; t += 2.f, samples++) {
            float px = ax + (bx-ax)*t/len, py = ay + (by-ay)*t/len + (i > 57 ? -2.0f : 1.4f);
            if (with_gate_at_turn) r.next_gate = s_true + t < ix.gate_along[1] ? 1 : 2;
            RouteGuide o;
            if (!route_guide_eval(&r,&ix,px,py,hint,&o)) { wrong++; continue; }
            if (fabsf(o.along - (s_true + t)) > 12) wrong++;
            /* within one look-ahead of the hairpin the arrow legitimately
               points round the turn, against the local tangent */
            float st = s_true + t;
            if ((st < 500 - ROUTE_GUIDE_LOOKAHEAD || st > 505) && dot2(o.dir, bx-ax, by-ay) <= 0) back++;
            hint = o.along;
            /* the nearest-node rule this layer replaces */
            int nearest = 0; float nd = INFINITY;
            for (int j = 0; j < n; j++) { float d = hypotf(xy[2*j]-px, xy[2*j+1]-py); if (d < nd) { nd = d; nearest = j; } }
            if (abs(nearest - i) > 3 && abs(nearest - (i+1)) > 3) naive_wrong++;
        }
        s_true += len;
    }
    printf("out-and-back (gate at turn=%d): %d samples, wrong edge %d, backward %d, nearest-node wrong %d\n",
           with_gate_at_turn, samples, wrong, back, naive_wrong);
    CHECK(wrong == 0 && back == 0, "ordered edge kept through the overlap");
    CHECK(naive_wrong > 0, "fixture must defeat the nearest-node rule");
    /* Resident swap: the route arrays are rebuilt; the same inputs (including
       the carried hint) give the same answer, and the gate window alone
       recovers the order when there is a gate between the passes. */
    float copy[200*2]; memcpy(copy, xy, sizeof copy);
    RouteGuideRoute r2 = r; r2.xy = copy; RouteGuideIndex ix2; route_guide_index(&ix2,&r2);
    r.next_gate = r2.next_gate = r.ngate - 1;
    RouteGuide a, b, c;
    route_guide_eval(&r,&ix,250,1.0f,ix.length-252,&a); route_guide_eval(&r2,&ix2,250,1.0f,ix.length-252,&b);
    CHECK(a.state == b.state && a.edge == b.edge && a.dir[0] == b.dir[0] && a.dir[1] == b.dir[1],
          "identical after rebuilding the route buffers");
    if (with_gate_at_turn) {
        route_guide_eval(&r2,&ix2,250,1.0f,NAN,&c);
        CHECK(c.state == ROUTE_GUIDE_ON && c.dir[0] < -.9f, "window alone picks the return leg");
    }
}

/* Circuit (square loop, 4 x 200 m) with the start line mid-side: wraparound
   windows, lap 0 grid behind the line, and the run back to gate 0. */
static void circuit(void) {
    float xy[81*2]; int n = 0;
    for (int s = 0; s < 4; s++) for (int i = 0; i < 20; i++) {
        float t = i*10.f, x = s==0?t:s==1?200:s==2?200-t:0, y = s==0?0:s==1?t:s==2?200:200-t;
        xy[2*n] = x; xy[2*n+1] = y; n++;
    }
    xy[2*n] = 0; xy[2*n+1] = 0; n++;     /* closed: last == first */
    RouteGuideGate g[4] = {{100,0,1,0,22},{200,100,0,1,22},{100,200,-1,0,22},{0,100,0,-1,22}};
    RouteGuideRoute r = {1,0,xy,n,1,g,4,0,0};
    RouteGuideIndex ix; CHECK(route_guide_index(&ix,&r), "index circuit");
    CHECK(fabsf(ix.length-800) < 1e-2f && fabsf(ix.gate_along[0]) < 1e-3f &&
          fabsf(ix.gate_along[1]-200) < 1e-2f && fabsf(ix.gate_along[3]-600) < 1e-2f, "rebased gates");
    RouteGuide o;
    CHECK(route_guide_eval(&r,&ix,80,1,NAN,&o) && o.dir[0] > .99f && !o.target_gate,
          "grid behind the start line drives on through it");
    CHECK(route_guide_eval(&r,&ix,120,1,NAN,&o) && o.dir[0] > .9f && !o.target_gate,
          "grid past the start line keeps going");
    r.lap = 1; r.next_gate = 0;
    CHECK(route_guide_eval(&r,&ix,10,80,NAN,&o) && o.dir[1] < -.9f, "last leg runs down the west side");
    CHECK(route_guide_eval(&r,&ix,0,10,NAN,&o) && o.dir[0] > 0, "wraps past the corner toward the line");
    CHECK(route_guide_eval(&r,&ix,80,1,NAN,&o) && o.target_gate && fabsf(o.target[0]-100) < .01f,
          "aims at the finish line on the last metres of the lap");
    CHECK(route_guide_eval(&r,&ix,10,190,NAN,&o) && o.dir[1] < -.9f,
          "a lap-wide window: before gate 3 still heads for the finish");
    r.next_gate = 2;
    CHECK(route_guide_eval(&r,&ix,195,195,NAN,&o) && o.dir[0] < -.5f, "corner turns toward gate 2");
    /* just past the armed gate on a circuit: forward round the lap, not back */
    r.next_gate = 1;
    CHECK(route_guide_eval(&r,&ix,201,120,NAN,&o) && o.dir[1] > .9f && !o.target_gate,
          "past the armed gate on a circuit keeps going forward (%.2f %.2f)", o.dir[0], o.dir[1]);
    r.lap = 0; r.next_gate = 0;
    CHECK(route_guide_eval(&r,&ix,201,120,NAN,&o) && o.dir[1] > .9f,
          "grid beyond gate 1 before the gun points forward (%.2f %.2f)", o.dir[0], o.dir[1]);
}

/* No gates (a free-roam route): the whole course is one window. */
static void gateless(void) {
    float xy[3*2] = {0,0,100,0,100,100};
    RouteGuideRoute r = {1,0,xy,3,0,NULL,0,0,1};
    RouteGuideIndex ix; CHECK(route_guide_index(&ix,&r), "index gateless");
    RouteGuide o;
    CHECK(route_guide_eval(&r,&ix,90,0,NAN,&o) && o.dir[1] > .3f, "turns at the corner");
    CHECK(route_guide_eval(&r,&ix,100,100,NAN,&o) && o.dir[1] > .99f, "end point uses the tangent");
}

/* Screen direction under camera rotation, against the gameplay camera's own
   view matrix (cam_view_matrix): a point ahead along the guidance direction
   must land on the side of the screen the arrow leans to. */
static void camera_rotation(void) {
    int checked = 0, bad = 0;
    for (int yaw_d = 0; yaw_d < 360; yaw_d += 15) for (int dir_d = 0; dir_d < 360; dir_d += 10) {
        float yaw = yaw_d*(float)M_PI/180, da = dir_d*(float)M_PI/180;
        float dir[2] = {cosf(da), sinf(da)};
        float eye[3] = {-6*cosf(yaw), -6*sinf(yaw), 2.5f}, look[3] = {0,0,1}, up[3] = {0,0,1}, V[16];
        cam_view_matrix(eye, look, up, V);
        float view_heading = atan2f(look[1]-eye[1], look[0]-eye[0]);
        float rel = route_guide_screen_angle(dir, view_heading);
        float p[3] = {20*dir[0], 20*dir[1], 0};
        float vx = V[0]*p[0] + V[4]*p[1] + V[8]*p[2] + V[12];
        float vz = V[2]*p[0] + V[6]*p[1] + V[10]*p[2] + V[14];
        if (vz >= -1 || fabsf(sinf(rel)) < .1f) continue;   /* behind the camera / dead ahead */
        checked++;
        /* rel > 0 = CCW = left = negative view x */
        if ((rel > 0) != (vx < 0)) bad++;
    }
    printf("camera rotation: %d projections checked, %d on the wrong side\n", checked, bad);
    CHECK(checked > 400 && bad == 0, "screen side under camera rotation");
    float d[2] = {0,1};
    CHECK(fabsf(route_guide_screen_angle(d, (float)M_PI*.5f)) < 1e-6f, "straight ahead is 0");
    CHECK(fabsf(route_guide_screen_angle(d, 0) - (float)M_PI*.5f) < 1e-6f, "north with an east camera is left");
    CHECK(fabsf(fabsf(route_guide_screen_angle(d, -(float)M_PI*.5f)) - (float)M_PI) < 1e-6f, "behind wraps to pi");
}

int main(void) {
    straight();
    out_and_back(0);
    out_and_back(1);
    circuit();
    gateless();
    camera_rotation();
    printf("route-guide-test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
