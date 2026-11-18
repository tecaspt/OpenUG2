/* pause_screen.c -- see pause_screen.h. Rules: SPEC_GUI_ART 2.4 (actions, timing, dialogs, creation
 * guard), 3.2 (navigation), 3.3 (edge fade). */
#include "pause_screen.h"

#include <string.h>

#include "gui_timing.h"

static void push(PauseScreen *p, PsEvent e) {
    if (p->nevents < (int)(sizeof p->events / sizeof p->events[0])) p->events[p->nevents++] = e;
}

static void open_common(PauseScreen *p, const PauseContext *ctx, const PauseEntry *entries, int n) {
    unsigned build = p->build_serial + 1, move = p->move_serial, exit = p->exit_serial, dlg = p->dialog_serial;
    memset(p, 0, sizeof *p);
    p->build_serial = build; p->move_serial = move; p->exit_serial = exit; p->dialog_serial = dlg;
    if (ctx) p->ctx = *ctx; else pe_context_default(&p->ctx);
    if (entries) {
        if (n > PE_MAX) n = PE_MAX;
        if (n < 0) n = 0;
        memcpy(p->entries, entries, sizeof(PauseEntry) * (size_t)n);
        p->n = n;
        for (int i = 0; i < n; i++) p->entries[i].disabled = p->entries[i].needs_replay && !p->ctx.replay_available;
    } else {
        p->n = pe_build(&p->ctx, p->entries);
    }
    int disabled[PE_MAX];
    for (int i = 0; i < p->n; i++) disabled[i] = p->entries[i].disabled;
    gc_init(&p->car, GC_RAIL_PAUSE);
    gc_set_entries(&p->car, p->n, disabled);                 /* focus on entry 1, scroll snapped */
    p->fresh = 1;
}
void ps_open(PauseScreen *p, const PauseContext *ctx) { open_common(p, ctx, NULL, 0); }
void ps_open_entries(PauseScreen *p, const PauseContext *ctx, const PauseEntry *entries, int n) { open_common(p, ctx, entries, n); }

static void begin_exit(PauseScreen *p, PsEvent then) {
    p->exiting = 1;
    p->exit_event = then;
    p->exit_left_ms = (float)gt_exit_complete_ms("UI_pause.fng");
    p->exit_serial++;
    gc_enable_fade(&p->car, 0);                              /* edge fade ends on activation / Back (3.3) */
}

static GdKind dialog_kind(PeAction a) {
    switch (a) {
    case PE_ACT_CONFIRM_RESTART: return GD_RESTART;
    case PE_ACT_CONFIRM_EXPLORE: return GD_EXPLORE;
    case PE_ACT_CONFIRM_WORLD_MAP: return GD_WORLD_MAP;
    case PE_ACT_CONFIRM_GARAGE: return GD_GARAGE;
    default: return GD_QUIT;
    }
}
static PsEvent confirm_event(PeAction a) {
    switch (a) {
    case PE_ACT_CONFIRM_RESTART: return PS_EV_RESTART;
    case PE_ACT_CONFIRM_EXPLORE: return PS_EV_EXPLORE;
    case PE_ACT_CONFIRM_WORLD_MAP: return PS_EV_WORLD_MAP;
    case PE_ACT_CONFIRM_GARAGE: return PS_EV_GARAGE;
    default: return PS_EV_QUIT;
    }
}

static void activate(PauseScreen *p) {
    if (p->n <= 0 || p->car.cur < 0 || p->car.cur >= p->n) return;
    const PauseEntry *e = &p->entries[p->car.cur];
    if (e->disabled) return;                                 /* cannot be activated (2.3) */
    switch (e->action) {
    case PE_ACT_RESUME_EXPLORE: begin_exit(p, PS_EV_RESUME); break;
    case PE_ACT_OPEN_OPTIONS: begin_exit(p, PS_EV_OPTIONS); break;
    case PE_ACT_OPEN_REPLAY: begin_exit(p, PS_EV_REPLAY); break;
    case PE_ACT_RESUME_RACE: push(p, PS_EV_RESUME); break;   /* immediate: the pause package is removed */
    default: {                                               /* confirmation dialog over the dimmed screen */
        GdContext g = { p->ctx.mode, p->ctx.substate4, p->ctx.session > 2, p->ctx.online_flag };
        GdSpec spec;
        if (e->dialog) gd_spec(&spec, e->dialog, &g);   /* the entry's own dialog kind (TASK-AL) */
        else gd_spec(&spec, dialog_kind(e->action), &g);
        gd_open(&p->dialog, &spec);
        p->dialog_entry = p->car.cur;
        p->dialog_serial++;
        gc_enable_fade(&p->car, 0);                          /* a dim starts: colours restored (3.3) */
        p->ptr.armed = 0;
        break;
    }
    }
}

static void step(PauseScreen *p, int dir) {
    if (gc_step(&p->car, dir)) { p->move_dir = dir; p->move_serial++; }
}

void ps_input(PauseScreen *p, PsInput in) {
    if (p->exiting) return;
    if (p->dialog.open) {
        GdInput g = in == PS_IN_LEFT ? GD_IN_LEFT : in == PS_IN_RIGHT ? GD_IN_RIGHT :
                    in == PS_IN_UP ? GD_IN_UP : in == PS_IN_DOWN ? GD_IN_DOWN :
                    in == PS_IN_ACCEPT ? GD_IN_ACCEPT :
                    in == PS_IN_BACK ? GD_IN_BACK : in == PS_IN_START ? GD_IN_START : GD_IN_NONE;
        if (p->fresh && (in == PS_IN_ACCEPT || in == PS_IN_BACK)) return;
        gd_input(&p->dialog, g);
        return;
    }
    switch (in) {
    case PS_IN_LEFT: step(p, -1); break;
    case PS_IN_RIGHT: step(p, 1); break;
    case PS_IN_ACCEPT: if (!p->fresh) activate(p); break;
    case PS_IN_BACK: case PS_IN_START:                        /* exit, then resume at exit-complete (2.4) */
        if (!p->fresh) begin_exit(p, PS_EV_RESUME);
        break;
    default: break;                                          /* Up/Down: the carousel is horizontal */
    }
}

/* The dialog's hit-testable rows, in the frame's own order: two-button pills, three-button
 * stacked rows, or the help sheet's single caption row. */
static int dialog_button_count(const GuiDialog *d) {
    return d->spec.frame ? d->spec.frame->buttons : 2;
}
static int dialog_button_at(const GuiDialog *d, float x, float y) {
    int n = dialog_button_count(d);
    for (int b = 0; b < n; b++) {
        float e[4];
        gd_spec_button_extent(&d->spec, b, e);
        if (x >= e[0] && x <= e[2] && y >= e[1] && y <= e[3]) return b + 1;
    }
    return 0;
}

void ps_pointer_press(PauseScreen *p, float x, float y) {
    p->ptr.armed = 0;
    if (p->exiting) return;
    if (p->dialog.open) {
        if (!p->fresh && !p->dialog.closing) p->ptr.armed = dialog_button_at(&p->dialog, x, y);
        return;
    }
    GcPtAction a = gc_pointer_press(&p->car, &p->ptr, x, y);
    if (a == GC_PT_LEFT) step(p, -1);
    else if (a == GC_PT_RIGHT) step(p, 1);
}
void ps_pointer_release(PauseScreen *p, float x, float y) {
    if (p->exiting) return;
    if (p->dialog.open) {
        int pressed = p->ptr.armed;
        p->ptr.armed = 0;
        if (!p->fresh && pressed && pressed == dialog_button_at(&p->dialog, x, y))
            gd_pointer_click(&p->dialog, x, y);
        return;
    }
    if (gc_pointer_release(&p->car, &p->ptr, x, y) == GC_PT_ACTIVATE && !p->fresh) activate(p);
}
void ps_pointer_click(PauseScreen *p, float x, float y) {
    ps_pointer_press(p, x, y);
    ps_pointer_release(p, x, y);
}

void ps_update(PauseScreen *p, float seconds) {
    p->fresh = 0;
    gc_update(&p->car, seconds);
    if (p->dialog.open) {
        gd_update(&p->dialog, seconds);
        GdResult r = gd_poll(&p->dialog);
        if (r != GD_RES_NONE) {
            p->ptr.armed = 0;
            PeAction a = p->entries[p->dialog_entry].action;
            memset(&p->dialog, 0, sizeof p->dialog);
            p->dialog_serial++;                              /* UNDIM */
            if (r == GD_RES_FIRST) push(p, confirm_event(a));
            else gc_enable_fade(&p->car, 1);                 /* first dialog close enables the edge fade (3.3) */
        }
    }
    if (p->exiting) {
        p->exit_left_ms -= seconds > 0 ? seconds * 1000.0f : 0.0f;
        if (p->exit_left_ms <= 0) {
            p->exiting = 0;
            push(p, p->exit_event);
        }
    }
}

PsEvent ps_poll(PauseScreen *p) {
    if (p->nevents == 0) return PS_EV_NONE;
    PsEvent e = p->events[0];
    for (int i = 1; i < p->nevents; i++) p->events[i - 1] = p->events[i];
    p->nevents--;
    return e;
}

int ps_count(const PauseScreen *p) { return p->n; }
int ps_selected(const PauseScreen *p) { return p->car.cur; }
const PauseEntry *ps_entry(const PauseScreen *p, int i) { return i >= 0 && i < p->n ? &p->entries[i] : NULL; }
const char *ps_caption(const PauseScreen *p) {
    return p->n > 0 && p->car.cur >= 0 && p->car.cur < p->n ? p->entries[p->car.cur].label : "";
}
int ps_dialog_open(const PauseScreen *p) { return p->dialog.open; }
int ps_busy(const PauseScreen *p) { return p->exiting; }
