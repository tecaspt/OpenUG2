/* Synthetic two-gate course: exercise real progression, including the clamped final lap. */
#include "world.h"
#include "postrace_stats.h"
#include <assert.h>
#include <stdio.h>
static int checks;
#define CHECK(x) do {assert(x);checks++;} while(0)
int main(void) {
    WRace r={.active=1,.kind=N2_RACE_CIRCUIT,.maxlaps=2,.ngate=2,
        .gate={{.x=0,.dx=1,.half=2},{.x=10,.dx=-1,.half=2}}};
    PrRaceStats s={0};world_race_begin(&r,1,0);pr_stats_start(&s,1);
    const float x[]={11,9,-1,1};
    for(int round=0;round<2;round++)for(int step=0;step<4;step++) {
        int gate=r.next,lap=r.lap;double dt=round?.25:.5;
        double speed=fabs(x[step]-r.px)/dt*3.6;
        int crossed=world_race_progress_update(&r,1,x[step],0);
        int complete=pr_stats_lap_crossed(1,crossed,gate,lap);
        CHECK(complete==(step==3));CHECK(pr_stats_step(&s,speed,dt,complete));
    }
    CHECK(r.finished && r.lap==2);CHECK(s.laps==2);CHECK(fabs(s.best_lap-1)<1e-12);
    CHECK(fabs(s.top_speed_kmh-144)<1e-9);CHECK(fabs(pr_stats_average(&s)-57.6)<1e-9);
    /* Artificial finish/knockout does not cross a gate or complete a lap. */
    world_race_begin(&r,1,0);pr_stats_start(&s,1);r.finished=1;
    int crossed=world_race_progress_update(&r,1,11,0);
    CHECK(!pr_stats_lap_crossed(1,crossed,r.next,r.lap));
    CHECK(pr_stats_step(&s,0,.5,0));CHECK(!s.laps && !s.best_lap);
    CHECK(!pr_stats_lap_crossed(0,1,0,1));CHECK(!pr_stats_lap_crossed(1,1,0,0));
    printf("postrace_lap_test: %d checks PASS\n",checks);return 0;
}
