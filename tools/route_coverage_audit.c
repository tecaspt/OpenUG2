/* Route ground-coverage audit (roadmap item 5, "World coverage").
 *
 * GL-free and read-only. For every shipped event racing line
 * (TRACKS/ROUTES<REG>/Paths*.bin leaf 0x34148, segmented by 0x34149) this
 * answers one measured question: does the ground scene that free roam actually
 * builds from STREAM<REG>.BUN have ROAD/TERRAIN under each route segment?
 *
 * The ground scene is rebuilt with the production instance-world rules from
 * src/world_instance.c, restricted to ground meshes:
 *   - winst_collect_models / ownership pre-pass / winst_place_ground_prototypes
 *     (the "ground chunk" pass, which is not radius limited at runtime), and
 *   - instanced ROAD/TERRAIN prototypes placed by their own section records,
 *     with the free-roam scenery selection (-1) applied per placement.
 * Instanced ground is walked over the whole bundle. A covering placement is
 * within metres of its route point, so the 1400 m resident radius around a
 * 400 m cell always contains it; route points never need the oversized radius.
 *
 * Every route point is sampled along consecutive nodes of one 0x34149
 * segment (edges longer than the 120 m production nav limit are skipped).
 * A point is covered when it lies inside the XY projection of a triangle.
 * For every run of points with no ROAD, the audit then attributes what the
 * bundle does carry there, in the guide's classes:
 *   vista-only      ground-category mesh routed to World.vista (PAN_/LOD sheet)
 *   group-filtered  instanced ground hidden by the free-roam scenery selection
 *   other-category  a placed non-ground mesh (name not classified ROAD/TERRAIN)
 *   unplaced-proto  a non-ground prototype no placement references
 *   other-bundle    ROAD in another STREAM bundle (wrong bundle/section)
 *   terrain-only    TERRAIN support but no ROAD mesh
 *   none            nothing above
 * It never changes scene assembly; a clean result is not a claim that the
 * renderer draws every covered triangle (draw/ordinary-tier culling is
 * checked separately with same-pose captures).
 *
 * usage: route_coverage_audit TRACKS_DIR [--region R]... [--step M]
 *            [--min-gap M] [--no-cross] [--quiet]
 * Exit status: 0 = audit ran (gaps are findings, not failures);
 *              2 = usage/data error.
 */
#include "../src/world_instance.c"

#include <dirent.h>

#define NAV_LINK_MAX 120.0f   /* production world_load_nav edge limit */
#define CELL 16.0f            /* XY triangle grid cell, metres */
#define RESIDENT_CELL 400.0f  /* production WResidentPolicy.cell_size */

enum { S_ROAD, S_TERR, S_VISTA, S_HIDDEN, S_OTHER, S_UNPLACED, S_COUNT };
static const char *set_name[S_COUNT] = {
    "road", "terrain", "vista-ground", "group-hidden-ground",
    "other-category", "unplaced-proto"
};

typedef struct {
    float x[3], y[3], z[3];
    int src;
} Tri;

typedef struct {
    char name[32];
    uint32_t model_key;
    uint32_t placement_id; /* section<<16|row, 0xffffffff for a ground chunk */
} Src;

typedef struct {
    int nmesh;
    Tri *tri; int ntri, captri;
    Src *src; int nsrc, capsrc;
    float bb[4];
    int gw, gh;
    int *cell_start, *cell_list;
} TriSet;

static void set_free(TriSet *s) {
    free(s->tri); free(s->src); free(s->cell_start); free(s->cell_list);
    memset(s, 0, sizeof *s);
}

static int set_add_src(TriSet *s, const char *name, uint32_t key, uint32_t pid) {
    if (s->nsrc == s->capsrc) {
        s->capsrc = s->capsrc ? s->capsrc * 2 : 256;
        s->src = realloc(s->src, (size_t)s->capsrc * sizeof *s->src);
        if (!s->src) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    Src *o = &s->src[s->nsrc];
    snprintf(o->name, sizeof o->name, "%.31s", name ? name : "");
    o->model_key = key; o->placement_id = pid;
    return s->nsrc++;
}

/* Append a mesh's triangles under `matrix` (NULL = already world space).
 * Optional clip box keeps only triangles touching it (memory bound for the
 * attribution pass). */
static void set_add_mesh(TriSet *s, const N2Mesh *m, const float *mx,
                         int src, const float *clip) {
    s->nmesh++;
    for (int t = 0; t + 2 < m->nidx; t += 3) {
        Tri tr; tr.src = src;
        int bad = 0;
        for (int k = 0; k < 3; k++) {
            int v = m->idx[t + k];
            if (v >= m->nverts) { bad = 1; break; }
            const float *p = m->verts + v * 5;
            float x = p[0], y = p[1], z = p[2];
            if (mx) {
                x = p[0]*mx[0] + p[1]*mx[4] + p[2]*mx[8]  + mx[12];
                y = p[0]*mx[1] + p[1]*mx[5] + p[2]*mx[9]  + mx[13];
                z = p[0]*mx[2] + p[1]*mx[6] + p[2]*mx[10] + mx[14];
            }
            if (!isfinite(x) || !isfinite(y) || fabsf(x) > 1e5f || fabsf(y) > 1e5f) { bad = 1; break; }
            tr.x[k] = x; tr.y[k] = y; tr.z[k] = z;
        }
        if (bad) continue;
        if (clip) {
            float x0 = fminf(tr.x[0], fminf(tr.x[1], tr.x[2]));
            float x1 = fmaxf(tr.x[0], fmaxf(tr.x[1], tr.x[2]));
            float y0 = fminf(tr.y[0], fminf(tr.y[1], tr.y[2]));
            float y1 = fmaxf(tr.y[0], fmaxf(tr.y[1], tr.y[2]));
            if (x1 < clip[0] || x0 > clip[2] || y1 < clip[1] || y0 > clip[3]) continue;
        }
        if (s->ntri == s->captri) {
            s->captri = s->captri ? s->captri * 2 : 4096;
            s->tri = realloc(s->tri, (size_t)s->captri * sizeof *s->tri);
            if (!s->tri) { fprintf(stderr, "out of memory\n"); exit(2); }
        }
        s->tri[s->ntri++] = tr;
    }
}

static void set_index(TriSet *s) {
    free(s->cell_start); free(s->cell_list);
    s->cell_start = s->cell_list = NULL; s->gw = s->gh = 0;
    if (!s->ntri) return;
    s->bb[0] = s->bb[1] = 1e30f; s->bb[2] = s->bb[3] = -1e30f;
    for (int i = 0; i < s->ntri; i++)
        for (int k = 0; k < 3; k++) {
            s->bb[0] = fminf(s->bb[0], s->tri[i].x[k]); s->bb[2] = fmaxf(s->bb[2], s->tri[i].x[k]);
            s->bb[1] = fminf(s->bb[1], s->tri[i].y[k]); s->bb[3] = fmaxf(s->bb[3], s->tri[i].y[k]);
        }
    s->gw = (int)((s->bb[2] - s->bb[0]) / CELL) + 1;
    s->gh = (int)((s->bb[3] - s->bb[1]) / CELL) + 1;
    if (s->gw <= 0 || s->gh <= 0 || s->gw > 20000 || s->gh > 20000) {
        fprintf(stderr, "triangle bounds out of range\n"); exit(2);
    }
    size_t nc = (size_t)s->gw * (size_t)s->gh;
    s->cell_start = calloc(nc + 1, sizeof *s->cell_start);
    if (!s->cell_start) { fprintf(stderr, "out of memory\n"); exit(2); }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s->ntri; i++) {
            const Tri *t = &s->tri[i];
            int cx0 = (int)((fminf(t->x[0], fminf(t->x[1], t->x[2])) - s->bb[0]) / CELL);
            int cx1 = (int)((fmaxf(t->x[0], fmaxf(t->x[1], t->x[2])) - s->bb[0]) / CELL);
            int cy0 = (int)((fminf(t->y[0], fminf(t->y[1], t->y[2])) - s->bb[1]) / CELL);
            int cy1 = (int)((fmaxf(t->y[0], fmaxf(t->y[1], t->y[2])) - s->bb[1]) / CELL);
            for (int cy = cy0; cy <= cy1; cy++)
                for (int cx = cx0; cx <= cx1; cx++) {
                    size_t c = (size_t)cy * s->gw + cx;
                    if (pass == 0) s->cell_start[c + 1]++;
                    else s->cell_list[s->cell_start[c]++] = i;
                }
        }
        if (pass == 0) {
            for (size_t c = 0; c < nc; c++) s->cell_start[c + 1] += s->cell_start[c];
            s->cell_list = malloc((size_t)(s->cell_start[nc] + 1) * sizeof *s->cell_list);
            if (!s->cell_list) { fprintf(stderr, "out of memory\n"); exit(2); }
        } else {
            for (size_t c = nc; c > 0; c--) s->cell_start[c] = s->cell_start[c - 1];
            s->cell_start[0] = 0;
        }
    }
}

static int tri_contains(const Tri *t, float x, float y, float *z) {
    float d = (t->y[1]-t->y[2])*(t->x[0]-t->x[2]) + (t->x[2]-t->x[1])*(t->y[0]-t->y[2]);
    if (fabsf(d) < 1e-6f) return 0;
    float a = ((t->y[1]-t->y[2])*(x-t->x[2]) + (t->x[2]-t->x[1])*(y-t->y[2])) / d;
    float b = ((t->y[2]-t->y[0])*(x-t->x[2]) + (t->x[0]-t->x[2])*(y-t->y[2])) / d;
    float c = 1.0f - a - b;
    const float e = -1e-4f;
    if (a < e || b < e || c < e) return 0;
    if (z) *z = a*t->z[0] + b*t->z[1] + c*t->z[2];
    return 1;
}

/* First covering triangle's source (or -1). */
static int set_hit(const TriSet *s, float x, float y, float *z) {
    if (!s->cell_start) return -1;
    if (x < s->bb[0] || y < s->bb[1] || x > s->bb[2] || y > s->bb[3]) return -1;
    int cx = (int)((x - s->bb[0]) / CELL), cy = (int)((y - s->bb[1]) / CELL);
    if (cx >= s->gw) cx = s->gw - 1;
    if (cy >= s->gh) cy = s->gh - 1;
    size_t c = (size_t)cy * s->gw + cx;
    for (int k = s->cell_start[c]; k < s->cell_start[c + 1]; k++) {
        const Tri *t = &s->tri[s->cell_list[k]];
        if (tri_contains(t, x, y, z)) return t->src;
    }
    return -1;
}

/* Covering triangle whose surface lies within [zref-below, zref+above]. */
static int set_hit_z(const TriSet *s, float x, float y, float zref,
                     float below, float above) {
    if (!s->cell_start) return -1;
    if (x < s->bb[0] || y < s->bb[1] || x > s->bb[2] || y > s->bb[3]) return -1;
    int cx = (int)((x - s->bb[0]) / CELL), cy = (int)((y - s->bb[1]) / CELL);
    if (cx >= s->gw) cx = s->gw - 1;
    if (cy >= s->gh) cy = s->gh - 1;
    size_t c = (size_t)cy * s->gw + cx;
    for (int k = s->cell_start[c]; k < s->cell_start[c + 1]; k++) {
        const Tri *t = &s->tri[s->cell_list[k]];
        float z;
        if (tri_contains(t, x, y, &z) && z >= zref - below && z <= zref + above)
            return t->src;
    }
    return -1;
}

/* Every covering source at one point, for --probe. */
static void set_probe(const TriSet *s, const char *label, float x, float y) {
    if (!s->cell_start) return;
    if (x < s->bb[0] || y < s->bb[1] || x > s->bb[2] || y > s->bb[3]) return;
    int cx = (int)((x - s->bb[0]) / CELL), cy = (int)((y - s->bb[1]) / CELL);
    if (cx >= s->gw) cx = s->gw - 1;
    if (cy >= s->gh) cy = s->gh - 1;
    size_t c = (size_t)cy * s->gw + cx;
    int last = -1;
    for (int k = s->cell_start[c]; k < s->cell_start[c + 1]; k++) {
        const Tri *t = &s->tri[s->cell_list[k]];
        float z;
        if (!tri_contains(t, x, y, &z) || t->src == last) continue;
        last = t->src;
        const Src *o = &s->src[t->src];
        if (o->placement_id == 0xffffffffu)
            printf("  PROBE %-20s z=%8.2f %-31s key=%08x chunk\n", label, z, o->name, o->model_key);
        else
            printf("  PROBE %-20s z=%8.2f %-31s key=%08x sec=%u row=%u\n", label, z, o->name,
                   o->model_key, o->placement_id >> 16, o->placement_id & 0xffffu);
    }
}

/* ---- production ground assembly (ground meshes only) ------------------ */

typedef struct {
    const WInstLibrary *lib;
    TriSet *sets;
    const WGSelection *free_sel;
    int attribution;        /* 0 = ground pass, 1 = non-ground near gaps */
    const float *clip;
} GroundVisit;

static int ground_visit(const WInstPlacement *pl, const char *type_name, void *ud) {
    GroundVisit *g = ud;
    const WInstProto *proto = winst_library_resolve(g->lib, pl, type_name, NULL);
    if (!proto) return 1;
    unsigned section = pl->placement_id >> 16, row = pl->placement_id & 0xffffu;
    int visible = wg_selection_visible(g->free_sel, section, row);
    for (int i = 0; i < proto->scene.count; i++) {
        const N2Mesh *m = &proto->scene.meshes[i];
        int ground = m->cat == N2_ROAD || m->cat == N2_TERRAIN;
        int set;
        if (!g->attribution) {
            /* winst_build_visit: chunk prototypes belong to the ground pass */
            if (!ground || !proto->instanced) continue;
            set = !visible ? S_HIDDEN : proto->is_vista ? S_VISTA
                : m->cat == N2_ROAD ? S_ROAD : S_TERR;
        } else {
            if (ground || !visible) continue;
            set = S_OTHER;
        }
        int src = set_add_src(&g->sets[set], type_name, proto->model_key, pl->placement_id);
        set_add_mesh(&g->sets[set], m, pl->matrix, src, g->clip);
    }
    return 1;
}

typedef struct {
    unsigned char *data; long len;
    WInstLibrary lib;
    WGSelection free_sel;
    WInstRegion *regions; int nregions;
} Bundle;

static void bundle_free(Bundle *b) {
    winst_library_free(&b->lib);
    free(b->free_sel.items);
    winst_free_regions(b->regions, b->nregions);
    free(b->data);
    memset(b, 0, sizeof *b);
}

static int bundle_open(Bundle *b, const char *troot, const char *reg) {
    memset(b, 0, sizeof *b);
    char name[64];
    snprintf(name, sizeof name, "STREAM%s.BUN", reg);
    b->data = winst_read_named(troot, name, &b->len);
    if (!b->data) return 0;
    long clen = 0;
    snprintf(name, sizeof name, "%s.BUN", reg);
    unsigned char *companion = winst_read_named(troot, name, &clen);
    if (companion) {
        winst_parse_regions(companion, clen, &b->regions, &b->nregions);
        WGTable table;
        if (wg_open_file(companion, (size_t)clen, &table) &&
            wg_selection_open(&table, -1, &b->free_sel)) {
            int valid = winst_check_scenery(b->data, 0, b->len, 0, &b->free_sel);
            for (size_t i = 0; i < b->free_sel.count; i++)
                if (!b->free_sel.items[i].checked) valid = 0;
            if (!valid) { free(b->free_sel.items); memset(&b->free_sel, 0, sizeof b->free_sel); }
        }
        free(companion);
    }
    uint32_t *keys = malloc(16384 * sizeof *keys);
    if (!keys) return 0;
    N2Tpk tpk = n2_tpk_open(b->data, b->len);
    int nkeys = n2_tpk_keys(b->data, tpk, keys, 16384);
    free(tpk.blk);
    winst_collect_models(&b->lib, b->data, 0, b->len, keys, nkeys);
    free(keys);
    WInstOwnership owner;
    if (!winst_ownership_open(&owner, &b->lib)) return 0;
    WInstWalk w; memset(&w, 0, sizeof w);
    w.find_region = -1; w.view_radius = WINST_WORLD_LIMIT;
    w.visit = winst_mark_instanced; w.userdata = &owner;
    int ok = winst_walk_sections(b->data, 0, b->len, &w);
    free(owner.ground_keys);
    return ok;
}

/* Instanced-ground placement filter: whole bundle by default, or the
 * production resident (focus, radius) for --resident cross-checks. */
static float g_focus[3] = { 0.0f, 0.0f, WINST_WORLD_LIMIT };

/* sets[S_ROAD/S_TERR/S_VISTA/S_HIDDEN] from one bundle. */
static int bundle_ground(Bundle *b, TriSet *sets) {
    static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int i = 0; i < b->lib.count; i++) {      /* winst_place_ground_prototypes */
        const WInstProto *p = &b->lib.items[i];
        if (p->instanced) continue;
        const float *mx = winst_proto_has_own_matrix(&b->lib, p) ? p->matrix : identity;
        for (int k = 0; k < p->scene.count; k++) {
            const N2Mesh *m = &p->scene.meshes[k];
            if (m->cat != N2_ROAD && m->cat != N2_TERRAIN) continue;
            int set = p->is_vista ? S_VISTA : m->cat == N2_ROAD ? S_ROAD : S_TERR;
            int src = set_add_src(&sets[set], p->name, p->model_key, 0xffffffffu);
            set_add_mesh(&sets[set], m, mx, src, NULL);
        }
    }
    GroundVisit g = { &b->lib, sets, &b->free_sel, 0, NULL };
    WInstWalk w; memset(&w, 0, sizeof w);
    w.find_region = -1;
    w.focus_x = g_focus[0]; w.focus_y = g_focus[1]; w.view_radius = g_focus[2];
    w.visit = ground_visit; w.userdata = &g;
    return winst_walk_sections(b->data, 0, b->len, &w);
}

/* sets[S_OTHER/S_UNPLACED] restricted to `clip`. */
static int bundle_attribution(Bundle *b, TriSet *sets, const float clip[4]) {
    static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    GroundVisit g = { &b->lib, sets, &b->free_sel, 1, clip };
    WInstWalk w; memset(&w, 0, sizeof w);
    w.find_region = -1; w.view_radius = WINST_WORLD_LIMIT;
    w.visit = ground_visit; w.userdata = &g;
    if (!winst_walk_sections(b->data, 0, b->len, &w)) return 0;
    for (int i = 0; i < b->lib.count; i++) {
        const WInstProto *p = &b->lib.items[i];
        if (p->instanced || winst_proto_is_ground(p)) continue;
        const float *mx = winst_proto_has_own_matrix(&b->lib, p) ? p->matrix : identity;
        int src = set_add_src(&sets[S_UNPLACED], p->name, p->model_key, 0xffffffffu);
        for (int k = 0; k < p->scene.count; k++)
            set_add_mesh(&sets[S_UNPLACED], &p->scene.meshes[k], mx, src, clip);
    }
    return 1;
}

static int home_district(const Bundle *b, float x, float y) {
    static unsigned char sel[65536]; int home = -1;
    if (!b->nregions) return -1;
    winst_select_regions(b->regions, b->nregions, x, y, 0.0f, sel, 65536, &home);
    return home >= 0 && home < b->nregions ? b->regions[home].id : -1;
}

/* ---- routes --------------------------------------------------------- */

typedef struct { float x, y, z, arc; int seg, node; signed char support; } Sample;
enum { SUP_NONE, SUP_TERRAIN, SUP_ROAD };

typedef struct {
    int event, seg, node0, node1;
    char name[17];
    float length, zref;
    int first, count;   /* sample run */
    int cls, site;
} Gap;

typedef struct {
    float step, min_gap, z_window, layer;
    int cross, verbose;
    char source;        /* 'P' Paths racing lines, 'L' event lanes, 'R' free roam */
} Opt;

static const char *regions_all[] = { "L4RA", "L4RB", "L4RC", "L4RD", "L4RF", "L4RG", "L4RH", "L4RR" };

static int event_cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

/* Gap classes, in attribution priority order (guide's classification). */
enum { C_HIDDEN, C_BUNDLE, C_OTHER, C_UNPLACED, C_VISTA, C_NONE, C_COUNT };
static const char *class_name[C_COUNT] = {
    "scenery-group-filtered", "wrong-stream-bundle", "material-category",
    "prototype-unreferenced", "vista-only", "missing-source"
};

typedef struct {
    int n[C_COUNT];
    int src_set, src;
    char bundle_name[8];
} GapEvidence;

typedef struct {
    char reg[8];
    Bundle b;
    TriSet sets[S_COUNT];
} Ctx;


/* One ordered run of authored route points (a 0x34149 segment of a Paths
 * racing line, or one 0x34121 lane record of a Routes file). */
typedef struct {
    int event;          /* Paths/Routes id; 0 = RoutesFreeRoam.bin */
    char kind;          /* 'P' racing line, 'F'/'B' event lanes, 'R' free roam */
    int seg;            /* segment / record index */
    char name[17];
    float *p; int n;    /* x,y,z triples; z = NAN when not authored */
} Chain;

typedef struct { Chain *v; int n, cap; } Chains;

static void chains_push(Chains *c, Chain ch) {
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 256;
        c->v = realloc(c->v, (size_t)c->cap * sizeof *c->v);
        if (!c->v) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    c->v[c->n++] = ch;
}

static void chains_free(Chains *c) {
    for (int i = 0; i < c->n; i++) free(c->v[i].p);
    free(c->v); memset(c, 0, sizeof *c);
}

/* 0x34148 nodes split by 0x34149 segment ranges (FORMATS.md). */
static int load_paths(Chains *out, const char *path, int event, int *nodes_total) {
    long len = 0;
    unsigned char *pd = n2_read_file(path, &len);
    if (!pd) return 0;
    N2Leaf nl[2], sl[2]; int nn = 0, ns = 0;
    n2_find_leaves(pd, 0, len, 0x00034148u, nl, &nn, 2);
    n2_find_leaves(pd, 0, len, 0x00034149u, sl, &ns, 2);
    if (nn != 1 || ns != 1) { free(pd); return 0; }
    const unsigned char *nodes = pd + nl[0].off;
    int nnodes = (int)(nl[0].size / 24), nseg = (int)(sl[0].size / 220);
    *nodes_total += nnodes;
    for (int si = 0; si < nseg; si++) {
        const unsigned char *r = pd + sl[0].off + (long)si * 220;
        int a = winst_u16(r + 28), b = winst_u16(r + 30);
        if (a >= b || b > nnodes) continue;
        Chain ch; memset(&ch, 0, sizeof ch);
        ch.event = event; ch.kind = 'P'; ch.seg = si;
        memcpy(ch.name, r, 16); ch.name[16] = 0;
        ch.p = malloc((size_t)(b - a) * 3 * sizeof *ch.p);
        if (!ch.p) { fprintf(stderr, "out of memory\n"); exit(2); }
        for (int i = a; i < b; i++) {
            memcpy(&ch.p[ch.n * 3], nodes + i * 24, 4);
            memcpy(&ch.p[ch.n * 3 + 1], nodes + i * 24 + 4, 4);
            ch.p[ch.n * 3 + 2] = NAN;
            ch.n++;
        }
        chains_push(out, ch);
    }
    free(pd);
    return 1;
}

/* 0x34121 lane records, with the layout src/ai.c ai_roads_read validates. */
static int load_lanes(Chains *out, const char *path, int event, char kind, int *nodes_total) {
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) return 0;
    N2Leaf leaf[2]; int nl = 0;
    n2_find_leaves(d, 0, len, 0x00034121u, leaf, &nl, 2);
    if (nl != 1 || leaf[0].size < 8) { free(d); return 0; }
    long at = leaf[0].off + 8, end = leaf[0].off + leaf[0].size;
    int rec = 0;
    while (at < end) {
        if (at + 128 > end) break;
        uint32_t pair = n2_u32(d + at + 44);
        int count = (int)(pair & 0xffffu);
        if (count < 1 || count > 4096 || (int)(pair >> 16) != count ||
            at + 128L + 56L * count > end) break;
        Chain ch; memset(&ch, 0, sizeof ch);
        ch.event = event; ch.kind = kind; ch.seg = rec++;
        memcpy(ch.name, d + at + 8, 16); ch.name[16] = 0;
        ch.p = malloc((size_t)count * 3 * sizeof *ch.p);
        if (!ch.p) { fprintf(stderr, "out of memory\n"); exit(2); }
        for (int j = 0; j < count; j++) {
            const unsigned char *q = d + at + 128 + 56 * j;
            float x, y;
            memcpy(&x, q + 4, 4); memcpy(&y, q + 8, 4);
            if (!isfinite(x) || !isfinite(y) || fabsf(x) > 1e5f || fabsf(y) > 1e5f) continue;
            ch.p[ch.n * 3] = x; ch.p[ch.n * 3 + 1] = y;
            /* signed eighths of a metre, as ai_roads_read reads event lanes */
            ch.p[ch.n * 3 + 2] = (int16_t)(n2_u32(q + 20) >> 16) / 8.0f;
            ch.n++;
        }
        *nodes_total += ch.n;
        if (ch.n) chains_push(out, ch); else free(ch.p);
        at += 140L + 56L * count;
        if (at == end + 8) at = end;
    }
    free(d);
    return 1;
}

static void push_sample(Sample **v, int *n, int *cap, Sample s) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 65536;
        *v = realloc(*v, (size_t)*cap * sizeof **v);
        if (!*v) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    (*v)[(*n)++] = s;
}

static void audit_region(Ctx *ctx, const char *troot, const Opt *opt,
                         TriSet *cross, const char **cross_reg, int ncross,
                         long totals[8]) {
    char dir[1200];
    snprintf(dir, sizeof dir, "%s/ROUTES%s", troot, ctx->reg);
    DIR *d = opendir(dir);
    if (!d) { printf("REGION %s routes=absent\n", ctx->reg); return; }
    int events[512], nev = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nev < 512) {
        int id; char tail[8];
        if (sscanf(e->d_name, "Paths%d.%7s", &id, tail) == 2 && !strcmp(tail, "bin"))
            events[nev++] = id;
    }
    closedir(d);
    qsort(events, (size_t)nev, sizeof *events, event_cmp);

    Chains chains = {0};
    int nodes_total = 0, events_with_nodes = 0;
    char path[1400];
    for (int ei = 0; ei < nev; ei++) {
        if (opt->source == 'P') {
            snprintf(path, sizeof path, "%s/Paths%d.bin", dir, events[ei]);
            events_with_nodes += load_paths(&chains, path, events[ei], &nodes_total);
        } else if (opt->source == 'L') {
            snprintf(path, sizeof path, "%s/Routes%dF.bin", dir, events[ei]);
            events_with_nodes += load_lanes(&chains, path, events[ei], 'F', &nodes_total);
        }
    }
    if (opt->source == 'R') {
        snprintf(path, sizeof path, "%s/RoutesFreeRoam.bin", dir);
        events_with_nodes += load_lanes(&chains, path, 0, 'R', &nodes_total);
    }

    long nsup[3] = {0}, wrong_layer = 0;
    double arc_sup[3] = {0};
    Gap *gaps = NULL; int ngaps = 0, capgaps = 0;
    Sample *sm = NULL; int nsm = 0, capsm = 0;

    for (int ci = 0; ci < chains.n; ci++) {
        const Chain *ch = &chains.v[ci];
        int seg_first = nsm;
        for (int ni = 0; ni < ch->n; ni++) {
            const float *p0 = ch->p + ni * 3;
            int last = ni + 1 >= ch->n;
            const float *p1 = last ? p0 : p0 + 3;
            float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
            float dist = sqrtf(dx*dx + dy*dy);
            int broken = !last && dist > NAV_LINK_MAX;
            int steps = last || broken ? 1 : (int)ceilf(dist / opt->step);
            if (steps < 1) steps = 1;
            float arc = last || broken ? 0.0f : dist / steps;
            for (int k = 0; k < steps; k++) {
                float t = (float)k / steps;
                Sample s = { p0[0] + dx * t, p0[1] + dy * t, NAN, arc, ci, ni, SUP_NONE };
                float az = p0[2] + dz * t, z;
                if (isnan(az)) {
                    if (set_hit(&ctx->sets[S_ROAD], s.x, s.y, &z) >= 0) { s.support = SUP_ROAD; s.z = z; }
                    else if (set_hit(&ctx->sets[S_TERR], s.x, s.y, &z) >= 0) { s.support = SUP_TERRAIN; s.z = z; }
                } else {
                    /* authored lane height: the support must be on that layer */
                    int h;
                    if ((h = set_hit_z(&ctx->sets[S_ROAD], s.x, s.y, az, opt->layer, opt->layer)) >= 0) s.support = SUP_ROAD;
                    else if ((h = set_hit_z(&ctx->sets[S_TERR], s.x, s.y, az, opt->layer, opt->layer)) >= 0) s.support = SUP_TERRAIN;
                    else if (set_hit(&ctx->sets[S_ROAD], s.x, s.y, NULL) >= 0 ||
                             set_hit(&ctx->sets[S_TERR], s.x, s.y, NULL) >= 0) wrong_layer++;
                    s.z = az;
                }
                nsup[(int)s.support]++; arc_sup[(int)s.support] += arc;
                push_sample(&sm, &nsm, &capsm, s);
            }
            if (broken) push_sample(&sm, &nsm, &capsm,
                (Sample){ NAN, NAN, NAN, 0, ci, ni, -1 });   /* run breaker */
        }
        /* maximal unsupported runs inside this chain */
        for (int i = seg_first; i < nsm;) {
            if (sm[i].support != SUP_NONE) { i++; continue; }
            int j = i;
            float length = 0;
            while (j < nsm && sm[j].support == SUP_NONE) { length += sm[j].arc; j++; }
            if (length + 1e-3f >= opt->min_gap) {
                if (ngaps == capgaps) { capgaps = capgaps ? capgaps*2 : 256; gaps = realloc(gaps, (size_t)capgaps * sizeof *gaps); }
                Gap *g = &gaps[ngaps++];
                memset(g, 0, sizeof *g);
                g->event = ch->event; g->seg = ch->seg;
                g->node0 = sm[i].node; g->node1 = sm[j-1].node;
                memcpy(g->name, ch->name, sizeof g->name);
                g->first = i; g->count = j - i; g->length = length;
                g->zref = sm[i + (j - i) / 2].z;   /* authored lane height */
                if (isnan(g->zref)) {
                    if (i - 1 >= seg_first && sm[i-1].support > 0) g->zref = sm[i-1].z;
                    else if (j < nsm && sm[j].support > 0) g->zref = sm[j].z;
                }
            }
            i = j;
        }
    }
    int nchains = chains.n;
    chains_free(&chains);

    /* Attribution: non-ground sets clipped to the gap bounding box. */
    if (ngaps) {
        float clip[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
        for (int i = 0; i < ngaps; i++)
            for (int k = 0; k < gaps[i].count; k++) {
                const Sample *s = &sm[gaps[i].first + k];
                clip[0] = fminf(clip[0], s->x); clip[1] = fminf(clip[1], s->y);
                clip[2] = fmaxf(clip[2], s->x); clip[3] = fmaxf(clip[3], s->y);
            }
        bundle_attribution(&ctx->b, ctx->sets, clip);
        set_index(&ctx->sets[S_OTHER]);
        set_index(&ctx->sets[S_UNPLACED]);
    }

    long samples = nsup[0] + nsup[1] + nsup[2];
    double arc_all = arc_sup[0] + arc_sup[1] + arc_sup[2];
    printf("REGION %s bundle=STREAM%s.BUN source=%s files=%d chains=%d points=%d step=%.1fm\n",
           ctx->reg, ctx->reg, opt->source == 'P' ? "paths-0x34148" : opt->source == 'L'
           ? "event-lanes-0x34121" : "freeroam-0x34121", events_with_nodes, nchains, nodes_total, opt->step);
    printf("  ground meshes road=%d terrain=%d vista=%d group-hidden=%d; tris road=%d terrain=%d vista=%d group-hidden=%d\n",
           ctx->sets[S_ROAD].nmesh, ctx->sets[S_TERR].nmesh,
           ctx->sets[S_VISTA].nmesh, ctx->sets[S_HIDDEN].nmesh,
           ctx->sets[S_ROAD].ntri, ctx->sets[S_TERR].ntri,
           ctx->sets[S_VISTA].ntri, ctx->sets[S_HIDDEN].ntri);
    if (g_focus[2] < WINST_WORLD_LIMIT)
        printf("  resident center=(%.0f,%.0f) radius=%.0f (instanced ground only)\n",
               g_focus[0], g_focus[1], g_focus[2]);
    printf("  route samples=%ld length=%.0fm ROAD=%.2f%% TERRAIN-only=%.2f%% unsupported=%.2f%% (%.0fm)\n",
           samples, arc_all,
           arc_all > 0 ? 100.0 * arc_sup[SUP_ROAD] / arc_all : 0.0,
           arc_all > 0 ? 100.0 * arc_sup[SUP_TERRAIN] / arc_all : 0.0,
           arc_all > 0 ? 100.0 * arc_sup[SUP_NONE] / arc_all : 0.0, arc_sup[SUP_NONE]);
    if (opt->source != 'P')
        printf("  layer window=+-%.1fm samples with XY ground only on another layer=%ld\n", opt->layer, wrong_layer);

    /* classify + group by site (gaps of different events on the same road) */
    long class_count[C_COUNT] = {0};
    int nsites = 0;
    for (int i = 0; i < ngaps; i++) {
        Gap *g = &gaps[i];
        GapEvidence ev; memset(&ev, 0, sizeof ev); ev.src_set = -1;
        float zr = g->zref, below = isnan(zr) ? 1e9f : opt->z_window, above = below;
        if (isnan(zr)) zr = 0;
        for (int k = 0; k < g->count; k++) {
            const Sample *s = &sm[g->first + k];
            int h;
            static const int set_of[C_COUNT] = { S_HIDDEN, -1, S_OTHER, S_UNPLACED, S_VISTA, -1 };
            for (int c = 0; c < C_COUNT; c++) {
                if (c == C_BUNDLE) {
                    for (int b = 0; b < ncross; b++)
                        if (set_hit_z(&cross[b], s->x, s->y, zr, below, above) >= 0) {
                            ev.n[c]++;
                            if (!ev.bundle_name[0]) snprintf(ev.bundle_name, sizeof ev.bundle_name, "%s", cross_reg[b]);
                            break;
                        }
                    continue;
                }
                if (set_of[c] < 0) continue;
                if ((h = set_hit_z(&ctx->sets[set_of[c]], s->x, s->y, zr, below, above)) >= 0) {
                    ev.n[c]++;
                    if (ev.src_set < 0 || set_of[c] == S_HIDDEN) { ev.src_set = set_of[c]; ev.src = h; }
                }
            }
        }
        g->cls = C_NONE;
        for (int c = 0; c < C_NONE; c++)
            if (ev.n[c] * 2 >= g->count) { g->cls = c; break; }
        class_count[g->cls]++;
        const Sample *mid = &sm[g->first + g->count / 2];
        g->site = -1;
        for (int k = 0; k < i && g->site < 0; k++) {
            const Sample *o = &sm[gaps[k].first + gaps[k].count / 2];
            if (fabsf(o->x - mid->x) < 15.0f && fabsf(o->y - mid->y) < 15.0f) g->site = gaps[k].site;
        }
        if (g->site < 0) g->site = nsites++;
        if (!opt->verbose && g->site != nsites - 1) continue;
        float cx = floorf(mid->x / RESIDENT_CELL + 0.5f) * RESIDENT_CELL;
        float cy = floorf(mid->y / RESIDENT_CELL + 0.5f) * RESIDENT_CELL;
        char srcbuf[112] = "-";
        if (ev.src_set >= 0) {
            const Src *s = &ctx->sets[ev.src_set].src[ev.src];
            if (s->placement_id == 0xffffffffu)
                snprintf(srcbuf, sizeof srcbuf, "%s:%s key=%08x chunk", set_name[ev.src_set], s->name, s->model_key);
            else
                snprintf(srcbuf, sizeof srcbuf, "%s:%s key=%08x sec=%u row=%u", set_name[ev.src_set], s->name,
                         s->model_key, s->placement_id >> 16, s->placement_id & 0xffffu);
        }
        const Sample *s0 = &sm[g->first], *s1 = &sm[g->first + g->count - 1];
        printf("  GAP site=%d %s event=%d seg=%d(%s) nodes=%d..%d from=(%.1f,%.1f) to=(%.1f,%.1f) len=%.1fm zref=%.1f "
               "district=%d resident_cell=(%.0f,%.0f) class=%s evidence[hidden=%d bundle=%d%s%s other=%d unref=%d vista=%d /%d] source=%s\n",
               g->site, ctx->reg, g->event, g->seg, g->name, g->node0, g->node1,
               s0->x, s0->y, s1->x, s1->y, g->length, g->zref,
               home_district(&ctx->b, mid->x, mid->y), cx, cy, class_name[g->cls],
               ev.n[C_HIDDEN], ev.n[C_BUNDLE], ev.bundle_name[0] ? ":" : "", ev.bundle_name,
               ev.n[C_OTHER], ev.n[C_UNPLACED], ev.n[C_VISTA], g->count, srcbuf);
    }
    /* site summary: one line per physical location */
    for (int st = 0; st < nsites; st++) {
        int nevents = 0; float maxlen = 0; int cls = -1, first = -1;
        char evbuf[256]; size_t used = 0; evbuf[0] = 0;
        for (int i = 0; i < ngaps; i++) {
            if (gaps[i].site != st) continue;
            if (first < 0) first = i;
            nevents++; maxlen = fmaxf(maxlen, gaps[i].length);
            if (cls < 0 || gaps[i].cls < cls) cls = gaps[i].cls;
            if (used + 8 < sizeof evbuf) {
                int w = snprintf(evbuf + used, sizeof evbuf - used, "%s%d", used ? "," : "", gaps[i].event);
                if (w > 0) used += (size_t)w;
            }
        }
        const Sample *mid = &sm[gaps[first].first + gaps[first].count / 2];
        printf("  SITE %d %s at=(%.1f,%.1f) district=%d max_len=%.1fm gaps=%d class=%s events=%s\n",
               st, ctx->reg, mid->x, mid->y, home_district(&ctx->b, mid->x, mid->y),
               maxlen, nevents, class_name[cls], evbuf);
    }
    printf("  SUMMARY %s gaps(>=%.1fm unsupported)=%d sites=%d", ctx->reg, opt->min_gap, ngaps, nsites);
    for (int c = 0; c < C_COUNT; c++)
        if (class_count[c]) printf(" %s=%ld", class_name[c], class_count[c]);
    printf("\n");
    totals[0] += samples; totals[1] += nsup[SUP_ROAD]; totals[2] += nsup[SUP_TERRAIN];
    totals[3] += nsup[SUP_NONE]; totals[4] += ngaps; totals[5] += nsites;
    free(gaps); free(sm);
}

int main(int argc, char **argv) {
    const char *troot = NULL;
    const char *want[8]; int nwant = 0;
    Opt opt = { 2.0f, 4.0f, 12.0f, 3.0f, 1, 0, 'P' };
    float probe[2] = { 0, 0 }; int have_probe = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--resident") && i + 2 < argc) {
            /* production resident radius around an explicit centre (the
               startup spawn, or a 400 m snapped cell for later swaps) */
            g_focus[0] = (float)atof(argv[i + 1]);
            g_focus[1] = (float)atof(argv[i + 2]);
            g_focus[2] = 1400.0f; i += 2;
        }
        else if (!strcmp(argv[i], "--probe") && i + 2 < argc) {
            probe[0] = (float)atof(argv[i + 1]); probe[1] = (float)atof(argv[i + 2]);
            have_probe = 1; i += 2;
        }
        else if (!strcmp(argv[i], "--region") && i + 1 < argc && nwant < 8) want[nwant++] = argv[++i];
        else if (!strcmp(argv[i], "--step") && i + 1 < argc) opt.step = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--min-gap") && i + 1 < argc) opt.min_gap = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--no-cross")) opt.cross = 0;
        else if (!strcmp(argv[i], "--source") && i + 1 < argc) {
            i++;
            opt.source = !strcmp(argv[i], "paths") ? 'P' : !strcmp(argv[i], "lanes") ? 'L'
                       : !strcmp(argv[i], "freeroam") ? 'R' : 0;
            if (!opt.source) { troot = NULL; break; }
        }
        else if (!strcmp(argv[i], "--layer") && i + 1 < argc) opt.layer = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) opt.verbose = 1;
        else if (argv[i][0] != '-' && !troot) troot = argv[i];
        else { troot = NULL; break; }
    }
    if (!troot || !(opt.step >= 0.25f) || !(opt.min_gap >= 0.0f)) {
        fprintf(stderr, "usage: %s TRACKS_DIR [--region L4RA]... [--step M] [--min-gap M]\n"
                        "          [--source paths|lanes|freeroam] [--layer M]\n"
                        "          [--no-cross] [--verbose] [--probe X Y] [--resident X Y]\n", argv[0]);
        return 2;
    }
    if (!nwant) for (int i = 0; i < 8; i++) want[nwant++] = regions_all[i];
    int failures = 0;

    if (have_probe) {
        float clip[4] = { probe[0] - 1, probe[1] - 1, probe[0] + 1, probe[1] + 1 };
        for (int w = 0; w < nwant; w++) {
            Ctx ctx; memset(&ctx, 0, sizeof ctx);
            if (!bundle_open(&ctx.b, troot, want[w]) || !bundle_ground(&ctx.b, ctx.sets) ||
                !bundle_attribution(&ctx.b, ctx.sets, clip)) {
                fprintf(stderr, "cannot assemble STREAM%s.BUN\n", want[w]);
                failures++;
            } else {
                printf("PROBE STREAM%s.BUN (%.2f,%.2f) district=%d\n", want[w], probe[0], probe[1],
                       home_district(&ctx.b, probe[0], probe[1]));
                for (int k = 0; k < S_COUNT; k++) {
                    set_index(&ctx.sets[k]);
                    set_probe(&ctx.sets[k], set_name[k], probe[0], probe[1]);
                }
            }
            bundle_free(&ctx.b);
            for (int k = 0; k < S_COUNT; k++) set_free(&ctx.sets[k]);
        }
        return failures ? 2 : 0;
    }

    /* Ground (ROAD+TERRAIN, ordinary scene) of every bundle, for the
       wrong-bundle class. Built one bundle at a time. */
    TriSet cross[8]; const char *cross_reg[8]; int ncross = 0;
    memset(cross, 0, sizeof cross);
    if (opt.cross) {
        for (int i = 0; i < 8; i++) {
            Bundle b; TriSet s[S_COUNT]; memset(s, 0, sizeof s);
            if (bundle_open(&b, troot, regions_all[i]) && bundle_ground(&b, s)) {
                TriSet *g = &s[S_ROAD];
                for (int t = 0; t < s[S_TERR].ntri; t++) {   /* merge TERRAIN into ROAD set */
                    if (g->ntri == g->captri) {
                        g->captri = g->captri ? g->captri * 2 : 4096;
                        g->tri = realloc(g->tri, (size_t)g->captri * sizeof *g->tri);
                        if (!g->tri) { fprintf(stderr, "out of memory\n"); return 2; }
                    }
                    Tri tr = s[S_TERR].tri[t];
                    tr.src = set_add_src(g, s[S_TERR].src[tr.src].name, s[S_TERR].src[tr.src].model_key,
                                         s[S_TERR].src[tr.src].placement_id);
                    g->tri[g->ntri++] = tr;
                }
                set_index(g);
                cross[ncross] = *g; memset(g, 0, sizeof *g);
                cross_reg[ncross++] = regions_all[i];
            }
            bundle_free(&b);
            for (int k = 0; k < S_COUNT; k++) set_free(&s[k]);
        }
    }
    long totals[8] = {0};
    for (int w = 0; w < nwant; w++) {
        Ctx ctx; memset(&ctx, 0, sizeof ctx);
        snprintf(ctx.reg, sizeof ctx.reg, "%s", want[w]);
        if (!bundle_open(&ctx.b, troot, ctx.reg) || !bundle_ground(&ctx.b, ctx.sets)) {
            fprintf(stderr, "cannot assemble ground for STREAM%s.BUN\n", ctx.reg);
            bundle_free(&ctx.b); failures++; continue;
        }
        for (int k = 0; k < S_OTHER; k++) set_index(&ctx.sets[k]);
        TriSet others[8]; const char *oreg[8]; int no = 0;
        for (int c = 0; c < ncross; c++)
            if (strcmp(cross_reg[c], ctx.reg)) { others[no] = cross[c]; oreg[no] = cross_reg[c]; no++; }
        audit_region(&ctx, troot, &opt, others, oreg, no, totals);
        bundle_free(&ctx.b);
        for (int k = 0; k < S_COUNT; k++) set_free(&ctx.sets[k]);
    }
    printf("TOTAL samples=%ld road=%ld terrain-only=%ld unsupported=%ld gaps=%ld sites=%ld\n",
           totals[0], totals[1], totals[2], totals[3], totals[4], totals[5]);
    for (int c = 0; c < ncross; c++) set_free(&cross[c]);
    return failures ? 2 : 0;
}
