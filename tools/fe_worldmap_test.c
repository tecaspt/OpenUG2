#include "../src/fe_worldmap.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 FeWorldMap m={0};FeMapProjection p={10,20,100,4,5,100,20,-10,80};float x,y;double wx,wy;
 const double world[5][2]={{10,20},{110,20},{110,-80},{10,-80},{60,-30}};
 const float expected[5][2]={{-41,-45},{59,-25},{49,55},{-51,35},{4,5}};
 for(int i=0;i<5;i++) {
  assert(fwm_project(&p,world[i][0],world[i][1],&x,&y));
  assert(fabsf(x-expected[i][0])<.0001f && fabsf(y-expected[i][1])<.0001f);
  assert(fwm_unproject(&p,x,y,&wx,&wy));
  assert(fabs(wx-world[i][0])<.0001 && fabs(wy-world[i][1])<.0001);
 }
 assert(fwm_project(&p,37,-15,&x,&y));assert(fwm_unproject(&p,x,y,&wx,&wy));assert(fabs(wx-37)<.0001&&fabs(wy+15)<.0001);
 FeMapProjection bad=p;bad.d=bad.b*bad.c/bad.a;x=123;y=456;assert(!fwm_project(&bad,0,0,&x,&y)&&x==123&&y==456);wx=7;wy=8;assert(!fwm_unproject(&bad,0,0,&wx,&wy)&&wx==7&&wy==8);assert(!fwm_project(&p,NAN,0,&x,&y));
 FeMapIcon q[]={{60,-30,1,0,11,0},{70,-30,2,1,22,0},{50,-30,1,0,33,0},{60,-20,1,1,44,0},{60,-40,2,0,55,0},{NAN,0,1,0,66,0},{0,0,9,0,0,0}};
 fwm_open(&m,q,7,60,-30,0);m.projection=p;assert(m.n==5&&m.selected==0);assert(fwm_project(&p,60,-30,&x,&y));assert(fwm_hit(&m,x,y,1)==0);assert(fwm_hit(&m,10000,10000,10)==-1);
 assert(fwm_input(&m,2,NULL)==0&&m.selected==1);assert(fwm_input(&m,2,NULL)==0&&m.selected==1);assert(fwm_select(&m,0));assert(fwm_input(&m,1,NULL)==0&&m.selected==2);
 assert(fwm_select(&m,0));assert(fwm_input(&m,5,NULL)==0&&m.selected==3);assert(fwm_select(&m,0));assert(fwm_input(&m,6,NULL)==0&&m.selected==4);
 FeMapDestination dest={0};assert(fwm_input(&m,3,&dest)==1&&dest.id==55&&dest.kind==2&&dest.x==60&&dest.y==-40);assert(fwm_input(&m,4,NULL)==2);
 FeMapIcon many[300];for(int i=0;i<300;i++)many[i]=(FeMapIcon){1,2,1,0,(unsigned)i,0};fwm_open(&m,many,300,1,2,0);assert(m.n==288&&m.selected==0);fwm_open(&m,NULL,0,0,0,0);assert(m.n==0&&m.selected==-1&&fwm_input(&m,3,&dest)==0&&fwm_input(&m,4,NULL)==2);
 FeMapIcon directional[]={{60,-30,1,0,1,0},{70,-29,1,0,2,0},{95,-30,1,0,3,0}};
 fwm_open(&m,directional,3,60,-30,0);m.projection=(FeMapProjection){10,20,100,0,0,100,0,0,100};
 assert(fwm_input(&m,2,NULL)==0 && m.selected==1);
 FeMapIcon outside={1000,1000,1,0,9,0};fwm_open(&m,&outside,1,0,0,0);
 assert(fwm_input(&m,3,&dest)==FWM_EV_NONE);
 FeMapIcon overlap[]={{60,-30,1,0,101,0},{60,-30,2,0,202,0}};
 fwm_open(&m,overlap,2,60,-30,0);assert(fwm_project(&m.projection,60,-30,&x,&y));
 assert(fwm_touch_select(&m,x,y,12)==1);
 assert(fwm_input(&m,3,&dest)==1 && dest.id==202 && dest.kind==2);
 assert(fwm_touch_select(&m,x,y,12)==0);
 assert(fwm_input(&m,FWM_IN_NEXT,NULL)==0 && m.selected==1);
 assert(fwm_input(&m,FWM_IN_PREVIOUS,NULL)==0 && m.selected==0);
 const float zone[4][2]={{-4,-4},{4,-4},{4,4},{-4,4}};
 assert(fwm_zone_contains(zone,0,0) && !fwm_zone_contains(zone,10,0));
 assert(!fwm_zone_contains(zone,NAN,0) && !fwm_zone_contains(NULL,0,0));
 puts("fe_worldmap_test PASS");return 0;
}
