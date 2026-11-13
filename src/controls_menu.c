/* controls_menu.c -- see controls_menu.h. Rules: CONTROLS_SPEC 3.8 (layout), 4.2-4.5 (selector,
 * input map, window, capture), 4.6 (reset request), 4.7 (leaving). */
#include "controls_menu.h"

#include <stdio.h>
#include <string.h>

#include <SDL_scancode.h>

/* ---- geometry (3.8): T = TITLE_MASTER top-left (-277,-106) 180x36, D1 (-64,-102), D2 (98,-102) ---------- */
#define LABEL_X (-277.0f)
#define TOP_Y (-106.0f)
#define CELL_X0 (-64.0f)
#define CELL_X1 98.0f
/* selector arrows (3.1): 24x24 at (-266,-158) and (265,-158) */
#define ARROW_Y (-170.0f)
#define ARROW_L_X (-278.0f)
#define ARROW_R_X 253.0f
#define ARROW_SIZE 24.0f
/* The title row between the arrows takes pointer hover (4.4); its extent is not specified [inferred]. */
#define TITLE_X0 (-250.0f)
#define TITLE_X1 250.0f
#define TITLE_Y0 (-182.0f)
#define TITLE_Y1 (-146.0f)

float cm_row_top(int line) { return TOP_Y + CM_ROW_PITCH * (float)line; }
float cm_label_x(void) { return LABEL_X; }
float cm_cell_x(int col) { return col ? CELL_X1 : CELL_X0; }
float cm_cell_cx(int col) { return cm_cell_x(col) + CM_CELL_W / 2; }

static int in_rect(float x, float y, float x0, float y0, float w, float h) {
    return x >= x0 && x <= x0 + w && y >= y0 && y <= y0 + h;       /* edges inclusive */
}

/* ---- construction ----------------------------------------------------------------------------------- */

static void build_rows(ControlsMenu *m) {
    m->nrows = cb_rows(m->dev[m->cur].cls, m->rows);
    m->top = 0;
    m->build_serial++;
}

void cm_open(ControlsMenu *m, CbBindings *b, const CmPlatform *plat) {
    unsigned build = m->build_serial, blink = m->blink_serial, scroll = m->scroll_serial, cap = m->capture_serial;
    memset(m, 0, sizeof *m);
    m->build_serial = build; m->blink_serial = blink; m->scroll_serial = scroll; m->capture_serial = cap;
    m->b = b;
    if (plat) m->plat = *plat;
    m->open = 1;
    m->dev[0].cls = CB_CLASS_KEYBOARD;                             /* the system keyboard is always first */
    m->dev[0].pad = -1;
    m->ndev = 1;
    for (int i = 0; i < CB_MAX_PADS; i++)
        if (b->pad[i].present) {
            m->dev[m->ndev].cls = cb_pad_class(&b->pad[i]);
            m->dev[m->ndev].pad = i;
            m->ndev++;
        }
    m->cur = 0;
    m->focus_row = 0;
    m->focus_col = 0;
    m->instruction = 0;
    build_rows(m);
}
void cm_close(ControlsMenu *m) { m->open = 0; m->capturing = 0; m->locked = 0; m->end_stage = 0; }
int cm_is_open(const ControlsMenu *m) { return m->open; }

/* ---- capture ---------------------------------------------------------------------------------------- */

static int cur_pad(const ControlsMenu *m) { return m->dev[m->cur].pad; }

static void begin_capture(ControlsMenu *m, int pulse) {
    m->capturing = 1;
    m->locked = 1;                                                 /* all menu input is disabled (4.5 step 3) */
    m->end_stage = 0;
    m->cap_row = m->focus_row;
    m->cap_col = m->focus_col;
    m->cap_pulse = pulse;
    m->instruction = 1;
    if (cur_pad(m) >= 0) cb_pad_capture_begin(&m->padcap);
    m->capture_serial++;
}

static void end_capture(ControlsMenu *m, int assigned) {
    m->capturing = 0;
    m->end_stage = 1;
    if (assigned) m->modified = 1;
    m->capture_serial++;
}

void cm_key_down(ControlsMenu *m, int scancode) {
    if (!m->open || !m->capturing) return;
    if (scancode == SDL_SCANCODE_ESCAPE) { end_capture(m, 0); return; }        /* cancels on any device */
    if (cur_pad(m) >= 0) return;                                               /* keys do nothing in a joystick capture */
    int action = m->rows[m->cap_row].action;
    end_capture(m, cb_kb_capture(&m->b->kb, action, m->cap_col, scancode) == CB_CAP_ASSIGNED);
}

void cm_pad_frame(ControlsMenu *m, const CbPadSample *s) {
    if (!m->open || !m->capturing || cur_pad(m) < 0) return;
    CbPad *p = &m->b->pad[cur_pad(m)];
    int action = m->rows[m->cap_row].action;
    CbPadSample local = *s;
    if (action == CB_ACT_THROTTLE || action == CB_ACT_BRAKE || action == CB_ACT_STEER_LEFT || action == CB_ACT_STEER_RIGHT)
        for (int k = 0; k < CB_VBTN_COUNT; k++)          /* a trigger pressed for a pedal row must still assign its axis, not the virtual button */
            if (p->info.std && p->info.vbtn_axis[k] >= 0) local.buttons &= ~(1u << (CB_VBTN_FIRST + k));
    if (cb_pad_capture_frame(&p->dev, &m->padcap, action, m->cap_col, &local, p->info.wheel))
        end_capture(m, 1);
}

void cm_tick(ControlsMenu *m) {
    if (!m->open) return;
    if (m->end_stage == 1) m->end_stage = 2;                       /* the end is detected, texts refresh */
    else if (m->end_stage == 2) {                                  /* next frame: input comes back */
        m->end_stage = 0;
        m->locked = 0;
        m->instruction = 2;                                        /* stays until the screen is left */
    }
}

/* ---- navigation ------------------------------------------------------------------------------------- */

static void change_device(ControlsMenu *m, int dir) {
    if (m->ndev <= 1) return;
    m->cur = (m->cur + (dir < 0 ? m->ndev - 1 : 1)) % m->ndev;      /* wraps at both ends */
    m->blink_dir = dir;
    m->blink_serial++;
    build_rows(m);
}

static void move_row(ControlsMenu *m, int dir) {
    m->scroll_dir = dir;
    m->scroll_serial++;                                            /* the package blinks its scroll arrows */
    if (m->focus_row < 0) {
        if (dir > 0) { m->focus_row = 0; m->focus_col = 0; }       /* Down from the selector: row 1, Primary */
        return;
    }
    int nr = m->focus_row + dir;
    if (nr < 0) { m->focus_row = -1; return; }                     /* Up on row 1 */
    if (nr >= m->nrows) return;                                    /* never wraps */
    m->focus_row = nr;
    if (nr >= m->top + CM_VISIBLE_ROWS) m->top++;
    if (nr < m->top) m->top--;
}

void cm_input(ControlsMenu *m, CmInput in) {
    if (!m->open || m->locked) return;
    switch (in) {
    case CM_IN_UP: move_row(m, -1); break;
    case CM_IN_DOWN: move_row(m, 1); break;
    case CM_IN_LEFT:
        if (m->focus_row < 0) change_device(m, -1);
        else if (m->focus_col == 1) m->focus_col = 0;
        break;
    case CM_IN_RIGHT:
        if (m->focus_row < 0) change_device(m, 1);
        else if (m->focus_col == 0) m->focus_col = 1;
        break;
    case CM_IN_ACCEPT:
        if (m->focus_row >= 0 && m->nrows) begin_capture(m, 1);
        break;
    case CM_IN_BACK: m->want_leave = 1; break;
    case CM_IN_RESET: m->want_reset = 1; break;
    default: break;
    }
}

/* ---- pointer ---------------------------------------------------------------------------------------- */

static int hit_cell(const ControlsMenu *m, float x, float y, int *row, int *col) {
    for (int k = 0; k < CM_VISIBLE_ROWS; k++) {
        int i = m->top + k;
        if (i >= m->nrows) break;
        for (int c = 0; c < CM_COLUMNS; c++)
            if (in_rect(x, y, cm_cell_x(c), cm_row_top(k), CM_CELL_W, CM_CELL_H)) { *row = i; *col = c; return 1; }
    }
    return 0;
}

void cm_pointer_move(ControlsMenu *m, float x, float y) {
    if (!m->open || m->locked) return;
    int r, c;
    if (hit_cell(m, x, y, &r, &c)) { m->focus_row = r; m->focus_col = c; }
    else if (in_rect(x, y, TITLE_X0, TITLE_Y0, TITLE_X1 - TITLE_X0, TITLE_Y1 - TITLE_Y0)) m->focus_row = -1;
}

void cm_pointer_click(ControlsMenu *m, float x, float y) {
    if (!m->open || m->locked) return;
    if (m->ndev > 1) {
        int dir = in_rect(x, y, ARROW_L_X, ARROW_Y, ARROW_SIZE, ARROW_SIZE) ? -1 :
                  in_rect(x, y, ARROW_R_X, ARROW_Y, ARROW_SIZE, ARROW_SIZE) ? 1 : 0;
        if (dir) {
            m->focus_row = -1;                                     /* rows are rebuilt: focus moves to the selector (U7) */
            change_device(m, dir);
            return;
        }
    }
    int r, c;
    if (hit_cell(m, x, y, &r, &c)) {
        m->focus_row = r;
        m->focus_col = c;
        begin_capture(m, 0);                                       /* a click starts the capture without Pulse */
    }
}

/* ---- reset, events -------------------------------------------------------------------------------------- */

void cm_reset_device(ControlsMenu *m) {
    int p = cur_pad(m);
    if (p < 0) cb_kb_reset(&m->b->kb);
    else cb_pad_defaults(&m->b->pad[p].dev, &m->b->pad[p].info);
    m->modified = 1;
}
int cm_take_reset(ControlsMenu *m) { int v = m->want_reset; m->want_reset = 0; return v; }
int cm_take_leave(ControlsMenu *m) { int v = m->want_leave; m->want_leave = 0; return v; }
int cm_take_modified(ControlsMenu *m) { int v = m->modified; m->modified = 0; return v; }

/* ---- queries -------------------------------------------------------------------------------------------- */

const CmDevice *cm_device(const ControlsMenu *m) { return &m->dev[m->cur]; }
uint32_t cm_title_key(const ControlsMenu *m) { return cb_title_key(m->dev[m->cur].cls); }
int cm_row_count(const ControlsMenu *m) { return m->nrows; }
const CbRow *cm_row(const ControlsMenu *m, int i) { return i >= 0 && i < m->nrows ? &m->rows[i] : NULL; }
int cm_row_visible(const ControlsMenu *m, int i) { return i >= m->top && i < m->top + CM_VISIBLE_ROWS && i < m->nrows; }
uint32_t cm_instruction_key(const ControlsMenu *m) {
    return m->instruction == 1 ? CM_KEY_INSTR_CAPTURE : m->instruction == 2 ? CM_KEY_INSTR_ASSIGN : CM_KEY_INSTR_CONFIGURE;
}
int cm_arrows_visible(const ControlsMenu *m) { return m->ndev > 1; }
int cm_scrollbar_visible(const ControlsMenu *m) { return m->nrows > 1; }
float cm_scroll_fraction(const ControlsMenu *m) {
    int r = m->focus_row < 0 ? 0 : m->focus_row;
    return m->nrows > 1 ? (float)r / (float)(m->nrows - 1) : 0.0f;
}
int cm_input_enabled(const ControlsMenu *m) { return m->open && !m->locked; }

void cm_cell_text(const ControlsMenu *m, int row, int col, char out[CB_NAME_MAX]) {
    const CbRow *r = cm_row(m, row);
    const char *name = NULL;
    char raw[32];
    int joystick = cur_pad(m) >= 0;
    if (r && !joystick) {
        int sc = cb_kb_display(&m->b->kb, r->action, col);
        if (sc && m->plat.key_name) name = m->plat.key_name(m->plat.user, sc);
    } else if (r) {
        CbCtl c = cb_pad_display(&m->b->pad[cur_pad(m)].dev, r->action, col);
        if (c.kind == CB_CTL_BUTTON) { snprintf(raw, sizeof raw, "Button %d", c.index); name = raw; }
        else if (c.kind != CB_CTL_NONE && m->plat.ctl_name) name = m->plat.ctl_name(m->plat.user, cur_pad(m), c);
    }
    cb_format_name(name, joystick, out);
}
