/* OpenUG2 PSP runtime.
 *
 * M3 mode (default): one parked car drawn from a U2PK file by an orbiting
 * camera. M4 mode (autotest.cfg "mode=world"): a static world slice - a 3x3
 * window of region cells streamed from data/<REGION>/ - seen from fixed poses.
 *
 * Both modes share the 17 MB pool carve-out and log frame_us (sceGuStart..
 * sceGuSync, so gfx_swap's vblank wait is not counted) and the per-pool
 * high-water. In autotest mode a screenshot is written and the run exits.
 */
#include <pspuser.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "platform.h"
#include "pool.h"
#include "gfx.h"
#include "shot.h"
#include "car.h"
#include "world.h"

#define CAR_REL "data/car.u2pk"
#define MAX_POSES 8

/* autotest.cfg keys. Missing keys fall back to car mode / default region. */
typedef struct {
	int   frames;      /* frames (world: per pose) */
	int   shot_at;     /* 1-based frame on which to screenshot */
	int   have_frames, have_shot;
	int   is_world;
	int   probe;       /* car render bug isolation bits, see car_set_probe */
	char  region[32];  /* world mode: subdir under data/ */
} Cfg;

/* Eye orbits radius 6, height 2, around a point just above the car; autotest
 * pins t so the golden is deterministic. */
static void set_camera(float t)
{
	ScePspFVector3 eye = { 6.0f * cosf(t), 6.0f * sinf(t), 2.0f };
	ScePspFVector3 center = { 0.0f, 0.0f, 0.6f };
	ScePspFVector3 up = { 0.0f, 0.0f, 1.0f };

	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadIdentity();
	sceGumPerspective(60.f, 480.f / 272.f, 0.5f, 100.f);

	sceGumMatrixMode(GU_VIEW);
	sceGumLoadIdentity();
	sceGumLookAt(&eye, &center, &up);
}

/* World camera: eye at (x,y,z) looking horizontally along `yaw`, far plane
 * 160 m per the M4 spec. */
static void set_camera_world(float x, float y, float z, float yaw)
{
	ScePspFVector3 eye = { x, y, z };
	ScePspFVector3 center = { x + cosf(yaw) * 10.0f, y + sinf(yaw) * 10.0f,
	                          z - 0.2f };
	ScePspFVector3 up = { 0.0f, 0.0f, 1.0f };

	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadIdentity();
	sceGumPerspective(60.f, 480.f / 272.f, 0.5f, 160.f);

	sceGumMatrixMode(GU_VIEW);
	sceGumLoadIdentity();
	sceGumLookAt(&eye, &center, &up);
}

static void cfg_get(const char *line, const char *key, char *dst, int dstsz)
{
	int n = (int)strlen(key);
	if (strncmp(line, key, n) == 0 && line[n] == '=') {
		const char *v = line + n + 1;
		int i = 0;
		while (v[i] && i < dstsz - 1) { dst[i] = v[i]; i++; }
		dst[i] = 0;
	}
}

/* Parses data/autotest.cfg. Returns 1 if it had both frames= and shot=. */
static int read_cfg(Cfg *c)
{
	char path[300];
	char buf[512];
	char *line;
	int fd, n;

	memset(c, 0, sizeof *c);
	snprintf(c->region, sizeof c->region, "L4RH");
	plat_path(path, (int)sizeof(path), "data/autotest.cfg");
	fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (fd < 0)
		return 0;
	n = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	for (line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		char s[40];
		s[0] = 0;
		cfg_get(line, "frames", s, sizeof s);
		if (s[0]) { c->frames = atoi(s); c->have_frames = 1; }
		s[0] = 0;
		cfg_get(line, "shot", s, sizeof s);
		if (s[0]) { c->shot_at = atoi(s); c->have_shot = 1; }
		s[0] = 0;
		cfg_get(line, "probe", s, sizeof s);
		if (s[0]) c->probe = (int)strtol(s, NULL, 0);
	}
	return c->have_frames && c->have_shot;
}

typedef struct { float x, y, z, yaw; } Pose;

/* Reads data/world.cfg - its very presence selects world mode. Keys: one
 * "region=<subdir under data/>" line and up to `max` "pose=x y z yaw" lines.
 * Returns the pose count (>=0) when the file exists, or -1 if it does not, in
 * which case main falls back to car mode. */
static int read_world_cfg(Cfg *c, Pose *poses, int max)
{
	char path[300];
	char buf[1024];
	char *line;
	int fd, n, cnt = 0;

	plat_path(path, (int)sizeof(path), "data/world.cfg");
	fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (fd < 0)
		return -1;
	n = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	for (line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
		char s[40];
		s[0] = 0;
		cfg_get(line, "region", s, sizeof s);
		if (s[0]) {
			strncpy(c->region, s, sizeof c->region - 1);
			c->region[sizeof c->region - 1] = 0;
			continue;
		}
		if (strncmp(line, "pose=", 5) != 0 || cnt >= max)
			continue;
		if (sscanf(line + 5, "%f %f %f %f", &poses[cnt].x, &poses[cnt].y,
		           &poses[cnt].z, &poses[cnt].yaw) == 4)
			cnt++;
	}
	return cnt;
}

static void run_car(CarG *player, CarG *ai, Cfg *cfg, int autotest)
{
	SceCtrlData pad;
	int frames = cfg->frames, shot_at = cfg->shot_at, frame;
	float t = autotest ? 0.8f : 0.0f;
	uint64_t acc = 0, worst = 0, t0;
	Pool *cars = pool_get(POOL_CARS);

	frame = 0;
	while (!g_quit) {
		t0 = plat_us();
		gfx_begin();
		set_camera(t);
		sceGuFog(20, 60, 0x00102030);   /* matches the clear colour, 0xAABBGGRR */
		sceGuEnable(GU_FOG);
		sceGuDisable(GU_CULL_FACE);
		car_draw(player);
		gfx_end();

		{
			uint64_t dt = plat_us() - t0;
			acc += dt;
			if (dt > worst) worst = dt;
		}

		frame++;
		if (autotest && frame == shot_at) {
			unsigned int high = (unsigned int)cars->high;
			shot_write("m3");
			plat_log("M3 frame_us avg=%llu max=%llu frames=%d",
			         (unsigned long long)(acc / (uint64_t)(frame ? frame : 1)),
			         (unsigned long long)worst, frame);
			plat_log("M3 OK carpool_high=%u heap=%d", high,
			         sceKernelTotalFreeMemSize());
			pool_report();
		}
		gfx_swap();

		if (sceCtrlReadBufferPositive(&pad, 1) == 1) {
			if (!autotest) {
				t += 0.01f + (pad.Ly - 128) * 0.0002f;
				if (pad.Buttons & PSP_CTRL_START)
					g_quit = 1;
			}
		}

		if (autotest && frame >= frames)
			break;
	}

	plat_log("M3 EXIT at frame %d", frame);
}

/* M4: stream the 3x3 window for each pose, render it, screenshot the last
 * frame. Logs per-pose batch/pool figures and the overall frame time. */
static void run_world(World *w, Pose *poses, int npose, Cfg *cfg)
{
	int p, f, per = cfg->have_frames && cfg->frames > 0 ? cfg->frames : 8;
	int batches = 0, total = 0;
	uint64_t acc = 0, worst = 0, t0;
	char name[24];

	if (npose < 1) {
		/* No poses file: one look at the grid centre looking along +x. */
		poses[0].x = w->x0 + (w->gw * U2PK_CELL_M) * 0.5f;
		poses[0].y = w->y0 + (w->gh * U2PK_CELL_M) * 0.5f;
		poses[0].z = 3.0f;
		poses[0].yaw = 0.0f;
		npose = 1;
	}

	for (p = 0; p < npose; p++) {
		if (!world_load_window(w, poses[p].x, poses[p].y)) {
			plat_log("M4 pose %d: no cells at (%.1f, %.1f)", p, poses[p].x,
			         poses[p].y);
			continue;
		}
		for (f = 0; f < per; f++) {
			t0 = plat_us();
			gfx_begin();
			set_camera_world(poses[p].x, poses[p].y, poses[p].z, poses[p].yaw);
			sceGuFog(60, 150, 0xff304050);   /* 60-150 m, per M4 spec */
			sceGuEnable(GU_FOG);
			batches = world_draw(w);
			gfx_end();
			{
				uint64_t dt = plat_us() - t0;
				acc += dt;
				if (dt > worst) worst = dt;
				total++;
			}
			if (f == per - 1) {
				snprintf(name, sizeof name, "m4_p%d", p);
				shot_write(name);
				plat_log("M4 pose %d (%.1f,%.1f,%.1f yaw%.2f) cells=%d batch=%d "
				         "world_high=%u",
				         p, poses[p].x, poses[p].y, poses[p].z, poses[p].yaw,
				         w->nwin, batches, (unsigned)w->pw->high);
			}
			gfx_swap();
		}
	}

	plat_log("M4 frame_us avg=%llu max=%llu frames=%d",
	         (unsigned long long)(acc / (uint64_t)(total ? total : 1)),
	         (unsigned long long)worst, total);
	plat_log("M4 heap=%d", sceKernelTotalFreeMemSize());
	pool_report();
}

int main(int argc, char **argv)
{
	Cfg cfg;
	Pose poses[MAX_POSES];
	int autotest, npose;

	plat_init(argc, argv);
	plat_setup_callbacks();
	plat_log("OPENUG2 START dir=%s", plat_dir());

	if (pool_init_all() != 0) {
		plat_log("FAIL cannot carve the 17 MB pool");
		sceKernelExitGame();
		return 1;
	}
	autotest = read_cfg(&cfg);
	npose = read_world_cfg(&cfg, poses, MAX_POSES);   /* -1 => no world.cfg */
	plat_log("autotest %s mode=%s frames=%d shot=%d region=%s",
	         autotest ? "ON" : "OFF", npose >= 0 ? "world" : "car",
	         cfg.frames, cfg.shot_at, cfg.region);

	gfx_init();
	sceCtrlSetSamplingMode(0);

	if (npose >= 0) {
		World world;
		char dir[64];

		snprintf(dir, sizeof dir, "data/%s", cfg.region);
		if (!world_open(&world, pool_get(POOL_WORLD), pool_get(POOL_TEXTURES),
		                dir)) {
			plat_log("M4 FAIL could not open world %s (run psp_convert first)",
			         dir);
			sceKernelExitGame();
			return 1;
		}
		world_set_probe(cfg.probe);   /* before window loads (fold happens there) */
		if (cfg.probe)
			plat_log("PROBE bits=%d (default=fold/identity; 2=model-matrix 1=updmat 4=no-wheel)",
			         cfg.probe);
		run_world(&world, poses, npose, &cfg);
	} else {
		CarG player, ai;
		Pool *cars = pool_get(POOL_CARS);
		car_set_probe(cfg.probe);   /* must precede car_load (fold happens there) */
		if (cfg.probe)
			plat_log("PROBE bits=%d (default=fold/identity; 2=model-matrix 1=updmat 4=no-wheel)",
			         cfg.probe);
		if (!car_load(cars, CAR_REL, 0, &player)) {
			plat_log("M3 FAIL could not load " CAR_REL " (run psp_convert first)");
			sceKernelExitGame();
			return 1;
		}
		/* Second load = the AI LOD, resident at the same time (acceptance 5). */
		if (!car_load(cars, CAR_REL, 1, &ai))
			plat_log("M3 WARN AI LOD did not load (player still drawn)");
		run_car(&player, &ai, &cfg, autotest);
	}

	if (!autotest)
		sceKernelExitGame();
	return 0;
}
