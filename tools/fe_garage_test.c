#include "fe_garage.h"
#include "fe_screen.h"
#include "fe_profile.h"
#include "career.h"
#include <unistd.h>
#include <sys/stat.h>
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
static void test_rims(void) {
 GarageCatalog *cat=calloc(1,sizeof *cat);GarageRims *rims=calloc(1,sizeof *rims);assert(cat&&rims);
 strcpy(cat->car,"SYNTH");rims->count=3;
 strcpy(rims->options[0].brand,"ALPHA");rims->options[0].style=1;
 strcpy(rims->options[1].brand,"BETA");rims->options[1].style=2;
 strcpy(rims->options[2].brand,"BETA");rims->options[2].style=3;
 GarageConfig cfg={0};strcpy(cfg.rim_brand,"BETA");cfg.rim_style=3;
 FeGarage *g=feg_create("");assert(g&&feg_use_catalog(g,cat,&cfg)&&feg_use_rims(g,rims));
 rims->options[2].style=2;assert(!feg_use_rims(g,rims));rims->options[2].style=3;
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_SLOTS);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_BRANDS);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_RIMS);
 feg_advance(g,1);GarageRequest r;assert(feg_take(g,&r)&&!strcmp(r.config.rim_brand,"BETA")&&r.config.rim_style==3);
 feg_ack(g,r.serial,1,NULL);assert(feg_input(g,FES_IN_LEFT)==0);feg_advance(g,1);
 assert(feg_take(g,&r)&&r.config.rim_style==2);feg_ack(g,r.serial,1,NULL);
 assert(feg_input(g,FES_IN_BACK)==0);assert(feg_take(g,&r)&&r.action==GAR_RESTORE&&r.config.rim_style==3);
 feg_ack(g,r.serial,1,NULL);assert(feg_advance(g,0)&&feg_page(g)==FEG_BRANDS);
 assert(feg_input(g,FES_IN_LEFT)==0);assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_RIMS);
 feg_advance(g,1);assert(feg_take(g,&r)&&!strcmp(r.config.rim_brand,"ALPHA"));feg_ack(g,r.serial,1,NULL);
 assert(feg_input(g,FES_IN_ACCEPT)==0);assert(feg_take(g,&r)&&r.action==GAR_KEEP);feg_ack(g,r.serial,1,NULL);
 assert(feg_advance(g,0)&&feg_page(g)==FEG_SLOTS);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_BRANDS);
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_page(g)==FEG_RIMS);feg_advance(g,1);
 assert(feg_take(g,&r)&&!strcmp(r.config.rim_brand,"ALPHA"));feg_ack(g,r.serial,1,NULL);
 assert(feg_input(g,FES_IN_BACK)==0);assert(feg_take(g,&r)&&r.action==GAR_RESTORE);feg_ack(g,r.serial,1,NULL);
 assert(feg_advance(g,0)&&feg_page(g)==FEG_BRANDS);assert(feg_input(g,FES_IN_BACK)==0);
 assert(feg_take(g,&r)&&r.action==GAR_RESTORE&&!strcmp(r.config.rim_brand,"ALPHA"));feg_ack(g,r.serial,1,NULL);
 assert(feg_advance(g,0)&&feg_page(g)==FEG_SLOTS);
 feg_destroy(g);free(rims);free(cat);
}
static size_t read_fixture(const char *path,unsigned char *bytes,size_t cap) {
 FILE *f=fopen(path,"rb");assert(f);size_t n=fread(bytes,1,cap,f);assert(!ferror(f)&&feof(f));assert(!fclose(f));return n;
}
static void test_rename_carry(void) {
 char root[]="/tmp/openug2-garage-profile-XXXXXX";assert(mkdtemp(root));
 StRegStore reg;assert(st_reg_open_memory(&reg)==ST_OK);FeProfile *p=fep_create(root,&reg);assert(p);
 /* Another menu can create this profile after FE opened: retain its opaque bytes/bindings. */
 StProfile *external=malloc(sizeof *external);assert(external);st_profile_defaults(external,0);
 snprintf(external->name,sizeof external->name,"%s",p->value.name);external->raw[40000]=0x71;external->audio.master=.42f;
 assert(!p->persisted&&st_profile_save(root,external)==ST_OK&&fep_persist(p)==ST_OK);
 assert(p->persisted&&p->value.raw[40000]==0x71&&p->value.audio.master==.42f);
 assert(st_profile_delete(root,p->value.name)==ST_OK);free(external);
 assert(fep_new(p,"Original")==ST_OK);GarageConfig cfg={0},out={0};cfg.car.body_kit=1;
 strcpy(cfg.rim_brand,"NFSU");cfg.rim_style=2;
 assert(garage_save(root,"Original","SYNTH",&cfg)==ST_OK);
 cfg.car.body_kit=2;assert(garage_save(root,"Original","OTHER",&cfg)==ST_OK);
 char before[1200],after[1200],career[1200];unsigned char bytes[512],copy[512];
 snprintf(before,sizeof before,"%s/Original/%s",root,GARAGE_FILE_NAME);
 size_t n=read_fixture(before,bytes,sizeof bytes);
 snprintf(career,sizeof career,"%s/Original/%s",root,CR_FILE_NAME);
 FILE *f=fopen(career,"wb");assert(f&&fwrite("old",1,3,f)==3&&!fclose(f));
 char opaque[1200];snprintf(opaque,sizeof opaque,"%s/Original/future.dat",root);
 f=fopen(opaque,"wb");assert(f&&fwrite("opaque",1,6,f)==6&&!fclose(f));
 char subdir[1200];snprintf(subdir,sizeof subdir,"%s/Original/nonempty",root);assert(!mkdir(subdir,0700));
 assert(fep_rename(p,"Blocked")==ST_E_IO&&!strcmp(p->value.name,"Original"));
 assert(st_profile_status(root,"Original")==ST_OK&&st_profile_status(root,"Blocked")==ST_E_NOTFOUND);
 assert(!rmdir(subdir));assert(fep_rename(p,"Renamed")==ST_OK);
 snprintf(opaque,sizeof opaque,"%s/Renamed/future.dat",root);
 assert(read_fixture(opaque,copy,sizeof copy)==6&&!memcmp(copy,"opaque",6));
 snprintf(after,sizeof after,"%s/Renamed/%s",root,GARAGE_FILE_NAME);
 assert(read_fixture(after,copy,sizeof copy)==n&&!memcmp(bytes,copy,n));
 assert(garage_load(root,"Renamed","SYNTH",&out)==ST_OK&&out.car.body_kit==1&&out.rim_style==2);
 assert(garage_load(root,"Renamed","OTHER",&out)==ST_OK&&out.car.body_kit==2);
 snprintf(career,sizeof career,"%s/Renamed/%s",root,CR_FILE_NAME);
 assert(read_fixture(career,copy,sizeof copy)==3&&!memcmp(copy,"old",3));
 /* Rename is opaque: even corrupt/future saves must travel unchanged. */
 f=fopen(after,"wb");assert(f&&fwrite("future",1,6,f)==6&&!fclose(f));
 assert(fep_rename(p,"Opaque")==ST_OK);
 snprintf(after,sizeof after,"%s/Opaque/%s",root,GARAGE_FILE_NAME);
 assert(read_fixture(after,copy,sizeof copy)==6&&!memcmp(copy,"future",6));
 assert(garage_load(root,"Opaque","SYNTH",&out)!=ST_OK);
 /* Refuse an auxiliary symlink and preserve the old profile on failed copy. */
 assert(!unlink(after)&&!symlink("missing",after));
 assert(fep_rename(p,"Rejected")==ST_E_IO&&!strcmp(p->value.name,"Opaque"));
 assert(st_profile_status(root,"Opaque")==ST_OK&&st_profile_status(root,"Rejected")==ST_E_NOTFOUND);
 assert(!unlink(after));assert(st_profile_delete(root,"Opaque")==ST_OK);
 fep_destroy(p);st_reg_close(&reg);assert(!rmdir(root));
}
typedef struct SaveProbe {int calls,result;} SaveProbe;
static int probe_save(void *user,const char *car,const GarageConfig *cfg) {
 SaveProbe *p=user;assert(!strcmp(car,"SYNTH")&&cfg->car.body_kit==1);p->calls++;return p->result;
}
static void test_save_recovery(void) {
 GarageCatalog *cat=calloc(1,sizeof *cat);assert(cat);strcpy(cat->car,"SYNTH");
 cat->menu[GARAGE_KIT].count=2;cat->menu[GARAGE_KIT].options[1].value=1;
 GarageConfig cfg={0};FeGarage *g=feg_create("");SaveProbe p={0,-2};GarageRequest r;
 assert(g&&feg_use_catalog(g,cat,&cfg)&&feg_set_save(g,probe_save,&p));
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_input(g,FES_IN_ACCEPT)==1&&feg_busy(g));
 assert(!feg_set_save(g,NULL,NULL));feg_input(g,FES_IN_RIGHT);feg_advance(g,1);
 assert(feg_take(g,&r));feg_ack(g,r.serial,1,NULL);assert(!p.calls);
 feg_input(g,FES_IN_ACCEPT);assert(feg_take(g,&r)&&r.action==GAR_KEEP);
 uint32_t old=r.serial;feg_ack(g,r.serial+1,1,NULL);assert(!p.calls);
 feg_ack(g,r.serial,1,NULL);assert(p.calls==1&&feg_busy(g));
 assert(feg_take(g,&r)&&r.action==GAR_RESTORE&&!r.config.car.body_kit);
 feg_ack(g,r.serial,0,"synthetic GPU failure");assert(strstr(feg_status(g),"restore failed"));
 feg_input(g,FES_IN_BACK);assert(feg_take(g,&r)&&r.action==GAR_RESTORE);
 feg_ack(g,r.serial,1,NULL);assert(feg_advance(g,0)&&feg_page(g)==FEG_SLOTS&&!feg_busy(g));
 assert(strstr(feg_status(g),"Save failed"));
 /* A reset/new edit must not reuse a serial that can write another Keep. */
 assert(feg_use_catalog(g,cat,&cfg));p.result=0;
 assert(feg_input(g,FES_IN_ACCEPT)==1&&feg_input(g,FES_IN_ACCEPT)==1);
 feg_input(g,FES_IN_RIGHT);feg_input(g,FES_IN_ACCEPT);assert(feg_take(g,&r)&&r.serial!=old);
 feg_ack(g,old,1,NULL);assert(p.calls==1&&feg_busy(g));
 feg_ack(g,r.serial,1,NULL);assert(p.calls==2&&feg_advance(g,0)&&!feg_busy(g));
 feg_destroy(g);free(cat);
}
static void test_shop_filter(void) {
 GarageCatalog *cat=calloc(1,sizeof *cat);assert(cat);strcpy(cat->car,"SYNTH");
 for(int i=0;i<GARAGE_SLOTS;i++){cat->menu[i].count=2;cat->menu[i].options[1].value=1;}
 GarageConfig cfg={0};FeGarage *g=feg_create("");GarageRequest r;
 assert(g&&feg_use_catalog(g,cat,&cfg));
 assert(feg_shop(g,5,(1u<<N2_PART_ENGINE)|(1u<<N2_PART_AUDIO)));
 assert(feg_page(g)==FEG_SLOTS&&feg_input(g,FES_IN_ACCEPT)==1);
 assert(!feg_shop(g,2,~0u)); /* filters cannot change during an edit */
 feg_input(g,FES_IN_RIGHT);feg_advance(g,1);assert(feg_take(g,&r));
 assert(r.config.car.parts[N2_PART_ENGINE]==1&&r.config.car.body_kit==0);
 feg_ack(g,r.serial,1,NULL);feg_input(g,FES_IN_ACCEPT);assert(feg_take(g,&r));
 feg_ack(g,r.serial,1,NULL);assert(feg_advance(g,0));
 assert(feg_input(g,FES_IN_BACK)==2); /* direct return to roam */
 assert(feg_shop(g,1,0)&&feg_page(g)==FEG_EMPTY);
 assert(feg_input(g,FES_IN_ACCEPT)==0&&feg_input(g,FES_IN_BACK)==2);
 assert(feg_shop(g,-1,0)&&feg_page(g)==FEG_HUB);
 feg_destroy(g);free(cat);
}
int main(void){
 test_save_recovery();
 test_rename_carry();
 test_rims();
 test_shop_filter();
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
