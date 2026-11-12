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

/* Hardware render bug isolation - the emulator draws these all identically, so
 * a bit only changes what a real GE sees. Select with "probe=<n>" in
 * data/autotest.cfg (works without frames=/shot=, so on hardware too). Call
 * before car_load so the fold happens as the verts are cut.
 *
 * DEFAULT (0) now FOLDS each batch's pos_offset/pos_scale into the float
 * positions and draws with the batch transform skipped, so the car body ends up
 * under an identity model matrix - the config M0 proved draws correctly on a
 * real PSP-1000, whose only non-identity transform before was that anisotropic
 * per-batch scale+offset. world.c does the same for the terrain. The old
 * model-matrix path is still selectable so one hardware run can A/B them:
 *   2  PROBE_MODEL: skip the fold, push each batch's T(pos_offset)S(pos_scale)
 *      as before (the path a real PSP-1000 distorted)
 *   1  extra sceGumUpdateMatrix() before each DrawArray
 *   4  draw the body only, skipping the four wheels (isolates the FR/RR
 *      negative-determinant mirror; the wheels still use their own hub+mirror
 *      model matrix even under the fold)
 * If the fold does not fix the hardware picture, the next diagnostics to add
 * are GU_TEXTURE_32BITF UVs and an absolute (non-batch-local) index base.
 */
void car_set_probe(int bits);

#endif /* OPENUG2_CAR_H */
