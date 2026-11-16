/* fe_lot.c -- see fe_lot.h. Package binding follows fe_car.c (UI_QRCarSelect). */
#include "fe_lot.h"
#include "fe_binding.h"
#include "fe_screen.h"
#include "gui_view.h"
#include "ugui/ugui_core.h"
#include "ugui/ugui_canvas.h"
#include "ugui/ugui_face.h"

#include <stdio.h>
#include <string.h>

#define FEL_PREVIEW_DELAY 0.3f   /* same debounce as fec_sync */
/* [inferred] Header width: from the header's left edge to the rail's right end (ScrollBar, 0xddefc, 564 wide). */
#define FEL_HEADER_W 578.0f

void fel_open(FeLot *l, const FeLotCar *cars, int n, int must_buy, long long bank) {
    memset(l, 0, sizeof *l);
    if (n < 0 || !cars) n = 0;
    if (n > FEL_MAX) n = FEL_MAX;
    for (int i = 0; i < n; i++) { l->car[i] = cars[i]; l->car[i].name[sizeof l->car[i].name - 1] = 0; }
    l->n = n;
    l->must_buy = must_buy != 0;
    l->bank = bank;
    l->header_scale = 1;
    for (int i = 0; i < n; i++) if (!l->car[i].owned) { l->sel = i; break; }
}

const char *fel_selected(const FeLot *l) { return l && l->n ? l->car[l->sel].name : ""; }

FelEvent fel_input(FeLot *l, int in) {
    if (!l) return FEL_EV_NONE;
    switch (in) {
    case FES_IN_LEFT: case FES_IN_RIGHT: {
        int next = l->sel + (in == FES_IN_LEFT ? -1 : 1);
        l->confirm = 0; l->msg = FEL_MSG_NONE;
        if (next >= 0 && next < l->n) { l->sel = next; l->preview_wait = 0; l->preview[0] = 0; }
        return FEL_EV_NONE;
    }
    case FES_IN_ACCEPT:
        if (!l->n) return FEL_EV_NONE;
        if (l->car[l->sel].owned) { l->confirm = 0; l->msg = FEL_MSG_SAME_CAR; return FEL_EV_NONE; }
        if (!l->confirm) { l->confirm = 1; l->msg = FEL_MSG_CONFIRM; return FEL_EV_NONE; }
        l->confirm = 0; l->msg = FEL_MSG_NONE;
        return FEL_EV_BUY;
    case FES_IN_BACK:
        if (l->confirm) { l->confirm = 0; l->msg = FEL_MSG_NONE; return FEL_EV_NONE; }
        if (l->must_buy) { l->msg = FEL_MSG_MUST_BUY; return FEL_EV_NONE; }
        l->msg = FEL_MSG_NONE;
        return FEL_EV_LEAVE;
    default: return FEL_EV_NONE;
    }
}

void fel_advance(FeLot *l, float seconds) {
    if (!l || !l->n) return;
    if (seconds > 0) l->preview_wait += seconds;
    if (l->preview_wait >= FEL_PREVIEW_DELAY) snprintf(l->preview, sizeof l->preview, "%s", l->car[l->sel].name);
}

/* ---- package binding ---- */

/* Retail text with its first "%s" replaced by `arg`; no other conversion is interpreted. */
static void fill_arg(char *out, size_t cap, const char *fmt, const char *arg) {
    const char *at = strstr(fmt, "%s");
    if (!at) { snprintf(out, cap, "%s", fmt); return; }
    snprintf(out, cap, "%.*s%s%s", (int)(at - fmt), fmt, arg, at + 2);
}

static int lot_set(void) {
    for (unsigned k = 0; k < sizeof fe_binding_sets / sizeof fe_binding_sets[0]; k++)
        if (!strcmp(fe_binding_sets[k].package, "UI_CareerCarLot.fng")) return (int)k;
    return -1;
}

int fel_bind(FeLot *l, GvScreen *s, const UgAssets *a) {
    if (l) l->header_scale = 1;           /* a fresh scene carries the authored header size */
    if (!gv_screen_load(s, a, "UI_CareerCarLot.fng")) return 0;
    int set = lot_set();
    if (set < 0) { snprintf(s->status, sizeof s->status, "car lot identity set missing"); return 0; }
    for (int i = 0; i < fe_binding_sets[set].count; i++)
        if (fe_identity_find(s->pk, &fe_binding_sets[set].identity[i]) < 0) {
            snprintf(s->status, sizeof s->status, "car lot identity mismatch (GUID %05x)", (unsigned)fe_binding_sets[set].identity[i].guid);
            return 0;
        }
    return 1;
}

void fel_sync(FeLot *l, GvScreen *s) {
    if (!l || !s || !s->loaded) return;
    const UgAssets *a = s->assets;
    const char *name = fel_selected(l);
    char key[80];
    snprintf(key, sizeof key, "CAR_NAME_%s", name);
    /* [data] CAR_NAME_<model> is the model's display name (e.g. CAR_NAME_MIATA "MX-5"); the logo carries the make. */
    const char *display = l->n ? gv_text(a, ug_hash_upper(key), name) : "";
    switch (l->msg) {
    case FEL_MSG_CONFIRM: fill_arg(l->caption, sizeof l->caption, gv_text(a, 0xda8f3e96u, "Do you want this stock %s?"), display); break;  /* CAR_SELECT_CONFIRM_CHOOSE_CAR */
    case FEL_MSG_SAME_CAR: fill_arg(l->caption, sizeof l->caption, gv_text(a, 0x7ee84e4eu, "You're already driving your %s!"), display); break;  /* CAR_SELECT_SAME_CAR */
    case FEL_MSG_MUST_BUY: snprintf(l->caption, sizeof l->caption, "%s", gv_text(a, 0x272ebebau, "You have to select a new car before you leave.")); break;  /* CAR_LOT_MUST_BUY */
    default: snprintf(l->caption, sizeof l->caption, "%s", gv_text(a, 0x7edc9715u, "Car Lot")); break;  /* CAREER_CAR_LOT_TITLE */
    }
    /* [inferred] "Bank:" (HUD_BANK 0x3298ccdb) followed by the amount in the authored "$999999999" form. */
    snprintf(l->cash, sizeof l->cash, "%s $%lld", gv_text(a, 0x3298ccdbu, "Bank:"), l->bank);
    l->status[0] = 0;
    for (int i = 0; i < s->scene.n; i++) {
        UgNode *n = &s->scene.nodes[i];
        uint32_t g = n->obj->guid;
        if (g == 0xdcc1b) {                                   /* car name */
            n->text_override = display; n->app_hidden = !l->n;
            float alpha = 255; ug_scene_app_write(&s->scene, i, UG_P_COLOR + 3, 1, &alpha);
        }
        if (g == 0xddf04) {                                   /* header: title or the current prompt */
            n->text_override = l->caption;
            /* Platform adaptation: a long prompt (CAR_LOT_MUST_BUY) is scaled to the header width rather than clipped. */
            float tw = 0, th = 0, k = 1;
            const UgFace *was = ug_text_face();
            if (n->face) ug_text_set_face(n->face);
            if (ug_text_face()) ug_text_measure(l->caption, 1, 0, 0, UG_GLYPHS_MOUSE, &tw, &th);
            ug_text_set_face(was);
            if (tw > FEL_HEADER_W) k = FEL_HEADER_W / tw;
            if (k != l->header_scale) {
                float sz[3] = { n->obj->size[0] * k, n->obj->size[1] * k, n->obj->size[2] };
                ug_scene_app_write(&s->scene, i, UG_P_SIZE, 3, sz);
                l->header_scale = k;
            }
        }
        if (g == 0xe1c11) n->text_override = l->cash;         /* PLAYER_CASH */
        if (g == 0xe1c10) n->app_hidden = 1;                  /* PLAYER NAME shares the cash position */
        /* Unimplemented stats: hide the group rather than show authored example values (as fec_sync). */
        if (g == 0xe1d21 || g == 0xdcac4) n->app_hidden = 1;
        if (g == 0xdcac7) {                                   /* make logo */
            char lk[80]; snprintf(lk, sizeof lk, "SECONDARY_LOGO_%s", name);
            int tex = l->n ? gv_texture(a, ug_hash_upper(lk)) : 0;
            n->tex_override = tex; n->app_hidden = !tex;
            if (!tex && l->n) snprintf(l->status, sizeof l->status, "car model logo unavailable: %.31s", name);
            if (tex) { float white[4] = { 255, 255, 255, 255 }; ug_scene_app_write(&s->scene, i, UG_P_COLOR, 4, white); }
        }
    }
    ug_scene_relayout(&s->scene);
}
