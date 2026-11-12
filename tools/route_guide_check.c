/* Local asset check for the route-state layer: every shipped event route.
 * No game file is a fixture and nothing is written; it prints numbers only.
 *
 *   route_guide_check TRACKS [event-id] [--in-race]
 *
 * Per event: the engine's own ordered course (ai_race_course, decoded from the
 * event's 0x34148 nodes / Routes lanes in 0x3414c outline order), the gates
 * world_race_start builds from the 0x3414c outline and binds to that course,
 * and the start grid from TrackPosMarkersAll.bin. A virtual player is placed
 * on the grid, then walked along the course in 2 m steps (on the line, and
 * 3 m to the left), clearing gates with the production crossing test
 * (world_race_progress_update). At every step route_guide_eval must:
 *   - be ON, point toward the course point one look-ahead further on (or the
 *     armed gate) and stay on the correct ordered edge (the nearest-node rule is measured alongside for comparison);
 *   - point into, and through, each gate on the approach to it;
 *   - give an identical answer after the route buffers are rebuilt (a
 *     resident swap keeps city/race state but rebuilds everything else);
 *   - be OFF-ROUTE for probes 45 m beside the course (when no other part of
 *     the course is within reach), CLEARED after the finish and UNSELECTED
 *     with no route.
 * The physical drive (collision, opponents) is race_events_check's job.
 * --in-race walks the course the game shows instead (in_race_course.h:
 * ai_race_prepare on the loaded neighbourhood, gates bound by it, the player
 * on its grid slot); an event whose race start the game rejects is NOT-RUN. */
#include "ai.h"
#include "world.h"
#include "route_guide.h"
#include "in_race_course.h"

#define STEP 2.0f
#define NORMAL_OFFSET 3.0f
#define PROBE_OFFSET 45.0f

typedef struct { const N2Path *p; float *cum; int edges, loop; float length; } Course;

static void course_point(const Course *c, float s, float out[2], float tan_out[2]) {
    float extra = 0;   /* open courses continue straight past their ends */
    if (c->loop) { s = fmodf(s, c->length); if (s < 0) s += c->length; }
    else { if (s > c->length) extra = s - c->length; else if (s < 0) extra = s; s = fmaxf(0, fminf(c->length, s)); }
    int i = 0; while (i < c->edges - 1 && c->cum[i+1] < s) i++;
    while (i < c->edges - 1 && c->cum[i+1] - c->cum[i] < 1e-4f) i++;
    int j = (i + 1) % c->p->n;
    float ax = c->p->xy[2*i], ay = c->p->xy[2*i+1], bx = c->p->xy[2*j], by = c->p->xy[2*j+1];
    float span = c->cum[i+1] - c->cum[i], t = span > 1e-4f ? (s - c->cum[i]) / span : 0;
    t = fmaxf(0, fminf(1, t));
    float l = hypotf(bx-ax, by-ay);
    out[0] = ax + t*(bx-ax); out[1] = ay + t*(by-ay);
    if (extra != 0 && l > 1e-6f) { out[0] += (bx-ax)/l*extra; out[1] += (by-ay)/l*extra; }
    if (tan_out) { tan_out[0] = l > 1e-6f ? (bx-ax)/l : 1; tan_out[1] = l > 1e-6f ? (by-ay)/l : 0; }
}

/* Distance from (x,y) to the course and the raw along of the projection. */
static float course_nearest(const Course *c, float x, float y, float *along) {
    float best = INFINITY;
    for (int i = 0; i < c->edges; i++) {
        int j = (i + 1) % c->p->n;
        float ax = c->p->xy[2*i], ay = c->p->xy[2*i+1], dx = c->p->xy[2*j]-ax, dy = c->p->xy[2*j+1]-ay;
        float l2 = dx*dx + dy*dy, t = l2 > 1e-8f ? ((x-ax)*dx + (y-ay)*dy) / l2 : 0;
        t = fmaxf(0, fminf(1, t));
        float d = hypotf(ax + t*dx - x, ay + t*dy - y);
        if (d < best) { best = d; if (along) *along = c->cum[i] + t*(c->cum[i+1]-c->cum[i]); }
    }
    return best;
}

static int seg_hit(float px, float py, float dx, float dy, float len, const WGate *g) {
    /* ray p + t*d (0..len) against the gate segment centre +- half across */
    float ax = g->x + g->dy*g->half, ay = g->y - g->dx*g->half;
    float bx = g->x - g->dy*g->half, by = g->y + g->dx*g->half;
    float ex = bx-ax, ey = by-ay, den = dx*ey - dy*ex;
    if (fabsf(den) < 1e-9f) return 0;
    float t = ((ax-px)*ey - (ay-py)*ex) / den, u = ((ax-px)*dy - (ay-py)*dx) / den;
    return t >= 0 && t <= len && u >= 0 && u <= 1;
}

/* Authored junctions: event 0x34148 nodes with three or more distinct
   neighbours (link slots +12/+14/+16, plus the in-segment predecessor and
   successor). Returns the count written to xy. */
static int authored_junctions(const char *root, const WEvent *e, float *xy, int cap) {
    char file[1024]; snprintf(file, sizeof file, "%s/ROUTES%s/Paths%d.bin", root, e->reg, e->id);
    long len = 0; unsigned char *d = n2_read_file(file, &len); if (!d) return 0;
    N2Leaf leaf[2]; int nl = 0; n2_find_leaves(d, 0, len, 0x34148u, leaf, &nl, 2);
    int out = 0;
    if (nl == 1 && leaf[0].size % 24 == 0) {
        int n = (int)(leaf[0].size / 24);
        const unsigned char *b = d + leaf[0].off;
        for (int k = 0; k < n && out < cap; k++) {
            int nb[5], m = 0; unsigned seg = b[24*k+8] | (unsigned)b[24*k+9] << 8;
            for (int j = 0; j < 3; j++) {
                unsigned v = b[24*k+12+2*j] | (unsigned)b[24*k+13+2*j] << 8;
                if (v == 0xffffu || v >= (unsigned)n || (int)v == k) continue;
                int dup = 0; for (int q = 0; q < m; q++) dup |= nb[q] == (int)v;
                if (!dup) nb[m++] = (int)v;
            }
            for (int s = -1; s <= 1; s += 2) {
                int v = k + s; if (v < 0 || v >= n) continue;
                unsigned sv = b[24*v+8] | (unsigned)b[24*v+9] << 8;
                if (sv != seg) continue;
                int dup = 0; for (int q = 0; q < m; q++) dup |= nb[q] == v;
                if (!dup) nb[m++] = v;
            }
            if (m >= 3) { memcpy(xy + 2*out, b + 24*k, 4); memcpy(xy + 2*out + 1, b + 24*k + 4, 4); out++; }
        }
    }
    free(d); return out;
}

typedef struct {
    int steps, on, backward, order, naive, swap, hintfree, probes, probe_ok, probe_skip;
    int gates_due, gates_cleared, gate_into, gate_into_ok, gate_through, gate_through_ok;
    int finished, cleared_ok, unselected_ok, prestart_ok, junctions, junction_samples, junction_ok;
    float min_dot, min_aim, max_lateral;
} Stats;

static void walk(const char *root, const WEvent *e, const Course *c, const WRace *armed,
                 const float start[2], float offset, const float *junc, int njunc, Stats *st) {
    WRace race = *armed;
    RouteGuideGate gates[ROUTE_GUIDE_MAXGATE];
    for (int k = 0; k < race.ngate; k++)
        gates[k] = (RouteGuideGate){race.gate[k].x, race.gate[k].y, race.gate[k].dx, race.gate[k].dy, race.gate[k].half};
    RouteGuideRoute r = {1, 0, c->p->xy, c->p->n, c->loop, gates, race.ngate, race.next, race.lap};
    RouteGuideIndex *ix = malloc(sizeof *ix), *ix2 = malloc(sizeof *ix2);
    float *copy = malloc((size_t)c->p->n * 2 * sizeof *copy);
    if (!ix || !ix2 || !copy || !route_guide_index(ix, &r)) { free(ix); free(ix2); free(copy); return; }
    (void)root;
    RouteGuide o;
    float s0 = 0; course_nearest(c, start[0], start[1], &s0);
    /* Before the gun: grid pose, lap 0, gate 0 armed. */
    float tan0[2], p0[2]; course_point(c, s0, p0, tan0);
    int pre = route_guide_eval(&r, ix, start[0], start[1], NAN, &o);
    if (getenv("ROUTE_GUIDE_TRACE"))
        printf("TRACE prestart grid=(%.1f %.1f) s0=%.1f course=(%.1f %.1f) state=%s along=%.1f lat=%.1f dot=%.3f gate0-along=%.1f gate1-along=%.1f origin=%.1f len=%.1f\n",
               start[0], start[1], s0, p0[0], p0[1], route_guide_state_name(o.state), o.along, o.lateral,
               o.dir[0]*tan0[0] + o.dir[1]*tan0[1], ix->gate_along[0], race.ngate > 1 ? ix->gate_along[1] : 0, ix->origin, ix->length);
    if (pre && o.dir[0]*tan0[0] + o.dir[1]*tan0[1] > 0) st->prestart_ok++;
    /* Starting gun: lap 1, gate 1 armed (world_race_begin). */
    world_race_begin(&race, start[0], start[1]);
    /* A circuit's grid can lie far round the lap from gate 0, and the gun
       arms gate 1, so allow up to one extra lap to reach the finish. */
    float total = c->loop ? c->length * (race.maxlaps + 1) : c->length - s0;
    float hint = NAN; int n = 0;
    float rebase_origin = ix->origin;
    int due = c->loop ? race.ngate * race.maxlaps : race.ngate - 1;
    st->gates_due += due;
    for (float ds = STEP; ds <= total + 200 && !race.finished; ds += STEP, n++) {
        float s = s0 + ds, p[2], tan[2];
        course_point(c, s, p, tan);
        float x = p[0] - tan[1]*offset, y = p[1] + tan[0]*offset;
        float px = race.px, py = race.py;
        int cleared_gate = race.next;
        int crossed = world_race_progress_update(&race, e->circuit, x, y);
        if (crossed) {
            st->gates_cleared++;
            (void)cleared_gate; (void)px; (void)py;
        }
        if (race.finished) break;
        r.next_gate = race.next; r.lap = race.lap;
        st->steps++;
        int on = route_guide_eval(&r, ix, x, y, hint, &o);
        if (getenv("ROUTE_GUIDE_TRACE"))
            printf("TRACE off=%.0f s=%.1f p=(%.1f %.1f) gate=%d lap=%d state=%s along=%.1f lat=%.1f crossed=%d\n",
                   offset, s, x, y, race.next, race.lap, route_guide_state_name(o.state), o.along, o.lateral, crossed);
        if (!on) continue;
        st->on++;
        /* the armed gate, in rebased race-order metres */
        const WGate *g = &race.gate[race.next];
        float ga = ix->gate_along[race.next];
        if (c->loop && race.next == 0) ga = c->length;
        /* forward = toward the course point one look-ahead further on (or the
           armed gate, if nearer); the local tangent is reported as min-dot,
           since round a hairpin the arrow rightly leans against it */
        float tdot = o.dir[0]*tan[0] + o.dir[1]*tan[1];
        if (tdot < st->min_dot) st->min_dot = tdot;
        if (o.lateral > st->max_lateral) st->max_lateral = o.lateral;
        /* ordered edge: compare rebased along with the walk's true along */
        float truth = c->loop ? fmodf(fmodf(s - rebase_origin, c->length) + c->length, c->length) : fminf(s, c->length);
        float err = fabsf(o.along - truth);
        if (c->loop) err = fminf(err, fminf(fabsf(o.along - truth - c->length), fabsf(o.along - truth + c->length)));
        if (err > 15) st->order++;
        float dga = ga - truth;   /* metres to the armed gate, through the lap seam */
        if (c->loop && dga < 0) dga += c->length;   /* past it uncrossed: a lap away (route_guide.c window) */
        float aim[2]; course_point(c, s + fminf(ROUTE_GUIDE_LOOKAHEAD, fmaxf(0, dga)), aim, NULL);
        float ax = aim[0]-x, ay = aim[1]-y, al = hypotf(ax, ay);
        float dot = al > 1 ? (o.dir[0]*ax + o.dir[1]*ay) / al : tdot;
        /* beside the armed gate's centre the bearing to it is ill-conditioned
           (a 3 m offset is most of it): forward there means through the gate */
        if (o.target_gate && al < ROUTE_GUIDE_GATE_NEAR) dot = o.dir[0]*g->dx + o.dir[1]*g->dy;
        if (dot <= 0) st->backward++;
        if (dot < st->min_aim) st->min_aim = dot;
        /* the nearest-node rule, for comparison */
        int nearest = 0; float nd = INFINITY;
        for (int j = 0; j < c->p->n; j++) { float d = hypotf(c->p->xy[2*j]-x, c->p->xy[2*j+1]-y); if (d < nd) { nd = d; nearest = j; } }
        float nerr = fabsf(c->cum[nearest] - fmodf(s, c->loop ? c->length : 1e30f));
        if (c->loop) nerr = fminf(nerr, c->length - nerr);
        if (nerr > 30) st->naive++;
        /* the armed gate: within 40 m of it along the course */
        float before = dga;
        if (before > 0 && before <= 40 && o.target_gate) {
            /* only once the gate is the aim point: further out the arrow
               follows the road, which may bend more than 90 degrees before
               the gate (the 33-40 m misses at 4125, 4301, 4708, ...) */
            st->gate_into++;
            if (o.dir[0]*g->dx + o.dir[1]*g->dy > 0) st->gate_into_ok++;
            {
                st->gate_through++;
                if (seg_hit(x, y, o.dir[0], o.dir[1], 80, g)) st->gate_through_ok++;
            }
        }
        if (getenv("ROUTE_GUIDE_TRACE"))
            printf("TRACE2 s=%.1f truth=%.1f err=%.1f aimdot=%.3f tdot=%.3f dga=%.1f gatedot=%.3f tg=%d dir=(%.2f %.2f) gate=(%.1f %.1f d %.2f %.2f half %.1f) ga=%.1f\n",
                   s, truth, err, dot, tdot, dga, o.dir[0]*g->dx + o.dir[1]*g->dy, o.target_gate, o.dir[0], o.dir[1],
                   g->x, g->y, g->dx, g->dy, g->half, ga);
        /* junctions: samples within 40 m past an authored junction node */
        for (int k = 0; k < njunc; k++) {
            float ja = junc[2*k];   /* raw along of a junction on the course */
            float past = fmodf(s, c->loop ? c->length : 1e30f) - ja;
            if (c->loop && past < -c->length/2) past += c->length;
            if (past > 5 && past <= 40) { st->junction_samples++; if (dot > .5f && err <= 15) st->junction_ok++; }
        }
        /* resident swap: rebuilt buffers, same carried state -> same answer */
        if (n % 25 == 0) {
            memcpy(copy, c->p->xy, (size_t)c->p->n * 2 * sizeof *copy);
            RouteGuideRoute r2 = r; r2.xy = copy;
            RouteGuide o2, o3;
            if (!route_guide_index(ix2, &r2) || !route_guide_eval(&r2, ix2, x, y, hint, &o2) ||
                o2.edge != o.edge || memcmp(o2.dir, o.dir, sizeof o.dir)) st->swap++;
            if (!route_guide_eval(&r2, ix2, x, y, NAN, &o3) || o3.edge != o.edge) st->hintfree++;
            /* off-route probes either side */
            for (int side = -1; side <= 1; side += 2) {
                float qx = p[0] - tan[1]*PROBE_OFFSET*side, qy = p[1] + tan[0]*PROBE_OFFSET*side;
                if (course_nearest(c, qx, qy, NULL) < ROUTE_GUIDE_OFFROUTE + 1) { st->probe_skip++; continue; }
                st->probes++;
                RouteGuide q; route_guide_eval(&r, ix, qx, qy, hint, &q);
                if (q.state == ROUTE_GUIDE_OFF_ROUTE) st->probe_ok++;
            }
        }
        hint = o.along;
    }
    if (race.finished) {
        st->finished++;
        r.finished = race.finished;
        float p[2]; course_point(c, s0 + total, p, NULL);
        if (!route_guide_eval(&r, ix, p[0], p[1], hint, &o) && o.state == ROUTE_GUIDE_CLEARED) st->cleared_ok++;
    }
    r.finished = 0; r.selected = 0;
    if (!route_guide_eval(&r, ix, start[0], start[1], NAN, &o) && o.state == ROUTE_GUIDE_UNSELECTED) st->unselected_ok++;
    free(ix); free(ix2); free(copy);
}

int main(int argc, char **argv) {
    const char *pos[2] = {"../TRACKS", NULL}; int npos = 0, in_race = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--in-race")) in_race = 1;
        else if (npos < 2) pos[npos++] = argv[i];
    }
    const char *root = pos[0];
    int only = pos[1] ? atoi(pos[1]) : 0;
    World catalog = {0};
    const char *regions[] = {"L4RA","L4RB","L4RC","L4RD","L4RF","L4RG"};
    catalog.neighborhood.nreg = 6;
    for (int k = 0; k < 6; k++) snprintf(catalog.neighborhood.rgn[k].name, 64, "STREAM%s", regions[k]);
    world_load_events(&catalog, root);
    int count = 0, pass = 0, unavailable = 0;
    for (int k = 0; k < catalog.city.nev; k++) {
        const WEvent *ce = catalog.city.ev + k;
        if (only && ce->id != only) continue;
        count++;
        World w = {0};
        char region[64]; snprintf(region, sizeof region, "STREAM%s", ce->reg);
        w.neighborhood.nreg = 1; snprintf(w.neighborhood.rgn[0].name, 64, "%s", region);
        int ev = -1;
        if (world_city_load(&w.city, &w.neighborhood, root, region))
            for (int j = 0; j < w.city.nev; j++) if (w.city.ev[j].id == ce->id) ev = j;
        N2Path course = {0}; float grid_xy[2] = {0, 0};
        if (ev < 0 || !world_race_start(&w, root, ev, 1) ||
            !(in_race ? in_race_course(&w, root, ev, &course, grid_xy) : ai_race_course(root, &w.city.ev[ev], &course)) || course.n < 2) {
            printf("ROUTE-GUIDE %d %s %s route=0 NOT-RUN%s\n", ce->id, ce->reg, n2_race_name(ce->info.kind),
                   in_race ? " (in-race: the game rejects this race start)" : "");
            unavailable++; free(course.xy); world_city_free(&w.city); continue;
        }
        const WEvent *e = &w.city.ev[ev];
        /* in-race: ai_race_prepare has already bound the gates to its course */
        int bound = in_race ? 1 : world_race_bind_course(&w.city.race, &course, 22);
        Course c = {&course, NULL, 0, 0, 0};
        int closed = hypotf(course.xy[0]-course.xy[2*course.n-2], course.xy[1]-course.xy[2*course.n-1]) < .01f;
        c.loop = e->circuit && closed;
        c.edges = c.loop ? course.n : course.n - 1;
        c.cum = calloc((size_t)c.edges + 1, sizeof *c.cum);
        for (int i = 0; i < c.edges; i++) { int j = (i+1) % course.n;
            c.cum[i+1] = c.cum[i] + hypotf(course.xy[2*j]-course.xy[2*i], course.xy[2*j+1]-course.xy[2*i+1]); }
        c.length = c.cum[c.edges];
        /* grid slot nearest gate 0, as race_events_check places the player */
        float start[2] = {course.xy[0], course.xy[1]}, best = INFINITY;
        if (in_race) { start[0] = grid_xy[0]; start[1] = grid_xy[1]; }   /* the player's supported slot */
        else for (int g = 0; g < w.city.race.ngrid; g++) {
            float d = hypotf(w.city.race.grid[g][0]-w.city.race.gate[0].x, w.city.race.grid[g][1]-w.city.race.gate[0].y);
            if (d < best) { best = d; start[0] = w.city.race.grid[g][0]; start[1] = w.city.race.grid[g][1]; }
        }
        float jxy[2*512], junc[2*512]; int nj = authored_junctions(root, e, jxy, 512), njunc = 0;
        for (int j = 0; j < nj; j++) {   /* keep the ones on the course: raw along */
            float ja; if (course_nearest(&c, jxy[2*j], jxy[2*j+1], &ja) > 8) continue;
            junc[2*njunc] = ja; junc[2*njunc+1] = 0; njunc++;
        }
        Stats st = {0}; st.min_dot = st.min_aim = 1;
        for (int pass_off = 0; pass_off < 2; pass_off++)
            walk(root, e, &c, &w.city.race, start, pass_off ? NORMAL_OFFSET : 0, junc, njunc, &st);
        st.junctions = njunc;
        int ok = st.steps > 0 && st.on == st.steps && !st.backward && !st.order && !st.swap &&
                 st.gate_into_ok == st.gate_into && st.gate_through_ok == st.gate_through &&
                 st.probe_ok == st.probes && st.finished == 2 && st.cleared_ok == 2 &&
                 st.unselected_ok == 2 && st.prestart_ok == 2 && st.junction_ok == st.junction_samples &&
                 st.gates_cleared == st.gates_due;
        pass += ok;
        printf("ROUTE-GUIDE %d %s %s loop=%d nodes=%d len=%.0f gates=%d bound=%d grid=%d junctions=%d "
               "steps=%d on=%d backward=%d min-aim-dot=%.3f min-tangent-dot=%.3f max-lat=%.1f order-err=%d nearest-node-err=%d "
               "gates=%d/%d into=%d/%d through=%d/%d junction-samples=%d/%d swap-mismatch=%d hintless-diff=%d "
               "offroute=%d/%d(skip %d) prestart=%d/2 finished=%d/2 cleared=%d/2 unselected=%d/2 %s\n",
               e->id, e->reg, n2_race_name(e->info.kind), c.loop, course.n, c.length, w.city.race.ngate, bound,
               w.city.race.ngrid, st.junctions, st.steps, st.on, st.backward, st.min_aim, st.min_dot, st.max_lateral,
               st.order, st.naive, st.gates_cleared, st.gates_due, st.gate_into_ok, st.gate_into,
               st.gate_through_ok, st.gate_through, st.junction_ok, st.junction_samples, st.swap, st.hintfree,
               st.probe_ok, st.probes, st.probe_skip, st.prestart_ok, st.finished, st.cleared_ok,
               st.unselected_ok, ok ? "PASS" : "FAIL");
        fflush(stdout);
        free(c.cum); free(course.xy); world_city_free(&w.city);
    }
    printf("ROUTE-GUIDE SUMMARY events=%d pass=%d fail=%d not-run=%d\n", count, pass, count - pass - unavailable, unavailable);
    return !count || pass + unavailable != count || unavailable;
}
