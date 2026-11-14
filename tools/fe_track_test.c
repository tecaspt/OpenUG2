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
    fet_move(&c,1,1,1);assert(c.option==1);
    fet_move(&c,1,0,1);assert(c.difficulty==1);
    fet_move(&c,1,0,1);fet_move(&c,1,0,1);assert(c.difficulty==2);
    c.ncars=2;c.cars[0]=2;c.cars[1]=4;c.car_index=2;c.preview_wait=1;
    fec_move(&c,-1);assert(c.car_index==2);
    fec_move(&c,1);assert(c.car_index==4 && c.preview_wait==0);
    fec_move(&c,1);assert(c.car_index==4);
    puts("fe_track_test: 11 checks PASS");return 0;
}
