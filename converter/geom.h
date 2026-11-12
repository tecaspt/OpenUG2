/* Geometry helpers for psp_convert: vertex colour bake, vertex clustering,
 * bounding boxes. Pure host C99, no PSP and no U2PK knowledge here.
 */
#ifndef OPENUG2_CONVERTER_GEOM_H
#define OPENUG2_CONVERTER_GEOM_H

#include <stdint.h>

#include "nfsu2.h"

/* One triangle list in model space with its colour already baked, ready to be
 * packed into a batch. col is 0x00RRGGBB. */
typedef struct {
	float    *pos;  /* n*3 */
	float    *uv;   /* n*2 */
	uint32_t *col;  /* n   */
	uint16_t *idx;  /* nidx */
	int n, nidx;
	int tex;        /* index into the U2PK texture table, -1 = untextured */
	uint32_t mode;  /* U2PK_MODE_* */
} Geo;

void geo_free(Geo *g);

/* Builds one drawable from a source mesh.
 *
 * Normals come from the mesh when it authors them, else from the average of the
 * (unnormalised) normals of the faces that use the vertex. Lighting is baked
 * because v1 has no hardware lights: shade = 0.45 + 0.55*max(0, N.L) with
 * L = normalize(0.3, 0.4, 0.85), multiplied by base. base is white for a
 * textured mesh, mid grey for an untextured one, tinted by paint for a body
 * panel, and replaced wholesale by the source prelight when it has one.
 *
 * Returns 1 and fills *g (caller frees with geo_free), or 0 with *why set.
 */
typedef struct {
	uint32_t base;     /* 0x00RRGGBB */
	int prelight;      /* 1 = use m->vcol and skip the bake */
} Bake;

int geo_from_mesh(Geo *g, const N2Mesh *m, const Bake *bake, const char **why);

/* Vertex clustering: snaps to a cell grid and welds. The cluster key is
 * (cell x, y, z, u quantised to 1/16, v quantised to 1/16) inside this one
 * part, so a welded vertex only replaces another that samples the same texel.
 * Each merged vertex is the mean of its members; triangles that lose two equal
 * indices are dropped. Returns 1 on success (out may have fewer vertices and
 * indices than in), 0 on allocation failure. */
int geo_cluster(Geo *out, const Geo *in, float cell);

/* Axis-aligned bounds of the positions. */
void geo_bbox(const Geo *g, float lo[3], float hi[3]);

/* ---- quantised encodings, shared with the host tests (t_vertex) ---- */

/* GU_COLOR_5650 as the GE actually reads it: bits 11..15 blue, 5..10 green,
 * 0..4 red. Measured by runtime/vfmt - the naive PC pack swaps red and blue. */
static inline uint16_t u2pk_color5650(uint32_t rgb)
{
	uint32_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
	return (uint16_t)((r >> 3) | (((g >> 2) & 0x3f) << 5) | ((b >> 3) << 11));
}

/* Position/UV fields are signed/unsigned 16-bit normalised coordinates in a
 * batch's own box. scale is the half-extent (position) or the uv range, both
 * always > 0; v is the coordinate relative to the batch offset.
 * q16_pos saturates at +-32767, q16_uv at 0..65535. */
static inline int16_t q16_pos(float v, float scale)
{
	float s = v / scale * 32767.0f;
	if (!(s > -32767.0f)) return -32767;   /* also catches NaN */
	if (s > 32767.0f) return 32767;
	return (int16_t)(s >= 0.0f ? s + 0.5f : s - 0.5f);
}

static inline uint16_t q16_uv(float v, float scale)
{
	float s = v / scale * 32768.0f;
	if (!(s > 0.0f)) return 0;
	if (s > 65535.0f) return 65535;
	return (uint16_t)(s + 0.5f);
}

#endif /* OPENUG2_CONVERTER_GEOM_H */
