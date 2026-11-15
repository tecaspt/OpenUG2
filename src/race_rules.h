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
enum { RR_FAIL_NONE, RR_FAIL_ENGINE, RR_FAIL_CRASH, RR_FAIL_KNOCKOUT };
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
#endif
