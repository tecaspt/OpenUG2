#include "race_rules.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    RrKnockout ko;assert(rr_knockout_init(&ko,4));float progress[RR_MAX_CARS]={10,8,6,4};
    assert(rr_knockout_lap(&ko,1,progress)==3 && ko.place[3]==4 && ko.remaining==3);
    assert(rr_knockout_lap(&ko,1,progress)==-1 && ko.remaining==3);
    progress[2]=9;assert(rr_knockout_lap(&ko,2,progress)==1 && ko.place[1]==3);
    progress[0]=9;assert(rr_knockout_lap(&ko,3,progress)==2 && rr_knockout_winner(&ko)==0);
    assert(ko.place[0]==1 && ko.place[2]==2);assert(rr_knockout_lap(&ko,4,progress)==-1);
    int events[3]={11,22,33},selected[RR_MAX_URL_RACES];
    assert(rr_url_select_events(events,3,22,3,selected)==3 && selected[0]==22 && selected[1]==33 && selected[2]==11);
    RrUrl url;assert(rr_url_init(&url,3,events,3,NULL));assert(rr_url_next_event(&url)==11);
    int a[RR_MAX_CARS]={2,1,3},bad[RR_MAX_CARS]={1,1,2},b[RR_MAX_CARS]={1,2,0};
    assert(!rr_url_record(&url,22,a) && url.completed==0);
    assert(!rr_url_record(&url,11,bad) && url.completed==0);
    assert(rr_url_record(&url,11,a));uint32_t initial=url.points[0];assert(rr_url_place(&url,0)==2);
    assert(!rr_url_record(&url,11,a) && url.completed==1);
    assert(rr_url_record(&url,22,b));assert(url.points[0]>initial && rr_url_next_event(&url)==33);
    assert(rr_url_undo_last(&url) && url.completed==1 && url.points[0]==initial);
    assert(rr_url_record(&url,22,b));assert(rr_url_record(&url,33,b));assert(rr_url_winner(&url)==0);
    assert(!rr_url_record(&url,33,b) && rr_url_next_event(&url)==0);
    assert(rr_url_undo_last(&url) && rr_url_winner(&url)==-1);
    assert(rr_url_undo_last(&url) && rr_url_undo_last(&url));assert(!rr_url_undo_last(&url));
    for(int k=0;k<3;k++)assert(url.points[k]==0 && url.wins[k]==0 && url.finishes[k]==0);
    RrRound round;int finished[RR_MAX_CARS]={1,0,0},failed[RR_MAX_CARS]={0};
    float progress2[RR_MAX_CARS]={100,50,40};int places[RR_MAX_CARS]={0};
    assert(rr_round_init(&round,3));assert(!rr_round_step(&round,finished,failed,progress2,.1));
    assert(!rr_round_places(&round,places));finished[1]=1;
    assert(!rr_round_step(&round,finished,failed,progress2,.1));finished[2]=1;
    assert(rr_round_step(&round,finished,failed,progress2,.1));assert(rr_round_places(&round,places));
    assert(places[0]==1 && places[1]==2 && places[2]==3);
    assert(rr_round_init(&round,3));finished[1]=finished[2]=0;
    assert(!rr_round_step(&round,finished,failed,progress2,.1));
    assert(rr_round_step(&round,finished,failed,progress2,RR_URL_FINISH_GRACE_SECONDS));
    assert(rr_round_places(&round,places) && places[0]==1 && places[1]==0 && places[2]==0);
    assert(rr_round_step(&round,finished,failed,progress2,NAN)==-1);
    puts("race_series_rules_test: PASS (KO elimination/ties/URL progression/DNF/undo/no duplicate awards)");
    return 0;
}
