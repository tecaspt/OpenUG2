/* Gameplay adapters for approved Stages 1–5 and body response. World query,
 * selectors and effective data are explicit. See docs/DRIVETRAIN.md. No IO. */
#ifndef OPENUG2_DRIVE_STAGE1_H
#define OPENUG2_DRIVE_STAGE1_H
#include "drivetrain.h"
#include "resistance.h"
#include "lateral.h"
#include "contact.h"
#include "physics.h"
#include "body_response.h"
#include "drive_session.h"
struct Stage1Drive;
typedef void (*StageBodyObserve)(void *user,const struct Stage1Drive *state,float h,int preparation);
typedef struct Stage1Drive {
    Drivetrain drivetrain;
    int dynamic_body,dynamic_initialized;BodyCar body_car;BodyMotion motion;BodyMode body_mode;
    unsigned inertia_selector;int body_race,impulses_suppressed;float body_gravity;
    DriveSession session; /* body_race retains the resolved lifecycle status, not event family. */
    StageBodyObserve body_observe;void *body_observe_user;
    int contact_ready;CtState contacts;CtQuery contact_query;void *contact_user;
    CtPose contact_pose;float contact_force[4][3],contact_moment[4][3];
    float contact_event,contact_v0,contact_s0; /* explicit U5-3 supplied host channels */
    int ready,reverse,prepared;
    ResCar resistance_car;
    ResInput resistance_input;
    ResBody body;
    ResAero aero;
    float prepared_wheel[4][2],prepared_moment[4];
    int lateral_ready;
    LatCar lateral_car;
    LatCommand steering;
    float wheel_angle[4],exported_forward,yaw_rate,yaw_momentum,inverse_yaw_inertia;
    /* Explicit host choices for U4-2/5: 60 Hz box histories; supplied angular damping. */
    float input_history[33],rate_history[9],previous_mapped,yaw_damping;
    unsigned input_cursor,rate_cursor;
    /* Supplied host channels; independent of existing arcade surface.drag. */
    float body_surface[4],rolling_surface[4];
    unsigned damage_mask;
    int state_code,alternate_movement,disable_stabilize;
    unsigned initial_lock_mask; /* explicit host reset fixture, not a retail default */
    float wheel_travel; /* mean signed angular travel during the last host tick */
    float wheel_angular_travel[4]; /* output-only per-wheel travel, including phase wraps */
} Stage1Drive;
/* Read only approved stock S1-1 fields via independent GLOBALB framing. */
int stage1_car_read(const unsigned char *data,long length,const char *name,DrvCar *out);
/* Career performance upgrades applied while reading (fractions 0..1 from cr_perf_fraction, levels 0..3);
 * NULL or all-zero = stock, identical to stage1_car_read. */
typedef struct DrvUpgrade { float engine; int engine_level; int gear_level; float tires; } DrvUpgrade;
int stage1_car_read_upgraded(const unsigned char *data,long length,const char *name,const DrvUpgrade *up,DrvCar *out);
/* Read only approved S2-1 stock fields from the same independent framing. */
int stage2_car_read(const unsigned char *data,long length,const char *name,ResCar *out);
/* Read only approved S3-1 stock fields via the same framing. */
int stage3_car_read(const unsigned char *data,long length,const char *name,BrkCar *out);
int stage5_car_read(const unsigned char *data,long length,const char *name,CtCar *out);
int stage5_body_car_read(const unsigned char *data,long length,const char *name,BodyCar *out);
/* Explicit selectors/suppression. Stock inertia and limits may be replaced by
 * a supplied effective BodyCar; configuration composition is external. */
int stage5_drive_body(Stage1Drive *state,const BodyCar *car,unsigned selector,int session_status,int suppress_impulses);
/* External host collision boundary: translate X with host origin correction
 * and seed only horizontal P/V; no wheel or angular reset. */
void stage5_body_host_correction(Stage1Drive *state,const float origin[3],const float velocity[2]);
int stage5_drive_contacts(Stage1Drive *state,const CtCar *car,CtQuery query,void *user);
void stage1_steering_frame(Stage1Drive *state,float raw,const ResBasis *basis);
float stage5_drive_step(Stage1Drive *state,float position[3],float velocity[2],float *heading,float *speed,float pedal,float steer,int handbrake,const PhysRideState *ride);
int stage4_car_read(const unsigned char *data,long length,const char *name,LatCar *out);
/* Configure geometry. Isolated prescribed/planar fixtures supply the inverse
 * yaw scalar/damping explicitly; BR composition uses its selected inertia.
 * The lower-stage planar fixture has a zero-centre mount-arm boundary. */
int stage4_drive_lateral(Stage1Drive *state,const LatCar *car,float inverse_inertia,float angular_damping);
/* Stage3 configuration is mandatory for gameplay service brakes. Initial lock
 * mask and relief switch are explicit host inputs (U3-2/3), not retail defaults. */
int stage3_drive_brakes(Stage1Drive *state,const BrkCar *car,unsigned initial_lock_mask,int relief_disabled);
/* Initial reduction is mandatory (U2-1). This host supplies ordinary mode,
 * G=1, no bonus/alternate mode/damage and unit surface channels explicitly. */
int stage1_drive_init(Stage1Drive *state,const DrvCar *car,const ResCar *resistance,float reduction);
/* Spawn/reset: retain car tuning, discard transient shaft/clutch state and
 * seed wheel rolling speed from host velocity (metres per 60 Hz tick). */
void stage1_drive_reset(Stage1Drive *state,const float velocity[2],float heading);
/* One host 60 Hz tick, subdivided per S1-2.2. Pedal: W=+1, S=-1.
 * S brakes forward then selects reverse at rest; W brakes reverse then auto.
 * Stage5 configuration selects geometric loads and handbrake/grip; body
 * configuration adds dynamic 3D response. The original
 * lower-stage supplied-load adapter remains for its independent fixtures. */
float stage1_drive_step(Stage1Drive *state,float position[3],float velocity[2],
    float *heading,float *speed,float pedal,float steer,int handbrake,
    const PhysSurface *surface,const PhysVehicle *vehicle,const PhysRideState *ride);
float stage1_drive_rpm(const Stage1Drive *state);
int stage1_drive_gear(const Stage1Drive *state); /* HUD: R=-1,N=0,forward=1..6 */
#endif
