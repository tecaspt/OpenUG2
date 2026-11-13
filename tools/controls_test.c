/* controls_test -- runs the Controls logic (controls_bind.c, controls_menu.c, controls_store.c) against
 * docs/cleanroom-settings/test-vectors/controls.
 *
 *   controls_test [VECTOR_DIR]
 *
 * tvc01..tvc09 are read at run time (test_json.h). Where a vector records a retail quirk that decision D5
 * of CONTROLS_DECISIONS.md fixes (the doubled display after a stolen primary key, the wheel Career
 * Statistics secondary cell), the test asserts the fixed behaviour and, for the first, the retail value the
 * vector states as the thing that is deliberately different. Beyond the vectors it drives the screen state
 * machine (capture, lock, reset, device change), the persistence and the display names. No retail data and
 * no window are needed. Exit code is nonzero if any check fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_keyboard.h>
#include <SDL_scancode.h>

#include "controls_bind.h"
#include "controls_menu.h"
#include "controls_store.h"
#include "options_menu.h"
#include "ugui_core.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    if (g_fail <= 80) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static const char *g_dir = "docs/cleanroom-settings/test-vectors/controls";
#include "test_json.h"

/* ---- name tables ----------------------------------------------------------------------------------- */
#define S(x) { #x, x }
static const struct { const char *name; int sc; } k_sc[] = {
    S(SDL_SCANCODE_LEFT), S(SDL_SCANCODE_RIGHT), S(SDL_SCANCODE_UP), S(SDL_SCANCODE_DOWN), S(SDL_SCANCODE_A),
    S(SDL_SCANCODE_D), S(SDL_SCANCODE_W), S(SDL_SCANCODE_S), S(SDL_SCANCODE_LSHIFT), S(SDL_SCANCODE_SEMICOLON),
    S(SDL_SCANCODE_LCTRL), S(SDL_SCANCODE_PERIOD), S(SDL_SCANCODE_SPACE), S(SDL_SCANCODE_END), S(SDL_SCANCODE_LALT),
    S(SDL_SCANCODE_RALT), S(SDL_SCANCODE_C), S(SDL_SCANCODE_INSERT), S(SDL_SCANCODE_B), S(SDL_SCANCODE_K),
    S(SDL_SCANCODE_RETURN), S(SDL_SCANCODE_F1), S(SDL_SCANCODE_M), S(SDL_SCANCODE_F2), S(SDL_SCANCODE_TAB),
    S(SDL_SCANCODE_F3), S(SDL_SCANCODE_T), S(SDL_SCANCODE_F4), S(SDL_SCANCODE_R), S(SDL_SCANCODE_HOME),
    S(SDL_SCANCODE_H), S(SDL_SCANCODE_Y), S(SDL_SCANCODE_J), S(SDL_SCANCODE_U), S(SDL_SCANCODE_ESCAPE),
    S(SDL_SCANCODE_P), S(SDL_SCANCODE_Q), S(SDL_SCANCODE_EQUALS), S(SDL_SCANCODE_5)
};
static int sc_by_name(const char *n) {
    for (size_t i = 0; i < sizeof k_sc / sizeof k_sc[0]; i++) if (!strcmp(k_sc[i].name, n)) return k_sc[i].sc;
    return -1;
}

/* ---- tvc01: label keys ---------------------------------------------------------------------------------- */
static int group_has(const J *g, uint32_t key) {
    for (int i = 0; i < g->n; i++) if (hexkey(jstrv(g->kid[i], "key")) == key) return 1;
    return 0;
}
static void test_labels(void) {
    J *v = jload("tvc01_labels.json");
    if (!v) return;
    const J *gr = jget(v, "groups");
    const J *screen = jget(gr, "screen"), *rows = jget(gr, "rows"), *dlg = jget(gr, "reset_dialog"), *hub = jget(gr, "options_hub");
    CHECK(group_has(screen, CM_KEY_HEADER) && group_has(hub, CM_KEY_HEADER), "header key");
    CHECK(group_has(screen, CM_KEY_PRIMARY) && group_has(screen, CM_KEY_SECONDARY), "column keys");
    CHECK(group_has(screen, CM_KEY_INSTR_CONFIGURE) && group_has(screen, CM_KEY_INSTR_CAPTURE) &&
          group_has(screen, CM_KEY_INSTR_ASSIGN), "instruction keys");
    CHECK(group_has(screen, cb_title_key(CB_CLASS_KEYBOARD)) && group_has(screen, cb_title_key(CB_CLASS_PAD)) &&
          group_has(screen, cb_title_key(CB_CLASS_WHEEL)), "title keys");
    CHECK(group_has(dlg, CM_KEY_RESET_MESSAGE) && group_has(dlg, CM_KEY_OK) && group_has(dlg, CM_KEY_CANCEL), "dialog keys");
    CbRow r[CB_MAX_ROWS];
    int n = cb_rows(CB_CLASS_KEYBOARD, r);
    CHECK(n == rows->n, "row label count %d vs %d", n, rows->n);
    for (int i = 0; i < n && i < rows->n; i++)
        CHECK(hexkey(jstrv(rows->kid[i], "key")) == r[i].label_key, "row %d label key", i + 1);
    jfree(v);
}

/* ---- tvc02: rows ------------------------------------------------------------------------------------- */
static void test_rows(void) {
    J *v = jload("tvc02_rows.json");
    if (!v) return;
    static const char *const names[3] = { "keyboard", "joypad", "wheel" };
    static const CbClass cls[3] = { CB_CLASS_KEYBOARD, CB_CLASS_PAD, CB_CLASS_WHEEL };
    for (int c = 0; c < 3; c++) {
        const J *list = jget(v, names[c]);
        CbRow r[CB_MAX_ROWS];
        int n = cb_rows(cls[c], r);
        CHECK(n == list->n, "%s: %d rows, vector %d", names[c], n, list->n);
        for (int i = 0; i < n && i < list->n; i++) {
            const J *e = list->kid[i];
            CHECK(r[i].slot == (int)jnum(e, "slot", -1), "%s row %d slot", names[c], i);
            CHECK(r[i].label_key == hexkey(jstrv(e, "label_key")), "%s row %d label key", names[c], i);
            CHECK(r[i].action == (int)jnum(e, "action", -1), "%s row %d action", names[c], i);
            for (int col = 0; col < 2; col++) {
                const J *cell = jget(e, col ? "secondary" : "primary");
                CHECK((int)jnum(cell, "capture_action", -1) == r[i].action, "%s row %d capture action col %d", names[c], i, col);
                int disp = (int)jnum(cell, "display_action", -1);
                if (disp != r[i].action)    /* the retail quirk Q2 (wheel Career Statistics secondary): fixed by D5 */
                    CHECK(cls[c] == CB_CLASS_WHEEL && r[i].action == CB_ACT_CAREER_STATS && col == 1 && disp == CB_ACT_READ_SMS,
                          "unexpected display deviation in %s row %d", names[c], i);
            }
        }
    }
    jfree(v);
}

/* ---- tvc03: keyboard defaults ---------------------------------------------------------------------------- */
static void test_kb_defaults(void) {
    J *v = jload("tvc03_keyboard_defaults.json");
    if (!v) return;
    CbKeyTable t;
    memset(&t, 0, sizeof t);
    cb_kb_reset(&t);
    const J *rows = jget(v, "menu_rows");
    for (int i = 0; i < rows->n; i++) {
        const J *e = rows->kid[i];
        int action = (int)jnum(e, "action", -1);
        for (int col = 0; col < 2; col++) {
            const J *k = jget(e, col ? "secondary" : "primary");
            int sc = sc_by_name(jstrv(k, "sdl_scancode"));
            int dik = (int)hexkey(jstrv(k, "dik"));
            CHECK(sc >= 0, "scancode name %s unknown to the test", jstrv(k, "sdl_scancode"));
            CHECK(cb_dik_to_scancode(dik) == sc, "action %d col %d: dik 0x%X -> %d, want %d", action, col, dik, cb_dik_to_scancode(dik), sc);
            CHECK(cb_scancode_to_dik(sc) == dik, "action %d col %d: scancode -> dik", action, col);
            CHECK(cb_kb_display(&t, action, col) == sc, "action %d col %d default key", action, col);
            /* names: SDL platform names (D2); documented differences only */
            char name[CB_NAME_MAX];
            cb_format_name(SDL_GetScancodeName((SDL_Scancode)sc), 0, name);
            const char *want = jstrv(k, "windows_english_name");
            int known = (!strcmp(want, "Esc") && !strcmp(name, "Escape")) || (!strcmp(want, "Enter") && !strcmp(name, "Return"));
            CHECK(!strcmp(name, want) || known, "action %d col %d: name \"%s\", Windows name \"%s\"", action, col, name, want);
        }
    }
    const J *order = jget(v, "full_table_order");
    CHECK(order->n == CB_KB_ENTRIES, "table size %d", order->n);
    for (int i = 0; i < order->n && i < CB_KB_ENTRIES; i++) {
        const J *e = order->kid[i];
        CHECK(t.e[i].action == (int)jnum(e, "action", -1), "entry %d action", i);
        CHECK(t.e[i].key == cb_dik_to_scancode((int)hexkey(jstrv(e, "dik"))), "entry %d key", i);
    }
    /* reset restores a table that was edited: the walk of 6.1 over a live table */
    cb_kb_capture(&t, CB_ACT_NITROUS, 0, SDL_SCANCODE_W);
    cb_kb_capture(&t, CB_ACT_CAMERA, 1, SDL_SCANCODE_UP);
    cb_kb_reset(&t);
    CbKeyTable d;
    memset(&d, 0, sizeof d);
    cb_kb_reset(&d);
    CHECK(!memcmp(&t, &d, sizeof t), "reset after edits is the default table");
    jfree(v);
}

/* ---- tvc04: keyboard capture ------------------------------------------------------------------------------ */
static void test_kb_capture(void) {
    J *v = jload("tvc04_keyboard_capture.json");
    if (!v) return;
    CbKeyTable t, base;
    memset(&base, 0, sizeof base);
    cb_kb_reset(&base);
    t = base;
    const J *cases = jget(v, "cases");
    for (int i = 0; i < cases->n; i++) {
        const J *c = cases->kid[i];
        const char *name = jstrv(c, "name");
        if (strcmp(jstrv(c, "start"), "previous")) t = base;
        const J *cap = jget(c, "capture");
        int dik = (int)hexkey(jstrv(cap, "key_dik")), sc = cb_dik_to_scancode(dik);
        CbCapResult r = cb_kb_capture(&t, (int)jnum(cap, "action", 0), (int)jnum(cap, "column", 0), sc);
        int want = !strcmp(jstrv(c, "result"), "assigned");
        CHECK((r == CB_CAP_ASSIGNED) == want, "case \"%s\": result", name);
        const J *disp = jget(c, "display_after");
        for (int k = 0; k < disp->n; k++) {
            int action = atoi(disp->key[k]);
            const J *pair = disp->kid[k];
            for (int col = 0; col < 2; col++) {
                int want_sc = cb_dik_to_scancode((int)hexkey(pair->kid[col]->s));
                int got = cb_kb_display(&t, action, col);
                if (!strncmp(name, "step 1", 6) && action == CB_ACT_THROTTLE && col == 1) {
                    /* retail shows W twice (Q3); D5 fixes the double display: the empty primary shows no copy */
                    CHECK(want_sc == SDL_SCANCODE_W && got == 0, "case \"%s\": D5 display of throttle secondary (%d)", name, got);
                } else
                    CHECK(got == want_sc, "case \"%s\": action %d col %d shows %d, want %d", name, action, col, got, want_sc);
            }
        }
    }
    jfree(v);
}

/* ---- joystick helpers -------------------------------------------------------------------------------------- */
static unsigned axes_mask_of(const J *names) {
    static const char *const n[8] = { "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider0", "Slider1" };
    unsigned m = 0;
    for (int i = 0; i < names->n; i++)
        for (int s = 0; s < 8; s++) if (!strcmp(names->kid[i]->s, n[s])) m |= 1u << s;
    return m;
}
static void check_axes(const StInputDevice *d, const J *ax, const char *what) {
    static const char *const f[4] = { "steer_axis", "throttle_axis", "brake_axis", "combined_axis" };
    static const char *const g[4] = { "steer_dir", "throttle_dir", "brake_dir", "combined_dir" };
    for (int i = 0; i < 4; i++) {
        CHECK(d->axis[i] == (int)jnum(ax, f[i], -99), "%s: %s %d vs %d", what, f[i], d->axis[i], (int)jnum(ax, f[i], -99));
        CHECK(d->dir[i] == (int)jnum(ax, g[i], -99), "%s: %s %d vs %d", what, g[i], d->dir[i], (int)jnum(ax, g[i], -99));
        CHECK(d->assigned[i] == (int)jget(ax, "assigned")->kid[i]->num, "%s: assigned[%d]", what, i);
    }
    CHECK(d->combined_mode == (int)jnum(ax, "combined_mode", -1), "%s: combined mode", what);
}
static void check_binding(const StInputDevice *d, const J *arr, int n, const char *what) {
    for (int i = 0; i < n && i < arr->n; i++)
        CHECK(d->binding[i] == (int)arr->kid[i]->num, "%s: binding[%d] = %d, want %d", what, i, d->binding[i], (int)arr->kid[i]->num);
}
static void load_state(StInputDevice *d, const J *binding, const J *axes) {
    memset(d, 0, sizeof *d);
    for (int i = 0; i < 32; i++) d->binding[i] = (int)binding->kid[i]->num;
    static const char *const f[4] = { "steer_axis", "throttle_axis", "brake_axis", "combined_axis" };
    static const char *const g[4] = { "steer_dir", "throttle_dir", "brake_dir", "combined_dir" };
    for (int i = 0; i < 4; i++) {
        d->axis[i] = (int)jnum(axes, f[i], 0);
        d->dir[i] = (int)jnum(axes, g[i], 0);
        d->assigned[i] = (uint8_t)jget(axes, "assigned")->kid[i]->num;
    }
    d->combined_mode = (uint8_t)jnum(axes, "combined_mode", 0);
}

/* ---- standard (game controller) layout ----------------------------------------------------------------------- */
static CbPadInfo std_info(void) {                      /* an Xbox-style pad as SDL would map it */
    CbPadInfo i;
    memset(&i, 0, sizeof i);
    i.buttons = 11;
    i.axes_mask = 0x3F;
    i.std = 1;
    static const int8_t ctl[CB_STD_COUNT] = { 0, 1, 2, 3, 6, 7, 8, 9, 4, 5, 14, 15, 16 + CB_DIR_UP, 16 + CB_DIR_DOWN, 16 + CB_DIR_LEFT, 16 + CB_DIR_RIGHT };
    memcpy(i.std_ctl, ctl, sizeof ctl);
    i.std_steer_axis = 0;
    i.std_pedal_axis = 3;
    i.vbtn_axis[0] = 2;
    i.vbtn_axis[1] = 5;
    return i;
}

static void test_pad_std(void) {
    static const int want[CB_STD_COUNT] = { CB_ACT_THROTTLE, CB_ACT_LOOK_BACK, CB_ACT_BRAKE, CB_ACT_CAMERA, CB_ACT_RESET_CAR, CB_ACT_NONE,
        CB_ACT_HYD_PRESSURE, CB_ACT_HYD_BOUNCE, CB_ACT_NITROUS, CB_ACT_HANDBRAKE, CB_ACT_SHIFT_DOWN, CB_ACT_SHIFT_UP,
        CB_ACT_ENTER_RACE, CB_ACT_WORLD_MAP, CB_ACT_STEER_LEFT, CB_ACT_STEER_RIGHT };
    CbPadInfo info = std_info();
    StInputDevice d;
    memset(&d, 0, sizeof d);
    memset(d.binding, 0x55, sizeof d.binding);
    cb_pad_defaults(&d, &info);
    int bound = 0;
    for (int e = 0; e < CB_STD_COUNT; e++) {
        CHECK(d.binding[info.std_ctl[e]] == want[e], "std element %d -> action %d (got %d)", e, want[e], d.binding[info.std_ctl[e]]);
        if (want[e]) bound++;
    }
    int n = 0;
    for (int k = 0; k < 32; k++) if (d.binding[k]) n++;
    CHECK(n == bound && n == 15, "exactly the 15 elements are bound, Start and the rest are free (%d)", n);
    CHECK(d.assigned[CB_ROLE_STEER] && d.axis[CB_ROLE_STEER] == 0 && d.dir[CB_ROLE_STEER] == 1, "std: left stick X steers");
    CHECK(d.assigned[CB_ROLE_COMBINED] && d.axis[CB_ROLE_COMBINED] == 12 && d.dir[CB_ROLE_COMBINED] == 1 && d.combined_mode, "std: right stick Y is the combined pedals");
    /* an element without a control is skipped, the others stay */
    CbPadInfo part = std_info();
    part.std_ctl[CB_STD_L1] = -1;
    part.std_pedal_axis = -1;
    memset(&d, 0, sizeof d);
    cb_pad_defaults(&d, &part);
    n = 0;
    for (int k = 0; k < 32; k++) if (d.binding[k]) n++;
    CHECK(n == 14 && d.binding[4] == CB_ACT_NONE && d.binding[0] == CB_ACT_THROTTLE, "std_ctl -1 is skipped (%d bound)", n);
    CHECK(!d.assigned[CB_ROLE_COMBINED] && d.assigned[CB_ROLE_STEER], "std_pedal_axis -1: pedals not assigned");
    /* a wheel ignores std, a known vid/pid keeps its table, std = 0 ignores the tables */
    CbPadInfo wh = std_info(), ref = std_info();
    wh.wheel = 1;
    ref.wheel = 1;
    ref.std = 0;
    StInputDevice a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    cb_pad_defaults(&a, &wh);
    cb_pad_defaults(&b, &ref);
    CHECK(!memcmp(&a, &b, sizeof a), "a wheel ignores std");
    CbPadInfo off = std_info(), plain = std_info();
    off.std = 0;
    memset(&plain, 0, sizeof plain);
    plain.buttons = off.buttons; plain.axes_mask = off.axes_mask;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    cb_pad_defaults(&a, &off);
    cb_pad_defaults(&b, &plain);
    CHECK(!memcmp(&a, &b, sizeof a), "std = 0: the tables are ignored, output is the positional default");
    CbPadInfo kn = std_info(), knref = std_info();
    kn.vid = knref.vid = 0x046D; kn.pid = knref.pid = 0xC216;
    knref.std = 0;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    cb_pad_defaults(&a, &kn);
    cb_pad_defaults(&b, &knref);
    CHECK(!memcmp(&a, &b, sizeof a) && a.binding[0] == 2, "a known vid/pid pad keeps its own table");
    /* Reset Keys To Default: a second call gives the same record, the guid is kept */
    memset(&a, 0, sizeof a);
    memset(a.guid, 0xCD, 16);
    cb_pad_defaults(&a, &info);
    b = a;
    a.binding[0] = CB_ACT_HUD;
    a.axis[CB_ROLE_STEER] = 8;
    cb_pad_defaults(&a, &info);
    CHECK(!memcmp(&a, &b, sizeof a) && a.guid[7] == 0xCD, "defaults twice: same record, guid kept");
}

/* ---- tvc05: joystick defaults ------------------------------------------------------------------------------ */
static void test_pad_defaults(void) {
    J *v = jload("tvc05_joystick_defaults.json");
    if (!v) return;
    const J *gen = jget(v, "generic");
    for (int i = 0; i < gen->n; i++) {
        const J *c = gen->kid[i];
        CbPadInfo info;
        memset(&info, 0, sizeof info);
        info.buttons = (int)jnum(c, "buttons", 0);
        info.axes_mask = axes_mask_of(jget(c, "axes_present"));
        info.wheel = jbool(c, "wheel");
        StInputDevice d;
        memset(&d, 0, sizeof d);
        cb_pad_defaults(&d, &info);
        check_binding(&d, jget(c, "binding32"), 32, jstrv(c, "name"));
        check_axes(&d, jget(c, "axes"), jstrv(c, "name"));
    }
    const J *known = jget(v, "known_devices");
    for (int i = 0; i < known->n; i++) {
        const J *c = known->kid[i];
        CbPadInfo info;
        memset(&info, 0, sizeof info);
        info.vid = hexkey(jstrv(c, "vid"));
        info.pid = hexkey(jstrv(c, "pid"));
        info.buttons = 12;
        info.axes_mask = 0x3;
        StInputDevice d;
        memset(&d, 0, sizeof d);
        memset(d.binding, 0x55, sizeof d.binding);
        cb_pad_defaults(&d, &info);
        check_binding(&d, jget(c, "game20"), 20, "known device");
        for (int j = 20; j < 32; j++) CHECK(d.binding[j] == 0, "known device %s:%s binding[%d]", jstrv(c, "vid"), jstrv(c, "pid"), j);
    }
    /* the guid survives a reset to defaults */
    CbPadInfo info = { .buttons = 4, .axes_mask = 0x3 };
    StInputDevice d;
    memset(&d, 0, sizeof d);
    memset(d.guid, 0xAB, 16);
    cb_pad_defaults(&d, &info);
    CHECK(d.guid[0] == 0xAB && d.guid[15] == 0xAB, "defaults keep the guid");
    /* a device without axes keeps the open defaults and no assigned flag */
    CbPadInfo noax = { .buttons = 4 };
    cb_pad_defaults(&d, &noax);
    CHECK(!d.assigned[0] && !d.assigned[3] && d.combined_mode == 1 && d.axis[3] == 4, "no axes: open defaults only");
    jfree(v);
    test_pad_std();
}

/* ---- tvc06: joystick capture -------------------------------------------------------------------------------- */
static void check_display(const StInputDevice *d, const J *disp, const char *what) {
    for (int k = 0; k < disp->n; k++) {
        int action = atoi(disp->key[k]);
        for (int col = 0; col < 2; col++) {
            const J *w = disp->kid[k]->kid[col];
            CbCtl c = cb_pad_display(d, action, col);
            if (w->t == J_NULL) CHECK(c.kind == CB_CTL_NONE, "%s: action %d col %d should be empty (kind %d)", what, action, col, c.kind);
            else {
                const char *kind = w->kid[0]->s;
                CbCtlKind wk = !strcmp(kind, "axis") ? CB_CTL_AXIS : !strcmp(kind, "button") ? CB_CTL_BUTTON : CB_CTL_POV;
                CHECK(c.kind == wk && c.index == (int)w->kid[1]->num, "%s: action %d col %d shows kind %d index %d, want %s %d",
                      what, action, col, c.kind, c.index, kind, (int)w->kid[1]->num);
            }
        }
    }
}
static void test_pad_capture(void) {
    J *v = jload("tvc06_joystick_capture.json");
    if (!v) return;
    const J *pd = jget(v, "pov_direction");
    for (int i = 0; i < pd->n; i++)
        CHECK(cb_pov_direction((int)jnum(pd->kid[i], "centidegrees", -1)) == (int)jnum(pd->kid[i], "dir", -1),
              "pov %d", (int)jnum(pd->kid[i], "centidegrees", -1));
    CHECK(cb_pov_direction(4500) == CB_DIR_RIGHT && cb_pov_direction(13500) == CB_DIR_RIGHT &&
          cb_pov_direction(22500) == CB_DIR_LEFT && cb_pov_direction(31500) == CB_DIR_LEFT, "exact diagonals are left/right");
    const J *cases = jget(v, "cases");
    StInputDevice base, d;
    load_state(&base, jget(cases->kid[0], "binding32"), jget(cases->kid[0], "axes"));
    check_display(&base, jget(cases->kid[0], "display"), "default display");
    d = base;
    for (int i = 1; i < cases->n; i++) {
        const J *c = cases->kid[i];
        const char *name = jstrv(c, "name");
        if (strncmp(name, "Brake axis", 10)) d = base;                  /* only the last case chains */
        const J *cap = jget(c, "capture");
        int action = (int)jnum(cap, "action", 0), column = (int)jnum(cap, "column", 0);
        const char *input = jstrv(cap, "input");
        const J *arg = jget(cap, "arg");
        CbPadSample s0, s1;
        memset(&s0, 0, sizeof s0);
        for (int p = 0; p < 4; p++) s0.pov[p] = -1;
        s1 = s0;
        if (!strcmp(input, "button")) s1.buttons = 1u << (int)arg->num;
        else if (!strcmp(input, "pov")) s1.pov[(int)arg->kid[0]->num] = (int)arg->kid[1]->num;
        else s1.axis[(int)arg->num / 4] = 2000 * (int)jnum(cap, "sign", 0);
        CbPadCapture pc;
        cb_pad_capture_begin(&pc);
        CHECK(!cb_pad_capture_frame(&d, &pc, action, column, &s0, 0), "case \"%s\": first frame only takes the reference", name);
        CHECK(cb_pad_capture_frame(&d, &pc, action, column, &s1, 0) == 1, "case \"%s\": assigned", name);
        CHECK(!pc.active, "case \"%s\": capture ended", name);
        check_binding(&d, jget(c, "binding32_after"), 32, name);
        check_axes(&d, jget(c, "axes_after"), name);
        check_display(&d, jget(c, "display_after"), name);
    }
    /* rules the vectors do not spell out */
    StInputDevice e = base;
    CbPadSample a, b;
    memset(&a, 0, sizeof a);
    for (int p = 0; p < 4; p++) a.pov[p] = -1;
    b = a;
    CbPadCapture pc;
    cb_pad_capture_begin(&pc);
    a.buttons = 1u << 3;                                           /* held when the capture starts: not taken */
    cb_pad_capture_frame(&e, &pc, CB_ACT_CAMERA, 0, &a, 0);
    CHECK(cb_pad_capture_frame(&e, &pc, CB_ACT_CAMERA, 0, &a, 0) == 0, "a button held at the start is not assigned");
    b = a;
    b.buttons |= 1u << 5;
    b.pov[0] = 9000;                                               /* D5: the POV is not read after a button */
    CHECK(cb_pad_capture_frame(&e, &pc, CB_ACT_CAMERA, 0, &b, 0) == 1 && e.binding[5] == CB_ACT_CAMERA && e.binding[19] == CB_ACT_STEER_RIGHT,
          "button wins over a POV of the same frame");
    cb_pad_capture_begin(&pc);                                     /* axes only for actions 1..4; wheel threshold 500 */
    memset(&a, 0, sizeof a);
    for (int p = 0; p < 4; p++) a.pov[p] = -1;
    b = a;
    b.axis[0] = 600;
    cb_pad_capture_frame(&e, &pc, CB_ACT_NITROUS, 0, &a, 0);
    CHECK(cb_pad_capture_frame(&e, &pc, CB_ACT_NITROUS, 0, &b, 0) == 0, "an axis cannot take a non-axis action");
    cb_pad_capture_begin(&pc);
    cb_pad_capture_frame(&e, &pc, CB_ACT_THROTTLE, 0, &a, 0);
    CHECK(cb_pad_capture_frame(&e, &pc, CB_ACT_THROTTLE, 0, &b, 0) == 0, "600 is below the 1000 threshold");
    cb_pad_capture_begin(&pc);
    cb_pad_capture_frame(&e, &pc, CB_ACT_THROTTLE, 0, &a, 1);
    CHECK(cb_pad_capture_frame(&e, &pc, CB_ACT_THROTTLE, 0, &b, 1) == 1, "600 passes the wheel threshold of 500");
    /* axis scan order X Rx Y Ry Z Rz s0 s1: Rx wins over Y when both moved */
    cb_pad_capture_begin(&pc);
    memset(&a, 0, sizeof a);
    for (int p = 0; p < 4; p++) a.pov[p] = -1;
    b = a;
    b.axis[1] = 5000; b.axis[3] = 5000;
    StInputDevice f = base;
    cb_pad_capture_frame(&f, &pc, CB_ACT_STEER_LEFT, 0, &a, 0);
    cb_pad_capture_frame(&f, &pc, CB_ACT_STEER_LEFT, 0, &b, 0);
    CHECK(f.axis[CB_ROLE_STEER] == 12 && f.dir[CB_ROLE_STEER] == -1, "scan order picks Rx first (axis %d dir %d)", f.axis[0], f.dir[0]);
    jfree(v);
}

/* ---- tvc07: navigation --------------------------------------------------------------------------------------- */
static CbBindings *new_bindings(int wheel) {
    CbBindings *b = (CbBindings *)calloc(1, sizeof *b);
    cb_bindings_defaults(b);
    if (wheel >= 0) {
        CbPad *p = &b->pad[0];
        p->present = 1;
        memset(p->guid, 0x11, 16);
        p->info.buttons = 10;
        p->info.axes_mask = 0x3;
        p->info.wheel = wheel;
        memcpy(p->dev.guid, p->guid, 16);
        cb_pad_defaults(&p->dev, &p->info);
    }
    return b;
}
static const char *fake_key_name(void *u, int sc) { (void)u; return SDL_GetScancodeName((SDL_Scancode)sc); }
static const char *fake_ctl_name(void *u, int pad, CbCtl c) {
    static char buf[32];
    (void)u;
    snprintf(buf, sizeof buf, c.kind == CB_CTL_AXIS ? "Axis%d.%d" : "Pov%d.%d", pad, c.index);
    return buf;
}
static const CmPlatform k_plat = { NULL, fake_key_name, fake_ctl_name };
static CmInput input_of(const char *s) {
    return !strcmp(s, "Up") ? CM_IN_UP : !strcmp(s, "Down") ? CM_IN_DOWN : !strcmp(s, "Left") ? CM_IN_LEFT : CM_IN_RIGHT;
}
static void test_navigation(void) {
    J *v = jload("tvc07_navigation.json");
    if (!v) return;
    const J *cases = jget(v, "cases");
    for (int ci = 0; ci < cases->n; ci++) {
        const J *c = cases->kid[ci];
        int rows = (int)jnum(c, "rows", 0);
        CbBindings *b = new_bindings(rows == 16 ? 1 : -1);
        ControlsMenu m;
        memset(&m, 0, sizeof m);
        cm_open(&m, b, &k_plat);
        if (rows == 16) {                                           /* the wheel list: via the device selector */
            cm_input(&m, CM_IN_UP);
            cm_input(&m, CM_IN_RIGHT);
            cm_input(&m, CM_IN_DOWN);
        }
        CHECK(cm_row_count(&m) == rows, "case %d: %d rows", ci, cm_row_count(&m));
        const J *exp = jget(c, "expected");
        for (int i = 0; i < exp->n; i++) {
            const J *e = exp->kid[i];
            cm_input(&m, input_of(jstrv(e, "input")));
            const J *f = jget(e, "focus");
            if (f->t == J_NULL) CHECK(m.focus_row == -1, "case %d step %d: focus should be the selector", ci, i);
            else {
                CHECK(m.focus_row + 1 == (int)jnum(f, "row", -1) && m.focus_col == (int)jnum(f, "column", -1),
                      "case %d step %d (%s): focus %d/%d, want %d/%d", ci, i, jstrv(e, "input"), m.focus_row + 1, m.focus_col,
                      (int)jnum(f, "row", -1), (int)jnum(f, "column", -1));
            }
            CHECK(m.top + 1 == (int)jnum(e, "top", -1), "case %d step %d: top %d, want %d", ci, i, m.top + 1, (int)jnum(e, "top", -1));
        }
        free(b);
    }
    jfree(v);
}

/* ---- tvc08: names -------------------------------------------------------------------------------------------- */
static void test_names(void) {
    J *v = jload("tvc08_display_names.json");
    if (!v) return;
    const J *list = jget(v, "english");
    for (int i = 0; i < list->n; i++) {
        const J *e = list->kid[i];
        const J *os = jget(e, "os_name");
        char out[CB_NAME_MAX];
        cb_format_name(os->t == J_STR ? os->s : NULL, !strcmp(jstrv(e, "device"), "joystick"), out);
        CHECK(!strcmp(out, jstrv(e, "shown")), "name %d: \"%s\" -> \"%s\", want \"%s\"", i, os->t == J_STR ? os->s : "null", out, jstrv(e, "shown"));
    }
    char out[CB_NAME_MAX], longname[100];
    memset(longname, 'Z', 80);
    longname[80] = 0;
    cb_format_name(longname, 0, out);
    CHECK(!strcmp(out, "Z"), "a name over 64 bytes is cut to its first character");
    cb_format_name("Shift", 1, out);
    CHECK(!strcmp(out, "Shift"), "the modifier replacement is a keyboard rule");
    cb_format_name("Button", 1, out);
    CHECK(!strcmp(out, "Button"), "Button without a number is unchanged");
    jfree(v);
}

/* ---- tvc09: identities --------------------------------------------------------------------------------------- */
static void test_package_keys(void) {
    J *v = jload("tvc09_package.json");
    if (!v) return;
    const J *pk = jget(v, "package");
    CHECK(ug_hash_upper("UI_Options_PC_Controller.fng") == hexkey(jstrv(pk, "key")), "package key");
    const J *objs = jget(v, "objects");
    for (int i = 0; i < objs->n; i++) {
        const char *name = jstrv(objs->kid[i], "name");
        if (name[0] != '(' && !strstr(name, "(hash)")) CHECK(ug_hash_upper(name) == hexkey(jstrv(objs->kid[i], "hash")), "object %s hash", name);
    }
    const J *tex = jget(v, "texture_keys_in_GLOBALB");
    for (int i = 0; i < tex->n; i++) CHECK(ug_hash_upper(tex->key[i]) == hexkey(tex->kid[i]->s), "texture %s key", tex->key[i]);
    const J *others = jget(v, "other_packages");
    for (int i = 0; i < others->n; i++)
        CHECK(ug_hash_upper(jstrv(others->kid[i], "name")) == hexkey(jstrv(others->kid[i], "key")), "package %s key", jstrv(others->kid[i], "name"));
    jfree(v);
}

/* ---- the screen state machine --------------------------------------------------------------------------------- */
static void tick(ControlsMenu *m, int n) { while (n--) cm_tick(m); }

static void test_capture_flow(void) {
    CbBindings *b = new_bindings(0);                               /* keyboard + gamepad */
    ControlsMenu m;
    memset(&m, 0, sizeof m);
    cm_open(&m, b, &k_plat);
    CHECK(m.ndev == 2 && m.cur == 0 && cm_device(&m)->cls == CB_CLASS_KEYBOARD, "opens on the keyboard");
    CHECK(cm_title_key(&m) == 0x5CF08EB7u && cm_instruction_key(&m) == CM_KEY_INSTR_CONFIGURE, "title and instruction on entry");
    CHECK(cm_arrows_visible(&m), "arrows are shown with two devices");
    char t[CB_NAME_MAX];
    cm_cell_text(&m, 0, 0, t);
    CHECK(!strcmp(t, "Left"), "row 1 primary \"%s\"", t);
    cm_cell_text(&m, 2, 1, t);
    CHECK(!strcmp(t, "W"), "throttle secondary \"%s\"", t);
    /* Accept starts a capture on Throttle (row 3), Secondary */
    cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_RIGHT);
    cm_input(&m, CM_IN_ACCEPT);
    CHECK(m.capturing && m.locked && m.cap_pulse && cm_instruction_key(&m) == CM_KEY_INSTR_CAPTURE, "capture started with Pulse");
    cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_BACK); cm_input(&m, CM_IN_RESET);
    CHECK(m.focus_row == 2 && !cm_take_leave(&m) && !cm_take_reset(&m), "menu input is ignored while capturing");
    cm_pad_frame(&m, &(CbPadSample){0});                           /* joystick input does nothing in a keyboard capture */
    cm_key_down(&m, SDL_SCANCODE_ESCAPE);
    CHECK(!m.capturing && !cm_take_modified(&m), "Escape cancels and changes nothing");
    CHECK(cb_kb_display(&b->kb, CB_ACT_THROTTLE, 1) == SDL_SCANCODE_W, "table unchanged after Escape");
    CHECK(!cm_input_enabled(&m) && cm_instruction_key(&m) == CM_KEY_INSTR_CAPTURE, "still locked in the end frame");
    cm_tick(&m);
    cm_input(&m, CM_IN_RESET);
    CHECK(!cm_take_reset(&m), "reset is refused until input is back");
    cm_tick(&m);
    CHECK(cm_input_enabled(&m) && cm_instruction_key(&m) == CM_KEY_INSTR_ASSIGN, "input and the third instruction come back one frame later");
    cm_input(&m, CM_IN_RESET);
    CHECK(cm_take_reset(&m), "reset requests the dialog");
    /* assign V to Throttle Secondary */
    cm_input(&m, CM_IN_ACCEPT);
    cm_key_down(&m, SDL_SCANCODE_V);
    CHECK(cb_kb_display(&b->kb, CB_ACT_THROTTLE, 1) == SDL_SCANCODE_V && cm_take_modified(&m), "V assigned");
    cm_cell_text(&m, 2, 1, t);
    CHECK(!strcmp(t, "V"), "cell shows V");
    tick(&m, 2);
    CHECK(cm_instruction_key(&m) == CM_KEY_INSTR_ASSIGN, "the third text stays");
    /* a click starts a capture without Pulse and focuses the cell */
    cm_pointer_click(&m, cm_cell_cx(0), cm_row_top(1) + 5);
    CHECK(m.capturing && !m.cap_pulse && m.focus_row == 1 && m.focus_col == 0, "click: capture without Pulse");
    cm_key_down(&m, SDL_SCANCODE_ESCAPE);
    tick(&m, 2);
    /* hover focuses a visible cell, and the title focuses the selector */
    cm_pointer_move(&m, cm_cell_cx(1), cm_row_top(4) + 5);
    CHECK(m.focus_row == 4 && m.focus_col == 1, "hover focuses the cell");
    cm_pointer_move(&m, 0, -165);
    CHECK(m.focus_row == -1, "hover over the title focuses the selector");
    cm_pointer_move(&m, 500, 400);
    CHECK(m.focus_row == -1, "hover elsewhere keeps the focus");
    /* reset acts on the displayed device (D5) and the dialog's OK performs it */
    cm_input(&m, CM_IN_DOWN);
    cm_reset_device(&m);
    CHECK(cb_kb_display(&b->kb, CB_ACT_THROTTLE, 1) == SDL_SCANCODE_W, "reset restores W on Throttle");
    /* device change: wraps both ways, rebuilds rows, focus stays on the selector */
    cm_input(&m, CM_IN_UP);
    cm_input(&m, CM_IN_UP);
    CHECK(m.focus_row == -1, "Up on row 1 reaches the selector");
    cm_input(&m, CM_IN_RIGHT);
    CHECK(m.cur == 1 && cm_title_key(&m) == 0xF91FBA4Du && m.focus_row == -1 && m.top == 0 && cm_row_count(&m) == 18, "Right: joypad");
    cm_cell_text(&m, 0, 0, t);
    CHECK(!strcmp(t, "Axis0.0"), "joypad steering shows its axis through the platform: \"%s\"", t);
    cm_cell_text(&m, 0, 1, t);
    CHECK(!strcmp(t, "Pov0.0"), "second column shows the POV: \"%s\"", t);
    cm_cell_text(&m, 2, 0, t);
    CHECK(!strcmp(t, "Axis0.4"), "throttle on the combined axis: \"%s\"", t);
    cm_cell_text(&m, 2, 1, t);
    CHECK(!strcmp(t, "Button 2"), "throttle button 1 is shown as Button 2: \"%s\"", t);
    cm_cell_text(&m, 11, 0, t);
    CHECK(!strcmp(t, "---"), "nothing bound shows ---: \"%s\"", t);
    cm_input(&m, CM_IN_RIGHT);
    CHECK(m.cur == 0 && cm_title_key(&m) == 0x5CF08EB7u, "Right wraps to the keyboard");
    cm_input(&m, CM_IN_LEFT);
    CHECK(m.cur == 1, "Left wraps back");
    /* joystick capture through the menu: a keyboard key does nothing, a button ends it */
    cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_DOWN); cm_input(&m, CM_IN_DOWN);   /* row 4 Back/Reverse */
    CHECK(m.focus_row == 3, "focus on row 4");
    cm_input(&m, CM_IN_ACCEPT);
    cm_key_down(&m, SDL_SCANCODE_V);
    CHECK(m.capturing, "a key does not end a joystick capture");
    CbPadSample s;
    memset(&s, 0, sizeof s);
    for (int p = 0; p < 4; p++) s.pov[p] = -1;
    cm_pad_frame(&m, &s);
    s.buttons = 1u << 12;
    cm_pad_frame(&m, &s);
    CHECK(!m.capturing && b->pad[0].dev.binding[12] == CB_ACT_BRAKE && cm_take_modified(&m), "button 12 assigned to Back / Reverse");
    tick(&m, 2);
    /* Escape cancels a joystick capture too */
    cm_input(&m, CM_IN_ACCEPT);
    cm_key_down(&m, SDL_SCANCODE_ESCAPE);
    CHECK(!m.capturing && !cm_take_modified(&m), "Escape cancels a joystick capture");
    tick(&m, 2);
    /* reset of the joypad restores its defaults */
    cm_reset_device(&m);
    CHECK(b->pad[0].dev.binding[12] == 0 && b->pad[0].dev.binding[0] == CB_ACT_BRAKE, "joypad reset");
    /* Back */
    cm_input(&m, CM_IN_BACK);
    CHECK(cm_take_leave(&m) && !cm_take_leave(&m), "Back leaves once");
    /* re-entry starts on the keyboard again (D5) */
    cm_open(&m, b, &k_plat);
    CHECK(m.cur == 0 && m.focus_row == 0 && m.focus_col == 0 && m.instruction == 0, "re-entry: keyboard, row 1, first instruction");
    free(b);
}

static void test_single_device(void) {
    CbBindings *b = new_bindings(-1);
    ControlsMenu m;
    memset(&m, 0, sizeof m);
    cm_open(&m, b, &k_plat);
    CHECK(!cm_arrows_visible(&m), "arrows hidden with one device");
    cm_input(&m, CM_IN_UP);
    unsigned blink = m.blink_serial;
    cm_input(&m, CM_IN_RIGHT);
    cm_input(&m, CM_IN_LEFT);
    CHECK(m.cur == 0 && m.blink_serial == blink, "Left/Right on the selector do nothing with one device");
    cm_pointer_click(&m, 265, -158);
    CHECK(m.cur == 0 && !m.capturing, "arrow click does nothing with one device");
    CHECK(cm_scrollbar_visible(&m) && cm_scroll_fraction(&m) == 0.0f, "scrollbar visible, tab at the top");
    cm_input(&m, CM_IN_DOWN);
    for (int i = 0; i < 17; i++) cm_input(&m, CM_IN_DOWN);
    CHECK(cm_scroll_fraction(&m) == 1.0f, "tab at the bottom on the last row");
    cm_input(&m, CM_IN_UP);
    for (int i = 0; i < 20; i++) cm_input(&m, CM_IN_UP);
    CHECK(m.focus_row == -1, "up to the selector");
    CHECK(cm_scroll_fraction(&m) == 0.0f || m.focus_row == -1, "selector row");
    /* pointer: arrows of a two-device list focus the selector and change the device */
    free(b);
    b = new_bindings(1);
    cm_open(&m, b, &k_plat);
    cm_pointer_click(&m, 265, -158);
    CHECK(m.cur == 1 && m.focus_row == -1 && cm_row_count(&m) == 16 && cm_title_key(&m) == 0x14D4BD3Bu, "right arrow click: wheel list");
    cm_pointer_click(&m, -266, -158);
    CHECK(m.cur == 0 && cm_row_count(&m) == 18, "left arrow click: back to the keyboard");
    free(b);
}

/* ---- persistence --------------------------------------------------------------------------------------------- */
static void test_store(void) {
    StRegStore s;
    CHECK(st_reg_open_memory(&s) == ST_OK, "memory store");
    CbKeyTable t, d, def;
    memset(&t, 0, sizeof t);
    cb_kb_reset(&t);
    def = t;
    CHECK(cs_kb_load(&s, &d) == 0, "absent value: not loaded");
    cb_kb_capture(&t, CB_ACT_NITROUS, 0, SDL_SCANCODE_N);
    cb_kb_capture(&t, CB_ACT_CAMERA, 1, SDL_SCANCODE_UP);        /* steals Up from Throttle */
    CHECK(cs_kb_save(&s, &t) == ST_OK, "save");
    memset(&d, 0, sizeof d);
    cb_kb_reset(&d);
    CHECK(cs_kb_load(&s, &d) == 1 && !memcmp(&d, &t, sizeof t), "round trip through the memory store");
    CHECK(memcmp(&d, &def, sizeof d) != 0, "round trip differs from the defaults");
    /* malformed values leave the table alone */
    CbKeyTable e = def;
    st_reg_set_string(&s, CS_KB_VALUE, "1:1,2,3");
    CHECK(cs_kb_load(&s, &e) == 0 && !memcmp(&e, &def, sizeof e), "short list rejected");
    st_reg_set_string(&s, CS_KB_VALUE, "9:1");
    CHECK(cs_kb_load(&s, &e) == 0, "unknown version rejected");
    char big[400] = "1:";
    for (int i = 0; i < CB_KB_ENTRIES; i++) strcat(big, i ? ",x" : "x");
    st_reg_set_string(&s, CS_KB_VALUE, big);
    CHECK(cs_kb_load(&s, &e) == 0 && !memcmp(&e, &def, sizeof e), "garbage rejected");
    /* out-of-range keys are dropped, duplicates keep the first holder */
    char buf[400] = "1:";
    for (int i = 0; i < CB_KB_ENTRIES; i++) {
        char one[16];
        snprintf(one, sizeof one, i ? ",%d" : "%d", i == 0 ? 4 : i == 1 ? 4 : i == 2 ? 9999 : 0);
        strcat(buf, one);
    }
    st_reg_set_string(&s, CS_KB_VALUE, buf);
    CHECK(cs_kb_load(&s, &e) == 1 && e.e[0].key == 4 && e.e[1].key == 0 && e.e[2].key == 0, "range and duplicate rules");
    st_reg_close(&s);

    /* the INI store of the game (D4): a file written by one run is read by the next */
    char path[256];
    snprintf(path, sizeof path, "build/controls_test_tmp.ini");
    remove(path);
    StRegStore f;
    CHECK(st_reg_open_ini(&f, path) == ST_OK, "open ini");
    CHECK(cs_kb_save(&f, &t) == ST_OK, "save to the ini");
    st_reg_close(&f);
    CHECK(st_reg_open_ini(&f, path) == ST_OK, "reopen ini");
    memset(&d, 0, sizeof d);
    cb_kb_reset(&d);
    CHECK(cs_kb_load(&f, &d) == 1 && !memcmp(&d, &t, sizeof t), "keyboard bindings survive a restart (ini)");
    st_reg_close(&f);
    remove(path);

    /* joystick records in the profile */
    StProfile *p = (StProfile *)calloc(1, sizeof *p);
    st_profile_defaults(p, 0);
    snprintf(p->name, sizeof p->name, "%s", CS_PROFILE_NAME);
    CbBindings *b = new_bindings(0);
    CbBindings *b2 = new_bindings(0);
    CHECK(cs_pads_load(p, b) == 0, "empty profile: nothing loaded");
    b->pad[0].dev.binding[7] = CB_ACT_HYD_BOUNCE;
    cb_pad_assign_axis(&b->pad[0].dev, CB_ACT_THROTTLE, 4, -1);
    cs_pads_store(p, b);
    CHECK(!memcmp(p->input[1].guid, b->pad[0].guid, 16), "stored in slot 1 (slot 0 is the keyboard's)");
    uint8_t *file = (uint8_t *)malloc(ST_PROFILE_FILE_SIZE);
    size_t n = st_profile_serialize(p, file, ST_PROFILE_FILE_SIZE);
    CHECK(n == ST_PROFILE_FILE_SIZE, "profile serialises");
    StProfile *q = (StProfile *)calloc(1, sizeof *q);
    CHECK(st_profile_parse(file, n, q) == ST_OK, "profile parses back");
    CHECK(cs_pads_load(q, b2) == 1, "record matched by guid");
    CHECK(!memcmp(&b2->pad[0].dev, &b->pad[0].dev, sizeof b->pad[0].dev), "pad record round trip");
    /* two pads with the same guid take the two records in order */
    b->pad[1] = b->pad[0];
    b->pad[1].dev.binding[7] = CB_ACT_PAUSE;
    cs_pads_store(p, b);
    memset(b2, 0, sizeof *b2);
    cb_bindings_defaults(b2);
    b2->pad[0] = b->pad[0]; b2->pad[1] = b->pad[1];
    b2->pad[0].dev.binding[7] = 0; b2->pad[1].dev.binding[7] = 0;
    CHECK(cs_pads_load(p, b2) == 2 && b2->pad[0].dev.binding[7] == CB_ACT_HYD_BOUNCE && b2->pad[1].dev.binding[7] == CB_ACT_PAUSE,
          "duplicate guids take the records in order");
    /* an invalid record leaves the defaults */
    p->input[1].axis[0] = 5;
    memcpy(b2->pad[0].guid, p->input[1].guid, 16);
    int before = b2->pad[0].dev.binding[7];
    cs_pads_load(p, b2);
    CHECK(b2->pad[0].dev.axis[0] != 5 && b2->pad[0].dev.binding[7] == before, "invalid record ignored");
    free(file); free(q); free(p); free(b); free(b2);
}

/* ---- the Controls screen inside the options hub ------------------------------------------------------------------ */
static void settle_om(OptionsMenu *m) { om_update(m, 1.0f); }

static void test_options_flow(void) {
    OmConfig cfg;
    om_config_default(&cfg, OM_CTX_INGAME);
    StGraphics g;
    StGfxCaps caps;
    st_gfx_defaults(&g);
    st_gfx_caps_full(&caps, 5);
    OptionsMenu *m = (OptionsMenu *)calloc(1, sizeof *m);
    CbBindings *b = new_bindings(0);
    CmPlatform plat = k_plat;

    /* resources missing: the hub keeps its four baseline entries (R3) */
    om_set_controls(m, b, &plat, 0);
    om_open_hub(m, &cfg, &g, &caps, NULL);
    CHECK(om_hub_count(m) == 4 && m->hub.n == 4 && om_hub_entry_of(m, 4) == NULL, "no Controls entry without resources");
    om_close(m);

    /* available: a fifth entry with the Controls icon and label key, no English fallback text */
    om_set_controls(m, b, &plat, 1);
    om_open_hub(m, &cfg, &g, &caps, NULL);
    const OmHubEntry *e = om_hub_entry_of(m, 4);
    CHECK(om_hub_count(m) == 5 && m->hub.n == 5 && e && e->icon_key == 0xA04A7B26u && e->label_key == 0x04DC6DB5u && e->label == NULL && e->page == 4,
          "fifth hub entry");
    CHECK(om_hub_entry(4) == NULL, "the baseline table is unchanged");
    CHECK(!strcmp(om_package_name(OM_CTX_INGAME, OM_SCREEN_CONTROLS), "UI_Options_PC_Controller.fng") &&
          om_package_key(OM_CTX_INGAME, OM_SCREEN_CONTROLS) == 0xE0EE65E6u && om_exit_ms(OM_CTX_INGAME, OM_SCREEN_CONTROLS) == 0, "package");
    uint32_t key = 0;
    CHECK(om_header(OM_CTX_INGAME, OM_SCREEN_CONTROLS, 0, &key) == NULL && key == CM_KEY_HEADER, "header is a key only");
    for (int i = 0; i < 4; i++) om_input(m, OM_IN_RIGHT);
    CHECK(om_hub_focus(m) == 4 && om_hub_remembered(m) == 5, "hub focus on Controls");
    om_input(m, OM_IN_ACCEPT);
    CHECK(m->exiting && om_screen(m) == OM_SCREEN_HUB && m->page == 4, "Accept runs the hub exit");
    settle_om(m);
    CHECK(om_screen(m) == OM_SCREEN_CONTROLS && cm_is_open(&m->cm) && m->cm.cur == 0, "Controls opened on the keyboard");

    /* a capture through the hub-level API; Back writes nothing itself and asks the host to persist */
    om_input(m, OM_IN_ACCEPT);
    CHECK(om_controls_capturing(m), "capturing");
    om_key_down(m, SDL_SCANCODE_ESCAPE);
    settle_om(m); settle_om(m);
    om_input(m, OM_IN_DOWN); om_input(m, OM_IN_DOWN);                 /* Throttle */
    om_input(m, OM_IN_ACCEPT);
    om_key_down(m, SDL_SCANCODE_V);
    CHECK(cb_kb_display(&b->kb, CB_ACT_THROTTLE, 0) == SDL_SCANCODE_V, "Up stolen by V on Throttle primary");
    settle_om(m); settle_om(m);
    CHECK(om_poll_event(m) == OM_EV_NONE, "nothing is persisted before Back");
    /* reset through the dialog: Cancel keeps, OK resets */
    om_input(m, OM_IN_RESET);
    CHECK(om_dialog_is_open(m), "reset opens the dialog");
    CHECK(m->dialog.spec.message_key == CM_KEY_RESET_MESSAGE && m->dialog.spec.label_key[0] == CM_KEY_OK && m->dialog.spec.label_key[1] == CM_KEY_CANCEL &&
          !strcmp(m->dialog.spec.package, "GenericDialog_SMALL.fng") && m->dialog.spec.message == NULL && m->dialog.spec.label[0] == NULL,
          "dialog texts are keys of the language file only");
    CHECK(om_dialog_focus(m) == 1, "the dialog opens on Cancel");
    om_input(m, OM_IN_ACCEPT);
    settle_om(m);
    CHECK(!om_dialog_is_open(m) && cb_kb_display(&b->kb, CB_ACT_THROTTLE, 0) == SDL_SCANCODE_V, "Cancel changes nothing");
    om_input(m, OM_IN_RESET);
    om_input(m, OM_IN_LEFT);
    om_input(m, OM_IN_ACCEPT);
    settle_om(m);
    CHECK(!om_dialog_is_open(m) && cb_kb_display(&b->kb, CB_ACT_THROTTLE, 0) == SDL_SCANCODE_UP, "OK resets the keyboard");
    om_input(m, OM_IN_ACCEPT);                                           /* edit again, then leave */
    om_key_down(m, SDL_SCANCODE_N);
    settle_om(m); settle_om(m);
    om_input(m, OM_IN_BACK);
    CHECK(om_screen(m) == OM_SCREEN_HUB && !m->exiting, "Back returns to the hub at once");
    CHECK(om_hub_focus(m) == 4 && om_hub_remembered(m) == 5, "the hub reopens on Controls");
    CHECK(om_poll_event(m) == OM_EV_CONTROLS_CHANGED && om_poll_event(m) == OM_EV_NONE, "changed bindings are reported once");
    /* leaving without a change reports nothing */
    om_input(m, OM_IN_ACCEPT); settle_om(m);
    om_input(m, OM_IN_BACK);
    CHECK(om_screen(m) == OM_SCREEN_HUB && om_poll_event(m) == OM_EV_NONE, "no change, no event");
    /* Start does nothing on the screen */
    om_input(m, OM_IN_ACCEPT); settle_om(m);
    om_input(m, OM_IN_START);
    CHECK(om_screen(m) == OM_SCREEN_CONTROLS && om_poll_event(m) == OM_EV_NONE, "Start is ignored on Controls");
    /* pointer: a click on a cell captures; the dialog is modal */
    om_pointer_click(m, cm_cell_cx(0), cm_row_top(2) + 4);
    CHECK(om_controls_capturing(m), "pointer click captures");
    om_key_down(m, SDL_SCANCODE_ESCAPE);
    settle_om(m); settle_om(m);
    om_pointer_move(m, cm_cell_cx(1), cm_row_top(3) + 4);
    CHECK(m->cm.focus_row == 3 && m->cm.focus_col == 1, "hover focuses");
    om_input(m, OM_IN_RESET);
    om_pointer_move(m, cm_cell_cx(0), cm_row_top(0) + 4);
    CHECK(m->cm.focus_row == 3, "hover is ignored under the dialog");
    om_close(m);
    CHECK(!om_is_open(m) && !cm_is_open(&m->cm), "close drops the screen");
    /* the lent bindings survive a reopen of the hub */
    om_open_hub(m, &cfg, &g, &caps, NULL);
    CHECK(om_hub_count(m) == 5, "bindings and availability survive om_open_hub");
    free(m); free(b);
}

int main(int argc, char **argv) {
    if (argc > 1) g_dir = argv[1];
    test_labels();
    test_rows();
    test_kb_defaults();
    test_kb_capture();
    test_pad_defaults();
    test_pad_capture();
    test_navigation();
    test_names();
    test_package_keys();
    test_capture_flow();
    test_single_device();
    test_store();
    test_options_flow();
    printf("controls_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
