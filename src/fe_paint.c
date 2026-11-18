#include "fe_paint.h"
#include "fe_binding.h"
#include "fe_screen.h"
#include <stdio.h>
#include <string.h>

/* [inferred] Body finishes are individual categories, each fits the retail 60-swatch palette.
 * Remaining categories combine their levels in the same palette (10+20+30). */
static const char *const names[] = {"Body: Gloss", "Body: Metallic", "Body: Pearl", "Rims", "Calipers", "Muffler", "Hoses", "Vinyl colours"};
static int clamp(int v,int n) { return v < 0 || n <= 0 ? 0 : v >= n ? n-1 : v; }
static void rows(FePaint *p) {
 p->nrows=0;
 if(!p->ncats)return;
 for(int i=0;i<p->shop.n && p->nrows<60;i++)
  if(p->shop.color[i].paint.group==p->cats[p->cat_sel])p->rows[p->nrows++]=i;
 p->sel=clamp(p->sel,p->nrows);
}
void fpt_open(FePaint *p,const FePaintShop *shop) {
 if(!p)return;
 memset(p,0,sizeof *p);
 if(shop)p->shop=*shop;
 p->shop.n=clamp(p->shop.n,CD_MAX_PAINT+1);
 for(int c=0;c<CD_PAINT_GROUPS;c++)for(int i=0;i<p->shop.n;i++)
  if(p->shop.color[i].paint.group==c){p->cats[p->ncats++]=c;break;}
 rows(p);
}
void fpt_refresh(FePaint *p,const FePaintShop *shop,FptMsg msg) {
 int page=p->page, cat=p->cat_sel, sel=p->sel;
 fpt_open(p,shop);p->page=page;p->cat_sel=clamp(cat,p->ncats);p->sel=sel;rows(p);p->msg=msg;
}
const FePaintColor *fpt_selected(const FePaint *p) {
 return p && p->page && p->nrows ? &p->shop.color[p->rows[p->sel]] : NULL;
}
FptEvent fpt_input(FePaint *p,int in) {
 if(!p)return FPT_EV_NONE;
 if(in==FES_IN_BACK){
  if(p->confirm){p->confirm=0;p->msg=FPT_MSG_NONE;return FPT_EV_NONE;}
  if(!p->page)return FPT_EV_LEAVE;
  p->page=0;p->msg=FPT_MSG_NONE;return FPT_EV_PAGE;
 }
 if(in==FES_IN_LEFT || in==FES_IN_RIGHT || in==5 || in==6){
  int d=in==FES_IN_LEFT?-1:in==FES_IN_RIGHT?1:in==5?-20:20;
  p->confirm=0;p->msg=FPT_MSG_NONE;
  if(p->page)p->sel=clamp(p->sel+d,p->nrows);
  else if(in<5){p->cat_sel=clamp(p->cat_sel+d,p->ncats);p->sel=0;rows(p);}
  return FPT_EV_NONE;
 }
 if(in!=FES_IN_ACCEPT || !p->ncats)return FPT_EV_NONE;
 if(!p->page){p->page=1;rows(p);return FPT_EV_PAGE;}
 const FePaintColor *c=fpt_selected(p);
 if(!c)return FPT_EV_NONE;
 FptMsg m=!c->open?FPT_MSG_LOCKED:c->installed?FPT_MSG_OWNED:c->price>p->shop.bank?FPT_MSG_NO_FUNDS:FPT_MSG_NONE;
 if(m){p->msg=m;p->confirm=0;return FPT_EV_NONE;}
 if(!p->confirm){p->confirm=1;p->msg=FPT_MSG_CONFIRM;return FPT_EV_NONE;}
 p->confirm=0;p->msg=FPT_MSG_NONE;p->buy_index=c->index;return FPT_EV_BUY;
}
const char *fpt_package(const FePaint *p) { return p && p->page ? "UI_Paint.fng" : "UI_ChoosePaintCategory.fng"; }
static int node(GvScreen *s,uint32_t hash) {
 for(int i=0;i<s->scene.n;i++)if(s->scene.nodes[i].obj->name_hash==hash)return i;
 return -1;
}
static void text(GvScreen *s,uint32_t hash,const char *v) {
 int i=node(s,hash);if(i>=0){s->scene.nodes[i].text_override=v;s->scene.nodes[i].app_hidden=!v;}
}
static void hide(GvScreen *s,uint32_t hash,int v) { int i=node(s,hash);if(i>=0)s->scene.nodes[i].app_hidden=v; }
int fpt_bind(FePaint *p,GvScreen *s,const UgAssets *a) {
 if(!p || !s || !gv_screen_load(s,a,fpt_package(p)))return 0;
 for(unsigned k=0;k<sizeof fe_binding_sets/sizeof fe_binding_sets[0];k++)if(!strcmp(fe_binding_sets[k].package,fpt_package(p)))
  for(int i=0;i<fe_binding_sets[k].count;i++)if(fe_identity_find(s->pk,&fe_binding_sets[k].identity[i])<0){
   snprintf(s->status,sizeof s->status,"paint shop identity mismatch");return 0;
  }
 return 1;
}
int fpt_sync(FePaint *p,GvScreen *s,float ms) {
 (void)ms;
 if(!p || !s || !s->loaded)return 0;
 const FePaintColor *c=fpt_selected(p);
 const char *cat=p->ncats?names[p->cats[p->cat_sel]]:"Paint";
 uint32_t key=p->msg==FPT_MSG_NO_FUNDS?0xd1ac60beu:p->msg==FPT_MSG_LOCKED?0xc9f7f7e5u:p->msg==FPT_MSG_OWNED?0xda15d7e5u:p->msg==FPT_MSG_CONFIRM?0x44b7167fu:0;
 snprintf(p->caption,sizeof p->caption,"%s",p->msg==FPT_MSG_SAVE?"Could not save paint purchase":key?gv_text(s->assets,key,""):"Paint Shop");
 snprintf(p->cash,sizeof p->cash,"%s $%lld",gv_text(s->assets,0x3298ccdbu,"Bank:"),p->shop.bank);
 snprintf(p->total,sizeof p->total,"$%d",c?c->price:0);
 snprintf(p->label,sizeof p->label,"%s",cat);
 snprintf(p->mark,sizeof p->mark,"%s",c && c->installed?"Installed":"");
 text(s,0x42adb44cu,p->caption);text(s,0xeb406fecu,NULL);text(s,0xeb3a688au,p->cash);
 text(s,0x2f5e7da3u,p->total);text(s,0x5e7b09c9u,p->label);
 /* [data] PAINT_ICON_1..6 are numbered paint-drop icons, inspected from FrontB.
  * [inferred] Body shares 1 across finishes; the other five groups use 2..6. */
 int icon=node(s,0xb04669e3u);
 if(icon>=0 && p->ncats) {
  char name[24];int group=p->cats[p->cat_sel];
  snprintf(name,sizeof name,"PAINT_ICON_%d",group<3?1:group-1);
  s->scene.nodes[icon].tex_override=ug_assets_texture_by_name(s->assets,name)+1;
  s->scene.nodes[icon].app_hidden=s->scene.nodes[icon].tex_override<=0;
 }
 int category_locked=1;
 for(int i=0;i<p->nrows;i++)if(p->shop.color[p->rows[i]].open)category_locked=0;
 hide(s,0xa2f11b13u,p->page?!(c && !c->open):!category_locked);
 if(!p->page){hide(s,0x9ff84a88u,1);ug_scene_relayout(&s->scene);return 1;}
 /* [data] The palette root's idle script starts transparent. The host owns its
  * visibility while this page is active, as the shared carousel owns its rail. */
 int palette=node(s,0xdb285767u);
 if(palette>=0){float alpha=255;ug_scene_app_write(&s->scene,palette,UG_P_COLOR+3,1,&alpha);}
 text(s,0xef653affu,p->label);text(s,0x0013c45eu,p->mark);
 hide(s,0x23a43844u,!(c && !c->open));
 /* Retail palette: 60 mini icons; index order is spatially reversed in its middle row. */
 int slots[60],n=0;
 for(int i=0;i<s->scene.n && n<60;i++) {
  const UgFngObject *o=s->scene.nodes[i].obj;
  if(o->type==UG_OBJ_IMAGE && o->size[0]==16 && o->size[1]==16 && o->parent_guid==0x000df227u)slots[n++]=i;
 }
 /* GUID parent is verified by the identity set. Order by authored position. */
 for(int i=0;i<n;i++)for(int j=i+1;j<n;j++){
  const UgFngObject *a=s->scene.nodes[slots[i]].obj,*b=s->scene.nodes[slots[j]].obj;
  if(b->pos[1]<a->pos[1] || (b->pos[1]==a->pos[1] && b->pos[0]<a->pos[0])){int t=slots[i];slots[i]=slots[j];slots[j]=t;}
 }
 if(n!=60){snprintf(p->status,sizeof p->status,"paint palette has %d of 60 swatches",n);return 0;}
 for(int i=0;i<n;i++){
  UgNode *v=&s->scene.nodes[slots[i]];v->app_hidden=i>=p->nrows;
  if(i>=p->nrows)continue;
  const FePaintColor *col=&p->shop.color[p->rows[i]];
  float rgba[4]={col->paint.rgb[0],col->paint.rgb[1],col->paint.rgb[2],col->open?255:100};
  ug_scene_app_write(&s->scene,slots[i],UG_P_COLOR,4,rgba);
 }
 int hi=node(s,0x9ba6d658u);
 if(hi>=0 && c){
  /* Convert palette-local coordinates through its parent into the root highlight. */
  UgNode *v=&s->scene.nodes[slots[p->sel]];
  ug_scene_relayout(&s->scene);
  float pos[3]={v->we,v->wf,0},alpha=255;
  ug_scene_app_write(&s->scene,hi,UG_P_POS,3,pos);
  ug_scene_app_write(&s->scene,hi,UG_P_COLOR+3,1,&alpha);
 }
 ug_scene_relayout(&s->scene);return 1;
}
