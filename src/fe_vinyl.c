/* Career graphics-shop vinyl flow. Node ids and language keys are verified against the
 * shipped UI_ChooseVinylLayer.fng and LANGUAGES/English.bin; docs/parity/VINYLS.md */
#include "fe_vinyl.h"
#include "fe_binding.h"
#include "fe_screen.h"
#include <stdio.h>
#include <string.h>

/* [data] Keys that all resolve in LANGUAGES/English.bin. The captions are retail's own:
 * CHOOSE_VINYL_LAYER on the layer page, PAINT_CHOOSE_VINYL_COLORS on the colour page,
 * CHOOSE_VINYL ("Apply Vinyl") once a design carries the row. Categories name themselves
 * through VINYL_CATEGORY_* and the layer slots through VINYL_LAYER_1..4. */
enum {
    KEY_CHOOSE_LAYER   = 0x2ac7d02du,   /* "Choose Vinyl Layer" */
    KEY_CHOOSE_COLORS  = 0xc034ce3du,   /* "Choose Vinyl Colors" */
    KEY_APPLY_VINYL    = 0x70b11cf1u,   /* "Apply Vinyl" */
    KEY_ADD_TO_SETUP   = 0x31fbc167u,   /* "Would you like to add this vinyl to your setup?" */
    KEY_CHANGE_OR_REMOVE = 0x6d7902cu,  /* "Would you like to change or remove this vinyl?" */
    KEY_REORDER        = 0xb9ac83ebu,   /* "Reorder Layers" */
    KEY_BANK           = 0x3298ccdbu,   /* "Bank:" */
    KEY_TOTAL          = 0x420aa71au,   /* "Total Cost:" */
    KEY_INSTALLED      = 0xed56be1fu,   /* "Installed" */
    KEY_NO_FUNDS       = 0xd1ac60beu,   /* "You do not have enough bank to purchase this item." */
    KEY_LOCKED         = 0xc9f7f7e5u,   /* "LOCKED" */
    KEY_ALREADY        = 0xda15d7e5u    /* "The part you are trying to apply is already installed!" */
};

/* [data] Category tabs with the exact key spelling each one uses. Both oddities are in the
 * table itself: VINYL_CATEGORY_MANUFACTURER reads "Sponsor", and the key behind the top
 * plate is VINYL_CATEGORY_TOP_LAYER ("Top Layer") - plain _HOOD and _TOP are absent. */
static const struct { int cat; uint32_t key; } cat_keys[] = {
    { CR_VINYL_CAT_FLAMES,        0xb3ab0d65u },   /* Flames */
    { CR_VINYL_CAT_UNIQUE,        0xd6d115e4u },   /* Unique */
    { CR_VINYL_CAT_TEAR,          0xb6d9a479u },   /* Tear */
    { CR_VINYL_CAT_SPARK,         0x9202238eu },   /* Spark */
    { CR_VINYL_CAT_EXOTIC,        0xb236d659u },   /* Exotic */
    { CR_VINYL_CAT_SMEAR,         0x92008d45u },   /* Smear */
    { CR_VINYL_CAT_ANIMAL,        0xa829d5bfu },   /* Animal */
    { CR_VINYL_CAT_CIRCUIT,       0x3c4cf1c0u },   /* Circuit */
    { CR_VINYL_CAT_STRIPE,        0xd2982464u },   /* Stripes */
    { CR_VINYL_CAT_SPLASH,        0xd24c5678u },   /* Splash */
    { CR_VINYL_CAT_MODERN,        0xc436f812u },   /* Modern */
    { CR_VINYL_CAT_LIGHTNING,     0x588a0a91u },   /* Lightning */
    { CR_VINYL_CAT_FLAG,          0xb6d214e7u },   /* Flag */
    { CR_VINYL_CAT_TRIBAL,        0xd4c40cebu },   /* Tribal */
    { CR_VINYL_CAT_WILD,          0xb6db5bfdu },   /* Wild */
    { CR_VINYL_CAT_MANUFACTURER,  0x34e6adbau },   /* Sponsor */
    { CR_VINYL_CAT_BODY,          0xb6cff09bu },   /* Body */
    { CR_VINYL_CAT_HOODS,         0x913ac36au },   /* Hoods */
    { CR_VINYL_CAT_TOP,           0x6fd3b71cu },   /* Top Layer */
    { CR_VINYL_CAT_AFTERMARKET,   0x64cf99a3u },   /* After Market */
    { CR_VINYL_CAT_CONTEST,       0x4a0c932du },   /* Contest */
    { CR_VINYL_CAT_ARTFACTORY,    0x8e076becu },   /* Art Factory */
    { CR_VINYL_CAT_RACING,        0xcee2f041u }    /* Racing */
};

/* [data] The four layer thumbnails are the numbered art slots of the package, authored
 * stacked on one another (each 10px up and left of the last). Their English names are
 * Bottom Layer / Middle Layer 1 / Middle Layer 2 / Top Layer, so the stack is named from
 * underneath and the selected layer is the one that ends up on top. */
static uint32_t layer_art(int layer) {
    switch (layer) {
    case 0: return 0x4b4e3888u;
    case 1: return 0x4b4e3889u;
    case 2: return 0x4b4e388au;
    default: return 0x4b4e388bu;
    }
}
static uint32_t layer_key(int layer) { return 0x27b8f0ddu + (uint32_t)layer; }   /* VINYL_LAYER_1..4 */

static int clampi(int v, int n) { return v < 0 || n <= 0 ? 0 : v >= n ? n - 1 : v; }

static uint32_t cat_key(int cat) {
    for (unsigned i = 0; i < sizeof cat_keys / sizeof cat_keys[0]; i++)
        if (cat_keys[i].cat == cat) return cat_keys[i].key;
    return 0;
}

static int row_design(const FeVinyl *v, int row) {
    return row >= 0 && row < v->nrows ? v->rows[row] : -1;
}

const FeVinylDesign *fvt_selected(const FeVinyl *v) {
    if (!v || v->page == FVT_PAGE_LAYER) return NULL;
    int i = row_design(v, v->sel);
    return i < 0 ? NULL : &v->shop.design[i];
}

static void design_rows(FeVinyl *v) {
    v->nrows = 0;
    if (!v->ncat) return;
    int want = v->cats[v->cat_sel];
    for (int i = 0; i < v->shop.ndesign && v->nrows < FEV_MAX; i++)
        if (v->shop.design[i].category == want) v->rows[v->nrows++] = i;
    v->first = clampi(v->first, v->nrows);
    v->sel = clampi(v->sel, v->nrows);
}

/* [data] A design ships one colour layer per channel (VINYLS.BIN carries NAMEA/NAMEB/NAMEC
 * beside the design); artFactory house styles ship exactly one. An owned colour of the
 * layer keeps its selection, otherwise the first colour the career has unlocked.
 * The design list also uses this, so that a row selected in the carousel already previews in
 * the colour the car would wear; only the layer row of an empty layer stays uncoloured. */
static void colour_defaults(FeVinyl *v) {
    const FeVinylDesign *d = fvt_selected(v);
    int colors = d ? d->colors : CR_VINYL_COLORS;
    if (colors <= 0) colors = CR_VINYL_COLORS;   /* a snapshot that named none means "A/B/C" */
    if (colors > CR_VINYL_COLORS) colors = CR_VINYL_COLORS;
    /* Walks the catalogue once, filling each channel in turn, so a one-colour shop cannot
     * leave channels B and C pointing at a colour that does not exist. */
    int fill[CR_VINYL_COLORS];
    for (int c = 0; c < CR_VINYL_COLORS; c++) fill[c] = -1;
    for (int i = 0; i < v->shop.ncolor; i++) {
        const FeVinylColor *col = &v->shop.color[i];
        for (int c = 0; c < colors; c++) {
            if (fill[c] >= 0) continue;
            if (v->shop.current_colour[c] && col->paint.key == v->shop.current_colour[c])
                fill[c] = i;                                   /* the layer keeps its colour */
            else if (!v->shop.current_colour[c] && col->open && c == 0)
                fill[c] = i;                                   /* A takes the first open one */
        }
    }
    /* Channels with no stored colour get the next open catalogue entry after the previous
     * channel's, so A/B/C read as distinct choices rather than one colour three times. */
    int next = 0;
    for (int c = 0; c < CR_VINYL_COLORS; c++) {
        v->colour_sel[c] = -1;
        if (c >= colors) continue;
        if (fill[c] >= 0) { next = fill[c] + 1; v->colour_sel[c] = fill[c]; continue; }
        while (next < v->shop.ncolor && !v->shop.color[next].open) next++;
        if (next < v->shop.ncolor) { v->colour_sel[c] = next++; }
    }
}

/* The design the car wears while this screen is open: the pending pick once one is made,
 * else what the layer already holds. */
static const char *pending(const FeVinyl *v) {
    const FeVinylDesign *d = fvt_selected(v);
    return d ? d->token : v->shop.current;
}

static int pending_price(const FeVinyl *v) {
    const FeVinylDesign *d = fvt_selected(v);
    return d && !d->owned ? d->price : 0;   /* recolouring and removing are free */
}

void fvt_open(FeVinyl *v, const FeVinylShop *shop) {
    if (!v) return;
    memset(v, 0, sizeof *v);
    if (shop) v->shop = *shop;
    /* The snapshot owns the state that outlives one visit: which layer is being edited and
     * what it already wears. The shopper's position inside the list is deliberately not kept. */
    if (v->shop.layer < 0 || v->shop.layer >= CR_VINYL_LAYERS) v->shop.layer = 0;
    /* [data] The tabs are exactly the categories the car's own VINYLS.BIN can fill. */
    for (int c = 1; c <= CR_VINYL_CAT_RACING && v->ncat < FEV_CATS; c++) {
        for (int i = 0; i < v->shop.ndesign; i++)
            if (v->shop.design[i].category == c) { v->cats[v->ncat++] = c; break; }
    }
    design_rows(v);
    colour_defaults(v);
}

void fvt_refresh(FeVinyl *v, const FeVinylShop *shop, FvtMsg msg) {
    if (!v) return;
    int page = v->page, cat = v->cat_sel, sel = v->sel, first = v->first, confirm = v->confirm;
    int layer = v->shop.layer;
    FeVinylRequest req = v->req;
    fvt_open(v, shop);
    v->page = page; v->cat_sel = clampi(cat, v->ncat); v->confirm = confirm; v->req = req;
    v->shop.layer = layer;
    design_rows(v);
    v->first = clampi(first, v->nrows);
    v->sel = clampi(sel, v->nrows);
    v->msg = msg;
}

void fvt_preview(const FeVinyl *v, char *token, int cap, uint32_t colour[CR_VINYL_COLORS]) {
    if (cap > 0) token[0] = 0;
    for (int c = 0; c < CR_VINYL_COLORS; c++) colour[c] = 0;
    if (!v) return;
    const char *t = pending(v);
    if (!t || !t[0]) return;
    snprintf(token, (size_t)cap, "%s", t);
    const FeVinylDesign *d = fvt_selected(v);
    for (int c = 0; c < CR_VINYL_COLORS; c++) {
        int i = v->colour_sel[c];
        if (i >= 0 && i < v->shop.ncolor && (!d || c < d->colors))
            colour[c] = v->shop.color[i].paint.key;
        else if (!d) colour[c] = v->shop.current_colour[c];
    }
}

int fvt_preview_layer(const FeVinyl *v) { return v ? v->shop.layer : 0; }

/* Colours are addressed by catalogue index (`colour_sel`), never by row position, so the
 * invoice and the preview can never be one colour apart from what the page shows. */
static void arm(FeVinyl *v) {
    const char *t = pending(v);
    const FeVinylDesign *d = fvt_selected(v);
    snprintf(v->req.token, sizeof v->req.token, "%s", t ? t : "");
    v->req.layer = v->shop.layer;
    v->req.remove = 0;
    for (int c = 0; c < CR_VINYL_COLORS; c++) {
        int i = v->colour_sel[c];
        v->req.colour[c] = (i >= 0 && i < v->shop.ncolor && (!d || c < d->colors))
                               ? v->shop.color[i].paint.key : 0;
    }
}

/* Moves the selection by one row or a whole page and keeps the window around it. Returns
 * nothing; the window only matters to the caller that draws the carousel. */
static void move_sel(FeVinyl *v, int d) {
    v->sel = clampi(v->sel + d, v->nrows);
    if (v->sel < v->first) v->first = v->sel;
    else if (v->sel >= v->first + FEV_ROWS) v->first = v->sel - (FEV_ROWS - 1);
    v->first = clampi(v->first, v->nrows);
}

FvtEvent fvt_input(FeVinyl *v, int in) {
    if (!v) return FVT_EV_NONE;
    /* Every input re-derives the row window and the colour selection, so a category tab
     * change can never leave the previous category's rows behind. */
    design_rows(v);
    if (in == FES_IN_BACK) {
        if (v->confirm) {
            /* Back on the remove question means "change it", so it walks into the list. */
            if (v->page == FVT_PAGE_LAYER && v->shop.current[0]) {
                v->confirm = 0; v->msg = FVT_MSG_NONE;
                v->page = FVT_PAGE_DESIGN; v->sel = v->first = 0;
                /* "Change it" re-opens the list on the tab that holds what the layer wears, so
                 * the shopper lands on the vinyl they are looking at, not on an empty category.
                 * With nothing to match (or no category for it) the first tab is used. */
                v->cat_sel = 0;
                for (int i = 0; i < v->shop.ndesign; i++) {
                    if (strcmp(v->shop.design[i].token, v->shop.current)) continue;
                    for (int c = 0; c < v->ncat; c++)
                        if (v->cats[c] == v->shop.design[i].category) { v->cat_sel = c; break; }
                    break;
                }
                design_rows(v);
                v->cat_sel = v->nrows ? v->cat_sel : 0;
                design_rows(v); colour_defaults(v);
                return FVT_EV_PAGE;
            }
            v->confirm = 0; v->msg = FVT_MSG_NONE; return FVT_EV_PAGE;
        }
        if (v->page == FVT_PAGE_LAYER) return FVT_EV_LEAVE;
        v->page--;
        if (v->page == FVT_PAGE_LAYER) v->req.token[0] = 0;
        /* Leaving a page never leaves an armed invoice behind, so a later Accept can only
         * ever carry what the current page shows. */
        if (v->confirm) { memset(&v->req, 0, sizeof v->req); v->confirm = 0; }
        design_rows(v);
        colour_defaults(v);
        v->msg = FVT_MSG_NONE;
        return FVT_EV_PAGE;
    }
    if (in == FVT_IN_PREV_TAB || in == FVT_IN_NEXT_TAB) {
        /* The category carousel retail keeps on screen at every level of the flow, so it
         * works from the layer row and from an open design list alike. Tabs the car cannot
         * fill are skipped, and a lap with nowhere to go leaves the choice alone. */
        if (!v->ncat) return FVT_EV_NONE;
        int d = in == FVT_IN_PREV_TAB ? -1 : 1;
        int start = v->cat_sel;
        for (int s = 0; s < v->ncat; s++) {
            v->cat_sel = (v->cat_sel + d + v->ncat) % v->ncat;
            design_rows(v);
            if (v->nrows) break;
        }
        if (!v->nrows) v->cat_sel = start;            /* nothing to move to */
        design_rows(v);
        v->sel = v->first = 0;
        colour_defaults(v);
        v->confirm = 0; v->msg = FVT_MSG_NONE;
        return FVT_EV_NONE;
    }
    if (in == FES_IN_LEFT || in == FES_IN_RIGHT || in == 5 || in == 6) {
        int d = in == FES_IN_LEFT ? -1 : in == FES_IN_RIGHT ? 1 : in == 5 ? -FEV_ROWS : FEV_ROWS;
        v->confirm = 0; v->msg = FVT_MSG_NONE;
        if (v->page == FVT_PAGE_LAYER) {
            /* The arrows step the layer itself, which is what the retail layer row is for;
             * the triggers page the category tabs instead, matching retail's "Reorder
             * Layers" prompt sitting on the same row. */
            if (d == -1 || d == 1) {
                v->shop.layer = (v->shop.layer + d + CR_VINYL_LAYERS) % CR_VINYL_LAYERS;
                colour_defaults(v);
            } else {
                if (v->ncat) v->cat_sel = (v->cat_sel + d / FEV_ROWS + v->ncat) % v->ncat;
                v->sel = v->first = 0;
                design_rows(v);
                colour_defaults(v);
            }
        } else if (v->page == FVT_PAGE_DESIGN) {
            move_sel(v, d);
        } else {
            int ch = -1;
            for (int c = 0; c < CR_VINYL_COLORS; c++)
                if (v->colour_sel[c] >= 0) { ch = c; break; }
            if (ch >= 0 && v->shop.ncolor)
                v->colour_sel[ch] =
                    (v->colour_sel[ch] + d % v->shop.ncolor + v->shop.ncolor) % v->shop.ncolor;
        }
        return FVT_EV_NONE;
    }
    if (in != FES_IN_ACCEPT) return FVT_EV_NONE;
    if (v->page == FVT_PAGE_LAYER) {
        if (!v->ncat) { v->msg = FVT_MSG_LOCKED; return FVT_EV_NONE; }
        if (v->shop.current[0]) {
            /* The layer already wears a design, so Accept asks the retail question ("change
             * or remove"); a second Accept takes the vinyl off, which costs nothing. */
            if (!v->confirm) { v->confirm = 1; v->msg = FVT_MSG_CONFIRM; return FVT_EV_PAGE; }
            v->confirm = 0; v->msg = FVT_MSG_NONE;
            /* A removal carries no design and no colours: the layer is simply emptied, and a
             * stale invoice from an earlier browse must never ride along with it. */
            memset(&v->req, 0, sizeof v->req);
            v->req.layer = v->shop.layer; v->req.remove = 1;
            return FVT_EV_REMOVE;
        }
        v->page = FVT_PAGE_DESIGN; v->sel = v->first = 0;
        design_rows(v);
        colour_defaults(v);
        return FVT_EV_PAGE;
    }
    if (v->page == FVT_PAGE_DESIGN) {
        const FeVinylDesign *d = fvt_selected(v);
        if (!d) return FVT_EV_NONE;
        if (!d->open) { v->msg = FVT_MSG_LOCKED; v->confirm = 0; return FVT_EV_NONE; }
        if (!d->owned && d->price > v->shop.bank) { v->msg = FVT_MSG_NO_FUNDS; v->confirm = 0; return FVT_EV_NONE; }
        v->page = FVT_PAGE_COLOR; v->confirm = 0;
        colour_defaults(v);
        return FVT_EV_PAGE;
    }
    /* Colour page: the first Accept puts the design on the invoice, the second pays for it. */
    const FeVinylDesign *d = fvt_selected(v);
    if (!d) return FVT_EV_NONE;
    for (int c = 0; c < CR_VINYL_COLORS; c++) {
        int i = v->colour_sel[c];
        if (i >= 0 && i < v->shop.ncolor && c < d->colors && !v->shop.color[i].open) {
            v->msg = FVT_MSG_LOCKED; v->confirm = 0; return FVT_EV_NONE;
        }
    }
    for (int c = 0; c < CR_VINYL_COLORS; c++) {
        /* A channel the design does not have (or a colour the catalogue cannot name) can
         * never go on the invoice, so drop it here rather than downstream. */
        int i = v->colour_sel[c];
        if (c >= d->colors || i < 0 || i >= v->shop.ncolor) v->colour_sel[c] = -1;
    }
    arm(v);
    if (!v->confirm) { v->confirm = 1; v->msg = FVT_MSG_CONFIRM; return FVT_EV_PAGE; }
    v->confirm = 0; v->msg = FVT_MSG_NONE;
    /* The retail colour page stays put and shows the vinyl it just applied; the shopper backs
     * out of it by hand. `req` keeps the purchase for the host, which then calls fvt_refresh
     * with the snapshot that has the new state in it. */
    return FVT_EV_BUY;
}

/* Settles the visit after a completed transaction: keeps `req` (the host may still be reading
 * it) but stops this screen previewing the design it just paid for, so nothing is left half
 * applied on the car behind the screens. fvt_refresh() re-derives the rows afterwards. */
void fvt_settle(FeVinyl *v) {
    if (!v || v->page == FVT_PAGE_LAYER) return;
    v->page = FVT_PAGE_LAYER;
    v->confirm = 0;
    v->sel = v->first = 0;
    design_rows(v);
}

const char *fvt_package(const FeVinyl *v) { (void)v; return "UI_ChooseVinylLayer.fng"; }

static int node(GvScreen *s, uint32_t hash) {
    for (int i = 0; i < s->scene.n; i++) if (s->scene.nodes[i].obj->name_hash == hash) return i;
    return -1;
}
static void text(GvScreen *s, uint32_t hash, const char *v) {
    int i = node(s, hash);
    if (i >= 0) { s->scene.nodes[i].text_override = v; s->scene.nodes[i].app_hidden = !v; }
}
static void hide(GvScreen *s, uint32_t hash, int v) {
    int i = node(s, hash);
    if (i >= 0) {
        s->scene.nodes[i].app_hidden = v;
        if (!v) { float a = s->scene.nodes[i].obj->color.a; ug_scene_app_write(&s->scene, i, UG_P_COLOR + 3, 1, &a); }
    }
}
static void show_layer(GvScreen *s, int layer) {
    for (int i = 0; i < CR_VINYL_LAYERS; i++) hide(s, layer_art(i), i != layer);
}

/* [inferred] One 118x59 thumbnail plate (0xb04669e3, centred where the four layer slots
 * stack) carries a design's own icon in the catalogue's key order, the same slot-list
 * trick the paint shop uses for PAINT_ICON_n. A missing icon stays hidden rather than
 * inventing art. */
static void show_icon(GvScreen *s, const char *token) {
    int plate = node(s, 0xb04669e3u);
    if (plate < 0) return;
    char name[32];
    snprintf(name, sizeof name, "%s", token);
    int tex = token && token[0] ? ug_assets_texture_by_name(s->assets, name) : -1;
    s->scene.nodes[plate].tex_override = tex + 1;
    s->scene.nodes[plate].app_hidden = tex < 0;
}

int fvt_bind(FeVinyl *v, GvScreen *s, const UgAssets *a) {
    if (!v || !s || !gv_screen_load(s, a, fvt_package(v))) return 0;
    for (unsigned k = 0; k < sizeof fe_binding_sets / sizeof fe_binding_sets[0]; k++)
        if (!strcmp(fe_binding_sets[k].package, fvt_package(v)))
            for (int i = 0; i < fe_binding_sets[k].count; i++)
                if (fe_identity_find(s->pk, &fe_binding_sets[k].identity[i]) < 0) {
                    snprintf(s->status, sizeof s->status, "vinyl shop identity mismatch"); return 0;
                }
    return 1;
}

int fvt_sync(FeVinyl *v, GvScreen *s, float ms) {
    (void)ms;
    if (!v || !s || !s->loaded) return 0;
    const FeVinylDesign *d = fvt_selected(v);
    uint32_t cap = v->page == FVT_PAGE_LAYER ? KEY_CHOOSE_LAYER
                 : v->page == FVT_PAGE_COLOR ? KEY_CHOOSE_COLORS : KEY_APPLY_VINYL;
    if (v->msg == FVT_MSG_CONFIRM) cap = v->page == FVT_PAGE_LAYER ? KEY_CHANGE_OR_REMOVE : KEY_ADD_TO_SETUP;
    else if (v->msg == FVT_MSG_NO_FUNDS) cap = KEY_NO_FUNDS;
    else if (v->msg == FVT_MSG_LOCKED) cap = KEY_LOCKED;
    else if (v->msg == FVT_MSG_OWNED) cap = KEY_ALREADY;
    snprintf(v->caption, sizeof v->caption, "%s",
             v->msg == FVT_MSG_SAVE ? "Could not save vinyl purchase" : gv_text(s->assets, cap, ""));
    snprintf(v->cash, sizeof v->cash, "%s $%lld", gv_text(s->assets, KEY_BANK, "Bank:"), v->shop.bank);
    snprintf(v->total, sizeof v->total, "$%d", pending_price(v));
    /* The option title names the layer, the category, or the design being coloured. */
    uint32_t lk = v->page == FVT_PAGE_LAYER ? layer_key(v->shop.layer)
              : v->page == FVT_PAGE_DESIGN && v->ncat ? cat_key(v->cats[v->cat_sel])
                                                      : cd_hash(d ? d->token : "");
    snprintf(v->label, sizeof v->label, "%s", gv_text(s->assets, lk, ""));
    snprintf(v->mark, sizeof v->mark, "%s",
             d && d->owned ? gv_text(s->assets, KEY_INSTALLED, "Installed") : "");
    text(s, 0x42adb44cu, v->caption);
    text(s, 0xeb406fecu, NULL);
    text(s, 0xeb3a688au, v->cash);
    text(s, 0x1c663aa6u, gv_text(s->assets, KEY_TOTAL, "Total Cost:"));
    text(s, 0x2f5e7da3u, v->total);
    text(s, 0x5e7b09c9u, v->label[0] ? v->label : NULL);
    text(s, 0xd999b6e7u, v->page == FVT_PAGE_LAYER ? gv_text(s->assets, KEY_REORDER, "Reorder Layers") : NULL);
    text(s, 0x00480e37u, NULL);   /* the trigger hint texts are authored, not ours to fill */
    text(s, 0x76bd10f1u, NULL);
    show_layer(s, v->shop.layer);
    show_icon(s, v->page == FVT_PAGE_LAYER ? "" : (d ? d->token : ""));
    int locked = v->page == FVT_PAGE_LAYER ? !v->ncat : d ? !d->open : 0;
    hide(s, 0xa2f11b13u, !locked);
    /* [inferred] The icon plate and its highlight belong to the pages that list something;
     * the layer row already shows its own thumbnail art. */
    hide(s, 0x687fda31u, v->page == FVT_PAGE_LAYER);
    hide(s, 0xdf422345u, v->page == FVT_PAGE_LAYER);
    ug_scene_relayout(&s->scene);
    return 1;
}
