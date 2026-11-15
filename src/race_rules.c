#include "race_rules.h"
int rr_drag_failure(int blown,float wall,float vehicle) {
    return rr_drag_failure_value(blown,wall,vehicle);
}
int rr_drag_grade(float rpm,float shift,float limiter,int from,int to) {
    return rr_drag_grade_value(rpm,shift,limiter,from,to);
}
float rr_drag_heat(float heat,float rpm,float limiter,float throttle,float dt) {
    return rr_drag_heat_value(heat,rpm,limiter,throttle,dt);
}
uint32_t rr_drag_grade_label(int grade) {
    switch(grade) {
    case RR_SHIFT_EARLY:return 0x2303d76cu; /* HUD_SHIFT_SHORT */
    case RR_SHIFT_GOOD:return 0x27d2dd45u; /* HUD_SHIFT_GOOD */
    case RR_SHIFT_PERFECT:return 0x0598b065u; /* HUD_SHIFT_PERFECT */
    case RR_SHIFT_LATE:return 0x27d75cd8u; /* HUD_SHIFT_OVER */
    default:return 0;
    }
}
int rr_points_place(const float *points,int count,int player) {
    if(!points || count<1 || count>RR_MAX_CARS || player<0 || player>=count)return 0;
    for(int k=0;k<count;k++)if(!isfinite(points[k]) || points[k]<0)return 0;
    int place=1;for(int k=0;k<count;k++)if(points[k]>points[player])place++;return place;
}
