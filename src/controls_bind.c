/* controls_bind.c -- see controls_bind.h. Rules: CONTROLS_SPEC 5 (rows), 6.1/6.3 (keyboard), 6.2 (names),
 * 6.4/6.5 (joystick). */
#include "controls_bind.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <SDL_scancode.h>

/* ---- rows (5.2) ---------------------------------------------------------------------------------- */

typedef struct RowDef { uint32_t key; int action; int steering; } RowDef;
static const RowDef k_rows[CB_MAX_ROWS] = {
    { 0x153E7337u, CB_ACT_STEER_LEFT, 1 },   { 0xBD7BA1AAu, CB_ACT_STEER_RIGHT, 1 },
    { 0x409607E2u, CB_ACT_THROTTLE, 0 },     { 0x82EBCC98u, CB_ACT_BRAKE, 0 },
    { 0x3EB318CEu, CB_ACT_SHIFT_UP, 0 },     { 0xB7D33921u, CB_ACT_SHIFT_DOWN, 0 },
    { 0xB3D1EA35u, CB_ACT_HANDBRAKE, 0 },    { 0xDF5AEF26u, CB_ACT_NITROUS, 0 },
    { 0x419CA63Au, CB_ACT_CAMERA, 0 },       { 0x35B9F571u, CB_ACT_LOOK_BACK, 0 },
    { 0x4A1341B3u, CB_ACT_ENTER_RACE, 0 },   { 0x1E7E3391u, CB_ACT_WORLD_MAP, 0 },
    { 0x58BAEBBAu, CB_ACT_READ_SMS, 0 },     { 0x735B2E6Cu, CB_ACT_CAREER_STATS, 0 },
    { 0x6FC04E6Fu, CB_ACT_RESET_CAR, 0 },    { 0xA081F0CCu, CB_ACT_HYD_PRESSURE, 0 },
    { 0x64730AACu, CB_ACT_HYD_BOUNCE, 0 },   { 0x6F99F62Au, CB_ACT_PAUSE, 0 },
};

int cb_rows(CbClass cls, CbRow out[CB_MAX_ROWS]) {
    int n = 0;
    for (int i = 0; i < CB_MAX_ROWS; i++) {
        if (cls == CB_CLASS_WHEEL && k_rows[i].steering) continue;   /* the wheel list has no steering rows */
        out[n].slot = n + 1;
        out[n].label_key = k_rows[i].key;
        out[n].action = k_rows[i].action;
        n++;
    }
    return n;
}

uint32_t cb_title_key(CbClass cls) {
    return cls == CB_CLASS_PAD ? 0xF91FBA4Du : cls == CB_CLASS_WHEEL ? 0x14D4BD3Bu : 0x5CF08EB7u;
}

/* ---- DirectInput <-> SDL (public key code tables) ---------------------------------------------------------- */

static const struct { uint8_t dik; int sc; } k_dik[] = {
    { 0x01, SDL_SCANCODE_ESCAPE }, { 0x02, SDL_SCANCODE_1 }, { 0x03, SDL_SCANCODE_2 }, { 0x04, SDL_SCANCODE_3 },
    { 0x05, SDL_SCANCODE_4 }, { 0x06, SDL_SCANCODE_5 }, { 0x07, SDL_SCANCODE_6 }, { 0x08, SDL_SCANCODE_7 },
    { 0x09, SDL_SCANCODE_8 }, { 0x0A, SDL_SCANCODE_9 }, { 0x0B, SDL_SCANCODE_0 }, { 0x0C, SDL_SCANCODE_MINUS },
    { 0x0D, SDL_SCANCODE_EQUALS }, { 0x0E, SDL_SCANCODE_BACKSPACE }, { 0x0F, SDL_SCANCODE_TAB },
    { 0x10, SDL_SCANCODE_Q }, { 0x11, SDL_SCANCODE_W }, { 0x12, SDL_SCANCODE_E }, { 0x13, SDL_SCANCODE_R },
    { 0x14, SDL_SCANCODE_T }, { 0x15, SDL_SCANCODE_Y }, { 0x16, SDL_SCANCODE_U }, { 0x17, SDL_SCANCODE_I },
    { 0x18, SDL_SCANCODE_O }, { 0x19, SDL_SCANCODE_P }, { 0x1A, SDL_SCANCODE_LEFTBRACKET },
    { 0x1B, SDL_SCANCODE_RIGHTBRACKET }, { 0x1C, SDL_SCANCODE_RETURN }, { 0x1D, SDL_SCANCODE_LCTRL },
    { 0x1E, SDL_SCANCODE_A }, { 0x1F, SDL_SCANCODE_S }, { 0x20, SDL_SCANCODE_D }, { 0x21, SDL_SCANCODE_F },
    { 0x22, SDL_SCANCODE_G }, { 0x23, SDL_SCANCODE_H }, { 0x24, SDL_SCANCODE_J }, { 0x25, SDL_SCANCODE_K },
    { 0x26, SDL_SCANCODE_L }, { 0x27, SDL_SCANCODE_SEMICOLON }, { 0x28, SDL_SCANCODE_APOSTROPHE },
    { 0x29, SDL_SCANCODE_GRAVE }, { 0x2A, SDL_SCANCODE_LSHIFT }, { 0x2B, SDL_SCANCODE_BACKSLASH },
    { 0x2C, SDL_SCANCODE_Z }, { 0x2D, SDL_SCANCODE_X }, { 0x2E, SDL_SCANCODE_C }, { 0x2F, SDL_SCANCODE_V },
    { 0x30, SDL_SCANCODE_B }, { 0x31, SDL_SCANCODE_N }, { 0x32, SDL_SCANCODE_M }, { 0x33, SDL_SCANCODE_COMMA },
    { 0x34, SDL_SCANCODE_PERIOD }, { 0x35, SDL_SCANCODE_SLASH }, { 0x36, SDL_SCANCODE_RSHIFT },
    { 0x37, SDL_SCANCODE_KP_MULTIPLY }, { 0x38, SDL_SCANCODE_LALT }, { 0x39, SDL_SCANCODE_SPACE },
    { 0x3A, SDL_SCANCODE_CAPSLOCK }, { 0x3B, SDL_SCANCODE_F1 }, { 0x3C, SDL_SCANCODE_F2 },
    { 0x3D, SDL_SCANCODE_F3 }, { 0x3E, SDL_SCANCODE_F4 }, { 0x3F, SDL_SCANCODE_F5 }, { 0x40, SDL_SCANCODE_F6 },
    { 0x41, SDL_SCANCODE_F7 }, { 0x42, SDL_SCANCODE_F8 }, { 0x43, SDL_SCANCODE_F9 }, { 0x44, SDL_SCANCODE_F10 },
    { 0x45, SDL_SCANCODE_NUMLOCKCLEAR }, { 0x46, SDL_SCANCODE_SCROLLLOCK }, { 0x47, SDL_SCANCODE_KP_7 },
    { 0x48, SDL_SCANCODE_KP_8 }, { 0x49, SDL_SCANCODE_KP_9 }, { 0x4A, SDL_SCANCODE_KP_MINUS },
    { 0x4B, SDL_SCANCODE_KP_4 }, { 0x4C, SDL_SCANCODE_KP_5 }, { 0x4D, SDL_SCANCODE_KP_6 },
    { 0x4E, SDL_SCANCODE_KP_PLUS }, { 0x4F, SDL_SCANCODE_KP_1 }, { 0x50, SDL_SCANCODE_KP_2 },
    { 0x51, SDL_SCANCODE_KP_3 }, { 0x52, SDL_SCANCODE_KP_0 }, { 0x53, SDL_SCANCODE_KP_PERIOD },
    { 0x56, SDL_SCANCODE_NONUSBACKSLASH }, { 0x57, SDL_SCANCODE_F11 }, { 0x58, SDL_SCANCODE_F12 },
    { 0x64, SDL_SCANCODE_F13 }, { 0x65, SDL_SCANCODE_F14 }, { 0x66, SDL_SCANCODE_F15 },
    { 0x9C, SDL_SCANCODE_KP_ENTER }, { 0x9D, SDL_SCANCODE_RCTRL }, { 0xB5, SDL_SCANCODE_KP_DIVIDE },
    { 0xB7, SDL_SCANCODE_PRINTSCREEN }, { 0xB8, SDL_SCANCODE_RALT }, { 0xC5, SDL_SCANCODE_PAUSE },
    { 0xC7, SDL_SCANCODE_HOME }, { 0xC8, SDL_SCANCODE_UP }, { 0xC9, SDL_SCANCODE_PAGEUP },
    { 0xCB, SDL_SCANCODE_LEFT }, { 0xCD, SDL_SCANCODE_RIGHT }, { 0xCF, SDL_SCANCODE_END },
    { 0xD0, SDL_SCANCODE_DOWN }, { 0xD1, SDL_SCANCODE_PAGEDOWN }, { 0xD2, SDL_SCANCODE_INSERT },
    { 0xD3, SDL_SCANCODE_DELETE }, { 0xDB, SDL_SCANCODE_LGUI }, { 0xDC, SDL_SCANCODE_RGUI },
    { 0xDD, SDL_SCANCODE_APPLICATION },
};
#define N_DIK ((int)(sizeof k_dik / sizeof k_dik[0]))

int cb_dik_to_scancode(int dik) {
    for (int i = 0; i < N_DIK; i++) if (k_dik[i].dik == dik) return k_dik[i].sc;
    return 0;
}
int cb_scancode_to_dik(int sc) {
    for (int i = 0; i < N_DIK; i++) if (k_dik[i].sc == sc) return k_dik[i].dik;
    return 0;
}

/* ---- keyboard (6.1) ------------------------------------------------------------------------------------ */

/* The default table in its fixed order (action, DirectInput key); the order is the table's layout. */
static const struct { uint8_t action, dik; } k_kb_default[CB_KB_ENTRIES] = {
    { 1, 0xC8 }, { 1, 0x11 }, { 2, 0xD0 }, { 2, 0x1F }, { 3, 0xCB }, { 3, 0x1E }, { 4, 0xCD }, { 4, 0x20 },
    { 7, 0x2A }, { 7, 0x27 }, { 8, 0x1D }, { 8, 0x34 }, { 5, 0x39 }, { 5, 0xCF }, { 6, 0x38 }, { 6, 0xB8 },
    { 11, 0x10 }, { 11, 0x0D }, { 10, 0x2E }, { 10, 0xD2 }, { 9, 0x30 }, { 9, 0x25 }, { 12, 0x01 }, { 12, 0x19 },
    { 14, 0x13 }, { 14, 0xC7 }, { 15, 0x1C }, { 15, 0x3B }, { 16, 0x32 }, { 16, 0x3C }, { 17, 0x0F }, { 17, 0x3D },
    { 18, 0x14 }, { 18, 0x3E }, { 19, 0x24 }, { 19, 0x16 }, { 22, 0x23 }, { 22, 0x15 }, { 25, 0x06 },
    { 49, 0x48 }, { 50, 0x50 }, { 51, 0x4B }, { 52, 0x4D }, { 53, 0x47 }, { 61, 0x49 }, { 62, 0x26 },
    { 54, 0x4F }, { 55, 0x51 }, { 56, 0x1A }, { 57, 0x1B }, { 58, 0xEA }, { 59, 0xE5 }, { 60, 0x22 },
};

void cb_kb_reset(CbKeyTable *t) {
    for (int j = 0; j < CB_KB_ENTRIES; j++) {
        int k = cb_dik_to_scancode(k_kb_default[j].dik);
        t->e[j].action = k_kb_default[j].action;
        if (k) for (int i = 0; i < j; i++) if (t->e[i].key == k) t->e[i].key = 0;   /* cleared up to the target */
        t->e[j].key = (int16_t)k;
    }
}

static int first_entry(const CbKeyTable *t, int action) {
    for (int i = 0; i < CB_KB_ENTRIES; i++) if (t->e[i].action == action) return i;
    return -1;
}
static int next_entry(const CbKeyTable *t, int action, int after) {
    for (int i = after + 1; i < CB_KB_ENTRIES; i++) if (t->e[i].action == action) return i;
    return -1;
}

CbCapResult cb_kb_capture(CbKeyTable *t, int action, int column, int scancode) {
    if (scancode == SDL_SCANCODE_ESCAPE || scancode <= 0 || scancode >= SDL_NUM_SCANCODES) return CB_CAP_CANCELLED;
    int i0 = first_entry(t, action), i1 = i0 >= 0 ? next_entry(t, action, i0) : -1;
    if (i0 < 0 || i1 < 0) return CB_CAP_CANCELLED;
    for (int i = 0; i < CB_KB_ENTRIES; i++) if (t->e[i].key == scancode) t->e[i].key = 0;   /* taken from whoever had it */
    t->e[column ? i1 : i0].key = (int16_t)scancode;
    if (!t->e[i0].key) t->e[i0].key = t->e[i1].key;                    /* normalise */
    if (t->e[i0].key == t->e[i1].key) t->e[i1].key = 0;
    return CB_CAP_ASSIGNED;
}

int cb_kb_display(const CbKeyTable *t, int action, int column) {
    int i0 = first_entry(t, action);
    if (i0 < 0) return 0;
    if (column == 0) {                                               /* first non-empty entry */
        for (int i = i0; i >= 0; i = next_entry(t, action, i)) if (t->e[i].key) return t->e[i].key;
        return 0;
    }
    if (!t->e[i0].key) return 0;                                     /* D5: an empty primary shows no second copy */
    for (int i = next_entry(t, action, i0); i >= 0; i = next_entry(t, action, i)) if (t->e[i].key) return t->e[i].key;
    return 0;
}

int cb_kb_keys(const CbKeyTable *t, int action, int out[2]) {
    int n = 0, i0 = first_entry(t, action);
    out[0] = out[1] = 0;
    if (i0 < 0) return 0;
    int i1 = next_entry(t, action, i0);
    if (t->e[i0].key) out[n++] = t->e[i0].key;
    if (i1 >= 0 && t->e[i1].key) out[n++] = t->e[i1].key;
    return n;
}

/* ---- joystick defaults (6.4) -------------------------------------------------------------------------- */

typedef struct KnownPad { unsigned vid, pid; int8_t game[20]; } KnownPad;
static const KnownPad k_known[] = {
    { 0x046D, 0xCA03, { 5, 6, 9, 10, 15, 17, 16, 12, 8, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { 0x0B43, 0x0003, { 10, 9, 1, 2, 8, 7, 6, 5, 19, 22, 0, 0, 0, 0, 0, 0, 0, 4, 0, 3 } },
    { 0x046D, 0xC216, { 2, 1, 9, 10, 6, 5, 8, 7, 14, 12, 22, 19, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x046D, 0xC21A, { 2, 1, 9, 10, 6, 5, 8, 7, 14, 12, 22, 19, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x046D, 0xC218, { 2, 1, 9, 10, 6, 5, 8, 7, 14, 12, 22, 19, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x046D, 0xC20A, { 1, 2, 8, 9, 10, 7, 6, 5, 12, 0, 0, 0, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x05FD, 0x3002, { 2, 10, 1, 9, 6, 5, 8, 7, 14, 12, 22, 19, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x06A3, 0xFF0C, { 2, 10, 1, 9, 7, 8, 6, 5, 19, 22, 0, 0, 0, 0, 0, 0, 18, 15, 16, 17 } },
    { 0x045E, 0x0027, { 1, 9, 2, 10, 6, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4 } },
};
#define N_KNOWN ((int)(sizeof k_known / sizeof k_known[0]))

static int popcount(unsigned v) { int n = 0; for (; v; v >>= 1) n += v & 1; return n; }
static int has_axis(const CbPadInfo *i, int slot) { return (i->axes_mask >> slot) & 1; }
enum { AX_X = 0, AX_Y = 1, AX_Z = 2, AX_RZ = 5, AX_S0 = 6 };

static int known_pad(const CbPadInfo *info) {
    for (int k = 0; k < N_KNOWN; k++) if (k_known[k].vid == info->vid && k_known[k].pid == info->pid) return 1;
    return 0;
}

/* Action of each standard element (CB_STD_*); -1 = unbound (Start). */
static const int std_action[CB_STD_COUNT] = {
    CB_ACT_THROTTLE, CB_ACT_LOOK_BACK, CB_ACT_BRAKE, CB_ACT_CAMERA, CB_ACT_RESET_CAR, -1, CB_ACT_HYD_PRESSURE, CB_ACT_HYD_BOUNCE,
    CB_ACT_NITROUS, CB_ACT_HANDBRAKE, CB_ACT_SHIFT_DOWN, CB_ACT_SHIFT_UP,
    CB_ACT_ENTER_RACE, CB_ACT_WORLD_MAP, CB_ACT_STEER_LEFT, CB_ACT_STEER_RIGHT,
};

static void pad_default_bindings(StInputDevice *d, const CbPadInfo *info) {
    for (int k = 0; k < N_KNOWN; k++) {
        if (k_known[k].vid != info->vid || k_known[k].pid != info->pid) continue;
        for (int j = 0; j < 20; j++) d->binding[j] = k_known[k].game[j];
        return;
    }
    int *b = d->binding, B = info->buttons;
    if (B < 6) {
        static const int btn[5] = { 6, 5, 8, 7, 9 };
        for (int j = 0; j < 5; j++) b[j] = btn[j];
        for (int p = 0; p < CB_BIND_POVS; p++) {
            b[CB_BIND_BUTTONS + 4 * p + CB_DIR_DOWN] = CB_ACT_BRAKE;
            b[CB_BIND_BUTTONS + 4 * p + CB_DIR_UP] = CB_ACT_THROTTLE;
            b[CB_BIND_BUTTONS + 4 * p + CB_DIR_LEFT] = CB_ACT_STEER_LEFT;
            b[CB_BIND_BUTTONS + 4 * p + CB_DIR_RIGHT] = CB_ACT_STEER_RIGHT;
        }
        return;
    }
    b[0] = CB_ACT_BRAKE;
    b[1] = CB_ACT_THROTTLE;
    if (B < 8) {
        static const int btn[5] = { 6, 5, 8, 7, 9 };
        for (int j = 0; j < 5; j++) b[2 + j] = btn[j];
    } else {
        static const int btn[8] = { 9, 10, 6, 5, 8, 7, 22, 19 };
        for (int j = 0; j < 8; j++) b[2 + j] = btn[j];
    }
    for (int p = 0; p < CB_BIND_POVS; p++) {
        b[CB_BIND_BUTTONS + 4 * p + CB_DIR_LEFT] = CB_ACT_STEER_LEFT;
        b[CB_BIND_BUTTONS + 4 * p + CB_DIR_RIGHT] = CB_ACT_STEER_RIGHT;
    }
}

void cb_pad_defaults(StInputDevice *d, const CbPadInfo *info) {
    uint8_t guid[16];
    memcpy(guid, d->guid, 16);
    memset(d, 0, sizeof *d);
    memcpy(d->guid, guid, 16);
    /* device-open defaults */
    d->axis[CB_ROLE_STEER] = 4 * AX_X;
    d->dir[CB_ROLE_STEER] = 1;
    d->axis[CB_ROLE_THROTTLE] = 4 * AX_X;
    d->axis[CB_ROLE_BRAKE] = 4 * AX_X;
    d->dir[CB_ROLE_THROTTLE] = -1;
    d->dir[CB_ROLE_BRAKE] = -1;
    d->axis[CB_ROLE_COMBINED] = 4 * AX_Y;
    d->dir[CB_ROLE_COMBINED] = 1;
    d->combined_mode = 1;
    if (info->std && !info->wheel && !known_pad(info)) {            /* PlayStation 2 release layout instead of the positional one; Start stays free: the host pauses on the controller Start event */
        for (int e = 0; e < CB_STD_COUNT; e++) {
            int ctl = info->std_ctl[e];
            if (ctl < 0 || ctl >= 32 || std_action[e] < 0) continue;
            d->binding[ctl] = std_action[e];
        }
        if (!info->axes_mask) return;
        if (info->std_steer_axis >= 0 && info->std_steer_axis < CB_AXIS_COUNT) {
            d->axis[CB_ROLE_STEER] = 4 * info->std_steer_axis;
            d->dir[CB_ROLE_STEER] = 1;
            d->assigned[CB_ROLE_STEER] = 1;
        }
        if (info->std_pedal_axis >= 0 && info->std_pedal_axis < CB_AXIS_COUNT) {
            d->axis[CB_ROLE_COMBINED] = 4 * info->std_pedal_axis;
            d->dir[CB_ROLE_COMBINED] = 1;
            d->combined_mode = 1;
            d->assigned[CB_ROLE_COMBINED] = 1;
        }
        return;
    }
    pad_default_bindings(d, info);
    if (!info->axes_mask) return;
    d->axis[CB_ROLE_STEER] = 4 * AX_X;
    d->dir[CB_ROLE_STEER] = 1;
    d->assigned[CB_ROLE_STEER] = 1;
    if (!info->wheel) {
        int slot = popcount(info->axes_mask) < 4 ? AX_Y : AX_RZ;
        if (info->vid == 0x0B43 && info->pid == 0x0003) slot = AX_Z;
        d->axis[CB_ROLE_COMBINED] = 4 * slot;
        d->assigned[CB_ROLE_COMBINED] = 1;
    } else if (has_axis(info, AX_S0) && has_axis(info, AX_Y) && !has_axis(info, AX_Z)) {
        d->axis[CB_ROLE_COMBINED] = 4 * AX_S0;
        d->assigned[CB_ROLE_COMBINED] = 1;
    } else if (has_axis(info, AX_Y) && !has_axis(info, AX_Z) && !has_axis(info, AX_RZ) && !has_axis(info, AX_S0)) {
        d->axis[CB_ROLE_COMBINED] = 4 * AX_Y;
        d->assigned[CB_ROLE_COMBINED] = 1;
    }                                                            /* else: separate pedals, none assigned */
}

/* ---- joystick display (6.2) ---------------------------------------------------------------------------- */

static CbCtl control_of(int index) {
    CbCtl c;
    if (index < CB_BIND_BUTTONS) { c.kind = CB_CTL_BUTTON; c.index = index; }
    else { c.kind = CB_CTL_POV; c.index = (index - CB_BIND_BUTTONS) / 4; }
    return c;
}

/* The axis occupying column 0 of an action, or -1. */
static int axis_in_column0(const StInputDevice *d, int action) {
    if ((action == CB_ACT_STEER_LEFT || action == CB_ACT_STEER_RIGHT) && d->assigned[CB_ROLE_STEER])
        return d->axis[CB_ROLE_STEER];
    if (action == CB_ACT_THROTTLE || action == CB_ACT_BRAKE) {
        int role = action == CB_ACT_THROTTLE ? CB_ROLE_THROTTLE : CB_ROLE_BRAKE;
        if (!d->combined_mode && d->assigned[role]) return d->axis[role];
        if (d->combined_mode && d->assigned[CB_ROLE_COMBINED]) return d->axis[CB_ROLE_COMBINED];
    }
    return -1;
}

CbCtl cb_pad_display(const StInputDevice *d, int action, int column) {
    CbCtl none = { CB_CTL_NONE, 0 };
    int axis = axis_in_column0(d, action);
    int want = column;                                          /* which bound control (0 first, 1 second) */
    if (axis >= 0) {
        if (column == 0) { CbCtl c = { CB_CTL_AXIS, axis }; return c; }
        want = 0;                                               /* column 1 shows the first button/POV */
    }
    int seen = 0;
    for (int j = 0; j < 32; j++) {
        if (d->binding[j] != action) continue;
        if (seen++ == want) return control_of(j);
    }
    return none;
}

/* ---- joystick capture (6.5) ---------------------------------------------------------------------------- */

int cb_pov_direction(int cd) {
    cd %= 36000;
    if (cd < 0) cd += 36000;
    if (cd < 4500 || cd > 31500) return CB_DIR_UP;                 /* |sin| < |cos|, cos > 0 */
    if (cd > 13500 && cd < 22500) return CB_DIR_DOWN;              /* |sin| < |cos|, cos < 0 */
    return cd <= 18000 ? CB_DIR_RIGHT : CB_DIR_LEFT;               /* exact diagonals count as left/right */
}

void cb_pad_assign_control(StInputDevice *d, int action, int column, int control) {
    if (control < 0 || control >= 32) return;
    int keep = axis_in_column0(d, action) < 0;
    int steer_class = action >= CB_ACT_THROTTLE && action <= CB_ACT_STEER_RIGHT;
    for (int j = 0; j < 32; j++) {
        if (d->binding[j] != action) continue;
        if (column == 1 && keep && !steer_class) keep = 0;         /* the first existing one is kept */
        else d->binding[j] = 0;
    }
    d->binding[control] = action;
}

void cb_pad_assign_axis(StInputDevice *d, int action, int slot, int s) {
    int off = 4 * slot;
    for (int r = 0; r < 4; r++) if (d->axis[r] == off) d->assigned[r] = 0;
    if (action == CB_ACT_STEER_LEFT || action == CB_ACT_STEER_RIGHT) {
        d->axis[CB_ROLE_STEER] = off;
        d->dir[CB_ROLE_STEER] = action == CB_ACT_STEER_LEFT ? -s : s;
        d->assigned[CB_ROLE_STEER] = 1;
    } else if (action == CB_ACT_THROTTLE) {
        d->axis[CB_ROLE_THROTTLE] = off;
        d->dir[CB_ROLE_THROTTLE] = s;
        if (d->axis[CB_ROLE_BRAKE] == off && d->dir[CB_ROLE_BRAKE] != s) {
            d->axis[CB_ROLE_COMBINED] = off;
            d->dir[CB_ROLE_COMBINED] = -s;
            d->assigned[CB_ROLE_COMBINED] = 1;
            d->combined_mode = 1;
        } else {
            d->assigned[CB_ROLE_THROTTLE] = 1;
            d->combined_mode = 0;
        }
    } else if (action == CB_ACT_BRAKE) {
        d->axis[CB_ROLE_BRAKE] = off;
        d->dir[CB_ROLE_BRAKE] = s;
        if (d->axis[CB_ROLE_THROTTLE] == off && d->dir[CB_ROLE_THROTTLE] != s) {
            d->axis[CB_ROLE_COMBINED] = off;
            d->dir[CB_ROLE_COMBINED] = s;
            d->assigned[CB_ROLE_COMBINED] = 1;
            d->combined_mode = 1;
        } else {
            d->assigned[CB_ROLE_BRAKE] = 1;
            d->combined_mode = 0;
        }
    }
}

void cb_pad_capture_begin(CbPadCapture *c) { memset(c, 0, sizeof *c); c->active = 1; }

int cb_pad_capture_frame(StInputDevice *d, CbPadCapture *c, int action, int column,
                         const CbPadSample *now, int wheel) {
    if (!c->active) return 0;
    if (!c->have_ref) { c->ref = *now; c->have_ref = 1; return 0; }  /* snapshot of the axes and held buttons */
    const CbPadSample *ref = &c->ref;
    int done = 0;
    for (int i = 0; i < CB_BIND_BUTTONS && !done; i++)
        if (((now->buttons >> i) & 1) && !((ref->buttons >> i) & 1)) {
            cb_pad_assign_control(d, action, column, i);
            done = 1;
        }
    for (int p = 0; p < CB_BIND_POVS && !done; p++)                  /* D5: not after a button of the same frame */
        if (now->pov[p] >= 0) {
            cb_pad_assign_control(d, action, column, CB_BIND_BUTTONS + 4 * p + cb_pov_direction(now->pov[p]));
            done = 1;
        }
    if (!done && action >= CB_ACT_THROTTLE && action <= CB_ACT_STEER_RIGHT) {
        static const int order[CB_AXIS_COUNT] = { 0, 3, 1, 4, 2, 5, 6, 7 };   /* X Rx Y Ry Z Rz slider0 slider1 */
        int limit = wheel ? 500 : 1000;
        for (int k = 0; k < CB_AXIS_COUNT && !done; k++) {
            int slot = order[k], delta = now->axis[slot] - ref->axis[slot];
            if (delta > limit || delta < -limit) {
                cb_pad_assign_axis(d, action, slot, delta > 0 ? 1 : -1);
                done = 1;
            }
        }
    }
    c->ref = *now;
    if (done) c->active = 0;
    return done;
}

/* ---- names (6.2) -------------------------------------------------------------------------------------------- */

void cb_format_name(const char *os, int joystick, char out[CB_NAME_MAX]) {
    char tmp[80];
    if (!os || !*os) { snprintf(out, CB_NAME_MAX, "---"); return; }
    if (strlen(os) > 64) { tmp[0] = os[0]; tmp[1] = 0; }               /* a name over 64 bytes keeps its first character */
    else snprintf(tmp, sizeof tmp, "%s", os);
    if (!joystick) {
        if (!strcasecmp(tmp, "Shift")) snprintf(tmp, sizeof tmp, "Left Shift");
        else if (!strcasecmp(tmp, "Alt")) snprintf(tmp, sizeof tmp, "Left Alt");
        else if (!strcasecmp(tmp, "Ctrl")) snprintf(tmp, sizeof tmp, "Left Ctrl");
    } else if (!strncasecmp(tmp, "Button", 6)) {
        const char *q = tmp + 6;
        while (*q == ' ') q++;
        int digits = 0, n = 0;
        for (; isdigit((unsigned char)q[digits]); digits++) n = n * 10 + (q[digits] - '0');
        if (digits && !q[digits] && digits < 6) snprintf(tmp, sizeof tmp, "Button %d", n + 1);
    }
    snprintf(out, CB_NAME_MAX, "%.23s", tmp);
}

/* ---- session ---------------------------------------------------------------------------------------------- */

void cb_bindings_defaults(CbBindings *b) {
    memset(b, 0, sizeof *b);
    cb_kb_reset(&b->kb);
}

CbClass cb_pad_class(const CbPad *p) { return p->info.wheel ? CB_CLASS_WHEEL : CB_CLASS_PAD; }
