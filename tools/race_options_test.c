/* Synthetic policies: selected rows, bounds, reverse order, no retail constants claimed. */
#include "race_rules.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    RrOptions r=rr_options_default();assert(rr_options_valid(&r));
    assert(rr_opponent_count(&r,3,8)==3 && rr_traffic_target(&r,11)==11);
    for(int n=0;n<6;n++){r.opponents=n;assert(rr_opponent_count(&r,3,8)==n);}
    assert(rr_opponent_count(&r,3,3)==-1);assert(rr_opponent_count(&r,3,0)==-1);
    r.opponents=6;assert(!rr_options_valid(&r));r=rr_options_default();
    int target[]={RR_TRAFFIC_OFF,RR_TRAFFIC_LOW,RR_TRAFFIC_MEDIUM,RR_TRAFFIC_HIGH};
    for(int n=0;n<4;n++){r.traffic=n;assert(rr_traffic_target(&r,7)==target[n]);}
    r.traffic=4;assert(rr_traffic_target(&r,7)==-1);r=rr_options_default();
    r.reverse=2;assert(!rr_options_valid(&r));r=rr_options_default();r.catch_up=-2;assert(!rr_options_valid(&r));
    assert(fabsf(rr_catchup_fallback(100)+.2f)<1e-6f);
    assert(fabsf(rr_catchup_fallback(-100)-.2f)<1e-6f);
    assert(rr_catchup_fallback(NAN)==0 && rr_catchup_fallback(0)==0);
    for(int n=2;n<34;n++)for(int loop=0;loop<2;loop++)for(int i=0;i<n;i++) {
        int j=rr_direction_index(i,n,loop,1);assert(j>=0&&j<n);
        assert(rr_direction_index(j,n,loop,1)==i);
        assert(rr_direction_index(i,n,loop,0)==i);
        if(loop&&i==0)assert(j==0);
    }
    assert(rr_direction_index(0,1,0,1)==-1);
    puts("race_options_test: PASS (selection bounds/defaults/traffic/catch-up/direction)");return 0;
}
