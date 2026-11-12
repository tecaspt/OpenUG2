/* car_conv.h - U2PK v1 car writer for psp_convert.
 *
 * Turns a loaded Car (car_load.h) into a U2PK v1 car file (docs/U2PK.md).
 *
 * The rules this file enforces, all from the M3 brief:
 *   - Body LODs: every mesh except wheel-mount ones and N2_CAR_INTERIOR.
 *   - Wheels: the stock tier only, one batch set shared by both LODs.
 *   - Textures: T8 + 256-entry CLUT, downscaled to 128 px for the player LOD and
 *     64 px for the AI LOD, swizzled, deduped by content.
 *   - Vertex colour is baked (v1 has no hardware lights) and packed 5650.
 *   - Batches are keyed by (texture, draw mode) and never cross LOD groups.
 *   - Budgets are hard; a car that cannot be clustered into them fails.
 */
#ifndef OPENUG2_CONVERTER_CAR_CONV_H
#define OPENUG2_CONVERTER_CAR_CONV_H

#include "car_load.h"

typedef struct {
	char name[32];
	int ok;
	char why[256];

	/* Per LOD group: 0 = player, 1 = AI, 2 = wheels. */
	int tris_before[3];
	int tris_after[3];
	int nbatch[3];
	long bytes[3];

	int ntex, nclut, nbatch_total;
	long total;                 /* bytes of the file written */
	long tex_bytes;             /* texture payloads plus their CLUT slots */
	int skipped;                /* meshes deliberately not drawn */
	int wheel_from_global;
	float wheel_radius;
	float body_box[6];
} CarReport;

/* Converts one car to outpath. Returns 1 on success. On failure returns 0 and
 * rep->why explains it in one line; rep is always filled enough to report.
 * The caller owns car and keeps it alive for other uses. */
int car_convert(Car *car, const char *outpath, CarReport *rep);

#endif /* OPENUG2_CONVERTER_CAR_CONV_H */
