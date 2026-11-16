/* controls_menu.h -- state machine of the Controls screen (CONTROLS_SPEC 3, 4): the device selector,
 * the seven-row window over the two binding columns, capture ("press a key / button"), the reset
 * request and leaving.
 *
 * GL-free, SDL-free and draws nothing: the screen is the retail authored package
 * UI_Options_PC_Controller.fng, evaluated by gui_view from the state published here. The host feeds
 * semantic inputs, raw key presses and per-frame joystick samples, and lends the bindings it edits.
 * Reset confirmation (GenericDialog_SMALL.fng) and the screen switch belong to options_menu.c, which
 * embeds this machine as its fifth screen. Design log: CONTROLS_IMPL_DESIGN.md. */
#ifndef OPENUG2_CONTROLS_MENU_H
#define OPENUG2_CONTROLS_MENU_H

#include "controls_bind.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CM_VISIBLE_ROWS 7              /* 3.8 */
#define CM_MAX_DEVICES (1 + CB_MAX_PADS)
#define CM_COLUMNS 2                   /* 0 Primary, 1 Secondary */

/* Language label keys of the fixed texts (3.3, 3.5, 4.6) */
#define CM_KEY_HEADER 0x04DC6DB5u      /* OPT_CONTROLLER ("Controls") */
#define CM_KEY_PRIMARY 0x1B15F53Cu
#define CM_KEY_SECONDARY 0x37E740E0u
#define CM_KEY_INSTR_CONFIGURE 0x8BAB66EFu
#define CM_KEY_INSTR_CAPTURE 0x23CF49AAu
#define CM_KEY_INSTR_ASSIGN 0xFED0D3DFu
#define CM_KEY_RESET_MESSAGE 0x2BBCCA6Bu
#define CM_KEY_OK 0x00000639u
#define CM_KEY_CANCEL 0x541389E5u

typedef enum {
    CM_IN_NONE = 0, CM_IN_UP, CM_IN_DOWN, CM_IN_LEFT, CM_IN_RIGHT, CM_IN_ACCEPT, CM_IN_BACK, CM_IN_RESET
} CmInput;

typedef struct CmDevice {
    CbClass cls;
    int pad;                           /* index into CbBindings.pad, -1 for the keyboard */
} CmDevice;

/* Names are platform data (decision D2); the host supplies them. NULL/empty text shows "---". */
typedef struct CmPlatform {
    void *user;
    const char *(*key_name)(void *user, int scancode);
    const char *(*ctl_name)(void *user, int pad, CbCtl ctl);    /* axes and POVs; buttons are "Button n" */
} CmPlatform;

typedef struct ControlsMenu {
    CbBindings *b;                     /* lent by the host; edited in place */
    CmPlatform plat;
    int open;
    CmDevice dev[CM_MAX_DEVICES];
    int ndev, cur;
    CbRow rows[CB_MAX_ROWS];
    int nrows;
    int focus_row;                     /* 0-based; -1 = the device selector */
    int focus_col;
    int top;                           /* 0-based first visible row */
    int capturing;
    int cap_row, cap_col, cap_pulse;
    CbPadCapture padcap;
    int end_stage;                     /* 1 capture just ended, 2 detected: input comes back next frame */
    int locked;                        /* menu input disabled (capture and the frame after) */
    int instruction;                   /* 0 configure, 1 capture, 2 assign */
    int modified;                      /* a binding changed since the screen was opened */
    int want_reset, want_leave;
    unsigned build_serial;             /* rows rebuilt (open, device change) */
    unsigned blink_serial;             /* a selector arrow blinks */
    int blink_dir;
    unsigned scroll_serial;            /* Up/Down: the scroll arrows blink */
    int scroll_dir;
    unsigned capture_serial;           /* a capture started or ended */
} ControlsMenu;

/* Opens the screen on the keyboard (decision D5: no stale device index). The device list is the keyboard
 * followed by every present pad. */
void cm_open(ControlsMenu *m, CbBindings *b, const CmPlatform *plat);
void cm_close(ControlsMenu *m);
int cm_is_open(const ControlsMenu *m);

void cm_input(ControlsMenu *m, CmInput in);
/* A key went down (not a repeat). Only meaningful while capturing. */
void cm_key_down(ControlsMenu *m, int scancode);
/* One frame of the current joystick while capturing a joystick binding. */
void cm_pad_frame(ControlsMenu *m, const CbPadSample *s);
/* Centred virtual coordinates (origin at the screen centre, 640x480). */
void cm_pointer_click(ControlsMenu *m, float x, float y);
void cm_pointer_move(ControlsMenu *m, float x, float y);
/* Advances the end-of-capture stages (one step per frame). */
void cm_tick(ControlsMenu *m);

/* The reset the dialog's OK performs, on the displayed device (D5). */
void cm_reset_device(ControlsMenu *m);
int cm_take_reset(ControlsMenu *m);    /* the reset dialog was requested */
int cm_take_leave(ControlsMenu *m);    /* Back */
int cm_take_modified(ControlsMenu *m); /* bindings changed since the last take */

/* ---- queries for the view and tests ---- */
const CmDevice *cm_device(const ControlsMenu *m);                /* the displayed device */
uint32_t cm_title_key(const ControlsMenu *m);
int cm_row_count(const ControlsMenu *m);
const CbRow *cm_row(const ControlsMenu *m, int i);
int cm_row_visible(const ControlsMenu *m, int i);
void cm_cell_text(const ControlsMenu *m, int row, int col, char out[CB_NAME_MAX]);
uint32_t cm_instruction_key(const ControlsMenu *m);
int cm_arrows_visible(const ControlsMenu *m);                    /* more than one device */
int cm_scrollbar_visible(const ControlsMenu *m);
/* Tab position over the rail (3.6): 0..1 along it, follows the focused row. */
float cm_scroll_fraction(const ControlsMenu *m);
int cm_input_enabled(const ControlsMenu *m);

/* Geometry (3.8), centred coordinates. */
float cm_row_top(int line);                                       /* y_k of visible line k */
float cm_label_x(void);                                           /* top-left x of the label column */
float cm_cell_x(int col);                                         /* top-left x of a cell column */
float cm_cell_cx(int col);                                        /* centre x of a cell's text */
#define CM_CELL_W 150.0f
#define CM_CELL_H 24.0f
#define CM_ROW_PITCH 36.0f

#ifdef __cplusplus
}
#endif
#endif
