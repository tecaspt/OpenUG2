#include "garage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif
static void wr(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void test_store(void) {
 GarageConfig a={0},b={0},out;strcpy(a.rim_brand,"BETA");a.rim_style=3;a.car.body_kit=2;b.car.parts[1]=22;
 uint8_t *one=NULL,*two=NULL;size_t n=0,m=0;
 assert(garage_store_put(NULL,0,"CAR_A",&a,&one,&n)==ST_OK);
 assert(garage_store_put(one,n,"CAR_B",&b,&two,&m)==ST_OK);
 memset(&out,0x5a,sizeof out);GarageConfig unchanged=out;
 assert(garage_store_get(two,m,"MISSING",&out)==ST_E_NOTFOUND&&!memcmp(&out,&unchanged,sizeof out));
 assert(garage_store_get(two,m,"CAR_A",&out)==ST_OK&&out.rim_style==3&&out.car.body_kit==2);
 assert(garage_store_get(two,m,"CAR_B",&out)==ST_OK&&out.car.parts[1]==22);
 /* Build extended A, an opaque TLV, and a known tail with unusual padding. */
 uint8_t ax[11]={0};wr(ax,0x5241474f);wr(ax+4,1);ax[8]=9;ax[9]=8;ax[10]=7;
 uint8_t bx[145]={0};bx[0]=77;bx[2]=0xa5;wr(bx+4,3);bx[8]=4;bx[9]=5;bx[10]=6;
 memcpy(bx+11,one+36,128);bx[13]=0x7a;wr(bx+15,126);bx[11+8+10]=0x41;bx[11+8+92+10]=0x42;
 memset(bx+139,0x6c,6);uint8_t original[256];size_t z=st_container_build(original,sizeof original,ax,sizeof ax,bx,sizeof bx);assert(z);
 uint8_t *patched=NULL;size_t pn=0;a.car.body_kit=5;strcpy(a.rim_brand,"X");
 assert(garage_store_put(original,z,"CAR_A",&a,&patched,&pn)==ST_OK&&pn==z);
 StContainerView v;assert(st_container_parse(patched,pn,&v)==ST_OK);assert(v.size_a==11&&!memcmp(v.a,ax,11));
 assert(!memcmp(v.b,bx,11));assert(v.b[13]==0x7a&&v.b[11+8+10]==0x41&&v.b[11+8+92+10]==0x42&&!memcmp(v.b+139,bx+139,6));
 assert(garage_store_get(patched,pn,"CAR_A",&out)==ST_OK&&out.car.body_kit==5&&!strcmp(out.rim_brand,"X"));
 uint8_t bad[512];memcpy(bad,original,z);bad[z-1]^=1;uint8_t *sentinel=(uint8_t *)one;size_t sentinel_n=123;
 assert(garage_store_put(bad,z,"CAR_A",&a,&sentinel,&sentinel_n)==ST_E_CORRUPT&&sentinel==one&&sentinel_n==123);
 assert(garage_store_get(original,z-1,"CAR_A",&out)!=ST_OK);
 memcpy(bad,original,z);bad[z]=0;assert(garage_store_get(bad,z+1,"CAR_A",&out)!=ST_OK);
 bad[z-1]^=1;memset(&out,0x5a,sizeof out);
 assert(garage_store_get(bad,z,"CAR_A",&out)==ST_E_CORRUPT&&!memcmp(&out,&unchanged,sizeof out));
 wr(ax+4,2);size_t badn=st_container_build(bad,sizeof bad,ax,sizeof ax,bx,sizeof bx);assert(garage_store_get(bad,badn,"CAR_A",&out)==ST_E_INCOMPATIBLE);
 wr(ax+4,0);badn=st_container_build(bad,sizeof bad,ax,sizeof ax,bx,sizeof bx);assert(garage_store_get(bad,badn,"CAR_A",&out)==ST_E_CORRUPT);
 wr(ax+4,1);memcpy(bad,bx+11,134);memcpy(bad+134,bx+11,134);uint8_t duplicate[400];badn=st_container_build(duplicate,sizeof duplicate,ax,sizeof ax,bad,268);
 assert(garage_store_get(duplicate,badn,"CAR_A",&out)==ST_E_CORRUPT);
 bx[11+8]='!';badn=st_container_build(bad,sizeof bad,ax,sizeof ax,bx,sizeof bx);assert(garage_store_get(bad,badn,"CAR_A",&out)==ST_E_CORRUPT);
 #ifndef _WIN32
 char root[]="/tmp/openug2-garage-XXXXXX";assert(mkdtemp(root));char dir[512],path[600],linkpath[600];
 snprintf(dir,sizeof dir,"%s/PROFILE",root);assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_E_NOTFOUND);
 snprintf(linkpath,sizeof linkpath,"%s/MISSING",root);assert(garage_load(linkpath,"PROFILE","CAR_A",&out)==ST_E_NOTFOUND);
 snprintf(linkpath,sizeof linkpath,"%s/PROFILE",root);assert(!symlink(root,linkpath));assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_E_IO);assert(!unlink(linkpath));
 assert(!mkdir(dir,0700));
 if(geteuid()!=0){assert(!chmod(dir,0));assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_E_IO);assert(!chmod(dir,0700));}
 snprintf(path,sizeof path,"%s/%s",dir,GARAGE_FILE_NAME);
 assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_E_NOTFOUND);assert(garage_save(root,"PROFILE","CAR_A",&a)==ST_E_IO);
 snprintf(linkpath,sizeof linkpath,"%s/PROFILE",dir);FILE *profilefile=fopen(linkpath,"wb");assert(profilefile&&!fclose(profilefile));
 assert(garage_save(root,"PROFILE","CAR_A",&a)==ST_OK);
 assert(garage_save(root,"PROFILE","CAR_B",&b)==ST_OK);assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_OK&&out.car.body_kit==5);
 snprintf(linkpath,sizeof linkpath,"%s/hard",dir);assert(!link(path,linkpath));assert(garage_save(root,"PROFILE","CAR_A",&a)==ST_E_IO);assert(!unlink(linkpath));
 FILE *f=fopen(path,"wb");assert(f);wr(ax+4,2);badn=st_container_build(bad,sizeof bad,ax,sizeof ax,bx,sizeof bx);
 assert(fwrite(bad,1,badn,f)==badn&&!fclose(f));assert(garage_save(root,"PROFILE","CAR_A",&a)==ST_E_INCOMPATIBLE);
 f=fopen(path,"rb");assert(f);uint8_t verify[512];assert(fread(verify,1,badn,f)==badn&&!fclose(f)&&!memcmp(verify,bad,badn));
 f=fopen(path,"wb");assert(f);assert(fwrite("bad",1,3,f)==3&&!fclose(f));assert(garage_save(root,"PROFILE","CAR_A",&a)!=ST_OK);
 f=fopen(path,"rb");assert(f);char old[4]={0};assert(fread(old,1,3,f)==3&&!fclose(f)&&!memcmp(old,"bad",3));
 assert(!unlink(path));snprintf(linkpath,sizeof linkpath,"%s/link",dir);f=fopen(linkpath,"wb");assert(f&&!fclose(f));assert(!symlink(linkpath,path));
 assert(garage_save(root,"PROFILE","CAR_A",&a)==ST_E_IO);assert(garage_load(root,"PROFILE","CAR_A",&out)==ST_E_IO);
 assert(!unlink(path)&&!unlink(linkpath));snprintf(linkpath,sizeof linkpath,"%s/PROFILE",dir);assert(!unlink(linkpath)&&!rmdir(dir)&&!rmdir(root));
 #endif
 free(patched);free(two);free(one);
}
int main(void){
 test_store();
 GarageCatalog cat={0};cat.menu[GARAGE_KIT].count=2;cat.menu[GARAGE_KIT].options[1].value=2;
 GarageConfig c={0};Garage g;GarageRequest r;garage_init(&g,&cat,&c);
 assert(!garage_begin(&g,-1));assert(garage_begin(&g,GARAGE_KIT));assert(!garage_select(&g,2));
 assert(garage_select(&g,1));garage_update(&g,.29f);assert(!garage_take(&g,&r));garage_update(&g,.02f);assert(garage_take(&g,&r));
 assert(r.config.car.body_kit==2&&!g.applied.car.body_kit);garage_ack(&g,r.serial+1,1,NULL);assert(g.inflight);
 garage_finish(&g,0);garage_ack(&g,r.serial,1,NULL);assert(garage_take(&g,&r)&&r.action==GAR_RESTORE&&!r.config.car.body_kit);
 garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&!g.applied.car.body_kit);
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,0,"failure");assert(g.editing&&!g.applied.car.body_kit);
 assert(garage_select(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&g.applied.car.body_kit==2);
 /* A final request cannot be retargeted before its acknowledgement. */
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,0));garage_finish(&g,1);assert(garage_take(&g,&r));
 assert(!garage_select(&g,1));garage_finish(&g,0);assert(!g.action);garage_ack(&g,r.serial,1,NULL);assert(!g.applied.car.body_kit);
 /* Preview coalescing preserves the latest choice while an old request completes. */
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,1));garage_update(&g,1);assert(garage_take(&g,&r));
 assert(garage_select(&g,0));garage_ack(&g,r.serial,1,NULL);garage_update(&g,1);assert(garage_take(&g,&r)&&!r.config.car.body_kit);
 garage_ack(&g,r.serial,1,NULL);garage_finish(&g,0);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);
 GarageRims rims={0};rims.count=2;strcpy(rims.options[0].brand,"ALPHA");rims.options[0].style=1;
 strcpy(rims.options[1].brand,"BETA");rims.options[1].style=2;
 assert(garage_begin_rims(&g,&rims));assert(!garage_select_rim(&g,2));assert(garage_select_rim(&g,0));
 garage_update(&g,.29f);assert(!garage_take(&g,&r));garage_update(&g,.02f);assert(garage_take(&g,&r));
 assert(!strcmp(r.config.rim_brand,"ALPHA")&&r.config.rim_style==1);garage_ack(&g,r.serial,1,NULL);
 assert(garage_select_rim(&g,1));garage_finish(&g,0);assert(garage_take(&g,&r)&&r.action==GAR_RESTORE&&!r.config.rim_brand[0]);
 garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&!g.applied.rim_brand[0]);
 assert(garage_begin_rims(&g,&rims));assert(garage_select_rim(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));
 garage_ack(&g,r.serial,0,"rim failure");assert(g.editing&&!g.applied.rim_brand[0]);
 assert(garage_select_rim(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);
 assert(!g.editing&&!strcmp(g.applied.rim_brand,"BETA")&&g.applied.rim_style==2);
 puts("garage_test: PASS (selection/debounce/ack/stale serial/restore/keep/failure)");return 0;
}
