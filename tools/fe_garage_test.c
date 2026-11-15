#include "fe_garage.h"
#include "fe_screen.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void test_parts_binding(void) {
 UgFngObject objects[18]={0};UgNode nodes[18]={0};GvScreen s={0};GuiCarousel car;GvCarousel v;
 s.loaded=1;s.scene.n=18;s.scene.nodes=nodes;
 const uint32_t hashes[]={0xef8bb088,0x6c9e933e,0x42adb44c,0x688a1551,0x3fbb7a4e,0x444969fd,0x444969fe};
 for(int i=0;i<7;i++){objects[i].guid=100+i;objects[i].name_hash=hashes[i];objects[i].type=UG_OBJ_IMAGE;}
 objects[0].guid=0xe2958;objects[0].pos[0]=12;objects[0].pos[1]=34;
 const uint32_t ids[GC_MAX]={0xe2a4f,0xe534f,0xe534a,0xe5345,0xe5340,0xe533b,0xe5336,0xe5331,0xe532c,0xe5327};
 for(int i=0;i<GC_MAX;i++){objects[7+i].guid=ids[i];objects[7+i].name_hash=0x87257585;objects[7+i].type=UG_OBJ_IMAGE;}
 for(int i=0;i<18;i++)nodes[i].obj=&objects[i];
 gc_init(&car,GC_RAIL_PAUSE);assert(gv_carousel_bind_parts(&v,&s,&car)&&v.nslots==10);
 objects[0].guid=1;assert(!gv_carousel_bind_parts(&v,&s,&car));objects[0].guid=0xe2958;
 objects[7].res_index=1;assert(!gv_carousel_bind_parts(&v,&s,&car));objects[7].res_index=0;
 objects[8].guid=objects[7].guid;assert(!gv_carousel_bind_parts(&v,&s,&car));
}
int main(void){
 test_parts_binding();
 GarageCatalog *cat=calloc(1,sizeof *cat);assert(cat);strcpy(cat->car,"SYNTH");
 cat->menu[GARAGE_KIT].count=20;for(int i=0;i<20;i++){cat->menu[GARAGE_KIT].options[i].value=i;snprintf(cat->menu[GARAGE_KIT].options[i].label,48,"Synthetic %d",i);}
 GarageConfig cfg={0};FeGarage *g=feg_create("");assert(g&&feg_use_catalog(g,cat,&cfg));
 assert(feg_page(g)==FEG_HUB);assert(feg_input(g,FES_IN_RIGHT)==0);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_SLOTS);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_PARTS);
 for(int i=0;i<18;i++)feg_input(g,FES_IN_RIGHT);
 feg_advance(g,1);GarageRequest r;assert(feg_take(g,&r)&&r.config.car.body_kit==18);
 assert(feg_input(g,FES_IN_BACK)==0);feg_ack(g,r.serial,1,NULL);assert(feg_take(g,&r)&&r.action==GAR_RESTORE&&!r.config.car.body_kit);
 feg_ack(g,r.serial,1,NULL);assert(feg_advance(g,0)==1&&feg_page(g)==FEG_SLOTS);
 assert(feg_input(g,FES_IN_BACK)==1&&feg_page(g)==FEG_HUB);assert(feg_input(g,FES_IN_BACK)==2);
 cat->menu[0].count=257;assert(!feg_use_catalog(g,cat,&cfg));
 feg_destroy(g);free(cat);puts("fe_garage_test: PASS (navigation/large catalogue/debounce/host ack/restore/bounds)");return 0;
}
