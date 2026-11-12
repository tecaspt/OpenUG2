/* See world.h. M4c: draw a static world slice.
 *
 * Memory layout in POOL_WORLD: the region index (index.u2pk, ~KB) is parked at
 * the base and its end recorded as w->world_floor. Each window reload resets
 * POOL_WORLD->used back to that floor and bump-allocates the 3x3 cell blobs and
 * their float-cut vertices on top, so the index survives a reload while the
 * transient cell data does not - the whole world stays in one pool (D8: no new
 * pools). tex.u2pk lives in POOL_TEXTURES and is shared by every cell batch.
 *
 * Only the 3x3 window around the pose is resident: cells.u2pk is seeked to each
 * cell's CELLTAB offset and just that cell's bytes are read, which is why a
 * region far larger than the pool can be streamed.
 *
 * Vertices are re-cut from the file's s16 format into GE float vertices exactly
 * as car.c does (a real PSP-1000 reads GU_VERTEX_16BIT differently from PPSSPP;
 * float is unambiguous on both). See car.c's header comment for the full why. */
#include <pspuser.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "world.h"
#include "platform.h"

#define VERTFMT (GU_TEXTURE_16BIT  | GU_COLOR_8888 | GU_VERTEX_32BITF | \
                 GU_INDEX_16BIT    | GU_TRANSFORM_3D)

/* Interleaved GE vertex: uv (u16,u16), colour (u32 8888), pos (float x,y,z) =
 * 20 B. A 32-bit position must land on a 4-byte boundary, so GU_COLOR_8888 (4B
 * after the 4B uv) keeps the float at offset 8 for every vertex; GU_COLOR_5650
 * would leave it misaligned at offset 6. Identical to car.c. */
#define WORLD_VSTRIDE 20

/* File stores colour as GU_COLOR_5650 (R in the low bits); expand to the GE's
 * 8888 order (also R-in-low, 0xAABBGGRR) so MODULATE matches. See car.c. */
static uint32_t rgb565_to_8888(uint16_t c)
{
	unsigned r = (c & 0x1f) * 255 / 31;
	unsigned g = ((c >> 5) & 0x3f) * 255 / 63;
	unsigned b = ((c >> 11) & 0x1f) * 255 / 31;
	return 0xff000000u | (b << 16) | (g << 8) | r;
}

/* Read a whole file into a fresh 16-byte-aligned pool block. Returns the block
 * (and *out_len) or NULL. */
static uint8_t *slurp(Pool *pool, const char *path, uint32_t *out_len)
{
	int fd, sz, r;
	uint8_t *b;

	fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (fd < 0) {
		plat_log("world: cannot open %s", path);
		return 0;
	}
	sz = sceIoLseek(fd, 0, PSP_SEEK_END);
	sceIoLseek(fd, 0, PSP_SEEK_SET);
	if (sz <= 0) {
		sceIoClose(fd);
		plat_log("world: %s is empty", path);
		return 0;
	}
	b = (uint8_t *)pool_alloc(pool, (size_t)sz, 16);
	if (!b) {
		sceIoClose(fd);
		plat_log("world: %s pool cannot take %d bytes", pool->name, sz);
		return 0;
	}
	r = sceIoRead(fd, b, (unsigned int)sz);
	sceIoClose(fd);
	if (r != sz) {
		plat_log("world: short read %d of %d (%s)", r, sz, path);
		return 0;
	}
	*out_len = (uint32_t)sz;
	return b;
}

/* CELLTAB entry accessor: the tab is ncell x 24B of
 * {i32 cx, i32 cy, u32 off, u32 size, f32 zmin, f32 zmax}, one row per grid cell
 * in index order c = cy*gw + cx. */
static void celltab_at(const World *w, int c, int *off, int *size)
{
	const uint8_t *e = w->celltab + (size_t)c * 24;
	int32_t o, s;
	memcpy(&o, e + 8, 4);
	memcpy(&s, e + 12, 4);
	*off = o;
	*size = s;
}

int world_open(World *w, Pool *pworld, Pool *ptex, const char *region_dir)
{
	char path[300];
	char rel[300];
	uint32_t s;
	uint16_t gw, gh;
	float x0, y0;

	memset(w, 0, sizeof *w);
	w->pw = pworld;
	w->pt = ptex;

	/* index.u2pk into the base of POOL_WORLD */
	snprintf(rel, sizeof rel, "%s/index.u2pk", region_dir);
	plat_path(path, (int)sizeof(path), rel);
	w->idxbuf = slurp(pworld, path, &w->idxlen);
	if (!w->idxbuf)
		return 0;
	sceKernelDcacheWritebackAll();
	if (!u2pk_open(w->idxbuf, w->idxlen, &w->iv)) {
		plat_log("world: index: %s", w->iv.err ? w->iv.err : "?");
		return 0;
	}
	if (w->iv.hdr.kind != U2PK_KIND_REGION) {
		plat_log("world: %s is not a region index (kind=%u)", rel,
		         (unsigned)w->iv.hdr.kind);
		return 0;
	}
	memcpy(&gw, w->iv.hdr.reserved + 0, 2);
	memcpy(&gh, w->iv.hdr.reserved + 2, 2);
	memcpy(&x0, w->iv.hdr.reserved + 4, 4);
	memcpy(&y0, w->iv.hdr.reserved + 8, 4);
	w->gw = gw;
	w->gh = gh;
	w->x0 = x0;
	w->y0 = y0;
	for (s = 0; s < w->iv.hdr.section_count; s++) {
		if (w->iv.sect[s].type == U2PK_S_CELLTAB) {
			w->celltab = w->iv.buf + w->iv.sect[s].offset;
			w->ncell = (int)w->iv.sect[s].count;
			break;
		}
	}
	if (!w->celltab || !w->ncell) {
		plat_log("world: index has no CELLTAB");
		return 0;
	}
	/* Mark the world pool just past the index: reloads reset to here, not 0. */
	w->world_floor = pworld->used;

	/* tex.u2pk into POOL_TEXTURES (shared by all cell batches) */
	snprintf(rel, sizeof rel, "%s/tex.u2pk", region_dir);
	plat_path(path, (int)sizeof(path), rel);
	pool_reset(ptex);
	w->texbuf = slurp(ptex, path, &w->texlen);
	if (!w->texbuf)
		return 0;
	sceKernelDcacheWritebackAll();
	if (!u2pk_open(w->texbuf, w->texlen, &w->tv)) {
		plat_log("world: tex: %s", w->tv.err ? w->tv.err : "?");
		return 0;
	}

	snprintf(w->cells_path, sizeof w->cells_path, "%s", path);
	{
		char *slash = strrchr(w->cells_path, '/');
		if (slash)
			strcpy(slash + 1, "cells.u2pk");
	}

	plat_log("world: %s grid=%dx%d x0=%.1f y0=%.1f ncell=%d ntex=%u floor=%u",
	         region_dir, w->gw, w->gh, w->x0, w->y0, w->ncell,
	         (unsigned)w->tv.ntex, (unsigned)w->world_floor);
	return 1;
}

int world_cell_at(const World *w, float x, float y)
{
	int cx, cy;
	if (!w->gw || !w->gh)
		return -1;
	cx = (int)floorf((x - w->x0) / U2PK_CELL_M);
	cy = (int)floorf((y - w->y0) / U2PK_CELL_M);
	if (cx < 0 || cy < 0 || cx >= w->gw || cy >= w->gh)
		return -1;
	return cy * w->gw + cx;
}

/* Re-cut one cell's s16 vertices into 20-byte GE float vertices (POOL_WORLD). */
static int build_cell_verts(Pool *pool, WorldCell *cell)
{
	uint32_t n = cell->v.nvert;
	const U2pkVertex *src = cell->v.vert;
	uint8_t *d;
	uint32_t i;

	d = (uint8_t *)pool_alloc(pool, (size_t)n * WORLD_VSTRIDE, 16);
	if (!d) {
		plat_log("world: cell %d,%d cannot take %u float verts", cell->cx,
		         cell->cy, (unsigned)n);
		return 0;
	}
	for (i = 0; i < n; i++) {
		uint8_t *o = d + (size_t)i * WORLD_VSTRIDE;
		uint32_t col = rgb565_to_8888(src[i].color);
		memcpy(o, &src[i].u, 4);
		memcpy(o + 4, &col, 4);
		memcpy(o + 8,  &(float){ (float)src[i].x / 32768.0f }, 4);
		memcpy(o + 12, &(float){ (float)src[i].y / 32768.0f }, 4);
		memcpy(o + 16, &(float){ (float)src[i].z / 32768.0f }, 4);
	}
	cell->vertf = d;
	return 1;
}

int world_load_window(World *w, float x, float y)
{
	int ccx, ccy, dx, dy, fd, n = 0;
	uint64_t t0 = plat_us();

	ccx = world_cell_at(w, x, y);
	if (ccx < 0)
		return 0;
	ccy = ccx / w->gw;
	ccx = ccx % w->gw;

	/* Drop the previous window but keep the resident index. */
	w->pw->used = w->world_floor;
	w->nwin = 0;
	w->have_win = 0;

	fd = sceIoOpen(w->cells_path, PSP_O_RDONLY, 0777);
	if (fd < 0) {
		plat_log("world: cannot open %s", w->cells_path);
		return 0;
	}
	for (dy = -1; dy <= 1; dy++) {
		for (dx = -1; dx <= 1; dx++) {
			int nx = ccx + dx, ny = ccy + dy, c, off, size;
			WorldCell *cell;

			if (nx < 0 || ny < 0 || nx >= w->gw || ny >= w->gh)
				continue;
			c = ny * w->gw + nx;
			celltab_at(w, c, &off, &size);
			if (size <= 0)
				continue;               /* empty cell: nothing emitted */
			if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) {
				plat_log("world: seek %d failed", off);
				continue;
			}
			cell = &w->win[n];
			memset(cell, 0, sizeof *cell);
			cell->cx = nx;
			cell->cy = ny;
			cell->len = (uint32_t)size;
			cell->blob = (uint8_t *)pool_alloc(w->pw, (size_t)size, 16);
			if (!cell->blob) {
				plat_log("world: world pool cannot take cell %d,%d (%d B)",
				         nx, ny, size);
				continue;
			}
			if (sceIoRead(fd, cell->blob, (unsigned int)size) != size) {
				plat_log("world: short read cell %d,%d", nx, ny);
				continue;
			}
			sceKernelDcacheWritebackAll();
			if (!u2pk_open(cell->blob, cell->len, &cell->v)) {
				plat_log("world: cell %d,%d: %s", nx, ny,
				         cell->v.err ? cell->v.err : "?");
				continue;
			}
			if (cell->v.hdr.kind != U2PK_KIND_CELL) {
				plat_log("world: cell %d,%d wrong kind %u", nx, ny,
				         (unsigned)cell->v.hdr.kind);
				continue;
			}
			if (!build_cell_verts(w->pw, cell))
				continue;
			n++;
		}
	}
	sceIoClose(fd);
	/* The GE reads the freshly-cut float vertices straight out of RAM. */
	sceKernelDcacheWritebackAll();

	w->nwin = n;
	w->win_cx = ccx;
	w->win_cy = ccy;
	w->have_win = (n > 0);
	plat_log("world: LOAD cx=%d cy=%d cells=%d/%d used=%u LOAD us=%llu",
	         ccx, ccy, n, WORLD_WIN * WORLD_WIN, (unsigned)w->pw->used,
	         (unsigned long long)(plat_us() - t0));
	return n > 0;
}

static void setup_texture(const World *w, const U2pkBatch *b)
{
	const U2pkTexHeader *t;
	const uint8_t *data;

	if (b->texture < 0 || (uint32_t)b->texture >= w->tv.ntex) {
		sceGuDisable(GU_TEXTURE_2D);
		return;
	}
	t = &w->tv.tex[b->texture];
	data = w->tv.buf + t->data_offset;
	sceGuEnable(GU_TEXTURE_2D);
	if (t->psm == U2PK_PSM_T8 && t->clut >= 0 && (uint32_t)t->clut < w->tv.nclut) {
		sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
		sceGuClutLoad(256 / 8, w->tv.clut + (size_t)t->clut * 256);
	}
	sceGuTexMode(t->psm, 0, 0, t->swizzled);
	sceGuTexImage(0, t->w, t->h, t->w, data);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuTexScale(b->uv_scale[0], b->uv_scale[1]);
	sceGuTexOffset(b->uv_offset[0], b->uv_offset[1]);
}

static void apply_mode(uint32_t mode)
{
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
	if (mode == U2PK_MODE_CUTOUT) {
		sceGuEnable(GU_ALPHA_TEST);
		sceGuAlphaFunc(GU_GREATER, 127, 0xff);
	} else if (mode == U2PK_MODE_BLEND) {
		sceGuEnable(GU_BLEND);
		sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
		sceGuDepthMask(GU_TRUE);
	}
}

int world_draw(const World *w)
{
	int sub = 0, pass, c, k;

	if (!w->have_win)
		return 0;
	/* Backface culling ON. Ground/road top faces render with GU_CCW: with GU_CW
	 * the whole ground was culled (verified on PPSSPP 2026-11-12 - CW left the
	 * frame ~99.7% clear, CCW fills the lower half to the horizon), so upstream
	 * world triangles are counter-clockwise seen from above in our Z-up frame. */
	sceGuEnable(GU_CULL_FACE);
	sceGuFrontFace(GU_CCW);

	for (pass = U2PK_MODE_OPAQUE; pass <= U2PK_MODE_BLEND; pass++) {
		for (c = 0; c < w->nwin; c++) {
			const WorldCell *cell = &w->win[c];
			for (k = 0; k < (int)cell->v.nbatch; k++) {
				const U2pkBatch *b = &cell->v.batch[k];
				if ((int)b->mode != pass)
					continue;
				sceGumMatrixMode(GU_MODEL);
				sceGumLoadIdentity();
				sceGumTranslate((const ScePspFVector3 *)b->pos_offset);
				sceGumScale((const ScePspFVector3 *)b->pos_scale);
				setup_texture(w, b);
				apply_mode(b->mode);
				sceGumDrawArray(GU_TRIANGLES, VERTFMT, b->index_count,
				                (unsigned int *)(cell->v.index + b->first_index),
				                cell->vertf + (size_t)b->first_vertex * WORLD_VSTRIDE);
				sub++;
			}
		}
	}
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
	return sub;
}
