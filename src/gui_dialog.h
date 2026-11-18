/* gui_dialog.h -- generic two- and three-button dialogs and the help sheet (SPEC_GUI_ART 2.4 and 6,
 * SPEC_GUI 6, tvg10, tvg06; three-button rows and the help caption measured from retail [data]).
 *
 * Pure state machine, no GL/SDL: describes the dialogs the pause menu opens (UI_InGameDialog.fng:
 * OK / Cancel), the Default Settings confirmation (GenericDialog_SMALL.fng: Yes / No), the retail
 * three-button frame (GenericDialog_ThreeButton.fng: three vertically stacked rows) and the help
 * sheet (HelpDialog_SMALL/MED/LARGE: title, body, back caption), owns the button focus and delivers
 * the result after the dialog's exit script (140 ms). The picture is the view's business
 * (gui_view.h). */
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
    GD_ENGAGE_BODY, GD_ENGAGE_PAINT, GD_ENGAGE_PERF, GD_ENGAGE_SPECIALTY,
    GD_ENGAGE_SHOP,                                               /* M6: same package, Yes / No: ASK_ENTER_CAR_LOT.
                                                                   * Every kind above keeps the frame it always used;
                                                                   * gd_frame_fit() picks a retail frame by text. */
    GD_DELETE_PROFILE,                                            /* GenericDialog_ThreeButton.fng: three rows.
                                                                   * The retail PC frontend has no three-button
                                                                   * host; this is the profile-delete question the
                                                                   * package's Yes/No/Cancel row stack is built for
                                                                   * [inferred]; the host call is documented in
                                                                   * docs/parity/DIALOGS.md */
    GD_HELP                                                       /* HelpDialog_SMALL/MED/LARGE: title, body and
                                                                   * back caption; opened with a caller-chosen title,
                                                                   * body and language keys (gd_spec_help) */
} GdKind;

/* Dialog size, as retail names it (GenericDialog_SMALL / _MED / _LARGE, HelpDialog_SMALL, ...). */
typedef enum { GD_SIZE_SMALL = 0, GD_SIZE_MED, GD_SIZE_LARGE, GD_SIZES } GdSize;

/* The frame families retail ships; a family is one dialog drawn in the sizes that share its art. */
typedef enum { GD_FAM_GENERIC = 0,   /* GenericDialog.fng, GenericDialog_SMALL/MED/LARGE and _Animate_* */
               GD_FAM_IG,            /* IG_GenericDialog_SMALL/MED/LARGE and UI_InGameDialog */
               GD_FAM_HELP,          /* HelpDialog_SMALL/MED/LARGE */
               GD_FAMS } GdFamily;

/* One retail dialog frame: everything needed to load and place it. The geometry is measured from
 * the retail nodes, the line capacity and wrap width are estimates (docs/parity/DIALOGS.md). */
typedef struct GdFrame {
    const char *package;          /* "GenericDialog_MED.fng" */
    uint32_t package_key;         /* hash of that name: the loader's key */
    GdFamily family;
    GdSize size;
    int lines;                    /* estimated line capacity */
    float box_w;                  /* estimated wrap width of the message box, centred coordinates */
    float msg_y;                  /* message anchor */
    float btn_y;                  /* button row centre; 0 when the frame has no two-button row */
    int buttons;                  /* buttons the frame holds: 2, or 3 for GenericDialog_ThreeButton, 0 for Help */
    int help;                     /* HelpDialog_*: a title and an OK pill instead of Yes / No */
    int preferred;                /* the frame of this family and size that the automatic choice uses */
} GdFrame;

#define GD_FRAMES 15              /* every Generic / Help dialog package retail ships */

/* The frame table, including the frames the automatic choice never picks. */
const GdFrame *gd_frame_at(int i);
/* The frame of one family and size that the automatic choice uses, or NULL when retail has no such
 * frame. UI_InGameDialog is the in-game family's SMALL frame (the confirmation retail opens in game,
 * SPEC_GUI_ART 2.4); IG_GenericDialog_SMALL is the pooled copy of the same art. */
const GdFrame *gd_frame_get(GdFamily family, GdSize size);
/* The frame retail knows by this package name (case-insensitive, the loader hashes the upper-cased
 * name), including the _Animate_ and ThreeButton frames; NULL when the table has no such package. */
const GdFrame *gd_frame_by_package(const char *package);
/* The narrowest frame of `family` whose box holds `lines`; the family's largest when none would,
 * which is all retail offers. NULL only when `family` is not a family retail has. */
const GdFrame *gd_frame_fit(GdFamily family, int lines);
/* Lines `message` needs at the 0.75 caption scale inside a box `box_w` wide: the '^' and newline
 * breaks the retail texts already use, plus estimated wraps based on character count. A face-free
 * estimate (gui_dialog.c carries no font): average capital width x the caption scale. */
int gd_message_lines(const char *message, float box_w);

/* Selectors that choose the message text (tvg10 "context"). */
typedef struct GdContext {
    uint32_t mode;                /* game mode: 1 career, 8 online, 0x200 LAN, others single player */
    int substate4;                /* racing with sub-state 4 -> "all unsaved race progress" texts */
    int session_test;             /* online-session test holds */
    int flag;                     /* extra online flag */
} GdContext;

#define GD_MAX_BUTTONS 3          /* L1 / L2 / L3 of GenericDialog_ThreeButton; two-button kinds use [0] and [1] */

typedef struct GdSpec {
    GdKind kind;
    const char *package;          /* "UI_InGameDialog.fng" / "GenericDialog_SMALL.fng" / "GenericDialog_ThreeButton.fng" / "HelpDialog_MED.fng" */
    uint32_t package_key;
    const GdFrame *frame;         /* the retail frame `package` names; NULL when no table frame matches */
    uint32_t title_key;           /* help sheet title (T); 0 for the other kinds */
    const char *title;            /* English text of that key (fallback when the table lacks it) */
    uint32_t message_key;
    const char *message;          /* English text of that key (fallback when the table lacks it) */
    uint32_t label_key[GD_MAX_BUTTONS];   /* button labels; [2] = the third row (L3) */
    const char *label[GD_MAX_BUTTONS];
    uint32_t back_key;            /* help caption (B) text key; 0 for the other kinds */
    const char *back_text;        /* English text of that key (fallback when the table lacks it) */
    uint32_t result_msg[GD_MAX_BUTTONS];  /* message posted for each button */
    uint32_t back_msg;            /* message posted by Back (the cancel message) */
    int initial_focus;            /* focus on open: the second button of a two-button dialog (6);
                                   * the middle row of a three-button dialog; ignored by the help sheet */
} GdSpec;

void gd_spec(GdSpec *out, GdKind kind, const GdContext *ctx);
/* Opens a help sheet (HelpDialog_SMALL/MED/LARGE chosen by the body's line estimate): `title_key`
 * and `title` are the T texts, `body_key` and `body` the M texts, `back_key` and `back` the B
 * caption; a key of 0 or text of "" leaves the authored retail sample. The sheet has no buttons:
 * Accept or a tap on the caption closes it (GD_RES_FIRST), Back closes it (GD_RES_CANCEL). */
void gd_spec_help(GdSpec *out, uint32_t title_key, const char *title,
                  uint32_t body_key, const char *body,
                  uint32_t back_key, const char *back);
/* Re-picks the frame of a spec whose default is already right for its text, and promotes it to the
 * retail frame that fits when the text does not fit the default. A kind that has always opened one
 * frame keeps it unless its message no longer fits that frame's box (the rule the brief fixes); so
 * the short retail confirmations stay on SMALL / UI_InGameDialog exactly as before. `family` is the
 * family to choose within: the frontend defaults use GenericDialog, the in-game ones IG_GenericDialog.
 * Returns the frame now in the spec, or NULL when the family has none. */
const GdFrame *gd_spec_fit(GdSpec *spec, GdFamily family);

typedef enum { GD_IN_NONE = 0, GD_IN_UP, GD_IN_DOWN, GD_IN_LEFT, GD_IN_RIGHT, GD_IN_ACCEPT, GD_IN_BACK, GD_IN_START } GdInput;
typedef enum { GD_RES_NONE = 0, GD_RES_FIRST, GD_RES_SECOND,   /* FIRST / SECOND button; SECOND is also
                                                                * the Back result of a two-button dialog */
               GD_RES_THIRD,                                   /* the third row (L3) of a three-button dialog */
               GD_RES_CANCEL } GdResult;                       /* Back on a three-button dialog or help sheet */

typedef struct GuiDialog {
    GdSpec spec;
    int open;                     /* logically open: owns all input */
    int focus;                    /* focused button: 0 first / 1 second / 2 third; the help sheet has none */
    int closing;                  /* exit script running */
    float exit_left_ms;
    GdResult pending;             /* chosen, delivered when the exit completes */
    GdResult delivered;
    int deliveries;
} GuiDialog;

/* Opens a fresh dialog: two-button kinds focus the second button (6), the three-button frame the
 * middle row, the help sheet nothing; nothing pending. */
void gd_open(GuiDialog *d, const GdSpec *spec);
/* Two-button: Left/Right move between the buttons without wrapping; Accept chooses the focused
 * button; Back chooses Cancel; Up/Down/Start do nothing.
 * Three-button: Up/Down step through the rows (L1 bottom to L3 top) without wrapping; Left/Right
 * do nothing (the rows are stacked vertically); Accept chooses the focused row; Back chooses
 * GD_RES_CANCEL. Help: Accept closes with GD_RES_FIRST, Back with GD_RES_CANCEL. Input is ignored
 * while the exit runs. */
void gd_input(GuiDialog *d, GdInput in);
/* Primary click at centred virtual coordinates: a click on a button's extent focuses and chooses it
 * (on a help sheet, a click on the caption row closes it). Returns 1 when handled. */
int gd_pointer_click(GuiDialog *d, float x, float y);
void gd_update(GuiDialog *d, float seconds);
/* Exactly-once result, after the 140 ms exit; the dialog is closed when it is returned. */
GdResult gd_poll(GuiDialog *d);
/* Button extents (centred coordinates, edges inclusive): x0,y0,x1,y1.
 * Two-button kinds (unchanged): the authored pill boxes, x149 wide around +/-150 (GenericDialog_
 * SMALL family) or +/-151, y 53..95, shifted with the frame's button row.
 * Three-button and help: gd_spec_button_extent below. `button` indexes the frame's buttons. */
void gd_button_extent(GdKind kind, int button, float e[4]);
/* Extent of one button of any frame family, derived from the spec's retail frame [data]:
 * three-button rows: x -256..256 (the authored 512-unit strips), row centres y 76/120/164 for
 * button 0/1/2 (L1 bottom, L2 middle, L3 top), each +/-21 (the authored 42-unit row height).
 * Help: button 0 is the caption row, x 172..268 (the B caption's authored extent), y = the
 * caption row centre +/-10; other buttons are empty. */
void gd_spec_button_extent(const GdSpec *spec, int button, float e[4]);

#ifdef __cplusplus
}
#endif
#endif
