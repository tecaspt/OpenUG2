/* [data] Retail bindings; application flow and map naming are [inferred], see SPEC_GUI_FE. */
#include "fe_menu.h"
#include "fe_binding.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
/* [inferred] Reject missing/blank/numeric language placeholders; no invented event caption. */
int fet_name_is_real(const char *s) {
    if(!s)return 0;
    for(;*s;s++)if(!isspace((unsigned char)*s) && !isdigit((unsigned char)*s))return 1;
    return 0;
}
/* [inferred] Layout aliases require the complete 24-byte path-node records to match. */
int fet_same_layout(const void *a,size_t na,const void *b,size_t nb) {
    return a && b && na>0 && na%24==0 && na==nb && !memcmp(a,b,na);
}
/* [data] Inspected retail TRACKMAP DXT3 RGB is populated while every alpha nibble is zero.
 * [inferred] These map surfaces use opaque RGB, rather than sprite alpha blending. */
int fet_map_opaque(uint8_t *rgba,size_t pixels) {
    if(!rgba || !pixels)return 0;
    for(size_t i=0;i<pixels;i++)if(rgba[i*4+3])return 0;
    for(size_t i=0;i<pixels;i++)rgba[i*4+3]=255;
    return 1;
}
typedef struct {const unsigned char *bytes;size_t len;int count;} FeNodes;
static int find_nodes(void *user,const UgChunkRef *chunk) {
    FeNodes *nodes=user;
    if(chunk->id!=0x34148u)return 0;
    nodes->count++;nodes->bytes=chunk->payload;nodes->len=chunk->size;return 1;
}
static int load_nodes(const UgAssets *a,const GcEvent *e,UgBuf *buffer,FeNodes *nodes) {
    char path[768],error[160];memset(nodes,0,sizeof *nodes);memset(buffer,0,sizeof *buffer);
    snprintf(path,sizeof path,"%s/TRACKS/ROUTES%s/Paths%d.bin",a->dir,e->region,e->id);
    if(!ug_bundle_read(path,buffer,error,sizeof error))return 0;
    return ug_chunks_walk(buffer->data,buffer->len,find_nodes,nodes,error,sizeof error) && nodes->count==1;
}
static void resolve_names(FeChoice *c,const UgAssets *a) {
    for(int k=0;k<c->nevents;k++) {
        const GcEvent *e=&c->cat->ev[c->event_indices[k]];
        c->track_name_keys[k]=0;c->track_map_ids[k]=0;
        const char *own=ug_strtab_get(&a->strings,e->name_key);
        if(fet_name_is_real(own)){c->track_name_keys[k]=e->name_key;c->track_map_ids[k]=e->id;continue;}
        UgBuf source={0};FeNodes nodes;
        if(!load_nodes(a,e,&source,&nodes)){ug_buf_free(&source);continue;}
        const char *match=NULL;uint32_t key=0;int map=0,ambiguous=0;
        for(int j=0;j<c->cat->nev;j++) {
            const GcEvent *candidate=&c->cat->ev[j];
            const char *name=ug_strtab_get(&a->strings,candidate->name_key);
            if(candidate->kind!=e->kind || strcmp(candidate->region,e->region) || !fet_name_is_real(name))continue;
            UgBuf other={0};FeNodes other_nodes;
            if(load_nodes(a,candidate,&other,&other_nodes) &&
               fet_same_layout(nodes.bytes,nodes.len,other_nodes.bytes,other_nodes.len)) {
                if(match && strcmp(match,name))ambiguous=1;
                else if(!match){match=name;key=candidate->name_key;map=candidate->id;}
            }
            ug_buf_free(&other);
        }
        ug_buf_free(&source);
        if(!ambiguous){c->track_name_keys[k]=key;c->track_map_ids[k]=map;}
    }
}
typedef struct {UgTexLib lib;int failed;} FeMapLoad;
static int map_chunk(void *user,const UgChunkRef *chunk) {
    FeMapLoad *load=user;
    if(chunk->id!=0xb3300000u)return 0;
    char error[160];if(!ug_texpack_parse(&load->lib,chunk,error,sizeof error))load->failed=1;
    return 1;
}
static void load_maps(FeChoice *c,UgAssets *a) {
    for(int k=0;k<c->nevents;k++) {
        int id=c->track_map_ids[k];c->map_textures[k]=0;if(!id)continue;
        char name[48];snprintf(name,sizeof name,"TRACKMAP%d",id);
        int tex=gv_texture(a,ug_hash_upper(name));
        if(!tex && c->nmap_buffers<GC_MAX_EVENTS) {
            UgBuf data={0};char path[768],error[160];
            snprintf(path,sizeof path,"%s/TRACKS/TRACKMAP%d.BIN",a->dir,id);
            if(ug_bundle_read(path,&data,error,sizeof error)) {
                FeMapLoad load={0};ug_texlib_init(&load.lib);
                int ok=ug_chunks_walk(data.data,data.len,map_chunk,&load,error,sizeof error);
                if(ok && !load.failed && load.lib.n>0) {
                    int before=a->tex.n,total=before+load.lib.n;
                    UgTexture *grown=realloc(a->tex.t,(size_t)total*sizeof *grown);
                    if(grown) {
                        a->tex.t=grown;a->tex.cap=total;
                        for(int i=0;i<load.lib.n;i++) {
                            a->tex.t[a->tex.n++]=load.lib.t[i];
                            snprintf(a->tex.t[a->tex.n-1].source,sizeof a->tex.t[0].source,"%.639s",path);
                        }
                        load.lib.n=0;c->map_buffers[c->nmap_buffers++]=data;data=(UgBuf){0};
                        ug_texlib_index(&a->tex);tex=gv_texture(a,ug_hash_upper(name));
                    }
                }
                ug_texlib_free(&load.lib);ug_buf_free(&data);
            }
        }
        if(tex) {
            UgTexture *map=&a->tex.t[tex-1];
            if(map->fmt==UG_PF_DXT3)fet_map_opaque(map->rgba,(size_t)map->w*map->h);
        }
        c->map_textures[k]=tex;
    }
}
void fet_free(FeChoice *c) {
    if(!c)return;
    for(int i=0;i<c->nmap_buffers;i++)ug_buf_free(&c->map_buffers[i]);
    c->nmap_buffers=0;
}
static int validate(GvScreen *s,int set) {
    for(int i=0;i<fe_binding_sets[set].count;i++)
        if(fe_identity_find(s->pk,&fe_binding_sets[set].identity[i])<0) {
            snprintf(s->status,sizeof s->status,"%s: identity mismatch",s->name);return 0;
        }
    return 1;
}
int fet_open(FeChoice *c,GvScreen *s,UgAssets *a,int options) {
    if(!c->cat){snprintf(s->status,sizeof s->status,"event catalogue unavailable");return 0;}
    c->nevents=0;
    for(int i=0;i<c->cat->nev;i++)if(c->cat->ev[i].kind==c->kind)c->event_indices[c->nevents++]=i;
    if(!c->nevents){snprintf(s->status,sizeof s->status,"no events for race kind %d",c->kind);return 0;}
    if(c->selected<0 || c->selected>=c->nevents)c->selected=0;
    c->event_index=c->event_indices[c->selected];
    if(c->resolved_kind!=c->kind){resolve_names(c,a);load_maps(c,a);c->resolved_kind=c->kind;}
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
    const char *caption=ug_strtab_get(&s->assets->strings,c->track_name_keys[c->selected]);
    if(!fet_name_is_real(caption))caption="";
    c->track_status[0]=0;
    if(!*caption)snprintf(c->track_status,sizeof c->track_status,"track name unavailable for event %d: no unambiguous named layout",e->id);
    else if(!c->map_textures[c->selected])snprintf(c->track_status,sizeof c->track_status,"track map unavailable for event %d",e->id);
    snprintf(c->distance,sizeof c->distance,"%.2f km",e->length_m/1000.0);
    static const char *modes[]={"","QR_CIRCUIT","QR_SPRINT","QR_DRAG","QR_DRIFT","QR_STREET_CROSS","QR_URL"};
    int backing=ug_scene_find_name(&s->scene,ug_hash_upper("U2_CarSelect_icon_backing"));
    int pool_slot=0;
    for(int i=0;i<s->scene.n;i++) {
        UgNode *n=&s->scene.nodes[i];
        if(n->obj->guid==0xe2501)n->app_hidden=1;
        if(n->obj->guid==0xe231b)n->text_override=gv_text(s->assets,ug_hash_upper(modes[c->kind]),"");
        if(n->obj->guid==0xe231c){n->text_override=c->distance;n->app_hidden=e->length_m<=0;}
        if(n->obj->guid==0xe231d || n->obj->guid==0xe2103)n->app_hidden=1; /* no saved record */
        if(n->obj->guid==0xdf142)n->text_override=caption;
        /* [inferred] Reuse one authored pool image for the selected map; park the rest. */
        if(n->obj->name_hash==0xf5ff3dbc) {
            int tex=c->map_textures[c->selected];
            n->app_hidden=pool_slot++!=0 || !tex || backing<0;
            if(!n->app_hidden){n->tex_override=tex;ug_scene_app_set_world_pos(&s->scene,i,s->scene.nodes[backing].we,s->scene.nodes[backing].wf);}
        }
        if(n->obj->guid==0xe2305) {
            int tex=c->map_textures[c->selected];
            n->tex_override=tex;n->app_hidden=!tex;
        }
    }
    ug_scene_relayout(&s->scene);
}
