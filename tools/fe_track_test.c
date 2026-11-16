#include "fe_menu.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    FeChoice c={0};c.nevents=3;c.event_indices[0]=8;c.event_indices[1]=12;c.event_indices[2]=15;
    fet_move(&c,-1,0,0);assert(c.selected==0 && c.event_index==8);
    fet_move(&c,1,0,0);assert(c.selected==1 && c.event_index==12);
    fet_move(&c,1,0,0);fet_move(&c,1,0,0);assert(c.selected==2 && c.event_index==15);
    c.laps=1;fet_move(&c,-1,0,1);assert(c.laps==1);
    c.laps=8;fet_move(&c,1,0,1);assert(c.laps==8);
    /* track-options page: Laps / Direction / Lap KO (circuit only) */
    c.kind=1;c.rules=rr_options_default();assert(c.rules.opponents==-1 && c.rules.reverse==0 && c.rules.lap_knockout==0);
    assert(fet_row_count(&c)==3 && fet_row_enabled(&c,2));
    fet_move(&c,1,1,1);assert(c.option==1);fet_move(&c,1,0,1);assert(c.rules.reverse==1);
    fet_move(&c,-1,0,1);assert(c.rules.reverse==0);
    fet_move(&c,1,1,1);assert(c.option==2);fet_move(&c,1,0,1);assert(c.rules.lap_knockout==1);
    c.kind=2;assert(!fet_row_enabled(&c,2));fet_move(&c,-1,0,1);assert(c.rules.lap_knockout==1);
    fet_move(&c,1,1,1);assert(c.option==0);   /* disabled KO row skipped */
    /* mode-options page: Difficulty / Opponents / Traffic / Catch Up */
    c.kind=1;c.options_page=1;c.option=0;assert(fet_row_count(&c)==4);
    fet_move(&c,1,0,1);assert(c.difficulty==1);
    fet_move(&c,1,0,1);fet_move(&c,1,0,1);assert(c.difficulty==2);
    fet_move(&c,1,1,1);assert(c.option==1);fet_move(&c,1,0,1);assert(c.rules.opponents==4);
    fet_move(&c,-1,0,1);fet_move(&c,-1,0,1);assert(c.rules.opponents==2);
    fet_move(&c,1,1,1);assert(c.option==2);fet_move(&c,1,0,1);assert(c.rules.traffic==3);
    fet_move(&c,1,1,1);assert(c.option==3);fet_move(&c,-1,0,1);assert(c.rules.catch_up==0);
    fet_move(&c,1,0,1);assert(c.rules.catch_up==1);
    c.kind=4;assert(!fet_row_enabled(&c,2));   /* drift without downhill: no traffic */
    {GameCatalog cat={0};cat.nev=1;cat.ev[0].kind=4;cat.ev[0].downhill=1;c.cat=&cat;c.event_index=0;
     assert(fet_row_enabled(&c,2) && !fet_row_enabled(&c,1));c.cat=NULL;}
    c.kind=5;assert(!fet_row_enabled(&c,2));
    c.options_page=0;c.option=0;
    c.ncars=2;c.cars[0]=2;c.cars[1]=4;c.car_index=2;c.preview_wait=1;
    fec_move(&c,-1);assert(c.car_index==2);
    fec_move(&c,1);assert(c.car_index==4 && c.preview_wait==0);
    fec_move(&c,1);assert(c.car_index==4);
    puts("fe_track_test: all checks PASS");return 0;
}
