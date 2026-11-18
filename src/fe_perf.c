/* fe_perf.c -- see fe_perf.h. Package binding follows fe_lot.c (identity set, header fit) and fe_garage.c
 * (carousel pages). Language keys and texture keys are retail data; their names are given where known. */
#include "fe_perf.h"
#include "fe_binding.h"
#include "fe_screen.h"
#include "ugui/ugui_core.h"
#include "ugui/ugui_canvas.h"
#include "ugui/ugui_face.h"

#include <stdio.h>
#include <string.h>

/* [inferred] Header width, as FEL_HEADER_W: the header's left edge to the rail's right end. */
#define FEP_HEADER_W 578.0f
#define FEP_LIST_ROWS 4      /* rows inside UI_BuyPerformanceParts' list box */

/* [data] PERF_CATEGORY_* labels (PERF_CATEGORY_WEIGHT_REDUCTION, _TRANSMISSION, _NOS, _ENGINE, _TURBO, _SUSPENSION,
 * _BRAKE, _ECU, _TIRES) and PERFORMANCE_* icons (WEIGHT_REDUCTION, TRANSMISSION, NITROUS, ENGINE, TURBO, SUSPENSION,
 * BRAKES, ECU, TIRES) in the order of the career categories. */
static const uint32_t k_cat_label[FEP_CATS] = { 0x758d5e3cu, 0x73ad2f72u, 0xa7886f38u, 0x026eea5eu, 0xabd0ff14u,
                                                0x9c34c75fu, 0xaa8959adu, 0xa7884765u, 0xabca6aefu };
static const uint32_t k_cat_icon[FEP_CATS] = { 0xea57d2c4u, 0x01a29ffau, 0x9701bde4u, 0xc15c94e6u, 0x06ef789cu,
                                               0x178475e7u, 0xbaa23a28u, 0x502abeedu, 0x06e8e477u };
/* [data] PERFORMANCE_PACKAGE_1..3 "Street/Pro/Extreme Package", PERFORMANCE_CHOOSE_SPECIFIC; icons
 * PERFORMANCE_ICON_PACKAGE_1..3 and PERFORMANCE_ICON_CHOOSE_SPECIFIC_PARTS. */
static const uint32_t k_pkg_label[FEP_LEVELS + 1] = { 0xdf3fd1d6u, 0xb75ba92cu, 0xb75ba92du, 0xb75ba92eu };
static const uint32_t k_pkg_icon[FEP_LEVELS + 1] = { 0xc7d3f6a7u, 0xb8c8c0d4u, 0xb8c8c0d5u, 0xb8c8c0d6u };
#define K_SHOP_TITLE     0xb938cb4bu   /* "Performance Shop" (the category header's own label) */
#define K_CONFIRM_PKG    0x94f4c140u   /* "Would you like to purchase and install this performance package?" */
#define K_CONFIRM_PART   0x44b7167fu   /* "Would you like to purchase this part?" */
#define K_OWNED_PKG      0xb84a37fau   /* "This performance package is already installed!" */
#define K_OWNED_PART     0xda15d7e5u   /* "The part you are trying to apply is already installed!" */
#define K_NO_MONEY       0xd1ac60beu   /* CUSTOMIZE_NOT_ENOUGH_MONEY */
#define K_LOCKED         0xc9f7f7e5u   /* "LOCKED" */
#define K_PURCHASED      0x1e6178feu   /* PURCHASED */
#define K_BANK           0x3298ccdbu   /* HUD_BANK "Bank:" */

/* ---- state machine ---- */

static const FePerfLevel *level(const FePerf *p, int cat, int lvl) {
    return cat >= 0 && cat < FEP_CATS && lvl >= 1 && lvl <= FEP_LEVELS ? &p->shop.cat[cat].lvl[lvl - 1] : NULL;
}
int fpr_cat(const FePerf *p) { return p && p->ncats ? p->cats[p->cat_sel] : -1; }
int fpr_level(const FePerf *p) { return p && p->page != FEP_PAGE_CAT && p->npkgs ? p->pkgs[p->pkg_sel] : -1; }
static int clampi(int v, int n) { return v < 0 || n <= 0 ? 0 : v >= n ? n - 1 : v; }

static void build_cats(FePerf *p) {
    p->ncats = 0;
    for (int c = 0; c < FEP_CATS; c++) {
        int any = 0;
        for (int l = 0; l < FEP_LEVELS; l++) any |= p->shop.cat[c].lvl[l].n > 0;
        if (any) p->cats[p->ncats++] = c;
    }
    p->cat_sel = clampi(p->cat_sel, p->ncats);
}
static void build_pkgs(FePerf *p) {
    int cat = fpr_cat(p);
    p->npkgs = 0;
    if (cat < 0) return;
    for (int l = 1; l <= FEP_LEVELS; l++) if (level(p, cat, l)->n) p->pkgs[p->npkgs++] = l;
    p->pkgs[p->npkgs++] = FEP_SPECIFIC;
}
static void build_rows(FePerf *p) {
    int cat = fpr_cat(p);
    p->nrows = 0;
    if (cat < 0) return;
    for (int l = 1; l <= FEP_LEVELS; l++) {
        const FePerfLevel *L = level(p, cat, l);
        for (int i = 0; i < L->n && i < FEP_PARTS; i++) { p->row_lvl[p->nrows] = l; p->row_part[p->nrows] = i; p->nrows++; }
    }
}

void fpr_open(FePerf *p, const FePerfShop *shop) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    if (shop) p->shop = *shop;
    for (int c = 0; c < FEP_CATS; c++) for (int l = 0; l < FEP_LEVELS; l++)
        if (p->shop.cat[c].lvl[l].n > FEP_PARTS) p->shop.cat[c].lvl[l].n = FEP_PARTS;
    p->header_scale = 1;
    build_cats(p);
}

void fpr_refresh(FePerf *p, const FePerfShop *shop, FepMsg msg) {
    if (!p) return;
    FepPage page = p->page;
    int cat = p->cat_sel, pkg = p->pkg_sel, row = p->row_sel;
    float scale = p->header_scale;
    FePerf keep = *p;
    fpr_open(p, shop);
    p->carousel = keep.carousel; p->view = keep.view; p->list = keep.list; p->header_scale = scale;
    p->cat_sel = clampi(cat, p->ncats);
    p->page = p->ncats ? page : FEP_PAGE_CAT;
    build_pkgs(p); p->pkg_sel = clampi(pkg, p->npkgs);
    build_rows(p); p->row_sel = clampi(row, p->nrows);
    if (p->page == FEP_PAGE_PARTS && !p->nrows) p->page = FEP_PAGE_PKG;
    p->msg = msg;
}

/* The caption a purchase of (cat, lvl, part; part -1 = package) is refused with, FEP_MSG_NONE when it may go ahead. */
static FepMsg refusal(const FePerf *p, int cat, int lvl, int part) {
    const FePerfLevel *L = level(p, cat, lvl);
    if (!L || !L->n) return FEP_MSG_LOCKED;
    if (!L->open) return FEP_MSG_LOCKED;
    if (part < 0 ? L->owned_all : L->part[part].owned) return FEP_MSG_OWNED;
    if ((long long)(part < 0 ? L->price : L->part[part].price) > p->shop.bank) return FEP_MSG_NO_FUNDS;
    return FEP_MSG_NONE;
}
static FepEvent ask_or_buy(FePerf *p, int cat, int lvl, int part) {
    FepMsg m = refusal(p, cat, lvl, part);
    if (m) { p->confirm = 0; p->msg = m; return FEP_EV_NONE; }
    if (!p->confirm) { p->confirm = 1; p->msg = FEP_MSG_CONFIRM; return FEP_EV_NONE; }
    p->confirm = 0; p->msg = FEP_MSG_NONE;
    p->buy_cat = cat; p->buy_lvl = lvl; p->buy_comp = part < 0 ? -1 : level(p, cat, lvl)->part[part].comp;
    return FEP_EV_BUY;
}
static int first_wanted_pkg(const FePerf *p) {
    int cat = fpr_cat(p);
    for (int i = 0; i < p->npkgs; i++) {
        const FePerfLevel *L = level(p, cat, p->pkgs[i]);
        if (L && L->open && !L->owned_all) return i;
    }
    return 0;
}
static int first_wanted_row(const FePerf *p) {
    int cat = fpr_cat(p);
    for (int i = 0; i < p->nrows; i++) {
        const FePerfLevel *L = level(p, cat, p->row_lvl[i]);
        if (L->open && !L->part[p->row_part[i]].owned) return i;
    }
    return 0;
}

FepEvent fpr_input(FePerf *p, int in) {
    if (!p) return FEP_EV_NONE;
    int *sel = p->page == FEP_PAGE_CAT ? &p->cat_sel : p->page == FEP_PAGE_PKG ? &p->pkg_sel : &p->row_sel;
    int n = p->page == FEP_PAGE_CAT ? p->ncats : p->page == FEP_PAGE_PKG ? p->npkgs : p->nrows;
    switch (in) {
    case FES_IN_LEFT: case FES_IN_RIGHT: case 5: case 6: {
        if ((in == 5 || in == 6) && p->page != FEP_PAGE_PARTS) return FEP_EV_NONE;   /* carousels browse left/right */
        int next = *sel + (in == FES_IN_LEFT || in == 5 ? -1 : 1);
        p->confirm = 0; p->msg = FEP_MSG_NONE;
        if (next >= 0 && next < n) *sel = next;
        return FEP_EV_NONE;
    }
    case FES_IN_ACCEPT: {
        if (!n) return FEP_EV_NONE;
        int cat = fpr_cat(p);
        if (p->page == FEP_PAGE_CAT) {
            p->page = FEP_PAGE_PKG; p->confirm = 0; p->msg = FEP_MSG_NONE;
            build_pkgs(p); p->pkg_sel = first_wanted_pkg(p);
            return FEP_EV_PAGE;
        }
        if (p->page == FEP_PAGE_PKG) {
            int lvl = p->pkgs[p->pkg_sel];
            if (lvl == FEP_SPECIFIC) {
                build_rows(p);
                if (!p->nrows) return FEP_EV_NONE;
                p->page = FEP_PAGE_PARTS; p->confirm = 0; p->msg = FEP_MSG_NONE; p->row_sel = first_wanted_row(p);
                return FEP_EV_PAGE;
            }
            return ask_or_buy(p, cat, lvl, -1);
        }
        return ask_or_buy(p, cat, p->row_lvl[p->row_sel], p->row_part[p->row_sel]);
    }
    case FES_IN_BACK:
        if (p->confirm) { p->confirm = 0; p->msg = FEP_MSG_NONE; return FEP_EV_NONE; }
        p->msg = FEP_MSG_NONE;
        if (p->page == FEP_PAGE_CAT) return FEP_EV_LEAVE;
        p->page = p->page == FEP_PAGE_PARTS ? FEP_PAGE_PKG : FEP_PAGE_CAT;
        return FEP_EV_PAGE;
    default: return FEP_EV_NONE;
    }
}

const char *fpr_package(const FePerf *p) {
    if (!p) return NULL;
    return p->page == FEP_PAGE_CAT ? "UI_ChoosePerformanceCategory.fng" :
           p->page == FEP_PAGE_PKG ? "UI_ChoosePerformancePackage.fng" : "UI_BuyPerformanceParts.fng";
}

/* ---- package binding ---- */

static int set_index(const char *pkg) {
    for (unsigned k = 0; k < sizeof fe_binding_sets / sizeof fe_binding_sets[0]; k++)
        if (!strcmp(fe_binding_sets[k].package, pkg)) return (int)k;
    return -1;
}
static int node_of(const GvScreen *s, uint32_t guid) {
    for (int i = 0; i < s->scene.n; i++) if (s->scene.nodes[i].obj->guid == guid) return i;
    return -1;
}
static void text_at(GvScreen *s, uint32_t guid, const char *text) {
    int i = node_of(s, guid);
    if (i >= 0) { s->scene.nodes[i].text_override = text; s->scene.nodes[i].app_hidden = !text; }
}
static void hide_at(GvScreen *s, uint32_t guid, int hidden) {
    int i = node_of(s, guid);
    if (i >= 0) s->scene.nodes[i].app_hidden = hidden;
}

int fpr_bind(FePerf *p, GvScreen *s, const UgAssets *a) {
    if (!p || !s) return 0;
    p->header_scale = 1;                  /* a fresh scene carries the authored header size */
    const char *pkg = fpr_package(p);
    if (!gv_screen_load(s, a, pkg)) return 0;
    int set = set_index(pkg);
    if (set < 0) { snprintf(s->status, sizeof s->status, "%s identity set missing", pkg); return 0; }
    for (int i = 0; i < fe_binding_sets[set].count; i++)
        if (fe_identity_find(s->pk, &fe_binding_sets[set].identity[i]) < 0) {
            snprintf(s->status, sizeof s->status, "%s identity mismatch (GUID %05x)", pkg, (unsigned)fe_binding_sets[set].identity[i].guid);
            return 0;
        }
    if (p->page == FEP_PAGE_PARTS) return gv_list_bind_rows(&p->list, s);
    if (p->page == FEP_PAGE_PKG) {
        /* [inferred] Platform binding alias: the package screen names its focused-entry title 0x0666176b where the other
         * carousel packages use ICON_TITLE; this screen's own parse is renamed so the shared carousel binding applies. */
        for (int i = 0; i < s->pkg.nobj; i++)
            if (s->pkg.obj[i].guid == 0xdf142u && s->pkg.obj[i].name_hash == 0x0666176bu) s->pkg.obj[i].name_hash = ug_hash_upper("ICON_TITLE");
    }
    gc_init(&p->carousel, GC_RAIL_PAUSE);
    return gv_carousel_bind(&p->view, s, &p->carousel);
}

/* Platform adaptation (as fel_sync): a long prompt is scaled to the header width rather than clipped. */
static void fit_header(FePerf *p, GvScreen *s, int node) {
    if (node < 0) return;
    UgNode *n = &s->scene.nodes[node];
    float tw = 0, th = 0, k = 1;
    const UgFace *was = ug_text_face();
    if (n->face) ug_text_set_face(n->face);
    if (ug_text_face()) ug_text_measure(p->caption, 1, 0, 0, UG_GLYPHS_MOUSE, &tw, &th);
    ug_text_set_face(was);
    if (tw > FEP_HEADER_W) k = FEP_HEADER_W / tw;
    if (k != p->header_scale) {
        float sz[3] = { n->obj->size[0] * k, n->obj->size[1] * k, n->obj->size[2] };
        ug_scene_app_write(&s->scene, node, UG_P_SIZE, 3, sz);
        p->header_scale = k;
    }
}

static void money(char *out, size_t cap, long long v) { snprintf(out, cap, "$%lld", v); }

int fpr_sync(FePerf *p, GvScreen *s, float ms) {
    if (!p || !s || !s->loaded) return 0;
    const UgAssets *a = s->assets;
    int cat = fpr_cat(p);
    int parts = p->page == FEP_PAGE_PARTS;
    switch (p->msg) {
    case FEP_MSG_CONFIRM: snprintf(p->caption, sizeof p->caption, "%s", gv_text(a, parts ? K_CONFIRM_PART : K_CONFIRM_PKG, "")); break;
    case FEP_MSG_OWNED: snprintf(p->caption, sizeof p->caption, "%s", gv_text(a, parts ? K_OWNED_PART : K_OWNED_PKG, "")); break;
    case FEP_MSG_NO_FUNDS: snprintf(p->caption, sizeof p->caption, "%s", gv_text(a, K_NO_MONEY, "")); break;
    case FEP_MSG_LOCKED: snprintf(p->caption, sizeof p->caption, "%s", gv_text(a, K_LOCKED, "")); break;
    default:
        snprintf(p->caption, sizeof p->caption, "%s", p->page == FEP_PAGE_CAT || cat < 0 ? gv_text(a, K_SHOP_TITLE, "") : gv_text(a, k_cat_label[cat], ""));
        break;
    }
    /* [inferred] "Bank:" (HUD_BANK) followed by the amount in the authored "$999999999" form, as fel_sync. */
    snprintf(p->cash, sizeof p->cash, "%s $%lld", gv_text(a, K_BANK, "Bank:"), p->shop.bank);
    p->status[0] = 0;

    if (parts) {
        GvListRow rows[FEP_MAX_ROWS];
        for (int i = 0; i < p->nrows; i++) {
            const FePerfLevel *L = level(p, cat, p->row_lvl[i]);
            const FePerfPart *pt = &L->part[p->row_part[i]];
            if (pt->owned) snprintf(p->data[i], sizeof p->data[i], "%s", gv_text(a, K_PURCHASED, "Purchased"));
            else if (!L->open) snprintf(p->data[i], sizeof p->data[i], "%s", gv_text(a, K_LOCKED, "LOCKED"));
            else money(p->data[i], sizeof p->data[i], pt->price);
            rows[i] = (GvListRow){ gv_text(a, pt->name_key, ""), p->data[i], 1 };
        }
        /* [inferred] The authored list box (backing and scroll arrows) holds 4 rows, the shared list view windows 8:
         * pass a 4-row window that follows the focus. */
        int top = p->row_sel >= FEP_LIST_ROWS ? p->row_sel - FEP_LIST_ROWS + 1 : 0;
        int shown = p->nrows - top < FEP_LIST_ROWS ? p->nrows - top : FEP_LIST_ROWS;
        gv_list_sync_rows(&p->list, rows + top, shown, p->row_sel - top, ms);
        /* [inferred] Platform adaptation: the stats panel (no stats implemented) carries the caption and the bank. */
        text_at(s, 0xe0676u, p->caption);
        text_at(s, 0xe06a7u, gv_text(a, K_BANK, "Bank:"));
        money(p->total, sizeof p->total, p->shop.bank);
        text_at(s, 0xe06bau, p->total);
        static const uint32_t unused[] = { 0xe06a6u, 0xe06b9u, 0xe0661u, 0xe0acbu, 0xe0ac9u, 0xe0acau, 0xe0accu, 0xe0ad8u, 0xe0ad7u, 0xe0adau, 0xe0ad9u };
        for (unsigned k = 0; k < sizeof unused / sizeof unused[0]; k++) hide_at(s, unused[k], 1);
        ug_scene_relayout(&s->scene);
        return 1;
    }

    int n = p->page == FEP_PAGE_CAT ? p->ncats : p->npkgs;
    int sel = p->page == FEP_PAGE_CAT ? p->cat_sel : p->pkg_sel;
    if (n > GC_MAX) n = GC_MAX;
    for (int i = 0; i < n; i++) {
        uint32_t icon = p->page == FEP_PAGE_CAT ? k_cat_icon[p->cats[i]] : k_pkg_icon[p->pkgs[i]];
        uint32_t label = p->page == FEP_PAGE_CAT ? k_cat_label[p->cats[i]] : k_pkg_label[p->pkgs[i]];
        p->entry[i] = (GvEntry){ gv_texture(a, icon), gv_text(a, label, NULL) };
        if (!p->entry[i].tex || !p->entry[i].label) {
            snprintf(p->status, sizeof p->status, "performance shop icon 0x%08x or caption 0x%08x unavailable", (unsigned)icon, (unsigned)label);
            return 0;
        }
    }
    for (int i = n; i < p->view.nslots; i++) s->scene.nodes[p->view.slot[i]].app_hidden = 1;
    gc_set_entries(&p->carousel, n, NULL);
    gc_snap_to(&p->carousel, sel);
    gc_update(&p->carousel, ms / 1000);
    gv_carousel_apply(&p->view, &p->carousel, p->entry, n, p->caption, 0, 0);
    fit_header(p, s, p->view.header);

    if (p->page == FEP_PAGE_CAT) {
        int locked = 1;
        for (int l = 1; l <= FEP_LEVELS && cat >= 0; l++) locked &= !(level(p, cat, l)->n && level(p, cat, l)->open);
        hide_at(s, 0xe520fu, !locked);                       /* lock icon: no package of the category is open */
        text_at(s, 0xe4fe8u, p->cash);                       /* PLAYER_CASH */
        hide_at(s, 0xe4fe7u, 1);                             /* PLAYER NAME shares the cash position */
    } else {
        int lvl = fpr_level(p);
        const FePerfLevel *L = level(p, cat, lvl);
        hide_at(s, 0xe520du, !(L && !L->open));              /* lock icon on a locked package */
        hide_at(s, 0xe52dfu, 1);
        text_at(s, 0xe504au, p->cash);
        hide_at(s, 0xe5049u, 1);
        hide_at(s, 0xe1240u, 1);                             /* unimplemented stats: hidden rather than example values */
        hide_at(s, 0xe0f41u, !L);                            /* package contents: not for "Choose Individual Parts" */
        static const uint32_t name_g[FEP_PARTS] = { 0xe067bu, 0xe067eu, 0xe0681u, 0xe0684u };
        static const uint32_t price_g[FEP_PARTS] = { 0xe06e0u, 0xe06e1u, 0xe06e2u, 0xe06e3u };
        static const uint32_t back_g[FEP_PARTS] = { 0xe0c1fu, 0xe0c23u, 0xe0c10u, 0xe05e4u };
        static const uint32_t mini_g[FEP_PARTS] = { 0xe0d2bu, 0xe0d21u, 0xe0d26u, 0xe0d10u };
        for (int i = 0; i < FEP_PARTS; i++) {
            hide_at(s, mini_g[i], 1);                        /* [inferred] mini icon meaning undecoded: not shown */
            if (!L || i >= L->n) { hide_at(s, name_g[i], 1); hide_at(s, price_g[i], 1); hide_at(s, back_g[i], 1); continue; }
            hide_at(s, back_g[i], 0);
            text_at(s, name_g[i], gv_text(a, L->part[i].name_key, ""));
            if (L->part[i].owned) snprintf(p->prices[i], sizeof p->prices[i], "%s", gv_text(a, K_PURCHASED, "Purchased"));
            else money(p->prices[i], sizeof p->prices[i], L->part[i].price);
            text_at(s, price_g[i], p->prices[i]);
        }
        if (L) {
            if (L->owned_all) snprintf(p->total, sizeof p->total, "%s", gv_text(a, K_PURCHASED, "Purchased"));
            else money(p->total, sizeof p->total, L->price);
            text_at(s, 0xe5ccbu, p->total);
            long long saved = (long long)L->full - L->price;
            money(p->discount, sizeof p->discount, saved > 0 ? saved : 0);
            text_at(s, 0xe0befu, saved > 0 && !L->owned_all ? p->discount : NULL);
            hide_at(s, 0xe06a7u, !(saved > 0 && !L->owned_all));   /* "10% Discount" label */
        }
    }
    ug_scene_relayout(&s->scene);
    return 1;
}
