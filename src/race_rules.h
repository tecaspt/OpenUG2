/* [inferred] Pure race policies, no SDL/GL or mutable global state.
 * Data supplies RPMs and language identities; grading windows/time policy
 * remain named, provisional constants until retail captures establish them. */
#ifndef OPENUG2_RACE_RULES_H
#define OPENUG2_RACE_RULES_H
#include <math.h>
#include <stdint.h>
#define RR_DRAG_BLOW_SECONDS 1.5f
#define RR_DRAG_COOL_PER_SECOND .5f
#define RR_DRAG_PERFECT_RPM 150.0f
#define RR_DRAG_GOOD_EARLY_RPM 600.0f
#define RR_DRAG_GOOD_LATE_RPM 350.0f
#define RR_SHIFT_FEEDBACK_SECONDS 1.0f
#define RR_DRAG_WALL_FAILURE_IMPULSE 8.0f
#define RR_DRAG_VEHICLE_FAILURE_IMPULSE 0.0f
enum { RR_FAIL_NONE, RR_FAIL_ENGINE, RR_FAIL_CRASH, RR_FAIL_KNOCKOUT, RR_FAIL_INVALID_RULES };
enum { RR_SHIFT_NONE, RR_SHIFT_EARLY, RR_SHIFT_GOOD, RR_SHIFT_PERFECT, RR_SHIFT_LATE };
/* [inferred] Inline primitives retain physics-only test/link compatibility.
 * External wrappers below use these same canonical policies. */
static inline int rr_drag_grade_value(float rpm,float shift,float limiter,int from,int to) {
    if(!isfinite(rpm)||rpm<0||!isfinite(shift)||!isfinite(limiter)||shift<=0||limiter<=shift||
       from<1||to>6||to!=from+1)return RR_SHIFT_NONE;
    float error=rpm-shift;
    if(fabsf(error)<=RR_DRAG_PERFECT_RPM)return RR_SHIFT_PERFECT;
    if(error < -RR_DRAG_GOOD_EARLY_RPM)return RR_SHIFT_EARLY;
    if(error > RR_DRAG_GOOD_LATE_RPM || rpm>=limiter)return RR_SHIFT_LATE;
    return RR_SHIFT_GOOD;
}
static inline float rr_drag_heat_value(float heat,float rpm,float limiter,float throttle,float dt) {
    if(!isfinite(heat)||!isfinite(rpm)||!isfinite(limiter)||limiter<=0||!isfinite(throttle)||!isfinite(dt)||dt<=0)return heat;
    return fminf(1,fmaxf(0,heat+(rpm>=limiter && throttle>0?
        dt/RR_DRAG_BLOW_SECONDS:-dt*RR_DRAG_COOL_PER_SECOND)));
}
static inline int rr_drag_failure_value(int blown,float wall,float vehicle) {
    return blown?RR_FAIL_ENGINE:wall>RR_DRAG_WALL_FAILURE_IMPULSE ||
        vehicle>RR_DRAG_VEHICLE_FAILURE_IMPULSE?RR_FAIL_CRASH:RR_FAIL_NONE;
}
int rr_drag_failure(int blown,float wall,float vehicle);
int rr_drag_grade(float rpm,float shift_rpm,float limiter_rpm,int from_gear,int to_gear);
float rr_drag_heat(float heat,float rpm,float limiter_rpm,float throttle,float dt);
/* [data] Installed English label keys; zero for no feedback. */
uint32_t rr_drag_grade_label(int grade);
#define RR_MAX_CARS 6
#define RR_LEGACY_TICK_HZ 60.0f
#define RR_DRIFT_MIN_KMH 35.0f
#define RR_DRIFT_MIN_ANGLE_DEG 10.0f
#define RR_DRIFT_MAX_ANGLE_DEG 80.0f
#define RR_DRIFT_POINTS_PER_METRE 100.0f
#define RR_DRIFT_LEVEL_POINTS 1000.0f
#define RR_DRIFT_MAX_MULTIPLIER 5
#define RR_DRIFT_BANK_SECONDS 1.0f
#define RR_SCORE_LIMIT 1000000000.0f
/* [inferred] Scoring inputs use SI velocity (m/s), measured metres and seconds. */
typedef struct {
    float bank,chain,quiet,angle; /* existing RaceDrift prefix */
    float raw_chain,multiplier;
    unsigned banked_chains,cancelled_chains;
} RrDrift;
/* [inferred] Header implementation keeps the existing race.h API link-compatible
 * for independently built world/AI test targets. One canonical scoring body. */
static inline void rr_drift_bank(RrDrift *d) {
    if(!d)return;
    if(d->chain>0){d->bank=fminf(RR_SCORE_LIMIT,d->bank+d->chain);d->banked_chains++;}
    d->chain=d->raw_chain=d->quiet=0;d->multiplier=1;
}
static inline void rr_drift_step(RrDrift *d,const float velocity[2],float heading,
        float metres,int grounded,int collision,float dt) {
    if(!d || !velocity || !isfinite(heading) || !isfinite(metres) ||
       !isfinite(velocity[0]) || !isfinite(velocity[1]) || !isfinite(dt) || dt<=0)return;
    if(d->chain<=0 && d->raw_chain<=0)d->multiplier=1;
    float forward=velocity[0]*cosf(heading)+velocity[1]*sinf(heading);
    float side=-velocity[0]*sinf(heading)+velocity[1]*cosf(heading);
    float angle=fabsf(atan2f(side,forward))*57.295779513f;d->angle=angle;
    if(collision) {
        if(d->chain>0)d->cancelled_chains++;
        d->chain=d->raw_chain=d->quiet=0;d->multiplier=1;return;
    }
    if(grounded && forward>0 && hypotf(velocity[0],velocity[1])*3.6f>RR_DRIFT_MIN_KMH &&
       angle>RR_DRIFT_MIN_ANGLE_DEG && angle<RR_DRIFT_MAX_ANGLE_DEG && metres>0) {
        /* Piecewise integration makes multiplier crossings independent of tick size. */
        float add=metres*sinf(angle*.01745329252f)*RR_DRIFT_POINTS_PER_METRE;
        add=fminf(RR_SCORE_LIMIT,add);
        while(add>0 && d->raw_chain<RR_SCORE_LIMIT) {
            int level=1+(int)(d->raw_chain/RR_DRIFT_LEVEL_POINTS);
            if(level>RR_DRIFT_MAX_MULTIPLIER)level=RR_DRIFT_MAX_MULTIPLIER;
            float boundary=level<RR_DRIFT_MAX_MULTIPLIER?level*RR_DRIFT_LEVEL_POINTS:RR_SCORE_LIMIT;
            float part=fminf(add,boundary-d->raw_chain);
            if(part<=0)break;
            d->raw_chain=fminf(RR_SCORE_LIMIT,d->raw_chain+part);
            d->chain=fminf(RR_SCORE_LIMIT,d->chain+part*level);add-=part;
        }
        d->multiplier=fminf(RR_DRIFT_MAX_MULTIPLIER,1+floorf(d->raw_chain/RR_DRIFT_LEVEL_POINTS));
        d->quiet=0;
    } else {
        d->quiet=fminf(RR_DRIFT_BANK_SECONDS,d->quiet+dt);
        if(d->quiet>=RR_DRIFT_BANK_SECONDS-1e-6f)rr_drift_bank(d);
    }
}
/* [inferred] Competition ranks: equal points share a rank, never use road position. */
int rr_points_place(const float *points,int count,int player);


#endif
