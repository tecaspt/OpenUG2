/* options_menu.c -- graphics option screens: hub, Display, Customize and the Default Settings
 * confirmation (SPEC_GUI.md, SPEC_GUI_ART.md). See options_menu.h and
 * docs/cleanroom-settings/IMPL_DESIGN_GUI.md. No GL, SDL or drawing: the screens are the authored
 * packages, evaluated by gui_view. */
#include "options_menu.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include "gui_timing.h"

/* ---- static tables --------------------------------------------------------------------------- */

typedef struct CustRow {         /* SPEC_GUI 4.1, in list order */
    int gfx;
    uint32_t key;
    const char *label;
    OmRowKind kind;
    OmValueText vt;
    int domain_max;              /* sliders: upper bound of the value domain (SPEC 2.3); -1 = the cap only */
    int reinit;                  /* SPEC 4.4 re-initialisation list */
} CustRow;
static const CustRow k_customize[] = {
    { ST_GFX_CAR_ENV_MAP,        0x7D3930E7u, "Car Reflection Update Rate", OM_ROW_SLIDER, OM_VT_NONE,    3, 1 },
    { ST_GFX_CAR_ENV_MAP_UPDATE, 0x8FE9288Eu, "Car Reflection Detail",      OM_ROW_CHOICE, OM_VT_LOWHIGH, 0, 0 },
    { ST_GFX_CAR_SHADOW,         0xA7A683ACu, "Car Shadow / Neon",          OM_ROW_SLIDER, OM_VT_NONE,    2, 0 },
    { ST_GFX_CAR_HEADLIGHT,      0xCFBEDB50u, "Car Headlight",              OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_CAR_LOD,            0x4AC50BCFu, "Car Geometry Detail",        OM_ROW_CHOICE, OM_VT_LOWHIGH, 0, 0 },
    { ST_GFX_CROWD,              0x06CC3750u, "Crowds",                     OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_WORLD_LOD,          0xBDE2E94Bu, "World Detail",               OM_ROW_SLIDER, OM_VT_NONE,    3, 0 },
    { ST_GFX_ROAD_REFLECTION,    0x2A2A4AB3u, "Road Reflection Detail",     OM_ROW_SLIDER, OM_VT_NONE,    3, 1 },
    { ST_GFX_LIGHT_STREAKS,      0x6F2C7417u, "Light Trails",               OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_LIGHT_GLOW,         0x62925361u, "Light Glow",                 OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_PARTICLES,          0x2522A4E9u, "Particle System",            OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_MOTION_BLUR,        0x822E4E9Bu, "Motion Blur",                OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_FOG,                0x95B8F38Du, "Fog",                        OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_DEPTH_OF_FIELD,     0xA9C27DFDu, "Depth Of Field",             OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_FSAA,               0x4CD775ACu, "Full Screen Anti-Aliasing",  OM_ROW_SLIDER, OM_VT_NONE,   -1, 1 },
    { ST_GFX_TINTING,            0x11504D0Eu, "Tinting",                    OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_HORIZON_FOG,        0x4E57D355u, "Horizon Fog",                OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_OVER_BRIGHT,        0xD6F5F38Cu, "Over Bright",                OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_BLEACH_BYPASS,      0x571F6C00u, "Enhanced Contrast",          OM_ROW_CHOICE, OM_VT_ONOFF,   0, 1 },
    { ST_GFX_RAIN,               0x4CDDBEBBu, "Rain Splatter",              OM_ROW_CHOICE, OM_VT_ONOFF,   0, 0 },
    { ST_GFX_TEXTURE_FILTERING,  0x61A50B8Fu, "Texture Filtering",          OM_ROW_SLIDER, OM_VT_NONE,    2, 1 },
    { ST_GFX_VSYNC,              0x5B4AA6E4u, "Vsync",                      OM_ROW_CHOICE, OM_VT_VSYNC,   0, 1 },
};
#define N_CUSTOMIZE ((int)(sizeof k_customize / sizeof k_customize[0]))

static const char *const k_resolution[6] = {            /* SPEC_GUI 3.1 (functional text) */
    "640x480", "800x600", "1024x768", "1280x960", "1280x1024", "1600x1200"
};


/* Hub entries, SPEC_GUI_ART 2.5 (icon texture keys, labels, page indices). */
static const OmHubEntry k_hub[OM_HUB_ENTRIES] = {
    { 0xF37AF144u, 0xE76CD783u, "Audio", 0 },
    { 0x8A006328u, 0x4204D527u, "Display", 1 },
    { 0x4DF98FB2u, 0xD0CF6EE1u, "Gameplay", 2 },
    { 0xD708EFEFu, 0xF760EABEu, "Player", 3 },
};
/* The Controls entry (CONTROLS_SPEC 2.1 row 5): icon OPTIONS_ICON_CONTROLS, label key of "Controls". Its text
 * comes from the language file only: no English fallback is carried. */
static const OmHubEntry k_hub_controls = { 0xA04A7B26u, 0x04DC6DB5u, NULL, OM_HUB_PAGE_CONTROLS };
#define HUB_PAGE_DISPLAY 1

/* ---- packages (SPEC_GUI 1.2) ------------------------------------------------------------------- */

const char *om_package_name(OmContext ctx, OmScreen screen) {
    if (screen == OM_SCREEN_CUSTOMIZE) return "UI_PC_Customize_Options.fng";
    if (screen == OM_SCREEN_DISPLAY || screen == OM_SCREEN_GAMEPLAY || screen==OM_SCREEN_AUDIO || screen==OM_SCREEN_PLAYER) return ctx == OM_CTX_INGAME ? "UI_PauseOptions.fng" : "UI_Options.fng";
    if (screen == OM_SCREEN_HUB) return om_hub_package_name(ctx);
    if (screen == OM_SCREEN_CONTROLS) return "UI_Options_PC_Controller.fng";
    return NULL;
}
uint32_t om_package_key(OmContext ctx, OmScreen screen) {
    if (screen == OM_SCREEN_CUSTOMIZE) return 0xC4ACC9A5u;
    if (screen == OM_SCREEN_DISPLAY || screen == OM_SCREEN_GAMEPLAY || screen==OM_SCREEN_AUDIO || screen==OM_SCREEN_PLAYER) return ctx == OM_CTX_INGAME ? 0xAEA9078Fu : 0x551CA351u;
    if (screen == OM_SCREEN_HUB) return ctx==OM_CTX_INGAME?0x73F51C74u:0x1e74c936u;
    if (screen == OM_SCREEN_CONTROLS) return 0xE0EE65E6u;
    return 0;
}
const char *om_hub_package_name(OmContext ctx) {
    return ctx == OM_CTX_INGAME ? "UI_PauseOptionsMain.fng" : "UI_OptionsMain.fng";
}
const char *om_dialog_package_name(void) { return "GenericDialog_SMALL.fng"; }
int om_exit_ms(OmContext ctx, OmScreen screen) {
    const char *n = om_package_name(ctx, screen);
    return n ? gt_exit_complete_ms(n) : 0;
}

/* Grid anchors (centre + size), SPEC_GUI 2.3 table. */
GlAnchors om_anchors(OmContext ctx, OmScreen screen) {
    if (screen == OM_SCREEN_CUSTOMIZE) return gl_anchors_from_centres(-152, -120, 240, 36, 126, -122, 200, 32);
    if (ctx == OM_CTX_INGAME) return gl_anchors_from_centres(-150.2f, -120.9f, 240, 36, 124.7f, -122.9f, 231.18f, 32);
    return gl_anchors_from_centres(-151, -120, 240, 36, 126, -122, 216, 32);
}
float om_row_center_y(const OptionsMenu *m, int k) {
    GlAnchors a = om_anchors(m->cfg.ctx, m->screen);
    return gl_row_top(&a, k) + a.th / 2;
}

/* ---- list engine (SPEC_GUI 2.5) ----------------------------------------------------------------- */

static void list_fit_window(OmList *l) {
    while (l->focus >= l->top + OM_VISIBLE_ROWS) l->top++;
    while (l->focus < l->top) l->top--;
}
void om_list_init(OmList *l, int n, const int *selectable, int initial_focus) {
    memset(l, 0, sizeof *l);
    if (n > OM_MAX_ROWS) n = OM_MAX_ROWS;
    if (n < 0) n = 0;
    l->n = n;
    for (int i = 0; i < n; i++) l->selectable[i] = selectable ? selectable[i] : 1;
    if (initial_focus < 0) initial_focus = 0;
    if (n > 0 && initial_focus > n - 1) initial_focus = n - 1;
    l->focus = initial_focus;
    if (n <= OM_VISIBLE_ROWS) l->top = 0;
    else l->top = initial_focus <= n - OM_VISIBLE_ROWS ? initial_focus : n - OM_VISIBLE_ROWS;
}
int om_first_selectable(const OmList *l) {
    for (int i = 0; i < l->n; i++) if (l->selectable[i]) return i;
    return 0;
}
int om_list_move(OmList *l, int dir) {
    if (l->n == 0 || dir == 0) return 0;
    int step = dir < 0 ? -1 : 1, last = step > 0 ? l->n - 1 : 0, k = l->focus;
    if (k == last) return 0;
    k += step;
    while (k != last && !l->selectable[k]) k += step;      /* stop at a selectable row or at the end */
    l->focus = k;
    list_fit_window(l);
    return 1;
}

/* ---- slider rule (SPEC_GUI 2.7) ------------------------------------------------------------------- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int om_slider_step(int *v, int min, int max, int dir) {
    int cur = clampi(*v, min, max), nv = cur;
    if (dir > 0) nv = cur + 1 > max ? max : cur + 1;
    else if (dir < 0) nv = cur - 1 < min ? min : cur - 1;
    if (nv == cur) return 0;
    *v = nv;
    return 1;
}
float om_slider_fill(int v, int min, int max) {
    if (max <= min) return 0.0f;
    return (float)(clampi(v, min, max) - min) / (float)(max - min);
}

/* ---- rows ----------------------------------------------------------------------------------------- */

void om_config_default(OmConfig *c, OmContext ctx) {
    memset(c, 0, sizeof *c);
    c->ctx = ctx;
    c->cpu_mhz = 3000;           /* CPU score 4, GPU score 5: SPEC 4.5 recommendation (P = 4) */
    c->gpu_score = 5.0;
}

static OmRow *add_row(OptionsMenu *m, OmRowKind kind, OmRowId id, uint32_t key, const char *label) {
    OmRow *r = &m->rows[m->nrows];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->id = id;
    r->slot = ++m->nrows;
    r->label_key = key;
    r->label = label;
    r->selectable = kind != OM_ROW_SPACER;
    r->gfx = -1;
    return r;
}

static void refresh_res_text(OptionsMenu *m) {
    int r = m->gfx->v[ST_GFX_RESOLUTION];
    if (r >= 0 && r <= 5) m->res_shown = r;                  /* outside 0..5 the text is left unchanged */
}

static void build_display(OptionsMenu *m) {
    OmRow *r = add_row(m, OM_ROW_CHOICE, OM_ID_RESOLUTION, 0xA9A76BBAu, "Resolution");
    r->gfx = ST_GFX_RESOLUTION; r->vt = OM_VT_RESOLUTION; r->needs_reinit = 1;
    r = add_row(m, OM_ROW_SLIDER, OM_ID_LEVEL, 0xB55E7665u, "Level Of Detail");
    r->gfx = ST_GFX_PERF_LEVEL; r->min = 1; r->max = 5; r->needs_reinit = 1;
    add_row(m, OM_ROW_BUTTON, OM_ID_CUSTOMIZE, OM_KEY_TITLE_CUSTOMIZE, "Customize Display Settings");
    add_row(m, OM_ROW_BUTTON, OM_ID_DEFAULTS, 0x13F0EDEBu, "Default Settings");
}

static void build_customize(OptionsMenu *m) {
    for (int i = 0; i < N_CUSTOMIZE; i++) {
        const CustRow *c = &k_customize[i];
        int max = 1;
        if (c->gfx != ST_GFX_VSYNC) {                       /* Vsync has no cap and is always present */
            int cap = m->caps ? m->caps->cap[c->gfx] : ST_NO_CAP;
            if (cap <= 0) continue;                         /* SPEC_GUI 4.1: zero cap, no row */
            if (c->kind == OM_ROW_SLIDER) {
                if (c->domain_max < 0) { if (cap == ST_NO_CAP) continue; max = cap; }
                else max = cap < c->domain_max ? cap : c->domain_max;
            }
        }
        OmRow *r = add_row(m, c->kind, OM_ID_OPTION, c->key, c->label);
        r->gfx = c->gfx; r->vt = c->vt; r->needs_reinit = c->reinit;
        if (c->kind == OM_ROW_SLIDER) { r->min = 0; r->max = max; }
        if (i == 0 && m->nrows == 1 && m->cfg.row1_label_has_caret)
            add_row(m, OM_ROW_SPACER, OM_ID_SPACER, 0, NULL);   /* SPEC_GUI 4.1: blank row after row 1 */
    }
}

static void build_hub(OptionsMenu *m) {
    m->hub_count = OM_HUB_ENTRIES + (m->controls_ok && m->bindings ? 1 : 0);
    int first = m->hub_remembered >= 1 && m->hub_remembered <= m->hub_count ? m->hub_remembered - 1 : 0;
    gc_init(&m->hub, GC_RAIL_HUB);
    int disabled[OM_HUB_MAX]={0};
    if(m->cfg.ctx==OM_CTX_FRONTEND && !m->cfg.profile)disabled[0]=disabled[2]=disabled[3]=1;
    gc_set_entries(&m->hub, m->hub_count, disabled);
    if(disabled[first])first=HUB_PAGE_DISPLAY;
    gc_snap_to(&m->hub, first);                             /* first build snaps (3.2) */
    m->hub_ptr.armed = 0;
}

/* [inferred] FE profile pages use typed settings fields; the existing pause path is unchanged. */
static float *audio_field(StProfile *p,int i) {
    float *fields[]={&p->audio.master,&p->audio.speech_movie,&p->audio.menu_music,&p->audio.game_music,&p->audio.sfx,&p->audio.engine,&p->audio.tire};
    return i>=0 && i<7?fields[i]:NULL;
}
static int profile_value(const OptionsMenu *m,int i) {
    StProfile *p=m->cfg.profile;if(!p)return 0;
    if(m->screen==OM_SCREEN_AUDIO)return i<7?(int)lroundf((isfinite(*audio_field(p,i))?fminf(1,fmaxf(0,*audio_field(p,i))):0)*100):p->audio.mode!=0;
    if(m->screen==OM_SCREEN_PLAYER) {
        StPlayer *v=&p->player[0];uint8_t *fields[]={&v->gauges,&v->race_position,&v->lap_info,&v->score,&v->feedback,&v->leaderboard,&v->ask_transmission,&v->transmission,&v->stability_control};
        return i<9?*fields[i]:(int)v->camera;
    }
    StGameplay *v=&p->gameplay;
    switch(i){case 0:return v->rearview_mirror;case 1:return v->speedo_units;case 2:return v->race_map_mode;case 3:return v->explore_map_mode;default:return v->jump_camera!=0;}
}
static void profile_set(OptionsMenu *m,int i,int value) {
    StProfile *p=m->cfg.profile;if(!p)return;
    if(m->screen==OM_SCREEN_AUDIO){if(i<7)*audio_field(p,i)=value/100.f;else p->audio.mode=(uint32_t)value;}
    else if(m->screen==OM_SCREEN_PLAYER) {
        StPlayer *v=&p->player[0];uint8_t *fields[]={&v->gauges,&v->race_position,&v->lap_info,&v->score,&v->feedback,&v->leaderboard,&v->ask_transmission,&v->transmission,&v->stability_control};
        if(i<9)*fields[i]=(uint8_t)value;else v->camera=(uint32_t)value;
    } else {StGameplay *v=&p->gameplay;switch(i){case 0:v->rearview_mirror=(uint8_t)value;break;case 1:v->speedo_units=(uint8_t)value;break;case 2:v->race_map_mode=(uint8_t)value;break;case 3:v->explore_map_mode=(uint8_t)value;break;default:v->jump_camera=value?127.f:0;break;}}
    m->profile_requested=1;
}
static void build_profile_page(OptionsMenu *m) {
    static const uint32_t audio_keys[]={0x26782c3e,0x9e5fb82a,0x418e681d,0xdf21eac2,0xfd487543,0xa2b1f888,0x69b745c6,0x2881ab87};
    static const char *audio[]={"Master Volume","Speech & Movie Volume","Menu Music Volume","Game Music Volume","Sound Effects Volume","Engine Volume","Tire Volume","Audio Mode"};
    static const uint32_t player_keys[]={0xac148579,0x82cd8f92,0x969f96a5,0xc03f9f19,0x6353f07e,0xc93fbfb5,0xbfb34120,0xd31407e7,0xdf06ed22,0xf6ccdc5f};
    static const char *player[]={"Gauges","Race Position","Lap Info","Score","Feedback","Leaderboard","Ask for Transmission","Transmission","Stability Control","Camera"};
    static const uint32_t gameplay_keys[]={0x85a6ce05,0xf86a6272,0x9fa5ec9e,0xc6269082,0xf26a5cbf};
    static const char *gameplay[]={"Rearview Mirror","Speedometer Units","Race Map Mode","Explore Map Mode","Jump Camera"};
    int n=m->screen==OM_SCREEN_AUDIO?8:m->screen==OM_SCREEN_PLAYER?10:5;
    for(int i=0;i<n;i++) {
        int audio_page=m->screen==OM_SCREEN_AUDIO,player_page=m->screen==OM_SCREEN_PLAYER;
        OmRow *r=add_row(m,audio_page && i<7?OM_ROW_SLIDER:OM_ROW_CHOICE,OM_ID_PROFILE,
            audio_page?audio_keys[i]:player_page?player_keys[i]:gameplay_keys[i],audio_page?audio[i]:player_page?player[i]:gameplay[i]);
        r->gfx=i;r->min=0;r->max=audio_page?(i<7?100:1):player_page?(i==9?4:1):((i==2 || i==3)?2:1);
        r->vt=OM_VT_ONOFF;
    }
}
static void enter_screen(OptionsMenu *m, OmScreen screen) {
    m->screen = screen;
    m->nrows = 0;
    memset(&m->dialog, 0, sizeof m->dialog);
    m->exiting = 0;
    m->exit_kind = OM_EXIT_NONE;
    if (screen == OM_SCREEN_DISPLAY) build_display(m);
    else if((screen==OM_SCREEN_AUDIO || screen==OM_SCREEN_PLAYER || screen==OM_SCREEN_GAMEPLAY) && m->cfg.profile)build_profile_page(m);
    else if(screen == OM_SCREEN_GAMEPLAY){
        OmRow *r=add_row(m,OM_ROW_CHOICE,OM_ID_MOUSE_LOOK,0,"Right mouse look");
        r->vt=OM_VT_ONOFF;
    }
    else if (screen == OM_SCREEN_CUSTOMIZE) build_customize(m);
    else if (screen == OM_SCREEN_HUB) build_hub(m);
    else if (screen == OM_SCREEN_CONTROLS) cm_open(&m->cm, m->bindings, &m->cm_plat);   /* always on the keyboard (D5) */
    int sel[OM_MAX_ROWS];
    for (int i = 0; i < m->nrows; i++) sel[i] = m->rows[i].selectable;
    om_list_init(&m->list, m->nrows, sel, 0);
    om_list_init(&m->list, m->nrows, sel, om_first_selectable(&m->list));   /* every screen opens on row 1 (SPEC_GUI_ART 4.7) */
    if (m->gfx) refresh_res_text(m);
    m->build_serial++;
}

static void begin(OptionsMenu *m, const OmConfig *cfg, StGraphics *gfx, const StGfxCaps *caps, StRegStore *store) {
    int apply = m->apply_requested, reinit = m->reinit_requested;   /* pending requests survive */
    int writes = m->registry_writes, status = m->last_write_status;
    int remembered = m->hub_remembered;
    CbBindings *bindings = m->bindings;
    CmPlatform plat = m->cm_plat;
    int controls_ok = m->controls_ok;
    ControlsMenu cm_keep;                                   /* only its serials survive */
    memset(&cm_keep, 0, sizeof cm_keep);
    cm_keep.build_serial = m->cm.build_serial; cm_keep.blink_serial = m->cm.blink_serial;
    cm_keep.scroll_serial = m->cm.scroll_serial; cm_keep.capture_serial = m->cm.capture_serial;
    unsigned build = m->build_serial, blink = m->blink_serial, exit = m->exit_serial, dlg = m->dialog_serial;
    memset(m, 0, sizeof *m);
    m->apply_requested = apply; m->reinit_requested = reinit;
    m->registry_writes = writes; m->last_write_status = status;
    m->hub_remembered = remembered;
    m->bindings = bindings; m->cm_plat = plat; m->controls_ok = controls_ok;
    m->cm = cm_keep;
    m->build_serial = build; m->blink_serial = blink; m->exit_serial = exit; m->dialog_serial = dlg;
    m->cfg = *cfg;
    m->gfx = gfx;
    m->caps = caps;
    m->store = store;
    m->res_shown = -1;
}

void om_open(OptionsMenu *m, const OmConfig *cfg, StGraphics *gfx, const StGfxCaps *caps,
             StRegStore *store, int remembered_focus) {
    (void)remembered_focus;      /* every path leaving the in-game Display resets the remembered row (SPEC_GUI_ART 4.7) */
    begin(m, cfg, gfx, caps, store);
    enter_screen(m, OM_SCREEN_DISPLAY);
}
void om_open_hub(OptionsMenu *m, const OmConfig *cfg, StGraphics *gfx, const StGfxCaps *caps,
                 StRegStore *store) {
    begin(m, cfg, gfx, caps, store);
    /* preserve the caller context; pause callers already pass OM_CTX_INGAME */
    enter_screen(m, OM_SCREEN_HUB);
}
void om_set_controls(OptionsMenu *m, CbBindings *b, const CmPlatform *plat, int available) {
    m->bindings = b;
    if (plat) m->cm_plat = *plat;
    m->controls_ok = available && b != NULL;
}
void om_close(OptionsMenu *m) {
    cm_close(&m->cm);
    m->screen = OM_SCREEN_CLOSED;
    m->nrows = 0;
    memset(&m->dialog, 0, sizeof m->dialog);
    m->exiting = 0;
    m->exit_kind = OM_EXIT_NONE;
    m->nevents = 0;
    m->build_serial++;
}
void om_reset_session(OptionsMenu *m) { m->hub_remembered = 0; }
int om_is_open(const OptionsMenu *m) { return m->screen != OM_SCREEN_CLOSED; }
OmScreen om_screen(const OptionsMenu *m) { return m->screen; }

static void push_event(OptionsMenu *m, OmEvent e) {
    if (m->nevents < (int)(sizeof m->events / sizeof m->events[0])) m->events[m->nevents++] = e;
}
OmEvent om_poll_event(OptionsMenu *m) {
    if (m->nevents == 0) return OM_EV_NONE;
    OmEvent e = m->events[0];
    for (int i = 1; i < m->nevents; i++) m->events[i - 1] = m->events[i];
    m->nevents--;
    return e;
}

/* ---- hook ------------------------------------------------------------------------------------------ */

int om_take_apply_request(OptionsMenu *m) { int v = m->apply_requested; m->apply_requested = 0; return v; }
int om_take_reinit_request(OptionsMenu *m) { int v = m->reinit_requested; m->reinit_requested = 0; return v; }
int om_apply_pending(const OptionsMenu *m) { return m->apply_requested; }
int om_reinit_pending(const OptionsMenu *m) { return m->reinit_requested; }

void om_notify_resolution_lowered(OptionsMenu *m) {
    /* SPEC_GUI 5.5: only the frontend Display list is rebuilt. */
    if (m->screen == OM_SCREEN_DISPLAY && m->cfg.ctx == OM_CTX_FRONTEND && !m->exiting)
        enter_screen(m, OM_SCREEN_DISPLAY);
}

/* ---- queries ----------------------------------------------------------------------------------------- */

int om_row_count(const OptionsMenu *m) { return m->nrows; }
const OmRow *om_row(const OptionsMenu *m, int i) { return i >= 0 && i < m->nrows ? &m->rows[i] : NULL; }
int om_focus(const OptionsMenu *m) { return m->list.focus; }
int om_top(const OptionsMenu *m) { return m->list.top; }
int om_row_visible(const OptionsMenu *m, int i) {
    return i >= m->list.top && i < m->list.top + OM_VISIBLE_ROWS && i < m->nrows;
}
int om_row_value(const OptionsMenu *m, int i) {
    const OmRow *r = om_row(m, i);
    if(r && r->id==OM_ID_PROFILE)return profile_value(m,r->gfx);
    if(r && r->id==OM_ID_MOUSE_LOOK)return m->cfg.mouse_look && *m->cfg.mouse_look!=0;
    if (!r || r->gfx < 0) return 0;
    int v = m->gfx->v[r->gfx];
    return r->kind == OM_ROW_SLIDER ? clampi(v, r->min, r->max) : v;
}
float om_row_fill(const OptionsMenu *m, int i) {
    const OmRow *r = om_row(m, i);
    return r && r->kind == OM_ROW_SLIDER ? om_slider_fill(om_row_value(m, i), r->min, r->max) : 0.0f;
}
const char *om_row_value_text(const OptionsMenu *m, int i, uint32_t *key) {
    const OmRow *r = om_row(m, i);
    uint32_t k = 0;
    const char *t = NULL;
    if (r) {
        int v = om_row_value(m, i);
        switch (r->vt) {
        case OM_VT_RESOLUTION: if (m->res_shown >= 0) t = k_resolution[m->res_shown]; break;
        case OM_VT_ONOFF:   k = v ? OM_KEY_ON : OM_KEY_OFF; t = v ? "On" : "Off"; break;
        case OM_VT_LOWHIGH: k = v ? OM_KEY_HIGH : OM_KEY_LOW; t = v ? "High" : "Low"; break;
        case OM_VT_VSYNC:   k = v ? OM_KEY_ON : OM_KEY_VSYNC_OFF; t = v ? "On" : "Off"; break;
        default: break;
        }
    }
    if(r && r->id==OM_ID_PROFILE) {
        int v=om_row_value(m,i);k=0;
        if(m->screen==OM_SCREEN_AUDIO && r->gfx==7){t=v?"Stereo":"Mono";k=v?0x55da8bf8:0xc50fa35f;}
        else if(m->screen==OM_SCREEN_PLAYER && r->gfx==9){static const char *c[]={"Bumper","Hood","Close","Far","Super Far"};int index=v>=0 && v<5?v:0;static const uint32_t keys[]={0xc3e9ae58,0x414f19d7,0x5ae3441f,0x1ea4cec2,0x916039b4};t=c[index];k=keys[index];}
        else if(m->screen==OM_SCREEN_PLAYER && r->gfx==7){t=v?"Manual":"Automatic";k=v?0x1b3a2485:0x61cb0720;}
        else if(m->screen==OM_SCREEN_PLAYER && r->gfx==8){t=v?"Off":"On";k=v?OM_KEY_OFF:OM_KEY_ON;}
        else if(m->screen==OM_SCREEN_GAMEPLAY && r->gfx==1){t=v?"KM/H":"MPH";k=v?0x8569a25f:0x8569ab44;}
        else if(m->screen==OM_SCREEN_GAMEPLAY && (r->gfx==2 || r->gfx==3)){static const char *c[]={"Off","Static","Rotate"};int index=v>=0 && v<3?v:0;static const uint32_t keys[]={OM_KEY_OFF,0xf75595f2,0xf4b00e99};t=c[index];k=keys[index];}
    }
    if (key) *key = k;
    return t;
}
int om_scrollbar_visible(const OptionsMenu *m) { return gl_scroll_visible(m->nrows); }
float om_scroll_tab(const OptionsMenu *m) {
    return m->nrows > 1 ? (float)m->list.focus / (float)(m->nrows - 1) : 0.0f;
}
const char *om_header(const OmContext ctx, OmScreen screen, int which, uint32_t *key) {
    uint32_t k = 0;
    const char *t = NULL;
    if (screen == OM_SCREEN_CUSTOMIZE && which == 0) { k = OM_KEY_TITLE_CUSTOMIZE; t = "Customize Display Settings"; }
    else if (screen == OM_SCREEN_HUB && which == 0) { k = ctx==OM_CTX_INGAME?OM_KEY_TITLE_PAUSE:0xc7ca52ed; t = ctx==OM_CTX_INGAME?"PAUSE OPTIONS":"Options"; }
    else if ((screen==OM_SCREEN_AUDIO || screen==OM_SCREEN_PLAYER) && which==0){k=screen==OM_SCREEN_AUDIO?0xe76cd783:0xf760eabe;t=screen==OM_SCREEN_AUDIO?"Audio":"Player";}
    else if (screen == OM_SCREEN_CONTROLS && which == 0) k = CM_KEY_HEADER;        /* text: language file only */
    else if(screen == OM_SCREEN_GAMEPLAY){
        if(which==0){k=ctx==OM_CTX_INGAME?OM_KEY_TITLE_PAUSE:0xd0cf6ee1;t=ctx==OM_CTX_INGAME?"PAUSE OPTIONS":"Gameplay";}
        else if(which==1){k=0xD0CF6EE1u;t="Gameplay";}
    }
    else if (screen == OM_SCREEN_DISPLAY) {
        if (ctx == OM_CTX_INGAME) {
            if (which == 0) { k = OM_KEY_TITLE_PAUSE; t = "PAUSE OPTIONS"; }
            else if (which == 1) { k = OM_KEY_TITLE_PAUSE_DISPLAY; t = "DISPLAY"; }
        } else if (which == 0) { k = OM_KEY_TITLE_DISPLAY; t = "Display"; }
    }
    if (key) *key = k;
    return t;
}
int om_dialog_is_open(const OptionsMenu *m) { return m->dialog.open; }
int om_dialog_focus(const OptionsMenu *m) { return m->dialog.focus; }
uint32_t om_dialog_button_key(int i) { return i == 0 ? OM_KEY_YES : OM_KEY_NO; }
const OmHubEntry *om_hub_entry(int i) { return i >= 0 && i < OM_HUB_ENTRIES ? &k_hub[i] : NULL; }
int om_hub_count(const OptionsMenu *m) { return m->hub_count ? m->hub_count : OM_HUB_ENTRIES; }
const OmHubEntry *om_hub_entry_of(const OptionsMenu *m, int i) {
    if (i == OM_HUB_ENTRIES && om_hub_count(m) > OM_HUB_ENTRIES) return &k_hub_controls;
    return om_hub_entry(i);
}
int om_hub_focus(const OptionsMenu *m) { return m->hub.cur; }
int om_hub_remembered(const OptionsMenu *m) { return m->hub_remembered; }

/* ---- input ----------------------------------------------------------------------------------------------- */

static void changed(OptionsMenu *m, const OmRow *r) {
    m->apply_requested = 1;
    if (r->needs_reinit) m->reinit_requested = 1;
    refresh_res_text(m);
}

static void blink(OptionsMenu *m, int dir) { m->blink_dir = dir; m->blink_serial++; }

static void begin_exit(OptionsMenu *m, OmExit kind) {
    if (m->exiting) return;                                 /* a second Back while exiting is ignored */
    m->exiting = 1;
    m->exit_kind = kind;
    m->exit_left_ms = (float)om_exit_ms(m->cfg.ctx, m->screen);
    m->exit_serial++;
}

static void finish_exit(OptionsMenu *m);

static void write_registry(OptionsMenu *m) {
    if (!m->store) return;
    m->last_write_status = st_gfx_save(m->store, m->gfx);
    m->registry_writes++;
}

static void finish_exit(OptionsMenu *m) {
    OmExit kind = m->exit_kind;
    m->exiting = 0;
    m->exit_kind = OM_EXIT_NONE;
    switch (kind) {
    case OM_EXIT_TO_DISPLAY: enter_screen(m, OM_SCREEN_DISPLAY); break;     /* fresh build, focus on row 1 */
    case OM_EXIT_TO_CUSTOMIZE: enter_screen(m, OM_SCREEN_CUSTOMIZE); break;
    case OM_EXIT_TO_CONTROLS: enter_screen(m, OM_SCREEN_CONTROLS); break;
    case OM_EXIT_TO_AUDIO: enter_screen(m,OM_SCREEN_AUDIO);break;
    case OM_EXIT_TO_PLAYER: enter_screen(m,OM_SCREEN_PLAYER);break;
    case OM_EXIT_TO_GAMEPLAY: enter_screen(m, OM_SCREEN_GAMEPLAY); break;
    case OM_EXIT_LEAVE_GAMEPLAY: enter_screen(m, OM_SCREEN_HUB); break;
    case OM_EXIT_LEAVE_DISPLAY:                              /* SPEC_GUI 5.1: write, then the hub */
        write_registry(m);
        if (m->cfg.ctx == OM_CTX_INGAME) enter_screen(m, OM_SCREEN_HUB);   /* UI_PauseOptionsMain.fng */
        else om_close(m);
        push_event(m, OM_EV_LEAVE_TO_HUB);
        break;
    case OM_EXIT_TO_PAUSE:                                   /* hub Back: forget the remembered entry */
        m->hub_remembered = 0;
        om_close(m);
        push_event(m, OM_EV_LEAVE_TO_PAUSE);
        break;
    case OM_EXIT_RESUME:                                     /* Start: nothing is written (SPEC_GUI 5.2) */
        m->hub_remembered = 0;
        om_close(m);
        push_event(m, OM_EV_RESUME_GAME);
        break;
    default: break;
    }
}

static void row_adjust(OptionsMenu *m, int dir) {
    const OmRow *r = &m->rows[m->list.focus];
    StGraphics *g = m->gfx;
    if (r->kind == OM_ROW_CHOICE || r->kind == OM_ROW_SLIDER) blink(m, dir);   /* the arrow blinks on any press */
    if(r->id==OM_ID_PROFILE) {
        int value=profile_value(m,r->gfx),step=r->kind==OM_ROW_SLIDER?5:1;
        value+=dir*step;
        if(r->kind==OM_ROW_CHOICE){if(value<r->min)value=r->max;if(value>r->max)value=r->min;}
        else {if(value<r->min)value=r->min;if(value>r->max)value=r->max;}
        profile_set(m,r->gfx,value);return;
    }
    if(r->id==OM_ID_MOUSE_LOOK){
        if(m->cfg.mouse_look)*m->cfg.mouse_look=!*m->cfg.mouse_look;
    }else if (r->kind == OM_ROW_CHOICE && r->id == OM_ID_RESOLUTION) {
        int res = g->v[ST_GFX_RESOLUTION];
        if (res >= 0 && res <= 5 && m->caps && st_resolution_step(g, m->caps, dir)) changed(m, r);
    } else if (r->kind == OM_ROW_CHOICE) {
        g->v[r->gfx] = g->v[r->gfx] ? 0 : 1;               /* SPEC 4.4: flip 0 <-> 1 */
        changed(m, r);
    } else if (r->kind == OM_ROW_SLIDER) {
        int v = g->v[r->gfx];
        if (!om_slider_step(&v, r->min, r->max, dir)) return;
        if (r->id == OM_ID_LEVEL && m->caps) st_gfx_set_level(g, m->caps, v, m->cfg.gpu_score);
        else g->v[r->gfx] = v;
        changed(m, r);
    }
}

static void open_defaults_dialog(OptionsMenu *m) {
    GdSpec spec;
    gd_spec(&spec, GD_DEFAULTS, NULL);
    gd_open(&m->dialog, &spec);                              /* focus on the second button (SPEC_GUI_ART 6) */
    m->dialog_serial++;
}

static void row_accept(OptionsMenu *m) {
    const OmRow *r = &m->rows[m->list.focus];
    if(r->id==OM_ID_MOUSE_LOOK || r->id==OM_ID_PROFILE)row_adjust(m,1);
    else if (r->kind == OM_ROW_CHOICE && r->id == OM_ID_OPTION) {
        m->gfx->v[r->gfx] = m->gfx->v[r->gfx] ? 0 : 1;
        changed(m, r);
    } else if (r->id == OM_ID_CUSTOMIZE) {
        begin_exit(m, OM_EXIT_TO_CUSTOMIZE);
    } else if (r->id == OM_ID_DEFAULTS) {
        open_defaults_dialog(m);
    }
}

static void defaults_yes(OptionsMenu *m) {
    int level = st_perf_level(m->cfg.cpu_mhz, m->cfg.gpu_score);
    if (m->caps) st_gfx_autodetect(m->gfx, m->caps, level, m->cfg.gpu_score);
    m->apply_requested = 1;
    m->reinit_requested = 1;
    enter_screen(m, OM_SCREEN_DISPLAY);                      /* SPEC_GUI 5.6: rebuild, focus row 1 */
}

static GdInput dialog_input_of(OmInput in) {
    switch (in) {
    case OM_IN_UP: return GD_IN_UP;
    case OM_IN_DOWN: return GD_IN_DOWN;
    case OM_IN_LEFT: return GD_IN_LEFT;
    case OM_IN_RIGHT: return GD_IN_RIGHT;
    case OM_IN_ACCEPT: return GD_IN_ACCEPT;
    case OM_IN_BACK: return GD_IN_BACK;
    case OM_IN_START: return GD_IN_START;
    default: return GD_IN_NONE;
    }
}

static void hub_activate(OptionsMenu *m) {
    const OmHubEntry *e = om_hub_entry_of(m, m->hub.cur);
    int page = e ? e->page : -1;
    m->page = page;
    if (page == HUB_PAGE_DISPLAY) begin_exit(m, OM_EXIT_TO_DISPLAY);
    else if(page==0 && m->cfg.profile)begin_exit(m,OM_EXIT_TO_AUDIO);
    else if(page==3 && m->cfg.profile)begin_exit(m,OM_EXIT_TO_PLAYER);
    else if(page==2 && (m->cfg.mouse_look || m->cfg.profile))begin_exit(m,OM_EXIT_TO_GAMEPLAY);
    else if (page == OM_HUB_PAGE_CONTROLS) begin_exit(m, OM_EXIT_TO_CONTROLS);
    /* Optional profile pages are unavailable to legacy callers without a profile. */
}

static void hub_input(OptionsMenu *m, OmInput in) {
    switch (in) {
    case OM_IN_LEFT: case OM_IN_RIGHT:
        if (gc_step(&m->hub, in == OM_IN_LEFT ? -1 : 1)) {
            m->hub_remembered = m->hub.cur + 1;              /* updated on every Left/Right move (SPEC_GUI_ART 2.5) */
            blink(m, in == OM_IN_LEFT ? -1 : 1);
        }
        break;
    case OM_IN_ACCEPT: hub_activate(m); break;
    case OM_IN_BACK: begin_exit(m, OM_EXIT_TO_PAUSE); break;
    case OM_IN_START: begin_exit(m, OM_EXIT_RESUME); break;
    default: break;                                          /* Up/Down are ignored */
    }
}

/* ---- Controls (CONTROLS_SPEC 4.3, 4.6, 4.7) ------------------------------------------------------------ */

static CmInput cm_input_of(OmInput in) {
    switch (in) {
    case OM_IN_UP: return CM_IN_UP;
    case OM_IN_DOWN: return CM_IN_DOWN;
    case OM_IN_LEFT: return CM_IN_LEFT;
    case OM_IN_RIGHT: return CM_IN_RIGHT;
    case OM_IN_ACCEPT: return CM_IN_ACCEPT;
    case OM_IN_BACK: return CM_IN_BACK;
    case OM_IN_RESET: return CM_IN_RESET;
    default: return CM_IN_NONE;                              /* Start does nothing here */
    }
}

/* Acts on what the screen asked for: the reset dialog, or leaving (immediately: the screen's exit script posts
 * exit-complete at 0 ms, 3.9). Leaving with changed bindings tells the host to persist them. */
static void controls_pump(OptionsMenu *m) {
    if (m->screen != OM_SCREEN_CONTROLS) return;
    if (cm_take_reset(&m->cm)) {
        GdSpec spec;
        gd_spec(&spec, GD_CONTROLS_RESET, NULL);
        gd_open(&m->dialog, &spec);
        m->dialog_serial++;
    }
    if (cm_take_leave(&m->cm)) {
        int changed = cm_take_modified(&m->cm);
        enter_screen(m, OM_SCREEN_HUB);                      /* remembered entry: Controls */
        if (changed) push_event(m, OM_EV_CONTROLS_CHANGED);
    }
}

void om_key_down(OptionsMenu *m, int scancode) {
    if (!om_is_open(m) || m->screen != OM_SCREEN_CONTROLS) return;
    cm_key_down(&m->cm, scancode);
}
void om_pad_frame(OptionsMenu *m, const CbPadSample *s) {
    if (!om_is_open(m) || m->screen != OM_SCREEN_CONTROLS) return;
    cm_pad_frame(&m->cm, s);
}
void om_pointer_move(OptionsMenu *m, float x, float y) {
    if (!om_is_open(m) || m->screen != OM_SCREEN_CONTROLS || m->dialog.open) return;
    cm_pointer_move(&m->cm, x, y);
}
int om_controls_capturing(const OptionsMenu *m) {
    return om_is_open(m) && m->screen == OM_SCREEN_CONTROLS && m->cm.capturing;
}

void om_input(OptionsMenu *m, OmInput in) {
    if (!om_is_open(m) || m->exiting) return;
    if (m->dialog.open) { gd_input(&m->dialog, dialog_input_of(in)); return; }
    if (m->screen == OM_SCREEN_CONTROLS) { cm_input(&m->cm, cm_input_of(in)); controls_pump(m); return; }
    if (m->screen == OM_SCREEN_HUB) { hub_input(m, in); return; }
    if (m->nrows == 0) return;
    switch (in) {
    case OM_IN_UP:    om_list_move(&m->list, -1); break;
    case OM_IN_DOWN:  om_list_move(&m->list, 1); break;
    case OM_IN_LEFT:  row_adjust(m, -1); break;
    case OM_IN_RIGHT: row_adjust(m, 1); break;
    case OM_IN_ACCEPT: row_accept(m); break;
    case OM_IN_BACK:
        begin_exit(m,(m->screen==OM_SCREEN_GAMEPLAY || m->screen==OM_SCREEN_AUDIO || m->screen==OM_SCREEN_PLAYER)?OM_EXIT_LEAVE_GAMEPLAY:
                     m->screen == OM_SCREEN_DISPLAY ? OM_EXIT_LEAVE_DISPLAY : OM_EXIT_TO_DISPLAY);
        break;
    case OM_IN_START:                                       /* SPEC_GUI 5.2: in-game Display only, no write */
        if (m->cfg.ctx == OM_CTX_INGAME && (m->screen == OM_SCREEN_DISPLAY || m->screen==OM_SCREEN_GAMEPLAY)) begin_exit(m, OM_EXIT_RESUME);
        break;
    default: break;
    }
}

static int in_rect(float x, float y, float x0, float y0, float w, float h) {
    return x >= x0 && x <= x0 + w && y >= y0 && y <= y0 + h;    /* edges inclusive (SPEC_GUI_ART 4.6) */
}

void om_pointer_click(OptionsMenu *m, float x, float y) {
    if (!om_is_open(m) || m->exiting) return;
    if (m->dialog.open) { gd_pointer_click(&m->dialog, x, y); return; }   /* the dialog is modal */
    if (m->screen == OM_SCREEN_CONTROLS) { cm_pointer_click(&m->cm, x, y); controls_pump(m); return; }
    if (m->screen == OM_SCREEN_HUB) {
        GcPtAction a = gc_pointer_press(&m->hub, &m->hub_ptr, x, y);
        if (a == GC_PT_LEFT) hub_input(m, OM_IN_LEFT);
        else if (a == GC_PT_RIGHT) hub_input(m, OM_IN_RIGHT);
        else if (gc_pointer_release(&m->hub, &m->hub_ptr, x, y) == GC_PT_ACTIVATE) hub_activate(m);
        return;
    }
    GlAnchors a = om_anchors(m->cfg.ctx, m->screen);
    for (int k = 0; k < OM_VISIBLE_ROWS; k++) {
        int i = m->list.top + k;
        if (i >= m->nrows) break;
        const OmRow *r = &m->rows[i];
        float ry = gl_row_top(&a, k);
        if (r->kind == OM_ROW_SPACER) continue;
        if (r->kind == OM_ROW_BUTTON) {                      /* a hit on the name object activates it (4.6) */
            if (in_rect(x, y, a.tx, ry, a.tw, a.th)) { m->list.focus = i; row_accept(m); return; }
        } else if (i == m->list.focus) {                     /* arrows of the focused row; left tested first */
            GlPt l = gl_left_arrow_tl(&a, k, OM_ARROW_SIZE), rt = gl_right_arrow_tl(&a, k);
            if (in_rect(x, y, l.x, l.y, OM_ARROW_SIZE, OM_ARROW_SIZE)) { row_adjust(m, -1); return; }
            if (in_rect(x, y, rt.x, rt.y, OM_ARROW_SIZE, OM_ARROW_SIZE)) { row_adjust(m, 1); return; }
            if (r->kind == OM_ROW_CHOICE && in_rect(x, y, a.dx, gl_data_top(&a, k), a.dw, a.dh)) { row_adjust(m, 1); return; }
        }
    }
}

void om_update(OptionsMenu *m, float seconds) {
    if (!om_is_open(m)) return;
    if (m->screen == OM_SCREEN_HUB) gc_update(&m->hub, seconds);
    if (m->screen == OM_SCREEN_CONTROLS) cm_tick(&m->cm);
    if (m->dialog.open) {
        gd_update(&m->dialog, seconds);
        GdResult res = gd_poll(&m->dialog);
        if (res != GD_RES_NONE) {
            memset(&m->dialog, 0, sizeof m->dialog);
            m->dialog_serial++;                              /* the screen undims */
            if (res == GD_RES_FIRST) {
                if (m->screen == OM_SCREEN_CONTROLS) cm_reset_device(&m->cm);   /* the displayed device (D5) */
                else defaults_yes(m);
            }
        }
    }
    if (!m->exiting) return;
    m->exit_left_ms -= seconds > 0 ? seconds * 1000.0f : 0.0f;
    if (m->exit_left_ms <= 0) finish_exit(m);
}

int om_take_profile_request(OptionsMenu *m){int changed=m->profile_requested;m->profile_requested=0;return changed;}
