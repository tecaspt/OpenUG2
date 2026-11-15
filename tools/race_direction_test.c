/* Synthetic marker chunk, never copied retail bytes. */
#include "world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned hash(const char *s){unsigned h=~0u;while(*s)h=h*33+(unsigned char)*s++;return h;}
static void word(unsigned char *b,int at,unsigned x){memcpy(b+at,&x,4);}
int main(void) {
    char root[]="/tmp/m7-direction-XXXXXX",dir[256],file[280];assert(mkdtemp(root));
    snprintf(dir,sizeof dir,"%s/ROUTESTEST",root);assert(!mkdir(dir,0700));
    snprintf(file,sizeof file,"%s/TrackPosMarkersAll.bin",dir);
    unsigned char data[112]={0};word(data,0,0x34146);word(data,4,104);memset(data+8,0x11,8);
    const char *names[]={"ForwardStartingPosition","BackwardStartingPosition"};
    for(int k=0;k<2;k++) {
        int at=16+48*k;word(data,at+8,hash(names[k]));word(data,at+36,77);
        float x=k?20:10;memcpy(data+at+16,&x,4);data[at+33]=k?128:0;
    }
    FILE *f=fopen(file,"wb");assert(f&&fwrite(data,1,sizeof data,f)==sizeof data);fclose(f);
    World *w=calloc(1,sizeof *w);assert(w);w->city.nev=1;w->neighborhood.nreg=1;
    strcpy(w->neighborhood.rgn[0].name,"TEST");
    WEvent *e=&w->city.ev[0];e->id=77;e->npoly=3;e->poly[1][0]=10;e->poly[2][0]=20;
    char error[120];assert(world_race_start(w,root,0,1)==3);
    assert(w->city.race.grid_named&&w->city.race.grid[0][0]==10&&w->city.race.gate[0].x==0);
    assert(world_race_start_direction(w,root,0,1,1,error,sizeof error)==3);
    assert(w->city.race.grid[0][0]==20 && w->city.race.gate[0].x==20 && w->city.race.gate[0].dx<0);
    assert(fabsf(w->city.race.grid_heading-3.14159265f)<.001f);
    e->circuit=1;e->npoly=4;e->poly[3][0]=0;
    assert(world_race_start_direction(w,root,0,3,1,error,sizeof error)==3);
    assert(w->city.race.gate[0].x==0&&w->city.race.gate[1].x==20&&w->city.race.maxlaps==3);
    assert(!world_race_start_direction(w,root,0,1,2,error,sizeof error));
    assert(!w->city.race.active && strstr(error,"Invalid"));
    assert(!unlink(file));assert(!world_race_start_direction(w,root,0,1,1,error,sizeof error));
    assert(!w->city.race.active && strstr(error,"Backward"));
    assert(!world_race_start_direction(w,root,0,1,2,error,sizeof error));
    free(w);assert(!rmdir(dir)&&!rmdir(root));
    puts("race_direction_test: PASS (forward compatibility/backward markers/gates/fail-closed)");return 0;
}
