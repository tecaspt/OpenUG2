/* gui_dialog.c -- see gui_dialog.h. Rules and data: SPEC_GUI_ART 2.4 (pause confirmation texts and
 * messages) and 6 (generic two-button dialog), SPEC_GUI 6 (Default Settings). */
#include "gui_dialog.h"

#include <string.h>

#define PKG_IN_GAME "UI_InGameDialog.fng"
#define PKG_SMALL "GenericDialog_SMALL.fng"
#define KEY_PKG_IN_GAME 0xAA469286u
#define KEY_PKG_SMALL 0x6962C0CDu
/* MSG_*: FE message ids the dialog sends on a choice (not language keys; absent from English.bin by design). */
#define MSG_CANCEL 0xB4623F67u           /* second argument of every pause confirmation, also Back */
#define MSG_DEFAULTS_NO 0x34DC1BCFu

typedef struct Text { uint32_t key; const char *en; } Text;
static const Text T_QUIT = { 0x6F4796A6u, "Quit to Main Menu?" };             /* career */
static const Text T_QUIT_OTHER = { 0x30ED2368u, "Quit to Main Menu?" };
static const Text T_QUIT_LOST = { 0xE15B558Au, "Quit to Main Menu?^All unsaved race progress will be lost." };
static const Text T_ONLINE = { 0xCE50DA14u, "Are you sure you want to quit to the Online Main Menu?" };
static const Text T_ONLINE_FLAG = { 0xBC25C6B1u, "Are you sure you want to quit to the Online Main Menu?  You will receive a loss and a disconnect for this game." };
static const Text T_SESSION = { 0x555BD5B6u, "Leaving the race now will end this session.  Are you sure you want to leave?" };
static const Text T_SESSION_FLAG = { 0x79A85513u, "Leaving the race now will end this session and you will be given a loss and a disconnect.  Are you sure you want to leave?" };
static const Text T_LAN = { 0xD63B6982u, "Are you sure you want to quit to the LAN main menu?" };
static const Text T_RESTART = { 0x0972A44Bu, "Restart Race?" };
static const Text T_RESTART_LOST = { 0xAA50AA02u, "Restart race?^All unsaved race progress will be lost." };
static const Text T_EXPLORE = { 0x60B42AFBu, "Return to Explore Mode?" };
static const Text T_EXPLORE_LOST = { 0x07CD17B0u, "Return to Explore Mode?^All unsaved race progress will be lost." };
static const Text T_MAP = { 0x33C42E03u, "Back to the World Map?" };
static const Text T_MAP_LOST = { 0xE761C8B8u, "Back to the World Map?^All unsaved race progress will be lost." };
static const Text T_GARAGE = { 0xC9BA9E3Eu, "Jump to Garage?" };
static const Text T_DEFAULTS = { 0x2BBCCA6Bu, "Reset to default settings?" };
static const Text T_ENGAGE = { 0xEC9488BBu, "Would you like to race this event?" };
static const Text T_ENGAGE_SHOP = { 0x0AAB553Eu, "Do you want to enter the car lot?" };   /* ASK_ENTER_CAR_LOT */

static const Text *quit_text(const GdContext *c) {              /* 2.4 table, "Quit" rows */
    if (c->mode == 8u) {
        if (c->session_test) return c->flag ? &T_SESSION_FLAG : &T_SESSION;
        return c->flag ? &T_ONLINE_FLAG : &T_ONLINE;
    }
    if (c->mode == 0x200u) return c->session_test ? &T_SESSION : &T_LAN;
    if (c->substate4) return &T_QUIT_LOST;
    return c->mode == 1u ? &T_QUIT : &T_QUIT_OTHER;
}

void gd_spec(GdSpec *out, GdKind kind, const GdContext *ctx) {
    static const GdContext none = { 0, 0, 0, 0 };
    const Text *t = &T_QUIT;
    uint32_t ok = 0;
    if (!ctx) ctx = &none;
    memset(out, 0, sizeof *out);
    out->kind = kind;
    out->initial_focus = 1;                       /* the second button for every two-button dialog (6) */
    if (kind == GD_DEFAULTS) {
        out->package = PKG_SMALL;
        out->package_key = KEY_PKG_SMALL;
        out->message_key = T_DEFAULTS.key;
        out->message = T_DEFAULTS.en;
        out->label_key[0] = 0x0000F770u; out->label[0] = "Yes";
        out->label_key[1] = 0x0000061Cu; out->label[1] = "No";
        out->result_msg[0] = 0xD05FC3A3u;         /* FE message for the first button */
        out->result_msg[1] = MSG_DEFAULTS_NO;
        out->back_msg = MSG_DEFAULTS_NO;          /* Back = No */
        return;
    }
    if (kind == GD_ENGAGE_RACE || kind == GD_ENGAGE_SHOP) {   /* M6: career event / shop zone; Yes / No like Default Settings */
        const Text *t = kind == GD_ENGAGE_SHOP ? &T_ENGAGE_SHOP : &T_ENGAGE;
        out->package = PKG_SMALL;
        out->package_key = KEY_PKG_SMALL;
        out->message_key = t->key;
        out->message = t->en;
        out->label_key[0] = 0x0000F770u; out->label[0] = "Yes";
        out->label_key[1] = 0x0000061Cu; out->label[1] = "No";
        return;
    }
    if (kind == GD_CONTROLS_RESET) {              /* CONTROLS_SPEC 4.6: texts come from the language file only */
        out->package = PKG_SMALL;
        out->package_key = KEY_PKG_SMALL;
        out->message_key = T_DEFAULTS.key;        /* the same message key as Default Settings */
        out->label_key[0] = 0x00000639u;          /* OK */
        out->label_key[1] = 0x541389E5u;          /* Cancel */
        out->result_msg[1] = MSG_DEFAULTS_NO;
        out->back_msg = MSG_DEFAULTS_NO;
        return;
    }
    switch (kind) {
    case GD_QUIT: t = quit_text(ctx); ok = 0x30ED2368u; break;
    case GD_RESTART: t = ctx->substate4 ? &T_RESTART_LOST : &T_RESTART; ok = 0xE1A57D51u; break;
    case GD_EXPLORE: t = ctx->substate4 ? &T_EXPLORE_LOST : &T_EXPLORE; ok = 0x451E768Eu; break;
    case GD_WORLD_MAP: t = ctx->substate4 ? &T_MAP_LOST : &T_MAP; ok = 0x33C42E03u; break;
    case GD_GARAGE: t = &T_GARAGE; ok = 0x43DA9FD0u; break;
    default: break;
    }
    out->package = PKG_IN_GAME;
    out->package_key = KEY_PKG_IN_GAME;
    out->message_key = t->key;
    out->message = t->en;
    out->label_key[0] = 0x00000639u; out->label[0] = "OK";
    out->label_key[1] = 0x541389E5u; out->label[1] = "Cancel";
    out->result_msg[0] = ok;
    out->result_msg[1] = MSG_CANCEL;
    out->back_msg = MSG_CANCEL;
}

void gd_open(GuiDialog *d, const GdSpec *spec) {
    memset(d, 0, sizeof *d);
    d->spec = *spec;
    d->open = 1;
    d->focus = spec->initial_focus ? 1 : 0;
}

static void choose(GuiDialog *d, GdResult r) {
    d->closing = 1;
    d->exit_left_ms = GD_EXIT_MS;
    d->pending = r;
}

void gd_input(GuiDialog *d, GdInput in) {
    if (!d->open || d->closing) return;
    switch (in) {
    case GD_IN_LEFT: d->focus = 0; break;                    /* authored responses: no wrap (6) */
    case GD_IN_RIGHT: d->focus = 1; break;
    case GD_IN_ACCEPT: choose(d, d->focus ? GD_RES_SECOND : GD_RES_FIRST); break;
    case GD_IN_BACK: choose(d, GD_RES_SECOND); break;        /* Back posts the cancel message */
    default: break;                                          /* Up/Down/Start: nothing */
    }
}

void gd_button_extent(GdKind kind, int button, float e[4]) {
    float cx = kind == GD_DEFAULTS || kind == GD_CONTROLS_RESET || kind == GD_ENGAGE_RACE || kind == GD_ENGAGE_SHOP ? 150.0f : 151.0f;        /* button centres, 6 */
    cx = button ? cx : -cx;
    e[0] = cx - 149.0f;                                      /* 128 + 21 */
    e[2] = cx + 149.0f;
    e[1] = 53.0f;
    e[3] = 95.0f;
}

int gd_pointer_click(GuiDialog *d, float x, float y) {
    if (!d->open || d->closing) return 0;
    for (int b = 0; b < 2; b++) {
        float e[4];
        gd_button_extent(d->spec.kind, b, e);
        if (x >= e[0] && x <= e[2] && y >= e[1] && y <= e[3]) {
            d->focus = b;
            choose(d, b ? GD_RES_SECOND : GD_RES_FIRST);
            return 1;
        }
    }
    return 0;
}

void gd_update(GuiDialog *d, float seconds) {
    if (!d->open || !d->closing) return;
    d->exit_left_ms -= seconds > 0 ? seconds * 1000.0f : 0.0f;
    if (d->exit_left_ms <= 0) {
        d->open = 0;
        d->closing = 0;
        d->delivered = d->pending;
        d->pending = GD_RES_NONE;
        d->deliveries++;
    }
}

GdResult gd_poll(GuiDialog *d) {
    GdResult r = d->delivered;
    d->delivered = GD_RES_NONE;
    return r;
}
