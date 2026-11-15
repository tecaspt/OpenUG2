/* [inferred] Garage flow over retail carousel packages. See GARAGE.md. */
#include "fe_garage.h"
#include "fe_screen.h"
#include "car_mod.h"
#include "settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct FeGarage {
 char root[512],car[32],loaded_car[32],status[220];
 GarageConfig current;GarageCatalog catalog;Garage model;
 FegSaveFn save;void *save_user;int recovery;char notice[160];
 int page,cursor,slot_cursor,first,count,slots[GARAGE_SLOTS+1],nslots;
 GarageRims rims;int rims_ready,brands[32],nbrands,brand,back_page;
 char labels[GC_MAX][48];
 GuiCarousel carousel;GvCarousel view;GvEntry entry[GC_MAX];
};
FeGarage *feg_create(const char *root){FeGarage *g=calloc(1,sizeof *g);if(g)snprintf(g->root,sizeof g->root,"%s",root?root:"");return g;}
void feg_destroy(FeGarage *g){free(g);}
int feg_busy(const FeGarage *g){return g&&(g->model.editing||g->model.inflight||g->model.action||g->recovery);}
int feg_set_save(FeGarage *g,FegSaveFn save,void *user){if(!g||feg_busy(g))return 0;g->save=save;g->save_user=user;return 1;}
static void model_reset(FeGarage *g,const GarageConfig *cfg) {
 uint32_t serial=g->model.serial;garage_init(&g->model,&g->catalog,cfg);g->model.serial=serial;
}
static void slots_build(FeGarage *g) {
 g->nslots=0;if(g->catalog.menu[GARAGE_KIT].count>1)g->slots[g->nslots++]=GARAGE_KIT;
 if(g->rims.count)g->slots[g->nslots++]=GARAGE_RIM;
 for(int i=0;i<N2_PART_COUNT;i++)if(g->catalog.menu[i].count>1)g->slots[g->nslots++]=i;
}
int feg_use_catalog(FeGarage *g,const GarageCatalog *cat,const GarageConfig *c) {
 if(!g||!cat||!c||!memchr(cat->car,0,sizeof cat->car)||!cat->car[0]||!garage_config_valid(c)||g->model.editing||g->model.inflight)return 0;
 for(int slot=0;slot<GARAGE_SLOTS;slot++) {
  const N2PartMenu *m=&cat->menu[slot];if(m->count<0||m->count>256)return 0;
  for(int i=0;i<m->count;i++)if(!memchr(m->options[i].label,0,sizeof m->options[i].label)||m->options[i].value<0||m->options[i].value>(slot==GARAGE_KIT?99:5999))return 0;
 }
 g->catalog=*cat;g->current=*c;snprintf(g->car,sizeof g->car,"%s",cat->car);snprintf(g->loaded_car,sizeof g->loaded_car,"%s",cat->car);
 model_reset(g,c);g->page=FEG_HUB;g->cursor=0;slots_build(g);return 1;
}
int feg_use_rims(FeGarage *g,const GarageRims *rims) {
 if(!g||!rims||rims->count<0||rims->count>GARAGE_MAX_RIMS||g->model.editing||g->model.inflight)return 0;
 int brands[32],n=0;
 for(int i=0;i<rims->count;i++) {
  GarageConfig c={0};memcpy(c.rim_brand,rims->options[i].brand,24);c.rim_style=rims->options[i].style;
  if(!c.rim_brand[0]||!garage_config_valid(&c))return 0;
  int cmp=i?strcmp(rims->options[i-1].brand,c.rim_brand):-1;
  if(cmp>0||(i&&cmp==0&&rims->options[i-1].style>=c.rim_style))return 0;
  if(cmp<0){if(n==32)return 0;brands[n++]=i;}
 }
 g->rims=*rims;memcpy(g->brands,brands,(size_t)n*sizeof *brands);g->nbrands=n;g->rims_ready=1;slots_build(g);return 1;
}
int feg_current(FeGarage *g,const char *car,const GarageConfig *c) {
 if(!g||!car||strlen(car)>=sizeof g->car||!garage_config_valid(c)||g->model.editing||g->model.inflight)return 0;
 snprintf(g->car,sizeof g->car,"%s",car);g->current=*c;return 1;
}
int feg_page(const FeGarage *g){return g?g->page:0;}
static int brand_end(FeGarage *g){return g->brand+1<g->nbrands?g->brands[g->brand+1]:g->rims.count;}
static int list_count(FeGarage *g){
 if(g->page==FEG_HUB)return 4;
 if(g->page==FEG_SLOTS)return g->nslots;
 if(g->page==FEG_BRANDS)return g->nbrands;
 if(g->page==FEG_RIMS)return brand_end(g)-g->brands[g->brand];
 return g->catalog.menu[g->model.slot].count;
}
static const char *const slot_keys[GARAGE_SLOTS]={
 "VISUAL_PART_FRONT_BUMPER","VISUAL_PART_REAR_BUMPER","VISUAL_PART_SIDE_SKIRT","VISUAL_PART_HOOD",
 "VISUAL_PART_HEAD_LIGHT","VISUAL_PART_TAIL_LIGHT","PERF_CATEGORY_ENGINE","UI_ICE_TRUNK_AUDIO",
 "VISUAL_PART_SPOILER","VISUAL_PART_MUFFLER_TIP","VISUAL_PART_ROOF_SCOOP","VISUAL_PART_BODY_KIT"};
int feg_open(FeGarage *g,GvScreen *s,UgAssets *a) {
 if(!s||!a)return 0;
 if(!g||!g->car[0]){snprintf(s->status,sizeof s->status,"Current car is unavailable");return 0;}
 if(strcmp(g->loaded_car,g->car)) {
  if(!n2_mod_garage_catalog(g->root,g->car,&g->catalog)){snprintf(s->status,sizeof s->status,"Car part catalogue is unavailable");return 0;}
  snprintf(g->loaded_car,sizeof g->loaded_car,"%s",g->car);g->page=FEG_HUB;g->cursor=0;
 }
 if(!g->model.editing&&!g->model.inflight)model_reset(g,&g->current);
 if(!g->rims_ready) {
  GarageRims *r=calloc(1,sizeof *r);
  int ok=r&&n2_mod_rim_catalog(g->root,r)&&feg_use_rims(g,r);free(r);
  if(!ok){snprintf(s->status,sizeof s->status,"Rim catalogue is unavailable");return 0;}
 }
 slots_build(g);
 const char *pkg=g->page==FEG_PARTS?"UI_GenericParts_Browser.fng":g->page==FEG_BRANDS?"UI_ChooseRimBrand.fng":g->page==FEG_RIMS?"UI_Rims_Browser.fng":"UI_ChooseCustomizeCategory.fng";
 if(!gv_screen_load(s,a,pkg))return 0;
 /* [data] This root group contains placeholder player/bank values. No bank is supplied here. */
 for(int i=0;i<s->scene.n;i++) {
  const UgFngObject *o=s->scene.nodes[i].obj;
  if((!o->parent_guid&&o->type==UG_OBJ_GROUP&&o->name_hash==0x02ddf58cu)||o->guid==0xe50fe||((g->page==FEG_RIMS)&&o->guid==0xe26b6))s->scene.nodes[i].app_hidden=1;
 }

 gc_init(&g->carousel,GC_RAIL_PAUSE);
 if(!(g->page==FEG_PARTS?gv_carousel_bind_parts(&g->view,s,&g->carousel):gv_carousel_bind(&g->view,s,&g->carousel)))return 0;
 g->status[0]=0;return 1;
}
int feg_input(FeGarage *g,int in) {
 if(!g||g->model.action>GAR_PREVIEW||(g->model.inflight&&g->model.flight.action>GAR_PREVIEW))return 0;
 if(in&&!g->recovery)g->notice[0]=0;
 if(in==FES_IN_BACK) {
  if(g->page==FEG_HUB)return 2;
  if(g->page==FEG_SLOTS){g->page=FEG_HUB;g->cursor=0;return 1;}
  g->back_page=g->page==FEG_RIMS?FEG_BRANDS:FEG_SLOTS;garage_finish(&g->model,0);return 0;
 }
 int n=list_count(g);
 if(in==FES_IN_LEFT||in==FES_IN_RIGHT||in==5||in==6) {
  int next=g->cursor+(in==FES_IN_LEFT||in==5?-1:1);
  if(next<0||next>=n||g->page==FEG_HUB)return 0;
  g->cursor=next;if(g->page==FEG_PARTS)garage_select(&g->model,next);
  if(g->page==FEG_RIMS)garage_select_rim(&g->model,g->brands[g->brand]+next);
  return 0;
 }
 if(in!=FES_IN_ACCEPT)return 0;
 if(g->page==FEG_HUB){if(!g->nslots)return 0;g->page=FEG_SLOTS;g->cursor=0;return 1;}
 if(g->page==FEG_SLOTS){
  if(g->cursor<0||g->cursor>=g->nslots)return 0;
  if(g->slots[g->cursor]==GARAGE_RIM) {
   if(!garage_begin_rims(&g->model,&g->rims))return 0;
   g->slot_cursor=g->cursor;g->page=FEG_BRANDS;g->cursor=0;
   for(int b=0;b<g->nbrands;b++)if(!strcmp(g->rims.options[g->brands[b]].brand,g->current.rim_brand))g->cursor=b;
   return 1;
  }
  if(!garage_begin(&g->model,g->slots[g->cursor]))return 0;
  g->slot_cursor=g->cursor;g->page=FEG_PARTS;g->cursor=0;
  int value=g->model.slot==GARAGE_KIT?g->model.applied.car.body_kit:g->model.applied.car.parts[g->model.slot];
  const N2PartMenu *m=&g->catalog.menu[g->model.slot];for(int i=0;i<m->count;i++)if(m->options[i].value==value)g->cursor=i;
  return 1;
 }
 if(g->page==FEG_BRANDS) {
  if(g->cursor<0||g->cursor>=g->nbrands)return 0;
  g->brand=g->cursor;g->page=FEG_RIMS;g->cursor=0;
  for(int i=g->brands[g->brand];i<brand_end(g);i++)if(!strcmp(g->rims.options[i].brand,g->current.rim_brand)&&g->rims.options[i].style==g->current.rim_style)g->cursor=i-g->brands[g->brand];
  garage_select_rim(&g->model,g->brands[g->brand]+g->cursor);return 1;
 }
 garage_finish(&g->model,1);return 0;
}
int feg_advance(FeGarage *g,float seconds) {
 if(!g)return 0;
 garage_update(&g->model,seconds);
 if(g->model.event){
  int event=g->model.event;g->current=g->model.applied;g->model.event=0;
  if(!g->recovery&&event==GAR_RESTORE&&g->back_page==FEG_BRANDS){g->page=FEG_BRANDS;g->cursor=g->brand;garage_begin_rims(&g->model,&g->rims);}
  else {g->page=FEG_SLOTS;g->cursor=g->slot_cursor;}
  if(event==GAR_KEEP)g->notice[0]=0;
  g->recovery=0;return 1;
 }
 return 0;
}
int feg_sync(FeGarage *g,GvScreen *s,float ms) {
 if(!g||!s->loaded)return 0;
 int n=list_count(g);g->count=n;g->first=g->cursor>4?g->cursor-4:0;
 if(g->first+GC_MAX>n)g->first=n>GC_MAX?n-GC_MAX:0;
 int visible=n-g->first;if(visible>GC_MAX)visible=GC_MAX;
 int disabled[GC_MAX]={0};
 const char *hub[]={"CUST_CATEGORY_BODY","CUST_CATEGORY_PERFORMANCE","CUST_CATEGORY_PAINT_VINYL","CUST_CATEGORY_INTERIORS"};
 const uint32_t hub_icons[]={0xb802f10eu,0xc56d3032u,0xdc99aafeu,0x3f40e75fu};
 const uint32_t slot_icons[GARAGE_SLOTS]={0x739c4a1bu,0x015c767cu,0x4e6c2af9u,0x028f7092u,
  0xf69ac384u,0x2cbb2d1cu,0xc15c94e6u,0xe803e1cdu,0xbb034ea6u,0xa434a006u,0x79165861u,0x028c24f6u};
 for(int i=0;i<visible;i++) {
  int at=i+g->first;const char *label=NULL;
  if(g->page==FEG_HUB){label=gv_text(s->assets,ug_hash_upper(hub[at]),NULL);disabled[i]=at!=0||!g->nslots;}
  else if(g->page==FEG_SLOTS)label=g->slots[at]==GARAGE_RIM?gv_text(s->assets,0xe4504d8du,NULL):g->slots[at]==GARAGE_KIT?"Body kit presets":gv_text(s->assets,ug_hash_upper(slot_keys[g->slots[at]]),NULL);
  else if(g->page==FEG_BRANDS) {
   const char *brand=g->rims.options[g->brands[at]].brand;
   snprintf(g->labels[i],48,"RIMS_BRAND_%s",brand);label=gv_text(s->assets,ug_hash_upper(g->labels[i]),brand);
  } else if(g->page==FEG_RIMS) {
   snprintf(g->labels[i],48,"STYLE%02d",g->rims.options[g->brands[g->brand]+at].style);label=g->labels[i];
  } else label=g->catalog.menu[g->model.slot].options[at].label;
  /* Only genuine retail art. Missing identities are reported, never painted natively. */
  int slot=g->page==FEG_SLOTS?g->slots[at]:g->model.slot;
  uint32_t icon=g->page==FEG_HUB?hub_icons[at]:slot==GARAGE_RIM?0x0294d2a3u:slot_icons[slot];
  if(g->page==FEG_BRANDS||g->page==FEG_RIMS) {
   char key[64];const char *brand=g->rims.options[g->brands[g->page==FEG_BRANDS?at:g->brand]].brand;
   snprintf(key,sizeof key,"VISUAL_RIMS_BRAND_%s",brand);
   uint32_t hash=ug_hash_upper(key);if(gv_texture(s->assets,hash))icon=hash;
  }
  g->entry[i]=(GvEntry){gv_texture(s->assets,icon),label};
  if(!label||!g->entry[i].tex){snprintf(g->status,sizeof g->status,"Garage caption or icon unavailable");return 0;}
 }
 for(int i=visible;i<g->view.nslots;i++)s->scene.nodes[g->view.slot[i]].app_hidden=1;
 gc_set_entries(&g->carousel,visible,disabled);gc_snap_to(&g->carousel,g->cursor-g->first);
 gc_update(&g->carousel,ms/1000);
 const char *heading=g->page==FEG_BRANDS?gv_text(s->assets,0x94c5e7bfu,NULL):g->page==FEG_RIMS?gv_text(s->assets,0xe4504d8du,NULL):g->page==FEG_PARTS?(g->model.slot==GARAGE_KIT?"Body kit presets":gv_text(s->assets,ug_hash_upper(slot_keys[g->model.slot]),NULL)):
  gv_text(s->assets,ug_hash_upper(g->page==FEG_SLOTS?"CUST_CATEGORY_BODY":"UI_MAIN_CUSTOMIZE"),NULL);
 const char *title=g->model.status[0]?g->model.status:g->notice[0]?g->notice:heading;
 if(!title){snprintf(g->status,sizeof g->status,"Garage caption unavailable");return 0;}
 gv_carousel_apply(&g->view,&g->carousel,g->entry,visible,title,0,0);
 if(g->page==FEG_PARTS||g->page==FEG_RIMS) {
  int at=ug_scene_find_name(&s->scene,ug_hash_upper("PART_NAME"));
  if(at>=0&&g->cursor>=g->first&&g->cursor<g->first+visible)s->scene.nodes[at].text_override=g->entry[g->cursor-g->first].label;
 }
 return 1;
}
int feg_take(FeGarage *g,GarageRequest *r){return g&&garage_take(&g->model,r);}
void feg_ack(FeGarage *g,uint32_t serial,int ok,const char *error) {
 if(!g||!g->model.inflight||serial!=g->model.flight.serial)return;
 if(ok&&g->model.flight.action==GAR_KEEP&&g->save) {
  int rc=g->save(g->save_user,g->car,&g->model.flight.config);
  if(rc!=ST_OK) {
   GarageConfig physical=g->model.flight.config;
   snprintf(g->notice,sizeof g->notice,"Save failed (%d); previous config restored",rc);
   garage_ack(&g->model,serial,0,"Save failed; restoring previous config");
   g->model.displayed=physical;g->model.pending=g->model.baseline;
   g->model.action=GAR_RESTORE;g->model.quiet=GARAGE_PREVIEW_SECONDS;
   g->recovery=1;g->back_page=FEG_SLOTS;return;
  }
 }
 if(!ok&&g->recovery) {
  char message[160];snprintf(message,sizeof message,"Save failed; restore failed: %.115s",error?error:"host error");
  garage_ack(&g->model,serial,0,message);return;
 }
 garage_ack(&g->model,serial,ok,error);
}
const char *feg_status(const FeGarage *g){return !g?"Garage unavailable":g->model.status[0]?g->model.status:g->notice[0]?g->notice:g->status;}
