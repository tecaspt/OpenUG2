/* region_conv.c - see region_conv.h. This increment implements region_survey:
 * the geometry half of M4 (grid + 8 m triangle splitting + centroid cell
 * assignment + budget table), no file output yet.
 *
 * Upstream geometry comes from region_load.c (MIT submodule). Everything here
 * is original converter code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "region_conv.h"
#include "region_load.h"

/* ---- per-cell accumulation ---- */
typedef struct {
	int      tris;
	float    zmin, zmax;
	int      nbatches;     /* distinct (cell, texture) after dedup */
	int      texoff, texn; /* slice of the per-cell unique-texture array */
} Cell;

/* A (cell, texkey) pair, collected per assigned triangle then sorted+unique'd
 * to recover distinct batches per cell and per 3x3 window. */
typedef struct { int cell; uint32_t tex; } Pair;

static int pair_cmp(const void *a, const void *b)
{
	const Pair *x = a, *y = b;
	if (x->cell != y->cell) return x->cell - y->cell;
	return (x->tex < y->tex) ? -1 : (x->tex > y->tex) ? 1 : 0;
}

/* dynamic Pair vector */
typedef struct { Pair *v; size_t n, cap; } PVec;
static void ppush(PVec *p, int cell, uint32_t tex)
{
	if (p->n == p->cap) {
		size_t nc = p->cap ? p->cap * 2 : 4096;
		Pair *nv = (Pair *)realloc(p->v, nc * sizeof(Pair));
		if (!nv) return;
		p->v = nv; p->cap = nc;
	}
	p->v[p->n].cell = cell;
	p->v[p->n].tex = tex;
	p->n++;
}

/* Split triangle (a,b,c) so no edge exceeds REGION_EDGE_MAX, assigning each
 * final triangle to the cell of its centroid. cx0/cy0 are grid origins; gw/gh
 * the grid size. Recursion is depth-capped so a degenerate giant triangle
 * cannot run away. */
static inline float dist3(const float *p, const float *q)
{
	float dx = p[0]-q[0], dy = p[1]-q[1], dz = p[2]-q[2];
	return sqrtf(dx*dx + dy*dy + dz*dz);
}

static int cell_of(float x, float y, float cx0, float cy0, int gw, int gh)
{
	int ix = (int)floorf((x - cx0) / REGION_CELL_M);
	int iy = (int)floorf((y - cy0) / REGION_CELL_M);
	if (ix < 0) ix = 0; if (ix > gw-1) ix = gw-1;
	if (iy < 0) iy = 0; if (iy > gh-1) iy = gh-1;
	return iy * gw + ix;
}

static long long assign_tri(const float *a, const float *b, const float *c,
                            float cx0, float cy0, int gw, int gh,
                            uint32_t tex, Cell *cells, PVec *pairs)
{
	float mx = (a[0]+b[0]+c[0]) * (1.0f/3.0f);
	float my = (a[1]+b[1]+c[1]) * (1.0f/3.0f);
	int cell = cell_of(mx, my, cx0, cy0, gw, gh);
	Cell *cl = &cells[cell];
	cl->tris++;
	float z0 = a[2] < b[2] ? (a[2] < c[2] ? a[2] : c[2])
	                      : (b[2] < c[2] ? b[2] : c[2]);
	float z1 = a[2] > b[2] ? (a[2] > c[2] ? a[2] : c[2])
	                      : (b[2] > c[2] ? b[2] : c[2]);
	if (cl->tris == 1) { cl->zmin = z0; cl->zmax = z1; }
	else { if (z0 < cl->zmin) cl->zmin = z0; if (z1 > cl->zmax) cl->zmax = z1; }
	ppush(pairs, cell, tex);
	return 1;
}

static long long split_assign(const float *a, const float *b, const float *c,
                              float cx0, float cy0, int gw, int gh,
                              uint32_t tex, Cell *cells, PVec *pairs, int depth)
{
	float eab = dist3(a, b), ebc = dist3(b, c), eca = dist3(c, a);
	float m = eab;
	int longest = 0;                 /* 0=AB, 1=BC, 2=CA */
	if (ebc > m) { m = ebc; longest = 1; }
	if (eca > m) { m = eca; longest = 2; }
	if (m <= REGION_EDGE_MAX || depth > 12)
		return assign_tri(a, b, c, cx0, cy0, gw, gh, tex, cells, pairs);

	/* Split the longest edge pq at its midpoint and join it to the opposite
	 * vertex r, giving children (r,p,mid) and (r,mid,q). Using r/p/q (not a
	 * fixed a/b/c) is essential: hard-coding the AB split built wrong,
	 * overlapping triangles whenever the longest edge was BC or CA, which
	 * never shrank and blew the count up ~67x. */
	float mid[3];
	const float *p = (longest == 0) ? a : (longest == 1) ? b : c;
	const float *q = (longest == 0) ? b : (longest == 1) ? c : a;
	const float *r = (longest == 0) ? c : (longest == 1) ? a : b;
	for (int k = 0; k < 3; k++) mid[k] = (p[k] + q[k]) * 0.5f;
	return split_assign(r, p, mid, cx0, cy0, gw, gh, tex, cells, pairs, depth+1)
	     + split_assign(r, mid, q, cx0, cy0, gw, gh, tex, cells, pairs, depth+1);
}

/* distinct-texture count of a merged list */
static int tex_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return (x < y) ? -1 : (x > y) ? 1 : 0;
}
static int uniq_count(uint32_t *t, size_t n)
{
	if (!n) return 0;
	qsort(t, n, sizeof *t, tex_cmp);
	size_t u = 1;
	for (size_t i = 1; i < n; i++) if (t[i] != t[u-1]) t[u++] = t[i];
	return (int)u;
}

int region_survey(const char *dataroot, const char *trackname, RegionReport *rep)
{
	memset(rep, 0, sizeof *rep);
	snprintf(rep->trackname, sizeof rep->trackname, "%s", trackname);

	Region reg;
	const char *why = 0;
	if (!region_open(&reg, dataroot, trackname, &why)) {
		fprintf(stderr, "region_survey: %s\n", why ? why : "load failed");
		return 0;
	}

	float x0 = reg.ground_bb[0], x1 = reg.ground_bb[1];
	float y0 = reg.ground_bb[2], y1 = reg.ground_bb[3];
	int gw = (int)ceilf((x1 - x0) / REGION_CELL_M); if (gw < 1) gw = 1;
	int gh = (int)ceilf((y1 - y0) / REGION_CELL_M); if (gh < 1) gh = 1;
	rep->grid_w = gw; rep->grid_h = gh; rep->ncell = gw * gh;

	Cell *cells = (Cell *)calloc((size_t)rep->ncell, sizeof(Cell));
	if (!cells) { region_close(&reg); return 0; }

	PVec pairs = {0};

	N2Scene *sc = reg.scene;
	for (int i = 0; i < sc->count; i++) {
		const N2Mesh *m = &sc->meshes[i];
		if (!region_mesh_visible(m)) continue;
		rep->nmesh_visible++;
		int ground = region_mesh_ground(m);
		int road = (m->cat == N2_ROAD);
		const float *V = m->verts;
		for (int t = 0; t + 2 < m->nidx; t += 3) {
			const float *a = &V[m->idx[t+0] * 5];
			const float *b = &V[m->idx[t+1] * 5];
			const float *c = &V[m->idx[t+2] * 5];
			/* source-edge histogram (XY only: that is what the cell grid sees) */
			float eab = hypotf(a[0]-b[0], a[1]-b[1]);
			float ebc = hypotf(b[0]-c[0], b[1]-c[1]);
			float eca = hypotf(c[0]-a[0], c[1]-a[1]);
			float em = eab > ebc ? (eab > eca ? eab : eca) : (ebc > eca ? ebc : eca);
			int h = em <= 2 ? 0 : em <= 4 ? 1 : em <= 8 ? 2 :
			        em <= 16 ? 3 : em <= 32 ? 4 : 5;
			rep->src_hist_n[h]++;
			if (em > rep->max_src_edge) rep->max_src_edge = em;
			if (em > 32.0f) rep->big_src_tris++;
			rep->tris_in++;
			if (road) rep->road_in++;
			else if (ground) rep->terr_in++;
			else rep->other_in++;
			long long made;
			if (ground)
				made = split_assign(a, b, c, x0, y0, gw, gh,
				                    m->texkey, cells, &pairs, 0);
			else
				made = assign_tri(a, b, c, x0, y0, gw, gh,
				                  m->texkey, cells, &pairs);
			rep->tris_out += made;
			if (road) rep->road_out += made;
			else if (ground) rep->terr_out += made;
			else rep->other_out += made;
		}
	}

	/* distinct (cell,tex) pairs -> per-cell batch counts and per-cell texture
	 * slices, then the worst 3x3 window. */
	if (pairs.n) {
		qsort(pairs.v, pairs.n, sizeof(Pair), pair_cmp);
		size_t u = 0;
		for (size_t i = 0; i < pairs.n; i++)
			if (!u || pairs.v[i].cell != pairs.v[u-1].cell ||
			    pairs.v[i].tex != pairs.v[u-1].tex)
				pairs.v[u++] = pairs.v[i];
		pairs.n = u;
		for (size_t i = 0; i < pairs.n; i++) {
			Cell *cl = &cells[pairs.v[i].cell];
			if (cl->texn == 0) cl->texoff = (int)i;
			cl->texn++;
			cl->nbatches = cl->texn;
		}
	}

	for (int c = 0; c < rep->ncell; c++) {
		if (cells[c].tris) rep->ncell_used++;
		if (cells[c].tris > rep->worst_cell_tris) {
			rep->worst_cell_tris = cells[c].tris;
			rep->worst_cell_cx = c % gw;
			rep->worst_cell_cy = c / gw;
		}
	}

	/* worst 3x3 window: triangles + distinct batches */
	uint32_t *wtex = (uint32_t *)malloc((size_t)pairs.n * sizeof(uint32_t));
	for (int cy = 0; cy < gh; cy++) {
		for (int cx = 0; cx < gw; cx++) {
			int tris = 0; size_t wt = 0;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					int nx = cx+dx, ny = cy+dy;
					if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
					Cell *cl = &cells[ny*gw+nx];
					tris += cl->tris;
					for (int k = 0; k < cl->texn; k++)
						wtex[wt++] = pairs.v[cl->texoff+k].tex;
				}
			}
			int batches = wtex ? uniq_count(wtex, wt) : 0;
			if (tris > rep->worst_win_tris) rep->worst_win_tris = tris;
			if (batches > rep->worst_win_batches) rep->worst_win_batches = batches;
		}
	}
	free(wtex);

	rep->ntex = (int)pairs.n; /* upper bound: distinct (cell,tex); refined later */
	rep->within_budget = rep->worst_win_tris <= 15000 &&
	                     rep->worst_win_batches <= 250;

	free(pairs.v);
	free(cells);
	region_close(&reg);
	return 1;
}
