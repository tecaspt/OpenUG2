#include "garage.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int garage_config_valid(const GarageConfig *c) {
 if(!c||!memchr(c->rim_brand,0,sizeof c->rim_brand)||c->rim_style<0||c->rim_style>999||
 c->car.body_kit<0||c->car.body_kit>99||c->car.hood_style<0||c->car.hood_style>999||
 c->car.spoiler_style<0||c->car.spoiler_style>999||c->car.wheel_style_id<0||c->car.wheel_style_id>999)return 0;
 for(int i=0;i<N2_PART_COUNT;i++)if(c->car.parts[i]<0||c->car.parts[i]>5999)return 0;
 return 1;
}
void garage_init(Garage *g,const GarageCatalog *cat,const GarageConfig *c) {
 memset(g,0,sizeof *g);g->catalog=cat;g->slot=-1;
 if(c&&garage_config_valid(c))g->applied=g->pending=g->displayed=*c;
}
int garage_begin(Garage *g,int slot) {
 if(!g||!g->catalog||g->editing||g->inflight||slot<0||slot>=GARAGE_SLOTS||g->catalog->menu[slot].count<1||g->catalog->menu[slot].count>256)return 0;
 g->slot=slot;g->baseline=g->pending=g->applied;g->editing=1;g->event=0;g->status[0]=0;return 1;
}
int garage_select(Garage *g,int index) {
 if(!g||!g->editing||g->action>GAR_PREVIEW||(g->inflight&&g->flight.action>GAR_PREVIEW))return 0;
 const N2PartMenu *m=&g->catalog->menu[g->slot];if(index<0||index>=m->count)return 0;
 if(g->slot==GARAGE_KIT) {
  g->pending.car.body_kit=m->options[index].value;
  for(int p=0;p<N2_PART_SPOILER;p++)if(p!=N2_PART_ENGINE&&p!=N2_PART_AUDIO)g->pending.car.parts[p]=0;
 }else g->pending.car.parts[g->slot]=m->options[index].value;
 g->quiet=0;g->action=GAR_PREVIEW;g->status[0]=0;return 1;
}
void garage_finish(Garage *g,int keep) {
 if(!g||!g->editing||g->action>GAR_PREVIEW||(g->inflight&&g->flight.action>GAR_PREVIEW))return;
 if(!keep)g->pending=g->baseline;
 g->action=keep?GAR_KEEP:GAR_RESTORE;g->quiet=GARAGE_PREVIEW_SECONDS;
}
void garage_update(Garage *g,float dt){if(g&&isfinite(dt)&&dt>0)g->quiet=fminf(GARAGE_PREVIEW_SECONDS,g->quiet+dt);}
int garage_take(Garage *g,GarageRequest *r) {
 if(!g||!r||!g->action||g->inflight||g->quiet+1e-6f<GARAGE_PREVIEW_SECONDS)return 0;
 g->serial++;if(!g->serial)g->serial++;
 g->flight=(GarageRequest){g->serial,g->action,g->pending};g->action=0;g->inflight=1;*r=g->flight;return 1;
}
void garage_ack(Garage *g,uint32_t serial,int success,const char *error) {
 if(!g||!g->inflight||serial!=g->flight.serial)return;
 g->inflight=0;
 if(!success) {
  snprintf(g->status,sizeof g->status,"%s",error?error:"Part could not be applied");
  if(!g->action)g->pending=g->displayed;
  return;
 }
 g->displayed=g->flight.config;
 if(g->flight.action!=GAR_PREVIEW) {
  g->applied=g->pending=g->flight.config;g->editing=0;g->event=g->flight.action;
 }
}
