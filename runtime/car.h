/* M3 runtime: load one U2PK car and draw it with sceGu/sceGum. The vertex and
 * batch encodings are exactly those in shared/u2pk.h (measured against the GE
 * in M3 step 1); the draw follows the milestone's step 4-5 sequence. */
#ifndef OPENUG2_CAR_H
#define OPENUG2_CAR_H

#include "pool.h"
#include "u2pk.h"

typedef struct {
	U2pkView v;
	uint8_t *buf;         /* file bytes, borrowed from the Cars pool */
	uint8_t *vertf;       /* s16 positions re-cut as GE float vertices (see car.c) */
	uint32_t len;
	int lod;              /* body drawn from LOD0 (player) or LOD1 (AI) */
	float world[3];       /* car position in world space */
	float yaw;            /* radians about +Z */
} CarG;

/* Read <dir>/<relpath> into `pool`, u2pk_open it, sanity-check it is a car.
 * Returns 1 on success (out filled), 0 on any failure (reason logged). */
int car_load(Pool *pool, const char *relpath, int lod, CarG *out);

/* Draw the body (LOD = out->lod) then the shared wheel set at its four hub
 * positions. Leaves GU_MODEL matrix dirty; callers reset it. */
void car_draw(const CarG *car);

#endif /* OPENUG2_CAR_H */
