/* gui_art_test -- the pure (GL-free, SDL-free) building blocks of the authored-art screens against
 * docs/cleanroom-settings/test-vectors-gui tvg07..tvg11 (SPEC_GUI_ART), plus behaviour tests of the
 * state machines around them.
 *
 *   gui_art_test [VECTOR_DIR]
 *
 * Vector files are read at run time with the JSON reader of test_json.h. Synthetic data only; no
 * retail package, texture or text is opened. Exit code is nonzero if any check fails. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/gui_carousel.h"
#include "../src/gui_dialog.h"
#include "../src/gui_layout.h"
#include "../src/gui_timing.h"
#include "../src/pause_entries.h"
#include "../src/pause_screen.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    if (g_fail <= 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

static const char *g_dir = "docs/cleanroom-settings/test-vectors-gui";
#include "test_json.h"

static uint32_t hash_name(const char *s) {
    uint32_t h = 0xFFFFFFFFu;
    for (; *s; s++) h = h * 33u + (uint32_t)((*s >= 'a' && *s <= 'z') ? *s - 32 : *s);
    return h;
}

/* ---- tvg07: pause entry catalogue ---------------------------------------------------------- */
static void test_tvg07(void) {
    J *j = jload("tvg07_pause_entries.json");
    if (!j) return;
    J *cases = jget(j, "cases");
    for (int ci = 0; ci < cases->n; ci++) {
        J *c = cases->kid[ci], *in = jget(c, "input"), *entries = jget(c, "entries");
        const char *name = jstrv(c, "name");
        PauseContext ctx;
        memset(&ctx, 0, sizeof ctx);
        ctx.mode = hexkey(jstrv(in, "mode"));
        ctx.race = (int)jnum(in, "race_state", 0);
        ctx.from_map = jbool(in, "from_map");
        ctx.session = (int)jnum(in, "session", 0);
        ctx.options_ok = jbool(in, "options_ok");
        ctx.garage_flag = jbool(in, "garage_flag");
        PauseEntry e[PE_MAX];
        int n = pe_build(&ctx, e);
        CHECK(n == entries->n, "%s: %d entries, expected %d", name, n, entries->n);
        CHECK((int)jnum(c, "initial_focus_slot", 0) == 1, "%s: vector initial focus", name);
        for (int i = 0; i < n && i < entries->n; i++) {
            J *x = entries->kid[i];
            char slot[16];
            snprintf(slot, sizeof slot, "option_%d", i + 1);
            CHECK(!strcmp(jstrv(x, "slot"), slot), "%s: slot name %s", name, jstrv(x, "slot"));
            CHECK(!strcmp(pe_id_name(e[i].id), jstrv(x, "id")), "%s #%d: id %s vs %s", name, i + 1, pe_id_name(e[i].id), jstrv(x, "id"));
            CHECK(e[i].icon_key == hexkey(jstrv(x, "icon_key")), "%s #%d: icon key %08X", name, i + 1, e[i].icon_key);
            CHECK(e[i].icon_name && !strcmp(e[i].icon_name, jstrv(x, "icon_name")), "%s #%d: icon name %s", name, i + 1, e[i].icon_name ? e[i].icon_name : "(null)");
            CHECK(e[i].label_key == hexkey(jstrv(x, "label_key")), "%s #%d: label key %08X", name, i + 1, e[i].label_key);
            CHECK(e[i].label && !strcmp(e[i].label, jstrv(x, "english")), "%s #%d: label %s", name, i + 1, e[i].label ? e[i].label : "(null)");
            CHECK(!strcmp(pe_action_name(e[i].action), jstrv(x, "action")), "%s #%d: action %s vs %s", name, i + 1, pe_action_name(e[i].action), jstrv(x, "action"));
            CHECK(e[i].immediate == !strcmp(jstrv(x, "timing"), "immediate"), "%s #%d: timing", name, i + 1);
            CHECK(e[i].needs_replay == jbool(x, "disabled_unless_replay_available"), "%s #%d: replay gate", name, i + 1);
            CHECK(e[i].disabled == e[i].needs_replay, "%s #%d: disabled without replay data", name, i + 1);
        }
        /* with replay data the Replay entry is focusable */
        ctx.replay_available = 1;
        n = pe_build(&ctx, e);
        for (int i = 0; i < n; i++) CHECK(!e[i].disabled, "%s #%d: disabled with replay data", name, i + 1);
    }
    jfree(j);

    /* the garage entry exists only with its (never set) flag, career mode */
    PauseContext ctx;
    pe_context_default(&ctx);
    PauseEntry e[PE_MAX];
    int n = pe_build(&ctx, e);
    CHECK(n == 4 && e[0].id == PE_EXPLORE_FREE && e[3].id == PE_QUIT_MAIN, "default context is the career explore pause");
    ctx.garage_flag = 1;
    n = pe_build(&ctx, e);
    CHECK(n == 5 && e[1].id == PE_GARAGE && e[1].icon_key == 0xB8038AC0u && e[1].label_key == 0x453890E1u &&
          e[1].action == PE_ACT_CONFIRM_GARAGE && e[1].immediate, "garage entry with flag in career mode");
    ctx.mode = 2;
    n = pe_build(&ctx, e);
    CHECK(n == 4, "no garage entry outside career mode");
    /* an invalid context still yields a bounded list */
    PauseContext crazy = { 0xFFFFFFFFu, -5, 1, 99, 1, 1, 1, 1, 1 };
    n = pe_build(&crazy, e);
    CHECK(n >= 1 && n <= PE_MAX, "bounded list for odd context values");
    CHECK(pe_build(NULL, e) == 0, "NULL context");
}

/* ---- tvg08: icon carousel -------------------------------------------------------------------- */
static void car_for(GuiCarousel *c, const char *package, int n, int cur1) {
    gc_init(c, !strcmp(package, "UI_PauseOptionsMain.fng") ? GC_RAIL_HUB : GC_RAIL_PAUSE);
    gc_set_entries(c, n, NULL);
    gc_snap_to(c, cur1 - 1);
}
static void check_carousel_case(const char *tag, GuiCarousel *c, const J *x, int check_visible) {
    J *items = jget(x, "items");
    for (int i = 0; i < items->n && i < c->n; i++) {
        J *it = items->kid[i], *ct = jget(it, "centre"), *sz = jget(it, "size");
        GcSlot s = gc_slot(c, i);
        CHECK(NEAR(s.cx, ct->kid[0]->num, 0.002) && NEAR(s.cy, ct->kid[1]->num, 0.002),
              "%s slot %d centre %.3f,%.3f vs %.3f,%.3f", tag, i + 1, s.cx, s.cy, ct->kid[0]->num, ct->kid[1]->num);
        CHECK(NEAR(s.w, sz->kid[0]->num, 0.002) && NEAR(s.h, sz->kid[1]->num, 0.002),
              "%s slot %d size %.3f,%.3f", tag, i + 1, s.w, s.h);
        if (check_visible) CHECK(s.visible == jbool(it, "visible"), "%s slot %d visible %d", tag, i + 1, s.visible);
    }
    J *tc = jget(x, "tab_centre_x");
    CHECK(gc_tab_visible(c) == jbool(x, "tab_visible"), "%s tab visible", tag);
    if (tc && tc->t == J_NUM) CHECK(NEAR(gc_tab_x(c), tc->num, 0.002), "%s tab x %.3f vs %.3f", tag, gc_tab_x(c), tc->num);
    CHECK(gc_left_arrow_visible(c) == jbool(x, "left_arrow_visible"), "%s left arrow", tag);
    CHECK(gc_right_arrow_visible(c) == jbool(x, "right_arrow_visible"), "%s right arrow", tag);
}
static void test_tvg08(void) {
    J *j = jload("tvg08_icon_carousel.json");
    if (!j) return;
    J *consts = jget(j, "constants");
    for (int k = 0; k < consts->n; k++) {
        J *cn = consts->kid[k];
        CHECK(NEAR(jget(cn, "pitch")->num, GC_PITCH, 0) && NEAR(jget(cn, "window_half_width")->num, GC_WINDOW, 0), "%s constants", consts->key[k]);
        CHECK(NEAR(jget(cn, "item_size")->kid[0]->num, GC_ITEM_W, 0) && NEAR(jget(cn, "item_size")->kid[1]->num, GC_ITEM_H, 0), "item size");
        CHECK(NEAR(jget(cn, "ScrollBar_Backing_size")->kid[0]->num, !strcmp(consts->key[k], "UI_PauseOptionsMain.fng") ? GC_RAIL_HUB : GC_RAIL_PAUSE, 0), "rail width");
    }
    J *rest = jget(j, "rest");
    for (int ci = 0; ci < rest->n; ci++) {
        J *x = rest->kid[ci];
        const char *pk = jstrv(x, "package");
        int n = (int)jnum(x, "N", 0), cur = (int)jnum(x, "current", 0);
        GuiCarousel c;
        car_for(&c, pk, n, cur);
        char tag[96];
        snprintf(tag, sizeof tag, "rest %s N=%d c=%d", pk, n, cur);
        CHECK(NEAR(gc_scroll_value(&c), jnum(x, "scroll_offset", 1e9), 0.0005), "%s: scroll %.3f", tag, c.scroll);
        check_carousel_case(tag, &c, x, 1);
    }
    J *tween = jget(j, "tween");
    for (int ci = 0; ci < tween->n; ci++) {
        J *x = tween->kid[ci], *tw = jget(x, "tween");
        int n = (int)jnum(x, "N", 0), from = (int)jnum(tw, "from_current", 0), to = (int)jnum(tw, "to_current", 0);
        double el = jnum(tw, "elapsed_s", 0);
        GuiCarousel c;
        car_for(&c, jstrv(x, "package"), n, from);
        CHECK(gc_step(&c, to > from ? 1 : -1) == 1 && c.cur == to - 1, "tween %d: focus changes at once", ci);
        gc_update(&c, (float)el);
        char tag[96];
        snprintf(tag, sizeof tag, "tween t=%.2f", el);
        CHECK(NEAR(gc_scroll_value(&c), jnum(x, "scroll_offset", 1e9), 0.001), "%s: scroll %.3f vs %.3f", tag, c.scroll, jnum(x, "scroll_offset", 0));
        check_carousel_case(tag, &c, x, 1);
    }
    J *fade = jget(j, "edge_fade");
    for (int ci = 0; ci < fade->n; ci++) {
        J *x = fade->kid[ci], *items = jget(x, "items");
        GuiCarousel c;
        car_for(&c, jstrv(x, "package"), (int)jnum(x, "N", 0), (int)jnum(x, "current", 0));
        CHECK(gc_alpha(&c, 0) == 255, "fade disabled by default: alpha 255");
        gc_enable_fade(&c, 1);
        for (int i = 0; i < items->n; i++) {
            J *it = items->kid[i];
            CHECK(NEAR(gc_fade_factor(&c, i), jnum(it, "edge_fade_factor", -1), 0.001), "fade case %d slot %d factor %.4f vs %.4f", ci, i + 1, gc_fade_factor(&c, i), jnum(it, "edge_fade_factor", -1));
            CHECK(gc_alpha(&c, i) == (int)jnum(it, "alpha_enabled_item", -1), "fade case %d slot %d alpha %d", ci, i + 1, gc_alpha(&c, i));
            c.disabled[i] = 1;
            CHECK(gc_alpha(&c, i) == (int)jnum(it, "alpha_disabled_item", -1), "fade case %d slot %d disabled alpha %d", ci, i + 1, gc_alpha(&c, i));
            c.disabled[i] = 0;
            CHECK(gc_slot(&c, i).visible == jbool(it, "visible"), "fade case %d slot %d visible", ci, i + 1);
        }
        gc_enable_fade(&c, 0);
        CHECK(gc_alpha(&c, 1) == 255, "fade off restores alpha 255");
    }
    jfree(j);

    /* behaviour beyond the vectors: no wrap, disabled entries skipped, retarget mid tween */
    GuiCarousel c;
    int dis[4] = { 0, 0, 1, 0 };
    gc_init(&c, GC_RAIL_PAUSE);
    gc_set_entries(&c, 4, dis);
    CHECK(c.cur == 0, "focus starts on entry 1");
    CHECK(gc_step(&c, -1) == 0 && c.cur == 0, "no wrap on the left end");
    CHECK(gc_step(&c, 1) == 1 && c.cur == 1, "Right");
    CHECK(gc_step(&c, 1) == 1 && c.cur == 3, "Right skips the disabled entry");
    CHECK(gc_step(&c, 1) == 0 && c.cur == 3, "no wrap on the right end");
    CHECK(gc_step(&c, -1) == 1 && c.cur == 1, "Left skips the disabled entry");
    dis[0] = 1;
    gc_set_entries(&c, 4, dis);
    CHECK(c.cur == 1, "first enabled entry is focused when entry 1 is disabled");
    gc_set_entries(&c, 1, NULL);
    CHECK(!gc_tab_visible(&c) && !gc_left_arrow_visible(&c) && !gc_right_arrow_visible(&c), "a single entry shows no rail, tab or arrows");
    gc_set_entries(&c, 0, NULL);
    CHECK(gc_step(&c, 1) == 0, "empty carousel does not step");
    gc_set_entries(&c, 4, NULL);
    gc_step(&c, 1);
    gc_update(&c, 0.1f);
    float mid = c.scroll;
    CHECK(mid < 0 && mid > -GC_PITCH, "mid tween offset %.3f", mid);
    gc_step(&c, 1);                                       /* retarget while moving */
    CHECK(NEAR(c.scroll, mid, 0.0005), "retarget keeps the offset continuous");
    float v0 = c.vel;
    CHECK(v0 < 0, "retarget keeps the velocity (moving left)");
    gc_update(&c, 1.0f);
    CHECK(NEAR(c.scroll, -2 * GC_PITCH, 0.0001) && !gc_tweening(&c), "retarget settles on the new target");
    gc_update(&c, 1.0f);
    CHECK(NEAR(c.scroll, -2 * GC_PITCH, 0.0001), "stays at rest");
    gc_snap_to(&c, 0);
    gc_step(&c, 1);
    gc_snap_to(&c, 0);
    CHECK(c.scroll == 0 && !gc_tweening(&c), "snap cancels the tween");
}

/* ---- tvg09: rows and sliders ------------------------------------------------------------------ */
typedef struct PkgAnchors { const char *name; float tcx, tcy, tw, th, dcx, dcy, dw, dh; } PkgAnchors;
static const PkgAnchors k_anchors[] = {                       /* SPEC_GUI 2.3 table (centre + size) */
    { "UI_Options.fng",              -151, -120, 240, 36, 126, -122, 216, 32 },
    { "UI_PauseOptions.fng",         -150.2f, -120.9f, 240, 36, 124.7f, -122.9f, 231.18f, 32 },
    { "UI_PC_Customize_Options.fng", -152, -120, 240, 36, 126, -122, 200, 32 },
};
static void test_tvg09(void) {
    J *j = jload("tvg09_rows_sliders.json");
    if (!j) return;
    J *cases = jget(j, "cases");
    const float arrow = 24.0f, base_w = 234.92f, base_h = 32.0f, knob = 16.0f;
    for (int ci = 0; ci < cases->n; ci++) {
        J *x = cases->kid[ci];
        const char *pk = jstrv(x, "package"), *kind = jstrv(x, "kind");
        const PkgAnchors *p = NULL;
        for (size_t i = 0; i < sizeof k_anchors / sizeof k_anchors[0]; i++) if (!strcmp(k_anchors[i].name, pk)) p = &k_anchors[i];
        CHECK(p != NULL, "case %d: unknown package %s", ci, pk);
        if (!p) continue;
        GlAnchors a = gl_anchors_from_centres(p->tcx, p->tcy, p->tw, p->th, p->dcx, p->dcy, p->dw, p->dh);
        int k = (int)jnum(x, "visible_row", 0);
        char tag[96];
        snprintf(tag, sizeof tag, "%s row %d (%s)", pk, k, kind);
        /* SPEC_GUI 2.3 gives the in-game anchors rounded to 0.1, so that package is checked to 0.1 */
        const double tol = !strcmp(pk, "UI_PauseOptions.fng") ? 0.1 : 0.02;
        CHECK(NEAR(gl_row_pitch(&a), 36, 0), "%s pitch", tag);
        GlPt lab = gl_label_tl(&a, k), bg = gl_backing_pos(&a, k);
        J *lt = jget(x, "label_topleft"), *bp = jget(x, "backing_group_position");
        CHECK(NEAR(lab.x, lt->kid[0]->num, tol) && NEAR(lab.y, lt->kid[1]->num, tol), "%s label %.2f,%.2f", tag, lab.x, lab.y);
        CHECK(NEAR(bg.x, bp->kid[0]->num, tol) && NEAR(bg.y, bp->kid[1]->num, tol), "%s backing %.2f,%.2f", tag, bg.x, bg.y);
        GlPt l = gl_left_arrow_tl(&a, k, arrow), r = gl_right_arrow_tl(&a, k);
        J *lc = jget(x, "left_arrow_centre"), *rc = jget(x, "right_arrow_centre");
        CHECK(NEAR(l.x + arrow / 2, lc->kid[0]->num, tol + 0.08) && NEAR(l.y + arrow / 2, lc->kid[1]->num, tol), "%s left arrow %.2f,%.2f", tag, l.x + arrow / 2, l.y + arrow / 2);
        CHECK(NEAR(r.x + arrow / 2, rc->kid[0]->num, tol + 0.08) && NEAR(r.y + arrow / 2, rc->kid[1]->num, tol), "%s right arrow %.2f,%.2f", tag, r.x + arrow / 2, r.y + arrow / 2);
        if (!strcmp(kind, "choice")) {
            CHECK(NEAR(gl_data_cx(&a), jnum(x, "value_text_centre_x", 1e9), tol), "%s value x %.2f", tag, gl_data_cx(&a));
            CHECK(NEAR(gl_data_top(&a, k), jnum(x, "value_text_top", 1e9), tol), "%s value top %.2f", tag, gl_data_top(&a, k));
        } else {
            GlPt bt = gl_slider_base_tl(&a, k, base_w);
            J *bc = jget(x, "slider_base_centre");
            CHECK(NEAR(bt.x + base_w / 2, bc->kid[0]->num, tol) && NEAR(bt.y + base_h / 2, bc->kid[1]->num, tol), "%s base centre", tag);
            double f = jnum(x, "fraction", -1);
            GlSlider s = gl_slider(bt, base_w, base_h, knob, (float)f);
            J *kc = jget(x, "knob_centre"), *ft = jget(x, "fill_topleft");
            CHECK(NEAR(s.usable_w, jnum(x, "usable_width", 0), 0.001), "%s usable width %.3f", tag, s.usable_w);
            CHECK(NEAR(s.knob_c.x, kc->kid[0]->num, tol) && NEAR(s.knob_c.y, kc->kid[1]->num, tol), "%s knob %.3f,%.3f vs %.3f,%.3f", tag, s.knob_c.x, s.knob_c.y, kc->kid[0]->num, kc->kid[1]->num);
            CHECK(NEAR(s.fill_tl.x, ft->kid[0]->num, tol) && NEAR(s.fill_tl.y, ft->kid[1]->num, tol), "%s fill origin %.3f,%.3f", tag, s.fill_tl.x, s.fill_tl.y);
            CHECK(NEAR(s.fill_w, jnum(x, "fill_width", -1), 0.02), "%s fill width %.3f", tag, s.fill_w);
            CHECK(NEAR(jnum(x, "fill_height", 0), 5.75, 0), "%s authored fill height", tag);
            J *rg = jget(x, "range");
            if (rg && jget(x, "value")) {            /* the fraction follows (v-min)/(max-min) */
                double v = jnum(x, "value", 0), lo = rg->kid[0]->num, hi = rg->kid[1]->num;
                CHECK(NEAR((v - lo) / (hi - lo), f, 0.001), "%s fraction from value", tag);
            }
        }
    }
    jfree(j);
    /* Audio-page cross-checks quoted in SPEC_GUI_ART 4.2/4.3 */
    GlAnchors fe = gl_anchors_from_centres(-151, -120, 240, 36, 126, -122, 216, 32);
    GlSlider s = gl_slider(gl_slider_base_tl(&fe, 3, base_w), base_w, base_h, knob, 0.703f);
    CHECK(NEAR(s.knob_c.x, 155.0, 0.05) && NEAR(s.fill_tl.x, 54.54, 0.01), "Audio-page slider numbers");
    /* scrollbar: frontend rail (273,2) 8x282, in game (275,10); c is 1-based */
    CHECK(NEAR(gl_scroll_tab_y(2, 282, 1, 22), -139 + 8, 0.001), "tab at the top: %.3f", gl_scroll_tab_y(2, 282, 1, 22));
    CHECK(NEAR(gl_scroll_tab_y(2, 282, 22, 22), 143 - 8, 0.001), "tab at the bottom: 8 units inside the rail end");
    CHECK(NEAR(gl_scroll_tab_y(10, 282, 1, 4), -131 + 8, 0.001), "in-game tab at the top");
    CHECK(NEAR(gl_scroll_tab_y(2, 282, 12, 23), -139 + 8 + 266 * 11.0 / 22.0, 0.001), "linear in the focused row");
    CHECK(!gl_scroll_visible(0) && !gl_scroll_visible(1) && gl_scroll_visible(2), "scrollbar needs more than one row");
}

/* ---- tvg10: dialogs ----------------------------------------------------------------------------- */
typedef struct DlgCase { const char *name; GdKind kind; GdContext ctx; } DlgCase;
static const DlgCase k_dlg[] = {
    { "quit_career",           GD_QUIT,      { 1, 0, 0, 0 } },
    { "quit_other_single",     GD_QUIT,      { 2, 0, 0, 0 } },
    { "quit_racing_substate4", GD_QUIT,      { 1, 1, 0, 0 } },
    { "quit_online_a",         GD_QUIT,      { 8, 0, 0, 0 } },
    { "quit_online_a_flag",    GD_QUIT,      { 8, 0, 0, 1 } },
    { "quit_online_b",         GD_QUIT,      { 8, 0, 1, 0 } },
    { "quit_online_b_flag",    GD_QUIT,      { 8, 0, 1, 1 } },
    { "quit_lan",              GD_QUIT,      { 0x200, 0, 0, 0 } },
    { "quit_lan_session",      GD_QUIT,      { 0x200, 0, 1, 0 } },
    { "restart",               GD_RESTART,   { 1, 0, 0, 0 } },
    { "restart_substate4",     GD_RESTART,   { 1, 1, 0, 0 } },
    { "explore",               GD_EXPLORE,   { 1, 0, 0, 0 } },
    { "explore_substate4",     GD_EXPLORE,   { 1, 1, 0, 0 } },
    { "world_map",             GD_WORLD_MAP, { 1, 0, 0, 0 } },
    { "world_map_substate4",   GD_WORLD_MAP, { 1, 1, 0, 0 } },
    { "garage",                GD_GARAGE,    { 1, 0, 0, 0 } },
};
static void test_tvg10(void) {
    J *j = jload("tvg10_dialogs.json");
    if (!j) return;
    J *cases = jget(j, "cases");
    CHECK(cases->n == (int)(sizeof k_dlg / sizeof k_dlg[0]) + 1, "all %d dialog vectors are mapped (%d + default_settings)", cases->n, (int)(sizeof k_dlg / sizeof k_dlg[0]));
    for (int ci = 0; ci < cases->n; ci++) {
        J *x = cases->kid[ci];
        const char *name = jstrv(x, "name");
        const DlgCase *dc = NULL;
        static const DlgCase defaults_case = { "default_settings", GD_DEFAULTS, { 0, 0, 0, 0 } };
        for (size_t i = 0; i < sizeof k_dlg / sizeof k_dlg[0]; i++) if (!strcmp(k_dlg[i].name, name)) dc = &k_dlg[i];
        if (!strcmp(name, "default_settings")) dc = &defaults_case;
        CHECK(dc != NULL, "unmapped dialog case %s", name);
        if (!dc) continue;
        GdSpec s;
        gd_spec(&s, dc->kind, &dc->ctx);
        J *b1 = jget(x, "button1"), *b2 = jget(x, "button2");
        CHECK(!strcmp(s.package, jstrv(x, "package")), "%s: package %s", name, s.package);
        CHECK(s.package_key == hexkey(jstrv(x, "package_key")) && s.package_key == hash_name(s.package), "%s: package key %08X", name, s.package_key);
        CHECK(s.message_key == hexkey(jstrv(x, "message_key")), "%s: message key %08X vs %s", name, s.message_key, jstrv(x, "message_key"));
        CHECK(s.message && !strcmp(s.message, jstrv(x, "message_english")), "%s: message text", name);
        CHECK(s.label_key[0] == hexkey(jstrv(b1, "label_key")) && !strcmp(s.label[0], jstrv(b1, "english")), "%s: first label", name);
        CHECK(s.label_key[1] == hexkey(jstrv(b2, "label_key")) && !strcmp(s.label[1], jstrv(b2, "english")), "%s: second label", name);
        CHECK(s.result_msg[0] == hexkey(jstrv(b1, "message")), "%s: first message %08X", name, s.result_msg[0]);
        CHECK(s.result_msg[1] == hexkey(jstrv(b2, "message")), "%s: second message %08X", name, s.result_msg[1]);
        CHECK(s.back_msg == s.result_msg[1] && (!jstrv(x, "back_message") || s.back_msg == hexkey(jstrv(x, "back_message"))), "%s: Back posts the cancel message", name);
        CHECK(!strcmp(jstrv(x, "initial_focus"), "Button2") && s.initial_focus == 1, "%s: initial focus", name);
        CHECK(hash_name("button1") == 0xB8A7C6CCu && hash_name("Button2") == 0xB8A7C6CDu, "button object hashes");
        CHECK(!strcasecmp(jstrv(b1, "object"), "button1") && !strcmp(jstrv(b2, "object"), "Button2"), "%s: button object names", name);
    }
    J *nav = jget(j, "navigation");
    CHECK(nav && jstrv(nav, "Right on button1") && jstrv(nav, "Left on Button2") && jstrv(nav, "Left on button1") && jstrv(nav, "Right on Button2"), "navigation vector present");

    /* Default Settings dialog (tvg06 defaults_yes): GenericDialog_SMALL, Yes/No, focus on No */
    GdSpec d;
    GdContext none = { 0 };
    gd_spec(&d, GD_DEFAULTS, &none);
    CHECK(!strcmp(d.package, "GenericDialog_SMALL.fng") && d.package_key == 0x6962C0CDu, "defaults dialog package");
    CHECK(d.message_key == 0x2BBCCA6Bu && !strcmp(d.message, "Reset to default settings?"), "defaults message");
    CHECK(d.label_key[0] == 0x0000F770u && !strcmp(d.label[0], "Yes") && d.label_key[1] == 0x0000061Cu && !strcmp(d.label[1], "No"), "defaults buttons");
    CHECK(d.initial_focus == 1 && d.back_msg == 0x34DC1BCFu, "defaults: focus on No, Back = No (0x34DC1BCF)");
    GdSpec en;   /* M6 career engage: same package, Yes/No, retail message key */
    gd_spec(&en, GD_ENGAGE_RACE, NULL);
    CHECK(!strcmp(en.package, "GenericDialog_SMALL.fng") && en.message_key == 0xEC9488BBu &&
          !strcmp(en.message, "Would you like to race this event?") && en.initial_focus == 1, "engage dialog spec");
    CHECK(en.label_key[0] == 0x0000F770u && en.label_key[1] == 0x0000061Cu, "engage buttons Yes/No");
    { float a[4], b[4]; gd_button_extent(GD_ENGAGE_RACE, 0, a); gd_button_extent(GD_DEFAULTS, 0, b);
      CHECK(!memcmp(a, b, sizeof a), "engage buttons share the small dialog extents"); }
    GdSpec sh;   /* M6 shop engage: same package and buttons, ASK_ENTER_CAR_LOT */
    gd_spec(&sh, GD_ENGAGE_SHOP, NULL);
    CHECK(!strcmp(sh.package, "GenericDialog_SMALL.fng") && sh.message_key == 0x0AAB553Eu &&
          !strcmp(sh.message, "Do you want to enter the car lot?") && sh.initial_focus == 1, "shop engage dialog spec");
    CHECK(sh.label_key[0] == 0x0000F770u && sh.label_key[1] == 0x0000061Cu, "shop engage buttons Yes/No");
    { float a[4], b[4]; gd_button_extent(GD_ENGAGE_SHOP, 1, a); gd_button_extent(GD_DEFAULTS, 1, b);
      CHECK(!memcmp(a, b, sizeof a), "shop engage buttons share the small dialog extents"); }
    jfree(j);

    /* state machine */
    GuiDialog g;
    GdSpec q;
    GdContext career = { 1, 0, 0, 0 };
    gd_spec(&q, GD_QUIT, &career);
    gd_open(&g, &q);
    CHECK(g.open && g.focus == 1 && !g.closing, "opens on Cancel");
    gd_input(&g, GD_IN_RIGHT); CHECK(g.focus == 1, "Right on the second button: no wrap");
    gd_input(&g, GD_IN_UP); gd_input(&g, GD_IN_DOWN); gd_input(&g, GD_IN_START); CHECK(g.focus == 1 && !g.closing, "Up/Down/Start ignored");
    gd_input(&g, GD_IN_LEFT); CHECK(g.focus == 0, "Left focuses OK");
    gd_input(&g, GD_IN_LEFT); CHECK(g.focus == 0, "Left on the first button: no wrap");
    gd_input(&g, GD_IN_RIGHT); CHECK(g.focus == 1, "Right focuses Cancel");
    gd_input(&g, GD_IN_LEFT);
    gd_input(&g, GD_IN_ACCEPT);
    CHECK(g.closing && g.open, "Accept starts the exit");
    gd_input(&g, GD_IN_RIGHT); gd_input(&g, GD_IN_BACK); gd_input(&g, GD_IN_ACCEPT);
    CHECK(g.focus == 0, "input ignored while the exit runs");
    gd_update(&g, 0.139f);
    CHECK(gd_poll(&g) == GD_RES_NONE && g.open, "no result before the 140 ms exit completes");
    gd_update(&g, 0.002f);
    CHECK(gd_poll(&g) == GD_RES_FIRST && !g.open, "OK delivered after the exit");
    CHECK(gd_poll(&g) == GD_RES_NONE, "result delivered once");
    gd_update(&g, 1.0f);
    gd_input(&g, GD_IN_ACCEPT);
    CHECK(!g.closing, "closed dialog ignores input");

    gd_open(&g, &q);
    gd_input(&g, GD_IN_ACCEPT);                       /* focus starts on Cancel */
    gd_update(&g, 0.15f);
    CHECK(gd_poll(&g) == GD_RES_SECOND, "Accept on the default focus cancels");
    gd_open(&g, &q);
    gd_input(&g, GD_IN_LEFT);
    gd_input(&g, GD_IN_BACK);
    gd_update(&g, 0.15f);
    CHECK(gd_poll(&g) == GD_RES_SECOND, "Back cancels whatever the focus");
    gd_open(&g, &q);
    gd_update(&g, 5.0f);
    CHECK(g.open && gd_poll(&g) == GD_RES_NONE, "an idle dialog stays open");

    /* pointer: extents are +-(128+21) around the button centre, y 53..95 (centred coordinates) */
    float e0[4], e1[4];
    gd_button_extent(GD_QUIT, 0, e0); gd_button_extent(GD_QUIT, 1, e1);
    CHECK(NEAR(e0[0], -300, 0.001) && NEAR(e0[2], -2, 0.001) && NEAR(e0[1], 53, 0.001) && NEAR(e0[3], 95, 0.001), "in-game first button extent %.1f..%.1f", e0[0], e0[2]);
    CHECK(NEAR(e1[0], 2, 0.001) && NEAR(e1[2], 300, 0.001), "in-game second button extent");
    gd_button_extent(GD_DEFAULTS, 0, e0);
    CHECK(NEAR(e0[0], -299, 0.001) && NEAR(e0[2], -1, 0.001), "defaults first button extent (centre -150)");
    gd_open(&g, &q);
    CHECK(!gd_pointer_click(&g, 0, 74) && !g.closing, "click between the buttons does nothing");
    CHECK(gd_pointer_click(&g, -151, 74) && g.closing, "click on OK chooses it");
    gd_update(&g, 0.2f);
    CHECK(gd_poll(&g) == GD_RES_FIRST, "pointer OK result");
    gd_open(&g, &q);
    CHECK(gd_pointer_click(&g, 151, 95) && g.closing, "extent edges are inclusive");
    gd_update(&g, 0.2f);
    CHECK(gd_poll(&g) == GD_RES_SECOND, "pointer Cancel result");
}

/* ---- tvg11: transitions ---------------------------------------------------------------------------- */
static int target_of(const char *s) { return !strcmp(s, "targets") ? GT_TO_TARGETS : !strcmp(s, "all") ? GT_TO_ALL : GT_TO_APP; }
static void check_script(const char *pk, const char *what, const GtScript *s, const J *js) {
    CHECK(!strcasecmp(s->name, jstrv(js, "script")), "%s %s: script %s vs %s", pk, what, s->name, jstrv(js, "script"));
    CHECK(s->length_ms == (int)jnum(js, "length_ms", -1), "%s %s: length %d", pk, what, s->length_ms);
    J *ev = jget(js, "events");
    CHECK(s->nevents == ev->n, "%s %s: %d events vs %d", pk, what, s->nevents, ev->n);
    for (int i = 0; i < ev->n && i < s->nevents; i++) {
        J *e = ev->kid[i];
        CHECK(s->events[i].msg == hexkey(e->kid[0]->s), "%s %s event %d msg %08X", pk, what, i, s->events[i].msg);
        CHECK((int)s->events[i].target == target_of(e->kid[1]->s), "%s %s event %d target", pk, what, i);
        CHECK(s->events[i].t_ms == (int)e->kid[2]->num, "%s %s event %d time %d", pk, what, i, s->events[i].t_ms);
    }
}
static void test_tvg11(void) {
    J *j = jload("tvg11_transitions.json");
    if (!j) return;
    J *pkgs = jget(j, "packages"), *done = jget(j, "exit_complete_ms");
    CHECK(gt_count() == pkgs->n, "%d packages in the table, %d in the vector", gt_count(), pkgs->n);
    for (int i = 0; i < pkgs->n; i++) {
        J *x = pkgs->kid[i];
        const char *pk = jstrv(x, "package");
        const GtPackage *g = gt_find(pk);
        CHECK(g != NULL, "%s missing from the table", pk);
        if (!g) continue;
        check_script(pk, "enter", &g->enter, jget(x, "enter"));
        check_script(pk, "exit", &g->exit, jget(x, "exit"));
        CHECK(g->exit_complete_ms == (int)jnum(done, pk, -1), "%s exit complete %d", pk, g->exit_complete_ms);
        CHECK(gt_exit_complete_ms(pk) == g->exit_complete_ms, "%s accessor", pk);
        int found = -1;
        for (int k = 0; k < g->exit.nevents; k++) if (g->exit.events[k].msg == GT_MSG_EXIT_COMPLETE) found = g->exit.events[k].t_ms;
        CHECK(found == g->exit_complete_ms, "%s: exit complete is the 0xE1FDE1D1 event time", pk);
        CHECK(gt_exit_script_hash(g) == hash_name(jstrv(jget(x, "exit"), "script")), "%s exit script hash", pk);
    }
    /* the issue statement of SPEC_GUI_ART 1.4: 140 ms front end, 320 ms in game */
    CHECK(gt_exit_complete_ms("UI_Options.fng") == 140 && gt_exit_complete_ms("UI_PC_Customize_Options.fng") == 140, "front end 140 ms");
    CHECK(gt_exit_complete_ms("UI_PauseOptions.fng") == 320 && gt_exit_complete_ms("UI_pause.fng") == 320 && gt_exit_complete_ms("UI_PauseOptionsMain.fng") == 320, "in game 320 ms");
    CHECK(gt_exit_complete_ms("UI_InGameDialog.fng") == 140 && gt_exit_complete_ms("GenericDialog_SMALL.fng") == 140, "dialogs 140 ms");
    CHECK(gt_find("ui_pause.FNG") == gt_find("UI_pause.fng") && gt_find("nonsense.fng") == NULL && gt_exit_complete_ms("nonsense.fng") == 0, "lookup rules");
    CHECK(hash_name("DIM") == GT_SCRIPT_DIM && hash_name("UNDIM") == GT_SCRIPT_UNDIM, "dim script hashes");
    jfree(j);
}


/* ---- pause screen state machine (SPEC_GUI_ART 2.4, 3) -------------------------------------------- */
static PauseScreen *g_ps;
static PsEvent drain(PauseScreen *p) { return ps_poll(p); }
static void ps_ready(PauseScreen *p, const PauseContext *c) { ps_open(p, c); ps_update(p, 0); }
static void test_pause_screen(void) {
    PauseScreen ps;
    memset(&ps, 0, sizeof ps);
    g_ps = &ps;
    PauseContext c;
    pe_context_default(&c);
    ps_open(&ps, &c);
    CHECK(ps_count(&ps) == 4 && ps_selected(&ps) == 0 && !strcmp(ps_caption(&ps), "Return to Explore Mode"), "career explore pause");
    /* creation-frame guard: activation, Back and Start are ignored until the first update */
    ps_input(&ps, PS_IN_ACCEPT); ps_input(&ps, PS_IN_BACK); ps_input(&ps, PS_IN_START);
    CHECK(!ps_busy(&ps) && ps.exit_serial == 0, "creation frame ignores Accept/Back/Start");
    ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_selected(&ps) == 1, "navigation is not guarded");
    ps_update(&ps, 0);
    ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_selected(&ps) == 3 && !strcmp(ps_caption(&ps), "Quit to Main Menu"), "Right skips the disabled Replay entry");
    ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_selected(&ps) == 3, "no wrap on the right end");
    ps_input(&ps, PS_IN_LEFT);
    CHECK(ps_selected(&ps) == 1 && ps.move_dir == -1, "Left skips it back");
    ps_input(&ps, PS_IN_LEFT); ps_input(&ps, PS_IN_LEFT);
    CHECK(ps_selected(&ps) == 0, "no wrap on the left end");
    CHECK(!gc_left_arrow_visible(&ps.car) && gc_right_arrow_visible(&ps.car), "arrows on the first entry");

    /* Options: exit 320 ms, then the event, once */
    ps_input(&ps, PS_IN_RIGHT);
    unsigned xs = ps.exit_serial;
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps_busy(&ps) && ps.exit_serial == xs + 1 && drain(&ps) == PS_EV_NONE, "Options starts the exit script, nothing yet");
    ps_input(&ps, PS_IN_BACK); ps_input(&ps, PS_IN_ACCEPT); ps_input(&ps, PS_IN_START); ps_input(&ps, PS_IN_LEFT);
    CHECK(ps.exit_serial == xs + 1 && ps_selected(&ps) == 1, "second Back/Start/Accept and moves ignored while the exit runs");
    ps_update(&ps, 0.319f);
    CHECK(drain(&ps) == PS_EV_NONE, "no event 1 ms early");
    ps_update(&ps, 0.002f);
    CHECK(drain(&ps) == PS_EV_OPTIONS && drain(&ps) == PS_EV_NONE && !ps_busy(&ps), "Options event after 320 ms, once");

    /* Return to Explore Mode: after exit */
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps_busy(&ps), "explore starts the exit");
    ps_update(&ps, 0.32f);
    CHECK(drain(&ps) == PS_EV_RESUME, "resume after the exit");
    /* Back and Start: exit, then resume */
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_BACK);
    CHECK(ps_busy(&ps) && drain(&ps) == PS_EV_NONE, "Back starts the exit");
    ps_update(&ps, 0.2f); CHECK(drain(&ps) == PS_EV_NONE, "still exiting at 200 ms");
    ps_update(&ps, 0.13f); CHECK(drain(&ps) == PS_EV_RESUME, "Back resumes at exit-complete");
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_START); ps_update(&ps, 0.4f);
    CHECK(drain(&ps) == PS_EV_RESUME, "Start resumes at exit-complete");

    /* Quit: dialog over the dimmed screen; focus on Cancel; Cancel changes nothing */
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_RIGHT);
    unsigned ds = ps.dialog_serial;
    CHECK(!ps.car.fade, "edge fade is off at the start");
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps_dialog_open(&ps) && ps.dialog.focus == 1 && !ps_busy(&ps) && ps.dialog_serial == ds + 1, "Quit opens the dialog without an exit");
    CHECK(!strcmp(ps.dialog.spec.package, "UI_InGameDialog.fng") && ps.dialog.spec.message_key == 0x6F4796A6u &&
          ps.dialog.spec.label_key[0] == 0x639 && ps.dialog.spec.label_key[1] == 0x541389E5u, "career quit dialog spec");
    ps_input(&ps, PS_IN_START); ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_dialog_open(&ps) && ps.dialog.focus == 1 && !ps.dialog.closing, "Start is ignored in the dialog");
    ps_input(&ps, PS_IN_BACK);
    CHECK(ps_dialog_open(&ps) && ps.dialog.closing && drain(&ps) == PS_EV_NONE, "Back cancels through the dialog exit");
    ps_update(&ps, 0.14f);
    CHECK(!ps_dialog_open(&ps) && drain(&ps) == PS_EV_NONE && ps.dialog_serial == ds + 2 && ps.car.fade, "Cancel: undim, edge fade on, no event");
    ps_input(&ps, PS_IN_LEFT);
    CHECK(ps_selected(&ps) == 1, "input works again after Cancel");
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_ACCEPT);
    CHECK(!ps.car.fade, "a new dialog turns the fade off again");
    ps_input(&ps, PS_IN_LEFT); ps_input(&ps, PS_IN_ACCEPT);      /* OK */
    CHECK(drain(&ps) == PS_EV_NONE, "OK delivers after the dialog exit");
    ps_update(&ps, 0.141f);
    CHECK(drain(&ps) == PS_EV_QUIT && drain(&ps) == PS_EV_NONE && !ps_dialog_open(&ps), "OK quits");

    /* disabled Replay cannot be activated even if it is the only thing focusable; with data it works */
    PauseContext rc = c;
    rc.replay_available = 1;
    ps_ready(&ps, &rc);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_selected(&ps) == 2 && !ps_entry(&ps, 2)->disabled, "Replay focusable with replay data");
    ps_input(&ps, PS_IN_ACCEPT); ps_update(&ps, 0.4f);
    CHECK(drain(&ps) == PS_EV_REPLAY, "Replay opens its screen after the exit");
    ps_ready(&ps, &c);
    ps.car.disabled[0] = 0;
    CHECK(ps_entry(&ps, 2)->disabled, "replay entry is disabled");
    ps.car.cur = 2;                                          /* forced focus on a disabled entry */
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(!ps_busy(&ps), "a disabled entry cannot be activated");

    /* race contexts */
    PauseContext race = c;
    race.race = 1;
    ps_ready(&ps, &race);
    CHECK(ps_count(&ps) == 6 && !strcmp(ps_caption(&ps), "Resume Race"), "career race pause starts on Resume Race");
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(!ps_busy(&ps) && drain(&ps) == PS_EV_RESUME, "Resume Race resumes at once (immediate)");
    ps_ready(&ps, &race);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps_dialog_open(&ps) && ps.dialog.spec.message_key == 0x0972A44Bu, "Restart opens its confirmation");
    ps_input(&ps, PS_IN_LEFT); ps_input(&ps, PS_IN_ACCEPT); ps_update(&ps, 0.2f);
    CHECK(drain(&ps) == PS_EV_RESTART, "Restart confirmed");
    ps_ready(&ps, &race);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps_dialog_open(&ps) && ps.dialog.spec.message_key == 0x60B42AFBu, "Return to Explore Mode (race) confirmation");
    ps_input(&ps, PS_IN_LEFT); ps_input(&ps, PS_IN_ACCEPT); ps_update(&ps, 0.2f);
    CHECK(drain(&ps) == PS_EV_EXPLORE, "Explore confirmed");
    PauseContext map = race;
    map.from_map = 1; map.substate4 = 1;
    ps_ready(&ps, &map);
    CHECK(!strcmp(ps_caption(&ps), "Return to World Map"), "world map first when launched from the map");
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps.dialog.spec.message_key == 0xE761C8B8u, "sub-state 4 texts");
    ps_input(&ps, PS_IN_LEFT); ps_input(&ps, PS_IN_ACCEPT); ps_update(&ps, 0.2f);
    CHECK(drain(&ps) == PS_EV_WORLD_MAP, "World map confirmed");
    PauseContext online = c;
    online.mode = 8; online.race = 1; online.session = 3; online.online_flag = 1;
    ps_ready(&ps, &online);
    CHECK(ps_count(&ps) == 1 && !gc_tab_visible(&ps.car), "connected online race: quit only, no rail");
    ps_input(&ps, PS_IN_ACCEPT);
    CHECK(ps.dialog.spec.message_key == 0x79A85513u, "online session + flag text");
    /* a lone entry: Left/Right do nothing */
    ps_input(&ps, PS_IN_BACK); ps_update(&ps, 0.2f);
    ps_ready(&ps, &online);
    ps_input(&ps, PS_IN_RIGHT);
    CHECK(ps_selected(&ps) == 0, "single entry");

    /* pointer: arrows step on press, the selection slot activates on release only when it began there */
    ps_ready(&ps, &c);
    ps_pointer_click(&ps, GC_ARROW_L_X, GC_ARROW_Y);
    CHECK(ps_selected(&ps) == 0, "hidden left arrow is inert");
    ps_pointer_click(&ps, GC_ARROW_R_X, GC_ARROW_Y);
    CHECK(ps_selected(&ps) == 1 && ps.move_dir == 1, "right arrow press steps");
    ps_pointer_click(&ps, 158.0f, GC_ARROW_Y);
    CHECK(ps_selected(&ps) == 1 && !ps_busy(&ps), "neighbour slot is inert");
    ps_pointer_press(&ps, 200, 0); ps_pointer_release(&ps, 0.388f, -14.296f);
    CHECK(!ps_busy(&ps), "release on the slot without a press there is inert");
    ps_pointer_press(&ps, 0.388f, -14.296f); ps_pointer_release(&ps, 200, 0);
    CHECK(!ps_busy(&ps), "release outside the slot is inert");
    ps_pointer_press(&ps, 0.388f, -14.296f); ps_pointer_release(&ps, 0.388f + 59.0f, -14.296f + 29.5f);
    CHECK(ps_busy(&ps), "press and release on the slot (edge inclusive) activates Options");
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_RIGHT);
    ps_pointer_click(&ps, 0.388f, -14.296f);
    CHECK(ps_dialog_open(&ps), "clicking the Quit slot opens the dialog");
    ps_pointer_click(&ps, GC_ARROW_R_X, GC_ARROW_Y);
    CHECK(ps_dialog_open(&ps) && !ps.dialog.closing && ps_selected(&ps) == 3, "arrows are inert behind the dialog");
    ps_pointer_release(&ps, -151, 74);
    CHECK(!ps.dialog.closing, "release without a press cannot confirm Quit");
    ps_pointer_press(&ps, 0, 0); ps_pointer_release(&ps, -151, 74);
    CHECK(!ps.dialog.closing, "press outside then release on OK cannot confirm Quit");
    ps_pointer_press(&ps, -151, 74); ps_pointer_release(&ps, 151, 74);
    CHECK(!ps.dialog.closing, "press OK then release Cancel cannot choose either button");
    ps_pointer_press(&ps, -151, 74); ps_pointer_release(&ps, 0, 0);
    CHECK(!ps.dialog.closing, "release outside the pressed button is inert");
    ps_pointer_click(&ps, -151, 74);
    CHECK(ps.dialog.closing, "a click on OK chooses it");
    ps_update(&ps, 0.2f);
    CHECK(drain(&ps) == PS_EV_QUIT, "pointer OK quits");
    ps_ready(&ps, &c);
    ps_input(&ps, PS_IN_RIGHT); ps_input(&ps, PS_IN_RIGHT);
    ps_input(&ps, PS_IN_ACCEPT);
    ps_pointer_press(&ps, -151, 74);
    ps_input(&ps, PS_IN_BACK);
    ps_update(&ps, 0.2f);
    ps_pointer_release(&ps, 0.388f, -14.296f);
    CHECK(!ps_dialog_open(&ps) && !ps_busy(&ps), "a dialog press cannot activate the carousel after keyboard cancellation");
    g_ps = NULL;
}

static void test_dialog_frames(void) {
    for (int i = 0; i < GD_FRAMES; i++) {
        const GdFrame *f = gd_frame_at(i);
        CHECK(f && gd_frame_by_package(f->package) == f, "frame %d lookup", i);
        CHECK(f && hash_name(f->package) == f->package_key, "frame %d package hash", i);
        CHECK(f && f->box_w > 0 && f->lines > 0, "frame %d fit geometry", i);
    }
    CHECK(!gd_frame_at(-1) && !gd_frame_at(GD_FRAMES), "frame bounds");
    CHECK(gd_frame_by_package("genericdialog_med.fng") == gd_frame_get(GD_FAM_GENERIC, GD_SIZE_MED), "case insensitive package lookup");
    CHECK(gd_message_lines("a^^b", 380) == 3, "empty explicit line counts");
    CHECK(gd_message_lines("a\nb\nc\nd\ne", 380) == 5, "explicit line breaks");
    for (int family = 0; family < GD_FAMS; family++) {
        for (int size = 0; size < GD_SIZES; size++) {
            const GdFrame *f = gd_frame_get((GdFamily)family, (GdSize)size);
            CHECK(f && gd_frame_fit((GdFamily)family, f->lines) == f, "family %d size %d fit", family, size);
        }
        CHECK(gd_frame_fit((GdFamily)family, 100) == gd_frame_get((GdFamily)family, GD_SIZE_LARGE), "largest fallback %d", family);
    }
    GdSpec spec;
    gd_spec(&spec, GD_DEFAULTS, NULL);
    CHECK(spec.frame == gd_frame_get(GD_FAM_GENERIC, GD_SIZE_SMALL), "default binds small");
    spec.message = "one^two^three^four^five";
    CHECK(gd_spec_fit(&spec, GD_FAM_GENERIC) == gd_frame_get(GD_FAM_GENERIC, GD_SIZE_MED) && spec.package_key == spec.frame->package_key, "long text promotes frame");
    spec.message = "short";
    CHECK(gd_spec_fit(&spec, GD_FAM_GENERIC)->size == GD_SIZE_MED, "explicit frame retained when it fits");
}

int main(int argc, char **argv) {
    test_dialog_frames();
    if (argc > 1) g_dir = argv[1];
    test_tvg07();
    test_tvg08();
    test_tvg09();
    test_tvg10();
    test_tvg11();
    test_pause_screen();
    printf("gui_art_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
