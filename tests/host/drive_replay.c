/* M5 host drive replay - INCREMENT 2: step the real physics on real COLLIDE.
 *
 * Runs the shared drive spine (shared/drive.c: stage1_drive_step + the upstream
 * sprung-ride integrator) for a scripted set of ticks on a converted region, and
 * reports the final pose plus a from-scratch determinism re-run. This is the
 * host arm of the acceptance; the PSP arm builds the same shared/drive.c into a
 * dedicated EBOOT and runs the same replay.txt, and the two final poses are
 * compared to 0.5 m.
 *
 * The loader is host_world.c - the same index -> grid -> 3x3 -> COLLIDE walk the
 * runtime uses - so the host and PSP ground cannot drift.
 *
 * usage: drive_replay <region_dir> [pose_index] [replay.txt]
 *   region_dir   holds index.u2pk / cells.u2pk (psp_convert output).
 *   pose_index   which "pose=" line of <region>/../world.cfg to spawn on (1).
 *   replay.txt   lines "frame,steer,throttle,brake"; missing frames coast.
 *
 * Exit 0 iff the two identical runs are bit-for-bit equal. Prints the final pose
 * with %.9g so a one-bit difference shows up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "u2pk.h"
#include "drive.h"
#include "host_world.h"

#define MAXFRAMES 2000

typedef struct {
	float steer, throttle, brake;
} Input;

static int load_replay(const char *path, Input *in, int *nframes_out)
{
	FILE *f = fopen(path, "r");
	char line[128];
	int n = 0, maxframe = 0;

	memset(in, 0, sizeof *in * MAXFRAMES);
	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		int frame;
		float steer, throttle, brake;
		if (sscanf(line, " %d , %f , %f , %f", &frame, &steer, &throttle,
		           &brake) != 4)
			continue;
		if (frame < 0 || frame >= MAXFRAMES)
			continue;
		in[frame].steer = steer;
		in[frame].throttle = throttle;
		in[frame].brake = brake;
		if (frame > maxframe)
			maxframe = frame;
		n = maxframe + 1;
	}
	fclose(f);
	*nframes_out = n;
	return 0;
}

typedef struct {
	float x, y, z, yaw;
} Pose;

static int read_pose(const char *cfgpath, int want, Pose *out)
{
	FILE *f = fopen(cfgpath, "r");
	char line[256];
	int i = 0;

	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, "pose=", 5) != 0)
			continue;
		if (i == want && sscanf(line + 5, "%f %f %f %f", &out->x, &out->y,
		                        &out->z, &out->yaw) == 4) {
			fclose(f);
			return 0;
		}
		i++;
	}
	fclose(f);
	return -1;
}

/* One full replay from a cold start; fills pose + a rolling mix so a
 * determinism difference anywhere in the run is visible even if the last pose
 * happens to coincide. */
static void run(HostWorld *ww, const Pose *p, const Input *in, int nframes,
                float out_pose[4], unsigned long *mask_sum,
                unsigned long *contact, unsigned long *air)
{
	Drive d;
	int k;

	if (drive_init(&d, host_world_ground, ww, p->x, p->y, p->yaw, p->z) != 0) {
		fprintf(stderr, "drive_init failed\n");
		out_pose[0] = out_pose[1] = out_pose[2] = out_pose[3] = NAN;
		return;
	}
	for (k = 0; k < nframes; k++) {
		float pedal = in[k].throttle - in[k].brake;

		drive_step(&d, host_world_ground, ww, pedal, in[k].steer, 0);
		*mask_sum += d.contact_mask;
	}
	out_pose[0] = d.pos[0];
	out_pose[1] = d.pos[1];
	out_pose[2] = d.pos[2];
	out_pose[3] = d.heading;
	*contact = d.contact_frames;
	*air = d.air_frames;
}

int main(int argc, char **argv)
{
	char cfgpath[512];
	const char *dir = argc > 1 ? argv[1] : "out/m4/L4RH";
	int pose_index = argc > 2 ? atoi(argv[2]) : 1;
	const char *replay = argc > 3 ? argv[3] : "replay.txt";
	HostWorld ww;
	Pose p;
	Input in[MAXFRAMES];
	int nframes = 0, det_ok, cross_cell;
	float poseA[4], poseB[4];
	unsigned long msA = 0, msB = 0, cA = 0, cB = 0, aA = 0, aB = 0;
	float cx, cy;

	if (!host_world_open(&ww, dir))
		return 2;
	if (argc > 4)
		snprintf(cfgpath, sizeof cfgpath, "%s", argv[4]);
	else
		snprintf(cfgpath, sizeof cfgpath, "%s/../world.cfg", dir);
	if (read_pose(cfgpath, pose_index, &p) != 0) {
		fprintf(stderr, "no pose %d in %s\n", pose_index, cfgpath);
		host_world_close(&ww);
		return 2;
	}
	if (load_replay(replay, in, &nframes) != 0) {
		fprintf(stderr, "no replay script at %s\n", replay);
		host_world_close(&ww);
		return 2;
	}
	if (nframes < 1) {
		fprintf(stderr, "replay script had no frames\n");
		host_world_close(&ww);
		return 2;
	}

	cx = (float)floor((p.x - ww.x0) / U2PK_CELL_M);
	cy = (float)floor((p.y - ww.y0) / U2PK_CELL_M);
	printf("region %s pose %d (%.2f,%.2f,%.2f yaw%.3f) cell (%.0f,%.0f) "
	       "grid %dx%d frames=%d\n",
	       dir, pose_index, p.x, p.y, p.z, p.yaw, cx, cy, ww.gw, ww.gh, nframes);

	run(&ww, &p, in, nframes, poseA, &msA, &cA, &aA);
	run(&ww, &p, in, nframes, poseB, &msB, &cB, &aB);

	printf("run A  final x=%.9g y=%.9g z=%.9g yaw=%.9g  masksum=%lu contact=%lu air=%lu\n",
	       poseA[0], poseA[1], poseA[2], poseA[3], msA, cA, aA);
	printf("run B  final x=%.9g y=%.9g z=%.9g yaw=%.9g  masksum=%lu contact=%lu air=%lu\n",
	       poseB[0], poseB[1], poseB[2], poseB[3], msB, cB, aB);

	det_ok = memcmp(poseA, poseB, sizeof poseA) == 0 && msA == msB &&
	         cA == cB && aA == aB && isfinite(poseA[0]) && isfinite(poseA[1]);
	cross_cell = poseA[0] != poseA[0] ? 0
	           : ((int)floor((poseA[0] - ww.x0) / U2PK_CELL_M) != (int)cx ||
	              (int)floor((poseA[1] - ww.y0) / U2PK_CELL_M) != (int)cy);
	printf("determinism(A==B): %s   crossed a cell boundary: %s   travel=%.2f m\n",
	       det_ok ? "PASS" : "FAIL", cross_cell ? "yes" : "no",
	       hypotf(poseA[0] - p.x, poseA[1] - p.y));

	host_world_close(&ww);
	if (!det_ok) {
		printf("\nDRIVE_REPLAY HOST: DETERMINISM FAILED\n");
		return 1;
	}
	printf("\nDRIVE_REPLAY HOST OK (deterministic; feed this final pose to the "
	       "PSP-arm 0.5 m comparison)\n");
	return 0;
}
