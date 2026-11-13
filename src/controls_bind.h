/* controls_bind.h -- binding model of the Controls screen: action ids, row lists, the keyboard
 * binding table with its retail default keys, the joystick binding records with their default
 * tables, capture/steal rules, and the display-name rules.
 *
 * Implements docs/cleanroom-settings/CONTROLS_SPEC.md (section numbers in comments refer to it) with the
 * project decisions of CONTROLS_DECISIONS.md. Pure: no SDL calls, no drawing, no assets. Keys are SDL
 * scancodes (0 = unbound); the DirectInput codes of the spec are converted by cb_dik_to_scancode() only
 * where the spec's tables are transcribed. Joystick controls use raw SDL joystick indices (D3), and the
 * joystick record is the profile's StInputDevice (settings.h), so persisting it is the profile's job.
 * Design log: docs/cleanroom-settings/CONTROLS_IMPL_DESIGN.md. */
#ifndef OPENUG2_CONTROLS_BIND_H
#define OPENUG2_CONTROLS_BIND_H

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- actions (5.1) ------------------------------------------------------------------------------- */
enum {
    CB_ACT_NONE = 0, CB_ACT_THROTTLE = 1, CB_ACT_BRAKE = 2, CB_ACT_STEER_LEFT = 3, CB_ACT_STEER_RIGHT = 4,
    CB_ACT_HANDBRAKE = 5, CB_ACT_NITROUS = 6, CB_ACT_SHIFT_UP = 7, CB_ACT_SHIFT_DOWN = 8,
    CB_ACT_LOOK_BACK = 9, CB_ACT_CAMERA = 10, CB_ACT_HUD = 11, CB_ACT_PAUSE = 12, CB_ACT_RESET_CAR = 14,
    CB_ACT_ENTER_RACE = 15, CB_ACT_WORLD_MAP = 16, CB_ACT_READ_SMS = 17, CB_ACT_CAREER_STATS = 18,
    CB_ACT_HYD_BOUNCE = 19, CB_ACT_HYD_PRESSURE = 22, CB_ACT_UNKNOWN25 = 25
};

/* ---- device classes and rows (5.2) ----------------------------------------------------------------- */
typedef enum { CB_CLASS_KEYBOARD = 0, CB_CLASS_PAD, CB_CLASS_WHEEL } CbClass;

#define CB_MAX_ROWS 18                  /* keyboard and joypad lists; the wheel list has 16 */
typedef struct CbRow {
    int slot;                           /* 1-based object slot n */
    uint32_t label_key;                 /* language label key of the row label */
    int action;                         /* what the row binds */
} CbRow;
/* Fills `out` (CB_MAX_ROWS entries) and returns the row count: 18 for keyboard and joypad, 16 for a wheel. */
int cb_rows(CbClass cls, CbRow out[CB_MAX_ROWS]);
/* Device-selector title label key per class (3.2). */
uint32_t cb_title_key(CbClass cls);

/* ---- DirectInput key codes <-> SDL scancodes ---------------------------------------------------------- */
int cb_dik_to_scancode(int dik);        /* 0 when there is no SDL equivalent */
int cb_scancode_to_dik(int scancode);   /* 0 when none */

/* ---- keyboard table (6.1, 6.3) --------------------------------------------------------------------------- */
#define CB_KB_ENTRIES 53
typedef struct CbKeyEntry { uint8_t action; int16_t key; } CbKeyEntry;     /* key: SDL scancode, 0 = unbound */
typedef struct CbKeyTable { CbKeyEntry e[CB_KB_ENTRIES]; } CbKeyTable;

/* "Reset Keys To Default": the walk of 6.1 over the live table. */
void cb_kb_reset(CbKeyTable *t);
typedef enum { CB_CAP_CANCELLED = 0, CB_CAP_ASSIGNED } CbCapResult;
/* 6.3: Escape cancels (and can therefore never be bound); otherwise the key is taken from every entry,
 * stored in the first (column 0) or second (column 1) entry of the action, then normalised. */
CbCapResult cb_kb_capture(CbKeyTable *t, int action, int column, int scancode);
/* Key shown in column 0/1 of an action, 0 = none. Column 0 is the first non-empty entry; column 1 is the
 * entry after the first one, and only when the first one holds a key (decision D5: no double display). */
int cb_kb_display(const CbKeyTable *t, int action, int column);
/* The (at most two) keys a game query tests for an action: first non-empty of each of the first two entries. */
int cb_kb_keys(const CbKeyTable *t, int action, int out[2]);

/* ---- joystick record (6.4) ------------------------------------------------------------------------- */
#define CB_AXIS_X 0                     /* axis slots; the record stores byte offsets 4 * slot */
#define CB_AXIS_COUNT 8
#define CB_BIND_BUTTONS 16              /* binding[0..15] buttons, binding[16 + 4p + d] POV p direction d */
#define CB_BIND_POVS 4
enum { CB_DIR_DOWN = 0, CB_DIR_UP = 1, CB_DIR_LEFT = 2, CB_DIR_RIGHT = 3 };
enum { CB_ROLE_STEER = 0, CB_ROLE_THROTTLE = 1, CB_ROLE_BRAKE = 2, CB_ROLE_COMBINED = 3 };

enum { CB_STD_SOUTH, CB_STD_EAST, CB_STD_WEST, CB_STD_NORTH, CB_STD_BACK, CB_STD_START, CB_STD_L3, CB_STD_R3, CB_STD_L1, CB_STD_R1,
       CB_STD_L2, CB_STD_R2, CB_STD_DPAD_UP, CB_STD_DPAD_DOWN, CB_STD_DPAD_LEFT, CB_STD_DPAD_RIGHT, CB_STD_COUNT };
#define CB_VBTN_FIRST 14                /* virtual buttons 14 = left trigger, 15 = right trigger, fed by an analog axis */
#define CB_VBTN_COUNT 2

typedef struct CbPadInfo {              /* what the defaults depend on */
    unsigned vid, pid;
    int buttons;                        /* number of buttons */
    unsigned axes_mask;                 /* bit s = axis slot s exists (X Y Z Rx Ry Rz slider0 slider1) */
    int wheel;
    /* Standard (SDL game controller) layout: all zero = off. std_ctl[e] is the binding index 0..31 of element e, -1 = none. */
    int std;                            /* 1 = the tables below are valid */
    int8_t std_ctl[CB_STD_COUNT];
    int8_t std_steer_axis, std_pedal_axis;   /* axis slot of left-stick X and right-stick Y, -1 = none */
    int8_t vbtn_axis[CB_VBTN_COUNT];    /* raw axis feeding virtual button CB_VBTN_FIRST + k (only when std), -1 = none */
} CbPadInfo;
/* Defaults of a freshly opened device and of "Reset Keys To Default" (guid is kept). */
void cb_pad_defaults(StInputDevice *d, const CbPadInfo *info);

typedef struct CbPadSample {            /* one frame of a device */
    uint32_t buttons;                   /* bit i = button i is down (0..15) */
    int pov[CB_BIND_POVS];              /* centidegrees clockwise from up, -1 = centred */
    int axis[CB_AXIS_COUNT];            /* raw axis values */
} CbPadSample;

typedef struct CbPadCapture {
    int active;
    int have_ref;                       /* the reference sample has been taken */
    CbPadSample ref;                    /* previous frame */
} CbPadCapture;
void cb_pad_capture_begin(CbPadCapture *c);
/* One frame of 6.5 steps 2..4 (Escape is the caller's, step 1). The first frame only takes the reference.
 * Returns 1 when something was assigned (the capture ends), else 0. Decision D5: the POVs are not read
 * after a button was taken in the same frame. */
int cb_pad_capture_frame(StInputDevice *d, CbPadCapture *c, int action, int column,
                         const CbPadSample *now, int wheel);
int cb_pov_direction(int centidegrees);                       /* 6.5 step 3 */
void cb_pad_assign_control(StInputDevice *d, int action, int column, int control);   /* button/POV rule */
void cb_pad_assign_axis(StInputDevice *d, int action, int axis_slot, int sign);      /* axis rule */

typedef enum { CB_CTL_NONE = 0, CB_CTL_AXIS, CB_CTL_BUTTON, CB_CTL_POV } CbCtlKind;
typedef struct CbCtl { CbCtlKind kind; int index; } CbCtl;   /* axis: byte offset; button: index; pov: hat number */
/* 6.2 joystick rule: what column 0/1 of an action shows. */
CbCtl cb_pad_display(const StInputDevice *d, int action, int column);

/* ---- display names (6.2) ----------------------------------------------------------------------------- */
#define CB_NAME_MAX 24                  /* 23 characters + NUL */
/* os_name -> cell text. joystick selects the Button renumbering instead of the modifier replacement.
 * NULL or empty gives "---". Names over 64 bytes are cut to their first character, texts to 23. */
void cb_format_name(const char *os_name, int joystick, char out[CB_NAME_MAX]);

/* ---- everything a game session holds ------------------------------------------------------------------- */
#define CB_MAX_PADS 5                   /* the six profile records minus the keyboard's */
typedef struct CbPad {
    int present;
    uint8_t guid[16];                   /* device GUID, what the profile record is matched by */
    CbPadInfo info;
    StInputDevice dev;
} CbPad;
typedef struct CbBindings {
    CbKeyTable kb;
    CbPad pad[CB_MAX_PADS];
} CbBindings;
void cb_bindings_defaults(CbBindings *b);                     /* keyboard defaults, no pads */
CbClass cb_pad_class(const CbPad *p);

#ifdef __cplusplus
}
#endif
#endif
