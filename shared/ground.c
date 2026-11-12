/* See ground.h. Pure data + arithmetic: no allocation, no platform calls, so it
 * is byte-identical on host and PSP and unit-testable on synthetic COLLIDE. */
#include "ground.h"
#include <math.h>
#include <string.h>

const U2pkSection *u2pk_find_section(const U2pkView *v, uint32_t type)
{
	uint32_t i;

	if (!v)
		return 0;
	for (i = 0; i < v->hdr.section_count; i++)
		if (v->sect[i].type == type)
			return &v->sect[i];
	return 0;
}

/* 2-D point-in-triangle (XY) returning barycentric (u,w) for point =
 * a + u*(b-a) + w*(c-a); 1 when inside (with a small tolerance), 0 otherwise.
 * A triangle with a degenerate XY projection (a near-vertical wall) is skipped
 * - the ground query is about near-horizontal road/terrain. */
static int point_in_tri(float x, float y, const float *a, const float *b,
                        const float *c, float *u, float *w)
{
	float d00 = b[0] - a[0], d01 = b[1] - a[1];
	float d10 = c[0] - a[0], d11 = c[1] - a[1];
	float px = x - a[0], py = y - a[1];
	float det = d00 * d11 - d10 * d01;
	const float eps = 1e-6f;

	if (fabsf(det) < eps)
		return 0;
	*u = (px * d11 - py * d10) / det;
	*w = (d00 * py - d01 * px) / det;
	return (*u >= -eps && *w >= -eps && *u + *w <= 1.0f + eps);
}

static void tri_normal(const float *a, const float *b, const float *c, float n[3])
{
	float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
	float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
	float nx = uy * vz - uz * vy;
	float ny = uz * vx - ux * vz;
	float nz = ux * vy - uy * vx;
	float len = sqrtf(nx * nx + ny * ny + nz * nz);

	if (len < 1e-9f) {
		n[0] = n[1] = 0.0f;
		n[2] = 1.0f;
		return;
	}
	n[0] = nx / len;
	n[1] = ny / len;
	n[2] = nz / len;
	if (n[2] < 0.0f) {          /* always hand back the up-facing normal */
		n[0] = -n[0];
		n[1] = -n[1];
		n[2] = -n[2];
	}
}

int u2pk_ground_at(const uint8_t *blob, uint32_t size, float x, float y,
                   float z_from, float *z_out, float n_out[3])
{
	int32_t ntri, ggw, ggh;
	float gx0, gy0;
	const float *tri;
	const uint16_t *start, *list;
	int bx, by, gy, gx, found = 0;
	float best = 0.0f, bn[3] = { 0.0f, 0.0f, 1.0f };
	uint32_t tri_bytes, start_bytes, need;

	if (!blob || size < 20)
		return 0;
	memcpy(&ntri, blob + 0, 4);
	memcpy(&gx0, blob + 4, 4);
	memcpy(&gy0, blob + 8, 4);
	memcpy(&ggw, blob + 12, 4);
	memcpy(&ggh, blob + 16, 4);
	if (ntri < 0 || ggw < 1 || ggh < 1)
		return 0;

	tri_bytes = (uint32_t)ntri * 9u * 4u;
	start_bytes = (uint32_t)(ggw * ggh + 1) * 2u;
	need = 20u + tri_bytes + start_bytes;
	if (size < need)
		return 0;                       /* header promises more than we have */

	tri = (const float *)(blob + 20);
	start = (const uint16_t *)(blob + 20 + tri_bytes);
	list = start + (uint32_t)(ggw * ggh + 1);
	if (size < need + (uint32_t)start[ggw * ggh] * 2u)
		return 0;                       /* truncated list */

	bx = (int)floorf((x - gx0) / 10.0f);
	by = (int)floorf((y - gy0) / 10.0f);

	for (gy = by - 1; gy <= by + 1; gy++) {
		int g;
		if (gy < 0 || gy >= ggh)
			continue;
		for (gx = bx - 1; gx <= bx + 1; gx++) {
			int i, lo, hi;
			if (gx < 0 || gx >= ggw)
				continue;
			g = gy * ggw + gx;
			lo = start[g];
			hi = start[g + 1];
			for (i = lo; i < hi; i++) {
				const float *a, *b, *c;
				float u, w, z;
				int ord = list[i];
				if (ord < 0 || ord >= ntri)
					continue;
				a = tri + (uint32_t)ord * 9u;
				b = a + 3;
				c = a + 6;
				if (!point_in_tri(x, y, a, b, c, &u, &w))
					continue;
				z = a[2] + u * (b[2] - a[2]) + w * (c[2] - a[2]);
				if (z > z_from + 1e-3f)   /* surface above the ray origin */
					continue;
				if (!found || z > best) { /* highest surface at or below z_from */
					best = z;
					tri_normal(a, b, c, bn);
					found = 1;
				}
			}
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
