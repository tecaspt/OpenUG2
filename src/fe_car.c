#include "fe_menu.h"
#include "fe_binding.h"
#include <stdio.h>
#include <string.h>
int fec_open(FeChoice *c,GvScreen *s,const UgAssets *a) {
    c->ncars=0;
    if(c->cat)for(int i=0;i<c->cat->ncar;i++)if(!c->cat->car[i].traffic)c->cars[c->ncars++]=i;
    if(!c->ncars){snprintf(s->status,sizeof s->status,"no player cars in catalogue");return 0;}
    int found=0;for(int i=0;i<c->ncars;i++)if(c->cars[i]==c->car_index)found=1;
    if(!found)c->car_index=c->cars[0];
    c->preview_wait=0;c->preview[0]=0;
    if(!gv_screen_load(s,a,"UI_QRCarSelect.fng"))return 0;
    for(int i=0;i<fe_binding_sets[3].count;i++)if(fe_identity_find(s->pk,&fe_binding_sets[3].identity[i])<0){
        snprintf(s->status,sizeof s->status,"car selection identity mismatch");return 0;
    }
    return 1;
}
void fec_move(FeChoice *c,int direction) {
    int at=0;while(at<c->ncars && c->cars[at]!=c->car_index)at++;
    int next=at+direction;
    if(next<0 || next>=c->ncars)return;
    c->car_index=c->cars[next];c->preview_wait=0;c->preview[0]=0;
}
void fec_sync(FeChoice *c,GvScreen *s,float seconds) {
    if(!c->cat || !c->ncars)return;
    c->preview_wait+=seconds;
    const char *name=c->cat->car[c->car_index].name;
    if(c->preview_wait>=.3f)snprintf(c->preview,sizeof c->preview,"%s",name);
    for(int i=0;i<s->scene.n;i++) {
        UgNode *n=&s->scene.nodes[i];
        /* [inferred] Runtime car name replaces the authored default-string field. */
        if(n->obj->guid==0xdcc1b){n->text_override=name;n->app_hidden=0;float alpha=255;ug_scene_app_write(&s->scene,i,UG_P_COLOR+3,1,&alpha);}
        if(n->obj->guid==0xe694b)n->text_override=name;
        /* Unimplemented stats: hide the whole group rather than show authored example values. */
        if(n->obj->guid==0xe12b4)n->app_hidden=1;
        if(n->obj->guid==0xdcac4 || n->obj->guid==0xdcac7 || n->obj->guid==0xe1f56)n->app_hidden=1;
        if(n->obj->guid==0xe1e52)n->text_override=gv_text(s->assets,ug_hash_upper("QR_CAR_SELECT_TITLE"),"Quick Race Car Select");
    }
    ug_scene_relayout(&s->scene);
}
