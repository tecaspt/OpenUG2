/* Host-side world loader shared by every M5 native harness.
 *
 * The host-vs-PSP 0.5 m gate only means something if the host reads the region
 * exactly the way the runtime does: same index -> grid -> 3x3-cell -> COLLIDE
 * walk, same u2pk_ground_at. Both replay harnesses (ground validation and drive
 * replay) therefore load through this one file instead of each keeping its own
 * copy. Mirrors runtime/world.c and shared/ground.c. */
#ifndef OPENUG2_TESTS_HOST_WORLD_H
#define OPENUG2_TESTS_HOST_WORLD_H

#include <stdint.h>

typedef struct {
	uint16_t gw, gh;
	float    x0, y0;
	const uint8_t *celltab;   /* ncell x 24B */
	int      ncell;
	uint8_t *index;           /* owned */
	uint8_t *cells;           /* owned */
} HostWorld;

/* Load dir/index.u2pk and dir/cells.u2pk, parse the grid and CELLTAB. Returns 1
 * on success. On failure prints to stderr and returns 0 (ww contents undefined
 * unless 1 was returned). */
int host_world_open(HostWorld *ww, const char *dir);
void host_world_close(HostWorld *ww);

/* DriveGround-shaped ground query over the loaded cells: highest surface at or
 * below z_from under (x,y), searching the 3x3 window around that point. Fills
 * *z_out and up-facing n_out[3]; returns 1 on a hit. Signature matches
 * DriveGround so the drive spine can call it directly via a user pointer. */
int host_world_ground(void *user, float x, float y, float z_from,
                      float *z_out, float n_out[3]);

#endif /* OPENUG2_TESTS_HOST_WORLD_H */
