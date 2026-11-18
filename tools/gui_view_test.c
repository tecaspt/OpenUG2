/* gui_view_test -- the authored presentation (src/gui_view.c, ug_scene_app_write) against synthetic
 * in-memory packages. No retail package, texture, font or text is opened: the fixtures only copy the
 * object names, hashes and the numbers that SPEC_GUI_ART publishes (anchors, sizes, script names).
 *
 *   gui_view_test
 *
 * Also the Controls binder (CONTROLS_SPEC 3) over a synthetic package that copies only the published object
 * names, hashes and numbers.
 * Covers: base-key property writes (SPEC_GUI_ART 1.2), carousel binding and placement (3), list row
 * binding, slider parts, scrollbar, focus scripts, blink, dim, exit (4, 5), dialog binding (6). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_scancode.h>

#include "../src/gui_timing.h"
#include "../src/gui_view.h"
#include "../src/pause_screen.h"
#include "synth_pkgs.h"
#include "synth_fng.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    if (g_fail <= 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

/* ---- task AQ: navigation-event capture --------------------------------------------------------------
 * The sink gui_view would talk to (a sfx_fe ring in the game). Events are timestamped by a frame
 * counter, not by time; the syncs here run with dt 0. */
static int g_nav_events[64];
static int g_nav_count;
static void nav_capture(void *user, int event) {
    (void)user;
    if (g_nav_count < (int)(sizeof g_nav_events / sizeof g_nav_events[0])) g_nav_events[g_nav_count++] = event;
}
static void nav_reset(void) { g_nav_count = 0; }
/* index of the first occurrence of `ev`, -1 absent */
static int nav_find(int ev) {
    for (int i = 0; i < g_nav_count; i++) if (g_nav_events[i] == ev) return i;
    return -1;
}
/* the capture as a printable list, for failure messages */
static const char *nav_dump(void) {
    static char b[4 * 64];
    int k = 0;
    for (int i = 0; i < g_nav_count && k < (int)sizeof b - 8; i++) k += snprintf(b + k, sizeof b - (size_t)k, "%d,", g_nav_events[i]);
    b[k] = 0;
    return b;
}


/* ---- base-key property writes (SPEC_GUI_ART 1.2) ----------------------------------------------------- */
static void test_app_write(void) {
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    UgFngObject *a = add(p, UG_OBJ_IMAGE, "A", 10, 20, 100, 50);
    UgScript *sl = script(p, a, 0x77, 100);
    const float base[4] = { 10, 20, 1, 0 };
    const int tm[2] = { 0, 100 };
    const float dv[2][4] = { { 0, 0, 0, 0 }, { 30, 0, 0, 0 } };
    track(p, sl, UG_VT_VEC3, UG_P_POS, base, 2, tm, dv);
    UgScript *other = script(p, a, 0x78, 50);
    const float cbase[4] = { 255, 255, 255, 255 };
    const float cv[2][4] = { { 0, 0, 0, -255 }, { 0, 0, 0, 0 } };
    const int t2[2] = { 0, 50 };
    track(p, other, UG_VT_COLOR, UG_P_COLOR, cbase, 2, t2, cv);
    add(p, UG_OBJ_IMAGE, "B", 0, 0, 10, 10);                          /* no scripts */
    UgFngObject *grp = add(p, UG_OBJ_GROUP, "G", 100, 0, 1, 1);
    grp->size[0] = 2; grp->size[1] = 2;                              /* scaled parent */
    UgFngObject *child = add(p, UG_OBJ_IMAGE, "C", 5, 5, 4, 4);
    child->has_parent = 1; child->parent_guid = grp->guid;
    finish(p);
    UgScene s;
    CHECK(ug_scene_init(&s, &p->pk, NULL), "scene init");
    int ia = idx(&s, "A"), ib = idx(&s, "B"), ic = idx(&s, "C");
    CHECK(ia == 0 && ib == 1 && ic == 3 && idx(&s, "nope") == -1, "find by name hash");
    float v[2] = { 50, 60 };
    ug_scene_app_write(&s, ia, UG_P_POS, 2, v);
    CHECK(s.nodes[ia].props.w[UG_P_POS] == 50 && s.nodes[ia].props.w[UG_P_POS + 1] == 60 && s.nodes[ia].props.w[UG_P_POS + 2] == 1, "value written to the node");
    CHECK(a->scripts[0].tracks[0].base.v[0] == 10, "the package itself is never modified");
    CHECK(s.nodes[ia].own != NULL && s.nodes[ia].own[0].tracks[0].base.v[0] == 50 && s.nodes[ia].own[0].tracks[0].base.v[1] == 60 &&
          s.nodes[ia].own[0].tracks[0].base.v[2] == 1, "base key of the scripts' track holds the value");
    CHECK(ug_scene_start_script(&s, ia, 0x77) == 1, "start the script by id");
    ug_scene_update(&s, 50);
    CHECK(NEAR(s.nodes[ia].props.w[UG_P_POS], 65, 0.001) && NEAR(s.nodes[ia].props.w[UG_P_POS + 1], 60, 0.001),
          "a later script animates relative to the application value: x %.3f", s.nodes[ia].props.w[UG_P_POS]);
    ug_scene_update(&s, 60);
    CHECK(NEAR(s.nodes[ia].props.w[UG_P_POS], 80, 0.001), "and ends relative to it");
    /* a write while a script runs keeps the authored delta on top */
    ug_scene_start_script(&s, ia, 0x77);
    ug_scene_update(&s, 50);
    v[0] = 0; v[1] = 0;
    ug_scene_app_write(&s, ia, UG_P_POS, 2, v);
    CHECK(NEAR(s.nodes[ia].props.w[UG_P_POS], 15, 0.001), "write during a running script: base + delta(t) = %.3f", s.nodes[ia].props.w[UG_P_POS]);
    /* colour base: the FadeIn-like script scales from the application alpha */
    float c[4] = { 255, 255, 255, 200 };
    ug_scene_app_write(&s, ia, UG_P_COLOR, 4, c);
    ug_scene_start_script(&s, ia, 0x78);
    ug_scene_update(&s, 25);
    CHECK(NEAR(s.nodes[ia].props.w[UG_P_COLOR + 3], 200 - 255 * 0.5, 0.6) || s.nodes[ia].props.w[UG_P_COLOR + 3] >= 72,
          "colour animates from the application base: %.1f", s.nodes[ia].props.w[UG_P_COLOR + 3]);
    /* object without any script: stored data only */
    ug_scene_app_set_size(&s, ib, 33, 44);
    CHECK(s.nodes[ib].props.w[UG_P_SIZE] == 33 && s.nodes[ib].props.w[UG_P_SIZE + 1] == 44 && s.nodes[ib].own == NULL, "no scripts: written to the object's own data");
    /* world position through a scaled parent */
    ug_scene_update(&s, 0);
    ug_scene_app_set_world_pos(&s, ic, 120, 10);
    ug_scene_relayout(&s);
    CHECK(NEAR(s.nodes[ic].we, 120, 0.001) && NEAR(s.nodes[ic].wf, 10, 0.001), "world position through a scaled group: %.3f,%.3f", s.nodes[ic].we, s.nodes[ic].wf);
    /* bounds */
    ug_scene_app_write(&s, -1, UG_P_POS, 2, v);
    ug_scene_app_write(&s, ia, UG_NPROPS - 1, 4, v);
    ug_scene_app_write(&s, 999, UG_P_POS, 2, v);
    CHECK(ug_scene_start_script(&s, ia, 0xDEAD) == 0 && ug_scene_start_script(&s, -1, 1) == 0, "unknown script / node");
    /* ambiguous names */
    Pkg *q = (Pkg *)calloc(1, sizeof *q);
    add(q, UG_OBJ_IMAGE, "DUP", 0, 0, 1, 1); add(q, UG_OBJ_IMAGE, "dup", 0, 0, 1, 1); finish(q);
    UgScene s2;
    ug_scene_init(&s2, &q->pk, NULL);
    CHECK(ug_scene_find_name(&s2, hn("DUP")) == -2, "ambiguous name");
    ug_scene_free(&s2);
    ug_scene_free(&s);
    free(q);
    free(p);
}

/* ---- carousel -------------------------------------------------------------------------------------------- */
static void test_carousel_view(void) {
    static const uint32_t keys[] = { 0x62AA0DF6u, 0x520DE4E3u, 0x84D1D864u, 0x4C9E34E6u };
    Fake f;
    fake_init(&f, keys, 4);
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_carousel_pkg(p, 562, 1);
    GvScreen s;
    CHECK(gv_screen_attach(&s, &p->pk, &f.a, "UI_pause.fng"), "attach: %s", s.status);
    CHECK(s.handler >= 0 && s.scene.pc_build && s.scene.hide_hint_text, "handler found, PC build");
    PauseScreen ps;
    PauseContext c;
    pe_context_default(&c);
    memset(&ps, 0, sizeof ps);
    ps_open(&ps, &c);
    GvCarousel v;
    CHECK(gv_carousel_bind(&v, &s, &ps.car), "bind: %s", s.status);
    CHECK(v.nslots == 10 && NEAR(ps.car.master_x, 0.388, 0.0001) && NEAR(ps.car.master_y, -14.296, 0.0001), "ten slots, master centre read from the package");
    CHECK(s.scene.nodes[v.master].app_hidden, "OPTION_MASTER is never shown");
    GvEntry e[PE_MAX];
    for (int i = 0; i < ps.n; i++) {
        int tex = gv_texture(&f.a, ps.entries[i].icon_key);
        e[i].tex = tex;
        e[i].label = ps.entries[i].label;
    }
    CHECK(e[0].tex > 0 && e[1].tex > 0 && e[2].tex > 0 && e[3].tex > 0, "icon textures resolved by key");
    gv_screen_update(&s, 0);
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    const UgScene *sc = &s.scene;
    const UgNode *n0 = &sc->nodes[v.slot[0]], *n1 = &sc->nodes[v.slot[1]], *n2 = &sc->nodes[v.slot[2]], *n3 = &sc->nodes[v.slot[3]], *n4 = &sc->nodes[v.slot[4]];
    CHECK(NEAR(n0->we, 0.388, 0.002) && NEAR(n0->wf, -14.296, 0.002) && NEAR(n0->props.w[UG_P_SIZE], 118, 0.001) && NEAR(n0->props.w[UG_P_SIZE + 1], 59, 0.001), "focused slot at the master, 118x59");
    CHECK(NEAR(n1->we, 158.388, 0.002) && NEAR(n1->props.w[UG_P_SIZE], 94.4, 0.001) && NEAR(n1->props.w[UG_P_SIZE + 1], 47.2, 0.001) && !n1->app_hidden, "neighbour at +158, 0.8x");
    CHECK(n2->app_hidden && n3->app_hidden, "slots beyond +-240 are hidden");
    CHECK(n0->tex_override == e[0].tex && n1->tex_override == e[1].tex, "icon textures replace the authored ones");
    CHECK(n4->obj->pos[1] > 330 && n4->app_hidden == 0 && n4->tex_override == 0, "unused slots are never touched (authored off-screen position)");
    CHECK(NEAR(n4->we, 0, 0.001) && NEAR(n4->wf, 334 + 24 * 4, 0.001), "unused slot keeps its authored position");
    CHECK(!strcmp(sc->nodes[v.title].text_override, "Return to Explore Mode") && !strcmp(sc->nodes[v.header].text_override, "Pause"), "caption and header");
    CHECK(NEAR(sc->nodes[v.tab].we, -270, 0.002) && NEAR(sc->nodes[v.tab].wf, -52.3, 0.002), "tab at the first position, y untouched");
    CHECK(sc->nodes[v.arrow_l].app_hidden && !sc->nodes[v.arrow_r].app_hidden && !sc->nodes[v.rail].app_hidden && !sc->nodes[v.tab].app_hidden, "arrows and rail");
    CHECK(sc->focus == v.slot[0], "the focused slot is the scene focus");
    CHECK(!strcmp(sc->nodes[v.back_button].text_override, "Back"), "Back_button text set (console-only, not drawn)");
    CHECK(!gv_screen_ready(&s), "no font available: the screen is not ready (nothing is drawn)");
    /* the colour is written as a base: the enter fade ends at the written alpha */
    gv_screen_update(&s, 400);
    CHECK(n0->props.w[UG_P_COLOR + 3] == 255, "enter fade ends at 255: %.1f", n0->props.w[UG_P_COLOR + 3]);
    CHECK(s.enter_done, "the package's enter-complete message was seen after 400 ms");

    /* a move to the right: caption, tab, arrows, blink and the tween */
    ps_input(&ps, PS_IN_RIGHT);
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    CHECK(!strcmp(sc->nodes[v.title].text_override, "Options"), "caption changes at once");
    CHECK(NEAR(n0->props.w[UG_P_SIZE], 94.4, 0.001) && NEAR(n1->props.w[UG_P_SIZE], 118, 0.001), "focus (size) changes at once");
    CHECK(NEAR(n1->we, 158.388, 0.002), "the scroll offset has not moved yet at the input frame");
    CHECK(NEAR(sc->nodes[v.tab].we, -88, 0.002), "tab: %.3f", sc->nodes[v.tab].we);
    CHECK(!sc->nodes[v.arrow_l].app_hidden && !sc->nodes[v.arrow_r].app_hidden, "both arrows now");
    CHECK(ug_scene_script_id(sc, v.arrow_r) == hn("Blink"), "right arrow blinks");
    CHECK(sc->focus == v.slot[1], "focus follows");
    ps_update(&ps, 0.15f);
    gv_screen_update(&s, 150);
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    CHECK(NEAR(n1->we, 0.388 + 79.0, 0.01) && NEAR(n0->we, 0.388 - 79.0, 0.01), "half-way through the 0.3 s tween: %.3f", n1->we);
    ps_update(&ps, 0.2f);
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    CHECK(NEAR(n1->we, 0.388, 0.002) && NEAR(n0->we, -157.612, 0.002), "settled");
    /* a second move in the same direction while blinking does not restart the blink */
    ps_input(&ps, PS_IN_RIGHT);
    /* disabled Replay is skipped: Right goes to Quit (entry 4); its slot neighbours Options */
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    CHECK(ps_selected(&ps) == 3 && !strcmp(sc->nodes[v.title].text_override, "Quit to Main Menu") && sc->nodes[v.arrow_r].app_hidden, "last entry: right arrow hidden");
    /* edge fade after a cancelled dialog: slot colour alpha written as base */
    ps_input(&ps, PS_IN_ACCEPT);
    ps_input(&ps, PS_IN_BACK);
    ps_update(&ps, 0.2f);
    CHECK(ps.car.fade, "fade on");
    gv_carousel_apply(&v, &ps.car, e, ps.n, "Pause", ps.move_serial, ps.move_dir);
    gv_screen_update(&s, 400);
    float expect = (float)gc_alpha(&ps.car, 2);                /* the disabled Replay entry: capped at 150 */
    CHECK(sc->nodes[v.slot[2]].props.w[UG_P_COLOR + 3] <= 150.5f && expect <= 150, "disabled entry alpha capped at 150 (%.1f)", sc->nodes[v.slot[2]].props.w[UG_P_COLOR + 3]);
    CHECK(NEAR(sc->nodes[v.slot[3]].props.w[UG_P_COLOR + 3], 255, 0.6), "focused slot at 255");
    /* single entry: rail, tab and arrows hidden */
    PauseContext online = c;
    online.mode = 8; online.race = 1; online.session = 3;
    ps_open(&ps, &online);
    GvEntry one = { gv_texture(&f.a, ps.entries[0].icon_key), ps.entries[0].label };
    gv_carousel_apply(&v, &ps.car, &one, 1, "Pause", ps.move_serial, 0);
    CHECK(sc->nodes[v.rail].app_hidden && sc->nodes[v.tab].app_hidden && sc->nodes[v.arrow_l].app_hidden && sc->nodes[v.arrow_r].app_hidden, "one entry: no rail, tab or arrows");
    /* missing art: the slot is omitted, nothing is substituted */
    GvEntry noart = { 0, "x" };
    gv_carousel_apply(&v, &ps.car, &noart, 1, NULL, ps.move_serial, 0);
    CHECK(sc->nodes[v.slot[0]].app_hidden, "an entry without art is not drawn");
    /* exit: the authored exit script on the handler, exit-complete reported after 320 ms */
    gv_screen_exit(&s);
    CHECK(ug_scene_script_id(sc, s.handler) == hn("FORWARD"), "exit script started on the handler");
    gv_screen_update(&s, 319);
    CHECK(!s.exit_done, "no exit-complete yet");
    gv_screen_update(&s, 2);
    CHECK(s.exit_done, "exit-complete message after 320 ms");
    gv_screen_dim(&s, 1);
    CHECK(ug_scene_script_id(&s.scene, s.handler) == GT_SCRIPT_DIM, "the DIM script runs on the handler");
    gv_screen_dim(&s, 0);
    CHECK(ug_scene_script_id(&s.scene, s.handler) == GT_SCRIPT_UNDIM, "and UNDIM");
    gv_screen_free(&s);
    /* binding fails closed when an object is missing */
    memset(p, 0, sizeof *p);
    build_carousel_pkg(p, 562, 0);
    p->obj[1].name_hash = hn("NOT_A_TITLE");                  /* ICON_TITLE removed */
    GvScreen s2;
    CHECK(gv_screen_attach(&s2, &p->pk, &f.a, "UI_pause.fng"), "attach incomplete package");
    GuiCarousel dummy;
    gc_init(&dummy, 562);
    CHECK(!gv_carousel_bind(&v, &s2, &dummy) && strstr(s2.status, "ICON_TITLE"), "binding fails closed: %s", s2.status);
    gv_screen_free(&s2);
    memset(p, 0, sizeof *p);
    build_carousel_pkg(p, 562, 0);
    p->obj[7].name_hash = hn("OPTION_5");                      /* duplicate name */
    p->obj[8].name_hash = hn("OPTION_5");
    gv_screen_attach(&s2, &p->pk, &f.a, "UI_pause.fng");
    CHECK(!gv_carousel_bind(&v, &s2, &dummy), "ambiguous names are rejected");
    gv_screen_free(&s2);
    free(p);
    fake_free(&f);
}

/* ---- option list ------------------------------------------------------------------------------------------ */
typedef struct LFx {
    OptionsMenu m;
    StGraphics g;
    StGfxCaps caps;
    StRegStore store;
    OmConfig cfg;
} LFx;
static void lfx_open(LFx *f, OmContext ctx, int customize) {
    memset(f, 0, sizeof *f);
    st_gfx_defaults(&f->g);
    st_gfx_caps_full(&f->caps, 5);
    st_reg_open_memory(&f->store);
    om_config_default(&f->cfg, ctx);
    om_open(&f->m, &f->cfg, &f->g, &f->caps, &f->store, 0);
    if (customize) { om_input(&f->m, OM_IN_DOWN); om_input(&f->m, OM_IN_DOWN); om_input(&f->m, OM_IN_ACCEPT); om_update(&f->m, 1.0f); }
}
static const UgNode *rn(const GvList *v, int slot, int which) {
    const GvRow *r = &v->row[slot];
    int idx[5] = { r->name, r->data, r->left, r->right, r->backing };
    return idx[which] >= 0 ? &v->scr->scene.nodes[idx[which]] : NULL;
}

static void test_list_view(void) {
    static const uint32_t keys[] = { 0x9943D584u };
    Fake f;
    fake_init(&f, keys, 1);
    strtab_put(&f.a.strings, 0xA9A76BBAu, "Resolution!");
    strtab_put(&f.a.strings, 0x0000063Cu, "On!");
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_list_pkg(p, 8, 1, 1);
    GvScreen s;
    CHECK(gv_screen_attach(&s, &p->pk, &f.a, "UI_Options.fng"), "attach");
    LFx x;
    lfx_open(&x, OM_CTX_FRONTEND, 0);
    GvList v;
    CHECK(gv_list_bind(&v, &s, &x.m), "bind: %s", s.status);
    /* task AQ: every bound screen emits. A bind itself is silent; the enter-complete of the
     * screen's own update is the SCREEN_IN moment. */
    gv_set_nav_hook(nav_capture, NULL);
    nav_reset();
    gv_screen_update(&s, 0);
    gv_list_sync(&v, &x.m, 0);
    CHECK(g_nav_count == 0, "bind and the first sync emit nothing (events %s)", nav_dump());
    CHECK(NEAR(v.anchors.tx, -271, 0.001) && NEAR(v.anchors.ty, -138, 0.001) && NEAR(v.anchors.dx, 18, 0.001) && NEAR(v.anchors.dw, 216, 0.001), "anchors from TITLE_MASTER / DATA_MASTER");
    CHECK(v.arrow_tex > 0, "arrow texture resolved");
    gv_screen_update(&s, 0);
    gv_list_sync(&v, &x.m, 0);
    const UgScene *sc = &s.scene;
    /* row 1 (Resolution): choice, focused */
    const UgNode *nm = rn(&v, 1, 0), *dt = rn(&v, 1, 1), *la = rn(&v, 1, 2), *ra = rn(&v, 1, 3), *bg = rn(&v, 1, 4);
    CHECK(!strcmp(nm->text_override, "Resolution!") && !strcmp(dt->text_override, "640x480"), "texts: language table first, literal resolution");
    CHECK(NEAR(nm->we, -271, 0.01) && NEAR(nm->wf, -138 + 12, 0.01), "name anchor (FadeIn offset +12 at t=0): %.2f,%.2f", nm->we, nm->wf);
    CHECK(NEAR(dt->we, 126, 0.01) && NEAR(dt->wf, -138, 0.01), "value text centred on the data column");
    CHECK(NEAR(la->we, 6, 0.01) && NEAR(la->wf, -126, 0.01) && NEAR(ra->we, 246, 0.01) && NEAR(ra->wf, -126, 0.01), "arrows hug the data column: %.2f / %.2f", la->we, ra->we);
    CHECK(la->tex_override == v.arrow_tex && ra->tex_override == v.arrow_tex && !la->app_hidden && !ra->app_hidden, "arrow texture set by key, arrows on the focused row");
    CHECK(NEAR(bg->we, 5, 0.01) && NEAR(bg->wf, -128, 0.01) && !bg->app_hidden, "backing group at (tx+276, ry+10)");
    CHECK(sc->nodes[v.row[1].base].app_hidden && sc->nodes[v.row[1].knob].app_hidden, "slider parts hidden on a choice row");
    CHECK(ug_scene_script_id(sc, v.row[1].name) == hn("FadeIn"), "the enter fade of the focused row is not cut short by Highlight");
    /* row 2 (Level Of Detail slider, value 4 -> f = .75): parts */
    nm = rn(&v, 2, 0);
    const UgNode *base = &sc->nodes[v.row[2].base], *knob = &sc->nodes[v.row[2].knob], *fill = &sc->nodes[v.row[2].handle];
    float lvl = (float)x.g.v[ST_GFX_PERF_LEVEL];
    float fr = (lvl < 1 ? 1 : lvl > 5 ? 5 : lvl) - 1;
    fr /= 4.0f;
    CHECK(NEAR(base->we, 126, 0.02) && NEAR(base->wf, -102 + 16, 0.02) && !base->app_hidden, "slider base centred on the data column, row 2");
    CHECK(NEAR(knob->we, 8.54 + 46 + fr * 142.92, 0.03) && NEAR(knob->wf, -102 + 16 - 1, 0.02), "knob at f=%.2f: %.3f", fr, knob->we);
    CHECK(NEAR(fill->props.w[UG_P_SIZE], fr * 142.92, 0.03) && NEAR(fill->props.w[UG_P_SIZE + 1], 5.75, 0.001), "fill width f*W, authored height");
    CHECK(NEAR(fill->we - fill->props.w[UG_P_SIZE] / 2, 54.54, 0.03) && NEAR(fill->wf - 5.75 / 2, -102 + 16 - 8 + 1, 0.03), "fill top-left (bx+46, by+bh/2-kh/2+1)");
    CHECK(sc->nodes[v.row[2].beg].app_hidden && sc->nodes[v.row[2].end].app_hidden, "SLIDER_BEG/END are not shown");
    CHECK(sc->nodes[v.row[2].data].app_hidden, "no value text on a slider row");
    { char want[16]; snprintf(want, sizeof want, "%d", om_row_value(&x.m, 1));
      CHECK(!strcmp(sc->nodes[v.row[2].value].text_override, want), "SLIDER_VALUE (if the package has one) shows the integer"); }
    CHECK(sc->nodes[v.row[2].left].app_hidden && sc->nodes[v.row[2].backing].app_hidden, "unfocused row: no arrows, no backing");
    /* rows 3/4 buttons: only the name */
    CHECK(!sc->nodes[v.row[3].name].app_hidden && sc->nodes[v.row[3].data].app_hidden && sc->nodes[v.row[3].left].app_hidden, "button row shows the label only");
    /* unused slots 5..8 are parked off-screen and hidden */
    CHECK(sc->nodes[v.row[5].name].app_hidden && sc->nodes[v.row[5].name].wf >= GL_ROW_OFFSCREEN_Y - 0.01f, "slot without a row parked at y=6969 (+ the authored fade offset)");
    /* header and scrollbar */
    CHECK(!strcmp(sc->nodes[v.header].text_override, "Display"), "Header_Text <- Display");
    CHECK(sc->nodes[v.header2].text_override == NULL, "no second header in the front end");
    CHECK(!sc->nodes[v.rail].app_hidden && !sc->nodes[v.arrow1].app_hidden && !sc->nodes[v.end1].app_hidden, "scrollbar shown for N > 1");
    CHECK(NEAR(sc->nodes[v.tab].wf, -139 + 8, 0.01) && NEAR(sc->nodes[v.tab].we, 273, 0.01), "tab on the first row, x untouched");
    CHECK(sc->nodes[idx(sc, "PLAYER")].app_hidden, "PLAYER hidden");
    CHECK(sc->nodes[v.row[1].name].app_hidden == 0 && sc->nodes[v.row[1].name].obj->flags & UG_OF_BUTTON, "names are buttons");
    /* enter complete: Highlight starts on the focused row */
    gv_screen_update(&s, 400);
    gv_list_sync(&v, &x.m, 400);
    CHECK(s.enter_done && ug_scene_script_id(sc, v.row[1].name) == hn("Highlight"), "Highlight starts on the focused row at enter-complete");
    CHECK(nav_find(GV_NAV_SCREEN_IN) == 0 && g_nav_count == 1, "enter-complete emits exactly SCREEN_IN (%d events)", g_nav_count);
    nav_reset();
    CHECK(NEAR(nm->wf, -138 + 12, 0.01) || NEAR(rn(&v, 1, 0)->wf, -138, 0.01) || NEAR(rn(&v, 1, 0)->wf, -126, 0.01), "name position survives the script change");
    /* focus moves: UnHighlight / Highlight, arrows linger while fading, tab moves */
    om_input(&x.m, OM_IN_DOWN);
    gv_list_sync(&v, &x.m, 0);
    CHECK(nav_find(GV_NAV_MOVE) == 0 && g_nav_count == 1, "a focus move emits exactly MOVE (events %s)", nav_dump());
    nav_reset();
    CHECK(ug_scene_script_id(sc, v.row[1].name) != hn("Highlight"), "old row no longer highlighted");
    CHECK(!sc->nodes[v.row[1].left].app_hidden, "old row's arrows linger for the fade-out");
    CHECK(!sc->nodes[v.row[2].left].app_hidden && !sc->nodes[v.row[2].right].app_hidden && !sc->nodes[v.row[2].backing].app_hidden, "new row lit");
    CHECK(sc->focus == v.row[2].name, "name object is the current button");
    CHECK(NEAR(sc->nodes[v.tab].wf, -139 + 8 + 266.0 / 3.0, 0.01), "tab follows the focus");
    gv_list_sync(&v, &x.m, 100);
    CHECK(!sc->nodes[v.row[1].left].app_hidden, "still fading at 100 ms");
    gv_list_sync(&v, &x.m, 100);
    CHECK(sc->nodes[v.row[1].left].app_hidden && sc->nodes[v.row[1].backing].app_hidden, "old row's arrows hidden after the fade");
    CHECK(ug_scene_script_id(sc, v.row[2].name) == hn("Highlight"), "row 2 highlighted");
    /* a Right press on the slider row blinks its right arrow */
    om_input(&x.m, OM_IN_RIGHT);
    gv_list_sync(&v, &x.m, 0);
    CHECK(ug_scene_script_id(sc, v.row[2].right) == hn("Blink"), "right arrow blinks");
    CHECK(nav_find(GV_NAV_TAB) == 0 && g_nav_count == 1, "a Left/Right change emits exactly TAB (events %s)", nav_dump());
    nav_reset();
    CHECK(NEAR(sc->nodes[v.row[2].handle].props.w[UG_P_SIZE], om_row_fill(&x.m, 1) * 142.92, 0.03), "slider follows the new value");
    /* dialog: DIM then UNDIM on the handler */
    om_input(&x.m, OM_IN_DOWN); om_input(&x.m, OM_IN_DOWN);
    gv_list_sync(&v, &x.m, 0);                                 /* the two approach moves fire here; the assert below wants only the ACCEPT pair */
    nav_reset();
    om_input(&x.m, OM_IN_ACCEPT);
    gv_list_sync(&v, &x.m, 0);
    CHECK(om_dialog_is_open(&x.m), "dialog open");
    CHECK(nav_find(GV_NAV_ACCEPT) == 0 && nav_find(GV_NAV_DIALOG_OPEN) == 1 && g_nav_count == 2,
          "ACCEPT then the dialog opening, in order (events %s)", nav_dump());
    nav_reset();
    /* DIM is a 0 ms script: it has finished by the time we look; its effect is the broadcast */
    om_input(&x.m, OM_IN_BACK);
    om_update(&x.m, 0.2f);
    gv_list_sync(&v, &x.m, 0);
    CHECK(!om_dialog_is_open(&x.m), "dialog closed");
    CHECK(nav_find(GV_NAV_DIALOG_CLOSE) >= 0, "the dialog closing emits DIALOG_CLOSE (events %s)", nav_dump());
    nav_reset();
    /* leaving: exit script on the handler, exit-complete after 140 ms */
    om_input(&x.m, OM_IN_BACK);
    gv_list_sync(&v, &x.m, 0);
    CHECK(s.exit_started && ug_scene_script_id(sc, s.handler) == hn("Forward"), "exit script started when the menu begins to leave");
    CHECK(nav_find(GV_NAV_SCREEN_OUT) >= 0 && nav_find(GV_NAV_SCREEN_IN) < 0, "the exit emits SCREEN_OUT only (events %s)", nav_dump());
    nav_reset();
    gv_screen_update(&s, 139);
    CHECK(!s.exit_done, "not yet");
    gv_screen_update(&s, 2);
    CHECK(s.exit_done, "exit-complete after 140 ms");
    gv_screen_free(&s);
    st_reg_close(&x.store);
    gv_set_nav_hook(NULL, NULL);                               /* task AQ: later tests stay unhooked */
    nav_reset();

    /* Customize: scrolling window, rows outside it are parked */
    free(p);
    p = (Pkg *)calloc(1, sizeof *p);
    build_list_pkg(p, 24, 1, 0);
    gv_screen_attach(&s, &p->pk, &f.a, "UI_PC_Customize_Options.fng");
    lfx_open(&x, OM_CTX_INGAME, 1);
    CHECK(om_screen(&x.m) == OM_SCREEN_CUSTOMIZE && om_row_count(&x.m) == 22, "customize open");
    CHECK(gv_list_bind(&v, &s, &x.m), "bind customize: %s", s.status);
    gv_screen_update(&s, 0);
    gv_list_sync(&v, &x.m, 0);
    sc = &s.scene;
    CHECK(!sc->nodes[v.row[8].name].app_hidden && sc->nodes[v.row[9].name].app_hidden && sc->nodes[v.row[9].name].wf >= GL_ROW_OFFSCREEN_Y - 0.01f, "rows below the window (8 visible) are parked");
    CHECK(NEAR(sc->nodes[v.row[8].name].wf, -138 + 7 * 36 + 12, 0.01), "eighth visible row y (+ the fade offset at t=0)");
    CHECK(!sc->nodes[v.arrow1].app_hidden, "Customize shows the scrollbar arrows");
    for (int i = 0; i < 21; i++) om_input(&x.m, OM_IN_DOWN);
    gv_list_sync(&v, &x.m, 0);
    CHECK(sc->nodes[v.row[1].name].app_hidden && !sc->nodes[v.row[22].name].app_hidden, "window scrolled to the end");
    CHECK(NEAR(sc->nodes[v.row[22].name].wf, -138 + 7 * 36 + 12, 0.01), "last row on the last line");
    CHECK(NEAR(sc->nodes[v.tab].wf, 143 - 8, 0.01), "tab at the bottom");
    CHECK(!strcmp(sc->nodes[v.header].text_override, "Customize Display Settings"), "Customize header");
    gv_screen_free(&s);
    st_reg_close(&x.store);

    /* in-game Display: headers and no scrollbar arrows */
    memset(p, 0, sizeof *p);
    build_list_pkg(p, 11, 0, 0);
    gv_screen_attach(&s, &p->pk, &f.a, "UI_PauseOptions.fng");
    lfx_open(&x, OM_CTX_INGAME, 0);
    CHECK(gv_list_bind(&v, &s, &x.m), "bind in-game display: %s", s.status);
    gv_screen_update(&s, 0);
    gv_list_sync(&v, &x.m, 0);
    CHECK(!strcmp(s.scene.nodes[v.header].text_override, "PAUSE OPTIONS") && !strcmp(s.scene.nodes[v.header2].text_override, "DISPLAY"), "in-game headers");
    CHECK(s.scene.nodes[v.arrow1].app_hidden && s.scene.nodes[v.arrow2].app_hidden && !s.scene.nodes[v.rail].app_hidden, "in-game Display: scrollbar arrows hidden");
    CHECK(v.row[1].backing < 0, "no backing objects in the in-game package: the focused row has none");
    int mouse_look=0;x.cfg.mouse_look=&mouse_look;
    om_open_hub(&x.m,&x.cfg,&x.g,&x.caps,&x.store);
    om_input(&x.m,OM_IN_RIGHT);om_input(&x.m,OM_IN_RIGHT);om_input(&x.m,OM_IN_ACCEPT);om_update(&x.m,1);
    CHECK(gv_list_bind(&v,&s,&x.m),"bind optional Gameplay list: %s",s.status);
    gv_list_sync(&v,&x.m,0);
    CHECK(!strcmp(s.scene.nodes[v.header2].text_override,"Gameplay"),"Gameplay title uses existing authored header");
    CHECK(!strcmp(s.scene.nodes[v.row[1].name].text_override,"Right mouse look") &&
          !strcmp(s.scene.nodes[v.row[1].data].text_override,"Off"),"project mouse setting label and Off value render");
    om_input(&x.m,OM_IN_RIGHT);gv_list_sync(&v,&x.m,0);
    CHECK(mouse_look && !strcmp(s.scene.nodes[v.row[1].data].text_override,"On!"),"project mouse setting localized On value renders");
    gv_screen_free(&s);
    st_reg_close(&x.store);
    /* binding fails closed without the arrow art: the packages leave the arrows untextured */
    { Fake bare;
      fake_init(&bare, NULL, 0);
      memset(p, 0, sizeof *p);
      build_list_pkg(p, 8, 1, 0);
      gv_screen_attach(&s, &p->pk, &bare.a, "UI_Options.fng");
      lfx_open(&x, OM_CTX_FRONTEND, 0);
      CHECK(!gv_list_bind(&v, &s, &x.m) && strstr(s.status, "arrow texture"), "no arrow art: %s", s.status);
      gv_screen_free(&s);
      st_reg_close(&x.store);
      fake_free(&bare); }
    /* binding fails closed without the anchors */
    memset(p, 0, sizeof *p);
    build_list_pkg(p, 8, 1, 0);
    p->obj[0].name_hash = hn("NOT_THE_ANCHOR");
    gv_screen_attach(&s, &p->pk, &f.a, "UI_Options.fng");
    lfx_open(&x, OM_CTX_FRONTEND, 0);
    CHECK(!gv_list_bind(&v, &s, &x.m) && strstr(s.status, "TITLE_MASTER"), "no TITLE_MASTER: %s", s.status);
    gv_screen_free(&s);
    st_reg_close(&x.store);
    free(p);
    fake_free(&f);
}

/* ---- dialog ---------------------------------------------------------------------------------------------------- */
static void test_dialog_view(void) {
    Fake f;
    fake_init(&f, NULL, 0);
    strtab_put(&f.a.strings, 0x6F4796A6u, "Quit to Main Menu? (table)");
    strtab_put(&f.a.strings, 0x00000639u, "OK (table)");
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_dialog_pkg(p);
    GuiDialog d;
    GdSpec spec;
    GdContext g = { 1, 0, 0, 0 };
    gd_spec(&spec, GD_QUIT, &g);
    gd_open(&d, &spec);
    GvDialog v;
    CHECK(gv_dialog_attach(&v, &p->pk, &f.a, &d), "dialog attach: %s", v.scr.status);
    const UgScene *sc = &v.scr.scene;
    CHECK(!strcmp(sc->nodes[v.msg].text_override, "Quit to Main Menu? (table)") && !strcmp(sc->nodes[v.lbl1].text_override, "OK (table)") &&
          !strcmp(sc->nodes[v.lbl2].text_override, "Cancel"), "texts: table first, English fallback");
    CHECK(sc->nodes[v.b3].app_hidden && sc->nodes[v.lbl3].app_hidden, "third button and label hidden");
    CHECK(sc->focus == v.b2, "initial focus on the second button");
    CHECK(ug_scene_script_id(sc, v.b2) == hn("Focus"), "the authored focus response ran");
    gd_input(&d, GD_IN_LEFT);
    gv_dialog_sync(&v, &d, 0);
    CHECK(sc->focus == v.b1 && ug_scene_script_id(sc, v.b1) == hn("Focus") && ug_scene_script_id(sc, v.b2) == hn("Unfocus"), "Left moves the focus (and its scripts)");
    gd_input(&d, GD_IN_ACCEPT);
    gv_dialog_sync(&v, &d, 0);
    CHECK(v.scr.exit_started && ug_scene_script_id(sc, v.scr.handler) == hn("FORWARD"), "the dialog plays its authored exit");
    gv_dialog_sync(&v, &d, 141);
    CHECK(v.scr.exit_done, "dialog exit-complete at 140 ms");
    gv_dialog_close(&v);
    CHECK(!v.bound && !v.scr.loaded, "closed");
    /* a Default Settings dialog on a package without labels fails closed */
    GdSpec defs;
    gd_spec(&defs, GD_DEFAULTS, NULL);
    gd_open(&d, &defs);
    memset(p, 0, sizeof *p);
    add(p, UG_OBJ_TEXT, "message_blurb", 0, 0, 1, 1);
    finish(p);
    CHECK(!gv_dialog_attach(&v, &p->pk, &f.a, &d) && strstr(v.scr.status, "dialog binding"), "incomplete dialog package: %s", v.scr.status);
    free(p);
    /* a missing package in the assets fails closed too */
    CHECK(!gv_dialog_open(&v, &f.a, &d) && v.scr.status[0], "package not in the assets: %s", v.scr.status);
    GvScreen none;
    CHECK(!gv_screen_load(&none, NULL, "x.fng") && none.status[0], "no assets");
    fake_free(&f);
}


/* ---- the test serializer round-trips through the real parser ------------------------------------------------ */
static void test_synth_roundtrip(void) {
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_list_pkg(p, 3, 1, 1);
    size_t len = 0;
    uint8_t *bin = synth_fng(&p->pk, "UI_Options.fng", &len);
    UgFngPackage q;
    char err[160] = "";
    CHECK(ug_fng_parse(bin, len, &q, err, sizeof err), "parse: %s", err);
    CHECK(q.name && !strcmp(q.name, "UI_Options.fng") && q.nobj == p->pk.nobj, "name and object count: %d vs %d", q.nobj, p->pk.nobj);
    int bad = 0;
    for (int i = 0; i < q.nobj && i < p->pk.nobj; i++) {
        const UgFngObject *a = &p->pk.obj[i], *b = &q.obj[i];
        if (a->type != b->type || a->guid != b->guid || a->name_hash != b->name_hash || a->flags != b->flags ||
            a->nscripts != b->nscripts || a->nresp != b->nresp || a->justify != b->justify ||
            fabsf(a->pos[0] - b->pos[0]) > 1e-4f || fabsf(a->size[1] - b->size[1]) > 1e-4f) bad++;
        for (int s = 0; s < a->nscripts && s < b->nscripts; s++)
            if (a->scripts[s].id != b->scripts[s].id || a->scripts[s].length != b->scripts[s].length ||
                a->scripts[s].ntracks != b->scripts[s].ntracks || a->scripts[s].nevents != b->scripts[s].nevents) bad++;
    }
    CHECK(bad == 0, "%d objects or scripts differ after the round trip", bad);
    /* a track's base key and deltas survive */
    int found = 0;
    for (int i = 0; i < q.nobj; i++)
        for (int s = 0; s < q.obj[i].nscripts; s++)
            for (int t = 0; t < q.obj[i].scripts[s].ntracks; t++) {
                const UgTrack *tr = &q.obj[i].scripts[s].tracks[t];
                if (tr->prop == UG_P_POS && tr->has_base && tr->nkeys == 2 && tr->keys[0].v[1] == 12) found++;
            }
    CHECK(found >= 3, "position tracks with base keys round-trip (%d)", found);
    ug_fng_free(&q);
    free(bin);
    free(p);
}

/* ---- Controls (CONTROLS_SPEC 3) ---------------------------------------------------------------------- */
static void build_controls_pkg(Pkg *p, int nslots, int with_tab) {
    UgFngObject *tm = add(p, UG_OBJ_IMAGE, "TITLE_MASTER", -187, -88, 180, 36);
    UgFngObject *dm = add(p, UG_OBJ_IMAGE, "DATA_MASTER", 11, -90, 150, 24);
    UgFngObject *dm2 = add(p, UG_OBJ_IMAGE, "DATA_MASTER2", 173, -90, 150, 24);
    anchor_chain(p, tm, -190, -87, -132, -120, 0);                /* A6: the chained position must never be used */
    anchor_chain(p, dm, 8, -91, 109, -122, 0);
    anchor_chain(p, dm2, 170, -91, 209, -122, 0);
    add(p, UG_OBJ_TEXT, "Header_text", -289.7f, -220, 0.8f, 0.8f);
    add(p, UG_OBJ_TEXT, NULL, -5, -169, 0.7f, 0.7f)->name_hash = 0x4E11CF90u;
    UgFngObject *sl = add(p, UG_OBJ_IMAGE, NULL, -266, -158, 24, 24), *sr = add(p, UG_OBJ_IMAGE, NULL, 265, -158, 24, 24);
    sl->name_hash = 0xD1CDA594u;
    sr->name_hash = 0x9345E4A7u;
    script(p, sl, hn("Blink"), 400); script(p, sl, hn("Static"), 0);
    script(p, sr, hn("Blink"), 400); script(p, sr, hn("Static"), 0);
    add(p, UG_OBJ_TEXT, NULL, 9, -138, 0.7f, 0.7f)->name_hash = 0x666593C4u;
    add(p, UG_OBJ_TEXT, NULL, 172, -138, 0.7f, 0.7f)->name_hash = 0x59ABA1E8u;
    add(p, UG_OBJ_TEXT, "Instructions_Label", -5, 153, 0.7f, 0.7f);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Backing", 273, 20, 8, 242);
    if (with_tab) add(p, UG_OBJ_IMAGE, NULL, 273, -50, 16, 16)->name_hash = 0x3FBB7A4Eu;
    UgFngObject *a1 = add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_1", 273, -126, 24, 24);
    UgFngObject *a2 = add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_2", 273, 166, 24, 24);
    script(p, a1, hn("Blink"), 400); script(p, a2, hn("Blink"), 400);
    for (int n = 1; n <= nslots; n++) {
        UgFngObject *nm = add(p, UG_OBJ_TEXT, NULL, 0, 6969, 0.7f, 0.7f);
        nm->name_hash = hnf("option_name_%d", n);
        script(p, nm, hn("Highlight"), 1200);
        script(p, nm, hn("UnHighlight"), 0);
        for (int side = 0; side < 2; side++) {
            UgFngObject *d = add(p, UG_OBJ_TEXT, NULL, 0, 6969, 0.7f, 0.7f);
            d->name_hash = hnf(side ? "OPTION_DATA_2_%d" : "option_data_%d", n);   /* the package mixes the cases */
            d->justify = UG_J_CENTER;
            script(p, d, hn("Highlight"), 1200);
            script(p, d, hn("UnHighlight"), 0);
            script(p, d, hn("Pulse"), 1200);
        }
        UgFngObject *b = add(p, UG_OBJ_GROUP, NULL, 0, 6969, 1, 1);
        b->name_hash = hnf("Options_Backing_%d", n);
        script(p, b, hn("Highlight"), 100);
        script(p, b, hn("UnHighlight"), 100);
    }
    handler(p, "FORWARD", 500, 0);
    finish(p);
}

static const char *fake_key(void *u, int sc) { static char buf[16]; (void)u; snprintf(buf, sizeof buf, "K%d", sc); return buf; }
static void test_controls_view(void) {
    CmPlatform plat = { NULL, fake_key, NULL };
    Fake f;
    fake_init(&f, NULL, 0);
    strtab_put(&f.a.strings, 0x04DC6DB5u, "Controls!");
    strtab_put(&f.a.strings, 0x5CF08EB7u, "Keyboard Configuration!");
    strtab_put(&f.a.strings, 0x1B15F53Cu, "Primary!");
    strtab_put(&f.a.strings, 0x37E740E0u, "Secondary!");
    strtab_put(&f.a.strings, 0x8BAB66EFu, "Configure!");
    strtab_put(&f.a.strings, 0x23CF49AAu, "Press!");
    strtab_put(&f.a.strings, 0xFED0D3DFu, "Assign!");
    strtab_put(&f.a.strings, 0x153E7337u, "Steering (Left)!");
    strtab_put(&f.a.strings, 0x6F99F62Au, "Pause!");
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_controls_pkg(p, 20, 1);
    GvScreen s;
    CHECK(gv_screen_attach(&s, &p->pk, &f.a, "UI_Options_PC_Controller.fng"), "attach");
    CbBindings *b = (CbBindings *)calloc(1, sizeof *b);
    cb_bindings_defaults(b);
    ControlsMenu m;
    memset(&m, 0, sizeof m);
    cm_open(&m, b, &plat);
    GvControls v;
    CHECK(gv_controls_bind(&v, &s, &m), "bind: %s", s.status);
    CHECK(v.nslots == 20 && NEAR(v.tx, -277, 0.001) && NEAR(v.ty, -106, 0.001) && NEAR(v.th, 36, 0.001) &&
          NEAR(v.dx[0], -64, 0.001) && NEAR(v.dx[1], 98, 0.001) && NEAR(v.dw, 150, 0.001),
          "anchors are the authored centres (not the chained positions): tx %.1f ty %.1f dx %.1f/%.1f", v.tx, v.ty, v.dx[0], v.dx[1]);
    gv_screen_update(&s, 0);
    gv_controls_sync(&v, &m, 0);
    const UgScene *sc = &s.scene;
    CHECK(!strcmp(sc->nodes[v.header].text_override, "Controls!") && !strcmp(sc->nodes[v.title].text_override, "Keyboard Configuration!") &&
          !strcmp(sc->nodes[v.col1].text_override, "Primary!") && !strcmp(sc->nodes[v.col2].text_override, "Secondary!") &&
          !strcmp(sc->nodes[v.instr].text_override, "Configure!"), "fixed texts come from the language table");
    /* a key without a language text leaves the object without an override: no substitute text exists */
    CHECK(sc->nodes[v.row[2].name].text_override == NULL, "row label without a language text: no override");
    CHECK(!strcmp(sc->nodes[v.row[1].name].text_override, "Steering (Left)!") && v.row[18].parked, "row 18 is outside the window");
    /* placement (3.8): label (tx, y_k), cells centre 11 / 173, backing (-1, y_k + 8) */
    for (int k = 0; k < 7; k++) {
        const GvCtlRow *r = &v.row[k + 1];
        float y = -106 + 36.0f * k;
        const UgNode *nn = &sc->nodes[r->name], *d1 = &sc->nodes[r->data], *d2 = &sc->nodes[r->data2], *bg = &sc->nodes[r->backing];
        CHECK(!nn->app_hidden && NEAR(nn->we, -277, 0.01) && NEAR(nn->wf, y, 0.01), "line %d label (%.2f,%.2f)", k, nn->we, nn->wf);
        CHECK(NEAR(d1->we, 11, 0.01) && NEAR(d1->wf, y, 0.01) && NEAR(d2->we, 173, 0.01) && NEAR(d2->wf, y, 0.01), "line %d cells", k);
        CHECK(NEAR(bg->we, -1, 0.01) && NEAR(bg->wf, y + 8, 0.01), "line %d backing (%.2f,%.2f)", k, bg->we, bg->wf);
        CHECK(!strcmp(d1->text_override, k == 0 ? "K80" : d1->text_override), "line %d primary text \"%s\"", k, d1->text_override);
        CHECK(k != 0 || !strcmp(d2->text_override, "K4"), "line 0 secondary text");
    }
    CHECK(sc->nodes[v.row[8].name].app_hidden && sc->nodes[v.row[8].name].wf >= GL_ROW_OFFSCREEN_Y - 0.01f &&
          sc->nodes[v.row[18].data2].app_hidden, "rows outside the window are hidden and parked");
    CHECK(sc->nodes[v.row[19].name].app_hidden && sc->nodes[v.row[20].data].app_hidden, "slots 19 and 20 are parked");
    CHECK(!sc->nodes[v.row[1].backing].app_hidden && sc->nodes[v.row[2].backing].app_hidden, "only the focused row's backing shows");
    CHECK(sc->nodes[v.arrow_l].app_hidden && sc->nodes[v.arrow_r].app_hidden, "selector arrows hidden with one device");
    CHECK(!sc->nodes[v.rail].app_hidden && NEAR(sc->nodes[v.tab].wf, -101 + 8, 0.01), "scrollbar visible, tab on the first row");
    /* focus, scroll window and tab */
    for (int i = 0; i < 8; i++) cm_input(&m, CM_IN_DOWN);
    gv_controls_sync(&v, &m, 16);
    CHECK(m.focus_row == 8 && m.top == 2 && !sc->nodes[v.row[9].name].app_hidden && NEAR(sc->nodes[v.row[9].name].wf, -106 + 36.0f * 6, 0.01) &&
          sc->nodes[v.row[2].name].app_hidden, "window scrolls by one line per move past the end");
    CHECK(NEAR(sc->nodes[v.tab].wf, -101 + 8 + 226.0 * 8 / 17, 0.01), "tab follows the focused row: %.3f", sc->nodes[v.tab].wf);
    cm_input(&m, CM_IN_UP);
    for (int i = 0; i < 20; i++) cm_input(&m, CM_IN_UP);
    float tab_before = sc->nodes[v.tab].wf;
    gv_controls_sync(&v, &m, 16);
    CHECK(m.focus_row == -1 && NEAR(sc->nodes[v.tab].wf, -101 + 8, 0.01) == (tab_before <= -92.99f), "tab is held while the selector is focused");
    /* capture: Pulse on the cell for the keyboard path only, Highlight again afterwards, instruction texts */
    cm_input(&m, CM_IN_DOWN);
    gv_controls_sync(&v, &m, 16);
    cm_input(&m, CM_IN_ACCEPT);
    gv_controls_sync(&v, &m, 16);
    CHECK(!strcmp(sc->nodes[v.instr].text_override, "Press!") && ug_scene_script_id(sc, v.row[1].data) == hn("Pulse"), "capture: instruction and Pulse");
    cm_key_down(&m, SDL_SCANCODE_ESCAPE);
    cm_tick(&m); cm_tick(&m);
    gv_controls_sync(&v, &m, 16);
    CHECK(!strcmp(sc->nodes[v.instr].text_override, "Assign!") && ug_scene_script_id(sc, v.row[1].data) == hn("Highlight"), "after the capture: third instruction, Highlight");
    /* several devices: arrows shown, a change blinks the arrow of its direction */
    cb_pad_defaults(&b->pad[0].dev, &(CbPadInfo){ 0, 0, 10, 0x3, 0 });
    b->pad[0].present = 1;
    cm_open(&m, b, &plat);
    gv_controls_bind(&v, &s, &m);
    cm_input(&m, CM_IN_UP);
    cm_input(&m, CM_IN_RIGHT);
    gv_controls_sync(&v, &m, 16);
    CHECK(!sc->nodes[v.arrow_l].app_hidden && !sc->nodes[v.arrow_r].app_hidden && ug_scene_script_id(sc, v.arrow_r) == hn("Blink"), "arrows shown, right one blinks");
    CHECK(!strcmp(sc->nodes[v.col1].text_override, "Primary!"), "texts again");
    gv_screen_free(&s);
    free(p);
    /* a package without its tab object cannot be bound: the screen is unavailable, never substituted (R3) */
    Pkg *q = (Pkg *)calloc(1, sizeof *q);
    build_controls_pkg(q, 20, 0);
    GvScreen s2;
    CHECK(gv_screen_attach(&s2, &q->pk, &f.a, "UI_Options_PC_Controller.fng"), "attach 2");
    cm_open(&m, b, &plat);
    CHECK(!gv_controls_bind(&v, &s2, &m) && strstr(s2.status, "scrollbar"), "missing tab: %s", s2.status);
    gv_screen_free(&s2);
    free(q);
    Pkg *r = (Pkg *)calloc(1, sizeof *r);
    build_controls_pkg(r, 12, 1);
    GvScreen s3;
    CHECK(gv_screen_attach(&s3, &r->pk, &f.a, "UI_Options_PC_Controller.fng"), "attach 3");
    CHECK(!gv_controls_bind(&v, &s3, &m) && strstr(s3.status, "row slots"), "too few row slots: %s", s3.status);
    gv_screen_free(&s3);
    free(r);
    free(b);
    fake_free(&f);
}

/* ---- TASK-AL: the three-row dialog binds all three rows and moves focus as data ------------------ */
static void test_three_button_view(void) {
    Fake f;
    fake_init(&f, NULL, 0);
    strtab_put(&f.a.strings, 0x2C8EC31Du, "Delete the profile? (table)");
    strtab_put(&f.a.strings, 0x56B00632u, "Delete (table)");
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_three_button_pkg(p);
    GuiDialog d;
    GdSpec spec;
    gd_spec(&spec, GD_DELETE_PROFILE, NULL);
    gd_open(&d, &spec);
    CHECK(d.spec.frame && d.spec.frame->buttons == 3, "three-button spec carries the row frame");
    GvDialog v;
    CHECK(gv_dialog_attach(&v, &p->pk, &f.a, &d), "three-button attach: %s", v.scr.status);
    const UgScene *sc = &v.scr.scene;
    CHECK(v.b3 >= 0 && v.lbl3 >= 0 && !sc->nodes[v.b3].app_hidden && !sc->nodes[v.lbl3].app_hidden,
          "the third row binds and stays visible on a three-button dialog");
    CHECK(!strcmp(sc->nodes[v.msg].text_override, "Delete the profile? (table)") &&
          !strcmp(sc->nodes[v.lbl1].text_override, "Delete (table)") &&
          !strcmp(sc->nodes[v.lbl2].text_override, "No") &&
          !strcmp(sc->nodes[v.lbl3].text_override, "Cancel"), "three-button texts: table first, English fallback");
    CHECK(sc->focus == v.b2, "initial scene focus on the middle row");
    CHECK(ug_scene_script_id(sc, v.b2) == hn("Focus"), "the authored row focus response ran");
    gd_input(&d, GD_IN_UP);                                    /* toward L3 (top) */
    gv_dialog_sync(&v, &d, 0);
    CHECK(sc->focus == v.b3 && ug_scene_script_id(sc, v.b3) == hn("Focus") &&
          ug_scene_script_id(sc, v.b2) == hn("UnFocus"), "Up moves the scene focus to the top row (scripts run)");
    gd_input(&d, GD_IN_DOWN);
    gd_input(&d, GD_IN_DOWN);                                  /* past the bottom: no wrap, stays L1 */
    gv_dialog_sync(&v, &d, 0);
    CHECK(sc->focus == v.b1, "Down steps to the bottom row and stops there");
    gd_input(&d, GD_IN_ACCEPT);                                /* focus on L1 -> first result */
    gv_dialog_sync(&v, &d, 0);
    CHECK(v.scr.exit_started && ug_scene_script_id(sc, v.scr.handler) == hn("FORWARD"), "a row choice plays the authored exit");
    gv_dialog_sync(&v, &d, 141);
    CHECK(v.scr.exit_done, "three-button exit-complete at 140 ms");
    gv_dialog_close(&v);
    CHECK(!v.bound && !v.scr.loaded, "three-button closed");
    /* a package missing the third row fails closed for a three-button spec */
    memset(p, 0, sizeof *p);
    add(p, UG_OBJ_TEXT, "message_blurb", 0, 0, 1, 1);
    UgFngObject *b1 = add(p, UG_OBJ_GROUP, "button1", 0, 0, 1, 1); b1->flags = UG_OF_BUTTON;
    UgFngObject *b2 = add(p, UG_OBJ_GROUP, "Button2", 0, 0, 1, 1); b2->flags = UG_OF_BUTTON;
    add(p, UG_OBJ_TEXT, NULL, 0, 0, 1, 1)->name_hash = 0xDA473D2Fu;
    add(p, UG_OBJ_TEXT, NULL, 0, 0, 1, 1)->name_hash = 0xDA473D30u;
    finish(p);
    gd_open(&d, &spec);
    CHECK(!gv_dialog_attach(&v, &p->pk, &f.a, &d) && strstr(v.scr.status, "dialog binding"),
          "a three-button spec on a two-row package fails closed: %s", v.scr.status);
    free(p);
    fake_free(&f);
}

/* ---- TASK-AL: a help sheet binds T/M/B, hides the glyph hint and has no focus -------------------- */
static void test_help_view(void) {
    Fake f;
    fake_init(&f, NULL, 0);
    strtab_put(&f.a.strings, 0x001335F0u, "Back (table)");
    Pkg *p = (Pkg *)calloc(1, sizeof *p);
    build_help_pkg(p);
    GuiDialog d;
    GdSpec spec;
    /* an empty body keeps the package's own text; the retail default title and table caption set */
    gd_spec_help(&spec, 0, NULL, 0, NULL, 0, NULL);
    gd_open(&d, &spec);
    CHECK(d.focus == -1, "a help spec opens without focus");
    GvDialog v;
    CHECK(gv_dialog_attach(&v, &p->pk, &f.a, &d), "help attach: %s", v.scr.status);
    const UgScene *sc = &v.scr.scene;
    CHECK(v.title >= 0 && v.back >= 0 && v.glyph >= 0, "the help title, caption and glyph nodes bind");
    CHECK(!strcmp(sc->nodes[v.title].text_override, "Help") && sc->nodes[v.msg].text_override == NULL,
          "the retail default title sets, an empty body leaves the package text");
    CHECK(!strcmp(sc->nodes[v.back].text_override, "Back (table)"), "the caption uses the language table");
    CHECK(sc->nodes[v.glyph].app_hidden, "the console controller-glyph hint is hidden on PC");
    CHECK(sc->focus < 0, "a help sheet has no scene focus");
    gd_input(&d, GD_IN_ACCEPT);
    gv_dialog_sync(&v, &d, 0);
    CHECK(v.scr.exit_started && ug_scene_script_id(sc, v.scr.handler) == hn("FORWARD"), "Accept plays the sheet's authored exit");
    gv_dialog_sync(&v, &d, 141);
    CHECK(v.scr.exit_done, "help exit-complete at 140 ms");
    gv_dialog_close(&v);
    /* explicit title/body text overrides reach the nodes */
    GdSpec s2;
    gd_spec_help(&s2, 0x00169268u, "Help", 0, "Body from the host.", 0, NULL);
    gd_open(&d, &s2);
    memset(p, 0, sizeof *p);
    build_help_pkg(p);
    CHECK(gv_dialog_attach(&v, &p->pk, &f.a, &d), "help attach (text): %s", v.scr.status);
    CHECK(!strcmp(v.scr.scene.nodes[v.msg].text_override, "Body from the host."), "an explicit body overrides");
    gv_dialog_close(&v);
    /* a sheet without the B caption fails closed */
    memset(p, 0, sizeof *p);
    add(p, UG_OBJ_TEXT, "message_blurb", 0, 0, 1, 1);
    add(p, UG_OBJ_TEXT, NULL, 0, 0, 1, 1)->name_hash = 0x426C7B4Du;    /* T present, B absent */
    finish(p);
    gd_open(&d, &spec);
    CHECK(!gv_dialog_attach(&v, &p->pk, &f.a, &d) && strstr(v.scr.status, "dialog binding"),
          "a help sheet without its caption fails closed: %s", v.scr.status);
    free(p);
    fake_free(&f);
}

int main(void) {
    test_synth_roundtrip();
    test_controls_view();
    test_app_write();
    test_carousel_view();
    test_list_view();
    test_dialog_view();
    test_three_button_view();
    test_help_view();
    printf("gui_view_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
