/* M5 drive spine: step one car on a real ground surface, identically on the PSP
 * runtime and the host replay, so the "same physics, same COLLIDE, final pose
 * within 0.5 m" acceptance compares one implementation run twice rather than two
 * look-alike implementations.
 *
 * It is IO-free and clock-free on purpose: the only thing it reaches outside
 * itself is a caller-supplied ground query. That keeps the PSP-run-twice
 * bit-identical requirement dependent only on the arithmetic, not on a wall
 * clock, file seek order, or a mutable static that could carry a previous run's
 * state. The caller owns the ground (the runtime reads it from the streamed
 * region's COLLIDE blobs; the host reads the same bytes straight off disk).
 *
 * Ground enters the arcade drive model through exactly one channel: the drive
 * stage reads only PhysRideState.contact_mask (see drive_stage1.c:
 * `unsigned mask=ride?ride->contact_mask:0`), one bit per wheel, which it turns
 * into per-wheel normal load and grip. The sprung-ride integrator
 * (phys_ride_init/phys_ride_step) is what upstream runs to turn per-wheel ground
 * heights into that mask plus the body heave/pitch/roll; we run the same
 * integrator so the mask matches the game's, and so pos[2] tracks the road. */
#ifndef OPENUG2_SHARED_DRIVE_H
#define OPENUG2_SHARED_DRIVE_H

#include <stdint.h>
#include "drive_stage1.h"

/* Highest surface at or below z_from under (x,y). Fills *z_out and the unit
 * up-facing normal n_out[3]. Returns 1 on a hit, 0 on a miss (nothing reachable
 * below z_from in the caller's search window). This is the same contract as
 * u2pk_ground_at; the caller wraps it in whatever world query it has. */
typedef int (*DriveGround)(void *user, float x, float y, float z_from,
                           float *z_out, float n_out[3]);

/* Fixed fleet-derived wheel footprint (metres, body space: +x forward, +y
 * left). The integrator re-centres these on their own centroid, so only the
 * wheelbase and track matter, not the absolute axle x. Kept identical on both
 * builds because the geometry, not retail data, drives the contact solve. */
#define DRIVE_WHEELBASE  2.6608f   /* PHYS_FLEET_WHEELBASE */
#define DRIVE_TRACK      1.5100f   /* PHYS_FLEET_TRACK     */

typedef struct {
	Stage1Drive  s1;
	PhysRideState ride;
	float pos[3];      /* body reference plane in world metres (pos[2]=ride.z) */
	float vel[2];      /* metres per 60 Hz tick */
	float heading;     /* radians about +Z */
	float speed;       /* signed forward speed, m/tick (drive export) */
	int   ride_ready;  /* 0 until the first support gather seeds the pose */
	unsigned contact_mask;   /* last tick's grounded-wheel bitmask (for HUD/log) */
	unsigned long contact_frames; /* ticks with at least one wheel in contact */
	unsigned long air_frames;     /* ticks with none */
} Drive;

/* Seed one car from the M2 synthetic fixture (mass 1.2 t) at world (x,y) with
 * yaw `yaw`, dropping it onto the ground the query finds below spawn_high
 * (metres, comfortably above the road - the converter poses sit road+2). The
 * pose is level unless the four wheel supports fit one plane, exactly as a live
 * placement does. Returns 0 on success, -1 if the car fixture itself failed to
 * initialise (a build error, not a data condition). */
int drive_init(Drive *d, DriveGround ground, void *user,
               float x, float y, float yaw, float spawn_high);

/* Advance one 60 Hz tick, in the game's frame order:
 *   1. run the drive stage on the PRIOR frame's contact_mask (the one-frame lag
 *      the production loop has); this is the only place the drive model reads
 *      ground, and only through the mask;
 *   2. at the new XY, gather each wheel's reachable support through the tight
 *      window anchored to the live wheel plane (PHYS_RIDE_REACH_UP above,
 *      phys_ride_reach_down below) - never a fixed multi-metre band, which is
 *      how a stacked overpass latches onto the car (M130 / physics.h);
 *   3. integrate the sprung body once (or seed it on the first tick) and write
 *      pos[2] = ride.z, which the drive stage never touches.
 * pedal in [-1,1] (W=+1,S=-1), steer in [-1,1] (left positive), handbrake 0/1. */
void drive_step(Drive *d, DriveGround ground, void *user,
                float pedal, float steer, int handbrake);

#endif /* OPENUG2_SHARED_DRIVE_H */
