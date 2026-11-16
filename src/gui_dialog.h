/* gui_dialog.h -- generic two-button dialogs (SPEC_GUI_ART 2.4 and 6, SPEC_GUI 6, tvg10, tvg06).
 *
 * Pure state machine, no GL/SDL: describes the dialogs the pause menu opens (UI_InGameDialog.fng:
 * OK / Cancel) and the Default Settings confirmation (GenericDialog_SMALL.fng: Yes / No), owns the
 * button focus and delivers the result after the dialog's exit script (140 ms). The picture is the
 * view's business (gui_view.h). */
#ifndef OPENUG2_GUI_DIALOG_H
#define OPENUG2_GUI_DIALOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GD_EXIT_MS 140.0f

typedef enum {
    GD_QUIT, GD_RESTART, GD_EXPLORE, GD_WORLD_MAP, GD_GARAGE,    /* UI_InGameDialog.fng */
    GD_DEFAULTS,                                                  /* GenericDialog_SMALL.fng */
    GD_CONTROLS_RESET,                                            /* same package: OK / Cancel (CONTROLS_SPEC 4.6) */
    GD_ENGAGE_RACE,                                               /* M6: same package, Yes / No: "Would you like to race this event?" */
    GD_ENGAGE_SHOP                                                /* M6: same package, Yes / No: ASK_ENTER_CAR_LOT */
} GdKind;

/* Selectors that choose the message text (tvg10 "context"). */
typedef struct GdContext {
    uint32_t mode;                /* game mode: 1 career, 8 online, 0x200 LAN, others single player */
    int substate4;                /* racing with sub-state 4 -> "all unsaved race progress" texts */
    int session_test;             /* online-session test holds */
    int flag;                     /* extra online flag */
} GdContext;

typedef struct GdSpec {
    GdKind kind;
    const char *package;          /* "UI_InGameDialog.fng" / "GenericDialog_SMALL.fng" */
    uint32_t package_key;
    uint32_t message_key;
    const char *message;          /* English text of that key (fallback when the table lacks it) */
    uint32_t label_key[2];        /* first / second button label */
    const char *label[2];
    uint32_t result_msg[2];       /* message posted for the first / second button */
    uint32_t back_msg;            /* message posted by Back (the cancel message) */
    int initial_focus;            /* 1 = the second button (6) */
} GdSpec;

void gd_spec(GdSpec *out, GdKind kind, const GdContext *ctx);

typedef enum { GD_IN_NONE = 0, GD_IN_UP, GD_IN_DOWN, GD_IN_LEFT, GD_IN_RIGHT, GD_IN_ACCEPT, GD_IN_BACK, GD_IN_START } GdInput;
typedef enum { GD_RES_NONE = 0, GD_RES_FIRST, GD_RES_SECOND } GdResult;    /* SECOND = Cancel / No / Back */

typedef struct GuiDialog {
    GdSpec spec;
    int open;                     /* logically open: owns all input */
    int focus;                    /* 0 first button, 1 second button */
    int closing;                  /* exit script running */
    float exit_left_ms;
    GdResult pending;             /* chosen, delivered when the exit completes */
    GdResult delivered;
    int deliveries;
} GuiDialog;

/* Opens a fresh dialog: focus on the second button, nothing pending. */
void gd_open(GuiDialog *d, const GdSpec *spec);
/* Left/Right move between the two buttons without wrapping; Accept chooses the focused button; Back
 * chooses Cancel; Up/Down/Start do nothing. Input is ignored while the exit runs. */
void gd_input(GuiDialog *d, GdInput in);
/* Primary click at centred virtual coordinates: a click on a button's extent focuses and chooses it.
 * Returns 1 when a button was hit. */
int gd_pointer_click(GuiDialog *d, float x, float y);
void gd_update(GuiDialog *d, float seconds);
/* Exactly-once result, after the 140 ms exit; the dialog is closed when it is returned. */
GdResult gd_poll(GuiDialog *d);
/* Button extents (centred coordinates, edges inclusive): x0,y0,x1,y1. */
void gd_button_extent(GdKind kind, int button, float e[4]);

#ifdef __cplusplus
}
#endif
#endif
