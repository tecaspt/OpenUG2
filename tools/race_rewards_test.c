#include "race_rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    RrRewardTable r=rr_reward_inferred_default(),copy=r;
    for(int place=1;place<=RR_MAX_CARS;place++) {
        RrReward v=rr_reward(&r,place,0);assert(v.available&&v.inferred);
        assert(v.cash==r.cash[place-1]&&v.reputation==r.reputation[place-1]);
    }
    assert(!memcmp(&r,&copy,sizeof r));
    assert(!rr_reward(NULL,1,0).available);assert(!rr_reward(&r,0,0).available);
    assert(!rr_reward(&r,7,0).available);assert(!rr_reward(&r,1,-1).available);
    RrReward dnf=rr_reward(&r,0,RR_FAIL_KNOCKOUT);assert(dnf.available&&!dnf.cash&&!dnf.reputation);
    r.provenance=RR_REWARD_UNKNOWN;assert(!rr_reward(&r,1,0).available);
    r.provenance=99;assert(!rr_reward(&r,1,0).available);
    r.provenance=RR_REWARD_DATA;r.cash[5]=UINT32_MAX;r.reputation[5]=123;
    RrReward decoded=rr_reward(&r,6,0);assert(decoded.available&&!decoded.inferred&&decoded.cash==UINT32_MAX&&decoded.reputation==123);
    assert(rr_reward(&r,1,0).cash==RR_REWARD_FIRST_CASH); /* no hidden scaling/mutation */
    puts("race_rewards_test: PASS (places/DNF/provenance/unknown/boundaries/immutable transfer)");return 0;
}
