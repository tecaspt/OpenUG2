/* gui_dialog.c -- see gui_dialog.h. Rules and data: SPEC_GUI_ART 2.4 (pause confirmation texts and
 * messages) and 6 (generic two-button dialog), SPEC_GUI 6 (Default Settings). */
#include "gui_dialog.h"

#include <string.h>

/* Package names compare case-blindly, because the loader hashes the upper-cased name
 * (src/ugui/ugui_assets.c:ug_assets_package); spelled out to keep gui_dialog.c free of strcase. */
static int gd_frame_strcasecmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
        char y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (x != y) return (unsigned char)x - (unsigned char)y;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#define PKG_IN_GAME "UI_InGameDialog.fng"
#define PKG_SMALL "GenericDialog_SMALL.fng"
#define KEY_PKG_IN_GAME 0xAA469286u
#define KEY_PKG_SMALL 0x6962C0CDu
/* MSG_*: FE message ids the dialog sends on a choice (not language keys; absent from English.bin by design). */
#define MSG_CANCEL 0xB4623F67u           /* second argument of every pause confirmation, also Back */
#define MSG_DEFAULTS_NO 0x34DC1BCFu

/* Package and message anchor values come from retail layouts [data]; the three-button row centres
 * (76/120/164) and the help caption centre (87/137) are composed node centres measured with
 * `ugui_demo --dump` [data]. Capacities and the common 380-unit width are estimates [inferred],
 * not font measurements. */
static const GdFrame gd_frames[GD_FRAMES] = {
    /* package, key, family, size, lines, box_w, msg_y, btn_y, buttons, help, preferred */
    { "GenericDialog_SMALL.fng",          0x6962C0CDu, GD_FAM_GENERIC, GD_SIZE_SMALL, 4, 380.f,  -3.f,  74.f, 2, 0, 1 },
    { "GenericDialog_MED.fng",            0xC971C7AAu, GD_FAM_GENERIC, GD_SIZE_MED,   5, 380.f, -20.f, 107.f, 2, 0, 1 },
    { "GenericDialog_LARGE.fng",          0x375B415Fu, GD_FAM_GENERIC, GD_SIZE_LARGE, 6, 380.f, -33.f, 165.f, 2, 0, 1 },
    { "GenericDialog.fng",                0xF68A7675u, GD_FAM_GENERIC, GD_SIZE_SMALL, 4, 380.f,  -3.f,  74.f, 2, 0, 0 },
    { "GenericDialog_Animate_SMALL.fng",  0xB8770D6Bu, GD_FAM_GENERIC, GD_SIZE_SMALL, 4, 380.f,  -3.f,  74.f, 2, 0, 0 },
    { "GenericDialog_Animate_MED.fng",    0xB72E54C8u, GD_FAM_GENERIC, GD_SIZE_MED,   5, 380.f, -20.f, 107.f, 2, 0, 0 },
    { "GenericDialog_Animate_LARGE.fng",  0x866F8DFDu, GD_FAM_GENERIC, GD_SIZE_LARGE, 6, 380.f, -33.f, 165.f, 2, 0, 0 },
    { "IG_GenericDialog_SMALL.fng",       0x191A579Cu, GD_FAM_IG,      GD_SIZE_SMALL, 4, 380.f,   1.f,  74.f, 2, 0, 0 },
    { "IG_GenericDialog_MED.fng",         0xA5675EB9u, GD_FAM_IG,      GD_SIZE_MED,   5, 380.f, -10.f, 108.f, 2, 0, 1 },
    { "IG_GenericDialog_LARGE.fng",       0xE712D82Eu, GD_FAM_IG,      GD_SIZE_LARGE, 6, 380.f, -26.f, 164.f, 2, 0, 1 },
    { "UI_InGameDialog.fng",              0xAA469286u, GD_FAM_IG,      GD_SIZE_SMALL, 4, 380.f,   1.f,  74.f, 2, 0, 1 },
    { "HelpDialog_SMALL.fng",             0x6EACFF39u, GD_FAM_HELP,    GD_SIZE_SMALL, 5, 380.f, -60.f,  87.f, 1, 1, 1 },
    { "HelpDialog_MED.fng",               0xEC017B16u, GD_FAM_HELP,    GD_SIZE_MED,   6, 380.f, -107.f, 137.f, 1, 1, 1 },
    { "HelpDialog_LARGE.fng",             0x3CA57FCBu, GD_FAM_HELP,    GD_SIZE_LARGE, 7, 380.f, -162.f, 192.f, 1, 1, 1 },   /* caption y [inferred]: the +50 step of SMALL -> MED [data] continues */
    { "GenericDialog_ThreeButton.fng",    0xD53E4A88u, GD_FAM_GENERIC, GD_SIZE_LARGE, 4, 380.f, -60.f, 164.f, 3, 0, 0 },
};

const GdFrame *gd_frame_at(int i) {
    return i >= 0 && i < GD_FRAMES ? &gd_frames[i] : NULL;
}

const GdFrame *gd_frame_get(GdFamily family, GdSize size) {
    if (family < 0 || family >= GD_FAMS || size < 0 || size >= GD_SIZES) return NULL;
    for (int i = 0; i < GD_FRAMES; i++)
        if (gd_frames[i].family == family && gd_frames[i].size == size && gd_frames[i].preferred)
            return &gd_frames[i];
    return NULL;
}

const GdFrame *gd_frame_by_package(const char *package) {
    if (!package) return NULL;
    for (int i = 0; i < GD_FRAMES; i++)
        if (!gd_frame_strcasecmp(gd_frames[i].package, package)) return &gd_frames[i];
    return NULL;
}

const GdFrame *gd_frame_fit(GdFamily family, int lines) {
    const GdFrame *widest = NULL;
    const GdFrame *pick = gd_frame_get(family, GD_SIZE_SMALL);
    if (!pick) return NULL;                                  /* not a family retail has */
    if (lines < 1) lines = 1;
    for (int s = GD_SIZE_SMALL; s < GD_SIZES; s++) {
        const GdFrame *f = gd_frame_get(family, (GdSize)s);
        if (!f) continue;
        widest = f;                                          /* sizes ascend, so the last is largest */
        if (f->lines >= lines) return f;                     /* the narrowest frame that fits */
    }
    return widest;                                           /* retail has nothing larger */
}

/* Approximate character width [inferred]; the pure state machine has no font metrics. */
#define GD_CAPTION_CHAR_W 7.5f

int gd_message_lines(const char *message, float box_w) {
    if (!message || !message[0]) return 1;
    if (!(box_w > 0)) box_w = 380.0f;
    int per_line = (int)(box_w / GD_CAPTION_CHAR_W);
    if (per_line < 1) per_line = 1;
    int total = 0;
    const char *line = message;
    for (;;) {
        const char *brk = line;
        while (*brk && *brk != '^' && *brk != '\n') brk++;
        int len = (int)(brk - line);
        total += len ? (len + per_line - 1) / per_line : 1;
        if (!*brk) break;
        line = brk + 1;
    }
    return total ? total : 1;
}

const GdFrame *gd_spec_fit(GdSpec *spec, GdFamily family) {
    if (!spec) return NULL;
    const GdFrame *now = spec->frame;
    if (!now) {
        now = gd_frame_by_package(spec->package);
    }
    int want = gd_message_lines(spec->message, now ? now->box_w : 380.0f);
    if (now && now->family == family && now->lines >= want) { spec->frame = now; return now; }   /* the default already fits */
    const GdFrame *pick = gd_frame_fit(family, want);
    if (!pick) return NULL;
    spec->package = pick->package;
    spec->package_key = pick->package_key;
    spec->frame = pick;
    return pick;
}

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

static const Text T_SHOP_BODY = { 0x78DAD861u, "Would you like to enter the Body Shop?" };
static const Text T_SHOP_PAINT = { 0x85D2ED13u, "Would you like to enter the Graphics Shop?" };
static const Text T_SHOP_GENERIC = { 0x15D6B438u, "Would you like to enter the Shop?" };
static int shop_kind(GdKind k) { return k >= GD_ENGAGE_BODY && k <= GD_ENGAGE_SHOP; }

/* Three-button frame: the retail profile-delete question. The retail PC frontend has no
 * three-button host (docs/parity/DIALOGS.md); this kind is what the row stack is built for
 * [inferred]. Every key is verified in LANGUAGES/English.bin; the fallbacks quote the table.
 * Rows: L1 (bottom) Delete, L2 (middle) No, L3 (top) Cancel - the two-button order (confirm
 * first, refuse second) read bottom-first on a vertical stack [inferred]. */
#define PKG_THREE "GenericDialog_ThreeButton.fng"
#define KEY_PKG_THREE 0xD53E4A88u
static const Text T_DELETE_PROFILE = { 0x2C8EC31Du, "Are you sure you want to delete^the profile named %s?" };   /* %s is a literal: text overrides carry no printf formatting */
static const Text L_DELETE = { 0x56B00632u, "Delete" };
static const Text L_NO = { 0x0000061Cu, "No" };
static const Text L_CANCEL = { 0x541389E5u, "Cancel" };
/* Help sheet: title "Help" (node T) and caption "Back" (node B); both keys verified. */
#define T_HELP_TITLE 0x00169268u
#define T_HELP_BACK  0x001335F0u

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
    if (kind == GD_HELP) {                        /* the help sheet family, retail default texts */
        gd_spec_help(out, 0, NULL, 0, NULL, 0, NULL);
        return;
    }
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
        gd_spec_fit(out, GD_FAM_GENERIC);
        return;
    }
    if (kind == GD_ENGAGE_RACE || shop_kind(kind)) {   /* M6: career event / shop zone; Yes / No like Default Settings */
        const Text *t = kind == GD_ENGAGE_BODY ? &T_SHOP_BODY : kind == GD_ENGAGE_PAINT ? &T_SHOP_PAINT :
            kind == GD_ENGAGE_PERF || kind == GD_ENGAGE_SPECIALTY ? &T_SHOP_GENERIC :
            kind == GD_ENGAGE_SHOP ? &T_ENGAGE_SHOP : &T_ENGAGE;
        out->package = PKG_SMALL;
        out->package_key = KEY_PKG_SMALL;
        out->message_key = t->key;
        out->message = t->en;
        out->label_key[0] = 0x0000F770u; out->label[0] = "Yes";
        out->label_key[1] = 0x0000061Cu; out->label[1] = "No";
        gd_spec_fit(out, GD_FAM_GENERIC);
        return;
    }
    if (kind == GD_DELETE_PROFILE) {              /* three vertically stacked rows, no two-button layout */
        out->package = PKG_THREE;
        out->package_key = KEY_PKG_THREE;
        out->frame = gd_frame_by_package(PKG_THREE);
        out->message_key = T_DELETE_PROFILE.key;
        out->message = T_DELETE_PROFILE.en;
        out->label_key[0] = L_DELETE.key; out->label[0] = L_DELETE.en;    /* L1 bottom */
        out->label_key[1] = L_NO.key; out->label[1] = L_NO.en;            /* L2 middle */
        out->label_key[2] = L_CANCEL.key; out->label[2] = L_CANCEL.en;    /* L3 top */
        out->initial_focus = 1;                   /* the middle row */
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
        gd_spec_fit(out, GD_FAM_GENERIC);
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
    gd_spec_fit(out, GD_FAM_IG);
}

void gd_spec_help(GdSpec *out, uint32_t title_key, const char *title,
                  uint32_t body_key, const char *body,
                  uint32_t back_key, const char *back) {
    memset(out, 0, sizeof *out);
    out->kind = GD_HELP;
    out->title_key = title_key; out->title = title;
    out->message_key = body_key; out->message = body;
    out->back_key = back_key ? back_key : T_HELP_BACK;
    out->back_text = back;
    if (!out->title_key && !out->title) { out->title_key = T_HELP_TITLE; out->title = "Help"; }
    /* No explicit size: the retail help frame that fits the body's line estimate. The body is a
     * 0.9-scale caption, so the same character estimate runs at 9 units per character [inferred,
     * docs/parity/DIALOGS.md]. */
    int lines = 1;
    if (body && body[0]) {
        int per_line = (int)(380.0f / 9.0f);                 /* 42 characters at the 0.9 scale */
        if (per_line < 1) per_line = 1;
        int total = 0;
        const char *line = body;
        for (;;) {
            const char *brk = line;
            while (*brk && *brk != '^' && *brk != '\n') brk++;
            int len = (int)(brk - line);
            total += len ? (len + per_line - 1) / per_line : 1;
            if (!*brk) break;
            line = brk + 1;
        }
        lines = total ? total : 1;
    }
    const GdFrame *pick = gd_frame_fit(GD_FAM_HELP, lines);
    if (pick) {
        out->package = pick->package;
        out->package_key = pick->package_key;
        out->frame = pick;
    }
}

void gd_open(GuiDialog *d, const GdSpec *spec) {
    memset(d, 0, sizeof *d);
    d->spec = *spec;
    d->open = 1;
    int buttons = spec->frame ? spec->frame->buttons : 2;
    if (buttons == 3) d->focus = spec->initial_focus >= 0 && spec->initial_focus < 3 ? spec->initial_focus : 1;
    else if (buttons == 1 && spec->frame && spec->frame->help) d->focus = -1;   /* a help sheet has no focus */
    else d->focus = spec->initial_focus ? 1 : 0;
}

static void choose(GuiDialog *d, GdResult r) {
    d->closing = 1;
    d->exit_left_ms = GD_EXIT_MS;
    d->pending = r;
}

static int spec_frame_buttons(const GdSpec *spec) {
    return spec->frame ? spec->frame->buttons : 2;
}
static int spec_frame_help(const GdSpec *spec) {
    return spec->frame && spec->frame->help;
}
static int dialog_buttons(const GuiDialog *d) { return spec_frame_buttons(&d->spec); }
static int dialog_help(const GuiDialog *d) { return spec_frame_help(&d->spec); }
static GdResult result_of_button(int button) {
    switch (button) {
    case 0: return GD_RES_FIRST;
    case 1: return GD_RES_SECOND;
    case 2: return GD_RES_THIRD;
    default: return GD_RES_NONE;
    }
}

void gd_input(GuiDialog *d, GdInput in) {
    if (!d->open || d->closing) return;
    int buttons = dialog_buttons(d);
    if (dialog_help(d)) {                                    /* a help sheet only dismisses */
        if (in == GD_IN_ACCEPT) choose(d, GD_RES_FIRST);
        else if (in == GD_IN_BACK) choose(d, GD_RES_CANCEL);
        return;
    }
    if (buttons == 3) {                                      /* rows are stacked: Up/Down step, no wrap */
        switch (in) {
        case GD_IN_UP: if (d->focus < buttons - 1) d->focus++; break;   /* toward L3 (top) */
        case GD_IN_DOWN: if (d->focus > 0) d->focus--; break;           /* toward L1 (bottom) */
        case GD_IN_ACCEPT: choose(d, result_of_button(d->focus)); break;
        case GD_IN_BACK: choose(d, GD_RES_CANCEL); break;
        default: break;                                              /* Left/Right/Start: nothing */
        }
        return;
    }
    switch (in) {
    case GD_IN_LEFT: d->focus = 0; break;                    /* authored responses: no wrap (6) */
    case GD_IN_RIGHT: d->focus = 1; break;
    case GD_IN_ACCEPT: choose(d, d->focus ? GD_RES_SECOND : GD_RES_FIRST); break;
    case GD_IN_BACK: choose(d, GD_RES_SECOND); break;        /* Back posts the cancel message */
    default: break;                                          /* Up/Down/Start: nothing */
    }
}

void gd_button_extent(GdKind kind, int button, float e[4]) {
    float cx = kind == GD_DEFAULTS || kind == GD_CONTROLS_RESET || kind == GD_ENGAGE_RACE || shop_kind(kind) ? 150.0f : 151.0f;        /* button centres, 6 */
    cx = button ? cx : -cx;
    e[0] = cx - 149.0f;                                      /* 128 + 21 */
    e[2] = cx + 149.0f;
    e[1] = 53.0f;
    e[3] = 95.0f;
}

void gd_spec_button_extent(const GdSpec *spec, int button, float e[4]) {
    int buttons = spec_frame_buttons(spec);
    float btn_y = spec->frame ? spec->frame->btn_y : 74.f;
    if (spec_frame_help(spec)) {                             /* the B caption row is the only target */
        if (button != 0) { e[0] = e[1] = e[2] = e[3] = 0; return; }
        e[0] = 172.0f;                                       /* the caption's authored extent [data] */
        e[2] = 268.0f;
        e[1] = btn_y - 10.0f;
        e[3] = btn_y + 10.0f;
        return;
    }
    if (buttons == 3) {                                      /* authored 512 x 42 row strips [data] */
        float cy = button == 0 ? btn_y : button == 1 ? 120.0f : 76.0f;   /* L1 bottom, L3 top */
        e[0] = -256.0f;
        e[2] = 256.0f;
        e[1] = cy - 21.0f;
        e[3] = cy + 21.0f;
        return;
    }
    gd_button_extent(spec->kind, button, e);                 /* the two-button pills, unchanged */
    e[1] += btn_y - 74.f;
    e[3] += btn_y - 74.f;
}

int gd_pointer_click(GuiDialog *d, float x, float y) {
    if (!d->open || d->closing) return 0;
    if (dialog_help(d)) {                                    /* the caption row dismisses the sheet */
        float e[4];
        gd_spec_button_extent(&d->spec, 0, e);
        if (x >= e[0] && x <= e[2] && y >= e[1] && y <= e[3]) { choose(d, GD_RES_FIRST); return 1; }
        return 0;
    }
    int buttons = dialog_buttons(d);
    for (int b = 0; b < buttons; b++) {
        float e[4];
        gd_spec_button_extent(&d->spec, b, e);
        if (x >= e[0] && x <= e[2] && y >= e[1] && y <= e[3]) {
            d->focus = b;
            choose(d, result_of_button(b));
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
