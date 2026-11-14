/* fe_menu.c -- see fe_menu.h. Structure follows pause_menu.c (assets, UgGL, canvas, GvScreen +
 * GvCarousel, pointer events) and pause_screen.c (exit timing: the choice fires after the exit). */
#include "fe_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe_entries.h"
#include "fe_screen.h"
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
#define FE_M1_MAIN_ENABLED (1u << FE_MAIN_QUICK_RACE)   /* Options joins in M2 */

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
    int main_focus, mode_focus;  /* last focused entry ids, restored when a screen is re-entered */
    char cur_car[32], cur_track[16];
    FeChoice choice;
    int pending_input, pressed;
    int loading_event;
    char status[256];
    char res_status[256];        /* why the resources are unavailable (set at creation) */
};

static const char *screen_name(FemScreen s) {
    switch (s) {
    case FEM_SCR_MAIN: return "MAIN";
    case FEM_SCR_QR_MODE: return "QR_MODE";
    case FEM_SCR_QR_TRACK: return "QR_TRACK";
    case FEM_SCR_QR_OPTIONS: return "QR_OPTIONS";
    case FEM_SCR_QR_CAR: return "QR_CAR";
    case FEM_SCR_LOADING: return "LOADING";
    default: return "NONE";
    }
}
static const char *package_of(FemScreen s) {
    switch (s) {
    case FEM_SCR_MAIN: return "UI_Main.fng";
    case FEM_SCR_QR_MODE: return "UI_QRModeSelect.fng";
    case FEM_SCR_LOADING: return "PC_Loading.fng";
    default: return s>=FEM_SCR_QR_TRACK && s<=FEM_SCR_QR_CAR ? k_hooks[s].name : NULL;
    }
}

/* ---- views ------------------------------------------------------------------------------------ */

static void drop_view(FeMenu *m) {
    if (m->scr.loaded) gv_screen_free(&m->scr);
    m->visual = 0;
    m->back_pressed = 0;
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

static void build_screen(FeMenu *m) {
    drop_view(m);
    m->status[0] = 0;
    m->icon_warned = 0;
    m->locked = 0;
    const char *pkg = package_of(m->screen);
    if (!pkg) {set_status(m,"%s: unknown screen",screen_name(m->screen));return;}
    if (!m->assets_open || !m->compatible || !m->gl) {
        snprintf(m->status, sizeof m->status, "%s: front-end resources unavailable (%.180s)", screen_name(m->screen), m->res_status);
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
        int n = m->screen == FEM_SCR_MAIN ? fe_main_entries(e, FE_M1_MAIN_ENABLED) : fe_mode_entries(e);
        fes_open(&m->fes, e, n, m->screen == FEM_SCR_MAIN ? m->main_focus : m->mode_focus, rail_width(&m->scr, GC_RAIL_PAUSE));
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
    if (m->has_carousel || (m->screen != FEM_SCR_MAIN && m->screen != FEM_SCR_QR_MODE)) return;
    FeEntry e[GC_MAX];
    int n = m->screen == FEM_SCR_MAIN ? fe_main_entries(e, FE_M1_MAIN_ENABLED) : fe_mode_entries(e);
    fes_open(&m->fes, e, n, m->screen == FEM_SCR_MAIN ? m->main_focus : m->mode_focus, GC_RAIL_PAUSE);
}

static int is_logic_screen(const FeMenu *m) { return m->screen==FEM_SCR_MAIN || m->screen==FEM_SCR_QR_MODE; }
static void menu_input(FeMenu *m,int in) {
    if(is_logic_screen(m))fes_input(&m->fes,in);
    else if(m->screen>=FEM_SCR_QR_TRACK && m->screen<=FEM_SCR_QR_CAR)m->pending_input=in;
}

static void entries_to_view(FeMenu *m, GvEntry out[GC_MAX]) {
    for (int i = 0; i < m->fes.n; i++) {
        const FeEntry *e = &m->fes.e[i];
        out[i].tex = gv_texture(&m->assets, e->icon_key);
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
    if(m->screen==FEM_SCR_QR_CAR)fec_sync(&m->choice,&m->scr,ms/1000);
    else if(m->screen==FEM_SCR_QR_TRACK || m->screen==FEM_SCR_QR_OPTIONS)
        fet_sync(&m->choice,&m->scr,ms,m->screen==FEM_SCR_QR_OPTIONS);
    if(m->screen==FEM_SCR_QR_TRACK && m->status[0])for(int i=0;i<m->scr.scene.n;i++)
        if(m->scr.scene.nodes[i].obj->guid==0xdf142)m->scr.scene.nodes[i].text_override=m->status;
    if (m->has_carousel) {
        if (m->fes.exiting && !m->exit_sent) { m->exit_sent = 1; gv_screen_exit(&m->scr); }
        GvEntry e[GC_MAX];
        entries_to_view(m, e);
        gv_carousel_apply(&m->car_v, &m->fes.car, e, m->fes.n, NULL, 0, 0);
    }
}

/* ---- creation --------------------------------------------------------------------------------- */

FeMenu *fem_create(const char *dataroot, const struct GameCatalog *cat, int allow_quit) {
    FeMenu *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    snprintf(m->dataroot, sizeof m->dataroot, "%s", dataroot ? dataroot : "");
    m->cat = cat;
    m->choice.cat=cat;m->choice.laps=2;m->choice.kind=1;
    m->allow_quit = allow_quit;
    m->main_focus = FE_MAIN_QUICK_RACE;
    m->mode_focus = FE_MODE_CIRCUIT;
    m->loading_event = -1;
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
    free(m);
}

void fem_open(FeMenu *m, FemScreen screen) {
    if (!m) return;
    if (screen == FEM_SCR_NONE) { fem_close(m); return; }
    m->pending_input=0;m->pressed=0;
    m->screen = screen;
    m->open = 1;
    fprintf(stderr, "fem: screen %s\n", screen_name(screen));
    build_screen(m);
    ensure_logic(m);
    if (!m->visual && m->status[0]) fprintf(stderr, "fem: %s; screen not drawn (no native fallback)\n", m->status);
    views_update(m, 0);
}

void fem_close(FeMenu *m) {
    if (!m || !m->open) return;
    m->open = 0;
    m->screen = FEM_SCR_NONE;
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

const char *fem_preview_car(const FeMenu *m) { return m && m->screen==FEM_SCR_QR_CAR ? m->choice.preview : ""; }
int fem_visual_available(const FeMenu *m) { return m && m->open && m->visual && m->compatible && m->gl; }
const char *fem_status(const FeMenu *m) { return m ? m->status : "front-end menu allocation unavailable"; }

/* ---- input ------------------------------------------------------------------------------------- */

/* [inferred] Platform adaptation: a release-inside Back target in 640x480 space.
 * Use the same retail pill kit as pause, kept below the screen content. */
static int back_hit(float x, float y) { return x >= 24 && x <= 170 && y >= 440 && y <= 472; }
static int choice_hit(FeMenu *m,float x,float y,int select_row) {
    if(m->screen==FEM_SCR_QR_OPTIONS)for(int row=0;row<2;row++) {
        float top=gl_row_top(&m->choice.list.anchors,row);
        if(y-240>=top-12 && y-240<=top+28) {
            if(select_row)m->choice.option=row;
            return x<440?FES_IN_LEFT:FES_IN_RIGHT;
        }
    }
    return x<180?FES_IN_LEFT:x>460?FES_IN_RIGHT:FES_IN_ACCEPT;
}
static void pointer_press(FeMenu *m, float x, float y) {
    m->back_pressed = !m->locked && back_hit(x, y);
    if (m->back_pressed) return;
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
    if(!is_logic_screen(m)) {
        int hit=choice_hit(m,x,y,0);
        if(m->pressed==hit)menu_input(m,hit);
        m->pressed=0;return;
    }
    if (m->locked || !is_logic_screen(m)) return;
    fes_pointer_release(&m->fes, x - 320, y - 240);
}

void fem_event(FeMenu *m, const SDL_Event *e, SDL_Window *win) {
    if (!fem_is_open(m) || m->locked || m->screen==FEM_SCR_LOADING) return;
    if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
        e->window.event == SDL_WINDOWEVENT_HIDDEN)) { m->back_pressed = 0; return; }
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
    r.laps=m->choice.laps;r.difficulty=m->choice.difficulty;
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
    int in=m->pending_input;m->pending_input=0;
    if(m->screen>=FEM_SCR_QR_TRACK && m->screen<=FEM_SCR_QR_CAR && in) {
        if(in==FES_IN_BACK) {
            if(m->screen==FEM_SCR_QR_OPTIONS && m->choice.options_page){m->choice.options_page=0;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else fem_open(m,m->screen==FEM_SCR_QR_TRACK?FEM_SCR_QR_MODE:m->screen==FEM_SCR_QR_OPTIONS?FEM_SCR_QR_TRACK:FEM_SCR_QR_OPTIONS);
        }
        else if(in==FES_IN_ACCEPT && m->visual) {
            if(m->screen==FEM_SCR_QR_CAR){req=mode_request(m,m->mode_focus);m->locked=req.kind!=0;}
            else if(m->screen==FEM_SCR_QR_TRACK){m->choice.options_page=0;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else if(!m->choice.options_page){m->choice.options_page=1;m->choice.option=1;fem_open(m,FEM_SCR_QR_OPTIONS);}
            else fem_open(m,FEM_SCR_QR_CAR);
        } else if(in==FES_IN_LEFT || in==FES_IN_RIGHT || in==5 || in==6) {
            int dir=in==FES_IN_LEFT || in==5?-1:1;
            if(m->screen==FEM_SCR_QR_CAR)fec_move(&m->choice,dir);
            else fet_move(&m->choice,dir,in>=5,m->screen==FEM_SCR_QR_OPTIONS);
        }
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
                if (id == FE_MAIN_QUICK_RACE) fem_open(m, FEM_SCR_QR_MODE);
                else fem_open(m, FEM_SCR_MAIN);              /* not built yet: cannot happen, entry is disabled */
            } else {
                m->mode_focus = id;
                if(fe_mode_race_kind(id)<0){req=mode_request(m,id);m->locked=1;}
                else {m->choice.kind=fe_mode_race_kind(id);m->choice.selected=0;fem_open(m,FEM_SCR_QR_TRACK);}
            }
        } else if (ev == FES_EV_BACK) {
            if (m->screen == FEM_SCR_QR_MODE) fem_open(m, FEM_SCR_MAIN);
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
    if (m->screen != FEM_SCR_QR_CAR) {
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
    if(m->screen!=FEM_SCR_LOADING)gv_touch_back(cv,&m->assets);
    if (m->visual && !gv_screen_draw(&m->scr, &m->ui, cv)) {
        m->visual = 0;
        snprintf(m->status, sizeof m->status, "%s", m->scr.status);
        fprintf(stderr, "fem: %s\n", m->status);
        ug_cv_clear(cv);
        cv->visual_status = UG_VIS_UNAVAILABLE;
    }
    ug_text_set_face(ug_assets_main_face(&m->assets));
    if (ug_cv_visual_status(cv) == UG_VIS_OK) ug_gl_render_overlay(m->gl, cv, w, h);
    ug_text_set_face(face);
    ug_kit_set_skin(old ? &saved : NULL);
}
