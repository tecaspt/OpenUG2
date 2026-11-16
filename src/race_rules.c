#include "race_rules.h"
#include <string.h>
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
    case RR_SHIFT_LATE:return 0x27d75cd8u; /* HUD_SHIFT_OVER ("Over Rev!") */
    default:return 0;
    }
}
int rr_points_place(const float *points,int count,int player) {
    if(!points || count<1 || count>RR_MAX_CARS || player<0 || player>=count)return 0;
    for(int k=0;k<count;k++)if(!isfinite(points[k]) || points[k]<0)return 0;
    int place=1;for(int k=0;k<count;k++)if(points[k]>points[player])place++;return place;
}
int rr_knockout_init(RrKnockout *r,int count) {
    if(!r || count<1 || count>RR_MAX_CARS)return 0;
    memset(r,0,sizeof *r);r->count=r->remaining=count;
    for(int k=0;k<count;k++)r->alive[k]=1;
    if(count==1)r->place[0]=1;
    return 1;
}
int rr_knockout_lap(RrKnockout *r,int lap,const float progress[RR_MAX_CARS]) {
    if(!r || !progress || r->count<1 || r->count>RR_MAX_CARS || r->remaining<=1 || lap<=r->last_lap)return -1;
    int last=-1;
    for(int k=0;k<r->count;k++)if(r->alive[k]) {
        if(!isfinite(progress[k]))return -1;
        if(last<0 || progress[k]<=progress[last])last=k;
    }
    if(last<0)return -1;
    r->last_lap=lap;r->alive[last]=0;r->place[last]=r->remaining--;
    if(r->remaining==1)for(int k=0;k<r->count;k++)if(r->alive[k])r->place[k]=1;
    return last;
}
void rr_url_default_points(uint16_t out[RR_MAX_CARS]) {
    const uint16_t provisional[RR_MAX_CARS]={8,6,4,3,2,1};
    if(out)memcpy(out,provisional,sizeof provisional);
}
int rr_url_init(RrUrl *r,int count,const int *events,int races,const uint16_t points[RR_MAX_CARS]) {
    if(!r || !events || count<1 || count>RR_MAX_CARS || races<1 || races>RR_MAX_URL_RACES)return 0;
    for(int k=0;k<races;k++)if(events[k]<=0)return 0;
    memset(r,0,sizeof *r);r->count=count;r->races=races;memcpy(r->event_id,events,(size_t)races*sizeof *events);
    if(points)memcpy(r->per_place,points,sizeof r->per_place);else rr_url_default_points(r->per_place);
    return 1;
}
int rr_url_record(RrUrl *r,int event_id,const int places[RR_MAX_CARS]) {
    if(!r || !places || r->count<1 || r->count>RR_MAX_CARS || r->races<1 || r->races>RR_MAX_URL_RACES || r->completed<0 || r->completed>=r->races || event_id!=r->event_id[r->completed])return 0;
    unsigned used=0;
    for(int k=0;k<r->count;k++) {
        int p=places[k];if(p<0 || p>r->count || (p && (used&(1u<<p))))return 0;
        if(p)used|=1u<<p;
    }
    for(int k=0;k<r->count;k++) {
        uint16_t add=places[k]?r->per_place[places[k]-1]:0;
        r->awarded[r->completed][k]=add;r->won[r->completed][k]=places[k]==1;
        r->placed[r->completed][k]=places[k]>0;
        r->points[k]+=add;r->wins[k]+=places[k]==1;r->finishes[k]+=places[k]>0;
    }
    r->completed++;return 1;
}
int rr_url_place(const RrUrl *r,int entrant) {
    if(!r || r->count<1 || r->count>RR_MAX_CARS || entrant<0 || entrant>=r->count)return 0;
    int place=1;for(int k=0;k<r->count;k++)if(r->points[k]>r->points[entrant])place++;return place;
}
int rr_url_next_event(const RrUrl *r){return r && r->completed>=0 && r->completed<r->races && r->races<=RR_MAX_URL_RACES?r->event_id[r->completed]:0;}

int rr_knockout_winner(const RrKnockout *r) {
    if(!r || r->count<1 || r->count>RR_MAX_CARS || r->remaining!=1)return -1;
    for(int k=0;k<r->count;k++)if(r->alive[k])return k;
    return -1;
}
int rr_url_select_events(const int *ids,int count,int first,int rounds,int out[RR_MAX_URL_RACES]) {
    if(!ids || !out || count<1 || rounds<1 || rounds>RR_MAX_URL_RACES)return 0;
    int start=-1;
    for(int i=0;i<count;i++) {
        if(ids[i]<=0)return 0;
        for(int j=0;j<i;j++)if(ids[i]==ids[j])return 0;
        if(ids[i]==first)start=i;
    }
    if(start<0)return 0;
    int n=rounds<count?rounds:count;
    for(int i=0;i<n;i++)out[i]=ids[(start+i)%count];
    return n;
}
int rr_url_undo_last(RrUrl *r) {
    if(!r || r->count<1 || r->count>RR_MAX_CARS || r->completed<1 || r->completed>r->races || r->races>RR_MAX_URL_RACES)return 0;
    int round=r->completed-1;
    for(int k=0;k<r->count;k++) {
        if(r->points[k]<r->awarded[round][k] || r->wins[k]<r->won[round][k] || r->finishes[k]<r->placed[round][k])return 0;
    }
    for(int k=0;k<r->count;k++) {
        r->points[k]-=r->awarded[round][k];r->wins[k]-=r->won[round][k];
        r->finishes[k]-=r->placed[round][k];
        r->awarded[round][k]=0;r->won[round][k]=r->placed[round][k]=0;
    }
    r->completed--;return 1;
}
int rr_url_winner(const RrUrl *r) {
    if(!r || r->count<1 || r->count>RR_MAX_CARS || r->races<1 || r->completed!=r->races)return -1;
    int winner=0,tied=0;
    for(int k=1;k<r->count;k++) {
        if(r->points[k]>r->points[winner]){winner=k;tied=0;}
        else if(r->points[k]==r->points[winner])tied=1;
    }
    return tied || r->points[winner]==0?-1:winner;
}

int rr_round_init(RrRound *r,int count) {
    if(!r || count<1 || count>RR_MAX_CARS)return 0;
    memset(r,0,sizeof *r);r->count=count;return 1;
}
int rr_round_step(RrRound *r,const int *finished,const int *failed,const float *progress,double dt) {
    if(!r || !finished || !failed || !progress || r->count<1 || r->count>RR_MAX_CARS ||
       !isfinite(dt) || dt<=0)return -1;
    if(r->ready)return 1;
    for(int k=0;k<r->count;k++)if(!isfinite(progress[k]))return -1;
    r->seconds=fmin(RR_ROUND_TIME_CAP_SECONDS,r->seconds+dt);
    for(int k=0;k<r->count;k++) {
        r->progress[k]=progress[k];
        if(!r->done[k] && (finished[k] || failed[k])) {
            r->done[k]=1;r->failed[k]=failed[k]!=0;r->finish_time[k]=r->seconds;
        }
    }
    if(r->done[0])r->after_player+=dt;
    int pending=0;
    for(int k=0;k<r->count;k++)if(!r->done[k]) {
        if(r->after_player>=RR_URL_FINISH_GRACE_SECONDS){r->done[k]=r->failed[k]=1;}
        else pending=1;
    }
    r->ready=!pending;return r->ready;
}
int rr_round_places(const RrRound *r,int places[RR_MAX_CARS]) {
    if(!r || !places || !r->ready || r->count<1 || r->count>RR_MAX_CARS)return 0;
    for(int k=0;k<r->count;k++) {
        if(r->failed[k]){places[k]=0;continue;}
        int rank=1;
        for(int j=0;j<r->count;j++)if(!r->failed[j] &&
           (r->finish_time[j]<r->finish_time[k] || (r->finish_time[j]==r->finish_time[k] && j<k)))rank++;
        places[k]=rank;
    }
    return 1;
}

RrOptions rr_options_default(void){RrOptions r={-1,-1,-1,0,0};return r;}
int rr_options_valid(const RrOptions *r){return rr_options_valid_value(r);}
int rr_opponent_count(const RrOptions *r,int a,int g){return rr_opponent_count_value(r,a,g);}
int rr_traffic_target(const RrOptions *r,int host){
    const int targets[4]={RR_TRAFFIC_OFF,RR_TRAFFIC_LOW,RR_TRAFFIC_MEDIUM,RR_TRAFFIC_HIGH};
    if(!rr_options_valid_value(r)||host<0)return -1;
    return r->traffic<0?host:targets[r->traffic];
}
float rr_catchup_fallback(float gap){return rr_catchup_fallback_value(gap);}

RrRewardTable rr_reward_inferred_default(void) {
    RrRewardTable r={{RR_REWARD_FIRST_CASH,RR_REWARD_SECOND_CASH,RR_REWARD_THIRD_CASH,0,0,0},
        {RR_REWARD_FIRST_REPUTATION,RR_REWARD_SECOND_REPUTATION,RR_REWARD_THIRD_REPUTATION,0,0,0},RR_REWARD_INFERRED};
    return r;
}
RrReward rr_reward(const RrRewardTable *r,int place,int failed) {
    RrReward result={0};
    if(!r || (r->provenance!=RR_REWARD_DATA && r->provenance!=RR_REWARD_INFERRED) || failed<0 ||
       (!failed && (place<1 || place>RR_MAX_CARS)))return result;
    result.available=1;result.inferred=r->provenance==RR_REWARD_INFERRED;
    if(!failed){result.cash=r->cash[place-1];result.reputation=r->reputation[place-1];}
    return result;
}
