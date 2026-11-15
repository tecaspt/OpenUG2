/* [inferred] Results actions follow the M1 contract, not executable-derived behavior. */
#include "postrace.h"
#include "race_rules.h"
#include "race.h"
#include "gui_view.h"
#include "fe_binding.h"
#include "pause_menu_layout.h"
#include "ugui/ugui_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* [inferred] Keep optional reward text inside the authored results frame. */
#define PR_REWARD_TITLE_WIDTH 580.0f
#define PR_DNF_RANK_SHIFT 18.0f
struct PostRace {
    UgAssets assets;UgSkin skin;UgGL *gl;UgCanvas canvas;UgCtx ui;
    GvScreen result,menu;GvCarousel view;GuiCarousel carousel;GvEntry entries[3];PostRaceInfo info;
    int open,visual,pending,pressed,phase;
    char status[256],place[16],time[32],title[384];
    char row_points[RR_MAX_CARS][32],row_places[RR_MAX_CARS][16];
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
    p->phase=0;p->open=1;p->pending=0;p->pressed=0;p->visual=0;
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
    text(&p->result,"player_data_1",gv_text(&p->assets,ug_hash_upper("PLAYER"),"Player"));
    rank_label(&p->result,"ranking_data_1",info->failed?"DNF":p->place);
    text(&p->result,"car_data_1",p->info.car);
    text(&p->result,"time_data_1",info->failed?"DNF":p->time);
    for(int row=2;row<=6;row++) {
        const char *prefix[]={"player_data_","ranking_data_","car_data_","time_data_"};
        for(int k=0;k<4;k++){char name[40];snprintf(name,sizeof name,"%s%d",prefix[k],row);
            int i=ug_scene_find_name(&p->result.scene,ug_hash_upper(name));if(i>=0)p->result.scene.nodes[i].app_hidden=1;}
    }
    if(info->kind==N2_RACE_URL && info->have_url_standings) {
        int count=info->standing_count;
        if(count<1 || count>RR_MAX_CARS || info->url_round<1 || info->url_round>info->url_rounds ||
           info->url_rounds>RR_MAX_URL_RACES) {
            snprintf(p->result.status,sizeof p->result.status,"postrace: invalid URL standings");goto fail;
        }
        const char *points=gv_text(&p->assets,0x70802830u,NULL),*url=gv_text(&p->assets,0x75f903f4u,NULL);
        if(!points || !*points || !url || !*url) {
            snprintf(p->result.status,sizeof p->result.status,"postrace: URL captions unavailable");goto fail;
        }
        int order[RR_MAX_CARS];
        for(int k=0;k<count;k++) {
            p->info.standing[k].name[31]=p->info.standing[k].car[31]=0;order[k]=k;
            if(p->info.standing[k].place<0 || p->info.standing[k].place>count) {
                snprintf(p->result.status,sizeof p->result.status,"postrace: invalid URL place");goto fail;
            }
        }
        for(int k=1;k<count;k++) {
            int value=order[k],j=k;
            while(j>0 && p->info.standing[order[j-1]].points<p->info.standing[value].points) {
                order[j]=order[j-1];j--;
            }
            order[j]=value;
        }
        snprintf(p->title,sizeof p->title,"%s %d/%d",url,info->url_round,info->url_rounds);
        text(&p->result,"Header_Text",p->title);text(&p->result,"Header_Text_3",points);
        const char *prefix[]={"player_data_","ranking_data_","car_data_","time_data_"};
        for(int row=0;row<count;row++) {
            const PostRaceStanding *entry=&p->info.standing[order[row]];
            snprintf(p->row_points[row],sizeof p->row_points[row],"%u",entry->points);
            if(entry->place)snprintf(p->row_places[row],sizeof p->row_places[row],"%d",entry->place);
            else snprintf(p->row_places[row],sizeof p->row_places[row],"DNF");
            const char *value[]={entry->name,p->row_places[row],entry->car,p->row_points[row]};
            for(int k=0;k<4;k++) {
                char name[40];snprintf(name,sizeof name,"%s%d",prefix[k],row+1);
                int node=ug_scene_find_name(&p->result.scene,ug_hash_upper(name));
                if(node>=0){p->result.scene.nodes[node].app_hidden=!value[k][0];
                    if(k==1)rank_label(&p->result,name,value[k]);else p->result.scene.nodes[node].text_override=value[k];}
            }
        }
    }
    if(info->reward.available) {
        const char *cash=gv_text(&p->assets,0x75d23e29u,NULL);
        const char *rep=gv_text(&p->assets,0x40152751u,NULL);
        if(!cash || !*cash || !rep || !*rep) {
            snprintf(p->result.status,sizeof p->result.status,"postrace: reward captions unavailable");goto fail;
        }
        char prefix[192];snprintf(prefix,sizeof prefix,"%.180s",p->title);
        snprintf(p->title,sizeof p->title,"%s%s%s %u   %s %u",prefix,*prefix?" | ":"",cash,
            info->failed?0:info->reward.cash,rep,info->failed?0:info->reward.reputation);
        text(&p->result,"Header_Text",p->title);
        int node=ug_scene_find_name(&p->result.scene,ug_hash_upper("Header_Text"));
        if(node>=0) {
            UgNode *n=&p->result.scene.nodes[node];const UgFace *saved=ug_text_face();
            float width=0,scale=fabsf(n->props.w[UG_P_SIZE]);
            ug_text_set_face(n->face);ug_text_measure(p->title,scale,0,(float)n->obj->leading,p->ui.glyphs,&width,NULL);
            ug_text_set_face(saved);
            if(width>PR_REWARD_TITLE_WIDTH) {
                float size[2]={scale*PR_REWARD_TITLE_WIDTH/width,scale*PR_REWARD_TITLE_WIDTH/width};
                ug_scene_app_write(&p->result.scene,node,UG_P_SIZE,2,size);
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
    p->visual=1;p->status[0]=0;return;
fail:snprintf(p->status,sizeof p->status,"%s",p->result.status);
}
static int hit(float x,float y) {
    if(y>=440 && y<=472 && x>=24 && x<=170)return PR_ACT_QUIT_MAIN;
    /* [data] PC restart label at (-256,170), next label at (147,171). */
    if(y>=385 && y<=435 && x>=24 && x<=190)return PR_ACT_RESTART;
    if(y>=385 && y<=435 && x>=400 && x<=620)return PR_ACT_CONTINUE;
    return 0;
}
void pr_event(PostRace *p,const SDL_Event *e,SDL_Window *win) {
    if(!p || !p->open || p->pending)return;
    if(e->type==SDL_KEYDOWN && !e->key.repeat) {
        if(e->key.keysym.sym==SDLK_RETURN)p->pending=p->phase?(p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN):PR_ACT_CONTINUE;
        if(p->phase && e->key.keysym.sym==SDLK_LEFT)gc_step(&p->carousel,-1);
        if(p->phase && e->key.keysym.sym==SDLK_RIGHT)gc_step(&p->carousel,1);
        if(e->key.keysym.sym==SDLK_r)p->pending=PR_ACT_RESTART;
        if(e->key.keysym.sym==SDLK_ESCAPE)p->pending=PR_ACT_QUIT_MAIN;
    } else if(e->type==SDL_CONTROLLERBUTTONDOWN) {
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_A)p->pending=p->phase?(p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN):PR_ACT_CONTINUE;
        if(p->phase && e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)gc_step(&p->carousel,-1);
        if(p->phase && e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)gc_step(&p->carousel,1);
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_X)p->pending=PR_ACT_RESTART;
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_B)p->pending=PR_ACT_QUIT_MAIN;
    } else if(win && (e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) && e->button.button==SDL_BUTTON_LEFT) {
        int w,h;float x,y;SDL_GetWindowSize(win,&w,&h);if(w<=0 || h<=0)return;
        pause_menu_to_virtual(pause_menu_transform(w,h),e->button.x,e->button.y,&x,&y);
        int action=hit(x,y);
        if(p->phase && y<400)action=x<220?10:x>420?11:12;
        if(e->type==SDL_MOUSEBUTTONDOWN)p->pressed=action;
        else {
            if(p->pressed==action) {
                if(action==10 || action==11)gc_step(&p->carousel,action==10?-1:1);
                else if(action==12)p->pending=p->carousel.cur==0?PR_ACT_RESTART:p->carousel.cur==1?PR_ACT_CONTINUE:PR_ACT_QUIT_MAIN;
                else p->pending=action;
            }
            p->pressed=0;
        }
    } else if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)p->pressed=0;
}
int pr_update(PostRace *p,float seconds) {
    if(!p || !p->open)return 0;
    int action=p->pending;p->pending=0;
    if(action==PR_ACT_CONTINUE && !p->phase) {
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
        p->phase=1;action=0;
    }
    if(action){p->open=0;return action;}
    if(p->phase && p->visual) {
        gc_update(&p->carousel,seconds);
        gv_screen_update(&p->menu,seconds>0?seconds*1000:0);
        gv_carousel_apply(&p->view,&p->carousel,p->entries,3,NULL,0,0);
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
    gv_touch_back(&p->canvas,&p->assets);
    if(!gv_screen_draw(p->phase?&p->menu:&p->result,&p->ui,&p->canvas)){p->visual=0;snprintf(p->status,sizeof p->status,"%s",p->result.status);}
    if(p->visual) {
        ug_text_set_face(ug_assets_main_face(&p->assets));
        ug_gl_render_overlay(p->gl,&p->canvas,w,h);
    }
    ug_text_set_face(old);ug_kit_set_skin(skin?&saved:NULL);
}
int pr_is_open(const PostRace *p){return p && p->open;}
const char *pr_status(const PostRace *p){return p?p->status:"postrace allocation unavailable";}
void pr_destroy(PostRace *p){if(!p)return;gv_screen_free(&p->result);gv_screen_free(&p->menu);ug_gl_destroy(p->gl);ug_cv_free(&p->canvas);ug_assets_close(&p->assets);free(p);}
