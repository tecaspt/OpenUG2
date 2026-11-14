/* [data] Retail bindings; application flow and map naming are [inferred], see SPEC_GUI_FE. */
#include "fe_menu.h"
#include "fe_binding.h"
#include <stdio.h>
#include <string.h>
static int validate(GvScreen *s,int set) {
    for(int i=0;i<fe_binding_sets[set].count;i++)
        if(fe_identity_find(s->pk,&fe_binding_sets[set].identity[i])<0) {
            snprintf(s->status,sizeof s->status,"%s: identity mismatch",s->name);return 0;
        }
    return 1;
}
int fet_open(FeChoice *c,GvScreen *s,const UgAssets *a,int options) {
    if(!c->cat){snprintf(s->status,sizeof s->status,"event catalogue unavailable");return 0;}
    c->nevents=0;
    for(int i=0;i<c->cat->nev;i++)if(c->cat->ev[i].kind==c->kind)c->event_indices[c->nevents++]=i;
    if(!c->nevents){snprintf(s->status,sizeof s->status,"no events for race kind %d",c->kind);return 0;}
    if(c->selected<0 || c->selected>=c->nevents)c->selected=0;
    c->event_index=c->event_indices[c->selected];
    const char *pkg=options?(c->options_page?"UI_QRModeOptions.fng":"UI_QRTrackOptions.fng"):"UI_QRTrackSelect.fng";
    if(!gv_screen_load(s,a,pkg) || !validate(s,options?(c->options_page?2:1):0))return 0;
    if(options)return gv_list_bind_rows(&c->list,s);
    return 1;
}
void fet_move(FeChoice *c,int dir,int vertical,int options) {
    if(options) {
        if(vertical)c->option=c->option?0:1;
        else if(c->option==0){c->laps+=dir;if(c->laps<1)c->laps=1;if(c->laps>8)c->laps=8;}
        else {c->difficulty+=dir;if(c->difficulty<0)c->difficulty=0;if(c->difficulty>2)c->difficulty=2;}
    } else {
        c->selected+=dir;
        if(c->selected<0)c->selected=0;
        if(c->selected>=c->nevents)c->selected=c->nevents-1;
        if(c->selected>=0)c->event_index=c->event_indices[c->selected];
    }
}
void fet_sync(FeChoice *c,GvScreen *s,float ms,int options) {
    if(options) {
        snprintf(c->laps_text,sizeof c->laps_text,"%d",c->laps);
        static const char *keys[]={"QR_DIFFICULTY_EASY","QR_DIFFICULTY_MEDIUM","QR_DIFFICULTY_HARD"};
        static const char *fallback[]={"Easy","Medium","Hard"};
        GvListRow rows[]={
            {gv_text(s->assets,ug_hash_upper("QR_LAPS"),"Laps"),c->laps_text,1},
            {gv_text(s->assets,ug_hash_upper("QR_DIFFICULTY"),"Difficulty"),gv_text(s->assets,ug_hash_upper(keys[c->difficulty]),fallback[c->difficulty]),1},
            {gv_text(s->assets,ug_hash_upper("QR_OPPONENTS"),"Opponents"),NULL,0},
            {gv_text(s->assets,ug_hash_upper("QR_TRAFFIC"),"Traffic Level"),NULL,0},
            {gv_text(s->assets,ug_hash_upper("QR_CATCH_UP"),"Catch Up"),NULL,0}};
        gv_list_sync_rows(&c->list,rows,5,c->option,ms);return;
    }
    if(c->event_index<0 || !c->cat)return;
    const GcEvent *e=&c->cat->ev[c->event_index];
    snprintf(c->label,sizeof c->label,"%d",e->id);
    snprintf(c->distance,sizeof c->distance,"%.2f km",e->length_m/1000.0);
    static const char *modes[]={"","QR_CIRCUIT","QR_SPRINT","QR_DRAG","QR_DRIFT","QR_STREET_CROSS","QR_URL"};
    for(int i=0;i<s->scene.n;i++) {
        UgNode *n=&s->scene.nodes[i];
        if(n->obj->guid==0xe2501)n->app_hidden=1;
        if(n->obj->guid==0xe231b)n->text_override=gv_text(s->assets,ug_hash_upper(modes[c->kind]),"");
        if(n->obj->guid==0xe231c){n->text_override=c->distance;n->app_hidden=e->length_m<=0;}
        if(n->obj->guid==0xe231d || n->obj->guid==0xe2103)n->app_hidden=1; /* no saved record */
        if(n->obj->guid==0xdf142)n->text_override=gv_text(s->assets,e->name_key,c->label);
        /* [data] Parked pool shares a missing UI_Icon_Map resource; omit unused pool. */
        if(n->obj->name_hash==0xf5ff3dbc)n->app_hidden=1;
        if(n->obj->guid==0xe2305) {
            char name[48];snprintf(name,sizeof name,"TRACK%d_MAP",e->id);
            int tex=gv_texture(s->assets,ug_hash_upper(name));
            n->tex_override=tex;n->app_hidden=!tex; /* never show Track4000 for another event */
        }
    }
    ug_scene_relayout(&s->scene);
}
