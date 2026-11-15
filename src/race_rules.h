/* [inferred] Pure race policies, no SDL/GL or mutable global state.
 * Data supplies RPMs and language identities; grading windows/time policy
 * remain named, provisional constants until retail captures establish them. */
#ifndef OPENUG2_RACE_RULES_H
#define OPENUG2_RACE_RULES_H
#include <math.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
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

#define RR_MAX_URL_RACES 16
#define RR_DEFAULT_URL_ROUNDS 3
/* [inferred] Notify once per leader-completed lap. Tie loses greatest entrant ID. */
typedef struct { int count,remaining,last_lap;uint8_t alive[RR_MAX_CARS];int place[RR_MAX_CARS]; } RrKnockout;
int rr_knockout_init(RrKnockout *,int count);
int rr_knockout_lap(RrKnockout *,int completed_lap,const float progress[RR_MAX_CARS]);
int rr_knockout_winner(const RrKnockout *);
typedef struct {
    int count,races,completed;int event_id[RR_MAX_URL_RACES];
    uint32_t points[RR_MAX_CARS];unsigned wins[RR_MAX_CARS];uint16_t per_place[RR_MAX_CARS];
    uint16_t awarded[RR_MAX_URL_RACES][RR_MAX_CARS];
    uint8_t won[RR_MAX_URL_RACES][RR_MAX_CARS],placed[RR_MAX_URL_RACES][RR_MAX_CARS];
    unsigned finishes[RR_MAX_CARS];
} RrUrl;
void rr_url_default_points(uint16_t out[RR_MAX_CARS]);
/* [inferred] Deterministic QR fallback grouping; explicit sequences replace it when decoded. */
int rr_url_select_events(const int *available,int count,int first,int rounds,int out[RR_MAX_URL_RACES]);
int rr_url_init(RrUrl *,int count,const int *events,int races,const uint16_t points[RR_MAX_CARS]);
/* [inferred] places indexed by persistent entrant ID; zero = DNF. Transactional validation. */
int rr_url_record(RrUrl *,int event_id,const int places[RR_MAX_CARS]);
int rr_url_undo_last(RrUrl *);
int rr_url_place(const RrUrl *,int entrant);
int rr_url_next_event(const RrUrl *);
int rr_url_winner(const RrUrl *); /* -1 incomplete or tied; no invented tie-break */

#define RR_URL_FINISH_GRACE_SECONDS 30.0
#define RR_ROUND_TIME_CAP_SECONDS 86400.0
/* [inferred] Collect the whole field after the player finishes. Timeout marks
 * remaining entrants DNF; no fabricated progress-based finish order. */
typedef struct {
    int count,ready;double seconds,after_player;
    uint8_t done[RR_MAX_CARS],failed[RR_MAX_CARS];
    double finish_time[RR_MAX_CARS];float progress[RR_MAX_CARS];
} RrRound;
int rr_round_init(RrRound *,int count);
/* -1 invalid, 0 collecting, 1 ready. */
int rr_round_step(RrRound *,const int *finished,const int *failed,const float *progress,double dt);
int rr_round_places(const RrRound *,int places[RR_MAX_CARS]);

/* [inferred] Explicit selections; -1 preserves authored/host defaults. */
typedef struct { int opponents,traffic,catch_up,reverse,lap_knockout; } RrOptions;
#define RR_TRAFFIC_OFF 0
#define RR_TRAFFIC_LOW 4
#define RR_TRAFFIC_MEDIUM 8
#define RR_TRAFFIC_HIGH 16
#define RR_CATCHUP_RANGE_METRES 100.0f
#define RR_CATCHUP_MAX_ADJUSTMENT .20f
static inline int rr_options_valid_value(const RrOptions *r) {
    return r && r->opponents>=-1 && r->opponents<RR_MAX_CARS && r->traffic>=-1 && r->traffic<=3 &&
        r->catch_up>=-1 && r->catch_up<=1 && r->reverse>=0 && r->reverse<=1 &&
        r->lap_knockout>=0 && r->lap_knockout<=1;
}
static inline int rr_opponent_count_value(const RrOptions *r,int authored,int grid) {
    if(!rr_options_valid_value(r)||authored<0||grid<1)return -1;
    int n=r->opponents<0?authored:r->opponents;
    if(n>grid-1 || n>=RR_MAX_CARS)return -1;
    return n;
}
static inline float rr_catchup_fallback_value(float gap) {
    if(!isfinite(gap))return 0;
    return -fmaxf(-1,fminf(1,gap/RR_CATCHUP_RANGE_METRES))*RR_CATCHUP_MAX_ADJUSTMENT;
}
RrOptions rr_options_default(void);
int rr_options_valid(const RrOptions *);
int rr_opponent_count(const RrOptions *,int authored,int grid);
int rr_traffic_target(const RrOptions *,int host_default);
float rr_catchup_fallback(float gap);
/* [inferred] Reverse index; circuits retain the start/finish at index zero. */
static inline int rr_direction_index(int index,int count,int circuit,int reverse) {
    if(index<0||index>=count||count<2||(reverse!=0&&reverse!=1))return -1;
    return !reverse?index:circuit?(count-index)%count:count-1-index;
}

/* [inferred] Data transfer only. Caller chooses eligibility/crediting; no balances. */
enum { RR_REWARD_UNKNOWN=0,RR_REWARD_DATA=1,RR_REWARD_INFERRED=2 };
#define RR_REWARD_FIRST_CASH 1000u
#define RR_REWARD_SECOND_CASH 500u
#define RR_REWARD_THIRD_CASH 250u
#define RR_REWARD_FIRST_REPUTATION 100u
#define RR_REWARD_SECOND_REPUTATION 50u
#define RR_REWARD_THIRD_REPUTATION 25u
typedef struct { uint32_t cash[RR_MAX_CARS],reputation[RR_MAX_CARS];int provenance; } RrRewardTable;
typedef struct { uint32_t cash,reputation;int available,inferred; } RrReward;
/* [inferred] Explicit opt-in provisional policy, not a recovered retail table. */
RrRewardTable rr_reward_inferred_default(void);
RrReward rr_reward(const RrRewardTable *,int place,int failed);

#ifdef __cplusplus
}
#endif

#endif
