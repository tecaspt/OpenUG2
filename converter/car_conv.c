/* car_conv.c - turns one loaded Car into a U2PK v1 car file.
 *
 * M3 steps 2-7: choose meshes -> rebuild textures -> bake vertex colours ->
 * batch by (texture, mode) -> fit the LOD budgets with vertex clustering ->
 * write. Budgets are hard: a car still over budget after the clustering loop
 * fails outright rather than emitting a file the runtime would have to drop.
 *
 * See docs/U2PK.md for what the bytes mean and car_conv.h for the rules.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "car_conv.h"
#include "geom.h"
#include "quant.h"
#include "swizzle.h"
#include "u2pk.h"

/* LOD groups. Wheels are one batch set shared by both LODs, not a LOD. */
enum { G_PLAYER = 0, G_AI = 1, G_WHEEL = 2 };
static const char *G_NAME[3] = { "LOD0", "LOD1", "wheels" };

/* Budgets. A group's byte figure is what it pulls into memory - its geometry
 * plus each texture slot it binds and that slot's 1 KB CLUT. The file cap is
 * what the runtime pays: the 1.5 MB Cars pool has to hold the player's car plus
 * an AI car, each read as a whole file. */
static const int G_TRIS[3] = { 3000, 1000, 400 };
static const long G_KB[3] = { 600, 300, 120 };
static const int G_MAXDIM[3] = { 128, 64, 128 };
#define FILE_KB_MAX 700

#define BATCH_MAXV 65535      /* vertices are indexed by u16, batch-local */
#define BATCH_MAXI 65535      /* indices per draw call */
#define MAX_TEX 512

/* ---- growable byte buffer ------------------------------------------------ */

typedef struct {
	uint8_t *b;
	size_t len, cap;
} Buf;

static int buf_put(Buf *s, const void *p, size_t n)
{
	if (s->len + n > s->cap) {
		size_t cap = s->cap ? s->cap : 8192;
		while (cap < s->len + n)
			cap *= 2;
		uint8_t *b = (uint8_t *)realloc(s->b, cap);
		if (!b)
			return 0;
		s->b = b;
		s->cap = cap;
	}
	if (n)
		memcpy(s->b + s->len, p, n);
	s->len += n;
	return 1;
}

/* Zero-fills up to an absolute length; never shrinks. */
static int buf_pad(Buf *s, size_t to)
{
	static const uint8_t z[64];
	while (s->len < to) {
		size_t k = to - s->len;
		if (k > sizeof z)
			k = sizeof z;
		if (!buf_put(s, z, k))
			return 0;
	}
	return 1;
}

static void buf_free(Buf *s)
{
	free(s->b);
	memset(s, 0, sizeof *s);
}

/* ---- failure text -------------------------------------------------------- */

static char whybuf[256];

static const char *failf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(whybuf, sizeof whybuf, fmt, ap);
	va_end(ap);
	return whybuf;
}

/* ---- built textures ------------------------------------------------------ */

typedef struct {
	uint32_t srckey;
	int maxdim;
	int w, h;              /* padded GE size, what the runtime binds */
	int mode;              /* U2PK_MODE_* implied by the source's alpha */
	uint8_t *data;         /* swizzled T8, w*h */
	size_t size;
	uint32_t clut[256];
	int nclut;
	int slot;              /* index in the written TEXTURES section, -1 = unused */
	int clut_slot;         /* index in the written CLUTS section, -1 = none */
} PTex;

static PTex *ptex;
static int nptex, capptex;

static void ptex_free_all(void)
{
	for (int i = 0; i < nptex; i++)
		free(ptex[i].data);
	free(ptex);
	ptex = 0;
	nptex = capptex = 0;
}

/* Decodes one texture key, downscales it to maxdim, quantises it into a T8 +
 * CLUT pair and swizzles it. Returns its index in ptex[], or -1 with whybuf
 * set. Two entries that come out byte-identical share one slot: LOD1's 64 px
 * variant of a texture that is already 64 px wide is the same bytes. */
static int build_ptex(Car *car, uint32_t key, int maxdim, int force_blend)
{
	for (int i = 0; i < nptex; i++)
		if (ptex[i].srckey == key && ptex[i].maxdim == maxdim)
			return i;

	N2Tex src;
	if (!car_tex(car, key, &src))
		return failf("texture key %08x is in neither the car pack nor GLOBALB",
		             key), -1;
	if (src.w <= 0 || src.h <= 0 || !src.rgb) {
		free(src.rgb);
		free(src.alpha);
		free(src.dxt);
		return failf("texture key %08x decoded to no pixels", key), -1;
	}
	int w = src.w, h = src.h;
	size_t np = (size_t)w * h;
	uint32_t *px = (uint32_t *)malloc(np * sizeof *px);
	if (!px) {
		free(src.rgb);
		free(src.alpha);
		free(src.dxt);
		return failf("out of memory"), -1;
	}
	int has_alpha = src.alpha != 0;
	int binary_alpha = 1, any_hole = 0;
	for (size_t i = 0; i < np; i++) {
		unsigned a = has_alpha ? src.alpha[i] : 255;
		if (a < 128)
			any_hole = 1;
		if (a != 0 && a != 255)
			binary_alpha = 0;
		px[i] = ((uint32_t)a << 24) | ((uint32_t)src.rgb[i * 3] << 16) |
		        ((uint32_t)src.rgb[i * 3 + 1] << 8) | src.rgb[i * 3 + 2];
	}
	free(src.rgb);
	free(src.alpha);
	free(src.dxt);

	/* Draw mode comes from the source's own alpha, decided before any
	 * resampling so that the two LODs of one texture always agree. */
	int mode = U2PK_MODE_OPAQUE;
	if (force_blend)
		mode = U2PK_MODE_BLEND;
	else if (has_alpha && any_hole)
		mode = binary_alpha ? U2PK_MODE_CUTOUT : U2PK_MODE_BLEND;

	if (!img_halve_to(&px, &w, &h, maxdim)) {
		free(px);
		return failf("texture %08x could not be downscaled", key), -1;
	}

	/* Grow to the GE's swizzle grid by edge replication: a pad texel repeats its
	 * row's last real colour, and the batch builder clamps UVs inside the real
	 * area, so a pad texel can never be sampled. */
	int pw = (w + 15) & ~15;
	int ph = (h + 7) & ~7;
	if (pw < 16) pw = 16;
	if (ph < 8) ph = 8;
	uint32_t *pad = (uint32_t *)malloc((size_t)pw * ph * sizeof *pad);
	if (!pad) {
		free(px);
		return failf("out of memory"), -1;
	}
	for (int y = 0; y < ph; y++) {
		int sy = y < h ? y : h - 1;
		for (int x = 0; x < pw; x++) {
			int sx = x < w ? x : w - 1;
			pad[(size_t)y * pw + x] = px[(size_t)sy * w + sx];
		}
	}
	free(px);

	size_t npad = (size_t)pw * ph;
	uint32_t pal[256];
	uint8_t *idx = (uint8_t *)malloc(npad);
	if (!idx) {
		free(pad);
		return failf("out of memory"), -1;
	}
	int n = quant_mediacut(pad, (int)npad, 256, pal, idx);
	if (n <= 0) {
		free(pad);
		free(idx);
		return failf("texture %08x quantised to nothing", key), -1;
	}

	PTex t;
	memset(&t, 0, sizeof t);
	t.srckey = key;
	t.maxdim = maxdim;
	t.w = pw;
	t.h = ph;
	t.mode = mode;
	t.nclut = n;
	t.slot = -1;
	t.clut_slot = -1;
	/* The CLUT alpha is what the GE's alpha test and blending read, so it is set
	 * per mode here rather than left at the box mean: an opaque texture must
	 * never carry a partial alpha, and a cutout must not half-pass a threshold
	 * the source authored as a hole. */
	for (int i = 0; i < n; i++) {
		int a = (int)((pal[i] >> 24) & 0xff);
		if (mode == U2PK_MODE_OPAQUE)
			a = 255;
		else if (mode == U2PK_MODE_CUTOUT)
			a = a >= 128 ? 255 : 0;
		/* pal[] carries 0xAARRGGBB (the order the quantiser and the source
		 * pixels use). The GE reads a GU_PSM_8888 CLUT as 0xAABBGGRR - blue in
		 * bits 16..23, red in the low byte - so swap red and blue on the way
		 * out; runtime/vfmt measured the GE's channel order. */
		uint32_t rgb = pal[i] & 0xffffffu;            /* 0xRRGGBB */
		uint32_t rr = (rgb >> 16) & 0xff, gg = (rgb >> 8) & 0xff, bb = rgb & 0xff;
		t.clut[i] = ((uint32_t)a << 24) | (bb << 16) | (gg << 8) | rr;
	}
	t.size = npad;
	t.data = (uint8_t *)malloc(npad);
	if (!t.data) {
		free(pad);
		free(idx);
		return failf("out of memory"), -1;
	}
	swizzle(t.data, idx, (unsigned)pw, (unsigned)ph);
	free(pad);
	free(idx);

	for (int i = 0; i < nptex; i++) {
		if (ptex[i].w == t.w && ptex[i].h == t.h && ptex[i].size == t.size &&
		    ptex[i].nclut == t.nclut &&
		    !memcmp(ptex[i].data, t.data, t.size) &&
		    !memcmp(ptex[i].clut, t.clut, (size_t)t.nclut * 4)) {
			free(t.data);
			return i;
		}
	}
	if (nptex >= MAX_TEX) {
		free(t.data);
		return failf("the car needs more than %d textures", MAX_TEX), -1;
	}
	if (nptex == capptex) {
		int cap = capptex ? capptex * 2 : 16;
		PTex *p = (PTex *)realloc(ptex, (size_t)cap * sizeof *p);
		if (!p) {
			free(t.data);
			return failf("out of memory"), -1;
		}
		ptex = p;
		capptex = cap;
	}
	ptex[nptex++] = t;
	return nptex - 1;
}

/* ---- parts and batches --------------------------------------------------- */

typedef struct {
	Geo g;                 /* baked geometry, car model space */
	int tex[3];            /* texture slot per LOD group, -1 = untextured */
	uint32_t mode;
	int wheel;
} Part;

typedef struct {
	Geo g;                 /* merged, still floats */
	int tex, wheel;
	uint32_t mode;         /* U2PK_MODE_*, matches U2pkBatch.mode */
	U2pkBatch h;
} Batch;

typedef struct {
	Batch *v;
	int n, cap;
} BatchList;

static void batchlist_free(BatchList *l)
{
	if (!l->v)
		return;
	for (int i = 0; i < l->n; i++)
		geo_free(&l->v[i].g);
	free(l->v);
	l->v = 0;
	l->n = l->cap = 0;
}

/* Tries to fold one part into a batch. On failure (no room, or no memory) the
 * batch is left exactly as it was, so the caller can start a new one. */
static int batch_append(Batch *b, const Geo *g)
{
	int nv = b->g.n + g->n;
	int ni = b->g.nidx + g->nidx;
	if (nv > BATCH_MAXV || ni > BATCH_MAXI)
		return 0;
	float *pos = (float *)malloc((size_t)nv * 3 * sizeof *pos);
	float *uv = (float *)malloc((size_t)nv * 2 * sizeof *uv);
	uint32_t *col = (uint32_t *)malloc((size_t)nv * sizeof *col);
	uint16_t *ix = (uint16_t *)malloc((size_t)ni * sizeof *ix);
	if (!pos || !uv || !col || !ix) {
		free(pos);
		free(uv);
		free(col);
		free(ix);
		return 0;
	}
	memcpy(pos + (size_t)b->g.n * 3, g->pos, (size_t)g->n * 3 * sizeof *pos);
	memcpy(uv + (size_t)b->g.n * 2, g->uv, (size_t)g->n * 2 * sizeof *uv);
	memcpy(col + b->g.n, g->col, (size_t)g->n * sizeof *col);
	/* A fresh batch has NULL backing store; skip the (zero-length) copies of
	 * its "existing" data so we never hand memcpy a null source pointer. */
	if (b->g.n) {
		memcpy(pos, b->g.pos, (size_t)b->g.n * 3 * sizeof *pos);
		memcpy(uv, b->g.uv, (size_t)b->g.n * 2 * sizeof *uv);
		memcpy(col, b->g.col, (size_t)b->g.n * sizeof *col);
	}
	if (b->g.nidx)
		memcpy(ix, b->g.idx, (size_t)b->g.nidx * sizeof *ix);
	for (int i = 0; i < g->nidx; i++)
		ix[b->g.nidx + i] = (uint16_t)(g->idx[i] + b->g.n);
	free(b->g.pos);
	free(b->g.uv);
	free(b->g.col);
	free(b->g.idx);
	b->g.pos = pos;
	b->g.uv = uv;
	b->g.col = col;
	b->g.idx = ix;
	b->g.n = nv;
	b->g.nidx = ni;
	return 1;
}

/* Groups parts by (this LOD group's texture slot, draw mode). Never merges
 * across a texture or a mode, and starts a fresh batch when the mergeable one
 * has no room left. */
static int build_batches(const Part *parts, int nparts, int group, BatchList *out)
{
	memset(out, 0, sizeof *out);
	for (int i = 0; i < nparts; i++) {
		const Part *p = &parts[i];
		int tex = p->tex[group];
		int hit = -1;
		for (int b = 0; b < out->n; b++)
			if (out->v[b].tex == tex && out->v[b].mode == p->mode) {
				hit = b;
				break;
			}
		if (hit >= 0 && batch_append(&out->v[hit], &p->g))
			continue;
		Batch fresh;
		memset(&fresh, 0, sizeof fresh);
		fresh.tex = tex;
		fresh.mode = p->mode;
		fresh.wheel = p->wheel;
		if (!batch_append(&fresh, &p->g)) {
			geo_free(&fresh.g);
			batchlist_free(out);
			return -1;
		}
		if (out->n == out->cap) {
			int cap = out->cap ? out->cap * 2 : 16;
			Batch *v = (Batch *)realloc(out->v, (size_t)cap * sizeof *v);
			if (!v) {
				geo_free(&fresh.g);
				batchlist_free(out);
				return -1;
			}
			out->v = v;
			out->cap = cap;
		}
		out->v[out->n++] = fresh;
	}
	return out->n ? 0 : -1;
}

/* Clamps every UV into the padded texture's texel centres. Car art is an atlas
 * in [0,1] and the desktop build clamps it for the same reason (main.c:4906);
 * with REPEAT wrap an unclamped u of exactly 1.0 samples texel 0 and bleeds the
 * opposite border along every panel edge. */
static void batch_clamp_uv(Batch *b)
{
	if (b->tex < 0)
		return;
	const PTex *t = &ptex[b->tex];
	float lu = 0.5f / (float)t->w, hu = 1.0f - 0.5f / (float)t->w;
	float lv = 0.5f / (float)t->h, hv = 1.0f - 0.5f / (float)t->h;
	for (int v = 0; v < b->g.n; v++) {
		float *u = b->g.uv + (size_t)v * 2;
		if (!isfinite(u[0]) || u[0] < lu) u[0] = lu;
		if (u[0] > hu) u[0] = hu;
		if (!isfinite(u[1]) || u[1] < lv) u[1] = lv;
		if (u[1] > hv) u[1] = hv;
	}
}

/* Quantises one batch into the VERTS and INDICES buffers and fills its header. */
static int batch_encode(Batch *b, Buf *verts, Buf *ixs)
{
	batch_clamp_uv(b);

	float lo[3], hi[3];
	geo_bbox(&b->g, lo, hi);
	U2pkBatch *h = &b->h;
	memset(h, 0, sizeof *h);
	for (int a = 0; a < 3; a++) {
		h->pos_offset[a] = 0.5f * (lo[a] + hi[a]);
		float e = 0.5f * (hi[a] - lo[a]);
		h->pos_scale[a] = e > 0.001f ? e : 0.001f;
	}
	float ulo[2] = { 1e30f, 1e30f }, uhi[2] = { -1e30f, -1e30f };
	for (int v = 0; v < b->g.n; v++)
		for (int a = 0; a < 2; a++) {
			float u = b->g.uv[(size_t)v * 2 + a];
			if (u < ulo[a]) ulo[a] = u;
			if (u > uhi[a]) uhi[a] = u;
		}
	for (int a = 0; a < 2; a++) {
		if (!(ulo[a] <= uhi[a])) {   /* untextured: the uv fields are inert */
			h->uv_offset[a] = 0.0f;
			h->uv_scale[a] = 1.0f;
			continue;
		}
		float off = floorf(ulo[a]);
		float range = uhi[a] - off;
		h->uv_offset[a] = off;
		h->uv_scale[a] = range <= 1.99f ? 1.0f : range / 1.99f;
	}

	h->first_vertex = (uint32_t)(verts->len / sizeof(U2pkVertex));
	h->vertex_count = (uint32_t)b->g.n;
	h->first_index = (uint32_t)(ixs->len / sizeof(uint16_t));
	h->index_count = (uint32_t)b->g.nidx;
	h->texture = b->tex;
	h->mode = b->mode;

	for (int v = 0; v < b->g.n; v++) {
		U2pkVertex x;
		const float *p = b->g.pos + (size_t)v * 3;
		const float *u = b->g.uv + (size_t)v * 2;
		x.u = q16_uv(u[0] - h->uv_offset[0], h->uv_scale[0]);
		x.v = q16_uv(u[1] - h->uv_offset[1], h->uv_scale[1]);
		x.color = u2pk_color5650(b->g.col[v]);
		x.x = q16_pos(p[0] - h->pos_offset[0], h->pos_scale[0]);
		x.y = q16_pos(p[1] - h->pos_offset[1], h->pos_scale[1]);
		x.z = q16_pos(p[2] - h->pos_offset[2], h->pos_scale[2]);
		if (!buf_put(verts, &x, sizeof x))
			return failf("out of memory"), 0;
	}
	/* Pad the run to 4 vertices (48 bytes) so the next batch's first_vertex * 12
	 * is still a multiple of 16. first_vertex is itself a multiple of 4 by
	 * induction on this same rule, starting from 0. */
	if (!buf_pad(verts, ((size_t)h->first_vertex + (size_t)((b->g.n + 3) & ~3)) *
	                        sizeof(U2pkVertex)))
		return failf("out of memory"), 0;

	if (!buf_put(ixs, b->g.idx, (size_t)b->g.nidx * sizeof(uint16_t)))
		return failf("out of memory"), 0;
	if (!buf_pad(ixs, ((size_t)h->first_index + (size_t)((b->g.nidx + 7) & ~7)) *
	                          sizeof(uint16_t)))
		return failf("out of memory"), 0;

	/* 12-byte vertices and 2-byte indices only stay 16-byte aligned because of
	 * the run padding above. If that ever broke, the real GE would read a
	 * garbage car, so check it here on the host instead. */
	if ((h->first_vertex * 12u) % U2PK_ALIGN || (h->first_index * 2u) % U2PK_ALIGN)
		return failf("internal: a batch lost its run alignment (v %u i %u)",
		             h->first_vertex, h->first_index), 0;
	return 1;
}

/* ---- grouping and budgeting --------------------------------------------- */

/* Bytes this group pulls into memory: its geometry plus each texture slot it
 * binds and that slot's 1 KB CLUT. A slot shared with another group is charged
 * to each of them, because that is what a per-group budget means. */
static long group_bytes(const BatchList *l)
{
	long b = 0;
	int seen[MAX_TEX];
	int nseen = 0;
	for (int i = 0; i < l->n; i++) {
		b += (long)l->v[i].g.n * (long)sizeof(U2pkVertex) +
		     (long)l->v[i].g.nidx * (long)sizeof(uint16_t);
		int t = l->v[i].tex;
		if (t < 0)
			continue;
		int dup = 0;
		for (int k = 0; k < nseen; k++)
			if (seen[k] == t)
				dup = 1;
		if (dup)
			continue;
		seen[nseen++] = t;
		b += (long)ptex[t].size + 1024;
	}
	return b;
}

static void partlist_free(Part *p, int n)
{
	if (!p)
		return;
	for (int i = 0; i < n; i++)
		geo_free(&p[i].g);
	free(p);
}

/* Clusters every batch of a freshly built group as a whole, so two meshes that
 * meet at a seam collapse their shared vertices onto one grid point. Clustering
 * the merged batch - not each mesh on its own - is what the brief's cluster key
 * (which contains the batch id) asks for, and it is what removes the structural
 * triangles that survive welding each mesh in isolation. A batch that collapses
 * to nothing is dropped. On success *out owns fresh batches and *tris is their
 * triangle total; on failure *out is left empty and whybuf is set. */
static int cluster_group(BatchList *out, const BatchList *base, float cell,
                         int *tris)
{
	memset(out, 0, sizeof *out);
	*tris = 0;
	for (int i = 0; i < base->n; i++) {
		const Batch *b = &base->v[i];
		Geo cg;
		if (!geo_cluster(&cg, &b->g, cell))
			continue;                 /* the whole batch collapsed away */
		if (out->n == out->cap) {
			int cap = out->cap ? out->cap * 2 : 16;
			Batch *v = (Batch *)realloc(out->v, (size_t)cap * sizeof *v);
			if (!v) {
				geo_free(&cg);
				batchlist_free(out);
				failf("out of memory");
				return -1;
			}
			out->v = v;
			out->cap = cap;
		}
		Batch nb;
		memset(&nb, 0, sizeof nb);
		nb.g = cg;
		nb.tex = b->tex;
		nb.mode = b->mode;
		nb.wheel = b->wheel;
		out->v[out->n++] = nb;
		*tris += cg.nidx / 3;
	}
	return 0;
}

/* Fits one group. First builds the merged batches from the untouched baked
 * geometry and takes them as-is when they already fit; otherwise it clusters the
 * merged batches from a 0.02 m cell upward by 1.5x, always from the original
 * merge, until they fit or the cell passes 0.5 m. Returns 1 with *out owning the
 * batches, 0 with whybuf set. */
static int fit_group(const Part *src, int nsrc, int group, BatchList *out,
                     int *tris_after, long *bytes_after)
{
	BatchList base;
	if (build_batches(src, nsrc, group, &base) < 0) {
		failf("the %s batch build failed (a part is larger than one batch)",
		      G_NAME[group]);
		return 0;
	}
	int tris = 0;
	for (int i = 0; i < base.n; i++)
		tris += base.v[i].g.nidx / 3;
	long bytes = group_bytes(&base);
	int dbg = getenv("U2PK_FITDBG") != 0;
	if (dbg)
		fprintf(stderr, "[fit %s] merge=%d tris %ld KB budget %d/%ld KB\n",
		        G_NAME[group], tris, bytes / 1024, G_TRIS[group], G_KB[group]);
	if (tris <= G_TRIS[group] && bytes <= G_KB[group] * 1024L) {
		*out = base;
		*tris_after = tris;
		*bytes_after = bytes;
		return 1;
	}
	int last_tris = tris;
	float cell = 0.0f;
	for (;;) {
		cell = cell > 0.0f ? cell * 1.5f : 0.02f;
		int final = cell > 0.5f;      /* 0.5 m is the last cell we are allowed */
		if (final)
			cell = 0.5f;
		BatchList bl;
		if (cluster_group(&bl, &base, cell, &tris) < 0) {
			batchlist_free(&base);
			return 0;
		}
		bytes = group_bytes(&bl);
		last_tris = tris;
		if (dbg)
			fprintf(stderr, "[fit %s] cell=%.4f -> %d tris %ld KB\n",
			        G_NAME[group], cell, tris, bytes / 1024);
		if (tris <= G_TRIS[group] && bytes <= G_KB[group] * 1024L) {
			batchlist_free(&base);
			*out = bl;
			*tris_after = tris;
			*bytes_after = bytes;
			return 1;
		}
		batchlist_free(&bl);
		if (final) {
			failf("%s is %d triangles / %ld KB and does not fit %d / %ld KB even "
			      "at a 0.5 m cluster cell", G_NAME[group], last_tris,
			      bytes / 1024, G_TRIS[group], G_KB[group]);
			batchlist_free(&base);
			return 0;
		}
	}
}

/* ---- entry point --------------------------------------------------------- */

int car_convert(Car *car, const char *outpath, CarReport *rep)
{
	const char *why = "no reason recorded";
	memset(rep, 0, sizeof *rep);
	snprintf(rep->name, sizeof rep->name, "%.31s", car->name);
	rep->wheel_from_global = car->wheel_from_global;
	rep->wheel_radius = car->wheel_radius;
	memcpy(rep->body_box, car->prof.body, sizeof rep->body_box);
	ptex_free_all();

	BatchList bl[3];
	Part *parts = 0;
	int nparts = 0, capnparts = 0;
	int tris0[3] = { 0, 0, 0 };
	int nskipped = 0;
	uint32_t *clutdata = 0;
	int ncluts = 0;
	int slot_of[MAX_TEX];               /* ptex index -> written slot, -1 unused */
	int slot_tex[MAX_TEX];              /* written slot -> ptex index */
	Buf batches = {0}, verts = {0}, ixs = {0}, file = {0};
	memset(bl, 0, sizeof bl);
	for (int i = 0; i < MAX_TEX; i++) {
		slot_of[i] = -1;
		slot_tex[i] = -1;
	}

	/* ---- step 2: choose meshes ----
	 * Body: everything that is not wheel-mounted and not the interior tub.
	 * Wheels: the stock tier only - n2_car_prepare_wheels oriented every wheel
	 * slice already, and the other tiers are other sizes we are not drawing. */
	for (int i = 0; i < car->scene.count; i++) {
		N2Mesh *m = &car->scene.meshes[i];
		int wheel = m->car_mount == N2_MOUNT_WHEEL;
		if (wheel && (car->stock_tier == 0 || m->tierid != car->stock_tier)) {
			nskipped++;
			continue;
		}
		if (!wheel && m->cat == N2_CAR_INTERIOR) {
			nskipped++;
			continue;
		}
		if (!m->verts || !m->idx || m->nverts <= 0 || m->nidx <= 0 || m->nidx % 3) {
			nskipped++;
			continue;
		}

		Bake bake;
		bake.base = m->texkey ? 0xffffffu : 0x808080u;
		bake.prelight = m->vcol != 0;
		/* The car skin is the paint layer: near-white decal art that the
		 * desktop build tints with the car's colour. Red is the fleet
		 * default and v1 has no per-car paint, so it is baked in. NFSU2
		 * stores the skin two ways - a textured decal sheet, or (the common
		 * case, e.g. MIATA) an untextured solid mesh whose colour IS the
		 * paint - so this test must run for both, not only under texkey. */
		if (m->cat == N2_CAR_BODY && m->car_material == N2_MAT_CARSKIN)
			bake.base = 0xc82828u;        /* 200, 40, 40 */
		int big = -1;
		uint32_t mode = U2PK_MODE_OPAQUE;
		if (m->texkey) {
			big = build_ptex(car, m->texkey, G_MAXDIM[G_PLAYER],
			                 m->cat == N2_CAR_GLASS);
			if (big < 0) {
				why = whybuf;
				goto done;
			}
			mode = (uint32_t)ptex[big].mode;
		}

		Part p;
		memset(&p, 0, sizeof p);
		if (!geo_from_mesh(&p.g, m, &bake, &why)) {
			if (!why)
				why = failf("a mesh could not be baked");
			goto done;
		}
		p.tex[G_PLAYER] = big;
		p.tex[G_WHEEL] = big;
		p.mode = mode;
		p.wheel = wheel;
		if (nparts == capnparts) {
			int cap = capnparts ? capnparts * 2 : 32;
			Part *q = (Part *)realloc(parts, (size_t)cap * sizeof *q);
			if (!q) {
				geo_free(&p.g);
				why = failf("out of memory");
				goto done;
			}
			parts = q;
			capnparts = cap;
		}
		parts[nparts++] = p;
		tris0[wheel ? G_WHEEL : G_PLAYER] += p.g.nidx / 3;
	}
	if (!nparts) {
		why = "the car has no drawable meshes";
		goto done;
	}
	rep->skipped = nskipped;
	rep->tris_before[G_PLAYER] = tris0[G_PLAYER];
	rep->tris_before[G_AI] = tris0[G_PLAYER];
	rep->tris_before[G_WHEEL] = tris0[G_WHEEL];

	/* LOD1's smaller texture variants, for the keys LOD1 actually draws. */
	for (int i = 0; i < nparts; i++) {
		if (parts[i].wheel || parts[i].tex[G_PLAYER] < 0)
			continue;
		uint32_t key = ptex[parts[i].tex[G_PLAYER]].srckey;
		int s = build_ptex(car, key, G_MAXDIM[G_AI],
		                   ptex[parts[i].tex[G_PLAYER]].mode == U2PK_MODE_BLEND);
		if (s < 0) {
			why = whybuf;
			goto done;
		}
		parts[i].tex[G_AI] = s;
	}

	/* ---- step 6: fit each group to its budget ---- */
	for (int g = 0; g < 3; g++) {
		int want = g == G_WHEEL;
		int n = 0;
		for (int i = 0; i < nparts; i++)
			if (!!parts[i].wheel == want)
				n++;
		if (!n) {
			if (g != G_WHEEL) {
				why = "the car has no body meshes";
				goto done;
			}
			continue;                 /* an authored-wheel-free car is legal */
		}
		Part *sv = (Part *)calloc((size_t)n, sizeof *sv);
		if (!sv) {
			why = failf("out of memory");
			goto done;
		}
		int k = 0;
		for (int i = 0; i < nparts; i++)
			if (!!parts[i].wheel == want)
				sv[k++] = parts[i];   /* shallow: fit_group copies */
		long bytes = 0;
		int okfit = 0;
		/* fit_group needs a real array; the shallow copies share Geos with
		 * parts[], which it never frees. */
		okfit = fit_group(sv, n, g, &bl[g], &rep->tris_after[g], &bytes);
		free(sv);
		if (!okfit) {
			why = whybuf;
			goto done;
		}
		rep->bytes[g] = bytes;
	}

	/* ---- step 7: write ---- */
	int nslot = 0;
	for (int g = 0; g < 3; g++)
		for (int i = 0; i < bl[g].n; i++) {
			int t = bl[g].v[i].tex;
			if (t >= 0 && slot_of[t] < 0) {
				slot_of[t] = nslot;
				slot_tex[nslot++] = t;
			}
		}

	/* CLUTs, deduped by content. */
	for (int t = 0; t < nptex; t++) {
		if (slot_of[t] < 0)
			continue;
		int dup = -1;
		for (int c = 0; c < ncluts; c++)
			if (!memcmp(clutdata + (size_t)c * 256, ptex[t].clut,
			            (size_t)ptex[t].nclut * 4)) {
				dup = c;
				break;
			}
		if (dup >= 0) {
			ptex[t].clut_slot = dup;
			continue;
		}
		uint32_t *c = (uint32_t *)realloc(clutdata,
		                                  (size_t)(ncluts + 1) * 256 * 4);
		if (!c) {
			why = failf("out of memory");
			goto done;
		}
		clutdata = c;
		memset(clutdata + (size_t)ncluts * 256, 0, 1024);
		memcpy(clutdata + (size_t)ncluts * 256, ptex[t].clut,
		       (size_t)ptex[t].nclut * 4);
		ptex[t].clut_slot = ncluts++;
	}

	for (int g = 0; g < 3; g++)
		for (int i = 0; i < bl[g].n; i++) {
			if (bl[g].v[i].tex >= 0)
				bl[g].v[i].tex = slot_of[bl[g].v[i].tex];
			if (!batch_encode(&bl[g].v[i], &verts, &ixs)) {
				why = whybuf;
				goto done;
			}
			if (!buf_put(&batches, &bl[g].v[i].h, sizeof(U2pkBatch))) {
				why = failf("out of memory");
				goto done;
			}
		}

	rep->nbatch[G_PLAYER] = bl[G_PLAYER].n;
	rep->nbatch[G_AI] = bl[G_AI].n;
	rep->nbatch[G_WHEEL] = bl[G_WHEEL].n;
	rep->nbatch_total = rep->nbatch[0] + rep->nbatch[1] + rep->nbatch[2];
	rep->ntex = nslot;
	rep->nclut = ncluts;
	rep->tex_bytes = 0;
	for (int t = 0; t < nptex; t++)
		if (slot_of[t] >= 0)
			rep->tex_bytes += (long)ptex[t].size + 1024;

	/* File layout: header, table, then six 16-byte-aligned sections; texture
	 * data lives inside the TEXTURES section, after its headers. */
	{
		long pos = (long)sizeof(U2pkHeader) + 16L * 6;
		long off[6];
		long data_at[MAX_TEX];
		off[0] = pos;
		pos += (long)batches.len;
		pos = (pos + 15) & ~15L;
		off[1] = pos;
		pos += (long)verts.len;
		pos = (pos + 15) & ~15L;
		off[2] = pos;
		pos += (long)ixs.len;
		pos = (pos + 15) & ~15L;
		off[3] = pos;
		pos += (long)nslot * (long)sizeof(U2pkTexHeader);
		for (int s = 0; s < nslot; s++) {
			pos = (pos + 15) & ~15L;
			data_at[s] = pos;
			pos += (long)ptex[slot_tex[s]].size;
		}
		pos = (pos + 15) & ~15L;
		off[4] = pos;
		pos += (long)ncluts * 1024;
		pos = (pos + 15) & ~15L;
		off[5] = pos;
		pos += (long)sizeof(U2pkCarInfo);

		if (pos > FILE_KB_MAX * 1024L) {
			why = failf("the file would be %ld KB, over the %d KB the 1.5 MB Cars "
			            "pool allows for two cars", pos / 1024, FILE_KB_MAX);
			goto done;
		}

		U2pkCarInfo info;
		memset(&info, 0, sizeof info);
		info.lod_count = U2PK_LODS;
		info.batch_first[0] = 0;
		info.batch_count[0] = (uint32_t)bl[G_PLAYER].n;
		info.batch_first[1] = (uint32_t)bl[G_PLAYER].n;
		info.batch_count[1] = (uint32_t)bl[G_AI].n;
		info.wheel_batch_first = (uint32_t)(bl[G_PLAYER].n + bl[G_AI].n);
		info.wheel_batch_count = (uint32_t)bl[G_WHEEL].n;
		memcpy(info.wheel_pos, car->hub, sizeof info.wheel_pos);
		info.wheel_radius = car->wheel_radius;
		memcpy(info.body_box, car->prof.body, sizeof info.body_box);

		U2pkHeader hdr;
		memset(&hdr, 0, sizeof hdr);
		memcpy(hdr.magic, U2PK_MAGIC, 4);
		hdr.version = U2PK_VERSION;
		hdr.kind = U2PK_KIND_CAR;
		hdr.section_count = 6;
		hdr.total_size = (uint32_t)pos;
		U2pkSection sect[6] = {
			{ U2PK_S_BATCHES, (uint32_t)off[0], (uint32_t)batches.len,
			  (uint32_t)rep->nbatch_total },
			{ U2PK_S_VERTS, (uint32_t)off[1], (uint32_t)verts.len,
			  (uint32_t)(verts.len / sizeof(U2pkVertex)) },
			{ U2PK_S_INDICES, (uint32_t)off[2], (uint32_t)ixs.len,
			  (uint32_t)(ixs.len / sizeof(uint16_t)) },
			{ U2PK_S_TEXTURES, (uint32_t)off[3],
			  (uint32_t)nslot * (uint32_t)sizeof(U2pkTexHeader),
			  (uint32_t)nslot },
			{ U2PK_S_CLUTS, (uint32_t)off[4], (uint32_t)ncluts * 1024,
			  (uint32_t)ncluts },
			{ U2PK_S_CARINFO, (uint32_t)off[5], (uint32_t)sizeof info, 1 }
		};
		int wr = buf_put(&file, &hdr, sizeof hdr) &&
		         buf_put(&file, sect, sizeof sect);
		wr = wr && buf_pad(&file, (size_t)off[0]) &&
		     buf_put(&file, batches.b, batches.len) &&
		     buf_pad(&file, (size_t)off[1]) &&
		     buf_put(&file, verts.b, verts.len) &&
		     buf_pad(&file, (size_t)off[2]) &&
		     buf_put(&file, ixs.b, ixs.len) &&
		     buf_pad(&file, (size_t)off[3]);
		for (int s = 0; s < nslot && wr; s++) {
			int t = slot_tex[s];
			U2pkTexHeader th;
			memset(&th, 0, sizeof th);
			th.w = (uint16_t)ptex[t].w;
			th.h = (uint16_t)ptex[t].h;
			th.psm = U2PK_PSM_T8;
			th.swizzled = 1;
			th.data_size = (uint32_t)ptex[t].size;
			th.data_offset = (uint32_t)data_at[s];
			th.clut = ptex[t].clut_slot;
			wr = buf_put(&file, &th, sizeof th);
		}
		for (int s = 0; s < nslot && wr; s++) {
			int t = slot_tex[s];
			wr = buf_pad(&file, (size_t)data_at[s]) &&
			     buf_put(&file, ptex[t].data, ptex[t].size);
		}
		wr = wr && buf_pad(&file, (size_t)off[4]) &&
		     buf_put(&file, clutdata, (size_t)ncluts * 1024) &&
		     buf_pad(&file, (size_t)off[5]) &&
		     buf_put(&file, &info, sizeof info);
		if (!wr) {
			why = failf("out of memory");
			goto done;
		}
		if ((long)file.len != pos) {
			why = failf("internal: laid out %ld bytes and wrote %lu", pos,
			            (unsigned long)file.len);
			goto done;
		}
		rep->total = (long)pos;
	}

	{
		FILE *f = fopen(outpath, "wb");
		if (!f) {
			why = "cannot open the output file for writing";
			goto done;
		}
		int wr = fwrite(file.b, 1, file.len, f) == file.len;
		if (fclose(f))
			wr = 0;
		if (!wr) {
			why = "writing the file failed";
			goto done;
		}
	}
	rep->ok = 1;

done:
	if (!rep->ok && why)
		snprintf(rep->why, sizeof rep->why, "%s", why);
	for (int g = 0; g < 3; g++)
		batchlist_free(&bl[g]);
	partlist_free(parts, nparts);
	free(clutdata);
	buf_free(&batches);
	buf_free(&verts);
	buf_free(&ixs);
	buf_free(&file);
	ptex_free_all();
	return rep->ok;
}
