#ifndef OPENUG2_RACE_H
#define OPENUG2_RACE_H
#include <stdint.h>
#include <math.h>
#include "race_rules.h"
enum { N2_RACE_UNKNOWN, N2_RACE_CIRCUIT, N2_RACE_SPRINT, N2_RACE_DRAG,
       N2_RACE_DRIFT, N2_RACE_STREETX, N2_RACE_URL };
typedef struct { char name[64]; uint32_t flags; int length, kind, downhill; } N2EventInfo;
static const char *n2_race_name(int kind) {
    static const char *names[]={"Unknown", "Circuit", "Sprint", "Drag", "Drift", "Street X", "URL"};
    return kind>=0 && kind<=N2_RACE_URL ? names[kind] : names[0];
}
static int race_has_traffic(int kind,int downhill) {
    return kind==N2_RACE_CIRCUIT || kind==N2_RACE_SPRINT || kind==N2_RACE_DRAG ||
           (kind==N2_RACE_DRIFT && downhill);
}
static int race_drag_failure(int blown,float wall,float vehicle) {
    return rr_drag_failure_value(blown,wall,vehicle);
}
/* [inferred] Legacy host velocity is metres/tick at 60 Hz; pure rules use SI. */
typedef RrDrift RaceDrift;
static void race_drift_step(RaceDrift *d,const float velocity[2],float heading,
                            float metres,int grounded,int collision,float dt) {
    if(!velocity)return;
    float si[2]={velocity[0]*RR_LEGACY_TICK_HZ,velocity[1]*RR_LEGACY_TICK_HZ};
    rr_drift_step(d,si,heading,metres,grounded,collision,dt);
}
static void race_drift_bank(RaceDrift *d) {rr_drift_bank(d);}
#endif
