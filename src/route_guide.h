/* route_guide.h -- route-state layer for the selected-route direction arrow.
 *
 * Pure and GL-free: no globals, no allocation in route_guide_eval, no engine
 * headers. The host supplies the ordered course (the event's racing line built
 * from 0x34148 route nodes, ordered by the 0x3414c outline), the ordered gates
 * (outline vertices projected onto that course, gate 0 = start/finish) and the
 * live race progress (armed gate, lap, finished). The result is a world-space
 * guidance direction, or an explicit "none" with the reason.
 *
 * Direction rule (OpenUG2 policy, not a recovered retail rule):
 *   1. Candidates are the course edges up to the armed gate (a whole lap
 *      back for a circuit; the start grid need not sit next to gate 0). At a
 *      junction or where the course passes itself, the nearest node can
 *      belong to another part of the course; near-ties are broken by forward
 *      continuity from the previous frame's along, or, without one, in favour
 *      of the stretch since the previous gate. That selects the next ORDERED
 *      edge rather than the nearest node.
 *   2. The player is projected onto the chosen edge. The aim point lies
 *      ROUTE_GUIDE_LOOKAHEAD metres further along the course, clamped to the
 *      armed gate, so the arrow never points past the gate it must clear.
 *   3. Farther than ROUTE_GUIDE_OFFROUTE metres from every candidate edge is
 *      off-route: the arrow is hidden rather than guessed.
 *
 * Every threshold here is an OpenUG2 diagnostic choice. No retail arrow
 * behaviour, asset or state table has been recovered (see
 * reports/route-guidance/REPORT.md and docs/DEVELOPER_GUIDE.md). */
#ifndef OPENUG2_ROUTE_GUIDE_H
#define OPENUG2_ROUTE_GUIDE_H

#define ROUTE_GUIDE_MAXGATE   40     /* = WORLD_MAXGATE */
#define ROUTE_GUIDE_MAXNODE   4096   /* course node cap used by the race code */
#define ROUTE_GUIDE_LOOKAHEAD 25.0f  /* metres along the course to the aim point */
#define ROUTE_GUIDE_OFFROUTE  30.0f  /* metres from the course: hide the arrow */
#define ROUTE_GUIDE_SLACK     40.0f  /* metres of window beyond the bounding gates */
#define ROUTE_GUIDE_GATE_NEAR  8.0f  /* metres from the armed gate centre: aim along its normal */
#define ROUTE_GUIDE_TIE       2.0f   /* metres: edges this close count as a tie */
#define ROUTE_GUIDE_TIE_HINT  4.0f   /* the same with a previous along: at a self-
                                        crossing the other pass can be a lane nearer */
#define ROUTE_GUIDE_BACKTRACK 2.0f   /* metres a tie may fall behind the last along */

typedef struct { float x, y, dx, dy, half; } RouteGuideGate;

/* Borrowed route state. Nothing is owned or modified. */
typedef struct {
    int selected;              /* a route/event is armed; 0 = nothing to guide */
    int finished;              /* the route was cleared (race finished/stopped) */
    const float *xy;           /* ordered course nodes, interleaved x,y */
    int n;                     /* node count, 2..ROUTE_GUIDE_MAXNODE */
    int loop;                  /* 1 = circuit (last node joins the first) */
    const RouteGuideGate *gate;/* ordered gates; may be NULL with ngate 0 */
    int ngate;                 /* 0 = whole course is one window */
    int next_gate;             /* the armed gate */
    int lap;                   /* 0 = before the start (countdown/grid) */
} RouteGuideRoute;

/* Precomputed course measures; build once per course, reuse every frame. */
typedef struct {
    int n, loop, ngate;
    float length;                         /* total, closing edge included for loops */
    float origin;                         /* along of gate 0 (loops rebase on it) */
    float gate_along[ROUTE_GUIDE_MAXGATE];/* per gate, rebased race-order metres */
    float cum[ROUTE_GUIDE_MAXNODE + 1];   /* raw along of every node (+ closure) */
} RouteGuideIndex;

enum {
    ROUTE_GUIDE_ON = 0,
    ROUTE_GUIDE_UNSELECTED,    /* no route selected: hide */
    ROUTE_GUIDE_CLEARED,       /* route finished/cleared: hide */
    ROUTE_GUIDE_OFF_ROUTE,     /* too far from the ordered course: hide */
    ROUTE_GUIDE_INVALID        /* malformed input: hide */
};

typedef struct {
    int state;                 /* ROUTE_GUIDE_* */
    float dir[2];              /* unit world XY direction (valid when ON) */
    int edge;                  /* chosen ordered edge (node edge..edge+1), -1 none */
    float along;               /* rebased race-order metres of the projection */
    float lateral;             /* metres from the chosen edge */
    float target[2];           /* aim point */
    int target_gate;           /* 1 = aim point is the armed gate centre */
    int window_edges;          /* candidate edges considered */
} RouteGuide;

/* 1 on success. Rejects non-finite/oversized input. */
int route_guide_index(RouteGuideIndex *ix, const RouteGuideRoute *r);

/* hint_along: the previous frame's RouteGuide.along (NAN = none). It only
 * breaks ties between equally near candidate edges inside the armed gate
 * window: progress continues forward, so the tie closest AHEAD of the last
 * along wins (a hairpin or parallel return leg cannot be told apart by
 * position alone).
 * Returns out->state == ROUTE_GUIDE_ON. */
int route_guide_eval(const RouteGuideRoute *r, const RouteGuideIndex *ix,
                     float px, float py, float hint_along, RouteGuide *out);

/* Screen rotation of the arrow: world direction relative to the camera's
 * horizontal view heading (radians CCW from +X). 0 = straight up the screen,
 * positive = CCW = to the left. Uses the CAMERA, not the car, so the arrow
 * stays correct when the view rotates independently (orbit, look-back). */
float route_guide_screen_angle(const float dir[2], float view_heading);

const char *route_guide_state_name(int state);

#endif
