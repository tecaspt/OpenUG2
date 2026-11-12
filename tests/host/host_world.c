/* See host_world.h. Loader mirrors runtime/world.c; the ground query is the
 * 3x3 COLLIDE search from replay_host.c, kept in one place so the two native
 * harnesses cannot drift apart. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "u2pk.h"
#include "ground.h"
#include "host_world.h"

static uint8_t *slurp(const char *path, uint32_t *len_out)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *b;

	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) {
		fclose(f);
		return 0;
	}
	b = malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
	}
	fclose(f);
	if (!b)
		return 0;
	*len_out = (uint32_t)n;
	return b;
}

int host_world_open(HostWorld *ww, const char *dir)
{
	char ipath[512], cpath[512];
	uint32_t ilen = 0, clen = 0;
	U2pkView iv;
	int s;

	memset(ww, 0, sizeof *ww);
	snprintf(ipath, sizeof ipath, "%s/index.u2pk", dir);
	snprintf(cpath, sizeof cpath, "%s/cells.u2pk", dir);
	ww->index = slurp(ipath, &ilen);
	if (!ww->index)
		return 0;
	ww->cells = slurp(cpath, &clen);
	if (!ww->cells) {
		free(ww->index);
		ww->index = 0;
		return 0;
	}
	if (!u2pk_open(ww->index, ilen, &iv)) {
		fprintf(stderr, "index not a U2PK: %s\n", iv.err ? iv.err : "?");
		host_world_close(ww);
		return 0;
	}
	memcpy(&ww->gw, iv.hdr.reserved + 0, 2);
	memcpy(&ww->gh, iv.hdr.reserved + 2, 2);
	memcpy(&ww->x0, iv.hdr.reserved + 4, 4);
	memcpy(&ww->y0, iv.hdr.reserved + 8, 4);
	for (s = 0; s < (int)iv.hdr.section_count; s++)
		if (iv.sect[s].type == U2PK_S_CELLTAB) {
			ww->celltab = iv.buf + iv.sect[s].offset;
			ww->ncell = (int)iv.sect[s].count;
		}
	if (!ww->celltab || !ww->ncell || !ww->gw || !ww->gh) {
		fprintf(stderr, "index has no grid/CELLTAB\n");
		host_world_close(ww);
		return 0;
	}
	return 1;
}

void host_world_close(HostWorld *ww)
{
	free(ww->index);
	free(ww->cells);
	memset(ww, 0, sizeof *ww);
}

/* One CELLTAB row {i32 cx, i32 cy, u32 off, u32 size, f32 zmin, f32 zmax}. */
static void cell_at(const HostWorld *ww, int c, int *off, int *size)
{
	const uint8_t *r = ww->celltab + (size_t)c * 24;
	uint32_t o, sz;

	memcpy(&o, r + 8, 4);
	memcpy(&sz, r + 12, 4);
	*off = (int)o;
	*size = (int)sz;
}

int host_world_ground(void *user, float x, float y, float z_from,
                      float *z_out, float n_out[3])
{
	HostWorld *ww = (HostWorld *)user;
	int ccx = (int)floorf((x - ww->x0) / U2PK_CELL_M);
	int ccy = (int)floorf((y - ww->y0) / U2PK_CELL_M);
	int dx, dy, found = 0;
	float best = 0.0f, bn[3] = { 0, 0, 1 };

	for (dy = -1; dy <= 1; dy++)
		for (dx = -1; dx <= 1; dx++) {
			int nx = ccx + dx, ny = ccy + dy, c, off, size;
			U2pkView cv;
			const U2pkSection *cs;
			float z, n[3];

			if (nx < 0 || ny < 0 || nx >= ww->gw || ny >= ww->gh)
				continue;
			c = ny * ww->gw + nx;
			cell_at(ww, c, &off, &size);
			if (size <= 0 || off < 0)
				continue;
			if (!u2pk_open(ww->cells + off, (uint32_t)size, &cv))
				continue;
			cs = u2pk_find_section(&cv, U2PK_S_COLLIDE);
			if (!cs)
				continue;
			if (u2pk_ground_at(cv.buf + cs->offset, cs->size, x, y, z_from,
			                   &z, n))
				if (!found || z > best) {
					best = z;
					bn[0] = n[0];
					bn[1] = n[1];
					bn[2] = n[2];
					found = 1;
				}
		}
	if (!found)
		return 0;
	*z_out = best;
	n_out[0] = bn[0];
	n_out[1] = bn[1];
	n_out[2] = bn[2];
	return 1;
}
