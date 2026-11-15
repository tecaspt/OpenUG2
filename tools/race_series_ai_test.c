#include "ai.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    AiCar car={.spd=.5f,.vel={.5f,.1f},.target_speed=100};AiRace field[2]={0};
    AiTraffic routes[N_OPENWORLD_AI]={0};
    assert(ai_actor_present_rules(0,1,2,routes,field));
    ai_race_eliminate(&car,&field[0]);assert(field[0].eliminated && car.spd==0 && car.vel[0]==0 && car.vel[1]==0);
    assert(!ai_actor_present_rules(0,1,2,routes,field));assert(ai_actor_present_rules(1,1,2,routes,field));
    routes[0].present=1;assert(ai_actor_present_rules(N_RACE_AI,1,2,routes,field));
    puts("race_series_ai_test: PASS (eliminated freeze/presence, surviving racer and traffic)");return 0;
}
