/* gui_view.c -- see gui_view.h. Rules: SPEC_GUI_ART 1.2 (property writes), 1.3 (visibility), 1.4
 * (handler, exit, dim), 3 (carousel), 4 (rows, sliders, scrollbar), 5 (headers), 6 (dialogs), 8. */
#include "gui_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui_timing.h"

#define H_HIGHLIGHT 0x249DB7B7u         /* script names (case-insensitive hashes) */
#define H_UNHIGHLIGHT 0x7AB5521Au
#define H_BLINK 0x0280164Fu
#define H_STATIC 0x7AB70D67u
#define H_ARROW_TEX 0x9943D584u         /* U2_MENU_ARROW_WHITE, SPEC_GUI 2.1 */
#define H_HEADER 0x42ADB44Cu            /* Header_Text / Header_text / HEADER_TEXT */
#define GV_ENTER_FALLBACK_MS 600.0f     /* enter-complete assumed after this long when a package never reports it */
#define HL_FADE_MS 150.0f               /* UnHighlight fades arrows and backing out within 100 ms (SPEC_GUI 2.4) */

static uint32_t hn(const char *s) { return ug_hash_upper(s); }
static uint32_t hnf(const char *fmt, int n) {
    char b[64];
    snprintf(b, sizeof b, fmt, n);
    return ug_hash_upper(b);
}

/* ---- helpers --------------------------------------------------------------------------------- */

int gv_texture(const UgAssets *a, uint32_t key) {
    if (!a) return 0;
    int i = ug_texlib_find(&a->tex, key);
    return i >= 0 && ug_texlib_pixels((UgTexLib *)&a->tex, i) ? i + 1 : 0;
}
const char *gv_text(const UgAssets *a, uint32_t key, const char *fallback) {
    const char *t = a && a->have_strings ? ug_strtab_get(&a->strings, key) : NULL;
    return t ? t : fallback;
}

/* ---- screen ----------------------------------------------------------------------------------- */

static void set_status(GvScreen *s, const char *fmt, const char *what) {
    snprintf(s->status, sizeof s->status, fmt, what);
}

/* AMEND1 A6: an anchor object is sampled once, here, at screen creation. At this point the scene holds
 * the authored values: no update has run, so the second script the master's Init is chained to (which
 * hides and moves it) has not been applied. The stored centre and size are used; a master that sits in a
 * group (none does in the covered packages) is taken at its composed centre. */
static void sample_anchor(const UgScene *sc, uint32_t name_hash, GvAnchor *a) {
    memset(a, 0, sizeof *a);
    int i = ug_scene_find_name(sc, name_hash);
    if (i < 0) return;
    const UgNode *n = &sc->nodes[i];
    a->found = 1;
    a->x = n->parent < 0 ? n->obj->pos[0] : n->we;
    a->y = n->parent < 0 ? n->obj->pos[1] : n->wf;
    a->w = n->obj->size[0];
    a->h = n->obj->size[1];
}

static int screen_start(GvScreen *s, const UgFngPackage *pk, const UgAssets *assets, const char *name) {
    s->pk = pk;
    s->assets = assets;
    snprintf(s->name, sizeof s->name, "%s", name ? name : "");
    if (!ug_scene_init(&s->scene, pk, assets && assets->have_strings ? &assets->strings : NULL)) {
        set_status(s, "%s: scene allocation failed", s->name);
        return 0;
    }
    s->loaded = 1;
    ug_scene_set_assets(&s->scene, assets);
    s->scene.pc_build = 1;              /* console-only objects are not drawn on PC (SPEC_GUI 3.3) */
    s->scene.hide_hint_text = 1;
    s->scene.console_strip = 0;
    s->handler = ug_scene_find_name(&s->scene, GT_HANDLER_HASH);
    if (s->handler < 0) s->handler = -1;
    sample_anchor(&s->scene, hn("OPTION_MASTER"), &s->opt_master);       /* before the first update (A6) */
    sample_anchor(&s->scene, hn("TITLE_MASTER"), &s->title_master);
    sample_anchor(&s->scene, hn("DATA_MASTER"), &s->data_master);
    sample_anchor(&s->scene, hn("DATA_MASTER2"), &s->data_master2);
    ug_scene_update(&s->scene, 0);
    s->draw_ok = 1;
    return 1;
}

int gv_screen_load(GvScreen *s, const UgAssets *assets, const char *package_name) {
    memset(s, 0, sizeof *s);
    s->handler = -1;
    snprintf(s->name, sizeof s->name, "%s", package_name ? package_name : "");
    if (!assets) { set_status(s, "%s: no resources", s->name); return 0; }
    const UgAssetPackage *pk = ug_assets_package(assets, package_name);
    if (!pk) { set_status(s, "%s: package not found in the loaded resources", s->name); return 0; }
    char err[120] = "";
    if (!ug_fng_parse(pk->data, pk->len, &s->pkg, err, sizeof err)) {
        snprintf(s->status, sizeof s->status, "%s: package unreadable (%s)", s->name, err);
        return 0;
    }
    s->own_pkg = 1;
    if (!screen_start(s, &s->pkg, assets, package_name)) {
        ug_fng_free(&s->pkg);
        s->own_pkg = 0;
        return 0;
    }
    return 1;
}

int gv_screen_attach(GvScreen *s, const UgFngPackage *pk, const UgAssets *assets, const char *package_name) {
    memset(s, 0, sizeof *s);
    s->handler = -1;
    return screen_start(s, pk, assets, package_name);
}

void gv_screen_free(GvScreen *s) {
    if (s->loaded) ug_scene_free(&s->scene);
    if (s->own_pkg) ug_fng_free(&s->pkg);
    memset(s, 0, sizeof *s);
    s->handler = -1;
}

void gv_screen_update(GvScreen *s, float dt_ms) {
    if (!s->loaded) return;
    s->time_ms += dt_ms > 0 ? dt_ms : 0;
    ug_scene_update(&s->scene, dt_ms);
    uint32_t msg;
    while (ug_scene_poll_app(&s->scene, &msg))
        if (msg == GT_MSG_INIT_COMPLETE || msg == GT_MSG_ENTER_PAUSE) {
            if (!s->enter_done) ug_scene_refocus(&s->scene);   /* the focus response is not honoured during the fade-in (SPEC 3.6.2) */
            s->enter_done = 1;
        }
        else if (msg == GT_MSG_EXIT_COMPLETE) s->exit_done = 1;
}

void gv_screen_exit(GvScreen *s) {
    if (!s->loaded || s->exit_started) return;
    s->exit_started = 1;
    const GtPackage *g = gt_find(s->name);
    uint32_t script = g ? gt_exit_script_hash(g) : hn("FORWARD");
    if (s->handler >= 0) ug_scene_start_script(&s->scene, s->handler, script);
}

void gv_screen_dim(GvScreen *s, int dim) {
    if (!s->loaded || s->handler < 0) return;
    ug_scene_start_script(&s->scene, s->handler, dim ? GT_SCRIPT_DIM : GT_SCRIPT_UNDIM);
}

void gv_screen_dialog(GvScreen *s, int open) {
    if (!s->loaded || s->handler < 0) return;
    if (!open) { gv_screen_dim(s, 0); return; }
    const GtPackage *g = gt_find(s->name);
    uint32_t script = g ? gt_exit_script_hash(g) : hn("FORWARD");
    if (ug_scene_script_id(&s->scene, s->handler) == script) return;   /* exit script already running: not restarted (B2) */
    gv_screen_dim(s, 1);                                     /* "start dim": DIM becomes the handler's script ... */
    ug_scene_start_script(&s->scene, s->handler, script);    /* ... then the exit script replaces it (B2 step 2) */
}

int gv_screen_ready(const GvScreen *s) {
    if (!s->loaded) return 0;
    for (int i = 0; i < s->scene.n; i++) {                   /* every shown text object needs its font */
        const UgNode *n = &s->scene.nodes[i];
        if (n->visible && n->obj->type == UG_OBJ_TEXT && n->wcolor.a && !n->face) return 0;
    }
    for (int i = 0; i < s->scene.n; i++) {                   /* every shown image its texture (or an explicit flat quad) */
        const UgNode *n = &s->scene.nodes[i];
        if (!n->visible || !n->wcolor.a) continue;
        if ((n->obj->type == UG_OBJ_IMAGE || n->obj->type == UG_OBJ_SIMPLEIMAGE) && n->tex_override <= 0 && !n->untextured && n->tex == 0)
            return 0;
    }
    return 1;
}

int gv_screen_draw(GvScreen *s, UgCtx *ctx, UgCanvas *cv) {
    if (!s->loaded || !s->draw_ok) return 0;
    int mark = cv->n;
    UgVisualStatus before = cv->visual_status;
    cv->visual_status = UG_VIS_OK;
    ug_scene_draw(&s->scene, ctx, cv);
    if (ug_cv_visual_status(cv) != UG_VIS_OK) {
        cv->n = mark;                                        /* drop this screen's geometry only */
        s->draw_ok = 0;
        snprintf(s->status, sizeof s->status, "%s: authored resources could not be drawn", s->name);
        cv->visual_status = before;
        return 0;
    }
    cv->visual_status = before;
    return 1;
}

/* ---- node helpers ------------------------------------------------------------------------------- */

static void hide(UgScene *sc, int node, int on) { if (node >= 0) sc->nodes[node].app_hidden = on; }
static void settext(UgScene *sc, int node, const char *t) { if (node >= 0) sc->nodes[node].text_override = t; }
static void start(UgScene *sc, int node, uint32_t script) { if (node >= 0) ug_scene_start_script(sc, node, script); }
static float auth_w(const UgScene *sc, int node) { return node >= 0 ? sc->nodes[node].obj->size[0] : 0; }
static float auth_h(const UgScene *sc, int node) { return node >= 0 ? sc->nodes[node].obj->size[1] : 0; }
/* Places an image by its top-left corner ("set top-left" form): centre = top-left + size/2. */
static void place_tl(UgScene *sc, int node, float x, float y, float w, float h) {
    if (node < 0) return;
    ug_scene_app_set_size(sc, node, w, h);
    ug_scene_app_set_world_pos(sc, node, x + w / 2, y + h / 2);
}
/* Finds a unique node by name hash; -1 absent, -2 ambiguous (recorded in *bad). */
static int find(GvScreen *s, uint32_t hash, int *bad) {
    int i = ug_scene_find_name(&s->scene, hash);
    if (i == -2 && bad) *bad = 1;
    return i < 0 ? -1 : i;
}

/* ---- carousel -------------------------------------------------------------------------------------- */

int gv_carousel_bind(GvCarousel *v, GvScreen *s, GuiCarousel *car) {
    memset(v, 0, sizeof *v);
    v->scr = s;
    if (!s->loaded) return 0;
    int bad = 0;
    v->master = find(s, hn("OPTION_MASTER"), &bad);
    v->title = find(s, hn("ICON_TITLE"), &bad);
    v->header = find(s, H_HEADER, &bad);
    v->back_button = find(s, hn("Back_button"), &bad);
    v->select_button = find(s, hn("Select_button"), &bad);
    v->rail = find(s, hn("ScrollBar_Backing"), &bad);
    v->tab = find(s, hn("ScrollBar_Handle"), &bad);
    v->arrow_l = find(s, hn("ScrollBar_Arrow_1"), &bad);
    v->arrow_r = find(s, hn("ScrollBar_Arrow_2"), &bad);
    for (int k = 1; k <= GC_MAX; k++) {
        int n = find(s, hnf("OPTION_%d", k), &bad);
        if (n < 0) break;
        v->slot[v->nslots++] = n;
    }
    const char *missing = bad ? "an ambiguous object name" : v->master < 0 ? "OPTION_MASTER" : v->title < 0 ? "ICON_TITLE" :
                          v->rail < 0 ? "ScrollBar_Backing" : v->tab < 0 ? "ScrollBar_Handle" :
                          v->arrow_l < 0 ? "ScrollBar_Arrow_1" : v->arrow_r < 0 ? "ScrollBar_Arrow_2" :
                          v->nslots < 1 ? "OPTION_1" : NULL;
    if (missing) {
        snprintf(s->status, sizeof s->status, "%s: authored carousel binding unavailable (%s)", s->name, missing);
        return 0;
    }
    if (!s->opt_master.found) {
        snprintf(s->status, sizeof s->status, "%s: authored carousel binding unavailable (OPTION_MASTER anchor)", s->name);
        return 0;
    }
    gc_set_master(car, s->opt_master.x, s->opt_master.y);     /* cached at creation, A6 */
    hide(&s->scene, v->master, 1);                           /* never shown: only an anchor */
    settext(&s->scene, v->back_button, gv_text(s->assets, 0x933DD695u, "Back"));        /* console-only group: not drawn on PC */
    settext(&s->scene, v->select_button, gv_text(s->assets, 0x8202C364u, "Select"));
    for (int i = 0; i < GC_MAX; i++) v->last_alpha[i] = -1;
    v->seen_move = 0;
    v->bound = 1;
    return 1;
}

static void set_color(UgScene *sc, int node, int alpha) {
    float c[4] = { 255, 255, 255, (float)alpha };
    ug_scene_app_write(sc, node, UG_P_COLOR, 4, c);          /* base colour: authored fades scale from it (1.2) */
}

void gv_carousel_apply(GvCarousel *v, const GuiCarousel *car, const GvEntry *entries, int n,
                       const char *header, unsigned move_serial, int move_dir) {
    if (!v->bound) return;
    UgScene *sc = &v->scr->scene;
    int shown = n < v->nslots ? n : v->nslots;
    for (int i = 0; i < shown && i < car->n; i++) {
        GcSlot g = gc_slot(car, i);
        int node = v->slot[i];
        UgNode *nd = &sc->nodes[node];
        nd->tex_override = entries ? entries[i].tex : 0;
        hide(sc, node, !g.visible || nd->tex_override <= 0);   /* no art: omitted, never replaced */
        ug_scene_app_set_size(sc, node, g.w, g.h);
        ug_scene_app_set_world_pos(sc, node, g.cx, g.cy);
        int a = gc_alpha(car, i);
        if (a != v->last_alpha[i]) { set_color(sc, node, a); v->last_alpha[i] = a; }
    }
    settext(sc, v->title, entries && car->cur >= 0 && car->cur < n ? entries[car->cur].label : "");
    if (header) settext(sc, v->header, header);
    /* rail, tab and arrows: hidden when N <= 1; the arrows follow the focused position */
    int rail = gc_tab_visible(car);
    hide(sc, v->rail, !rail);
    hide(sc, v->tab, !rail);
    hide(sc, v->arrow_l, !gc_left_arrow_visible(car));
    hide(sc, v->arrow_r, !gc_right_arrow_visible(car));
    if (rail) ug_scene_app_set_world_pos(sc, v->tab, gc_tab_x(car), sc->nodes[v->tab].wf);
    if (car->cur >= 0 && car->cur < v->nslots && sc->focus != v->slot[car->cur]) ug_scene_set_focus(sc, v->slot[car->cur]);
    /* the arrow of the move direction blinks (unless already blinking), the other one idles (3.2) */
    if (move_serial != v->seen_move) {
        v->seen_move = move_serial;
        int moved = move_dir > 0 ? v->arrow_r : v->arrow_l, other = move_dir > 0 ? v->arrow_l : v->arrow_r;
        int visible = move_dir > 0 ? gc_right_arrow_visible(car) : gc_left_arrow_visible(car);
        if (visible && ug_scene_script_id(sc, moved) != H_BLINK) start(sc, moved, H_BLINK);
        if (ug_scene_script_id(sc, other) != H_STATIC) start(sc, other, H_STATIC);
    }
    ug_scene_relayout(sc);
}

/* ---- option list ------------------------------------------------------------------------------------ */

static const char *const k_hidden_names[] = {            /* SPEC_GUI 3.2, 4.2: objects of the shared screens not used here */
    "ColorCal_Logo", "ColorCal_Blurb", "Colour_Calibration_Backing", "CAR_STATS", "JOY_EVENT_BUTTON1", "PLAYER"
};
#define PLAYER_SELECT_LABEL 0xC90DC4ABu                  /* the "Player Select" text */

int gv_list_bind(GvList *v, GvScreen *s, const OptionsMenu *m) {
    memset(v, 0, sizeof *v);
    v->scr = s;
    v->ctx = m->cfg.ctx;
    v->kind = m->screen;
    v->highlighted = -1;
    v->seen_build = m->build_serial;
    v->seen_blink = m->blink_serial;
    v->seen_exit = m->exit_serial;
    v->seen_dialog = m->dialog_serial;
    if (!s->loaded) return 0;
    UgScene *sc = &s->scene;
    int bad = 0;
    int tm = find(s, hn("TITLE_MASTER"), &bad), dm = find(s, hn("DATA_MASTER"), &bad);
    v->header = find(s, H_HEADER, &bad);
    v->header2 = find(s, hn("Header_text2"), &bad);
    v->rail = find(s, hn("ScrollBar_Backing"), &bad);
    v->tab = find(s, hn("ScrollBar_Handle"), &bad);
    v->arrow1 = find(s, hn("ScrollBar_Arrow_1"), &bad);
    v->arrow2 = find(s, hn("ScrollBar_Arrow_2"), &bad);
    v->end1 = find(s, hn("ScrollBar_Backing_End_1"), &bad);
    v->end2 = find(s, hn("ScrollBar_Backing_End_2"), &bad);
    for (int n = 1; n <= OM_MAX_ROWS + 1; n++) {
        GvRow *r = &v->row[n];
        r->name = find(s, hnf("OPTION_NAME_%d", n), &bad);
        r->data = find(s, hnf("OPTION_DATA_%d", n), &bad);
        r->left = find(s, hnf("LEFT_ARROW_%d", n), &bad);
        r->right = find(s, hnf("RIGHT_ARROW_%d", n), &bad);
        r->backing = find(s, hnf("OPTION_BACKING_%d", n), &bad);
        r->base = find(s, hnf("SLIDER_BASE_%d", n), &bad);
        r->knob = find(s, hnf("SLIDER_KNOB_%d", n), &bad);
        r->handle = find(s, hnf("SLIDER_HANDLE_%d", n), &bad);
        r->value = find(s, hnf("SLIDER_VALUE_%d", n), &bad);
        r->beg = find(s, hnf("SLIDER_BEG_%d", n), &bad);
        r->end = find(s, hnf("SLIDER_END_%d", n), &bad);
        if (r->name >= 0 || r->base >= 0 || r->backing >= 0) v->nslots = n;
    }
    const char *missing = bad ? "an ambiguous object name" : tm < 0 ? "TITLE_MASTER" : dm < 0 ? "DATA_MASTER" :
                          v->row[1].name < 0 ? "OPTION_NAME_1" : NULL;
    if (missing) {
        snprintf(s->status, sizeof s->status, "%s: authored list binding unavailable (%s)", s->name, missing);
        return 0;
    }
    /* grid anchors: top-left + size of the two master objects (4.2); images carry their size, anything
     * else falls back to the specified table */
    GlAnchors fb = om_anchors(m->cfg.ctx, m->screen);
    /* centres and sizes are the ones cached at creation (A6), never the evaluated objects */
    const UgNode *t = &sc->nodes[tm], *d = &sc->nodes[dm];
    const GvAnchor *ta = &s->title_master, *da = &s->data_master;
    float tw = t->obj->type == UG_OBJ_IMAGE || t->obj->type == UG_OBJ_GROUP ? ta->w : fb.tw;
    float th = t->obj->type == UG_OBJ_IMAGE || t->obj->type == UG_OBJ_GROUP ? ta->h : fb.th;
    float dw = d->obj->type == UG_OBJ_IMAGE || d->obj->type == UG_OBJ_GROUP ? da->w : fb.dw;
    float dh = d->obj->type == UG_OBJ_IMAGE || d->obj->type == UG_OBJ_GROUP ? da->h : fb.dh;
    if (tw <= 0 || th <= 0 || dw <= 0 || dh <= 0) { tw = fb.tw; th = fb.th; dw = fb.dw; dh = fb.dh; }
    v->anchors = gl_anchors_from_centres(ta->x, ta->y, tw, th, da->x, da->y, dw, dh);
    hide(sc, tm, 1);                                         /* anchors are never drawn */
    hide(sc, dm, 1);
    for (size_t i = 0; i < sizeof k_hidden_names / sizeof k_hidden_names[0]; i++) {
        int n = find(s, hn(k_hidden_names[i]), NULL);
        if (n >= 0) hide(sc, n, 1);
    }
    for (int i = 0; i < sc->n; i++)
        if (sc->nodes[i].obj->has_label_hash && sc->nodes[i].obj->label_hash == PLAYER_SELECT_LABEL) hide(sc, i, 1);
    v->arrow_tex = gv_texture(s->assets, H_ARROW_TEX);
    if (!v->arrow_tex) {                                     /* the packages leave the arrows untextured: no art, no screen */
        snprintf(s->status, sizeof s->status, "%s: row arrow texture 0x%08X unavailable", s->name, H_ARROW_TEX);
        return 0;
    }
    v->bound = 1;
    return 1;
}

static void row_objs(const GvRow *r, int out[11]) {
    out[0] = r->name; out[1] = r->data; out[2] = r->left; out[3] = r->right; out[4] = r->backing;
    out[5] = r->base; out[6] = r->knob; out[7] = r->value; out[8] = r->handle; out[9] = r->beg; out[10] = r->end;
}
static void row_script(UgScene *sc, const GvRow *r, uint32_t script) {
    int o[11];
    row_objs(r, o);
    for (int i = 0; i < 11; i++) start(sc, o[i], script);
}

static void park(UgScene *sc, GvRow *r) {
    int o[11];
    row_objs(r, o);
    for (int i = 0; i < 11; i++) {
        if (o[i] < 0) continue;
        hide(sc, o[i], 1);
        if (!r->parked) ug_scene_app_set_world_pos(sc, o[i], sc->nodes[o[i]].we, GL_ROW_OFFSCREEN_Y);
    }
    r->parked = 1;
}

static const char *value_text(const OptionsMenu *m, const UgAssets *a, int i) {
    uint32_t key = 0;
    const char *t = om_row_value_text(m, i, &key);
    return key ? gv_text(a, key, t) : t;
}

void gv_list_sync(GvList *v, const OptionsMenu *m, float dt_ms) {
    if (!v->bound || !om_is_open(m)) return;
    GvScreen *s = v->scr;
    UgScene *sc = &s->scene;
    const UgAssets *as = s->assets;
    const GlAnchors *a = &v->anchors;
    int ingame_display = m->cfg.ctx == OM_CTX_INGAME && m->screen == OM_SCREEN_DISPLAY;

    /* headers (5) */
    uint32_t key;
    const char *t0 = om_header(m->cfg.ctx, m->screen, 0, &key);
    if (t0) settext(sc, v->header, gv_text(as, key, t0));
    const char *t1 = om_header(m->cfg.ctx, m->screen, 1, &key);
    if (t1) settext(sc, v->header2, gv_text(as, key, t1));

    int focus = om_focus(m);
    /* focus scripts (4.4): UnHighlight the old row, Highlight the new one. The data's focus response does not
     * start its highlight while the enter fade runs and it is re-sent at enter-complete (SPEC 3.6.2): the
     * explicit scripts wait for that, so the enter animation of the focused row is never cut short. */
    int ready = s->enter_done || s->time_ms >= GV_ENTER_FALLBACK_MS;
    if (focus >= 0 && focus < m->nrows && m->rows[focus].slot >= 1 && v->scr->loaded) {
        GvRow *cur = &v->row[m->rows[focus].slot];
        if (cur->name >= 0 && sc->focus != cur->name) ug_scene_set_focus(sc, cur->name);   /* the name object is the current button */
        if (ready && v->highlighted != focus) {
            int old = v->highlighted;
            if (old >= 0 && old < m->nrows && m->rows[old].slot >= 1) {
                GvRow *r = &v->row[m->rows[old].slot];
                row_script(sc, r, H_UNHIGHLIGHT);
                r->hl_fade_ms = HL_FADE_MS;
            }
            row_script(sc, cur, H_HIGHLIGHT);
            cur->hl_fade_ms = 0;
            v->highlighted = focus;
        }
    }
    for (int n = 1; n <= v->nslots; n++) {                   /* slots without a list row are parked */
        int used = 0;
        for (int i = 0; i < m->nrows; i++) if (m->rows[i].slot == n) used = 1;
        if (!used) park(sc, &v->row[n]);
    }
    for (int i = 0; i < m->nrows; i++) {
        const OmRow *row = om_row(m, i);
        int n = row->slot;
        if (n < 1 || n > OM_MAX_ROWS + 1) continue;
        GvRow *r = &v->row[n];
        if (!om_row_visible(m, i) || row->kind == OM_ROW_SPACER) { park(sc, r); continue; }
        int k = i - om_top(m);
        r->parked = 0;
        float ry = gl_row_top(a, k);
        int focused = i == focus;
        if (r->hl_fade_ms > 0 && !focused) r->hl_fade_ms -= dt_ms;
        /* name */
        if (r->name >= 0) {
            const UgNode *nn = &sc->nodes[r->name];
            float x = a->tx;
            if (nn->obj->justify & UG_J_CENTER) x = gl_label_centre_x(a);
            else if (nn->obj->justify & UG_J_RIGHT) x = a->tx + a->tw;
            hide(sc, r->name, 0);
            settext(sc, r->name, gv_text(as, row->label_key, row->label));
            ug_scene_app_set_world_pos(sc, r->name, x, ry);
        }
        /* value text of a choice row; nothing on sliders and buttons */
        if (r->data >= 0) {
            int show = row->kind == OM_ROW_CHOICE;
            hide(sc, r->data, !show);
            if (show) {
                settext(sc, r->data, value_text(m, as, i));
                ug_scene_app_set_world_pos(sc, r->data, gl_data_cx(a), gl_data_top(a, k));
            }
        }
        /* arrows (choice and slider rows) and backing: visible on the focused row, plus while it fades out */
        int arrows = row->kind == OM_ROW_CHOICE || row->kind == OM_ROW_SLIDER;
        int lit = focused || r->hl_fade_ms > 0;
        int dirs[2] = { r->left, r->right };
        for (int d = 0; d < 2; d++) {
            int an = dirs[d];
            if (an < 0) continue;
            hide(sc, an, !arrows || !lit);
            if (!arrows) continue;
            UgNode *nd = &sc->nodes[an];
            nd->tex_override = v->arrow_tex;
            float w = auth_w(sc, an), h = auth_h(sc, an);
            if (w <= 0 || h <= 0) { w = OM_ARROW_SIZE; h = OM_ARROW_SIZE; }      /* size not authored: the measured one */
            GlPt tl = d == 0 ? gl_left_arrow_tl(a, k, w) : gl_right_arrow_tl(a, k);
            place_tl(sc, an, tl.x, tl.y, w, h);
        }
        if (r->backing >= 0) {
            GlPt p = gl_backing_pos(a, k);
            hide(sc, r->backing, !lit || row->kind == OM_ROW_SPACER);
            ug_scene_app_set_world_pos(sc, r->backing, p.x, p.y);
        }
        /* slider parts (4.3); SLIDER_BEG/END stay where they were authored */
        int is_slider = row->kind == OM_ROW_SLIDER;
        int parts[4] = { r->base, r->knob, r->handle, r->value };
        for (int q = 0; q < 4; q++) hide(sc, parts[q], !is_slider);
        if (is_slider && r->base >= 0) {
            float bw = auth_w(sc, r->base), bh = auth_h(sc, r->base);
            float kw = r->knob >= 0 ? auth_w(sc, r->knob) : 16, kh = r->knob >= 0 ? auth_h(sc, r->knob) : 16;
            float fh = r->handle >= 0 ? auth_h(sc, r->handle) : 5.75f;
            GlPt btl = gl_slider_base_tl(a, k, bw);
            GlSlider g = gl_slider(btl, bw, bh, kh, om_row_fill(m, i));
            place_tl(sc, r->base, btl.x, btl.y, bw, bh);
            if (r->knob >= 0) {
                ug_scene_app_set_size(sc, r->knob, kw, kh);
                ug_scene_app_set_world_pos(sc, r->knob, g.knob_c.x, g.knob_c.y);
            }
            if (r->handle >= 0) place_tl(sc, r->handle, g.fill_tl.x, g.fill_tl.y, g.fill_w, fh);
            if (r->value >= 0) {
                snprintf(r->vtext, sizeof r->vtext, "%d", om_row_value(m, i));
                settext(sc, r->value, r->vtext);
            }
        }
        if (r->beg >= 0) hide(sc, r->beg, 1);
        if (r->end >= 0) hide(sc, r->end, 1);
    }

    /* Left/Right: the arrow of that direction blinks */
    if (m->blink_serial != v->seen_blink) {
        v->seen_blink = m->blink_serial;
        if (focus >= 0 && focus < m->nrows) {
            const GvRow *r = &v->row[m->rows[focus].slot];
            start(sc, m->blink_dir < 0 ? r->left : r->right, H_BLINK);
        }
    }
    /* dialog over the screen: DIM / UNDIM on the handler (1.4) */
    if (m->dialog_serial != v->seen_dialog) {
        v->seen_dialog = m->dialog_serial;
        gv_screen_dim(s, om_dialog_is_open(m));
    }
    if (m->exit_serial != v->seen_exit) {
        v->seen_exit = m->exit_serial;
        gv_screen_exit(s);
    }

    /* vertical scrollbar (4.5): tab on the focused row, everything hidden for N <= 1 */
    int bar = om_scrollbar_visible(m);
    hide(sc, v->rail, !bar);
    hide(sc, v->tab, !bar);
    hide(sc, v->end1, !bar);
    hide(sc, v->end2, !bar);
    hide(sc, v->arrow1, !bar || ingame_display);             /* in-game Display page: arrows hidden */
    hide(sc, v->arrow2, !bar || ingame_display);
    if (bar && v->tab >= 0 && v->rail >= 0) {
        const UgNode *rail = &sc->nodes[v->rail];
        float rh = rail->obj->size[1] > 0 ? rail->obj->size[1] : 282.0f;
        ug_scene_app_set_world_pos(sc, v->tab, sc->nodes[v->tab].we,
                                   gl_scroll_tab_y(rail->wf, rh, focus + 1, m->nrows));
    }
    ug_scene_relayout(sc);
}

/* ---- Controls ------------------------------------------------------------------------------------------ */

#define H_CTL_TITLE 0x4E11CF90u          /* device-selector title (3.1) */
#define H_CTL_ARROW_L 0xD1CDA594u
#define H_CTL_ARROW_R 0x9345E4A7u
#define H_CTL_COL1 0x666593C4u           /* Primary / Secondary column headers */
#define H_CTL_COL2 0x59ABA1E8u
#define H_CTL_TAB 0x3FBB7A4Eu            /* scroll tab */

int gv_controls_bind(GvControls *v, GvScreen *s, const ControlsMenu *m) {
    memset(v, 0, sizeof *v);
    v->scr = s;
    v->hl_row = v->hl_col = -1;
    v->seen_build = m->build_serial;
    v->seen_blink = m->blink_serial;
    v->seen_scroll = m->scroll_serial;
    v->seen_capture = m->capture_serial;
    if (!s->loaded) return 0;
    UgScene *sc = &s->scene;
    int bad = 0;
    int tm = find(s, hn("TITLE_MASTER"), &bad), dm = find(s, hn("DATA_MASTER"), &bad), dm2 = find(s, hn("DATA_MASTER2"), &bad);
    v->header = find(s, H_HEADER, &bad);
    v->title = find(s, H_CTL_TITLE, &bad);
    v->arrow_l = find(s, H_CTL_ARROW_L, &bad);
    v->arrow_r = find(s, H_CTL_ARROW_R, &bad);
    v->col1 = find(s, H_CTL_COL1, &bad);
    v->col2 = find(s, H_CTL_COL2, &bad);
    v->instr = find(s, hn("Instructions_Label"), &bad);
    v->rail = find(s, hn("ScrollBar_Backing"), &bad);
    v->tab = find(s, H_CTL_TAB, &bad);
    v->arrow1 = find(s, hn("ScrollBar_Arrow_1"), &bad);
    v->arrow2 = find(s, hn("ScrollBar_Arrow_2"), &bad);
    for (int n = 1; n <= GV_CTL_SLOTS; n++) {
        GvCtlRow *r = &v->row[n];
        r->name = find(s, hnf("OPTION_NAME_%d", n), &bad);
        r->data = find(s, hnf("OPTION_DATA_%d", n), &bad);
        r->data2 = find(s, hnf("OPTION_DATA_2_%d", n), &bad);
        r->backing = find(s, hnf("OPTIONS_BACKING_%d", n), &bad);
        if (r->name >= 0 && r->data >= 0 && r->data2 >= 0) v->nslots = n; else break;
    }
    const char *missing = bad ? "an ambiguous object name" : tm < 0 ? "TITLE_MASTER" : dm < 0 ? "DATA_MASTER" : dm2 < 0 ? "DATA_MASTER2" :
                          v->header < 0 ? "Header_text" : v->title < 0 ? "selector title" : v->arrow_l < 0 || v->arrow_r < 0 ? "selector arrows" :
                          v->col1 < 0 || v->col2 < 0 ? "column headers" : v->instr < 0 ? "Instructions_Label" :
                          v->rail < 0 || v->tab < 0 || v->arrow1 < 0 || v->arrow2 < 0 ? "scrollbar" :
                          v->nslots < CB_MAX_ROWS ? "row slots" : NULL;
    if (!missing && !(s->title_master.found && s->data_master.found && s->data_master2.found)) missing = "anchors";
    if (!missing && s->title_master.h <= 0) missing = "row pitch";
    if (missing) {
        snprintf(s->status, sizeof s->status, "%s: authored Controls binding unavailable (%s)", s->name, missing);
        return 0;
    }
    v->tx = s->title_master.x - s->title_master.w / 2;               /* top-left + size, read once (A6) */
    v->ty = s->title_master.y - s->title_master.h / 2;
    v->th = s->title_master.h;
    v->dx[0] = s->data_master.x - s->data_master.w / 2;
    v->dx[1] = s->data_master2.x - s->data_master2.w / 2;
    v->dw = s->data_master.w;
    hide(sc, tm, 1);                                                  /* anchors are never drawn */
    hide(sc, dm, 1);
    hide(sc, dm2, 1);
    v->bound = 1;
    return 1;
}

/* The anchor y that puts the top of a text at `top`: a vertically centred text is anchored at the middle of
 * its line (CONTROLS_SPEC 3.8: "top y_k" for all three columns). */
static float ctl_text_y(const UgScene *sc, int node, float top) {
    const UgNode *n = &sc->nodes[node];
    if (!(n->obj->justify & UG_J_VCENTER) || !n->face) return top;
    return top + 0.5f * (float)n->face->line_height * n->props.w[UG_P_SIZE + 1] * fabsf(n->wd);
}

static void ctl_park(UgScene *sc, GvCtlRow *r) {
    int o[4] = { r->name, r->data, r->data2, r->backing };
    for (int i = 0; i < 4; i++) {
        if (o[i] < 0) continue;
        hide(sc, o[i], 1);
        if (!r->parked) ug_scene_app_set_world_pos(sc, o[i], sc->nodes[o[i]].we, GL_ROW_OFFSCREEN_Y);
    }
    r->parked = 1;
}

static void ctl_row_script(UgScene *sc, const GvCtlRow *r, int col, uint32_t script) {
    start(sc, col ? r->data2 : r->data, script);
    start(sc, r->name, script);
    start(sc, r->backing, script);
}

void gv_controls_sync(GvControls *v, const ControlsMenu *m, float dt_ms) {
    if (!v->bound || !cm_is_open(m)) return;
    GvScreen *s = v->scr;
    UgScene *sc = &s->scene;
    const UgAssets *as = s->assets;
    static uint32_t pulse = 0;
    if (!pulse) pulse = hn("Pulse");

    /* texts: every one from the language file (3.1, 3.3, 3.5); no fallback text exists */
    settext(sc, v->header, gv_text(as, CM_KEY_HEADER, NULL));
    settext(sc, v->title, gv_text(as, cm_title_key(m), NULL));
    settext(sc, v->col1, gv_text(as, CM_KEY_PRIMARY, NULL));
    settext(sc, v->col2, gv_text(as, CM_KEY_SECONDARY, NULL));
    settext(sc, v->instr, gv_text(as, cm_instruction_key(m), NULL));

    int ready = s->enter_done || s->time_ms >= GV_ENTER_FALLBACK_MS;
    if (m->build_serial != v->seen_build) {                           /* a device change rebuilt the rows */
        v->seen_build = m->build_serial;
        v->hl_row = v->hl_col = -1;
        for (int n = 1; n <= v->nslots; n++) {
            GvCtlRow *r = &v->row[n];
            int o[4] = { r->name, r->data, r->data2, r->backing };
            for (int i = 0; i < 4; i++) start(sc, o[i], H_UNHIGHLIGHT);
            r->hl_fade_ms = 0;
        }
    }

    /* rows */
    int focus = m->focus_row;
    if (focus >= 0) v->tab_row = focus;
    for (int n = 1; n <= v->nslots; n++) {
        GvCtlRow *r = &v->row[n];
        int i = n - 1;
        if (i >= cm_row_count(m) || !cm_row_visible(m, i)) { ctl_park(sc, r); continue; }
        r->parked = 0;
        int k = i - m->top;
        float y = v->ty + v->th * (float)k;
        if (r->hl_fade_ms > 0 && !(i == focus)) r->hl_fade_ms -= dt_ms;
        const CbRow *row = cm_row(m, i);
        hide(sc, r->name, 0);
        settext(sc, r->name, gv_text(as, row->label_key, NULL));
        {
            const UgNode *nn = &sc->nodes[r->name];
            float x = v->tx;
            if (nn->obj->justify & UG_J_CENTER) x += s->title_master.w / 2;
            else if (nn->obj->justify & UG_J_RIGHT) x += s->title_master.w;
            ug_scene_app_set_world_pos(sc, r->name, x, ctl_text_y(sc, r->name, y));
        }
        for (int c = 0; c < 2; c++) {
            int node = c ? r->data2 : r->data;
            hide(sc, node, 0);
            cm_cell_text(m, i, c, r->text[c]);
            settext(sc, node, r->text[c]);
            ug_scene_app_set_world_pos(sc, node, v->dx[c] + v->dw / 2, ctl_text_y(sc, node, y));
        }
        if (r->backing >= 0) {
            int lit = i == focus || r->hl_fade_ms > 0;
            hide(sc, r->backing, !lit);
            ug_scene_app_set_world_pos(sc, r->backing, v->tx + 276.0f, y + 8.0f);   /* (tx + 276, y_k + 8) */
        }
    }

    /* focus scripts (4.4): UnHighlight the old cell and its row, Highlight the new one; the selector title too */
    if (ready) {
        int want_title = focus < 0;
        if (v->hl_title != want_title) { start(sc, v->title, want_title ? H_HIGHLIGHT : H_UNHIGHLIGHT); v->hl_title = want_title; }
        int row = focus, col = m->focus_col;
        if (v->hl_row != row || v->hl_col != col) {
            if (v->hl_row >= 0 && v->hl_row < m->nrows) {
                GvCtlRow *o = &v->row[v->hl_row + 1];
                ctl_row_script(sc, o, v->hl_col, H_UNHIGHLIGHT);
                o->hl_fade_ms = HL_FADE_MS;
            }
            if (row >= 0) {
                GvCtlRow *n = &v->row[row + 1];
                ctl_row_script(sc, n, col, H_HIGHLIGHT);
                n->hl_fade_ms = 0;
            }
            v->hl_row = row;
            v->hl_col = col;
        }
    }
    /* capture: the cell starts Pulse (keyboard Accept only); when it ends the focused cell is highlighted again */
    if (m->capture_serial != v->seen_capture) {
        v->seen_capture = m->capture_serial;
        if (m->capturing && m->cap_pulse && m->cap_row >= 0 && m->cap_row < m->nrows) {
            const GvCtlRow *r = &v->row[m->cap_row + 1];
            start(sc, m->cap_col ? r->data2 : r->data, pulse);
        } else if (!m->capturing && v->was_capturing && m->cap_row >= 0 && m->cap_row < m->nrows) {
            const GvCtlRow *r = &v->row[m->cap_row + 1];
            start(sc, m->cap_col ? r->data2 : r->data, H_HIGHLIGHT);
        }
        v->was_capturing = m->capturing;
    }

    /* selector arrows: hidden with one device, otherwise Static; the arrow of a change blinks */
    int arrows = cm_arrows_visible(m);
    hide(sc, v->arrow_l, !arrows);
    hide(sc, v->arrow_r, !arrows);
    if (m->blink_serial != v->seen_blink) {
        v->seen_blink = m->blink_serial;
        if (arrows) {
            start(sc, m->blink_dir < 0 ? v->arrow_l : v->arrow_r, H_BLINK);
            start(sc, m->blink_dir < 0 ? v->arrow_r : v->arrow_l, H_STATIC);
        }
    }
    /* Up/Down: the package blinks its scroll arrows */
    if (m->scroll_serial != v->seen_scroll) {
        v->seen_scroll = m->scroll_serial;
        start(sc, m->scroll_dir < 0 ? v->arrow1 : v->arrow2, H_BLINK);
    }

    /* scrollbar (3.6): the tab follows the focused row and is not moved while the selector is focused */
    int bar = cm_scrollbar_visible(m);
    hide(sc, v->rail, !bar);
    hide(sc, v->tab, !bar);
    hide(sc, v->arrow1, !bar);
    hide(sc, v->arrow2, !bar);
    if (bar) {
        const UgNode *rail = &sc->nodes[v->rail];
        float rh = rail->obj->size[1];
        ug_scene_app_set_world_pos(sc, v->tab, sc->nodes[v->tab].we, gl_scroll_tab_y(rail->wf, rh, v->tab_row + 1, cm_row_count(m)));
    }
    ug_scene_relayout(sc);
}

/* ---- dialog ---------------------------------------------------------------------------------------------- */

static int dialog_bind(GvDialog *v, const GuiDialog *d) {
    GvScreen *s = &v->scr;
    UgScene *sc = &s->scene;
    int bad = 0;
    v->msg = find(s, hn("message_blurb"), &bad);
    v->b1 = find(s, hn("button1"), &bad);
    v->b2 = find(s, hn("Button2"), &bad);
    v->b3 = find(s, hn("button3"), &bad);
    v->lbl1 = find(s, 0xDA473D2Fu, &bad);
    v->lbl2 = find(s, 0xDA473D30u, &bad);
    v->lbl3 = find(s, 0xDA473D31u, &bad);
    if (bad || v->msg < 0 || v->b1 < 0 || v->b2 < 0 || v->lbl1 < 0 || v->lbl2 < 0) {
        snprintf(s->status, sizeof s->status, "%s: authored dialog binding unavailable (message, buttons or labels missing)", s->name);
        return 0;
    }
    settext(sc, v->msg, gv_text(s->assets, d->spec.message_key, d->spec.message));
    settext(sc, v->lbl1, gv_text(s->assets, d->spec.label_key[0], d->spec.label[0]));
    settext(sc, v->lbl2, gv_text(s->assets, d->spec.label_key[1], d->spec.label[1]));
    hide(sc, v->b3, 1);                                      /* the third button and its label stay unused (6) */
    hide(sc, v->lbl3, 1);
    ug_scene_set_focus(sc, d->focus ? v->b2 : v->b1);
    v->seen_focus = d->focus;
    ug_scene_relayout(sc);
    v->bound = 1;
    v->open = 1;
    return 1;
}

/* A dialog that cannot be bound is dropped, keeping the reason. */
static void dialog_fail(GvDialog *v) {
    char why[sizeof v->scr.status];
    memcpy(why, v->scr.status, sizeof why);
    gv_screen_free(&v->scr);
    memcpy(v->scr.status, why, sizeof why);
}

int gv_dialog_open(GvDialog *v, const UgAssets *assets, const GuiDialog *d) {
    memset(v, 0, sizeof *v);
    if (!gv_screen_load(&v->scr, assets, d->spec.package)) return 0;
    if (!dialog_bind(v, d)) { dialog_fail(v); return 0; }
    return 1;
}
int gv_dialog_attach(GvDialog *v, const UgFngPackage *pk, const UgAssets *assets, const GuiDialog *d) {
    memset(v, 0, sizeof *v);
    if (!gv_screen_attach(&v->scr, pk, assets, d->spec.package)) return 0;
    if (!dialog_bind(v, d)) { dialog_fail(v); return 0; }
    return 1;
}

void gv_dialog_sync(GvDialog *v, const GuiDialog *d, float dt_ms) {
    if (!v->bound) return;
    gv_screen_update(&v->scr, dt_ms);
    if (d->focus != v->seen_focus) {                         /* authored focus responses run as data */
        ug_scene_set_focus(&v->scr.scene, d->focus ? v->b2 : v->b1);
        v->seen_focus = d->focus;
    }
    if (d->closing && !v->exit_sent) { v->exit_sent = 1; gv_screen_exit(&v->scr); }
    ug_scene_relayout(&v->scr.scene);
}

void gv_dialog_close(GvDialog *v) {
    gv_screen_free(&v->scr);
    memset(v, 0, sizeof *v);
}

/* General list adapter; the pause/options interface above remains unchanged. */
int gv_list_bind_rows(GvList *v, GvScreen *s) {
    OptionsMenu m; memset(&m,0,sizeof m);
    m.cfg.ctx=OM_CTX_FRONTEND; m.screen=OM_SCREEN_DISPLAY;
    return gv_list_bind(v,s,&m);
}
void gv_list_sync_rows(GvList *v,const GvListRow *rows,int count,int focus,float dt) {
    if(!v->bound || !rows || count<0 || count>OM_MAX_ROWS)return;
    OptionsMenu m; memset(&m,0,sizeof m);
    m.cfg.ctx=OM_CTX_FRONTEND; m.screen=OM_SCREEN_DISPLAY;
    m.nrows=count; m.list.n=count; m.list.focus=focus;
    m.list.top=focus>=OM_VISIBLE_ROWS?focus-OM_VISIBLE_ROWS+1:0;
    for(int i=0;i<count;i++) {
        m.rows[i].slot=i+1; m.rows[i].label=rows[i].label;m.rows[i].gfx=-1;
        m.rows[i].kind=rows[i].value?OM_ROW_CHOICE:OM_ROW_BUTTON;
    }
    int h=v->header,h2=v->header2;
    v->header=v->header2=-1; /* preserve the package's own heading */
    gv_list_sync(v,&m,dt);v->header=h;v->header2=h2;
    for(int i=0;i<count;i++) {
        GvRow *r=&v->row[i+1];
        if(rows[i].value)settext(&v->scr->scene,r->data,rows[i].value);
        if(!rows[i].enabled) {
            hide(&v->scr->scene,r->left,1);hide(&v->scr->scene,r->right,1);
        }
    }
}
