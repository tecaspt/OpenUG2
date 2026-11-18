/* [inferred] Results actions follow the M1 contract, not executable-derived behavior. */
#include "postrace.h"
#include "race_rules.h"
#include "race.h"
#include "gui_view.h"
#include "fe_binding.h"
#include "pause_menu_layout.h"
#include "fe_worldmap.h"
#include "ugui/ugui_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PR_DNF_RANK_SHIFT 18.0f
struct PostRace {
    UgAssets assets;UgSkin skin;UgGL *gl;UgCanvas canvas;UgCtx ui;
    GvScreen result,menu,family,mapbase;FeWorldMap map;GvCarousel view;GuiCarousel carousel;GvEntry entries[3];PostRaceInfo info;
    int open,visual,pending,pressed,phase,flow[7],flow_count,flow_at;
    char values[12][64];
    char status[256],place[16],time[32],title[384];
    char row_points[RR_MAX_CARS][32],row_places[RR_MAX_CARS][16],row_times[RR_MAX_CARS][32];
};
static void text(GvScreen *s,const char *name,const char *value) {
    int i=ug_scene_find_name(&s->scene,ug_hash_upper(name));
    if(i>=0)s->scene.nodes[i].text_override=value;
}
/* [inferred] The authored rank column fits one digit; DNF needs leftward room. */
static void rank_label(GvScreen *s,const char *name,const char *value) {
    int i=ug_scene_find_name(&s->scene,ug_hash_upper(name));
    if(i<0)return;
    UgNode *n=&s->scene.nodes[i];n->text_override=value;
    if(!strcmp(value,"DNF"))ug_scene_app_set_world_pos(&s->scene,i,n->we-PR_DNF_RANK_SHIFT,n->wf);
}
/* [inferred] Cross-package order follows Next, with stats Back returning to the
 * preceding page; career map Accept precedes the existing action menu. */
int pr_flow(const PostRaceInfo *info,int screens[7]) {
    if(!info || !screens)return 0;
    int n=0;
    screens[n++]=info->have_url_standings?PR_SCREEN_TOURN_RESULTS:PR_SCREEN_RESULTS;
    if(info->have_url_standings)screens[n++]=PR_SCREEN_TOURN_STANDINGS;
    if(info->career && info->reward.available)screens[n++]=PR_SCREEN_REWARD;
    if(info->kind>=N2_RACE_CIRCUIT && info->kind<=N2_RACE_URL)screens[n++]=PR_SCREEN_STATS;
    if(info->career)screens[n++]=PR_SCREEN_WORLD_MAP;
    screens[n++]=PR_SCREEN_MENU;return n;
}
int pr_screen(const PostRace *p){return p && p->open?p->phase:-1;}
const PostRaceInfo *pr_info(const PostRace *p){return p?&p->info:NULL;}
const char *pr_bound_text(const PostRace *p,uint32_t hash) {
    if(!p)return NULL;
    const GvScreen *s=p->phase==PR_SCREEN_RESULTS?&p->result:p->phase==PR_SCREEN_MENU?&p->menu:&p->family;
    int at=ug_scene_find_name(&s->scene,hash);
    return at>=0?s->scene.nodes[at].text_override:NULL;
}
static void hash_text(GvScreen *s,uint32_t hash,const char *value) {
    int node=ug_scene_find_name(&s->scene,hash);
    if(node>=0){s->scene.nodes[node].text_override=value;s->scene.nodes[node].app_hidden=!value[0];}
}
static void format_time(char out[64],double seconds) {
    if(!isfinite(seconds) || seconds<=0){out[0]=0;return;}
    unsigned c=(unsigned)(fmin(seconds,359999)*100);
    snprintf(out,64,"%u:%02u.%02u",c/6000,c/100%60,c%100);
}
static int family_load(PostRace *p,const char *name) {
    gv_screen_free(&p->family);
    if(!gv_screen_load(&p->family,&p->assets,name))goto fail;
    for(unsigned k=0;k<sizeof fe_binding_sets/sizeof fe_binding_sets[0];k++) {
        const FeBindingSet *set=&fe_binding_sets[k];if(strcmp(set->package,name))continue;
        for(int j=0;j<set->count;j++)if(fe_identity_find(p->family.pk,&set->identity[j])<0) {
            snprintf(p->family.status,sizeof p->family.status,"postrace: family identity mismatch");goto fail;
        }
        return 1;
    }
    snprintf(p->family.status,sizeof p->family.status,"postrace: missing family identities");
fail:
    p->visual=0;snprintf(p->status,sizeof p->status,"%s",p->family.status);return 0;
}
static void family_open(PostRace *p) {
    static const char *names[]={"UI_PostRaceResults.fng","UI_PostRace_TournResults.fng",
        "UI_PostRace_TournStandings.fng","UI_PostRaceReward.fng","UI_PostRaceStats.fng",
        "UI_PostRaceWorldMap.fng"};
    /* Flow state remains testable when assets/renderer are unavailable. */
    if(!p->gl)return;
    if(p->phase==PR_SCREEN_RESULTS){p->visual=p->result.loaded;return;}
    if(!family_load(p,names[p->phase]))return;
    p->visual=1;p->status[0]=0;
    GvScreen *s=&p->family;PostRaceInfo *info=&p->info;
    /* Suppress authored sample entrants, timer and controller-token placeholders.
     * Text is exposed explicitly only when its measured value is available. */
    for(int i=0;i<s->scene.n;i++) {
        UgNode *n=&s->scene.nodes[i];const char *t=n->obj->text;
        if(n->obj->is_text && t && (strstr(t,"$JOY") || strstr(t,"Timmy") ||
           strstr(t,"Event Handler") || strstr(t,"Overtime") || strstr(t,"45 Sec")))n->app_hidden=1;
    }
    if(p->phase==PR_SCREEN_REWARD) {
        snprintf(p->values[0],64,"%u",info->reward.reputation);
        snprintf(p->values[1],64,"%u",info->reward.cash);
        hash_text(s,0x18d97a23u,p->values[0]);hash_text(s,0x18d97a24u,p->values[1]);
        hash_text(s,0xeb406fecu,info->car);
    } else if(p->phase==PR_SCREEN_STATS) {
        /* [data] The stats package stores its reusable rows below the viewport.
         * [inferred] Bring only measured player rows into its authored frame. */
        for(int i=0;i<s->scene.n;i++) {
            UgNode *n=&s->scene.nodes[i];const char *t=n->obj->text;
            if(n->obj->is_text && t && (!strcmp(t,"option_data") || !strcmp(t,"option_name") ||
               !strcmp(t,"Race Time") || !strcmp(t,"Position")))n->app_hidden=1;
        }
        const char *labels[]={"Race Time","Top Speed (km/h)","Average Speed (km/h)","Best Lap",
                              "Drift Points","Banked Drifts","Cancelled Drifts"};
        memset(p->values,0,sizeof p->values);
        format_time(p->values[0],info->seconds);
        if(info->have_top_speed && isfinite(info->top_speed_kmh) && info->top_speed_kmh>=0)
            snprintf(p->values[1],64,"%.1f",info->top_speed_kmh);
        if(info->have_average_speed && isfinite(info->average_speed_kmh) && info->average_speed_kmh>=0)
            snprintf(p->values[2],64,"%.1f",info->average_speed_kmh);
        if(info->have_best_lap)format_time(p->values[3],info->best_lap);
        if(info->have_drift_points)snprintf(p->values[4],64,"%u",info->drift_points);
        if(info->have_drift_chains) {
            snprintf(p->values[5],64,"%u",info->drift_banked);
            snprintf(p->values[6],64,"%u",info->drift_cancelled);
        }
        for(int row=0;row<7;row++)for(int col=0;col<2;col++) {
            uint32_t hash=(col?0x9b483b01u:0xb2980ba8u)+(unsigned)row;
            int at=ug_scene_find_name(&s->scene,hash);if(at<0)continue;
            UgNode *n=&s->scene.nodes[at];n->text_override=col?p->values[row]:labels[row];
            float white[4]={255,255,255,255};ug_scene_app_write(&s->scene,at,UG_P_COLOR,4,white);
            float z=-1;ug_scene_app_write(&s->scene,at,UG_P_POS+2,1,&z);
            n->app_hidden=0;ug_scene_app_set_world_pos(&s->scene,at,col?260:-275,-75+row*30);
        }
        hash_text(s,0xb4701445u,info->car);
    } else if(p->phase==PR_SCREEN_TOURN_RESULTS || p->phase==PR_SCREEN_TOURN_STANDINGS) {
        int total=p->phase==PR_SCREEN_TOURN_STANDINGS;
        int count=info->standing_count;if(count<0)count=0;if(count>RR_MAX_CARS)count=RR_MAX_CARS;
        PostRaceStanding *field=total?info->standing:info->round_standing;
        int order[RR_MAX_CARS];for(int i=0;i<count;i++)order[i]=i;
        for(int i=1;i<count;i++){int v=order[i],j=i;
            while(j>0 && (field[order[j-1]].place?field[order[j-1]].place:RR_MAX_CARS+1)>(field[v].place?field[v].place:RR_MAX_CARS+1)){order[j]=order[j-1];j--;}order[j]=v;}
        for(int row=0;row<RR_MAX_CARS;row++) {
            PostRaceStanding *entry=row<count && (total || info->have_round_standings)?&field[order[row]]:NULL;
            if(entry){entry->name[31]=entry->car[31]=0;
                snprintf(p->row_places[row],16,entry->place?"%d":"DNF",entry->place);
                if(total)snprintf(p->row_points[row],32,"%u",entry->points);
                else {char value[64];format_time(value,entry->place?entry->seconds:0);snprintf(p->row_points[row],32,"%.31s",value);}
            }
            const uint32_t hashes[]={0xa5cc56b5u,0xda2e9a32u,0xfe58e79eu,total?0xf2028e7eu:0xa3ea9c37u};
            const char *values[]={entry?entry->name:"",entry?p->row_places[row]:"",entry?entry->car:"",entry?p->row_points[row]:""};
            for(int col=0;col<4;col++)hash_text(s,hashes[col]+(unsigned)row,values[col]);
            if(!total) {
                snprintf(p->values[row],64,"%u",entry?entry->points:0);
                hash_text(s,0xf2028e7eu+(unsigned)row,entry?p->values[row]:"");
            }
        }
        hash_text(s,0x9b8c9d7au,"");hash_text(s,0xf6d3c68au,"");
    } else if(p->phase==PR_SCREEN_WORLD_MAP) {
        for(int i=0;i<s->scene.n;i++)if(s->scene.nodes[i].obj->is_text &&
            s->scene.nodes[i].obj->name_hash!=0x42adb44cu)s->scene.nodes[i].app_hidden=1;
        fwm_open(&p->map,NULL,0,info->player_x,info->player_y,info->heading);
        gv_screen_free(&p->mapbase);
        if(!fwm_bind(&p->map,&p->mapbase,&p->assets)) {
            p->visual=0;snprintf(p->status,sizeof p->status,"%s",p->map.status);return;
        }
        for(int i=0;i<p->mapbase.scene.n;i++)p->mapbase.scene.nodes[i].app_hidden=
            p->mapbase.scene.nodes[i].obj->guid!=0xe252e;
        int mapnode=ug_scene_find_guid(&p->mapbase.scene,0xe252e);
        for(int node=mapnode;node>=0;node=p->mapbase.scene.nodes[node].parent)
            p->mapbase.scene.nodes[node].app_hidden=0;
        int hint=ug_scene_find_guid(&p->mapbase.scene,0xe6af6);
        if(hint>=0)p->mapbase.scene.nodes[hint].text_override="";
    }
    /* [inferred] A shared bottom-right Next target keeps all input methods aligned.
     * The stats package has only Back; its reused caption denotes forward flow. */
    uint32_t next=p->phase==PR_SCREEN_STATS?0x1c7fcf8bu:p->phase==PR_SCREEN_WORLD_MAP?0x74806286u:0x0019de3eu;
    int at=ug_scene_find_name(&s->scene,next);
    if(at>=0){for(int j=0;j<s->pkg.nobj;j++)if(s->pkg.obj[j].guid==s->scene.nodes[at].obj->guid)
            s->pkg.obj[j].flags&=~(UG_OF_CONSOLE|UG_OF_INVISIBLE|4u);
        hash_text(s,next,p->phase==PR_SCREEN_WORLD_MAP?"Accept":"Next");
        ug_scene_app_set_world_pos(&s->scene,at,200,180);
    }
    s->scene.hide_hint_text=0;
}

PostRace *pr_create(const char *root) {
    PostRace *p=calloc(1,sizeof *p);if(!p)return NULL;
    ug_cv_init(&p->canvas);ug_ctx_init(&p->ui);
    if(!ug_assets_open_ingame(&p->assets,root,NULL,p->status,sizeof p->status))return p;
    if(!ug_assets_main_face(&p->assets)){snprintf(p->status,sizeof p->status,"postrace: font unavailable");return p;}
    ug_skin_resolve(&p->skin,&p->assets);
    p->gl=ug_gl_create();if(p->gl)ug_gl_set_texlib(p->gl,&p->assets.tex);
    return p;
}
void pr_open(PostRace *p,const PostRaceInfo *info) {
    if(!p || !info)return;
    gv_screen_free(&p->result);p->info=*info;p->info.car[31]=0;
    p->title[0]=0;
    p->flow_count=pr_flow(info,p->flow);p->flow_at=0;p->phase=p->flow[0];p->open=1;p->pending=0;p->pressed=0;p->visual=0;
    if(!p->gl){snprintf(p->status,sizeof p->status,"postrace: renderer unavailable");return;}
    if(!gv_screen_load(&p->result,&p->assets,"UI_PostRaceResults.fng"))goto fail;
    for(int i=0;i<fe_binding_sets[5].count;i++)if(fe_identity_find(p->result.pk,&fe_binding_sets[5].identity[i])<0){
        snprintf(p->result.status,sizeof p->result.status,"postrace: results identity mismatch");goto fail;
    }
    snprintf(p->place,sizeof p->place,"%d",info->finish_place);
    double seconds=isfinite(info->seconds) && info->seconds>0?info->seconds:0;
    if(seconds>359999)seconds=359999;
    unsigned centis=(unsigned)(seconds*100);
    snprintf(p->time,sizeof p->time,"%u:%02u.%02u",centis/6000,centis/100%60,centis%100);
    if(info->kind==N2_RACE_DRIFT && info->have_drift_points) {
        uint32_t points=info->drift_points>(uint32_t)RR_SCORE_LIMIT?(uint32_t)RR_SCORE_LIMIT:info->drift_points;
        const char *title=gv_text(&p->assets,0x70802830u,NULL); /* PRR_POINTS_TITLE */
        if(!title || !*title){snprintf(p->result.status,sizeof p->result.status,"postrace: points caption unavailable");goto fail;}
        snprintf(p->time,sizeof p->time,"%u",points);
        text(&p->result,"Header_Text_3",title);
    }
    text(&p->result,"player_data_1",gv_text(&p->assets,0xfa37740bu,"Player"));   /* LABEL_PLAYER (no PLAYER key in the table) */
    rank_label(&p->result,"ranking_data_1",info->failed?"DNF":p->place);
    text(&p->result,"car_data_1",p->info.car);
    text(&p->result,"time_data_1",info->failed?"DNF":p->time);
    for(int row=2;row<=6;row++) {
        const char *prefix[]={"player_data_","ranking_data_","car_data_","time_data_"};
        for(int k=0;k<4;k++){char name[40];snprintf(name,sizeof name,"%s%d",prefix[k],row);
            int i=ug_scene_find_name(&p->result.scene,ug_hash_upper(name));if(i>=0)p->result.scene.nodes[i].app_hidden=1;}
    }
    if(info->have_standings && !info->have_url_standings && info->kind!=N2_RACE_DRIFT) {
        /* M7: the whole field in place order: finishers by time, then the cars still racing, then those out. */
        int count=info->standing_count;
        if(count<1 || count>RR_MAX_CARS) {
            snprintf(p->result.status,sizeof p->result.status,"postrace: invalid standings");goto fail;
        }
        int order[RR_MAX_CARS];
        for(int k=0;k<count;k++) {
            p->info.standing[k].name[31]=p->info.standing[k].car[31]=0;order[k]=k;
            if(p->info.standing[k].place<1 || p->info.standing[k].place>count) {
                snprintf(p->result.status,sizeof p->result.status,"postrace: invalid place");goto fail;
            }
        }
        for(int k=1;k<count;k++) {
            int value=order[k],j=k;
            while(j>0 && p->info.standing[order[j-1]].place>p->info.standing[value].place){order[j]=order[j-1];j--;}
            order[j]=value;
        }
        const char *prefix[]={"player_data_","ranking_data_","car_data_","time_data_"};
        for(int row=0;row<count;row++) {
            const PostRaceStanding *entry=&p->info.standing[order[row]];
            double s=isfinite(entry->seconds) && entry->seconds>0?fmin(entry->seconds,359999):0;
            unsigned c=(unsigned)(s*100);
            if(entry->status==PR_STAND_OUT){snprintf(p->row_places[row],sizeof p->row_places[row],"DNF");
                snprintf(p->row_times[row],sizeof p->row_times[row],"DNF");}
            else {snprintf(p->row_places[row],sizeof p->row_places[row],"%d",entry->place);
                if(entry->status==PR_STAND_FINISHED && c)snprintf(p->row_times[row],sizeof p->row_times[row],"%u:%02u.%02u",c/6000,c/100%60,c%100);
                else p->row_times[row][0]=0;}   /* still racing: no time yet */
            const char *value[]={entry->name,p->row_places[row],entry->car,p->row_times[row]};
            for(int k=0;k<4;k++) {
                char name[40];snprintf(name,sizeof name,"%s%d",prefix[k],row+1);
                int node=ug_scene_find_name(&p->result.scene,ug_hash_upper(name));
                if(node>=0){p->result.scene.nodes[node].app_hidden=!value[k][0];
                    if(k==1)rank_label(&p->result,name,value[k]);else p->result.scene.nodes[node].text_override=value[k];}
            }
        }
    }
    for(int i=0;i<p->result.scene.n;i++) {
        UgNode *n=&p->result.scene.nodes[i];
        if(n->obj->name_hash==ug_hash_upper("OL_COUNTDOWN_TIMER") || n->obj->guid==0xe73f5)n->app_hidden=1;
    }
    p->result.scene.hide_hint_text=0;
    for(int i=0;i<p->result.pkg.nobj;i++) {
        UgFngObject *o=&p->result.pkg.obj[i];
        if(o->guid==0xe5c74 || o->guid==0xe5c0b || o->guid==0xdeb62)o->flags&=~(UG_OF_CONSOLE|4u);
    }
    p->visual=1;p->status[0]=0;
    if(p->phase==PR_SCREEN_TOURN_RESULTS)family_open(p);
    return;
fail:snprintf(p->status,sizeof p->status,"%s",p->result.status);
}
static int hit(float x,float y) {
    if(y>=440 && y<=472 && x>=24 && x<=170)return PR_ACT_QUIT_MAIN;
    /* [data] PC restart label at (-256,170), next label at (147,171). */
    if(y>=385 && y<=435 && x>=24 && x<=190)return PR_ACT_RESTART;
    if(y>=385 && y<=435 && x>=400 && x<=620)return PR_ACT_CONTINUE;
    return 0;
}
static int pointer_hit(const PostRace *p,float x,float y) {
    int action=hit(x,y);
    if(p->phase!=PR_SCREEN_MENU && p->phase!=PR_SCREEN_RESULTS) {
        action=0;
        if(y>=385 && y<=472)action=x<220?(p->flow_at>0?-1:PR_ACT_QUIT_MAIN):x>=400?PR_ACT_CONTINUE:0;
    }
    if(p->phase==PR_SCREEN_MENU && y<400)action=x<220?10:x>420?11:12;
    return action;
}
void pr_event(PostRace *p,const SDL_Event *e,SDL_Window *win) {
    if(!p || !p->open || p->pending)return;
    if(e->type==SDL_KEYDOWN && !e->key.repeat) {
        if(e->key.keysym.sym==SDLK_RETURN)p->pending=p->phase==PR_SCREEN_MENU?(p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN):PR_ACT_CONTINUE;
        if(p->phase==PR_SCREEN_MENU && e->key.keysym.sym==SDLK_LEFT)gc_step(&p->carousel,-1);
        if(p->phase==PR_SCREEN_MENU && e->key.keysym.sym==SDLK_RIGHT)gc_step(&p->carousel,1);
        if(e->key.keysym.sym==SDLK_r)p->pending=PR_ACT_RESTART;
        if(e->key.keysym.sym==SDLK_ESCAPE)p->pending=(p->phase!=PR_SCREEN_MENU && p->flow_at>0)?-1:PR_ACT_QUIT_MAIN;
    } else if(e->type==SDL_CONTROLLERBUTTONDOWN) {
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_A)p->pending=p->phase==PR_SCREEN_MENU?(p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN):PR_ACT_CONTINUE;
        if(p->phase==PR_SCREEN_MENU && e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)gc_step(&p->carousel,-1);
        if(p->phase==PR_SCREEN_MENU && e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)gc_step(&p->carousel,1);
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_X)p->pending=PR_ACT_RESTART;
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_B)p->pending=(p->phase!=PR_SCREEN_MENU && p->flow_at>0)?-1:PR_ACT_QUIT_MAIN;
    } else if(win && (e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) && e->button.button==SDL_BUTTON_LEFT && e->button.which!=SDL_TOUCH_MOUSEID) {
        int w,h;float x,y;SDL_GetWindowSize(win,&w,&h);if(w<=0 || h<=0)return;
        pause_menu_to_virtual(pause_menu_transform(w,h),e->button.x,e->button.y,&x,&y);
        int action=pointer_hit(p,x,y);
        if(e->type==SDL_MOUSEBUTTONDOWN)p->pressed=action;
        else {
            if(p->pressed==action) {
                if(action==10 || action==11)gc_step(&p->carousel,action==10?-1:1);
                else if(action==12)p->pending=p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN;
                else p->pending=action;
            }
            p->pressed=0;
        }
    } else if(e->type==SDL_FINGERDOWN || e->type==SDL_FINGERUP) {
        float x=e->tfinger.x*640,y=e->tfinger.y*480;
        if(win){int w,h;SDL_GetWindowSize(win,&w,&h);if(w<=0 || h<=0)return;
            pause_menu_to_virtual(pause_menu_transform(w,h),e->tfinger.x*w,e->tfinger.y*h,&x,&y);}
        int action=pointer_hit(p,x,y);
        if(e->type==SDL_FINGERDOWN)p->pressed=action;
        else {if(action && action==p->pressed) {
            if(action==10 || action==11)gc_step(&p->carousel,action==10?-1:1);
            else p->pending=action==12?(p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN):action;
        }p->pressed=0;}
    } else if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)p->pressed=0;
}
int pr_update(PostRace *p,float seconds) {
    if(!p || !p->open)return 0;
    int action=p->pending;p->pending=0;
    if(action==-1) {p->phase=p->flow[--p->flow_at];family_open(p);return 0;}
    if(action==PR_ACT_CONTINUE && p->phase!=PR_SCREEN_MENU) {
        if(p->flow_at+1<p->flow_count) p->phase=p->flow[++p->flow_at];
        if(p->phase!=PR_SCREEN_MENU && p->phase!=PR_SCREEN_RESULTS) {family_open(p);return 0;}
        gv_screen_free(&p->menu);
        gc_init(&p->carousel,GC_RAIL_PAUSE);gc_set_entries(&p->carousel,3,NULL);
        int valid=gv_screen_load(&p->menu,&p->assets,"UI_postrace.fng");
        if(valid)for(int i=0;i<fe_binding_sets[4].count;i++)
            if(fe_identity_find(p->menu.pk,&fe_binding_sets[4].identity[i])<0) {
                valid=0;snprintf(p->menu.status,sizeof p->menu.status,"postrace menu identity mismatch");break;
            }
        if(!valid || !gv_carousel_bind(&p->view,&p->menu,&p->carousel)) {
            snprintf(p->status,sizeof p->status,"%s",p->menu.status);p->visual=0;return 0;
        }
        const uint32_t icons[]={0xb295a6b6,0x62aa0df6,0x4c9e34e6};
        const uint32_t labels[]={0x80833fc2,0xde02cf84,0xc7e1420c};
        const char *fallback[]={"Restart","Continue","Quit to Main Menu"};
        for(int i=0;i<3;i++){p->entries[i].tex=gv_texture(&p->assets,icons[i]);p->entries[i].label=gv_text(&p->assets,labels[i],fallback[i]);}
        action=0;
    }
    if(action){p->open=0;return action;}
    if(p->phase==PR_SCREEN_MENU && p->visual) {
        gc_update(&p->carousel,seconds);
        gv_screen_update(&p->menu,seconds>0?seconds*1000:0);
        gv_carousel_apply(&p->view,&p->carousel,p->entries,3,NULL,0,0);
        return 0;
    }
    if(p->visual && p->phase!=PR_SCREEN_RESULTS) {
        gv_screen_update(&p->family,seconds>0?seconds*1000:0);
        uint32_t next=p->phase==PR_SCREEN_STATS?0x1c7fcf8bu:p->phase==PR_SCREEN_WORLD_MAP?0x74806286u:0x0019de3eu;
        int at=ug_scene_find_name(&p->family.scene,next);
        if(at>=0){float alpha=255;p->family.scene.nodes[at].app_hidden=0;
            ug_scene_app_write(&p->family.scene,at,UG_P_COLOR+3,1,&alpha);}
        ug_scene_relayout(&p->family.scene);
        if(p->phase==PR_SCREEN_WORLD_MAP) {
            gv_screen_update(&p->mapbase,seconds>0?seconds*1000:0);fwm_sync(&p->map,&p->mapbase);
        }
        return 0;
    }
    if(p->visual) {
        gv_screen_update(&p->result,seconds>0?seconds*1000:0);
        /* [inferred] Expose the authored PC action captions, without joystick tokens. */
        for(int i=0;i<p->result.scene.n;i++)if(p->result.scene.nodes[i].obj->guid==0xe5c74) {
            p->result.scene.nodes[i].app_hidden=0;
            float alpha=255;ug_scene_app_write(&p->result.scene,i,UG_P_COLOR+3,1,&alpha);
        }
        const char *names[]={"restart","next"};
        for(int k=0;k<2;k++) {
            int i=ug_scene_find_name(&p->result.scene,ug_hash_upper(names[k]));
            if(i>=0){UgNode *n=&p->result.scene.nodes[i];n->app_hidden=0;
                float alpha=255;ug_scene_app_write(&p->result.scene,i,UG_P_COLOR+3,1,&alpha);
                ug_scene_app_set_world_pos(&p->result.scene,i,k?147:-256,180);}
        }
        ug_scene_relayout(&p->result.scene);
    }
    return 0;
}
void pr_draw(PostRace *p,int w,int h) {
    if(!p || !p->open || !p->visual || w<=0 || h<=0)return;
    const UgFace *old=ug_text_face();const UgSkin *skin=ug_kit_skin();UgSkin saved;if(skin)saved=*skin;
    ug_text_set_face(ug_assets_main_face(&p->assets));ug_kit_set_skin(&p->skin);ug_cv_clear(&p->canvas);
    GvScreen *screen=p->phase==PR_SCREEN_MENU?&p->menu:p->phase==PR_SCREEN_RESULTS?&p->result:&p->family;
    if(!gv_screen_draw(screen,&p->ui,&p->canvas)){p->visual=0;snprintf(p->status,sizeof p->status,"%s",screen->status);}
    if(p->visual && p->phase==PR_SCREEN_WORLD_MAP) {
        if(!gv_screen_draw(&p->mapbase,&p->ui,&p->canvas) || !fwm_draw(&p->map,&p->mapbase,&p->canvas)) {
            p->visual=0;snprintf(p->status,sizeof p->status,"postrace: map draw failed");
        }
    }
    if(p->visual) {
        gv_touch_back(&p->canvas,&p->assets);
        ug_text_set_face(ug_assets_main_face(&p->assets));
        ug_gl_render_overlay(p->gl,&p->canvas,w,h);
    }
    ug_text_set_face(old);ug_kit_set_skin(skin?&saved:NULL);
}
int pr_is_open(const PostRace *p){return p && p->open;}
const char *pr_status(const PostRace *p){return p?p->status:"postrace allocation unavailable";}
void pr_destroy(PostRace *p){if(!p)return;gv_screen_free(&p->result);gv_screen_free(&p->menu);gv_screen_free(&p->family);gv_screen_free(&p->mapbase);fwm_destroy(&p->map);ug_gl_destroy(p->gl);ug_cv_free(&p->canvas);ug_assets_close(&p->assets);free(p);}
