/* M5 drive spine. See drive.h for the contract. No IO, no clock, no file
 * access, no mutable file-scope state: every byte of run-to-run state lives in
 * the caller's Drive, which drive_init fully re-seeds, so a PSP replay is
 * reproducible to the bit from a cold start.
 *
 * The physics entry points are exactly the ones the production loop uses
 * (main.c: stage1_drive_step on the prior mask, then ride_gather ->
 * phys_ride_init/phys_ride_step -> carpos[2]=g_ride.z). We reuse the upstream
 * sprung-ride integrator rather than a hand-rolled mask so the contact decision
 * - which faces are reachable, which carry load, when the car is airborne - is
 * the game's, not our approximation of it. */
#include <string.h>
#include <math.h>
#include "drive.h"

/* --- M2 synthetic car fixture (runtime/bench_phys.c, from the upstream
 *     drive-stage1-test). Reproducible host<->PSP with no retail parse, so the
 *     0.5 m gate compares arithmetic, not GLOBALB decoding. --- */
static int car_setup(DrvCar *c, float mass)
{
	int k;

	memset(c, 0, sizeof *c);
	c->mass = mass;
	c->gear_count = 4;
	c->rear_share = .9f;
	c->final_front = c->final_rear = 4;
	c->clutch_k = .05f;
	c->clutch_rate = 2.5f;
	c->ratio[0] = -3;
	c->ratio[2] = 3;
	c->ratio[3] = 2;
	c->ratio[4] = 1.4f;
	c->ratio[5] = 1;
	c->idle_rpm = 1000;
	c->shift_rpm = 6000;
	c->max_rpm = 7000;
	c->engine_inertia = .0003f;
	for (k = 0; k < 4; k++)
		c->wheel_radius[k] = .32f;
	for (k = 0; k < 2; k++) {
		c->theta_max[k] = 12;
		c->mu_peak[k] = 1.3f;
		c->mu_slide[k] = 1.1f;
	}
	for (k = 0; k < 9; k++)
		c->torque_primary[k] = .3f;
	for (k = 0; k < 3; k++)
		c->engine_brake[k] = .03f;
	return drv_car_finalize(c);
}

static const BrkCar g_brakes = { .service = .45f, .handbrake = .75f,
	                             .rear_share = .3f };
static const ResCar g_resistance = { .drag = .0005f, .front = .12f,
	                             .rear = .18f, .limit_mph = 0 };

/* Body-space wheel offsets, front-left, front-right, rear-left, rear-right. */
static void wheel_offsets(float ax[4], float ay[4])
{
	float fx = DRIVE_WHEELBASE * 0.5f, rx = -DRIVE_WHEELBASE * 0.5f;
	float ty = DRIVE_TRACK * 0.5f;

	ax[0] = ax[1] = fx;
	ax[2] = ax[3] = rx;
	ay[0] = ay[2] = ty;
	ay[1] = ay[3] = -ty;
}

/* Fill one frame of support from the caller's ground query, at world pos/hdg.
 * The selection window is anchored to each wheel's CURRENT contact plane, not a
 * fixed band off the body: a candidate is valid only within
 * [wheel_z - reach_down, wheel_z + PHYS_RIDE_REACH_UP]. This is what refuses an
 * overpass above the road (physics.h "the deck 7.53 m overhead is refused
 * outright") that a search-from-body+2m would wrongly latch onto once driving. */
static void gather(Drive *d, DriveGround ground, void *user, float old_heading,
                   PhysRideSupport *sup)
{
	float ax[4], ay[4];
	float co, so, down, gz, gn[3];
	int k;

	wheel_offsets(ax, ay);
	memcpy(sup->ax, ax, sizeof ax);
	memcpy(sup->ay, ay, sizeof ay);
	/* Ground-face gradients only ever *widen* the integrator's attitude clamp
	 * (PHYS_RIDE_MAXTILT already covers every drivable replay grade), so the
	 * replay path passes zero rather than an atan2f the M2 bit-equality never
	 * exercised. On authored terrain steep enough to need it, compute it. */
	sup->pitch_limit = 0.0f;
	sup->roll_limit = 0.0f;

	co = cosf(d->heading);
	so = sinf(d->heading);
	down = d->ride_ready ? phys_ride_reach_down(&d->ride, 1.0f / 60.0f)
	                    : PHYS_RIDE_REACH_DOWN;
	(void)old_heading;

	for (k = 0; k < 4; k++) {
		float wx = d->pos[0] + co * ax[k] - so * ay[k];
		float wy = d->pos[1] + so * ax[k] + co * ay[k];
		/* Prior-frame wheel contact plane; zero on the very first gather. */
		float wz = d->ride_ready ? phys_ride_wheel_z(&d->ride, sup, k)
		                         : d->pos[2];
		sup->valid[k] = 0;
		sup->vz[k] = 0.0f;
		if (!ground(user, wx, wy, wz + PHYS_RIDE_REACH_UP, &gz, gn))
			continue;
		if (gz < wz - down)
			continue;              /* below the droop window: hanging clear */
		sup->z[k] = gz;
		sup->valid[k] = 1;
		sup->vz[k] = phys_ride_support_vz(gn, d->vel, old_heading, d->heading,
		                                 ax[k], ay[k], 1.0f / 60.0f);
	}
}

int drive_init(Drive *d, DriveGround ground, void *user,
               float x, float y, float yaw, float spawn_high)
{
	DrvCar car;
	PhysRideSupport sup;

	if (car_setup(&car, 1.2f) != 0)
		return -1;
	memset(d, 0, sizeof *d);
	if (stage1_drive_init(&d->s1, &car, &g_resistance, 0) != 0)
		return -1;
	if (stage3_drive_brakes(&d->s1, &g_brakes, 0, 0) != 0)
		return -1;

	d->pos[0] = x;
	d->pos[1] = y;
	d->heading = yaw;
	d->ride_ready = 0;

	/* Drop the body plane onto the first surface below the spawn eye. The
	 * converter's poses are camera heights (road+2); pos[2]=ride.z is the body
	 * reference, which sits AT the road under the wheels, not 2 m up. The
	 * per-wheel gather window is only +/- reach wide (so an overpass cannot
	 * latch once driving), which is too tight to close that 2 m from cold - so
	 * this one-time centre probe (the loose highest-below rule, safe because a
	 * spawn eye is on open road) parks the body on the surface, and only then
	 * does the tight per-wheel fit see it. */
	{
		float gz, gn[3];

		d->pos[2] = spawn_high;
		if (ground(user, x, y, spawn_high, &gz, gn))
			d->pos[2] = gz;
	}

	/* Seed the pose from the surface reachable under each wheel: level unless
	 * the supports fit one plane, airborne if nothing is reachable. A supported
	 * spawn starts grounded so the first drive tick has a real contact mask,
	 * not the M2 hard-wired 15. */
	memset(&sup, 0, sizeof sup);
	gather(d, ground, user, yaw, &sup);
	phys_ride_init(&d->ride, &sup);
	d->ride_ready = 1;
	d->pos[2] = d->ride.z;
	d->contact_mask = d->ride.contact_mask;
	if (d->contact_mask)
		d->contact_frames = 1;
	else
		d->air_frames = 1;
	return 0;
}

void drive_step(Drive *d, DriveGround ground, void *user,
                float pedal, float steer, int handbrake)
{
	PhysRideSupport sup;
	float old_heading = d->heading;

	/* 1. Drive on the PRIOR frame's contact mask (the production one-frame lag).
	 *    This is the only ground the drive model sees - through the mask - and
	 *    it moves pos[0]/pos[1]/heading/speed; pos[2] is untouched here. */
	stage1_drive_step(&d->s1, d->pos, d->vel, &d->heading, &d->speed, pedal,
	                  steer, handbrake, NULL, NULL, &d->ride);

	/* 2. Gather reachable support at the NEW XY. */
	memset(&sup, 0, sizeof sup);
	gather(d, ground, user, old_heading, &sup);

	/* 3. Integrate the sprung body once; it owns pos[2] (heave), pitch, roll and
	 *    the next frame's contact mask. */
	phys_ride_step(&d->ride, &sup, 1.0f / 60.0f);
	d->pos[2] = d->ride.z;
	d->contact_mask = d->ride.contact_mask;
	if (d->contact_mask)
		++d->contact_frames;
	else
		++d->air_frames;
}
