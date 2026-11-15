#include "race_rules.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    const float slip[2]={20,15},straight[2]={25,0};
    RrDrift one={0},many={0};
    rr_drift_step(&one,slip,0,100,1,0,4);
    for(int k=0;k<600;k++)rr_drift_step(&many,slip,0,100/600.f,1,0,4/600.f);
    assert(one.chain>10000 && one.bank==0 && one.multiplier==RR_DRIFT_MAX_MULTIPLIER);
    assert(fabsf(one.chain-many.chain)<1 && one.multiplier==many.multiplier);
    rr_drift_step(&one,straight,0,25,1,0,1);
    assert(one.chain==0 && one.bank>0 && one.banked_chains==1 && one.multiplier==1);
    float bank=one.bank;
    rr_drift_step(&one,slip,0,10,1,0,.4f);assert(one.chain>0);
    rr_drift_step(&one,slip,0,10,1,1,.4f);
    assert(one.bank==bank && one.chain==0 && one.cancelled_chains==1 && one.raw_chain==0);
    rr_drift_bank(&one);assert(one.bank==bank && one.banked_chains==1);
    RrDrift none={0};rr_drift_step(&none,slip,0,25,0,0,1);assert(none.bank==0 && none.chain==0);
    const float slow[2]={1,1},backwards[2]={-20,15},bad[2]={NAN,1};
    rr_drift_step(&none,slow,0,1,1,0,1);rr_drift_step(&none,backwards,0,1,1,0,1);
    rr_drift_step(&none,bad,0,1,1,0,1);assert(none.bank==0 && none.chain==0);
    rr_drift_step(&none,slip,0,1e30f,1,0,1);assert(isfinite(none.chain) && none.chain<=RR_SCORE_LIMIT);
    rr_drift_bank(&none);assert(none.bank<=RR_SCORE_LIMIT);
    float points[3]={100,500,300};assert(rr_points_place(points,3,0)==3);
    points[0]=500;assert(rr_points_place(points,3,0)==1 && rr_points_place(points,3,1)==1);
    assert(rr_points_place(points,3,3)==0);points[1]=NAN;assert(rr_points_place(points,3,0)==0);
    puts("race_drift_rules_test: PASS (cadence/multiplier/bank/crash/invalid/point ranking)");
    return 0;
}
