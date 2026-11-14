/* gui_view.h -- presentation of the pause menu and the graphics option screens by evaluating the
 * authored retail packages (SPEC_GUI_ART 1, 3, 4, 5, 6, 8).
 *
 * GL-free: a view owns a UgScene over a parsed package, binds the application-driven objects by
 * name hash (rows, sliders, arrows, scrollbar, carousel slots, dialog buttons), writes the state of
 * the state machines into them with the base-key rule (ug_scene_app_write) and lets the authored
 * scripts do the animation. Nothing is drawn natively: if a package, texture or font is missing the
 * view reports itself unavailable and draws nothing (fail closed, SPEC_GUI_ART 8.5).
 *
 * Packages come from the user's install at run time (UgAssets). Tests attach synthetic in-memory
 * packages instead. Design log: docs/cleanroom-settings/IMPL_DESIGN_GUI.md. */
#ifndef OPENUG2_GUI_VIEW_H
#define OPENUG2_GUI_VIEW_H

#include "ugui/ugui_assets.h"
#include "ugui/ugui_scene.h"
#include "ugui/ugui_ui.h"

#include "gui_carousel.h"
#include "gui_dialog.h"
#include "gui_layout.h"
#include "options_menu.h"
#include "controls_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- helpers --------------------------------------------------------------------------------- */
/* Texture library index + 1 for a texture key, 0 when missing or undecodable. */
int gv_texture(const UgAssets *a, uint32_t key);
/* Language text for a label key; `fallback` (the English text of the key) when the table lacks it. */
const char *gv_text(const UgAssets *a, uint32_t key, const char *fallback);

/* ---- one loaded package ---------------------------------------------------------------------- */
/* An anchor ("master") object sampled once when the screen is created, before any script of the
 * freshly loaded package has run: its authored (stored) centre and size (AMEND1 A6). Never re-read. */
typedef struct GvAnchor {
    int found;                   /* a unique object with that name existed at creation */
    float x, y, w, h;
} GvAnchor;

typedef struct GvScreen {
    UgFngPackage pkg;            /* owned parse when loaded from the assets */
    int own_pkg;
    const UgFngPackage *pk;
    UgScene scene;
    int loaded;
    const UgAssets *assets;
    int handler;                 /* event-handler object (name hash 0x47FF4E7C) or -1 */
    char name[48];
    char status[220];            /* why the screen is unavailable, empty when fine */
    int enter_done;              /* the package reported its enter-complete message */
    int exit_done;               /* ... and its exit-complete message (0xE1FDE1D1) */
    float time_ms;               /* scene time since creation */
    int exit_started;
    int draw_ok;                 /* the last draw produced everything */
    GvAnchor opt_master;         /* OPTION_MASTER (carousel packages) */
    GvAnchor title_master;       /* TITLE_MASTER and DATA_MASTER (list packages) */
    GvAnchor data_master;
    GvAnchor data_master2;       /* DATA_MASTER2 (Controls) */
} GvScreen;

/* Loads `package_name` from the assets, builds the scene (PC build, hint text hidden) and runs the
 * first update. Returns 0 and fills status when the package is missing or unreadable. */
int gv_screen_load(GvScreen *s, const UgAssets *assets, const char *package_name);
/* Attaches an in-memory package (borrowed; tests). `assets` may be NULL. */
int gv_screen_attach(GvScreen *s, const UgFngPackage *pk, const UgAssets *assets, const char *package_name);
void gv_screen_free(GvScreen *s);
/* Advances the scene by dt_ms and collects the package's messages to the application. */
void gv_screen_update(GvScreen *s, float dt_ms);
/* Starts the authored exit script on the event-handler object (SPEC_GUI_ART 1.4). */
void gv_screen_exit(GvScreen *s);
void gv_screen_dim(GvScreen *s, int dim);                /* handler script DIM / UNDIM */
/* A confirmation dialog opens over (open != 0) or closes over the screen (AMEND1 B2). Opening sends
 * the "start dim" request (DIM on the handler) and then, on the same handler object, the screen's own
 * exit script unless that is already running: the last message the targets receive is the exit one,
 * so the presentation group, slots, rail, tab and arrows fade to alpha 0. This is not the screen's
 * real exit (exit_started stays clear). Closing sends "stop dim" (UNDIM): UnDim brings everything
 * back from alpha 60 to 255 within 100 ms. */
void gv_screen_dialog(GvScreen *s, int open);
/* 1 when everything visible has its font and texture. */
int gv_screen_ready(const GvScreen *s);
/* Draws into the canvas (centred coordinates). Returns 1 when the canvas stayed complete; on failure
 * the screen's geometry is dropped and draw_ok is cleared. */
int gv_screen_draw(GvScreen *s, UgCtx *ctx, UgCanvas *cv);

/* ---- icon carousel (UI_pause.fng, UI_PauseOptionsMain.fng) -------------------------------- */
typedef struct GvEntry { int tex; const char *label; } GvEntry;

typedef struct GvCarousel {
    GvScreen *scr;
    int slot[GC_MAX];
    int nslots;                  /* option_k objects found, consecutive from 1 */
    int master, title, header, back_button, select_button;
    int rail, tab, arrow_l, arrow_r;
    int last_alpha[GC_MAX];
    unsigned seen_move;
    int bound;
} GvCarousel;

/* Binds the carousel objects by name. The OPTION_MASTER centre is the anchor cached when the screen
 * was created (AMEND1 A6), never the object's current, script-evaluated position; it is copied into `car`. Returns 0 (and
 * leaves the screen's status) when a required object is missing or ambiguous. */
int gv_carousel_bind(GvCarousel *v, GvScreen *s, GuiCarousel *car);
/* Writes the carousel state into the objects: slot icon, position, size, visibility, edge-fade alpha,
 * caption, header, rail/tab/arrows, focus and the arrow blink for `move_serial`. */
void gv_carousel_apply(GvCarousel *v, const GuiCarousel *car, const GvEntry *entries, int n,
                       const char *header, unsigned move_serial, int move_dir);

/* ---- option list (UI_Options / UI_PauseOptions / UI_PC_Customize_Options) --------------------- */
typedef struct GvRow {
    int name, data, left, right, backing;
    int base, knob, handle, value, beg, end;
    int parked;                  /* currently moved off-screen */
    char vtext[16];              /* SLIDER_VALUE text (when the package has one) */
    float hl_fade_ms;            /* backing/arrows stay visible while UnHighlight plays */
} GvRow;

typedef struct GvList {
    GvScreen *scr;
    GvRow row[OM_MAX_ROWS + 2];  /* indexed by slot number 1..N */
    int nslots;
    GlAnchors anchors;
    int header, header2;
    int rail, tab, arrow1, arrow2, end1, end2;
    int arrow_tex;
    OmContext ctx;
    OmScreen kind;
    int highlighted;             /* row (0-based) that currently runs Highlight, -1 none */
    unsigned seen_build, seen_blink, seen_exit, seen_dialog;
    int bound;
} GvList;

/* Binds rows, anchors, header and scrollbar objects of the screen the menu is on. Returns 0 when a
 * required object (anchors, row-1 name) is missing; the screen's status says which. */
int gv_list_bind(GvList *v, GvScreen *s, const OptionsMenu *m);
/* Writes the menu state into the objects (placement, text, sliders, focus scripts, scrollbar, dim,
 * exit). dt_ms advances the highlight fade-out bookkeeping. */
void gv_list_sync(GvList *v, const OptionsMenu *m, float dt_ms);

/* General application list. Text pointers must survive until the next sync/draw. */
typedef struct GvListRow { const char *label, *value; int enabled; } GvListRow;
int gv_list_bind_rows(GvList *, GvScreen *);
void gv_list_sync_rows(GvList *, const GvListRow *, int count, int focus, float dt_ms);

/* ---- Controls (UI_Options_PC_Controller) -------------------------------------------------------- */
#define GV_CTL_SLOTS 20          /* option_name_n / OPTION_DATA_n / OPTION_DATA_2_n / Options_Backing_n, n = 1..20 */
typedef struct GvCtlRow {
    int name, data, data2, backing;
    int parked;
    float hl_fade_ms;            /* the backing stays visible while UnHighlight plays */
    char text[2][CB_NAME_MAX];   /* cell texts (the scene keeps the pointers) */
} GvCtlRow;

typedef struct GvControls {
    GvScreen *scr;
    GvCtlRow row[GV_CTL_SLOTS + 1];          /* indexed by slot number */
    int nslots;
    int header, title, arrow_l, arrow_r, col1, col2, instr;
    int rail, tab, arrow1, arrow2;
    float tx, ty, th, dx[2], dw;             /* anchors as top-left + size (3.8) */
    int hl_row, hl_col;                      /* highlighted cell, -1 none */
    int hl_title;                            /* the selector title runs Highlight */
    int tab_row;                             /* row the scroll tab follows */
    unsigned seen_build, seen_blink, seen_scroll, seen_capture;
    int was_capturing;
    int bound;
} GvControls;

/* Binds the authored objects by name hash. Returns 0 (status says what is missing) when any object the screen
 * needs is absent or ambiguous; the screen is then not drawn (CONTROLS_SPEC R3). */
int gv_controls_bind(GvControls *v, GvScreen *s, const ControlsMenu *m);
/* Writes the state of the Controls screen into the objects: texts (language file only), row placement and
 * window, focus scripts, capture pulse, selector arrows, scrollbar. */
void gv_controls_sync(GvControls *v, const ControlsMenu *m, float dt_ms);

/* ---- two-button dialog (UI_InGameDialog / GenericDialog_SMALL) ---------------------------- */
typedef struct GvDialog {
    GvScreen scr;
    int msg, lbl1, lbl2, lbl3, b1, b2, b3;
    int open;
    int seen_focus;
    int exit_sent;
    int bound;
} GvDialog;
/* Loads the dialog package named by `d->spec`, fills texts, hides the third button and focuses the
 * second. Returns 0 (unavailable) when the package, its objects or art are missing. */
int gv_dialog_open(GvDialog *v, const UgAssets *assets, const GuiDialog *d);
int gv_dialog_attach(GvDialog *v, const UgFngPackage *pk, const UgAssets *assets, const GuiDialog *d);
void gv_dialog_sync(GvDialog *v, const GuiDialog *d, float dt_ms);
void gv_dialog_close(GvDialog *v);

#ifdef __cplusplus
}
#endif
#endif
