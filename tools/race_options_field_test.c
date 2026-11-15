/* Synthetic flat pavement/source paths: all selectable field sizes and B-only selection. */
#include "ai.h"
#include "world.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
static void fixture(const char *root) {
    char file[512];snprintf(file,sizeof file,"%s/ROUTESTEST/Paths1.bin",root);
    unsigned char paths[128]={0};uint32_t head[]={0x34148,120};memcpy(paths,head,8);
    for(int k=0;k<5;k++){float x=k*50;memcpy(paths+8+k*24,&x,4);}
    FILE *f=fopen(file,"wb");assert(f&&fwrite(paths,1,sizeof paths,f)==sizeof paths);fclose(f);
    for(int reverse=0;reverse<2;reverse++) {
        unsigned char data[436]={0};uint32_t header[]={0x80034120,428,0x34121,420};memcpy(data,header,16);
        uint32_t pair=5|(5<<16);memcpy(data+24+44,&pair,4);
        for(int k=0;k<5;k++) {
            float point[]={reverse?200-k*50:k*50,0,k*50};memcpy(data+24+128+k*56+4,point,sizeof point);
            uint16_t width[]={8*256,8*256};memcpy(data+24+128+k*56+24,width,4);
        }
        snprintf(file,sizeof file,"%s/ROUTESTEST/Routes1%c.bin",root,reverse?'B':'F');
        f=fopen(file,"wb");assert(f&&fwrite(data,1,sizeof data,f)==sizeof data);fclose(f);
    }
}
int main(void) {
    char root[]="/tmp/m7-field-XXXXXX",dir[512],file[560];assert(mkdtemp(root));
    snprintf(dir,sizeof dir,"%s/ROUTESTEST",root);assert(!mkdir(dir,0700));fixture(root);
    float floor[]={-30,-30,0,0,0,230,-30,0,0,0,230,30,0,0,0,-30,30,0,0,0};
    uint16_t idx[]={0,1,2,0,2,3};N2Mesh mesh={.verts=floor,.nverts=4,.idx=idx,.nidx=6,.cat=N2_ROAD};
    N2Scene scene={.meshes=&mesh,.count=1,.cap=1};float bounds[][4]={{-30,-30,230,30}};
    WGroundGrid grid={0};assert(world_ground_grid_build(&grid,&scene,bounds));world_ground_grid_activate(&grid);
    phys_wall_index_build(NULL,NULL,0);
    WEvent e={.id=1,.reg="TEST",.npoly=3,.poly={{0,0},{100,0},{200,0}},.info={.kind=N2_RACE_SPRINT}};
    for(int reverse=0;reverse<2;reverse++)for(int count=0;count<=N_RACE_AI;count++) {
        RrOptions o=rr_options_default();o.opponents=count;o.reverse=reverse;
        AiTrafficWorld world={.scene=&scene,.race_options=&o};
        WRace race={.active=1,.kind=N2_RACE_SPRINT,.maxlaps=1,.ngate=3,.ngrid=8,.grid_named=1,.grid_heading=reverse?3.14159265f:0};
        for(int k=0;k<3;k++)race.gate[k]=(WGate){.x=reverse?200-k*100:k*100,.dx=reverse?-1:1,.half=22};
        for(int k=0;k<8;k++){race.grid[k][0]=reverse?180+(k/2)*6:20-(k/2)*6;race.grid[k][1]=k%2?3:-3;}
        AiCar player={.pos={reverse?180:20,0,0},.head=race.grid_heading,.half_length=2,.half_width=.85f,.height=1.5f};
        AiCar cars[N_RACE_AI];AiRace drivers[N_RACE_AI];N2Path path={0};
        for(int k=0;k<N_RACE_AI;k++)cars[k]=(AiCar){.half_length=2,.half_width=.85f,.height=1.5f};
        int got=ai_race_prepare(root,&e,&race,&world,&path,cars,drivers,&player);
        assert(got==count&&path.n>=2);
        assert(reverse?path.xy[0]>path.xy[2*path.n-2]:path.xy[0]<path.xy[2*path.n-2]);
        for(int k=0;k<got;k++)assert(drivers[k].entrant_id==k+1&&!drivers[k].eliminated&&cars[k].ride.contact_mask==15);
        free(path.xy);
    }
    snprintf(file,sizeof file,"%s/Routes1B.bin",dir);assert(!unlink(file));
    for(int bad=0;bad<2;bad++) {
        if(bad){FILE *f=fopen(file,"wb");assert(f);fputs("invalid synthetic route",f);fclose(f);}
        RrOptions o=rr_options_default();o.opponents=1;o.reverse=1;
        AiTrafficWorld world={.scene=&scene,.race_options=&o};
        WRace race={.active=1,.ngate=3,.ngrid=8};AiCar player={0},cars[N_RACE_AI]={0};AiRace drivers[N_RACE_AI];N2Path path={0};
        assert(ai_race_prepare(root,&e,&race,&world,&path,cars,drivers,&player)==0&&path.n==0);
    }
    const char *names[]={"Paths1.bin","Routes1F.bin","Routes1B.bin"};
    for(int k=0;k<3;k++){snprintf(file,sizeof file,"%s/%s",dir,names[k]);assert(!unlink(file));}
    assert(!rmdir(dir)&&!rmdir(root));world_ground_grid_activate(NULL);world_ground_grid_free(&grid);
    puts("race_options_field_test: PASS (0..5 opponents, source F/B orientation, supported grids, missing/corrupt B rejection)");return 0;
}
