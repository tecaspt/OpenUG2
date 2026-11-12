/* M5 PSP drive replay - the PSP arm of "same physics, same COLLIDE, final pose
 * within 0.5 m of the host". It runs the SAME shared/drive.c the host harness
 * runs, over the region streamed exactly as M4 streamed it (3x3 window, re-cut
 * float verts), and logs the final 600-frame pose. No drawing: the milestone's
 * picture is inherited-open from the M3 render defect, so this measures the
 * numeric spine only.
 *
 * It replays data/replay.txt (frame,steer,throttle,brake) from the pose named
 * in data/replay.cfg (region=, pose=; default L4RH / 1) and:
 *   - reloads the 3x3 window whenever the centre cell changes, so a drive that
 *     crosses a cell boundary re-reads its COLLIDE blobs and never touches a
 *     pointer a reload invalidated (world_ground_at re-resolves fresh too);
 *   - runs the whole replay TWICE from a cold re-init and compares bit-for-bit,
 *     the PSP-run-twice acceptance;
 *   - logs the final pose with %.9g so the m5.sh host comparison can diff it.
 *
 * data/replay.cfg keys (all optional): region (default L4RH), pose index
 * (default 1), frames (default: full script length). */
#include <pspuser.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "platform.h"
#include "pool.h"
#include "world.h"
#include "drive.h"
#include "u2pk.h"

#define MAXFRAMES 2000

static World g_world;   /* caller-owned state the ground callback borrows */

/* File-scope so the PSP user-thread stack is never a concern, and sized for a
 * full MAXFRAMES-line script (a stack truncation would silently shorten the
 * replay and corrupt the host comparison). */
static char g_rbuf[MAXFRAMES * 16];   /* replay.txt (~16 B/line max) */
static char g_buf[4096];              /* world.cfg */

static int psp_ground(void *user, float x, float y, float z_from, float *z,
                      float *n)
{
	return world_ground_at((const World *)user, x, y, z_from, z, n);
}

/* Read a whole file anchored at the module dir (cwd is not the game folder). */
static int slurp(const char *rel, char *buf, int cap)
{
	char path[300];
	int fd, n;

	plat_path(path, (int)sizeof path, rel);
	fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (fd < 0)
		return -1;
	n = sceIoRead(fd, buf, cap - 1);
	sceIoClose(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	return n;
}

static void cfg_get(const char *line, const char *key, char *dst, int dstsz)
{
	int n = (int)strlen(key);

	if (strncmp(line, key, n) == 0 && line[n] == '=') {
		const char *v = line + n + 1;
		int i = 0;
		while (v[i] && i < dstsz - 1)
			dst[i] = v[i], i++;
		dst[i] = 0;
	}
}

typedef struct {
	float steer, throttle, brake;
} Input;

/* Parse the replay script into per-frame inputs; returns the frame count. */
static int load_replay(const char *buf, Input *in, int cap)
{
	char *save = NULL, *line;
	int maxframe = -1;

	memset(in, 0, sizeof *in * cap);
	for (line = strtok_r((char *)buf, "\r\n", &save); line;
	     line = strtok_r(NULL, "\r\n", &save)) {
		int frame;
		float steer, throttle, brake;
		if (sscanf(line, " %d , %f , %f , %f", &frame, &steer, &throttle,
		           &brake) != 4)
			continue;
		if (frame < 0 || frame >= cap)
			continue;
		in[frame].steer = steer;
		in[frame].throttle = throttle;
		in[frame].brake = brake;
		if (frame > maxframe)
			maxframe = frame;
	}
	return maxframe + 1;
}

typedef struct {
	float x, y, z, yaw;
} Pose;

static int read_pose(const char *buf, int want, Pose *out)
{
	char *save = NULL, *line, copy[2048];
	int i = 0;

	strncpy(copy, buf, sizeof copy - 1);
	copy[sizeof copy - 1] = 0;
	for (line = strtok_r(copy, "\r\n", &save); line;
	     line = strtok_r(NULL, "\r\n", &save)) {
		if (strncmp(line, "pose=", 5) != 0)
			continue;
		if (i == want && sscanf(line + 5, "%f %f %f %f", &out->x, &out->y,
		                        &out->z, &out->yaw) == 4)
			return 0;
		i++;
	}
	return -1;
}

static void run(const Input *in, int nframes, const Pose *p, float pose[4],
                unsigned long *mask_sum, unsigned long *contact,
                unsigned long *air, int *reloads)
{
	Drive d;
	int k;

	world_load_window(&g_world, p->x, p->y);   /* window covering the spawn */
	*reloads = 0;
	if (drive_init(&d, psp_ground, &g_world, p->x, p->y, p->yaw, p->z) != 0) {
		pose[0] = pose[1] = pose[2] = pose[3] = NAN;
		return;
	}
	for (k = 0; k < nframes; k++) {
		int cc = world_cell_at(&g_world, d.pos[0], d.pos[1]);
		float pedal = in[k].throttle - in[k].brake;

		if (cc >= 0) {
			int nx = cc % g_world.gw, ny = cc / g_world.gw;
			if (nx != g_world.win_cx || ny != g_world.win_cy) {
				world_load_window(&g_world, d.pos[0], d.pos[1]);
				++*reloads;
			}
		}
		drive_step(&d, psp_ground, &g_world, pedal, in[k].steer, 0);
		*mask_sum += d.contact_mask;
	}
	pose[0] = d.pos[0];
	pose[1] = d.pos[1];
	pose[2] = d.pos[2];
	pose[3] = d.heading;
	*contact = d.contact_frames;
	*air = d.air_frames;
}

int main(int argc, char **argv)
{
	char region[32] = "L4RH";
	char cfgbuf[256];
	Input in[MAXFRAMES];
	Pose p;
	int pose_index = 1, want_frames = 0, nframes;
	float poseA[4], poseB[4];
	unsigned long msA = 0, msB = 0, cA = 0, cB = 0, aA = 0, aB = 0;
	int rlA = 0, rlB = 0, det;
	uint64_t t0, t1;
	char dir[64];

	(void)argc;
	plat_init(argc, argv);
	plat_setup_callbacks();
	plat_log("REPLAY start dir=%s", plat_dir());

	if (pool_init_all() != 0) {
		plat_log("REPLAY FAIL no pool");
		sceKernelExitGame();
		return 1;
	}

	/* Optional replay.cfg: region + pose index + optional frame cap. */
	if (slurp("data/replay.cfg", cfgbuf, sizeof cfgbuf) > 0) {
		char *save = NULL, *line, copy[256];
		strncpy(copy, cfgbuf, sizeof copy - 1);
		copy[sizeof copy - 1] = 0;
		for (line = strtok_r(copy, "\r\n", &save); line;
		     line = strtok_r(NULL, "\r\n", &save)) {
			char v[32];
			cfg_get(line, "region", region, sizeof region);
			v[0] = 0;
			cfg_get(line, "pose", v, sizeof v);
			if (v[0])
				pose_index = atoi(v);
			v[0] = 0;
			cfg_get(line, "frames", v, sizeof v);
			if (v[0])
				want_frames = atoi(v);
		}
	}

	if (slurp("data/replay.txt", g_rbuf, (int)sizeof g_rbuf) <= 0) {
		plat_log("REPLAY FAIL no data/replay.txt");
		sceKernelExitGame();
		return 1;
	}
	nframes = load_replay(g_rbuf, in, MAXFRAMES);
	if (want_frames > 0 && want_frames < nframes)
		nframes = want_frames;
	if (nframes < 1) {
		plat_log("REPLAY FAIL empty script");
		sceKernelExitGame();
		return 1;
	}

	if (slurp("data/world.cfg", g_buf, (int)sizeof g_buf) <= 0 ||
	    read_pose(g_buf, pose_index, &p) != 0) {
		plat_log("REPLAY FAIL no pose %d in data/world.cfg", pose_index);
		sceKernelExitGame();
		return 1;
	}

	snprintf(dir, sizeof dir, "data/%s", region);
	if (!world_open(&g_world, pool_get(POOL_WORLD), pool_get(POOL_TEXTURES),
	                dir)) {
		plat_log("REPLAY FAIL world %s (run psp_convert first)", dir);
		sceKernelExitGame();
		return 1;
	}
	plat_log("REPLAY region=%s pose %d (%.3f,%.3f,%.3f yaw%.4f) frames=%d "
	         "grid=%dx%d",
	         region, pose_index, p.x, p.y, p.z, p.yaw, nframes, g_world.gw,
	         g_world.gh);

	t0 = plat_us();
	run(in, nframes, &p, poseA, &msA, &cA, &aA, &rlA);
	run(in, nframes, &p, poseB, &msB, &cB, &aB, &rlB);
	t1 = plat_us();

	det = memcmp(poseA, poseB, sizeof poseA) == 0 && msA == msB && cA == cB &&
	      aA == aB && rlA == rlB && isfinite(poseA[0]);

	/* %.9g prints a float32 exactly, so the host comparison sees every bit. */
	plat_log("REPLAY final x=%.9g y=%.9g z=%.9g yaw=%.9g masksum=%lu "
	         "contact=%lu air=%lu reloads=%d",
	         poseA[0], poseA[1], poseA[2], poseA[3], msA, cA, aA, rlA);
	plat_log("REPLAY determinism(A==B)=%s travel=%.3f m guest_us_2runs=%llu",
	         det ? "PASS" : "FAIL", hypotf(poseA[0] - p.x, poseA[1] - p.y),
	         (unsigned long long)(t1 - t0));
	plat_log("REPLAY world_high=%u", (unsigned)g_world.pw->high);
	pool_report();

	if (!det)
		plat_log("REPLAY RESULT: DETERMINISM FAILED");
	else
		plat_log("REPLAY RESULT: OK (host compares this final pose to 0.5 m)");

	sceKernelDelayThread(300000);   /* let deferred log writes flush */
	sceKernelExitGame();
	return det ? 0 : 1;
}
