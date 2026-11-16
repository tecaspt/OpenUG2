#include "fe_worldmap.h"
#include "fe_screen.h"
#include "fe_binding.h"
#include "gui_view.h"
#include "hud_minimap.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

static int valid_projection(const FeMapProjection *p) {
 return p && isfinite(p->origin_x) && isfinite(p->origin_y) && isfinite(p->extent) && p->extent>0 &&
  isfinite(p->cx)&&isfinite(p->cy)&&isfinite(p->a)&&isfinite(p->b)&&isfinite(p->c)&&isfinite(p->d)&&
  isfinite((double)p->a*p->d-(double)p->b*p->c) && fabs((double)p->a*p->d-(double)p->b*p->c)>1e-12;
}
int fwm_project(const FeMapProjection *p,double x,double y,float *ox,float *oy) {
 if(!valid_projection(p)||!ox||!oy||!isfinite(x)||!isfinite(y))return 0;
 double u=(x-p->origin_x)/p->extent-.5,v=(p->origin_y-y)/p->extent-.5;
 double xx=p->cx+p->a*u+p->c*v,yy=p->cy+p->b*u+p->d*v;
 if(!isfinite(xx)||!isfinite(yy)||fabs(xx)>FLT_MAX||fabs(yy)>FLT_MAX)return 0;
 *ox=(float)xx;*oy=(float)yy;return 1;
}
int fwm_unproject(const FeMapProjection *p,float x,float y,double *ox,double *oy) {
 if(!valid_projection(p)||!ox||!oy||!isfinite(x)||!isfinite(y))return 0;
 double det=(double)p->a*p->d-(double)p->b*p->c,dx=x-p->cx,dy=y-p->cy;
 double u=(p->d*dx-p->c*dy)/det+.5,v=(-p->b*dx+p->a*dy)/det+.5;
 double xx=p->origin_x+u*p->extent,yy=p->origin_y-v*p->extent;
 if(!isfinite(xx)||!isfinite(yy))return 0;
 *ox=xx;*oy=yy;return 1;
}
int fwm_zone_contains(const float p[4][2],float x,float y) {
 if(!p || !isfinite(x) || !isfinite(y))return 0;
 for(int i=0;i<4;i++)if(!isfinite(p[i][0]) || !isfinite(p[i][1]))return 0;
 int inside=0;
 for(int i=0,j=3;i<4;j=i++) {
  float xi=p[i][0],yi=p[i][1],xj=p[j][0],yj=p[j][1];
  if((yi>y)!=(yj>y) && x<(xj-xi)*(y-yi)/(yj-yi)+xi)inside=!inside;
 }
 return inside;
}
static int visible(const FeMapProjection *p,double x,double y) {
 double u=(x-p->origin_x)/p->extent,v=(p->origin_y-y)/p->extent;
 return valid_projection(p)&&isfinite(u)&&isfinite(v)&&u>=0&&u<=1&&v>=0&&v<=1;
}
void fwm_open(FeWorldMap *m,const FeMapIcon *in,int n,float x,float y,float heading) {
 if(!m)return;
 struct FeWorldMapView *view=m->view;FeMapProjection p=m->projection;
 memset(m,0,sizeof *m);m->view=view;m->projection=p;m->selected=-1;
 m->player_x=x;m->player_y=y;m->heading=heading;
 if(!in||n<0)n=0;
 if(n>FWM_MAX_ICONS)n=FWM_MAX_ICONS;
 double best=HUGE_VAL;
 for(int i=0;i<n && m->n<FWM_MAX_ICONS;i++) {
  FeMapIcon q=in[i];if(!isfinite(q.x)||!isfinite(q.y)||(q.kind!=FWM_ICON_EVENT&&q.kind!=FWM_ICON_SHOP))continue;
  int j=m->n++;m->icon[j]=q;double dx=(double)q.x-x,dy=(double)q.y-y,dist=dx*dx+dy*dy;
  if(m->selected<0||dist<best){m->selected=j;best=dist;}
 }
}
int fwm_hit(const FeWorldMap *m,float x,float y,float radius) {
 if(!m||!isfinite(x)||!isfinite(y)||!isfinite(radius)||radius<0)return -1;
 double wx,wy;if(!fwm_unproject(&m->projection,x,y,&wx,&wy)||!visible(&m->projection,wx,wy))return -1;
 int best=-1;double bd=(double)radius*radius;
 for(int i=0;i<m->n;i++){float xx,yy;if(!visible(&m->projection,m->icon[i].x,m->icon[i].y)||!fwm_project(&m->projection,m->icon[i].x,m->icon[i].y,&xx,&yy))continue;
  double dx=(double)xx-x,dy=(double)yy-y,d=dx*dx+dy*dy;if(d<=bd&&(best<0||d<bd)){best=i;bd=d;}}
 return best;
}
int fwm_select(FeWorldMap *m,int i){if(!m||i<0||i>=m->n)return 0;m->selected=i;return 1;}
int fwm_touch_select(FeWorldMap *m,float x,float y,float radius) {
 int hit=fwm_hit(m,x,y,radius);
 if(hit<0)return -1;
 int start=hit;
 if(m->selected>=0 && m->selected<m->n &&
    m->icon[m->selected].x==m->icon[hit].x && m->icon[m->selected].y==m->icon[hit].y)
  start=(m->selected+1)%m->n;
 for(int j=0;j<m->n;j++) {
  int i=(start+j)%m->n;
  if(m->icon[i].x==m->icon[hit].x && m->icon[i].y==m->icon[hit].y) {
   m->selected=i;return i;
  }
 }
 return -1;
}
int fwm_input(FeWorldMap *m,int in,FeMapDestination *out) {
 if(in==FES_IN_BACK)return FWM_EV_LEAVE;
 if(!m)return FWM_EV_NONE;
 if(in==FWM_IN_NEXT || in==FWM_IN_PREVIOUS) {
  int step=in==FWM_IN_NEXT?1:-1;
  for(int j=1;j<=m->n;j++) {
   int base=m->selected>=0?m->selected:(step>0?-1:0);
   int i=(base+step*j+m->n)%m->n;
   if(visible(&m->projection,m->icon[i].x,m->icon[i].y)){m->selected=i;break;}
  }
  return FWM_EV_NONE;
 }
 if(in==FES_IN_ACCEPT){if(m->selected<0||m->selected>=m->n||!out)return FWM_EV_NONE;FeMapIcon *q=&m->icon[m->selected];if(!visible(&m->projection,q->x,q->y))return FWM_EV_NONE;*out=(FeMapDestination){q->x,q->y,q->kind,q->id};return FWM_EV_DESTINATION;}
 if(in!=FES_IN_LEFT&&in!=FES_IN_RIGHT&&in!=5&&in!=6)return FWM_EV_NONE;
 if(m->selected<0||m->selected>=m->n)return FWM_EV_NONE;
 float sx,sy;if(!fwm_project(&m->projection,m->icon[m->selected].x,m->icon[m->selected].y,&sx,&sy))return FWM_EV_NONE;
 int best=-1;double bd=HUGE_VAL;
 for(int i=0;i<m->n;i++){float x,y;if(i==m->selected||!visible(&m->projection,m->icon[i].x,m->icon[i].y)||!fwm_project(&m->projection,m->icon[i].x,m->icon[i].y,&x,&y))continue;
  double dx=x-sx,dy=y-sy,forward=in==FES_IN_LEFT?-dx:in==FES_IN_RIGHT?dx:in==5?-dy:dy;
  if(forward<=0)continue;
  double d=dx*dx+dy*dy, perpendicular=(in==FES_IN_LEFT||in==FES_IN_RIGHT)?dy:dx;
  double score=d*(1+4*perpendicular*perpendicular/(forward*forward));
  if(score<bd){best=i;bd=score;}}
 if(best>=0)m->selected=best;
 return FWM_EV_NONE;
}

struct FeWorldMapView { UgBuf buffers[2]; int nbuf; int map,mini,highlight,car,mini_node,highlight_node; };
typedef struct MapRead { int count,bad; double x,y,extent; } MapRead;
static uint32_t rd32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float rdf(const unsigned char *p){uint32_t v=rd32(p);float f;memcpy(&f,&v,4);return f;}
static int metadata(void *u,const UgChunkRef *c){
 MapRead *r=u;if(c->id!=0x34201u)return 0;if(c->size%296){r->bad=1;return 1;}
 for(size_t i=0;i<c->size;i+=296){const unsigned char *p=c->payload+i;if(rd32(p+140)==4000){r->count++;r->x=rdf(p+172);r->y=rdf(p+176);r->extent=rdf(p+180);}}
 return 1;
}
typedef struct PackRead {UgTexLib lib;int failed;} PackRead;
static int pack_chunk(void *u,const UgChunkRef *c){PackRead *p=u;if(c->id!=0xb3300000u)return 0;char err[160];if(!ug_texpack_parse(&p->lib,c,err,sizeof err))p->failed=1;return 1;}
static int add_pack(FeWorldMap *m,UgAssets *a,const char *name){
 struct FeWorldMapView *v=m->view;if(v->nbuf>=2)return 0;
 char path[768],err[160];snprintf(path,sizeof path,"%s/%s",a->dir,name);UgBuf data={0};
 if(!ug_bundle_read(path,&data,err,sizeof err))return 0;
 PackRead p={0};ug_texlib_init(&p.lib);int ok=ug_chunks_walk(data.data,data.len,pack_chunk,&p,err,sizeof err);
 if(ok&&!p.failed&&p.lib.n){int total=a->tex.n+p.lib.n;UgTexture *t=realloc(a->tex.t,(size_t)total*sizeof *t);
  if(t){a->tex.t=t;a->tex.cap=total;for(int i=0;i<p.lib.n;i++){a->tex.t[a->tex.n++]=p.lib.t[i];snprintf(a->tex.t[a->tex.n-1].source,sizeof a->tex.t[0].source,"%.639s",path);}p.lib.n=0;v->buffers[v->nbuf++]=data;data=(UgBuf){0};ug_texlib_index(&a->tex);}else ok=0;
 }else ok=0;ug_texlib_free(&p.lib);ug_buf_free(&data);return ok;
}
static int fail(FeWorldMap *m,GvScreen *s,const char *msg){snprintf(m->status,sizeof m->status,"%s",msg);if(s)snprintf(s->status,sizeof s->status,"%s",msg);return 0;}
int fwm_bind(FeWorldMap *m,GvScreen *s,UgAssets *a){
 if(!m||!s||!a)return 0;
 if(!isfinite(m->player_x)||!isfinite(m->player_y)||!isfinite(m->heading))return fail(m,s,"world map player pose invalid");
 if(!m->view){m->view=calloc(1,sizeof *m->view);if(!m->view)return fail(m,s,"world map allocation failed");}
 /* [data] Required language labels carried by the authored screen. */
 if(!ug_strtab_get(&a->strings,0xa0c6ae56u) || !ug_strtab_get(&a->strings,0xdaac95f9u))
  return fail(m,s,"world map title or Select Event language key unavailable");
 if(!gv_screen_load(s,a,"UI_CareerWorldMap.fng")){snprintf(m->status,sizeof m->status,"%s",s->status);return 0;}
 int set=-1;for(unsigned i=0;i<sizeof fe_binding_sets/sizeof fe_binding_sets[0];i++)if(!strcmp(fe_binding_sets[i].package,"UI_CareerWorldMap.fng")){set=(int)i;break;}
 if(set<0)return fail(m,s,"world map identity set missing");
 if(s->pk->nobj!=fe_binding_sets[set].count)return fail(m,s,"world map object count mismatch");
 for(int i=0;i<fe_binding_sets[set].count;i++)if(fe_identity_find(s->pk,&fe_binding_sets[set].identity[i])<0)return fail(m,s,"world map identity mismatch");
 char path[768],err[160];UgBuf data={0};MapRead r={0};snprintf(path,sizeof path,"%s/GLOBAL/GLOBALB.BUN",a->dir);
 if(!ug_bundle_read(path,&data,err,sizeof err))return fail(m,s,"world map TrackInfo GLOBALB unavailable");
 int ok=ug_chunks_walk(data.data,data.len,metadata,&r,err,sizeof err);ug_buf_free(&data);
 if(!ok||r.bad||r.count!=1||!isfinite(r.x)||!isfinite(r.y)||!isfinite(r.extent)||r.extent<=0)return fail(m,s,"world map TrackInfo 4000 invalid or ambiguous");
 m->projection.origin_x=r.x;m->projection.origin_y=r.y;m->projection.extent=r.extent;
 struct FeWorldMapView *v=m->view;
 v->map=gv_texture(a,ug_hash_upper("TRACKMAP4000"));if(!v->map){if(!add_pack(m,a,"TRACKS/TRACKMAP4000.BIN"))return fail(m,s,"world map TRACKMAP4000 pack unavailable");v->map=gv_texture(a,ug_hash_upper("TRACKMAP4000"));}
 v->mini=ug_assets_texture_for(a,"U2_mini_icon.tga")+1;v->highlight=ug_assets_texture_for(a,"U2_mini_icon_highlight.tga")+1;
 v->car=ug_assets_texture_for(a,"Minimap_Icon_Car.tga")+1;
 if(!v->car){if(!add_pack(m,a,"GLOBAL/InGameFreeRoam.bun"))return fail(m,s,"world map player texture pack unavailable");v->car=ug_assets_texture_for(a,"Minimap_Icon_Car.tga")+1;}
 if(!v->map||!v->mini||!v->highlight||!v->car)return fail(m,s,"world map required retail texture unavailable");
 int ids[]={v->map,v->mini,v->highlight,v->car};for(unsigned i=0;i<4;i++)if(!ug_texlib_pixels(&a->tex,ids[i]-1))return fail(m,s,"world map required retail texture undecodable");
 UgTexture *map=&a->tex.t[v->map-1];if(map->fmt==UG_PF_DXT3){int zero=1;size_t count=(size_t)map->w*map->h;for(size_t i=0;i<count;i++)if(map->rgba[4*i+3]){zero=0;break;}if(zero)for(size_t i=0;i<count;i++)map->rgba[4*i+3]=255;}
 int node=ug_scene_find_guid(&s->scene,0xe252e);if(node<0)return fail(m,s,"world map node missing");s->scene.nodes[node].tex_override=v->map;
 v->mini_node=ug_scene_find_guid(&s->scene,0xe3117);
 v->highlight_node=ug_scene_find_guid(&s->scene,0xe3114);
 if(v->mini_node<0||v->highlight_node<0)return fail(m,s,"world map icon templates missing");
 m->status[0]=0;return fwm_sync(m,s);
}
int fwm_sync(FeWorldMap *m,GvScreen *s){
 if(!m||!m->view||!s||!s->loaded)return 0;
 int index=ug_scene_find_guid(&s->scene,0xe252e);if(index<0)return fail(m,s,"world map node missing");
 UgNode *n=&s->scene.nodes[index];float w=n->props.w[UG_P_SIZE],h=n->props.w[UG_P_SIZE+1];
 m->projection.cx=n->we;m->projection.cy=n->wf;m->projection.a=n->wa*w;m->projection.b=n->wb*w;m->projection.c=n->wc*h;m->projection.d=n->wd*h;
 if(!valid_projection(&m->projection))return fail(m,s,"world map node transform invalid");
 for(int i=0;i<s->scene.n;i++){UgNode *q=&s->scene.nodes[i];uint32_t g=q->obj->guid;
  if(g==0xe551d||g==0xe551e||g==0xe5524||g==0xe5525||g==0xe6a31||g==0xe6794||g==0xe6854||g==0xe6866||q->obj->name_hash==0xa334a7bcu||g==0xe551d||g==0xe551e||g==0xe3114||g==0xe3112||g==0xe3115||g==0xe3116||g==0xe3118||g==0xdb8e9||g==0xdf163||g==0xdf164||g==0xe25de||g==0xe336a||g==0xe36de||g==0xe36dd||g==0xe25dd||g==0xe33c5||g==0xe25dc||g==0xe33c9)q->app_hidden=1;
  if(g==0xe336a&&m->selected>=0&&m->selected<m->n){const char *label=ug_strtab_get(&s->assets->strings,m->icon[m->selected].label_key);if(label&&*label){q->text_override=label;q->app_hidden=0;}}
 }
 ug_scene_relayout(&s->scene);return 1;
}
/* Clip textured triangles to the map's affine frame. Original art/UVs are
 * interpolated at the border; no replacement geometry or icon art is introduced. */
static double clip_axis(const FeMapProjection *p, const UgVertex *v, int axis) {
 double det=(double)p->a*p->d-(double)p->b*p->c;
 double x=v->x-p->cx,y=v->y-p->cy;
 return axis ? (-p->b*x+p->a*y)/det : (p->d*x-p->c*y)/det;
}
static int clip_images(UgCanvas *cv,int first,const FeMapProjection *p) {
 if(cv->n==first)return ug_cv_visual_status(cv)==UG_VIS_OK;
 UgVertex result[64]; int written=0;
 for(int at=first;at+2<cv->n;at+=3) {
  UgVertex poly[12],next[12]; int count=3;
  memcpy(poly,cv->v+at,3*sizeof poly[0]);
  for(int edge=0;edge<4 && count;edge++) {
   int n=0,axis=edge/2; double bound=(edge&1)?.5:-.5;
   for(int i=0;i<count;i++) {
    UgVertex a=poly[i],b=poly[(i+1)%count];
    double av=clip_axis(p,&a,axis),bv=clip_axis(p,&b,axis);
    int ai=(edge&1)?av<=bound:av>=bound,bi=(edge&1)?bv<=bound:bv>=bound;
    if(ai)next[n++]=a;
    if(ai!=bi) {
     float t=(float)((bound-av)/(bv-av)); UgVertex v=a;
     v.x=a.x+t*(b.x-a.x);v.y=a.y+t*(b.y-a.y);
     v.lx=a.lx+t*(b.lx-a.lx);v.ly=a.ly+t*(b.ly-a.ly);
     next[n++]=v;
    }
   }
   count=n;memcpy(poly,next,(size_t)n*sizeof poly[0]);
  }
  for(int i=1;i+1<count;i++) {
   if(written+3>(int)(sizeof result/sizeof result[0]))return 0;
   result[written++]=poly[0];result[written++]=poly[i];result[written++]=poly[i+1];
  }
 }
 if(first+written>cv->cap) {
  int cap=first+written; UgVertex *grown=realloc(cv->v,(size_t)cap*sizeof *grown);
  if(!grown)return 0;
  cv->v=grown;cv->cap=cap;
 }
 memcpy(cv->v+first,result,(size_t)written*sizeof result[0]);cv->n=first+written;
 return 1;
}
int fwm_draw(FeWorldMap *m,GvScreen *s,UgCanvas *cv){
 if(!m||!m->view||!s||!cv||m->status[0]||!valid_projection(&m->projection))return 0;
 struct FeWorldMapView *v=m->view;
 const UgFngObject *mini=s->scene.nodes[v->mini_node].obj;
 const UgFngObject *highlight=s->scene.nodes[v->highlight_node].obj;
 for(int i=0;i<m->n;i++){FeMapIcon *q=&m->icon[i];float x,y;if(!visible(&m->projection,q->x,q->y)||!fwm_project(&m->projection,q->x,q->y,&x,&y))continue;
  unsigned char rgb[3];int type=q->kind==FWM_ICON_EVENT?HMI_EVENT:HMI_SHOP;if(!hud_minimap_tint(type,q->variant,rgb))hud_minimap_tint(type,0,rgb);
  int first=cv->n;
  if(i==m->selected)ug_cv_image_rect(cv,UG_RECT_C(x,y,highlight->size[0],highlight->size[1]),v->highlight,highlight->color);
  ug_cv_image_rect(cv,UG_RECT_C(x,y,mini->size[0],mini->size[1]),v->mini,UG_RGBA(rgb[0],rgb[1],rgb[2],mini->color.a));
  if(!clip_images(cv,first,&m->projection))return fail(m,s,"world map icon clipping allocation failed");
 }
 float x,y;if(isfinite(m->heading)&&visible(&m->projection,m->player_x,m->player_y)&&fwm_project(&m->projection,m->player_x,m->player_y,&x,&y)){
  float dx=m->projection.a*cosf(m->heading)-m->projection.c*sinf(m->heading);
  float dy=m->projection.b*cosf(m->heading)-m->projection.d*sinf(m->heading);
  int first=cv->n;
  ug_cv_push(cv);ug_cv_translate(cv,x,y);ug_cv_rotate(cv,atan2f(dx,-dy));ug_cv_image_rect(cv,UG_RECT_C(0,0,16,16),v->car,UG_RGBA(48,207,48,255));ug_cv_pop(cv);
  if(!clip_images(cv,first,&m->projection))return fail(m,s,"world map player clipping allocation failed");
 }return 1;
}
void fwm_destroy(FeWorldMap *m){if(!m||!m->view)return;for(int i=0;i<m->view->nbuf;i++)ug_buf_free(&m->view->buffers[i]);free(m->view);m->view=NULL;}
