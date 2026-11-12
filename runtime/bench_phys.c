/* M2: what does one physics substep for one car cost?

 * The car set-up and the stepping call are the upstream drive-stage1-test's
 * (tools/drive_stage1_test.c), with fixed inputs, so the host build and the PSP
 * build must land on the same bits and the timing measures the same work. The
 * assertions that loop carries are left out: they are test scaffolding, and an
 * isfinite() per step is cost this milestone is trying to measure.
 *
 * Four cars (the player plus three copies) run 1000 substeps each, three times,
 * from an identical re-initialisation. The repeat is not decoration: the first
 * pass pays for cold caches and the log line prints all three so a reader can
 * see whether the reported figure is the steady one. Because every pass starts
 * from the same state, the printed final states are the same for all passes,
 * which is what makes them a comparison point against the host run.
 *
 * Built twice from this one file: as the `bench` EBOOT (make -f Makefile.bench,
 * same -Os flags as runtime/Makefile, so the number describes the shipped
 * runtime rather than an -O2 build) and natively with gcc. Without -DPSP the
 * platform calls below become clock_gettime and stdout.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "drive_stage1.h"
#include "physics.h"

#ifdef BENCH_DFCOUNT
/* Provided by dfcount.c, which the Makefile links only for make DFCOUNT=1. */
void dfc_snapshot(unsigned long out[11]);
#endif

#ifdef PSP
#include <pspuser.h>
#include <psprtc.h>
#include "platform.h"
#else /* host */

#include <stdint.h>
#include <time.h>

static uint64_t plat_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void plat_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

#endif /* PSP */

/* --- upstream fixtures, from tools/drive_stage1_test.c --- */

static DrvCar car_setup(float mass)
{
	DrvCar c;
	int k;

	memset(&c, 0, sizeof c);
	c.mass = mass;
	c.gear_count = 4;
	c.rear_share = .9f;
	c.final_front = c.final_rear = 4;
	c.clutch_k = .05f;
	c.clutch_rate = 2.5f;
	c.ratio[0] = -3;
	c.ratio[2] = 3;
	c.ratio[3] = 2;
	c.ratio[4] = 1.4f;
	c.ratio[5] = 1;
	c.idle_rpm = 1000;
	c.shift_rpm = 6000;
	c.max_rpm = 7000;
	c.engine_inertia = .0003f;
	for (k = 0; k < 4; k++)
		c.wheel_radius[k] = .32f;
	for (k = 0; k < 2; k++) {
		c.theta_max[k] = 12;
		c.mu_peak[k] = 1.3f;
		c.mu_slide[k] = 1.1f;
	}
	for (k = 0; k < 9; k++)
		c.torque_primary[k] = .3f;
	for (k = 0; k < 3; k++)
		c.engine_brake[k] = .03f;
	if (drv_car_finalize(&c) != 0)
		abort();
	return c;
}

static const BrkCar brakes = { .service = .45f, .handbrake = .75f, .rear_share = .3f };
static const ResCar resistance = { .drag = .0005f, .front = .12f, .rear = .18f, .limit_mph = 0 };

typedef struct {
	Stage1Drive drive;
	float p[3], v[2], h, s;
	int tick;
} Car;

static void car_init(Car *c, float mass)
{
	DrvCar dc = car_setup(mass);

	memset(c, 0, sizeof *c);
	if (stage1_drive_init(&c->drive, &dc, &resistance, 0) != 0)
		abort();
	if (stage3_drive_brakes(&c->drive, &brakes, 0, 0) != 0)
		abort();
}

/* One substep, the same call the upstream loop makes: full throttle, straight
 * ahead, no handbrake, all four wheels on the ground. */
static void car_step(Car *c)
{
	PhysRideState ride = { .contact_mask = 15 };

	stage1_drive_step(&c->drive, c->p, c->v, &c->h, &c->s, 1, 0, 0, NULL, NULL, &ride);
	++c->tick;
}

#define NCARS   4
#define SUBSTEPS 1000
#define PASSES  3

/* Substeps a 33.3 ms frame runs. physics.h bounds catch-up at eight ticks per
 * frame (the worst case, reached after a stall) while a steady 30 fps frame
 * runs two; the milestone budget is written against eight, so both are printed
 * and the gate is graded on the stricter one. */
#define SUBSTEPS_WORST 8
#define SUBSTEPS_STEADY 2

/*
 * data/bench.cfg switches the bench to `mode=long rounds=<n>`, which steps
 * SUBSTEPS_WORST substeps for NCARS cars, n times, and reports one figure per
 * simulated 33.3 ms frame.
 *
 * It exists because the only clock user code can read is not a stopwatch here:
 * sceRtcGetCurrentTick under PPSSPP headless advances with emulated frames, not
 * with host time, so the same 12 ms of physics reported ~7800 us per substep
 * both in a JIT run that took 11 s of real time and in an interpreter run that
 * took 261 s (out/m2/rtc-*.txt). Nothing measured inside the emulator can
 * therefore answer "does the physics fit in 33.3 ms of PSP time".
 *
 * The host process can answer it as an upper bound, by differencing the wall
 * time of a rounds=0 run against a rounds=N run: the subtraction cancels
 * emulator start-up, module load, the log flush and the frame count, leaving
 * the guest's physics work, timed on real hardware. That number is an upper
 * bound on PSP time because the emulator runs translated PSP code on a 2.4 GHz
 * four-wide out-of-order Cortex-A76, which retires this loop faster than the
 * PSP's in-order 333 MHz R4000 does - see PROGRESS.md M2 for the arithmetic
 * that turns the bound into the budget verdict.
 */
static int cfg_long(long *rounds)
{
	char line[128];
	char mode[16];
	char path[256];
	FILE *f;

	*rounds = -1;
#ifdef PSP
	/* Relative paths resolve against the emulator's or firmware's working
	 * directory, which is not the game folder, so anchor on the module path. */
	snprintf(path, sizeof path, "%s/data/bench.cfg", plat_dir());
#else
	snprintf(path, sizeof path, "data/bench.cfg");
#endif
	if (!(f = fopen(path, "r")))
		return 0;
	if (!fgets(line, sizeof line, f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	if (sscanf(line, "mode=%15s rounds=%ld", mode, rounds) != 2)
		return 0;
	if (strcmp(mode, "long") != 0)
		return 0;
	if (*rounds < 0)
		*rounds = 0;
	return 1;
}

int main(int argc, char **argv)
{
	Car cars[NCARS];
	long rounds;
	int pass, i, k;
	uint64_t t0, t1;
	double us;

	(void)argc; (void)argv;
#ifdef PSP
	plat_init(argc, argv);
	plat_setup_callbacks();
	plat_log("BENCH start dir=%s", plat_dir());
	/* The RTC tick is the only clock user code can read; show its resolution so
	 * a surprising timing figure can be read for what it is. */
	plat_log("BENCH clock_res=%u", (unsigned)sceRtcGetTickResolution());
#else
	plat_log("BENCH host build");
#endif
	plat_log("BENCH cars=%d substeps=%d passes=%d", NCARS, SUBSTEPS, PASSES);

	if (cfg_long(&rounds)) {
		long r;
		double per_step = (double)rounds * SUBSTEPS_WORST * NCARS;

#ifdef BENCH_DFCOUNT
		unsigned long c0[11], c1[11];

		dfc_snapshot(c0);
#endif
		for (i = 0; i < NCARS; i++)
			car_init(&cars[i], 1.2f);
		t0 = plat_us();
		for (r = 0; r < rounds; r++)
			for (k = 0; k < SUBSTEPS_WORST; k++)
				for (i = 0; i < NCARS; i++)
					car_step(&cars[i]);
		t1 = plat_us();
		plat_log("PHYS long rounds=%ld substeps_per_frame=%d cars=%d "
		         "us_total=%.1f us_per_frame=%.3f",
		         rounds, SUBSTEPS_WORST, NCARS, (double)(t1 - t0),
		         rounds ? (double)(t1 - t0) / (double)rounds : 0.0);
#ifdef BENCH_DFCOUNT
		dfc_snapshot(c1);
		if (per_step > 0)
			plat_log("PHYS dfops_per_car_substep mul=%.1f add=%.1f sub=%.1f "
			         "div=%.1f cmp=%.1f df2sf=%.1f sf2df=%.1f i2d=%.1f d2i=%.1f "
			         "cosf=%.1f total=%.1f",
			         (c1[0] - c0[0]) / per_step, (c1[1] - c0[1]) / per_step,
			         (c1[2] - c0[2]) / per_step, (c1[3] - c0[3]) / per_step,
			         (c1[4] - c0[4]) / per_step, (c1[5] - c0[5]) / per_step,
			         (c1[6] - c0[6]) / per_step, (c1[7] - c0[7]) / per_step,
			         (c1[8] - c0[8]) / per_step, (c1[9] - c0[9]) / per_step,
			         (c1[10] - c0[10]) / per_step);
#endif
		goto done;
	}

	for (pass = 0; pass < PASSES; pass++) {
		for (i = 0; i < NCARS; i++)
			car_init(&cars[i], 1.2f);
		t0 = plat_us();
		for (k = 0; k < SUBSTEPS; k++)
			for (i = 0; i < NCARS; i++)
				car_step(&cars[i]);
		t1 = plat_us();
		us = (double)(t1 - t0) / ((double)NCARS * SUBSTEPS);
		plat_log("PHYS pass%d us_per_substep_per_car=%.3f", pass, us);
		if (pass == PASSES - 1) {
			plat_log("PHYS us_per_substep_per_car=%.3f", us);
			plat_log("PHYS frame_us_worst8=%.1f frame_us_steady2=%.1f",
			         us * NCARS * SUBSTEPS_WORST, us * NCARS * SUBSTEPS_STEADY);
			plat_log("PHYS car0 speed_kmh=%.3f gear=%d rpm=%.1f",
			         PHYS_KMH(cars[0].s), stage1_drive_gear(&cars[0].drive),
			         stage1_drive_rpm(&cars[0].drive));
		}
	}

done:
	/* %.9g prints a float exactly: 9 significant digits are enough for any
	 * float32 and no more, so a difference in the last bit shows up. */
	for (i = 0; i < NCARS; i++)
		plat_log("PHYS car%d p=%.9g,%.9g,%.9g v=%.9g,%.9g yaw=%.9g",
		         i, (double)cars[i].p[0], (double)cars[i].p[1], (double)cars[i].p[2],
		         (double)cars[i].v[0], (double)cars[i].v[1], (double)cars[i].h);

#ifdef PSP
	plat_log("BENCH done");
	/* ms0 writes are deferred and commit later, out of the buffer they were
	 * handed; give them time while that buffer (static, see plat_log) is still
	 * intact, then leave. See docs/PPSSPP.md (d). */
	sceKernelDelayThread(500000);
	sceKernelExitGame();
#endif
	return 0;
}
