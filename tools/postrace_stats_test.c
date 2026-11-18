#include "postrace_stats.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
static int checks;
#define CHECK(x) do {assert(x);checks++;} while(0)
int main(void) {
    PrRaceStats s={0};
    CHECK(!pr_stats_step(&s,100,.5,0));CHECK(pr_stats_average(&s)==0);
    pr_stats_start(&s,0);
    CHECK(pr_stats_step(&s,36,.25,0));CHECK(pr_stats_step(&s,108,.75,0));
    CHECK(s.samples==2 && s.elapsed==1);CHECK(s.top_speed_kmh==108);
    CHECK(pr_stats_average(&s)==90); /* weighted, not the 72 km/h sample mean */
    CHECK(!s.laps && !s.best_lap);
    CHECK(!pr_stats_step(&s,NAN,.1,0));CHECK(!pr_stats_step(&s,INFINITY,.1,0));
    CHECK(!pr_stats_step(&s,50,0,0));CHECK(!pr_stats_step(&s,50,-1,0));
    CHECK(!pr_stats_step(&s,50,NAN,0));CHECK(!pr_stats_step(&s,50,2,0));
    CHECK(!pr_stats_step(&s,50,.1,2));CHECK(s.samples==2 && pr_stats_average(&s)==90);
    CHECK(pr_stats_step(&s,-72,.5,1));CHECK(!s.laps && s.top_speed_kmh==108);
    pr_stats_start(&s,1);CHECK(!s.samples && !s.laps && s.elapsed==0 && s.top_speed_kmh==0);
    CHECK(pr_stats_step(&s,0,.4,0));CHECK(pr_stats_step(&s,0,.6,1));
    CHECK(s.laps==1 && fabs(s.best_lap-1)<1e-12);
    CHECK(pr_stats_step(&s,36,.3,0));CHECK(pr_stats_step(&s,36,.2,1));
    CHECK(s.laps==2 && fabs(s.best_lap-.5)<1e-12);
    CHECK(pr_stats_step(&s,72,.75,1));CHECK(s.laps==3 && fabs(s.best_lap-.5)<1e-12);
    CHECK(pr_stats_average(&s)>0 && s.top_speed_kmh==72);
    /* A partial/developer-finished race has speed samples, but no completed lap. */
    pr_stats_start(&s,1);
    for(int tick=0;tick<120;tick++)CHECK(pr_stats_step(&s,0,1./60,0));
    CHECK(s.samples==120 && fabs(s.elapsed-2)<1e-12 && !s.laps && s.best_lap==0);
    CHECK(pr_stats_average(&s)==0 && s.top_speed_kmh==0);
    /* Reset removes the preceding race, including best lap and peak. */
    pr_stats_reset(&s);CHECK(!s.active && !s.samples && !s.laps && s.best_lap==0);
    pr_stats_start(&s,1);CHECK(pr_stats_step(&s,DBL_MAX,1,0));
    CHECK(!pr_stats_step(&s,DBL_MAX,1,0));CHECK(isfinite(s.speed_integral));
    pr_stats_reset(NULL);pr_stats_start(NULL,1);CHECK(!pr_stats_step(NULL,1,.1,0));CHECK(pr_stats_average(NULL)==0);
    printf("postrace_stats_test: %d checks PASS\n",checks);return 0;
}
