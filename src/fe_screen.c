/* fe_screen.c -- see fe_screen.h. */
#include "fe_screen.h"

#include <string.h>

void fes_open(FeCarouselScreen *s, const FeEntry *entries, int n, int focus_id, float rail_w) {
    int disabled[GC_MAX];
    memset(s, 0, sizeof *s);
    if (n > GC_MAX) n = GC_MAX;
    if (n < 0 || !entries) n = 0;
    if (n) memcpy(s->e, entries, sizeof(FeEntry) * (size_t)n);
    s->n = n;
    for (int i = 0; i < n; i++) disabled[i] = s->e[i].disabled;
    gc_init(&s->car, rail_w);
    fes_set_arrows(s, GC_ARROW_L_X, GC_ARROW_R_X, GC_ARROW_Y);
    gc_set_entries(&s->car, n, disabled);                    /* focus on the first enabled entry */
    for (int i = 0; i < n; i++)
        if (s->e[i].id == focus_id && !s->e[i].disabled) { gc_snap_to(&s->car, i); break; }
}

void fes_set_arrows(FeCarouselScreen *s, float left_x, float right_x, float y) {
    s->arrow_x[0] = left_x; s->arrow_x[1] = right_x; s->arrow_y = y;
}

/* -1 / +1 when (x, y) is inside the visible left / right arrow box (edges inclusive), else 0. */
static int arrow_at(const FeCarouselScreen *s, float x, float y) {
    float h = GC_ARROW_SIZE / 2;
    if (y < s->arrow_y - h || y > s->arrow_y + h) return 0;
    if (x >= s->arrow_x[0] - h && x <= s->arrow_x[0] + h) return gc_left_arrow_visible(&s->car) ? -1 : 0;
    if (x >= s->arrow_x[1] - h && x <= s->arrow_x[1] + h) return gc_right_arrow_visible(&s->car) ? 1 : 0;
    return 0;
}

static void begin_exit(FeCarouselScreen *s, int reason) {
    s->exiting = 1;
    s->reason = reason;
    gc_enable_fade(&s->car, 0);
}

static void accept(FeCarouselScreen *s) {
    int c = s->car.cur;
    if (s->n <= 0 || c < 0 || c >= s->n || s->e[c].disabled) return;   /* cannot be chosen */
    s->chosen = s->e[c].id;
    begin_exit(s, FES_IN_ACCEPT);
}

void fes_input(FeCarouselScreen *s, FesInput in) {
    if (s->exiting) return;
    switch (in) {
    case FES_IN_LEFT: gc_step(&s->car, -1); break;
    case FES_IN_RIGHT: gc_step(&s->car, 1); break;
    case FES_IN_ACCEPT: accept(s); break;
    case FES_IN_BACK: begin_exit(s, FES_IN_BACK); break;
    }
}

void fes_pointer_press(FeCarouselScreen *s, float x, float y) {
    s->ptr.armed = 0;
    if (s->exiting) return;
    int a = arrow_at(s, x, y);
    if (a) gc_step(&s->car, a);                               /* arrows step at press-down */
    else s->ptr.armed = s->n > 0 && gc_slot_hit(&s->car, x, y);
}

void fes_pointer_release(FeCarouselScreen *s, float x, float y) {
    int armed = s->ptr.armed;
    s->ptr.armed = 0;
    if (s->exiting) return;
    if (armed && gc_slot_hit(&s->car, x, y)) accept(s);       /* activates when the press began on the slot */
}

void fes_exit_complete(FeCarouselScreen *s) {
    if (!s->exiting) return;
    s->exiting = 0;
    s->pending = s->reason == FES_IN_ACCEPT ? FES_EV_CHOSEN : FES_EV_BACK;
}

void fes_update(FeCarouselScreen *s, float seconds) { gc_update(&s->car, seconds); }

FesEvent fes_poll(FeCarouselScreen *s, int *entry_id) {
    FesEvent e = s->pending;
    s->pending = FES_EV_NONE;
    if (e == FES_EV_CHOSEN && entry_id) *entry_id = s->chosen;
    return e;
}

const char *fes_caption(const FeCarouselScreen *s) {
    return s->n > 0 && s->car.cur >= 0 && s->car.cur < s->n && s->e[s->car.cur].label ? s->e[s->car.cur].label : "";
}
