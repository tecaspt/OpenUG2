/* M5 host replay harness - INCREMENT 1: real-data ground validation.
 *
 * The native half of the "host build of the same physics and the same COLLIDE
 * data, final position within 0.5 m" acceptance. It links the SAME shared/
 * ground.c the PSP runtime uses and loads the region through host_world.c - the
 * same index -> grid -> 3x3 cells -> COLLIDE walk the runtime does - so the two
 * sides cannot drift.
 *
 * This increment establishes the ground primitive on real data: for each pose
 * the converter guarantees to sit 2 m above a road surface, the query from that
 * height must return a ground at most 4 m below the pose, facing up, and
 * bit-identical on repeat. Increment 2 (drive_replay.c) steps the physics on
 * top of the same loader.
 *
 * usage: replay_host <region_dir> [world.cfg]
 *   region_dir holds index.u2pk and cells.u2pk (as produced by psp_convert).
 *   world.cfg defaults to <region_dir>/../world.cfg; its "pose=x y z yaw" lines
 *   (z = road + 2, per the converter) are each validated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_world.h"

static int fails;

static void check(const char *what, int ok)
{
	printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok)
		++fails;
}

int main(int argc, char **argv)
{
	char cfgpath[512];
	const char *dir = argc > 1 ? argv[1] : "out/m4/L4RH";
	HostWorld ww;
	FILE *cfg;
	char line[256];
	int npose = 0;

	if (argc > 2)
		snprintf(cfgpath, sizeof cfgpath, "%s", argv[2]);
	else
		snprintf(cfgpath, sizeof cfgpath, "%s/../world.cfg", dir);

	if (!host_world_open(&ww, dir))
		return 2;
	printf("grid %dx%d x0=%.1f y0=%.1f ncell=%d\n", ww.gw, ww.gh, ww.x0, ww.y0,
	       ww.ncell);

	cfg = fopen(cfgpath, "r");
	if (!cfg) {
		fprintf(stderr, "no world.cfg at %s\n", cfgpath);
		host_world_close(&ww);
		return 2;
	}
	while (fgets(line, sizeof line, cfg)) {
		float x, y, z, yaw, gz, gn[3], gz2, gn2[3];
		int hit, hit2;
		char what[96];

		if (strncmp(line, "pose=", 5) != 0)
			continue;
		if (sscanf(line + 5, "%f %f %f %f", &x, &y, &z, &yaw) != 4)
			continue;
		hit = host_world_ground(&ww, x, y, z, &gz, gn);
		hit2 = host_world_ground(&ww, x, y, z, &gz2, gn2);
		printf("pose %d (%.2f,%.2f) pose_z=%.2f -> ground %s z=%.4f n=(%.3f,%.3f,%.3f)\n",
		       npose, x, y, z, hit ? "hit" : "MISS",
		       hit ? gz : 0.0f, hit ? gn[0] : 0, hit ? gn[1] : 0, hit ? gn[2] : 0);
		snprintf(what, sizeof what, "pose %d ground present + sane + up-facing", npose);
		check(what, hit && gz <= z + 0.01f && gz > z - 4.0f && gn[2] > 0.7f);
		snprintf(what, sizeof what, "pose %d determinism", npose);
		check(what, hit2 == hit && memcmp(&gz, &gz2, sizeof gz) == 0 &&
		          memcmp(gn, gn2, sizeof gn) == 0);
		npose++;
	}
	fclose(cfg);

	if (!npose) {
		fprintf(stderr, "world.cfg had no poses\n");
		host_world_close(&ww);
		return 2;
	}
	host_world_close(&ww);
	if (fails) {
		printf("\nREPLAY_HOST GROUND VALIDATION FAILED (%d)\n", fails);
		return 1;
	}
	printf("\nREPLAY_HOST GROUND VALIDATION PASS (%d poses on real L4RH COLLIDE)\n",
	       npose);
	return 0;
}
