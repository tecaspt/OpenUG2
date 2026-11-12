/* M5 ground-contact query. Shared by the PSP runtime and the host replay build
 * so both derive the ground from the exact same COLLIDE bytes and arithmetic -
 * the host-vs-PSP "within 0.5 m" gate only means something if the two sides run
 * one implementation, not two. See docs/U2PK.md COLLIDE.
 *
 * The COLLIDE blob layout (written by converter/region_convert.c) is:
 *   i32 ntri; f32 gx0, gy0; i32 ggw, ggh;
 *   ntri * (9 f32) triangles, xyz xyz xyz, WORLD coordinates;
 *   u16 start[ggw*ggh + 1];   CSR row starts over a 10 m grid anchored at gx0,gy0
 *   u16 list[start[ggw*ggh]]; triangle ordinals per grid bucket (by centroid)
 *
 * A triangle is bucketed by its centroid, so one near a bucket seam can be
 * missed if only the query bucket is searched; the query searches the 3x3
 * neighbourhood, which is cheap and covers the <=8 m edges the converter is
 * required to keep. */
#ifndef OPENUG2_GROUND_H
#define OPENUG2_GROUND_H

#include <stdint.h>
#include "u2pk.h"

/* Find a section of `type` in an opened U2PK, or NULL if the file has none. */
const U2pkSection *u2pk_find_section(const U2pkView *v, uint32_t type);

/* Ground height + up-normal under (x, y), searched downward from z_from. On a
 * hit returns 1 and writes the highest triangle surface at or below z_from into
 * *z_out and its (up-facing) unit normal into n_out[3]. Returns 0 (leaving the
 * outputs untouched) when the blob has no COLLIDE, is malformed, or no triangle
 * covers the point below z_from - the caller then treats that wheel as
 * airborne. `blob`/`size` are the COLLIDE section bytes (view.buf + off). */
int u2pk_ground_at(const uint8_t *blob, uint32_t size, float x, float y,
                   float z_from, float *z_out, float n_out[3]);

#endif /* OPENUG2_GROUND_H */
