#ifndef OPENUG2_POSTRACE_STATS_H
#define OPENUG2_POSTRACE_STATS_H
#include <math.h>
#include <stdint.h>
/* Measured simulation samples only. The host calls step once per racing tick;
 * lap_completed denotes an actual lap-line crossing, including the final lap. */
typedef struct PrRaceStats {
    int active,lap_race;
    uint64_t samples,laps;
    double elapsed,speed_integral,top_speed_kmh,lap_started,best_lap;
} PrRaceStats;
static inline void pr_stats_reset(PrRaceStats *s) {
    if(s)*s=(PrRaceStats){0};
}
static inline void pr_stats_start(PrRaceStats *s,int lap_race) {
    if(!s)return;
    pr_stats_reset(s);s->active=1;s->lap_race=!!lap_race;
}
static inline int pr_stats_lap_crossed(int lap_race,int crossed,int gate_before,int lap_before) {
    return lap_race && crossed && gate_before==0 && lap_before>0;
}
static inline int pr_stats_step(PrRaceStats *s,double speed_kmh,double dt,int lap_completed) {
    if(!s || !s->active || !isfinite(speed_kmh) || !isfinite(dt) || dt<=0 || dt>1 ||
       (lap_completed!=0 && lap_completed!=1))return 0;
    double speed=fabs(speed_kmh),elapsed=s->elapsed+dt,integral=s->speed_integral+speed*dt;
    if(!isfinite(elapsed) || !isfinite(integral))return 0;
    s->elapsed=elapsed;s->speed_integral=integral;s->samples++;
    if(speed>s->top_speed_kmh)s->top_speed_kmh=speed;
    if(s->lap_race && lap_completed) {
        double lap=s->elapsed-s->lap_started;
        if(!s->laps || lap<s->best_lap)s->best_lap=lap;
        s->laps++;s->lap_started=s->elapsed;
    }
    return 1;
}
static inline double pr_stats_average(const PrRaceStats *s) {
    return s && s->elapsed>0?s->speed_integral/s->elapsed:0;
}
#endif
