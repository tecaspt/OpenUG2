/* region_convert.c - M4 region -> world-cell conversion (M4b: geometry +
 * COLLIDE + index; textures are a valid placeholder slot per key until M4c).
 *
 * Output files, all under <outdir>:
 *   index.u2pk  kind REGION. One CELLTAB section: ncell entries of
 *               {i32 cx, i32 cy, u32 off, u32 size, f32 zmin, f32 zmax} (24B).
 *               The header's 16 reserved+flags bytes carry the grid: {u16 gw,
 *               u16 gh, f32 x0, f32 y0} so the runtime maps a pose to a cell.
 *   cells.u2pk  ncell complete kind-CELL U2PK blobs, each at a 16-byte aligned
 *               offset, each with BATCHES/VERTS/INDICES/COLLIDE. A blob has no
 *               TEXTURES section: its batches index the shared tex.u2pk.
 *   tex.u2pk    kind REGION. TEXTURES (one entry per distinct source key). In
 *               M4b each is a 4x4 white DXT1 block; M4c encodes the real pixels.
 *
 * Upstream geometry comes from region_load.c (MIT submodule); the quant helpers
 * are geom.h; the byte-order helpers are shared/dxt.h. This file is original.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "region_conv.h"
#include "region_load.h"
#include "region_tex.h"
#include "geom.h"
#include "u2pk.h"

/* ---- growable byte buffer (mirrors the converter's writer pattern) ---- */
typedef struct { uint8_t *b; size_t len, cap; } Buf;
static int bgrow(Buf *s, size_t need)
{
	if (need <= s->cap) return 1;
	size_t nc = s->cap ? s->cap : 1024;
	while (nc < need) nc *= 2;
	uint8_t *nb = (uint8_t *)realloc(s->b, nc);
	if (!nb) return 0;
	s->b = nb; s->cap = nc;
	return 1;
}
static int bput(Buf *s, const void *p, size_t n)
{
	if (n == 0) return 1;
	if (!bgrow(s, s->len + n)) return 0;
	memcpy(s->b + s->len, p, n);
	s->len += n;
	return 1;
}
static int bpad(Buf *s, size_t to)   /* zero-fill up to absolute offset `to` */
{
	if (to <= s->len) return 1;
	if (!bgrow(s, to)) return 0;
	memset(s->b + s->len, 0, to - s->len);   /* memset the grown destination,
	                                          * not a memcpy of a small static */
	s->len = to;
	return 1;
}
static size_t baligned(const Buf *s) { return (s->len + 15) & ~(size_t)15; }

/* ---- a triangle carrying uv + colour, so splitting keeps them consistent ---- */
typedef struct {
	float    p[9];    /* v0x v0y v0z v1x ... v2z (world space) */
	float    uv[6];
	uint32_t col[3];  /* 0x00RRGGBB per vertex */
	int      ground;
	uint32_t texkey;
	uint32_t mode;
} RTri;

typedef struct { RTri *v; size_t n, cap; } TBucket;
static int tpush(TBucket *b, const RTri *t)
{
	if (b->n == b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 64;
		RTri *nv = (RTri *)realloc(b->v, nc * sizeof(RTri));
		if (!nv) return 0;
		b->v = nv; b->cap = nc;
	}
	b->v[b->n++] = *t;
	return 1;
}

static uint32_t col_avg(uint32_t a, uint32_t b)
{
	uint32_t r = (((a >> 16) & 0xff) + ((b >> 16) & 0xff)) >> 1;
	uint32_t g = (((a >> 8) & 0xff) + ((b >> 8) & 0xff)) >> 1;
	uint32_t bl = ((a & 0xff) + (b & 0xff)) >> 1;
	return (r << 16) | (g << 8) | bl;
}

typedef struct {
	TBucket  tris;
	float    zmin, zmax;
	int      has;
} CellB;

static float tri_edge_max(const RTri *t)
{
	float m = 0.0f;
	for (int e = 0; e < 3; e++) {
		int i = e, j = (e + 1) % 3;
		float dx = t->p[i*3+0]-t->p[j*3+0];
		float dy = t->p[i*3+1]-t->p[j*3+1];
		float dz = t->p[i*3+2]-t->p[j*3+2];
		float d = sqrtf(dx*dx + dy*dy + dz*dz);
		if (d > m) m = d;
	}
	return m;
}

static void push_leaf(const RTri *t, float cx0, float cy0, int gw, int gh,
                      CellB *cells)
{
	float mx = (t->p[0]+t->p[3]+t->p[6]) * (1.0f/3.0f);
	float my = (t->p[1]+t->p[4]+t->p[7]) * (1.0f/3.0f);
	int ix = (int)floorf((mx - cx0) / REGION_CELL_M);
	int iy = (int)floorf((my - cy0) / REGION_CELL_M);
	if (ix < 0) ix = 0; if (ix > gw-1) ix = gw-1;
	if (iy < 0) iy = 0; if (iy > gh-1) iy = gh-1;
	CellB *c = &cells[iy*gw + ix];
	if (!tpush(&c->tris, t)) return;
	for (int v = 0; v < 3; v++) {
		float z = t->p[v*3+2];
		if (!c->has) { c->has = 1; c->zmin = c->zmax = z; }
		else { if (z < c->zmin) c->zmin = z; if (z > c->zmax) c->zmax = z; }
	}
}

/* Split until every 3D edge <= REGION_EDGE_MAX, pushing leaves to cells.
 * Children of split edge (i,j) with opposite vertex r are (r,i,mid)+(r,mid,j),
 * interpolating uv and colour at the midpoint. */
static void split_push(const RTri *t, float cx0, float cy0, int gw, int gh,
                       CellB *cells, int depth)
{
	if (tri_edge_max(t) <= REGION_EDGE_MAX || depth > 12) {
		push_leaf(t, cx0, cy0, gw, gh, cells);
		return;
	}
	int longest = 0;
	float best = -1.0f;
	for (int e = 0; e < 3; e++) {
		int i = e, j = (e + 1) % 3;
		float dx = t->p[i*3+0]-t->p[j*3+0];
		float dy = t->p[i*3+1]-t->p[j*3+1];
		float dz = t->p[i*3+2]-t->p[j*3+2];
		float d = dx*dx + dy*dy + dz*dz;
		if (d > best) { best = d; longest = e; }
	}
	int i = longest, j = (longest + 1) % 3, r = (longest + 2) % 3;
	float mp[3] = { (t->p[i*3+0]+t->p[j*3+0])*0.5f,
	                (t->p[i*3+1]+t->p[j*3+1])*0.5f,
	                (t->p[i*3+2]+t->p[j*3+2])*0.5f };
	float mu0 = (t->uv[i*2+0]+t->uv[j*2+0])*0.5f;
	float mu1 = (t->uv[i*2+1]+t->uv[j*2+1])*0.5f;
	uint32_t mcol = col_avg(t->col[i], t->col[j]);

	RTri a = *t, b = *t;
	/* child A: v0=r v1=i v2=mid */
	for (int k = 0; k < 3; k++) {
		a.p[0+k] = t->p[r*3+k]; a.p[3+k] = t->p[i*3+k]; a.p[6+k] = mp[k];
		b.p[0+k] = t->p[r*3+k]; b.p[3+k] = mp[k];        b.p[6+k] = t->p[j*3+k];
	}
	a.uv[0]=t->uv[r*2+0]; a.uv[1]=t->uv[r*2+1];
	a.uv[2]=t->uv[i*2+0]; a.uv[3]=t->uv[i*2+1];
	a.uv[4]=mu0;          a.uv[5]=mu1;
	a.col[0]=t->col[r];   a.col[1]=t->col[i];   a.col[2]=mcol;
	/* child B: v0=r v1=mid v2=j */
	b.uv[0]=t->uv[r*2+0]; b.uv[1]=t->uv[r*2+1];
	b.uv[2]=mu0;          b.uv[3]=mu1;
	b.uv[4]=t->uv[j*2+0]; b.uv[5]=t->uv[j*2+1];
	b.col[0]=t->col[r];   b.col[1]=mcol;        b.col[2]=t->col[j];

	split_push(&a, cx0, cy0, gw, gh, cells, depth+1);
	split_push(&b, cx0, cy0, gw, gh, cells, depth+1);
}

typedef struct { uint32_t key; int slot; } TexSlot;
static int texslot_find(const TexSlot *t, int n, uint32_t key)
{
	for (int i = 0; i < n; i++) if (t[i].key == key) return t[i].slot;
	return -1;
}

/* Build one CELL U2PK blob and append it to `cells`. On success *out_size is
 * its byte length and *out_batches its batch count. Returns 0 on failure. */
static int emit_cell(const CellB *c, float x0, float y0, int cx, int cy,
                     TexSlot *tex, int ntex, Buf *cells,
                     uint32_t *out_size, int *out_batches)
{
	float cellx0 = x0 + cx * REGION_CELL_M, celly0 = y0 + cy * REGION_CELL_M;
	float cellx1 = cellx0 + REGION_CELL_M, celly1 = celly0 + REGION_CELL_M;
	float z0 = c->zmin, z1 = c->zmax;
	if (!(z1 > z0)) z1 = z0 + 0.001f;
	float poff[3] = { (cellx0+cellx1)*0.5f, (celly0+celly1)*0.5f, (z0+z1)*0.5f };
	float psc [3] = { (cellx1-cellx0)*0.5f, (celly1-celly0)*0.5f, (z1-z0)*0.5f };
	for (int a = 0; a < 3; a++) if (psc[a] < 0.001f) psc[a] = 0.001f;

	/* group by (texkey, mode), first-seen order */
	int *grp = (int *)malloc((c->tris.n ? c->tris.n : 1) * sizeof(int));
	if (!grp) return 0;
	int maxb = (int)c->tris.n;
	U2pkBatch *hdrs = (U2pkBatch *)malloc((maxb ? maxb : 1) * sizeof(U2pkBatch));
	uint32_t *keys = (uint32_t *)malloc((maxb ? maxb : 1) * sizeof(uint32_t));
	uint32_t *mds  = (uint32_t *)malloc((maxb ? maxb : 1) * sizeof(uint32_t));
	if (!hdrs || !keys || !mds) { free(grp); free(hdrs); free(keys); free(mds); return 0; }
	int nhdr = 0;
	for (size_t k = 0; k < c->tris.n; k++) {
		uint32_t key = c->tris.v[k].texkey, md = c->tris.v[k].mode;
		int found = -1;
		for (int b = 0; b < nhdr; b++)
			if (keys[b] == key && mds[b] == md) { found = b; break; }
		if (found < 0) { found = nhdr; keys[nhdr] = key; mds[nhdr] = md; nhdr++; }
		grp[k] = found;
	}

	Buf verts = {0}, ixs = {0}, bts = {0};
	int ok = 1;
	for (int b = 0; b < nhdr && ok; b++) {
		U2pkBatch h; memset(&h, 0, sizeof h);
		h.texture = (int32_t)texslot_find(tex, ntex, keys[b]);
		h.mode = mds[b];
		for (int a = 0; a < 3; a++) { h.pos_offset[a] = poff[a]; h.pos_scale[a] = psc[a]; }
		h.uv_scale[0] = h.uv_scale[1] = 1.0f;
		h.uv_offset[0] = h.uv_offset[1] = 0.0f;
		h.first_vertex = (uint32_t)(verts.len / sizeof(U2pkVertex));
		h.first_index  = (uint32_t)(ixs.len  / sizeof(uint16_t));
		uint32_t nv = 0, ni = 0;
		for (size_t k = 0; k < c->tris.n; k++) {
			if (grp[k] != b) continue;
			const RTri *t = &c->tris.v[k];
			if (nv + 3 > 65535) { ok = 0; break; }
			for (int v = 0; v < 3 && ok; v++) {
				U2pkVertex x;
				x.u = q16_uv(t->uv[v*2+0], 1.0f);
				x.v = q16_uv(t->uv[v*2+1], 1.0f);
				x.color = u2pk_color5650(t->col[v]);
				x.x = q16_pos(t->p[v*3+0] - poff[0], psc[0]);
				x.y = q16_pos(t->p[v*3+1] - poff[1], psc[1]);
				x.z = q16_pos(t->p[v*3+2] - poff[2], psc[2]);
				ok = bput(&verts, &x, sizeof x); nv++;
			}
			for (int v = 0; v < 3 && ok; v++) {
				uint16_t ii = (uint16_t)(nv - 3 + v);
				ok = bput(&ixs, &ii, sizeof ii); ni++;
			}
		}
		h.vertex_count = nv;
		h.index_count = ni;
		if (ok && (nv == 0 || ni == 0 || ni % 3)) ok = 0;
		/* pad runs so the next batch stays 16-byte aligned (12B verts, 2B idx) */
		if (ok) ok = bpad(&verts, ((size_t)h.first_vertex + ((nv+3)&~3u)) * sizeof(U2pkVertex));
		if (ok) ok = bput(&bts, &h, sizeof h);
		if (ok) ok = bpad(&ixs,  ((size_t)h.first_index  + ((ni+7)&~7u))  * sizeof(uint16_t));
		if (ok) hdrs[b] = h;
	}
	free(grp); free(keys); free(mds);
	if (!ok || nhdr == 0) {
		free(hdrs); free(verts.b); free(ixs.b); free(bts.b);
		return 0;
	}

	/* COLLIDE section: ground tris as f32 xyz triples + a 10 m centroid grid.
	 * layout: i32 ntri; f32 gx0, gy0; i32 ggw, ggh; then ntri*9 f32; then
	 * u16 start[ggw*ggh+1]; then u16 list[start[last]]. */
	Buf col = {0};
	int ggw = (int)ceilf(REGION_CELL_M / 10.0f), ggh = ggw, ng = ggw * ggh;
	int ntri = 0;
	for (size_t k = 0; k < c->tris.n; k++) if (c->tris.v[k].ground) ntri++;
	{
		int32_t n = ntri; bput(&col, &n, 4);
		float gx0 = cellx0, gy0 = celly0; bput(&col, &gx0, 4); bput(&col, &gy0, 4);
		int32_t w = ggw, hh = ggh; bput(&col, &w, 4); bput(&col, &hh, 4);
	}
	for (size_t k = 0; k < c->tris.n; k++)
		if (c->tris.v[k].ground) bput(&col, c->tris.v[k].p, 9 * sizeof(float));
	{
		int *cnt = (int *)calloc(ng, sizeof(int));
		int *gid = (int *)malloc((ntri ? ntri : 1) * sizeof(int)); /* source->ground ordinal */
		int ord = 0;
		for (size_t k = 0; k < c->tris.n; k++) {
			if (!c->tris.v[k].ground) continue;
			const float *p = c->tris.v[k].p;
			float mx = (p[0]+p[3]+p[6])/3.0f, my = (p[1]+p[4]+p[7])/3.0f;
			int gx = (int)floorf((mx-cellx0)/10.0f), gy = (int)floorf((my-celly0)/10.0f);
			if (gx<0)gx=0; if (gx>ggw-1)gx=ggw-1;
			if (gy<0)gy=0; if (gy>ggh-1)gy=ggh-1;
			gid[ord] = gy*ggw+gx;
			cnt[gy*ggw+gx]++;
			ord++;
		}
		int *start = (int *)malloc((ng + 1) * sizeof(int));
		start[0] = 0;
		for (int g = 0; g < ng; g++) start[g+1] = start[g] + cnt[g];
		uint16_t *list = (uint16_t *)malloc((start[ng] ? start[ng] : 1) * sizeof(uint16_t));
		int *fill = (int *)malloc(ng * sizeof(int));
		for (int g = 0; g < ng; g++) fill[g] = start[g];
		ord = 0;
		for (size_t k = 0; k < c->tris.n; k++) {
			if (!c->tris.v[k].ground) continue;
			list[fill[gid[ord]]] = (uint16_t)ord;
			fill[gid[ord]]++;
			ord++;
		}
		for (int g = 0; g <= ng; g++) { uint16_t v = (uint16_t)start[g]; bput(&col, &v, 2); }
		for (int g = 0; g < start[ng]; g++) { uint16_t v = list[g]; bput(&col, &v, 2); }
		free(cnt); free(gid); free(start); free(list); free(fill);
	}

	/* assemble: header(32) + 4 sections(64) then payloads, all 16-aligned */
	uint32_t o0 = 32 + 16 * 4;
	uint32_t o1 = (uint32_t)((o0 + bts.len + 15) & ~(size_t)15);
	uint32_t o2 = (uint32_t)((o1 + verts.len + 15) & ~(size_t)15);
	uint32_t o3 = (uint32_t)((o2 + ixs.len  + 15) & ~(size_t)15);
	uint32_t total = (uint32_t)(o3 + col.len);

	U2pkHeader hd; memset(&hd, 0, sizeof hd);
	memcpy(hd.magic, U2PK_MAGIC, 4);
	hd.version = U2PK_VERSION; hd.kind = U2PK_KIND_CELL;
	hd.section_count = 4; hd.total_size = total;
	U2pkSection sect[4] = {
		{ U2PK_S_BATCHES, o0, (uint32_t)bts.len, (uint32_t)nhdr },
		{ U2PK_S_VERTS,   o1, (uint32_t)verts.len, (uint32_t)(verts.len/sizeof(U2pkVertex)) },
		{ U2PK_S_INDICES, o2, (uint32_t)ixs.len, (uint32_t)(ixs.len/sizeof(uint16_t)) },
		{ U2PK_S_COLLIDE, o3, (uint32_t)col.len, (uint32_t)ntri },
	};
	if (!bpad(cells, baligned(cells))) { goto fail; }
	uint32_t base = (uint32_t)cells->len;
	ok = bput(cells, &hd, sizeof hd) && bput(cells, sect, sizeof sect);
	ok = ok && bpad(cells, base + o0) && bput(cells, bts.b, bts.len);
	ok = ok && bpad(cells, base + o1) && bput(cells, verts.b, verts.len);
	ok = ok && bpad(cells, base + o2) && bput(cells, ixs.b, ixs.len);
	ok = ok && bpad(cells, base + o3) && bput(cells, col.b, col.len);
	free(hdrs); free(verts.b); free(ixs.b); free(bts.b); free(col.b);
	if (!ok) return 0;
	*out_size = (uint32_t)(cells->len - base);
	*out_batches = nhdr;
	return 1;
fail:
	free(hdrs); free(verts.b); free(ixs.b); free(bts.b); free(col.b);
	return 0;
}

int region_convert(const char *dataroot, const char *trackname,
                   const char *outdir, RegionReport *rep)
{
	memset(rep, 0, sizeof *rep);
	snprintf(rep->trackname, sizeof rep->trackname, "%s", trackname);

	Region reg;
	const char *why = 0;
	if (!region_open(&reg, dataroot, trackname, &why)) {
		fprintf(stderr, "region_convert: %s\n", why ? why : "load failed");
		return 0;
	}
	float x0 = reg.ground_bb[0], x1 = reg.ground_bb[1];
	float y0 = reg.ground_bb[2], y1 = reg.ground_bb[3];
	int gw = (int)ceilf((x1 - x0) / REGION_CELL_M); if (gw < 1) gw = 1;
	int gh = (int)ceilf((y1 - y0) / REGION_CELL_M); if (gh < 1) gh = 1;
	int ncell = gw * gh;

	CellB *cells = (CellB *)calloc((size_t)ncell, sizeof(CellB));
	if (!cells) { region_close(&reg); return 0; }

	N2Scene *sc = reg.scene;
	for (int i = 0; i < sc->count; i++) {
		const N2Mesh *m = &sc->meshes[i];
		if (!region_mesh_visible(m)) continue;
		rep->nmesh_visible++;
		int ground = region_mesh_ground(m);
		const float *V = m->verts;
		const unsigned char *vc = m->vcol;
		for (int t = 0; t + 2 < m->nidx; t += 3) {
			int idx[3] = { m->idx[t+0], m->idx[t+1], m->idx[t+2] };
			RTri rt; memset(&rt, 0, sizeof rt);
			for (int v = 0; v < 3; v++) {
				const float *p = &V[idx[v]*5];
				rt.p[v*3+0] = p[0]; rt.p[v*3+1] = p[1]; rt.p[v*3+2] = p[2];
				rt.uv[v*2+0] = p[3]; rt.uv[v*2+1] = p[4];
				if (vc) { const unsigned char *q = vc + (size_t)idx[v]*4;
					rt.col[v] = ((uint32_t)q[0]<<16) | ((uint32_t)q[1]<<8) | q[2]; }
				else rt.col[v] = 0x00ffffff;
			}
			rt.ground = ground;
			rt.texkey = m->texkey;
			rt.mode = U2PK_MODE_OPAQUE;
			split_push(&rt, x0, y0, gw, gh, cells, 0);
			rep->tris_out++;
		}
	}

	/* distinct texture keys -> slots (and whether the key is used by ground) */
	static TexSlot tex[4096];
	static int kground[4096];
	int ntex = 0;
	for (int c = 0; c < ncell; c++) {
		if (!cells[c].has) continue;
		for (size_t k = 0; k < cells[c].tris.n; k++) {
			uint32_t key = cells[c].tris.v[k].texkey;
			int g = cells[c].tris.v[k].ground;
			int found = texslot_find(tex, ntex, key);
			if (found < 0) {
				if (ntex >= 4096) continue;
				tex[ntex].key = key; tex[ntex].slot = ntex;
				kground[ntex] = g; ntex++;
			} else {
				kground[found] |= g;
			}
		}
	}
	rep->ntex = ntex;

	Buf cellfile = {0};
	int *coff = (int *)calloc((size_t)ncell, sizeof(int));
	int *csz  = (int *)calloc((size_t)ncell, sizeof(int));
	float *cz0 = (float *)calloc((size_t)ncell, sizeof(float));
	float *cz1 = (float *)calloc((size_t)ncell, sizeof(float));
	int nb = 0;
	for (int c = 0; c < ncell; c++) {
		int cx = c % gw, cy = c / gw;
		if (!cells[c].has) continue;
		uint32_t sz = 0; int cell_batches = 0;
		if (!bpad(&cellfile, baligned(&cellfile))) { why = "out of memory"; goto fail; }
		coff[c] = (int)cellfile.len;
		if (!emit_cell(&cells[c], x0, y0, cx, cy, tex, ntex,
		               &cellfile, &sz, &cell_batches)) {
			why = "cell emit failed"; goto fail;
		}
		csz[c] = (int)sz;
		cz0[c] = cells[c].zmin; cz1[c] = cells[c].zmax;
		nb += cell_batches;
		rep->ncell_used++;
		if ((int)cells[c].tris.n > rep->worst_cell_tris) {
			rep->worst_cell_tris = (int)cells[c].tris.n;
			rep->worst_cell_cx = cx; rep->worst_cell_cy = cy;
		}
	}
	rep->nbatch_total = nb;
	rep->grid_w = gw; rep->grid_h = gh; rep->ncell = ncell;

	for (int cy = 0; cy < gh; cy++) for (int cx = 0; cx < gw; cx++) {
		int tris = 0;
		for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
			int nx = cx+dx, ny = cy+dy;
			if (nx<0||ny<0||nx>=gw||ny>=gh) continue;
			tris += (int)cells[ny*gw+nx].tris.n;
		}
		if (tris > rep->worst_win_tris) rep->worst_win_tris = tris;
	}
	rep->within_budget = rep->worst_win_tris <= 15000;

	{	char path[1024]; snprintf(path, sizeof path, "%s/cells.u2pk", outdir);
		FILE *f = fopen(path, "wb");
		if (!f) { why = "cannot write cells.u2pk"; goto fail; }
		if (cellfile.len) fwrite(cellfile.b, 1, cellfile.len, f);
		fclose(f);
		rep->cells_bytes = (long long)cellfile.len;
	}
	{ /* index.u2pk */
		uint32_t tabbytes = (uint32_t)ncell * 24;
		uint32_t total = 32 + 16 + tabbytes;
		U2pkHeader hd; memset(&hd, 0, sizeof hd);
		memcpy(hd.magic, U2PK_MAGIC, 4);
		hd.version = U2PK_VERSION; hd.kind = U2PK_KIND_REGION;
		hd.section_count = 1; hd.total_size = total;
		uint16_t gw16 = (uint16_t)gw, gh16 = (uint16_t)gh;
		memcpy(&hd.reserved[0], &gw16, 2);
		memcpy(&hd.reserved[2], &gh16, 2);
		memcpy(&hd.reserved[4], &x0, 4);
		memcpy(&hd.reserved[8], &y0, 4);
		U2pkSection sect = { U2PK_S_CELLTAB, 48, tabbytes, (uint32_t)ncell };
		Buf idx = {0};
		bput(&idx, &hd, sizeof hd); bput(&idx, &sect, sizeof sect);
		for (int c = 0; c < ncell; c++) {
			int32_t ccx = c % gw, ccy = c / gw;
			uint32_t off = (uint32_t)coff[c], sz = (uint32_t)csz[c];
			float za = cz0[c], zb = cz1[c];
			bput(&idx, &ccx, 4); bput(&idx, &ccy, 4);
			bput(&idx, &off, 4); bput(&idx, &sz, 4);
			bput(&idx, &za, 4); bput(&idx, &zb, 4);
		}
		char path[1024]; snprintf(path, sizeof path, "%s/index.u2pk", outdir);
		FILE *f = fopen(path, "wb");
		if (f) { fwrite(idx.b, 1, idx.len, f); fclose(f); }
		rep->index_bytes = (long long)idx.len;
		free(idx.b);
	}
	{ /* tex.u2pk: real T8 art per distinct key (decoded from GLOBALB), or a
	   * white placeholder for a key not present there. */
		RTex *rtex = (RTex *)calloc((size_t)(ntex ? ntex : 1), sizeof(RTex));
		if (!rtex) { why = "out of memory (tex)"; goto fail; }
		for (int i = 0; i < ntex; i++) {
			rtex[i].key = tex[i].key;
			rtex[i].ground = kground[i];
		}
		char tpath[1024]; snprintf(tpath, sizeof tpath, "%s/tex.u2pk", outdir);
		char terr[256]; long long tbytes = 0;
		int ok = region_tex_write(&reg.w->neighborhood, rtex, ntex, tpath, &tbytes,
		                          terr, (int)sizeof terr);
		int nenc = 0;
		for (int i = 0; i < ntex; i++) if (rtex[i].encoded) nenc++;
		free(rtex);
		if (!ok) { why = terr[0] ? terr : "tex write failed"; goto fail; }
		rep->tex_bytes = tbytes;
		rep->ntex_real = nenc;
	}
	rep->ncell_written = rep->ncell_used;
	free(cellfile.b); free(coff); free(csz); free(cz0); free(cz1);
	for (int c = 0; c < ncell; c++) free(cells[c].tris.v);
	free(cells);
	region_close(&reg);
	return 1;

fail:
	free(cellfile.b); free(coff); free(csz); free(cz0); free(cz1);
	for (int c = 0; c < ncell; c++) free(cells[c].tris.v);
	free(cells);
	region_close(&reg);
	fprintf(stderr, "region_convert: %s\n", why ? why : "failed");
	return 0;
}

/* Emulator-test helper: see region_conv.h.
 *
 * The spec names ROUTES<REG>/Paths*.bin racing-line nodes as the pose source,
 * but the L4RH free-roam file stores a route graph (chunk 0x34147/0x3414a), not
 * the coarse 0x34148 centreline n2_load_path parses, and its points are in the
 * game's absolute frame. So this picks poses straight from the geometry it is
 * going to render: it samples N2_ROAD triangle centroids from the loaded
 * world-space scene, greedily selects <npose> that are far apart, and for each
 * sets the camera 2 m above the road surface looking along that triangle's
 * longest edge - the same "on a road, 2 m up, along the road" the spec asks
 * for, guaranteed on-road and inside the cell grid. Deviation is noted in
 * PROGRESS.md. */
typedef struct { float x, y, z, yaw; } RoadPose;

static float road_tri_yaw(const float *a, const float *b, const float *c)
{
	/* Heading along the longest horizontal (XY) edge: a road's local direction. */
	float best = -1.0f, dx = 0.0f, dy = 0.0f;
	const float *e[3][2] = { { a, b }, { b, c }, { c, a } };
	for (int k = 0; k < 3; k++) {
		const float *p = e[k][0], *q = e[k][1];
		float ex = q[0] - p[0], ey = q[1] - p[1];
		float d = ex * ex + ey * ey;
		if (d > best) { best = d; dx = ex; dy = ey; }
	}
	return atan2f(dy, dx);
}

int region_poses(const char *dataroot, const char *trackname,
                 const char *cfgpath, int npose)
{
	const char *reg = !strncmp(trackname, "STREAM", 6) ? trackname + 6 : trackname;
	Region rgn;
	const char *why = 0;
	N2Scene *sc;
	FILE *o;
	int wrote = 0;
	RoadPose *cand;
	int ncand = 0, cap = 8192;

	if (npose < 1) npose = 1;
	if (npose > MAX_POSE_DUMPS) npose = MAX_POSE_DUMPS;

	if (!region_open(&rgn, dataroot, trackname, &why)) {
		fprintf(stderr, "region_poses: %s\n", why ? why : "region open failed");
		return -1;
	}
	sc = rgn.scene;

	cand = (RoadPose *)malloc((size_t)cap * sizeof(RoadPose));
	if (!cand) { region_close(&rgn); return -1; }
	for (int m = 0; m < sc->count && ncand < cap; m++) {
		const N2Mesh *me = &sc->meshes[m];
		if (me->cat != N2_ROAD || !me->idx || !me->verts) continue;
		for (int t = 0; t + 2 < me->nidx && ncand < cap; t += 3) {
			const float *a = me->verts + me->idx[t] * 5;
			const float *b = me->verts + me->idx[t + 1] * 5;
			const float *c = me->verts + me->idx[t + 2] * 5;
			RoadPose *p = &cand[ncand++];
			p->x = (a[0] + b[0] + c[0]) * (1.0f / 3.0f);
			p->y = (a[1] + b[1] + c[1]) * (1.0f / 3.0f);
			p->z = (a[2] + b[2] + c[2]) * (1.0f / 3.0f);
			p->yaw = road_tri_yaw(a, b, c);
		}
	}
	if (ncand < 1) {
		fprintf(stderr, "region_poses: no ROAD triangles to stand on\n");
		free(cand);
		region_close(&rgn);
		return 0;
	}

	/* Greedy farthest-point selection so the poses are spread across the slice.
	 * Seed on the road centroid farthest from the mean (a region extreme), so
	 * the run is deterministic regardless of mesh order. */
	{
		float mx = 0, my = 0;
		int sel[MAX_POSE_DUMPS], ns = 0;
		for (int i = 0; i < ncand; i++) { mx += cand[i].x; my += cand[i].y; }
		mx /= ncand; my /= ncand;
		int seed = 0;
		float bd = -1.0f;
		for (int i = 0; i < ncand; i++) {
			float dx = cand[i].x - mx, dy = cand[i].y - my, d = dx * dx + dy * dy;
			if (d > bd) { bd = d; seed = i; }
		}
		sel[ns++] = seed;
		while (ns < npose) {
			int pick = -1;
			float best = -1.0f;
			for (int i = 0; i < ncand; i++) {
				float mind = 1e30f;
				for (int s = 0; s < ns; s++) {
					float dx = cand[i].x - cand[sel[s]].x;
					float dy = cand[i].y - cand[sel[s]].y;
					float d = dx * dx + dy * dy;
					if (d < mind) mind = d;
				}
				if (mind > best) { best = mind; pick = i; }
			}
			if (pick < 0 || best <= 1.0f) break;   /* exhausted spread */
			sel[ns++] = pick;
		}
		o = fopen(cfgpath, "w");
		if (!o) {
			fprintf(stderr, "region_poses: cannot write %s\n", cfgpath);
			free(cand);
			region_close(&rgn);
			return -1;
		}
		fprintf(o, "region=%s\n", reg);
		for (int s = 0; s < ns; s++) {
			RoadPose *p = &cand[sel[s]];
			fprintf(o, "pose=%.2f %.2f %.2f %.4f\n", p->x, p->y, p->z + 2.0f,
			        p->yaw);
			printf("  pose %d x=%.1f y=%.1f ground=%.2f yaw=%.3f\n", s, p->x,
			       p->y, p->z, p->yaw);
			wrote++;
		}
		fclose(o);
	}
	free(cand);
	region_close(&rgn);
	printf("  road triangles sampled: %d\n", ncand);
	return wrote;
}
