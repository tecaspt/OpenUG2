/* fe_menu.c -- see fe_menu.h. Structure follows pause_menu.c (assets, UgGL, canvas, GvScreen +
 * GvCarousel, pointer events) and pause_screen.c (exit timing: the choice fires after the exit). */
#include "fe_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "fe_entries.h"
#include "fe_screen.h"
#include "fe_garage.h"
#include "game_catalog.h"
#include "gui_timing.h"
#include "gui_view.h"
#include "pause_menu_layout.h"      /* window <-> 640x480 virtual transform */
#include "rhi.h"
#include "ugui/ugui_assets.h"
#include "ugui/ugui_core.h"
#include "ugui/ugui_gl.h"
#include "ugui/ugui_scene.h"
#include "ugui/ugui_ui.h"

#define FE_EXIT_TIMEOUT_MS 700.0f     /* safety: a package whose exit script never reports completion */
#define FE_M1_MAIN_ENABLED ((1u << FE_MAIN_CUSTOMIZE)|(1u << FE_MAIN_QUICK_RACE)|(1u << FE_MAIN_OPTIONS)|(1u << FE_MAIN_PROFILE))   /* Options joins in M2 */

typedef struct FeHook { const char *name; } FeHook;
static const FeHook k_hooks[] = {
 [FEM_SCR_QR_TRACK]={"UI_QRTrackSelect.fng"},
 [FEM_SCR_QR_OPTIONS]={"UI_QRTrackOptions.fng"},
 [FEM_SCR_QR_CAR]={"UI_QRCarSelect.fng"}
};

struct FeMenu {
    char dataroot[512];
    const struct GameCatalog *cat;
    int allow_quit;
    unsigned main_enabled;   /* FE_MAIN_* bits; Career joins when the host says career data loaded (M6) */
    int career_save;         /* the current profile has a saved career: Career continues it */
    UgAssets assets;
    UgSkin skin;
    UgGL *gl;
    UgCanvas canvas;
    UgCtx ui;
    int assets_open, compatible;
    float ui_ms;
    FemScreen screen;
    int open;
    GvScreen scr;
    GvCarousel car_v;
    FeCarouselScreen fes;
    int has_carousel;            /* the current screen is a carousel */
    int visual;                  /* the current screen was built and can be drawn */
    int exit_sent;               /* the authored exit script was started */
    float exit_wait_ms;
    int back_pressed;
    int locked;                  /* a request was issued: input is ignored until the host reacts */
    unsigned icon_warned;
    int career_focus, career_confirm;   /* M6 career start screen */
    int main_focus, mode_focus;  /* last focused entry ids, restored when a screen is re-entered */
    char cur_car[32], cur_track[16];
    FeChoice choice;
    FeGarage *garage;
    int (*career_parts_save)(void *,const char *,const GarageConfig *);
    void *career_save_user;
    int career_garage;           /* M6: Garage opened from career roam; leaving it requests FEM_REQ_CAREER_ROAM */
    FeWorldMap world_map;
    int map_pressed;
    FeLot lot;                   /* M6 Car Lot state (fe_lot.h) */
    FePerf perf;                 /* career Performance Shop state (fe_perf.h) */
    int pending_input, pressed;
    int loading_event;
    OptionsMenu options;
    GvList opt_list;
    GvDialog opt_dialog;
    OmScreen opt_shown;
    StGraphics graphics;
    StGfxCaps caps;
    StRegStore store;
    int store_open;
    char config_path[1024]; /* empty: platform default store */
    FeProfile *profile;
    unsigned apply_flags;
    int editing,profile_action,key_focus,key_nodes[96],nkeys,caps_lock,delete_confirm;
    char edit_name[ST_NAME_MAX+1],profile_title[192];
    char status[256];
    char res_status[256];        /* why the resources are unavailable (set at creation) */
};

static const char *screen_name(FemScreen s) {
    switch (s) {
    case FEM_SCR_WORLD_MAP: return "WORLD_MAP";
    case FEM_SCR_MAIN: return "MAIN";
    case FEM_SCR_QR_MODE: return "QR_MODE";
    case FEM_SCR_QR_TRACK: return "QR_TRACK";
    case FEM_SCR_QR_OPTIONS: return "QR_OPTIONS";
    case FEM_SCR_QR_CAR: return "QR_CAR";
    case FEM_SCR_OPTIONS:return "OPTIONS";
    case FEM_SCR_PROFILE:return "PROFILE";
    case FEM_SCR_CAREER: return "CAREER";
    case FEM_SCR_GARAGE:return "GARAGE";
    case FEM_SCR_CAREER_LOT: return "CAREER_LOT";
    case FEM_SCR_CAREER_PERF: return "CAREER_PERF";
    case FEM_SCR_LOADING: return "LOADING";
    default: return "NONE";
    }
}
static const char *package_of(FemScreen s) {
    switch (s) {
    case FEM_SCR_WORLD_MAP:return "UI_CareerWorldMap.fng";
    case FEM_SCR_GARAGE:return "UI_ChooseCustomizeCategory.fng";
    case FEM_SCR_CAREER_LOT: return "UI_CareerCarLot.fng";
    case FEM_SCR_CAREER_PERF: return "UI_ChoosePerformanceCategory.fng";   /* the current page: fpr_package */
    case FEM_SCR_OPTIONS:return "UI_OptionsMain.fng";
    case FEM_SCR_PROFILE:return "UI_ProfileManager.fng";
    case FEM_SCR_MAIN: return "UI_Main.fng";
    case FEM_SCR_QR_MODE: return "UI_QRModeSelect.fng";
    case FEM_SCR_CAREER: return "UI_StartCareer.fng";   /* [data] same carousel layout as UI_QRModeSelect */
    case FEM_SCR_LOADING: return "PC_Loading.fng";
    default: return s>=FEM_SCR_QR_TRACK && s<=FEM_SCR_QR_CAR ? k_hooks[s].name : NULL;
    }
}

/* ---- views ------------------------------------------------------------------------------------ */

static void drop_view(FeMenu *m) {
    gv_dialog_close(&m->opt_dialog);
    if (m->scr.loaded) gv_screen_free(&m->scr);
    m->visual = 0;
    m->back_pressed = 0;
    m->map_pressed = -1;
    m->has_carousel = 0;
    m->exit_sent = 0;
    m->exit_wait_ms = 0;
}

static void set_status(FeMenu *m, const char *fmt, const char *a) {
    snprintf(m->status, sizeof m->status, fmt, a);
}

/* Width of the package's ScrollBar_Backing (the rail), or `dflt` when absent. */
static float rail_width(const GvScreen *s, float dflt) {
    int i = ug_scene_find_name(&s->scene, ug_hash_upper("ScrollBar_Backing"));
    return i >= 0 ? s->scene.nodes[i].obj->size[0] : dflt;
}

static void profile_entries(FeMenu *m,FeEntry out[GC_MAX],int *count) {
    const char *labels[]={"Create Profile","Rename Profile","Load Profile","Delete Profile"};
    const uint32_t keys[]={0xf54cc524,0,0x6a9b74bd,0x66516470};
    for(int i=0;i<4;i++)out[i]=(FeEntry){i,0x6b303856,keys[i],"MAIN_ICON_PROFILE_MANAGER",labels[i],0};
    (void)m;*count=4;
}
static int build_options(FeMenu *m) {
    const char *pkg=om_package_name(OM_CTX_FRONTEND,om_screen(&m->options));
    if(!pkg || !gv_screen_load(&m->scr,&m->assets,pkg))return 0;
    m->opt_shown=om_screen(&m->options);
    if(m->opt_shown==OM_SCREEN_HUB) {
        m->has_carousel=0;
        return gv_carousel_bind(&m->car_v,&m->scr,&m->options.hub);
    }
    return gv_list_bind(&m->opt_list,&m->scr,&m->options);
}
static void build_screen(FeMenu *m) {
    drop_view(m);
    m->status[0] = 0;
    m->icon_warned = 0;
    m->locked = 0;
    if((m->screen==FEM_SCR_OPTIONS || m->screen==FEM_SCR_PROFILE) && !m->profile){set_status(m,"%s","profile storage unavailable");return;}
    const char *pkg = package_of(m->screen);
    if (!pkg) {set_status(m,"%s: unknown screen",screen_name(m->screen));return;}
    if (!m->assets_open || !m->compatible || !m->gl) {
        snprintf(m->status, sizeof m->status, "%s: front-end resources unavailable (%.180s)", screen_name(m->screen), m->res_status);
        return;
    }
    if(m->screen==FEM_SCR_WORLD_MAP) {
        m->visual=fwm_bind(&m->world_map,&m->scr,&m->assets);
        if(!m->visual)snprintf(m->status,sizeof m->status,"%s",
            m->world_map.status[0]?m->world_map.status:m->scr.status);
        return;
    }
    if(m->screen==FEM_SCR_GARAGE) {
        m->visual=feg_open(m->garage,&m->scr,&m->assets);
        if(!m->visual)snprintf(m->status,sizeof m->status,"%s",m->scr.status);
        return;
    }
    if(m->screen==FEM_SCR_CAREER_LOT) {
        m->visual=fel_bind(&m->lot,&m->scr,&m->assets);
        if(!m->visual)snprintf(m->status,sizeof m->status,"%s",m->scr.status);
        return;
    }
    if(m->screen==FEM_SCR_CAREER_PERF) {
        m->visual=fpr_bind(&m->perf,&m->scr,&m->assets);
        if(!m->visual)snprintf(m->status,sizeof m->status,"%s",m->scr.status);
        return;
    }
    if(m->screen==FEM_SCR_OPTIONS) {
        m->visual=build_options(m);
        if(!m->visual)snprintf(m->status,sizeof m->status,"%s",m->scr.status);
        return;
    }
    if(m->screen==FEM_SCR_PROFILE && m->editing) {
        if(!gv_screen_load(&m->scr,&m->assets,"UI_VirtualKeyboard.fng")){set_status(m,"%s",m->scr.status);return;}
        m->nkeys=0;m->key_focus=0;
        for(int i=0;i<m->scr.scene.n;i++) {
            UgNode *n=&m->scr.scene.nodes[i];const UgFngObject *o=n->obj;
            if(o->type==UG_OBJ_TEXT && o->parent_guid==0x1130e && o->text && m->nkeys<96) {
                n->text_override=o->text;m->key_nodes[m->nkeys++]=i;
            }
        }
        int header=0,entry=0,done=0,back=0;
        for(int i=0;i<m->scr.scene.n;i++) {
            const UgFngObject *o=m->scr.scene.nodes[i].obj;
            header|=o->type==UG_OBJ_TEXT && o->guid==0xdec3d && o->name_hash==0x42adb44c;
            entry|=o->type==UG_OBJ_TEXT && o->guid==0x10b1f && o->name_hash==0x5dfa6642;
        }
        for(int k=0;k<m->nkeys;k++) {
            const char *label=m->scr.scene.nodes[m->key_nodes[k]].obj->text;
            done|=!strcmp(label,"DONE");back|=!strcmp(label,"BACK");
        }
        m->visual=m->nkeys>0 && header && entry && done && back;
        if(!m->visual)set_status(m,"%s","profile keyboard: required retail fields/keys unavailable");
        return;
    }
    if(m->screen>=FEM_SCR_QR_TRACK && m->screen<=FEM_SCR_QR_CAR) {
        int ok=m->screen==FEM_SCR_QR_CAR ? fec_open(&m->choice,&m->scr,&m->assets) :
               fet_open(&m->choice,&m->scr,&m->assets,m->screen==FEM_SCR_QR_OPTIONS);
        m->visual=ok;
        if(!ok)snprintf(m->status,sizeof m->status,"%s",m->scr.status);
        return;
    }
    if (!gv_screen_load(&m->scr, &m->assets, pkg)) { set_status(m, "%s", m->scr.status); return; }
    int ok = 1;
    if (m->screen != FEM_SCR_LOADING) {
        FeEntry e[GC_MAX];
        int n;
        if(m->screen==FEM_SCR_PROFILE)profile_entries(m,e,&n);
        else if(m->screen==FEM_SCR_CAREER)n=fe_career_entries(e,m->career_save,m->career_confirm);
        else n = m->screen == FEM_SCR_MAIN ? fe_main_entries(e, m->main_enabled) : fe_mode_entries(e);
        fes_open(&m->fes, e, n, m->screen == FEM_SCR_MAIN ? m->main_focus : m->screen == FEM_SCR_CAREER ? m->career_focus : m->mode_focus,
                 rail_width(&m->scr, GC_RAIL_PAUSE));
        ok = gv_carousel_bind(&m->car_v, &m->scr, &m->fes.car);
        m->has_carousel = ok;
        if (ok) {                                            /* geometry of this package: rail and arrow boxes */
            const UgScene *sc = &m->scr.scene;
            const GvCarousel *v = &m->car_v;
            m->fes.car.rail_cx = sc->nodes[v->rail].obj->pos[0];
            m->fes.car.rail_cy = sc->nodes[v->rail].obj->pos[1];
            fes_set_arrows(&m->fes, sc->nodes[v->arrow_l].obj->pos[0], sc->nodes[v->arrow_r].obj->pos[0], sc->nodes[v->arrow_l].obj->pos[1]);
        }
    }
    if (!ok) { set_status(m, "%s", m->scr.status); gv_screen_free(&m->scr); return; }
    m->visual = 1;
}

/* Logic-only carousel when the package cannot be bound: state and input still work. */
static void ensure_logic(FeMenu *m) {
    if (m->has_carousel || (m->screen != FEM_SCR_MAIN && m->screen != FEM_SCR_QR_MODE && m->screen != FEM_SCR_CAREER)) return;
    FeEntry e[GC_MAX];
    int n = m->screen == FEM_SCR_MAIN ? fe_main_entries(e, m->main_enabled) :
            m->screen == FEM_SCR_CAREER ? fe_career_entries(e, m->career_save, m->career_confirm) : fe_mode_entries(e);
    fes_open(&m->fes, e, n, m->screen == FEM_SCR_MAIN ? m->main_focus : m->screen == FEM_SCR_CAREER ? m->career_focus : m->mode_focus,
             GC_RAIL_PAUSE);
}

static int is_logic_screen(const FeMenu *m) { return m->screen==FEM_SCR_MAIN || m->screen==FEM_SCR_QR_MODE || m->screen==FEM_SCR_CAREER || (m->screen==FEM_SCR_PROFILE && !m->editing); }
static void menu_input(FeMenu *m,int in) {
    if(is_logic_screen(m))fes_input(&m->fes,in);
    else if(m->screen==FEM_SCR_GARAGE || m->screen==FEM_SCR_CAREER_LOT || m->screen==FEM_SCR_CAREER_PERF || m->screen==FEM_SCR_WORLD_MAP)m->pending_input=in;
    else if(m->screen>=FEM_SCR_QR_TRACK && m->screen<=FEM_SCR_QR_CAR)m->pending_input=in;
}

static void entries_to_view(FeMenu *m, GvEntry out[GC_MAX]) {
    for (int i = 0; i < m->fes.n; i++) {
        const FeEntry *e = &m->fes.e[i];
        out[i].tex = gv_texture(&m->assets, m->screen==FEM_SCR_MAIN && e->id==FE_MAIN_PROFILE?0x6b303856:e->icon_key);
        out[i].label = gv_text(&m->assets, e->label_key, e->label);
        if (!out[i].tex && !(m->icon_warned & (1u << i))) {          /* omitted, never replaced */
            m->icon_warned |= 1u << i;
            fprintf(stderr, "fem: icon texture 0x%08X (%s) of \"%s\" unavailable; the entry is not drawn\n", e->icon_key, e->icon_name, e->label);
        }
    }
}

static void views_update(FeMenu *m, float ms) {
    if (!m->visual || !m->scr.loaded) return;
    gv_screen_update(&m->scr, ms);
    if(m->screen==FEM_SCR_WORLD_MAP && !fwm_sync(&m->world_map,&m->scr)) {
        m->visual=0;
        snprintf(m->status,sizeof m->status,"%s",m->world_map.status);
        return;
    }
    if(m->screen==FEM_SCR_GARAGE && !feg_sync(m->garage,&m->scr,ms)){m->visual=0;snprintf(m->status,sizeof m->status,"%s",feg_status(m->garage));return;}
    if(m->screen==FEM_SCR_QR_CAR)fec_sync(&m->choice,&m->scr,ms/1000);
    else if(m->screen==FEM_SCR_CAREER_LOT)fel_sync(&m->lot,&m->scr);
    else if(m->screen==FEM_SCR_CAREER_PERF && !fpr_sync(&m->perf,&m->scr,ms)){m->visual=0;snprintf(m->status,sizeof m->status,"%s",m->perf.status);return;}
    else if(m->screen==FEM_SCR_QR_TRACK || m->screen==FEM_SCR_QR_OPTIONS)
        fet_sync(&m->choice,&m->scr,ms,m->screen==FEM_SCR_QR_OPTIONS);
    if(m->screen==FEM_SCR_QR_TRACK && m->status[0])for(int i=0;i<m->scr.scene.n;i++)
        if(m->scr.scene.nodes[i].obj->guid==0xdf142)m->scr.scene.nodes[i].text_override=m->status;
    if(m->screen==FEM_SCR_OPTIONS) {
        if(m->opt_shown==OM_SCREEN_HUB) {
            GvEntry entries[OM_HUB_MAX];
            for(int i=0;i<om_hub_count(&m->options);i++){const OmHubEntry *e=om_hub_entry_of(&m->options,i);entries[i]=(GvEntry){gv_texture(&m->assets,e->icon_key),gv_text(&m->assets,e->label_key,e->label)};}
            gv_carousel_apply(&m->car_v,&m->options.hub,entries,om_hub_count(&m->options),gv_text(&m->assets,0xc7ca52ed,"Options"),m->options.blink_serial,m->options.blink_dir);
        } else gv_list_sync(&m->opt_list,&m->options,ms);
        if(m->options.dialog.open) {
            if(!m->opt_dialog.open)gv_dialog_open(&m->opt_dialog,&m->assets,&m->options.dialog);
            gv_dialog_sync(&m->opt_dialog,&m->options.dialog,ms);
        } else gv_dialog_close(&m->opt_dialog);
    }
    if(m->screen==FEM_SCR_PROFILE && m->profile) {
        const char *name=m->editing?m->edit_name:m->profile->value.name;
        snprintf(m->profile_title,sizeof m->profile_title,"%s: %s",m->editing?(m->delete_confirm?"Confirm Delete":"Enter Name"):"Profile Manager",name);
        if(m->profile->status[0])snprintf(m->profile_title,sizeof m->profile_title,"%s",m->profile->status);
        for(int i=0;i<m->scr.scene.n;i++) {
            UgNode *n=&m->scr.scene.nodes[i];
            if(n->obj->name_hash==0x42adb44c)n->text_override=m->profile_title;
            if(m->editing && n->obj->guid==0x10b1f)n->text_override=m->edit_name;
        }
    }
    if(m->screen==FEM_SCR_PROFILE && m->editing) {
        for(int k=0;k<m->nkeys;k++){float col[4]={255,255,255,255};if(k==m->key_focus){col[0]=202;col[1]=244;col[2]=138;}ug_scene_app_write(&m->scr.scene,m->key_nodes[k],UG_P_COLOR,4,col);}
        ug_scene_relayout(&m->scr.scene);
    }
    if (m->has_carousel) {
        if (m->fes.exiting && !m->exit_sent) { m->exit_sent = 1; gv_screen_exit(&m->scr); }
        GvEntry e[GC_MAX];
        entries_to_view(m, e);
        /* [data] UI_StartCareer's header object resolves to the Options caption; the application titles it */
        const char *hdr = m->screen == FEM_SCR_CAREER ? gv_text(&m->assets, ug_hash_upper("UI_MAIN_CAREER"), "Career") : NULL;
        gv_carousel_apply(&m->car_v, &m->fes.car, e, m->fes.n, hdr, 0, 0);
    }
}

static int save_garage(void *user,const char *car,const GarageConfig *cfg) {
 FeMenu *m=user;
 if(!m||!m->profile||!m->profile->valid)return ST_E_ARG;
 if(m->career_garage&&m->career_parts_save)return m->career_parts_save(m->career_save_user,car,cfg);
 int rc=fem_persist_profile(m);
 return rc==ST_OK?garage_save(m->profile->root,m->profile->value.name,car,cfg):rc;
}

/* ---- creation --------------------------------------------------------------------------------- */

FeMenu *fem_create(const char *dataroot, const struct GameCatalog *cat, int allow_quit) {
    FeMenu *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    snprintf(m->dataroot, sizeof m->dataroot, "%s", dataroot ? dataroot : "");
    m->cat = cat;
    m->garage=feg_create(dataroot);
    feg_set_save(m->garage,save_garage,m);
    m->choice.cat=cat;m->choice.laps=2;m->choice.kind=1;m->choice.rules=rr_options_default();
    m->allow_quit = allow_quit;
    m->main_enabled = FE_M1_MAIN_ENABLED;
    m->main_focus = FE_MAIN_QUICK_RACE;
    m->mode_focus = FE_MODE_CIRCUIT;
    m->loading_event = -1;
    st_gfx_defaults(&m->graphics);st_gfx_caps_full(&m->caps,1);
    m->store_open=st_reg_open_default(&m->store)==ST_OK;
    if(m->store_open)st_gfx_load(&m->store,&m->graphics);
    m->profile=fep_create(NULL,m->store_open?&m->store:NULL);
    if(m->profile && m->profile->valid && m->profile->persisted)m->apply_flags|=FEM_APPLY_PROFILE;
    ug_cv_init(&m->canvas);
    ug_ctx_init(&m->ui);
    m->assets.main_face = -1;
    m->assets_open = ug_assets_open(&m->assets, dataroot, NULL, m->res_status, sizeof m->res_status);
    if (m->assets_open) {
        ug_skin_resolve(&m->skin, &m->assets);
        const UgFace *face = ug_assets_main_face(&m->assets);
        m->compatible = face && face->tex > 0;
        if (!m->compatible) snprintf(m->res_status, sizeof m->res_status, "required genuine PC font unavailable");
        else {
            m->gl = ug_gl_create();
            if (m->gl) ug_gl_set_texlib(m->gl, &m->assets.tex); else snprintf(m->res_status, sizeof m->res_status, "UI renderer unavailable");
        }
    }
    if (!m->compatible || !m->gl)
        fprintf(stderr, "fem: front-end resources unavailable; authored presentation omitted (%s)\n", m->res_status);
    return m;
}

void fem_destroy(FeMenu *m) {
    if (!m) return;
    drop_view(m);
    ug_gl_destroy(m->gl);
    ug_cv_free(&m->canvas);
    ug_assets_close(&m->assets);
    fet_free(&m->choice);
    fwm_destroy(&m->world_map);
    feg_destroy(m->garage);
    fep_destroy(m->profile);if(m->store_open)st_reg_close(&m->store);
    if(m->editing)SDL_StopTextInput();
    free(m);
}

void fem_open(FeMenu *m, FemScreen screen) {
    if(m&&screen!=FEM_SCR_GARAGE&&feg_busy(m->garage))return;
    if (!m) return;
    if (screen == FEM_SCR_NONE) { fem_close(m); return; }
    m->pending_input=0;m->pressed=0;
    if(screen!=FEM_SCR_GARAGE)m->career_garage=0;
    if(m->editing && screen!=FEM_SCR_PROFILE){m->editing=0;SDL_StopTextInput();}
    /* Pause uses a separate store handle; reload its keys before any FE write. */
    if(screen==FEM_SCR_GARAGE&&!m->career_garage)feg_shop(m->garage,-1,~0u);
    int from_roam=screen==FEM_SCR_GARAGE && m->career_garage && !m->open;   /* career Garage: pause may have written */
    if((screen==FEM_SCR_OPTIONS || screen==FEM_SCR_PROFILE || screen==FEM_SCR_GARAGE) && (m->screen==FEM_SCR_MAIN || from_roam)) {
        StRegStore fresh={0};
        int rc=m->config_path[0]?st_reg_open_ini(&fresh,m->config_path):st_reg_open_default(&fresh);
        if(rc==ST_OK) {
            if(m->store_open)st_reg_close(&m->store);
            m->store=fresh;m->store_open=1;
            if(m->profile)m->profile->store=&m->store;
        } else {
            /* Fail closed on writes rather than overwrite keys from a stale snapshot. */
            snprintf(m->status,sizeof m->status,"settings store reload failed (%d)",rc);
            m->screen=screen;m->open=1;drop_view(m);return;
        }
    }
    /* Pause Controls may have updated the existing OPENUG2 container since last visit. */
    if((screen==FEM_SCR_OPTIONS || screen==FEM_SCR_PROFILE || screen==FEM_SCR_GARAGE) && (m->screen==FEM_SCR_MAIN || from_roam) &&
       m->profile && m->profile->persisted)fep_load(m->profile,m->profile->value.name);
    if(screen==FEM_SCR_OPTIONS && (!om_is_open(&m->options) || m->screen!=FEM_SCR_OPTIONS)) {
        OmConfig cfg;om_config_default(&cfg,OM_CTX_FRONTEND);
        cfg.profile=m->profile && m->profile->valid?&m->profile->value:NULL;
        om_open_hub(&m->options,&cfg,&m->graphics,&m->caps,m->store_open?&m->store:NULL);
    }
    m->screen = screen;
    m->open = 1;
    fprintf(stderr, "fem: screen %s\n", screen_name(screen));
    build_screen(m);
    ensure_logic(m);
    if (!m->visual && m->status[0]) fprintf(stderr, "fem: %s; screen not drawn (no native fallback)\n", m->status);
    views_update(m, 0);
}

void fem_close(FeMenu *m) {
    if (!m || !m->open || feg_busy(m->garage)) return;
    if(m->editing){m->editing=0;SDL_StopTextInput();}
    om_close(&m->options);
    if(m->screen==FEM_SCR_GARAGE)feg_input(m->garage,FES_IN_BACK);
    m->open = 0;
    m->screen = FEM_SCR_NONE;
    m->career_garage = 0;
    drop_view(m);
    fprintf(stderr, "fem: closed\n");
}
int fem_is_open(const FeMenu *m) { return m && m->open; }
FemScreen fem_screen(const FeMenu *m) { return m && m->open ? m->screen : FEM_SCR_NONE; }

void fem_set_current(FeMenu *m, const char *car, const char *track) {
    if (!m) return;
    if (car) {
        snprintf(m->cur_car, sizeof m->cur_car, "%s", car);
        if(m->screen!=FEM_SCR_QR_CAR && m->cat)for(int i=0;i<m->cat->ncar;i++)
            if(!strcmp(car,m->cat->car[i].name))m->choice.car_index=i;
    }
    if (track) snprintf(m->cur_track, sizeof m->cur_track, "%s", track);
}
void fem_set_loading(FeMenu *m, int event_id) {
    if (!m) return;
    m->loading_event = event_id;
    fem_open(m, FEM_SCR_LOADING);
}

void fem_show_error(FeMenu *m, const char *message) {
    if (!m) return;
    fem_open(m, FEM_SCR_QR_TRACK);
    snprintf(m->status, sizeof m->status, "%s", message ? message : "launch failed");
}

const char *fem_preview_car(const FeMenu *m) {
    if(m && m->open && m->screen==FEM_SCR_CAREER_LOT)return m->lot.preview;
    return m && m->screen==FEM_SCR_QR_CAR ? m->choice.preview : "";
}
int fem_visual_available(const FeMenu *m) { return m && m->open && m->visual && m->compatible && m->gl; }
const char *fem_status(const FeMenu *m) {
    if(!m)return "front-end menu allocation unavailable";
    if(m->status[0])return m->status;
    if(m->screen==FEM_SCR_OPTIONS && m->options.last_write_status!=ST_OK)return "options: graphics settings could not be persisted";
    if((m->screen==FEM_SCR_PROFILE || m->screen==FEM_SCR_OPTIONS) && m->profile && m->profile->status[0])return m->profile->status;
    if(m->screen==FEM_SCR_GARAGE)return feg_status(m->garage);
    if(m->screen==FEM_SCR_QR_TRACK)return m->choice.track_status;
    if(m->screen==FEM_SCR_QR_CAR)return m->choice.car_status;
    if(m->screen==FEM_SCR_CAREER_LOT)return m->lot.status;
    if(m->screen==FEM_SCR_CAREER_PERF)return m->perf.status;
    return m->status;
}

/* ---- input ------------------------------------------------------------------------------------- */

/* [inferred] Platform adaptation: a release-inside Back target in 640x480 space.
 * Use the same retail pill kit as pause, kept below the screen content. */
static int back_hit(float x, float y) { return x >= 24 && x <= 170 && y >= 440 && y <= 472; }
static int choice_hit(FeMenu *m,float x,float y,int select_row) {
    if(m->screen==FEM_SCR_QR_OPTIONS)for(int row=0;row<fet_row_count(&m->choice);row++) {
        if(!fet_row_enabled(&m->choice,row))continue;
        float top=gl_row_top(&m->choice.list.anchors,row);
        if(y-240>=top-12 && y-240<=top+28) {
            if(select_row)m->choice.option=row;
            return x<440?FES_IN_LEFT:FES_IN_RIGHT;
        }
    }
    return x<180?FES_IN_LEFT:x>460?FES_IN_RIGHT:FES_IN_ACCEPT;
}
/* [inferred] The authored Select Event caption is a touch Accept target. */
static int map_accept_hit(float x,float y) { return x>=180 && x<=430 && y>=430 && y<=472; }
static void pointer_press(FeMenu *m, float x, float y) {
    m->back_pressed = !m->locked && back_hit(x, y);
    if (m->back_pressed) return;
    if(m->screen==FEM_SCR_WORLD_MAP) {
        m->map_pressed=map_accept_hit(x,y)?-2:m->visual?fwm_hit(&m->world_map,x-320,y-240,12): -1;
        return;
    }
    if(!is_logic_screen(m)) {
        m->pressed=choice_hit(m,x,y,1);return;
    }
    if (m->locked) return;
    if (x < 0 || x >= 640 || y < 0 || y >= 480) return;
    fes_pointer_press(&m->fes, x - 320, y - 240);
}
static void pointer_release(FeMenu *m, float x, float y) {
    int back = m->back_pressed && back_hit(x, y);
    m->back_pressed = 0;
    if (back && !m->locked) { menu_input(m,FES_IN_BACK); return; }
    if(m->screen==FEM_SCR_WORLD_MAP) {
        if(!m->locked && m->map_pressed==-2 && map_accept_hit(x,y))menu_input(m,FES_IN_ACCEPT);
        else {
            int hit=m->visual?fwm_hit(&m->world_map,x-320,y-240,12): -1;
            if(!m->locked && hit>=0 && hit==m->map_pressed)
                fwm_touch_select(&m->world_map,x-320,y-240,12);
        }
        m->map_pressed=-1;
        return;
    }
    if(!is_logic_screen(m)) {
        int hit=choice_hit(m,x,y,0);
        if(m->pressed==hit)menu_input(m,hit);
        m->pressed=0;return;
    }
    if (m->locked || !is_logic_screen(m)) return;
    fes_pointer_release(&m->fes, x - 320, y - 240);
}

static void edit_append(FeMenu *m,const char *text) {
    size_t have=strlen(m->edit_name),add=strlen(text);
    if(have+add<=ST_NAME_MAX){memcpy(m->edit_name+have,text,add+1);m->delete_confirm=0;if(m->profile)m->profile->status[0]=0;}
}
static void edit_backspace(FeMenu *m) {
    size_t n=strlen(m->edit_name);if(n){do{n--;}while(n && ((unsigned char)m->edit_name[n]&0xc0)==0x80);m->edit_name[n]=0;}m->delete_confirm=0;
}
static void edit_commit(FeMenu *m) {
    if(!m->profile)return;
    if(m->profile_action==3 && !m->delete_confirm){m->delete_confirm=1;return;}
    int rc=m->profile_action==0?fep_new(m->profile,m->edit_name):m->profile_action==1?fep_rename(m->profile,m->edit_name):m->profile_action==2?fep_load(m->profile,m->edit_name):fep_delete(m->profile,m->edit_name);
    if(rc==ST_OK){m->apply_flags|=FEM_APPLY_PROFILE;m->editing=0;SDL_StopTextInput();fem_open(m,FEM_SCR_PROFILE);}
}
static void keyboard_activate(FeMenu *m,int key) {
    if(key<0 || key>=m->nkeys)return;
    const char *text=m->scr.scene.nodes[m->key_nodes[key]].obj->text;
    if(!strcmp(text,"DONE")){edit_commit(m);return;}
    if(!strcmp(text,"BACK")){edit_backspace(m);return;}
    if(!strcmp(text,"CAPS") || !strcmp(text,"SHIFT")){m->caps_lock=!m->caps_lock;return;}
    if(!strcmp(text,"SPACE")){edit_append(m," ");return;}
    if(strlen(text)==1){char ch[2]={text[0],0};if(m->caps_lock)ch[0]=(char)tolower((unsigned char)ch[0]);edit_append(m,ch);}
}
static void advanced_event(FeMenu *m,const SDL_Event *e,SDL_Window *win) {
    int edit=m->screen==FEM_SCR_PROFILE;
    if(e->type==SDL_TEXTINPUT && edit){edit_append(m,e->text.text);return;}
    int input=0;
    if(e->type==SDL_KEYDOWN && !e->key.repeat) {
        switch(e->key.keysym.sym){case SDLK_LEFT:input=OM_IN_LEFT;break;case SDLK_RIGHT:input=OM_IN_RIGHT;break;case SDLK_UP:input=OM_IN_UP;break;case SDLK_DOWN:input=OM_IN_DOWN;break;case SDLK_RETURN:case SDLK_KP_ENTER:input=OM_IN_ACCEPT;break;case SDLK_ESCAPE:input=OM_IN_BACK;break;case SDLK_BACKSPACE:if(edit)edit_backspace(m);break;default:break;}
    } else if(e->type==SDL_CONTROLLERBUTTONDOWN) {
        switch(e->cbutton.button){case SDL_CONTROLLER_BUTTON_DPAD_LEFT:input=OM_IN_LEFT;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:input=OM_IN_RIGHT;break;case SDL_CONTROLLER_BUTTON_DPAD_UP:input=OM_IN_UP;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:input=OM_IN_DOWN;break;case SDL_CONTROLLER_BUTTON_A:if(edit){keyboard_activate(m,m->key_focus);return;}input=OM_IN_ACCEPT;break;case SDL_CONTROLLER_BUTTON_START:if(edit){edit_commit(m);return;}break;case SDL_CONTROLLER_BUTTON_B:input=OM_IN_BACK;break;case SDL_CONTROLLER_BUTTON_X:if(edit)edit_backspace(m);break;default:break;}
    } else if(e->type==SDL_MOUSEWHEEL)input=e->wheel.y>0?OM_IN_UP:OM_IN_DOWN;
    else if(win && (e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) && e->button.button==SDL_BUTTON_LEFT) {
        int w,h;float x,y;SDL_GetWindowSize(win,&w,&h);if(w<=0 || h<=0)return;
        pause_menu_to_virtual(pause_menu_transform(w,h),e->button.x,e->button.y,&x,&y);
        if(e->type==SDL_MOUSEBUTTONDOWN){m->back_pressed=back_hit(x,y);m->pressed=1;return;}
        if(!m->pressed)return;
        m->pressed=0;
        if(m->back_pressed && back_hit(x,y))input=OM_IN_BACK;
        else if(!m->back_pressed) {
            x-=320;y-=240;
            if(edit) {
                float best=2000;int found=-1;
                for(int k=0;k<m->nkeys;k++){UgNode *n=&m->scr.scene.nodes[m->key_nodes[k]];float dx=x-n->we,dy=y-n->wf;float d=dx*dx+dy*dy;if(fabsf(dx)<35 && fabsf(dy)<25 && d<best){best=d;found=k;}}
                if(found>=0){m->key_focus=found;keyboard_activate(m,found);}
            } else if(m->options.dialog.open) {
                om_pointer_click(&m->options,x,y);
            } else if(m->options.screen==OM_SCREEN_HUB) {
                const UgNode *left=&m->scr.scene.nodes[m->car_v.arrow_l],*right=&m->scr.scene.nodes[m->car_v.arrow_r];
                if(fabsf(x-left->we)<28 && fabsf(y-left->wf)<28)input=OM_IN_LEFT;
                else if(fabsf(x-right->we)<28 && fabsf(y-right->wf)<28)input=OM_IN_RIGHT;
                else if(gc_slot_hit(&m->options.hub,x,y))input=OM_IN_ACCEPT;
            } else {
                GvList *v=&m->opt_list;
                int arrows[]={v->arrow1,v->arrow2};
                for(int k=0;k<2;k++)if(arrows[k]>=0){const UgNode *n=&m->scr.scene.nodes[arrows[k]];if(fabsf(x-n->we)<22 && fabsf(y-n->wf)<22)input=k?OM_IN_DOWN:OM_IN_UP;}
                if(!input) {
                    for(int row=0;row<OM_VISIBLE_ROWS;row++){int i=m->options.list.top+row;float top=gl_row_top(&v->anchors,row);if(i<m->options.nrows && y>=top-10 && y<=top+28)m->options.list.focus=i;}
                    om_pointer_click(&m->options,x,y);
                }
            }
        }
        m->back_pressed=0;
    }
    if(!input)return;
    if(!edit){om_input(&m->options,(OmInput)input);return;}
    if(input==OM_IN_BACK){m->editing=0;SDL_StopTextInput();fem_open(m,FEM_SCR_PROFILE);}
    else if(input==OM_IN_ACCEPT)edit_commit(m);
    else if(m->nkeys){int delta=input==OM_IN_LEFT?-1:input==OM_IN_RIGHT?1:input==OM_IN_UP?-10:10;m->key_focus=(m->key_focus+delta+m->nkeys)%m->nkeys;}
}
void fem_event(FeMenu *m, const SDL_Event *e, SDL_Window *win) {
    if (!fem_is_open(m) || m->locked || m->screen==FEM_SCR_LOADING) return;
    if((m->screen==FEM_SCR_OPTIONS || m->screen==FEM_SCR_PROFILE) && !m->visual) {
        int back=(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) ||
            (e->type==SDL_CONTROLLERBUTTONDOWN && e->cbutton.button==SDL_CONTROLLER_BUTTON_B);
        if(win && e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT) {
            int w,h;float x,y;SDL_GetWindowSize(win,&w,&h);
            if(w>0 && h>0){pause_menu_to_virtual(pause_menu_transform(w,h),e->button.x,e->button.y,&x,&y);back=back_hit(x,y);}
        }
        if(back){om_close(&m->options);fem_open(m,FEM_SCR_MAIN);}return;
    }
    if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
        e->window.event == SDL_WINDOWEVENT_HIDDEN)) { m->back_pressed = m->pressed = 0; m->map_pressed=-1; return; }
    if(m->screen==FEM_SCR_OPTIONS || (m->screen==FEM_SCR_PROFILE && m->editing)){advanced_event(m,e,win);return;}
    if(m->screen==FEM_SCR_WORLD_MAP) {
        if(e->type==SDL_KEYDOWN && !e->key.repeat && e->key.keysym.sym==SDLK_TAB) {
            m->pending_input=(e->key.keysym.mod&KMOD_SHIFT)?FWM_IN_PREVIOUS:FWM_IN_NEXT;return;
        }
        if(e->type==SDL_CONTROLLERBUTTONDOWN &&
           (e->cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER || e->cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
            m->pending_input=e->cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER?FWM_IN_PREVIOUS:FWM_IN_NEXT;return;
        }
    }
    FesInput in = 0;
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        switch (e->key.keysym.sym) {
        case SDLK_UP: if(!is_logic_screen(m)){m->pending_input=5;return;} break;
        case SDLK_DOWN: if(!is_logic_screen(m)){m->pending_input=6;return;} break;
        case SDLK_LEFT: in = FES_IN_LEFT; break;
        case SDLK_RIGHT: in = FES_IN_RIGHT; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: in = FES_IN_ACCEPT; break;
        case SDLK_ESCAPE: in = FES_IN_BACK; break;
        default: break;
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: m->pending_input=5;return;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: m->pending_input=6;return;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: in = FES_IN_LEFT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in = FES_IN_RIGHT; break;
        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: in = FES_IN_ACCEPT; break;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: in = FES_IN_BACK; break;
        default: break;
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT) in = FES_IN_BACK;
    else if (e->type == SDL_MOUSEWHEEL && e->wheel.y) {
        int d = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        in = d > 0 ? FES_IN_LEFT : FES_IN_RIGHT;
    } else if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.button == SDL_BUTTON_LEFT && win) {
        int w, h;
        SDL_GetWindowSize(win, &w, &h);                      /* also the path of touch taps (SDL_TOUCH_MOUSEID) */
        if (w <= 0 || h <= 0) return;
        float x, y;
        pause_menu_to_virtual(pause_menu_transform(w, h), e->button.x, e->button.y, &x, &y);
        if (e->type == SDL_MOUSEBUTTONDOWN) pointer_press(m, x, y); else pointer_release(m, x, y);
        return;
    }
    if (in) menu_input(m,in);
}

/* ---- update ----------------------------------------------------------------------------------- */

static FemRequest mode_request(FeMenu *m, int mode) {
    FemRequest r;
    memset(&r, 0, sizeof r);
    int kind = fe_mode_race_kind(mode);
    if (kind < 0) {
        r.kind = FEM_REQ_FREE_RUN;
        snprintf(r.track, sizeof r.track, "STREAML4RA");
        snprintf(r.car, sizeof r.car, "%s", m->cur_car);
        return r;
    }
    int i=m->choice.event_index;
    if(!m->cat || i<0 || i>=m->cat->nev)return r;
    const GcEvent *ev = &m->cat->ev[i];
    r.kind = FEM_REQ_QUICK_RACE;
    r.event_id = ev->id;
    snprintf(r.car,sizeof r.car,"%s",m->cat->car[m->choice.car_index].name);
    r.laps=m->choice.laps;r.difficulty=m->choice.difficulty;r.rules=m->choice.rules;if(ev->kind!=1)r.rules.lap_knockout=0;   /* Lap KO is circuit-only */
    for (int k = 0; k < m->cat->nregion; k++)
        if (!strcmp(m->cat->region[k].stem, ev->region)) { snprintf(r.track, sizeof r.track, "%s", m->cat->region[k].track); break; }
    return r;
}

FemRequest fem_update(FeMenu *m, float seconds) {
    FemRequest req;
    memset(&req, 0, sizeof req);
    if (!fem_is_open(m)) return req;
    float ms = seconds > 0 ? seconds * 1000.0f : 0.0f;
    m->ui_ms += ms;
    m->ui.time_ms = m->ui_ms;
    if(m->screen==FEM_SCR_OPTIONS) {
        om_update(&m->options,seconds);
        if(om_take_apply_request(&m->options))m->apply_flags|=FEM_APPLY_GRAPHICS;
        if(om_take_reinit_request(&m->options))m->apply_flags|=FEM_APPLY_REINIT;
        if(om_take_profile_request(&m->options)){if(m->profile)fep_save(m->profile);m->apply_flags|=FEM_APPLY_PROFILE;}
        OmEvent event;while((event=om_poll_event(&m->options))!=OM_EV_NONE) {
            if(event==OM_EV_LEAVE_TO_PAUSE || event==OM_EV_RESUME_GAME){fem_open(m,FEM_SCR_MAIN);break;}
            if(event==OM_EV_LEAVE_TO_HUB){OmConfig cfg;om_config_default(&cfg,OM_CTX_FRONTEND);cfg.profile=m->profile && m->profile->valid?&m->profile->value:NULL;om_open_hub(&m->options,&cfg,&m->graphics,&m->caps,m->store_open?&m->store:NULL);}
        }
        if(m->screen==FEM_SCR_OPTIONS && m->opt_shown!=om_screen(&m->options))build_screen(m);
    }
    int in=m->pending_input;m->pending_input=0;
    if(m->screen==FEM_SCR_WORLD_MAP && in && !m->locked) {
        int event=(m->visual || in==FES_IN_BACK)?fwm_input(&m->world_map,in,&req.destination):FWM_EV_NONE;
        if(event==FWM_EV_DESTINATION){req.kind=FEM_REQ_SET_DESTINATION;m->locked=1;}
        else if(event==FWM_EV_LEAVE){req.kind=FEM_REQ_CAREER_ROAM;m->locked=1;}
    }
    if(m->screen==FEM_SCR_GARAGE) {
        int event=in&&(m->visual||in==FES_IN_BACK)?feg_input(m->garage,in):0;
        if(event==2 && m->career_garage){req.kind=FEM_REQ_CAREER_ROAM;m->locked=1;}   /* M6: back to roam, not Main */
        else if(event==2)fem_open(m,FEM_SCR_MAIN);
        else if(event==1||feg_advance(m->garage,seconds))build_screen(m);
    }

    if(m->screen>=FEM_SCR_QR_TRACK && m->screen<=FEM_SCR_QR_CAR && in) {
        if(in==FES_IN_BACK) {
            if(m->screen==FEM_SCR_QR_OPTIONS && m->choice.options_page){m->choice.options_page=0;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else fem_open(m,m->screen==FEM_SCR_QR_TRACK?FEM_SCR_QR_MODE:m->screen==FEM_SCR_QR_OPTIONS?FEM_SCR_QR_TRACK:FEM_SCR_QR_OPTIONS);
        }
        else if(in==FES_IN_ACCEPT && m->visual) {
            if(m->screen==FEM_SCR_QR_CAR){req=mode_request(m,m->mode_focus);m->locked=req.kind!=0;}
            else if(m->screen==FEM_SCR_QR_TRACK){m->choice.options_page=0;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else if(!m->choice.options_page){m->choice.options_page=1;m->choice.option=0;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else fem_open(m,FEM_SCR_QR_CAR);
        } else if(in==FES_IN_LEFT || in==FES_IN_RIGHT || in==5 || in==6) {
            int dir=in==FES_IN_LEFT || in==5?-1:1;
            if(m->screen==FEM_SCR_QR_CAR)fec_move(&m->choice,dir);
            else fet_move(&m->choice,dir,in>=5,m->screen==FEM_SCR_QR_OPTIONS);
        }
    }
    if(m->screen==FEM_SCR_CAREER_LOT) {
        FelEvent ev=in && !m->locked?fel_input(&m->lot,in):FEL_EV_NONE;
        if(ev==FEL_EV_BUY){req.kind=FEM_REQ_LOT_BUY;snprintf(req.car,sizeof req.car,"%s",fel_selected(&m->lot));m->locked=1;}
        else if(ev==FEL_EV_LEAVE){req.kind=FEM_REQ_CAREER_ROAM;m->locked=1;}
        fel_advance(&m->lot,seconds);
    }
    if(m->screen==FEM_SCR_CAREER_PERF) {
        FepEvent ev=in && !m->locked?fpr_input(&m->perf,in):FEP_EV_NONE;
        if(ev==FEP_EV_BUY){req.kind=FEM_REQ_PERF_BUY;req.perf_cat=m->perf.buy_cat;req.perf_lvl=m->perf.buy_lvl;req.perf_comp=m->perf.buy_comp;m->locked=1;}
        else if(ev==FEP_EV_LEAVE){req.kind=FEM_REQ_CAREER_ROAM;m->locked=1;}
        else if(ev==FEP_EV_PAGE){fprintf(stderr,"fem: performance shop page %s\n",fpr_package(&m->perf));build_screen(m);}
    }
    if (is_logic_screen(m)) {
        fes_update(&m->fes, seconds);
        if (m->fes.exiting) {
            m->exit_wait_ms += ms;
            /* the authored exit script reports completion; without art there is nothing to wait for */
            if (!m->visual || m->scr.exit_done || m->exit_wait_ms >= FE_EXIT_TIMEOUT_MS) fes_exit_complete(&m->fes);
        }
        int id = -1;
        FesEvent ev = fes_poll(&m->fes, &id);
        if (ev == FES_EV_CHOSEN) {
            if (m->screen == FEM_SCR_MAIN) {
                m->main_focus = id;
                if (id == FE_MAIN_CAREER && (m->main_enabled & (1u << FE_MAIN_CAREER))) {
                    m->career_confirm = 0; m->career_focus = m->career_save ? FE_CAREER_RESUME : FE_CAREER_NEW;
                    fem_open(m, FEM_SCR_CAREER);
                } else if (id == FE_MAIN_QUICK_RACE) fem_open(m, FEM_SCR_QR_MODE);
                else if(id==FE_MAIN_CUSTOMIZE){m->career_garage=0;fem_open(m,FEM_SCR_GARAGE);}
                else if(id==FE_MAIN_OPTIONS)fem_open(m,FEM_SCR_OPTIONS);
                else if(id==FE_MAIN_PROFILE)fem_open(m,FEM_SCR_PROFILE);
                else fem_open(m,FEM_SCR_MAIN);
            } else if (m->screen == FEM_SCR_CAREER) {
                m->career_focus = id;
                if (id == FE_CAREER_NEW && m->career_save && !m->career_confirm) {
                    m->career_confirm = 1; fem_open(m, FEM_SCR_CAREER);   /* a second choice confirms the restart */
                } else {
                    req.kind = FEM_REQ_CAREER; req.event_id = id == FE_CAREER_NEW;   /* 1 = new, 0 = resume */
                    snprintf(req.car, sizeof req.car, "%s", m->cur_car); m->locked = 1;
                }
            } else if(m->screen==FEM_SCR_PROFILE) {
                m->profile_action=id;m->editing=1;m->delete_confirm=0;
                snprintf(m->edit_name,sizeof m->edit_name,"%.*s",ST_NAME_MAX,id==1 && m->profile?m->profile->value.name:"");
                build_screen(m);
#ifndef N2_IOS
                SDL_StartTextInput();
#endif
            } else {
                m->mode_focus = id;
                if(fe_mode_race_kind(id)<0){req=mode_request(m,id);m->locked=1;}
                else {m->choice.kind=fe_mode_race_kind(id);m->choice.selected=0;fem_open(m,FEM_SCR_QR_TRACK);}
            }
        } else if (ev == FES_EV_BACK) {
            if (m->screen == FEM_SCR_QR_MODE || m->screen==FEM_SCR_PROFILE || m->screen==FEM_SCR_CAREER) fem_open(m, FEM_SCR_MAIN);
            else if (m->allow_quit) { req.kind = FEM_REQ_QUIT; m->locked = 1; }
            else fem_open(m, FEM_SCR_MAIN);
        }
    }
    views_update(m, ms);
    return req;
}

/* ---- drawing ---------------------------------------------------------------------------------- */

void fem_draw(FeMenu *m, int w, int h) {
    if (!fem_is_open(m) || w <= 0 || h <= 0) return;
    if (m->screen != FEM_SCR_QR_CAR && m->screen != FEM_SCR_GARAGE && m->screen != FEM_SCR_CAREER_LOT && m->screen != FEM_SCR_CAREER_PERF) {   /* 3D car behind */
        static const float black[4] = { 0, 0, 0, 1 };
        rhi_cmd_clear_main(black, 1.0f);
    }
    if (!m->gl || !m->compatible) return;
    const UgFace *face = ug_text_face();
    const UgSkin *old = ug_kit_skin();
    UgSkin saved;
    if (old) saved = *old;
    ug_text_set_face(ug_assets_main_face(&m->assets));
    ug_kit_set_skin(&m->skin);
    UgCanvas *cv = &m->canvas;
    ug_cv_clear(cv);
    if (m->visual && !gv_screen_draw(&m->scr, &m->ui, cv)) {
        m->visual = 0;
        snprintf(m->status, sizeof m->status, "%s", m->scr.status);
        fprintf(stderr, "fem: %s\n", m->status);
        ug_cv_clear(cv);
        cv->visual_status = UG_VIS_UNAVAILABLE;
    }
    if(m->screen==FEM_SCR_WORLD_MAP && m->visual && !fwm_draw(&m->world_map,&m->scr,cv)) {
        m->visual=0;
        snprintf(m->status,sizeof m->status,"%s",m->world_map.status);
        ug_cv_clear(cv);       /* fail closed, retaining only the reachable Back adaptation below */
    }
    if(m->screen==FEM_SCR_OPTIONS && m->opt_dialog.open)gv_screen_draw(&m->opt_dialog.scr,&m->ui,cv);
    if(m->screen!=FEM_SCR_LOADING)gv_touch_back(cv,&m->assets);
    ug_text_set_face(ug_assets_main_face(&m->assets));
    if (ug_cv_visual_status(cv) == UG_VIS_OK) ug_gl_render_overlay(m->gl, cv, w, h);
    ug_text_set_face(face);
    ug_kit_set_skin(old ? &saved : NULL);
}

int fem_set_storage(FeMenu *m,const char *config,const char *root) {
    if(!m || m->screen==FEM_SCR_OPTIONS || m->screen==FEM_SCR_PROFILE || m->screen==FEM_SCR_GARAGE || feg_busy(m->garage))return ST_E_ARG;
    if(config && strlen(config)>=sizeof m->config_path)return ST_E_ARG;
    StRegStore next={0};int rc=config?st_reg_open_ini(&next,config):st_reg_open_default(&next);
    if(rc!=ST_OK)return rc;
    FeProfile *profile=fep_create(root,&next);if(!profile){st_reg_close(&next);return ST_E_NOMEM;}
    fep_destroy(m->profile);if(m->store_open)st_reg_close(&m->store);
    m->store=next;m->store_open=1;m->profile=profile;profile->store=&m->store;
    snprintf(m->config_path,sizeof m->config_path,"%s",config?config:"");
    if(profile->valid && profile->persisted)m->apply_flags|=FEM_APPLY_PROFILE;
    st_gfx_defaults(&m->graphics);st_gfx_load(&m->store,&m->graphics);return ST_OK;
}
void fem_set_caps(FeMenu *m,const StGfxCaps *caps){if(m && caps)m->caps=*caps;}
unsigned fem_take_apply(FeMenu *m){if(!m)return 0;unsigned bits=m->apply_flags;m->apply_flags=0;return bits;}
const StGraphics *fem_graphics(const FeMenu *m){return m?&m->graphics:NULL;}
const StProfile *fem_current_profile(const FeMenu *m){return m && m->profile && m->profile->valid?&m->profile->value:NULL;}
int fem_options_page(const FeMenu *m){return m && m->screen==FEM_SCR_OPTIONS?m->options.screen:OM_SCREEN_CLOSED;}

void fem_set_graphics(FeMenu *m,const StGraphics *graphics){if(m && graphics && m->screen!=FEM_SCR_OPTIONS)m->graphics=*graphics;}

void fem_set_career(FeMenu *m, int available, int has_save)
{
    if (!m) return;
    if (available) m->main_enabled |= 1u << FE_MAIN_CAREER; else m->main_enabled &= ~(1u << FE_MAIN_CAREER);
    m->career_save = has_save != 0;
    if (m->screen == FEM_SCR_MAIN) build_screen(m);
}
const char *fem_profile_root(const FeMenu *m){return m && m->profile?m->profile->root:NULL;}
int fem_persist_profile(FeMenu *m){if(!m || !m->profile || !m->profile->valid)return ST_E_ARG;return fep_persist(m->profile);}

int fem_garage_current(FeMenu *m,const char *car,const GarageConfig *cfg) {
    return m && m->screen!=FEM_SCR_GARAGE && feg_current(m->garage,car,cfg);
}
int fem_garage_take(FeMenu *m,GarageRequest *r){return m&&feg_take(m->garage,r);}
void fem_garage_ack(FeMenu *m,uint32_t serial,int ok,const char *error){if(m)feg_ack(m->garage,serial,ok,error);}
int fem_garage_page(const FeMenu *m){return m?feg_page(m->garage):-1;}

int fem_garage_saved(const FeMenu *m,const char *car,GarageConfig *out) {
 if(!m||!m->profile||!m->profile->valid)return ST_E_ARG;
 return garage_load(m->profile->root,m->profile->value.name,car,out);
}

void fem_open_lot(FeMenu *m,const FeLotCar *cars,int n,int must_buy,long long bank) {
    if(!m || feg_busy(m->garage))return;
    fel_open(&m->lot,cars,n,must_buy,bank);
    m->career_garage=0;
    fem_open(m,FEM_SCR_CAREER_LOT);
}
void fem_open_perf(FeMenu *m,const FePerfShop *shop) {
    if(!m || feg_busy(m->garage))return;
    fpr_open(&m->perf,shop);
    m->career_garage=0;
    fem_open(m,FEM_SCR_CAREER_PERF);
}
void fem_perf_refresh(FeMenu *m,const FePerfShop *shop,int msg) {
    if(!m || m->screen!=FEM_SCR_CAREER_PERF)return;
    fpr_refresh(&m->perf,shop,(FepMsg)msg);
    m->locked=0;
    build_screen(m);
    views_update(m,0);
}
void fem_open_career_garage(FeMenu *m) {
    if(!m || feg_busy(m->garage))return;
    m->career_garage=1;
    fem_open(m,FEM_SCR_GARAGE);
}

void fem_open_world_map(FeMenu *m,const FeMapIcon *icons,int n,float x,float y,float heading) {
    if(!m)return;
    fwm_open(&m->world_map,icons,n,x,y,heading);
    fem_open(m,FEM_SCR_WORLD_MAP);
}
const FeWorldMap *fem_world_map_state(const FeMenu *m) {
    return m && m->screen==FEM_SCR_WORLD_MAP?&m->world_map:NULL;
}

void fem_open_career_shop(FeMenu *m,int type,uint32_t slots,
                         int (*save)(void *,const char *,const GarageConfig *),void *user) {
    if(!m||feg_busy(m->garage))return;
    if(!feg_shop(m->garage,type,slots))return;
    m->career_parts_save=save;m->career_save_user=user;
    fem_open_career_garage(m);
}
