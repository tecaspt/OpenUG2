/* Local asset check: does the minimap route agree with the authored route?
 *
 *   route_minimap_check DATAROOT [event-id [OUTDIR]]
 *
 * Prints numbers only; no game file is a fixture and nothing is written.
 * For every shipped event (0x3414c catalog) it compares three things:
 *
 *  1. OVERLAY vs AUTHORED: the polyline the race HUD draws over the map
 *     (main.c passes the event course, ai_race_course, decoded here without
 *     loaded geometry) against the event's own 0x34148 route nodes and their
 *     in-segment and linked edges. Every course point, sampled each 5 m, must
 *     lie within OVERLAY_TOL m of an authored edge. Coverage of the authored
 *     nodes by the course is reported (branches the course does not take are
 *     expected to stay uncovered).
 *  2. OVERLAY CAPACITY: the HUD draws at most HA_LIMIT points and silently
 *     skips a longer course (hud.c map_overlay).
 *  3. RETAIL MAP: TRACKS/TRACKMAP<id>.BIN, placed with the GLOBALB TrackInfo
 *     origin/extent exactly as hud.c does (u = (x-ox)/e, v = (oy-y)/e), must
 *     show its bright route (luminance >= MB_RACE_LOW, the value map_bake.h
 *     uses for "the route begins") within OVERLAY_TOL m of every course
 *     sample and of every authored node the course covers.
 * With OUTDIR (one event only) the retail map is written as OUTDIR/<id>.ppm
 * with the authored nodes (yellow) and the course (cyan) drawn over it, for
 * local inspection. Those files derive from retail data: never commit them. */
#include "ai.h"
#include "world.h"
#include "hud_asset.h"
#include "hud_texture.h"
#include "map_bake.h"

#define OVERLAY_TOL 12.0f   /* metres: the AI route's own cross-track limit */
#define SAMPLE 5.0f

typedef struct { float *xy; int n; int *edge; int ne; } Authored;

static int authored_read(const char *root, const WEvent *e, Authored *a) {
    memset(a, 0, sizeof *a);
    char file[1024]; snprintf(file, sizeof file, "%s/TRACKS/ROUTES%s/Paths%d.bin", root, e->reg, e->id);
    long len = 0; unsigned char *d = n2_read_file(file, &len); if (!d) return 0;
    N2Leaf leaf[2]; int nl = 0; n2_find_leaves(d, 0, len, 0x34148u, leaf, &nl, 2);
    if (nl != 1 || leaf[0].size % 24) { free(d); return 0; }
    int n = (int)(leaf[0].size / 24);
    const unsigned char *b = d + leaf[0].off;
    a->xy = malloc((size_t)n * 2 * sizeof *a->xy); a->edge = malloc((size_t)n * 8 * sizeof *a->edge);
    if (!a->xy || !a->edge) { free(d); return 0; }
    a->n = n;
    for (int k = 0; k < n; k++) { memcpy(a->xy + 2*k, b + 24*k, 4); memcpy(a->xy + 2*k + 1, b + 24*k + 4, 4); }
    for (int k = 0; k < n; k++) {
        unsigned seg = b[24*k+8] | (unsigned)b[24*k+9] << 8;
        int cand[4], m = 0;
        if (k + 1 < n && (unsigned)(b[24*(k+1)+8] | (unsigned)b[24*(k+1)+9] << 8) == seg) cand[m++] = k + 1;
        for (int j = 0; j < 3; j++) {
            unsigned v = b[24*k+12+2*j] | (unsigned)b[24*k+13+2*j] << 8;
            if (v != 0xffffu && v < (unsigned)n && (int)v != k) cand[m++] = (int)v;
        }
        for (int q = 0; q < m; q++) {
            int o = cand[q];
            if (hypotf(a->xy[2*o]-a->xy[2*k], a->xy[2*o+1]-a->xy[2*k+1]) > 120) continue;
            a->edge[2*a->ne] = k; a->edge[2*a->ne+1] = o; a->ne++;
        }
    }
    free(d); return 1;
}

static float authored_dist(const Authored *a, float x, float y) {
    float best = INFINITY;
    for (int k = 0; k < a->n; k++) { float d = hypotf(a->xy[2*k]-x, a->xy[2*k+1]-y); if (d < best) best = d; }
    for (int i = 0; i < a->ne; i++) {
        const float *p = a->xy + 2*a->edge[2*i], *q = a->xy + 2*a->edge[2*i+1];
        float dx = q[0]-p[0], dy = q[1]-p[1], l2 = dx*dx + dy*dy;
        float t = l2 > 1e-8f ? ((x-p[0])*dx + (y-p[1])*dy) / l2 : 0; t = fmaxf(0, fminf(1, t));
        float d = hypotf(p[0] + t*dx - x, p[1] + t*dy - y); if (d < best) best = d;
    }
    return best;
}

static float course_dist(const N2Path *c, float x, float y) {
    float best = INFINITY;
    for (int i = 0; i + 1 < c->n; i++) {
        const float *p = c->xy + 2*i, *q = p + 2;
        float dx = q[0]-p[0], dy = q[1]-p[1], l2 = dx*dx + dy*dy;
        float t = l2 > 1e-8f ? ((x-p[0])*dx + (y-p[1])*dy) / l2 : 0; t = fmaxf(0, fminf(1, t));
        float d = hypotf(p[0] + t*dx - x, p[1] + t*dy - y); if (d < best) best = d;
    }
    return best;
}

/* hud.c track_map_origin: TrackInfo rows (0x34201, 296 bytes) in GLOBALB. */
static int map_origin(const unsigned char *g, long len, int event, double o[3]) {
    N2Leaf leaves[8]; int count = 0; n2_find_leaves(g, 0, len, 0x34201u, leaves, &count, 8);
    for (int i = 0; i < count; i++) {
        if (leaves[i].size % 296) continue;
        const unsigned char *d = g + leaves[i].off;
        for (size_t at = 0; at < leaves[i].size; at += 296) {
            if (n2_u32(d + at + 140) != (uint32_t)event) continue;
            for (int k = 0; k < 3; k++) { uint32_t bits = n2_u32(d + at + 172 + 4*k); float v; memcpy(&v, &bits, 4); o[k] = v; }
            return isfinite(o[0]) && isfinite(o[1]) && isfinite(o[2]) && o[2] > 0;
        }
    }
    return 0;
}

typedef struct { const HaPixels *px; double o[3]; int radius; } RetailMap;

/* 1 if a bright route texel lies within the tolerance disc around (x,y);
   -1 if (x,y) falls outside the map image. */
static int retail_lit(const RetailMap *m, float x, float y, int *peak_out) {
    double u = (x - m->o[0]) / m->o[2] * m->px->width, v = (m->o[1] - y) / m->o[2] * m->px->height;
    int cx = (int)floor(u), cy = (int)floor(v);
    if (cx < 0 || cy < 0 || cx >= (int)m->px->width || cy >= (int)m->px->height) return -1;
    int peak = 0;
    for (int dy = -m->radius; dy <= m->radius; dy++) for (int dx = -m->radius; dx <= m->radius; dx++) {
        if (dx*dx + dy*dy > m->radius*m->radius) continue;
        int xx = cx + dx, yy = cy + dy;
        if (xx < 0 || yy < 0 || xx >= (int)m->px->width || yy >= (int)m->px->height) continue;
        const unsigned char *p = m->px->rgba + 4*((size_t)yy*m->px->width + xx);
        int l = (p[0]*77 + p[1]*150 + p[2]*29) >> 8;
        if (l > peak) peak = l;
    }
    if (peak_out) *peak_out = peak;
    return peak >= MB_RACE_LOW;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    int only = argc > 2 ? atoi(argv[2]) : 0;
    char troot[1024]; snprintf(troot, sizeof troot, "%s/TRACKS", root);
    char gpath[1024]; snprintf(gpath, sizeof gpath, "%s/GLOBAL/GLOBALB.BUN", root);
    long glen = 0; unsigned char *global = n2_read_file(gpath, &glen);
    World catalog = {0};
    const char *regions[] = {"L4RA","L4RB","L4RC","L4RD","L4RF","L4RG"};
    catalog.neighborhood.nreg = 6;
    for (int k = 0; k < 6; k++) snprintf(catalog.neighborhood.rgn[k].name, 64, "STREAM%s", regions[k]);
    world_load_events(&catalog, troot);
    int count = 0, pass = 0, notrun = 0, maps = 0;
    for (int k = 0; k < catalog.city.nev; k++) {
        const WEvent *e = catalog.city.ev + k;
        if (only && e->id != only) continue;
        count++;
        Authored a; N2Path course = {0};
        if (!authored_read(root, e, &a) || !ai_race_course(troot, e, &course) || course.n < 2) {
            printf("MINIMAP %d %s %s NOT-RUN (route unavailable)\n", e->id, e->reg, n2_race_name(e->info.kind));
            notrun++; free(a.xy); free(a.edge); free(course.xy); continue;
        }
        /* 1. overlay vs authored */
        int samples = 0, outside = 0; float worst = 0, sum = 0;
        float *dev = NULL; int cap = 0;
        for (int i = 0; i + 1 < course.n; i++) {
            const float *p = course.xy + 2*i, *q = p + 2;
            float l = hypotf(q[0]-p[0], q[1]-p[1]);
            int steps = (int)ceilf(l / SAMPLE); if (steps < 1) steps = 1;
            for (int s = 0; s < steps; s++) {
                float t = (float)s / steps, x = p[0] + t*(q[0]-p[0]), y = p[1] + t*(q[1]-p[1]);
                float d = authored_dist(&a, x, y);
                if (samples == cap) { cap = cap ? cap*2 : 1024; dev = realloc(dev, (size_t)cap * sizeof *dev); }
                dev[samples++] = d; sum += d;
                if (d > worst) worst = d;
                if (d > OVERLAY_TOL) outside++;
            }
        }
        /* p95 by partial selection sort is fine at these sizes */
        for (int i = 0; i < samples; i++) for (int j = i+1; j < samples; j++) if (dev[j] < dev[i]) { float t = dev[i]; dev[i] = dev[j]; dev[j] = t; }
        float p95 = samples ? dev[(int)(0.95f * (samples - 1))] : 0;
        free(dev);
        int covered = 0;
        for (int n = 0; n < a.n; n++) covered += course_dist(&course, a.xy[2*n], a.xy[2*n+1]) <= OVERLAY_TOL;
        int capacity_ok = course.n <= HA_LIMIT;
        /* 3. retail TRACKMAP */
        char mname[32], mpath[1024]; snprintf(mname, sizeof mname, "TRACKMAP%d", e->id);
        snprintf(mpath, sizeof mpath, "%s/TRACKS/%s.BIN", root, mname);
        long mlen = 0; unsigned char *mdata = n2_read_file(mpath, &mlen);
        HaPixels px = {0}; RetailMap m = {&px, {0,0,0}, 1};
        int have_map = mdata && global && map_origin(global, glen, e->id, m.o) &&
                       ha_texture_find(mdata, (size_t)mlen, ha_hash(mname), &px) == 1;
        int lit = 0, lit_total = 0, off_image = 0, node_lit = 0, node_total = 0;
        int out_lit = 0, out_total = 0;
        float mpt = 0;
        if (have_map) {
            maps++;
            mpt = (float)(m.o[2] / px.width);
            m.radius = (int)ceilf(OVERLAY_TOL / mpt); if (m.radius < 1) m.radius = 1;
            for (int i = 0; i + 1 < course.n; i++) {
                const float *p = course.xy + 2*i, *q = p + 2;
                float l = hypotf(q[0]-p[0], q[1]-p[1]);
                int steps = (int)ceilf(l / SAMPLE); if (steps < 1) steps = 1;
                for (int s = 0; s < steps; s++) {
                    float t = (float)s / steps, x = p[0] + t*(q[0]-p[0]), y = p[1] + t*(q[1]-p[1]);
                    int r = retail_lit(&m, x, y, NULL);
                    if (r < 0) { off_image++; continue; }
                    lit_total++; lit += r;
                    /* does the retail image side with the course where it leaves the authored nodes? */
                    if (authored_dist(&a, x, y) > OVERLAY_TOL) { out_total++; out_lit += r; }
                }
            }
            for (int n = 0; n < a.n; n++) {
                if (course_dist(&course, a.xy[2*n], a.xy[2*n+1]) > OVERLAY_TOL) continue;
                int r = retail_lit(&m, a.xy[2*n], a.xy[2*n+1], NULL);
                if (r < 0) continue;
                node_total++; node_lit += r;
            }
        }
        if (have_map && only && argc > 3) {
            char out[1024]; snprintf(out, sizeof out, "%s/%d.ppm", argv[3], e->id);
            unsigned char *img = malloc((size_t)px.width * px.height * 3);
            for (size_t i = 0; img && i < (size_t)px.width * px.height; i++) memcpy(img + 3*i, px.rgba + 4*i, 3);
            #define PLOT(X, Y, R, G, B) do { double u_ = ((X) - m.o[0]) / m.o[2] * px.width, v_ = (m.o[1] - (Y)) / m.o[2] * px.height; \
                int x_ = (int)u_, y_ = (int)v_; if (img && x_ >= 0 && y_ >= 0 && x_ < (int)px.width && y_ < (int)px.height) { \
                unsigned char *q_ = img + 3*((size_t)y_*px.width + x_); q_[0] = R; q_[1] = G; q_[2] = B; } } while (0)
            for (int i = 0; i + 1 < course.n; i++) for (int t = 0; t < 8; t++) {
                const float *p = course.xy + 2*i; PLOT(p[0] + (p[2]-p[0])*t/8, p[1] + (p[3]-p[1])*t/8, 0, 255, 255); }
            for (int n = 0; n < a.n; n++) PLOT(a.xy[2*n], a.xy[2*n+1], 255, 255, 0);
            #undef PLOT
            FILE *f = img ? fopen(out, "wb") : NULL;
            if (f) { fprintf(f, "P6\n%u %u\n255\n", px.width, px.height); fwrite(img, 3, (size_t)px.width * px.height, f); fclose(f); }
            free(img);
        }
        int overlay_ok = outside == 0;
        int retail_ok = !have_map || (off_image == 0 && lit == lit_total && node_lit == node_total);
        int ok = overlay_ok && capacity_ok && retail_ok;
        pass += ok;
        printf("MINIMAP %d %s %s course=%d authored=%d samples=%d max-dev=%.1f p95-dev=%.1f mean-dev=%.1f "
               "outside=%d node-coverage=%d/%d capacity=%s retail-map=%s m/texel=%.2f course-lit=%d/%d "
               "off-image=%d nodes-lit=%d/%d outside-lit=%d/%d %s\n",
               e->id, e->reg, n2_race_name(e->info.kind), course.n, a.n, samples, worst, p95,
               samples ? sum / samples : 0, outside, covered, a.n, capacity_ok ? "ok" : "OVER",
               have_map ? "yes" : "none", mpt, lit, lit_total, off_image, node_lit, node_total, out_lit, out_total,
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        free(px.rgba); free(mdata); free(a.xy); free(a.edge); free(course.xy);
    }
    printf("MINIMAP SUMMARY events=%d pass=%d fail=%d not-run=%d retail-maps=%d\n",
           count, pass, count - pass - notrun, notrun, maps);
    free(global);
    return !count || pass != count;
}
