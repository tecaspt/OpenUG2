/* synth_pkgs.h -- synthetic in-memory screen packages for the presentation tests. They only carry
 * the object names/hashes, sizes and script names that SPEC_GUI_ART publishes; no retail data.
 * Needs <ugui_scene.h>, gui_timing.h, and defines static helpers (include once per test). */
#ifndef TOOLS_SYNTH_PKGS_H
#define TOOLS_SYNTH_PKGS_H
#include "../src/gui_timing.h"
#include "../src/ugui/ugui_scene.h"
#include "../src/ugui/ugui_ui.h"

static uint32_t hn(const char *s) { return ug_hash_upper(s); }
static uint32_t hnf(const char *f, int n) { char b[64]; snprintf(b, sizeof b, f, n); return ug_hash_upper(b); }

/* ---- synthetic package builder --------------------------------------------------------------- */
#define MAXOBJ 1024
#define MAXSCR 1024
typedef struct Pkg {
    UgFngObject obj[MAXOBJ];
    int n;
    UgScript scripts[MAXSCR];
    int ns;
    UgTrack tracks[MAXSCR * 2];
    int nt;
    UgKey keys[MAXSCR * 8];
    int nk;
    UgEvent events[MAXSCR * 2];
    int ne;
    UgFngCmd cmds[MAXSCR];
    int nc;
    UgFngResponse resp[MAXSCR];
    int nr;
    UgFngPackage pk;
} Pkg;

static UgFngObject *add(Pkg *p, int type, const char *name, float x, float y, float w, float h) {
    UgFngObject *o = &p->obj[p->n];
    memset(o, 0, sizeof *o);
    o->type = (uint32_t)type;
    o->guid = (uint32_t)(0x1000 + p->n);
    o->name_hash = name ? hn(name) : 0;
    o->res_index = -1;
    o->color = UG_RGBA(255, 255, 255, 255);
    o->pos[0] = x; o->pos[1] = y; o->pos[2] = 1;
    o->size[0] = w; o->size[1] = h; o->size[2] = 1;
    o->uv[2] = o->uv[3] = 1;
    o->rot[3] = 1;
    p->n++;
    return o;
}
static UgFngObject *nth(Pkg *p, int i) { return &p->obj[i]; }
/* scripts are attached per object: the object's scripts array is a slice of p->scripts */
static UgScript *script(Pkg *p, UgFngObject *o, uint32_t id, uint32_t len) {
    if (o->nscripts == 0) o->scripts = &p->scripts[p->ns];
    UgScript *s = &p->scripts[p->ns++];
    memset(s, 0, sizeof *s);
    s->id = id;
    s->length = len;
    o->nscripts++;
    return s;
}
static UgTrack *track(Pkg *p, UgScript *s, int type, int prop, const float *base, int nkeys, const int *times, const float (*vals)[4]) {
    if (s->ntracks == 0) s->tracks = &p->tracks[p->nt];
    UgTrack *t = &p->tracks[p->nt++];
    memset(t, 0, sizeof *t);
    s->ntracks++;
    t->type = (uint8_t)type;
    t->vsize = (uint8_t)(ug_type_ncomp(type) * 4);
    t->interp = UG_INTERP_LINEAR;
    t->length = s->length;
    t->prop = (uint32_t)prop;
    if (base) { t->has_base = 1; t->base.t = -1; memcpy(t->base.v, base, sizeof(float) * (size_t)ug_type_ncomp(type)); }
    t->keys = &p->keys[p->nk];
    for (int i = 0; i < nkeys; i++) {
        p->keys[p->nk].t = times[i];
        memcpy(p->keys[p->nk].v, vals[i], sizeof(float) * 4);
        p->nk++;
    }
    t->nkeys = nkeys;
    return t;
}
static void respond(Pkg *p, UgFngObject *o, uint32_t msg, uint32_t cmd, uint32_t arg) {
    if (o->nresp == 0) o->resp = &p->resp[p->nr];
    UgFngResponse *r = &p->resp[p->nr++];
    r->msg = msg;
    r->cmds = &p->cmds[p->nc];
    r->ncmds = 1;
    p->cmds[p->nc].id = cmd;
    p->cmds[p->nc].arg = arg;
    p->nc++;
    o->nresp++;
}
static UgEvent *event(Pkg *p, UgScript *s, uint32_t msg, uint32_t target, uint32_t t_ms) {
    if (s->nevents == 0) s->events = &p->events[p->ne];
    UgEvent *e = &p->events[p->ne++];
    e->msg = msg; e->target = target; e->t_ms = t_ms;
    s->nevents++;
    return e;
}
/* the event-handler object of every package: Init -> call (enter broadcast), exit script, DIM/UNDIM */
static UgFngObject *handler(Pkg *p, const char *exit_script, uint32_t exit_len, uint32_t exit_complete_ms) {
    UgFngObject *o = add(p, UG_OBJ_TEXT, NULL, 0, -2000, 1, 1);
    o->name_hash = GT_HANDLER_HASH;
    UgScript *init = script(p, o, UG_SCRIPT_INITIAL, 0);
    init->has_chain = 1; init->chain = hn("call");
    UgScript *call = script(p, o, hn("call"), 320);
    event(p, call, GT_MSG_ENTER, 0, 0);
    event(p, call, GT_MSG_INIT_COMPLETE, 0xFFFFFFFFu, 320);
    UgScript *ex = script(p, o, hn(exit_script), exit_len);
    event(p, ex, GT_MSG_EXIT, 0, 0);
    event(p, ex, GT_MSG_EXIT_COMPLETE, 0xFFFFFFFFu, exit_complete_ms);
    UgScript *dim = script(p, o, GT_SCRIPT_DIM, 0);
    event(p, dim, UG_MSG_DIM, 0, 0);
    UgScript *undim = script(p, o, GT_SCRIPT_UNDIM, 0);
    event(p, undim, UG_MSG_UNDIM, 0, 0);
    return o;
}
static void finish(Pkg *p) {
    memset(&p->pk, 0, sizeof p->pk);
    p->pk.obj = p->obj;
    p->pk.nobj = p->n;
}
static int idx(const UgScene *s, const char *name) { return ug_scene_find_name(s, hn(name)); }

/* fake assets: textures by key with a tiny opaque bitmap, optional strings */
typedef struct Fake {
    UgAssets a;
    UgTexture tex[32];
    uint8_t px[4];
} Fake;
static void fake_init(Fake *f, const uint32_t *keys, int n) {
    memset(f, 0, sizeof *f);
    f->px[0] = f->px[1] = f->px[2] = f->px[3] = 255;
    for (int i = 0; i < n; i++) f->tex[i] = (UgTexture){ .key = keys[i], .w = 1, .h = 1, .rgba = f->px };
    f->a.tex.t = f->tex;
    f->a.tex.n = n;
    ug_texlib_index(&f->a.tex);
    ug_strtab_init(&f->a.strings);
    f->a.have_strings = 1;
    f->a.main_face = -1;
}
static void fake_free(Fake *f) { free(f->a.tex.order); ug_strtab_free(&f->a.strings); }
static void strtab_put(UgStrTab *t, uint32_t hash, const char *text) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 8;
        t->e = (UgStrEntry *)realloc(t->e, sizeof(UgStrEntry) * (size_t)t->cap);
    }
    int i = t->n++;
    while (i > 0 && t->e[i - 1].hash > hash) { t->e[i] = t->e[i - 1]; i--; }
    t->e[i].hash = hash;
    t->e[i].text = strdup(text);
}

/* An anchor ("master") object as the retail packages author it (AMEND1 A6): its Init script (stored =
 * Init: base key plus delta, A4) is automatically followed by a second authored script that makes the
 * object invisible and moves it to (chain_x, chain_y). Reading the anchor once that chain has run
 * therefore gives the wrong position. Every shared fixture builds its masters this way, so any suite
 * that reads an anchor late fails. The object's scripts must be added consecutively (one slice). */
#define SYNTH_CHAIN_SCRIPT 0x6048A18Bu
static void anchor_chain(Pkg *p, UgFngObject *o, float bx, float by, float cx, float cy, uint32_t init_len) {
    UgScript *init = script(p, o, UG_SCRIPT_INITIAL, init_len);
    init->has_chain = 1;
    init->chain = SYNTH_CHAIN_SCRIPT;
    const float base[4] = { bx, by, 1, 0 };
    const int t[1] = { 0 };
    const float d[1][4] = { { o->pos[0] - bx, o->pos[1] - by, 0, 0 } };
    track(p, init, UG_VT_VEC3, UG_P_POS, base, 1, t, d);
    UgScript *ch = script(p, o, SYNTH_CHAIN_SCRIPT, 600);
    const float cb[4] = { 255, 255, 255, 0 };
    track(p, ch, UG_VT_COLOR, UG_P_COLOR, cb, 0, NULL, NULL);
    const float pb[4] = { cx, cy, 1, 0 };
    track(p, ch, UG_VT_VEC3, UG_P_POS, pb, 0, NULL, NULL);
}
/* Re-authors an anchor built by anchor_chain: new stored centre/size, chained position and Init length. */
static void anchor_set(UgFngObject *o, float cx, float cy, float w, float h, float chx, float chy, uint32_t init_len) {
    o->pos[0] = cx; o->pos[1] = cy; o->size[0] = w; o->size[1] = h;
    UgScript *init = &o->scripts[0], *ch = &o->scripts[1];
    init->length = init_len;
    UgTrack *it = &init->tracks[0];
    it->length = init_len;
    it->base.v[0] = cx - 3; it->base.v[1] = cy + 1;
    it->keys[0].v[0] = 3; it->keys[0].v[1] = -1;
    UgTrack *pt = &ch->tracks[1];
    pt->base.v[0] = chx; pt->base.v[1] = chy;
}

static void build_carousel_pkg(Pkg *p, int rail_w, int blink_scripts) {
    UgFngObject *m = add(p, UG_OBJ_IMAGE, "OPTION_MASTER", 0.388f, -14.296f, 118, 59);
    anchor_chain(p, m, 0, -12.35f, 0, 170, 0);                    /* the D1 hazard: hidden and moved after Init (A6) */
    add(p, UG_OBJ_TEXT, "ICON_TITLE", -50, 20, 1, 1);
    add(p, UG_OBJ_TEXT, "Header_text", -280, -200, 1, 1);
    add(p, UG_OBJ_TEXT, "Back_button", -280, 200, 1, 1);
    add(p, UG_OBJ_TEXT, "Select_button", 280, 200, 1, 1);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Backing", 3, -52, (float)rail_w, 8);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Handle", -270, -52.3f, 16, 16);
    UgFngObject *l = add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_1", -250.291f, -12.769f, 26, 26);
    UgFngObject *r = add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_2", 259.14f, -12.769f, 26, 26);
    if (blink_scripts) {
        script(p, l, hn("Blink"), 400); script(p, l, hn("Static"), 0);
        script(p, r, hn("Blink"), 400); script(p, r, hn("Static"), 0);
    }
    for (int k = 1; k <= 10; k++) {
        char nm[32];
        snprintf(nm, sizeof nm, "option_%d", k);
        UgFngObject *o = add(p, UG_OBJ_IMAGE, nm, 0, 334.0f + 24 * (k - 1), 118, 59);
        UgScript *fi = script(p, o, hn("FadeIn"), 220);
        const float cb[4] = { 255, 255, 255, 255 };
        const int t[2] = { 40, 220 };
        const float dv[2][4] = { { 0, 0, 0, -255 }, { 0, 0, 0, 0 } };
        track(p, fi, UG_VT_COLOR, UG_P_COLOR, cb, 2, t, dv);
        respond(p, o, GT_MSG_ENTER, UG_CMD_START_SCRIPT, hn("FadeIn"));
    }
    handler(p, "Forward", 320, 320);
    finish(p);
}
static void build_list_pkg(Pkg *p, int nslots, int with_backing, int slider_value_obj) {
    UgFngObject *tm = add(p, UG_OBJ_IMAGE, "TITLE_MASTER", -151, -120, 240, 36);
    UgFngObject *dm = add(p, UG_OBJ_IMAGE, "DATA_MASTER", 126, -122, 216, 32);
    anchor_chain(p, tm, -154, -119, -132, -120, 0);               /* A6: the chained position must never be used */
    anchor_chain(p, dm, 123, -121, 109, -122, 0);
    add(p, UG_OBJ_TEXT, "Header_Text", -289.7f, -220, 0.8f, 0.8f);
    add(p, UG_OBJ_TEXT, "Header_text2", 288.3f, -208.3f, 0.75f, 0.75f);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Backing", 273, 2, 8, 282);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Handle", 273, -131, 16, 16);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_1", 273, -164, 20, 20);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Arrow_2", 273, 167, 20, 20);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Backing_End_1", 273, -140, 8, 8);
    add(p, UG_OBJ_IMAGE, "ScrollBar_Backing_End_2", 273, 144, 8, 8);
    add(p, UG_OBJ_IMAGE, "PLAYER", 0, 200, 50, 50);
    for (int n = 1; n <= nslots; n++) {
        UgFngObject *nm = add(p, UG_OBJ_TEXT, NULL, -271, 100 + n, 0.7f, 0.7f);
        nm->name_hash = hnf("OPTION_NAME_%d", n);
        nm->flags = UG_OF_BUTTON;
        UgScript *hl = script(p, nm, hn("Highlight"), 1200);
        const float cb[4] = { 50, 50, 50, 255 };
        const int tt[2] = { 0, 600 };
        const float dv[2][4] = { { 150, 190, 0, 0 }, { 0, 0, 0, 0 } };
        track(p, hl, UG_VT_COLOR, UG_P_COLOR, cb, 2, tt, dv);
        hl->flags = UG_SCRIPT_LOOP;
        script(p, nm, hn("UnHighlight"), 0);
        UgScript *fi = script(p, nm, hn("FadeIn"), 220);
        const float pb[4] = { -271, 100.0f + n, 1, 0 };
        const int ft[2] = { 0, 200 };
        const float pv[2][4] = { { 0, 12, 0, 0 }, { 0, 0, 0, 0 } };
        track(p, fi, UG_VT_VEC3, UG_P_POS, pb, 2, ft, pv);
        respond(p, nm, GT_MSG_ENTER, UG_CMD_START_SCRIPT, hn("FadeIn"));
        UgFngObject *d = add(p, UG_OBJ_TEXT, NULL, 100, 100, 0.7f, 0.7f);
        d->name_hash = hnf("OPTION_DATA_%d", n);
        d->justify = UG_J_CENTER;
        for (int side = 0; side < 2; side++) {
            UgFngObject *a = add(p, UG_OBJ_IMAGE, NULL, 0, 600, 24, 24);
            a->name_hash = hnf(side ? "RIGHT_ARROW_%d" : "Left_arrow_%d", n);
            script(p, a, hn("Highlight"), 180);
            script(p, a, hn("UnHighlight"), 140);
            script(p, a, hn("Blink"), 400);
        }
        if (with_backing) {
            UgFngObject *b = add(p, UG_OBJ_GROUP, NULL, 0, 600, 1, 1);
            b->name_hash = hnf("OPTION_BACKING_%d", n);
            script(p, b, hn("Highlight"), 100);
        }
        const char *parts[5] = { "SLIDER_BASE_%d", "SLIDER_KNOB_%d", "SLIDER_HANDLE_%d", "SLIDER_BEG_%d", "SLIDER_END_%d" };
        const float ps[5][2] = { { 234.92f, 32 }, { 16, 16 }, { 16, 5.75f }, { 8, 8 }, { 8, 8 } };
        for (int q = 0; q < 5; q++) {
            UgFngObject *o = add(p, UG_OBJ_IMAGE, NULL, q > 2 ? 900 : 0, 700, ps[q][0], ps[q][1]);
            o->name_hash = hnf(parts[q], n);
            script(p, o, hn("Highlight"), 100);
        }
        if (slider_value_obj) {
            UgFngObject *o = add(p, UG_OBJ_TEXT, NULL, 0, 0, 1, 1);
            o->name_hash = hnf("SLIDER_VALUE_%d", n);
        }
    }
    handler(p, "Forward", 500, 140);
    finish(p);
}

static void build_dialog_pkg(Pkg *p) {
    UgFngObject *m = add(p, UG_OBJ_TEXT, "message_blurb", 0, -28, 0.7f, 0.7f);
    (void)m;
    UgFngObject *b1 = add(p, UG_OBJ_GROUP, "button1", -151, 74, 1, 1);
    b1->flags = UG_OF_BUTTON;
    UgFngObject *b2 = add(p, UG_OBJ_GROUP, "Button2", 151, 74, 1, 1);
    b2->flags = UG_OF_BUTTON;
    UgFngObject *b3 = add(p, UG_OBJ_GROUP, "button3", 0, 74, 1, 1);
    b3->flags = UG_OF_BUTTON;
    script(p, b1, hn("Focus"), 100); script(p, b2, hn("Focus"), 100);
    script(p, b1, hn("Unfocus"), 100); script(p, b2, hn("Unfocus"), 100);
    respond(p, b1, UG_MSG_FOCUS_GAINED, UG_CMD_START_SCRIPT, hn("Focus"));
    respond(p, b2, UG_MSG_FOCUS_GAINED, UG_CMD_START_SCRIPT, hn("Focus"));
    respond(p, b1, UG_MSG_FOCUS_LOST, UG_CMD_START_SCRIPT, hn("Unfocus"));
    respond(p, b2, UG_MSG_FOCUS_LOST, UG_CMD_START_SCRIPT, hn("Unfocus"));
    UgFngObject *l1 = add(p, UG_OBJ_TEXT, NULL, -158, 63, 1, 1); l1->name_hash = 0xDA473D2Fu;
    UgFngObject *l2 = add(p, UG_OBJ_TEXT, NULL, 140, 63, 1, 1); l2->name_hash = 0xDA473D30u;
    UgFngObject *l3 = add(p, UG_OBJ_TEXT, NULL, 0, 63, 1, 1); l3->name_hash = 0xDA473D31u;
    handler(p, "FORWARD", 140, 140);
    finish(p);
}

/* TASK-AL: the retail GenericDialog_ThreeButton.fng structure -- three full-width row buttons
 * stacked L1 bottom / L2 middle / L3 top, each with the authored Focus/UnFocus responses, plus the
 * message and the L1/L2/L3 captions. Node name hashes match dialog_bind; positions are the retail
 * composed row centres (164/120/76). The button3 name is the lowercase retail form (hash-equal to
 * Button2's). No retail data. */
static void build_three_button_pkg(Pkg *p) {
    add(p, UG_OBJ_TEXT, "message_blurb", 0, -60, 0.75f, 0.75f);
    static const float row_y[3] = { 164.0f, 120.0f, 76.0f };        /* L1 bottom, L2 middle, L3 top */
    static const uint32_t lbl[3] = { 0xDA473D2Fu, 0xDA473D30u, 0xDA473D31u };
    const char *btn_name[3] = { "button1", "Button2", "button3" };
    for (int i = 0; i < 3; i++) {
        UgFngObject *b = add(p, UG_OBJ_GROUP, btn_name[i], 0, row_y[i], 512, 42);
        b->flags = UG_OF_BUTTON;
        script(p, b, hn("Focus"), 100);
        script(p, b, hn("UnFocus"), 100);
        respond(p, b, UG_MSG_FOCUS_GAINED, UG_CMD_START_SCRIPT, hn("Focus"));
        respond(p, b, UG_MSG_FOCUS_LOST, UG_CMD_START_SCRIPT, hn("UnFocus"));
        UgFngObject *l = add(p, UG_OBJ_TEXT, NULL, 0, row_y[i] - 11, 0.75f, 0.75f);
        l->name_hash = lbl[i];
    }
    handler(p, "FORWARD", 140, 140);
    finish(p);
}

/* TASK-AL: the retail HelpDialog sheet structure -- a title (T), a body (M) and a back caption (B)
 * located by their retail node name hashes, plus the console controller-glyph hint beside the
 * caption. None of these is a button node: a help sheet has no scene focus, it only dismisses.
 * Positions differ per sheet size but do not affect binding. No retail data. */
static void build_help_pkg(Pkg *p) {
    UgFngObject *t = add(p, UG_OBJ_TEXT, NULL, -288, -101, 0.75f, 0.75f);
    t->name_hash = 0x426C7B4Du;                                       /* T: the "Help" title */
    UgFngObject *m = add(p, UG_OBJ_TEXT, "message_blurb", -281, -60, 0.9f, 0.9f);
    m->justify = 0;                                                 /* left-justified body (the retail help M) */
    UgFngObject *b = add(p, UG_OBJ_TEXT, NULL, 220, 87, 0.75f, 0.75f);
    b->name_hash = 0x426C7B4Eu;                                       /* B: the "Back" caption */
    UgFngObject *g = add(p, UG_OBJ_TEXT, NULL, 187, 87, 0.75f, 0.75f);
    g->name_hash = 0x35E8F0B2u;                                       /* "$JOY_EVENT_FENG_CANCEL$": console hint */
    handler(p, "FORWARD", 140, 140);
    finish(p);
}
#endif
