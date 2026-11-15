#include "ai.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    float xy[]={0,0,200,0};N2Path path={.xy=xy,.n=2};
    WRace p={.active=1,.ngate=2,.maxlaps=1,.next=1};p.gate[1].x=200;
    AiCar player={.pos={100,0,0}},car={.pos={0,0,0}};
    AiRace d={.drive={.path=&path},.progress=p};
    RrOptions options=rr_options_default();
    AiTrafficWorld world={.race_pace=100,.race_player=&p,.race_options=&options};
    assert(ai_race_pace(&car,&d,0,&world,&player)==100);
    options.catch_up=1;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-120)<.001f);
    car.pos[0]=200;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-80)<.001f);
    options.catch_up=0;assert(ai_race_pace(&car,&d,0,&world,&player)==100);
    options.catch_up=1;car.pos[1]=100;assert(ai_race_pace(&car,&d,0,&world,&player)==100);
    d.eliminated=1;assert(ai_race_pace(&car,&d,0,&world,&player)==0);
    d.eliminated=0;car.pos[0]=car.pos[1]=0;
    RaceAiSource source={0};
    for(int j=0;j<RACE_AI_SOURCE_POINTS;j++){source.catchup[j][0]=-500+100*j;source.catchup[j][1]=.05f;}
    d.source=world.race_source=&source;d.source_scale=.8f;d.source_catch_up=0;
    options.catch_up=1;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-84)<.001f);
    options.catch_up=0;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-80)<.001f);
    options.catch_up=-1;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-80)<.001f);
    d.source_catch_up=1;assert(fabsf(ai_race_pace(&car,&d,0,&world,&player)-84)<.001f);
    puts("race_options_ai_test: PASS (opt-in catch-up/on/off/off-course/eliminated)");return 0;
}
