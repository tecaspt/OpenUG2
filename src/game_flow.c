/* game_flow.c -- see game_flow.h. */
#include "game_flow.h"

#include <string.h>

#define GF_SWITCH_WAIT_FRAMES 3
#define GF_RESULTS_DELAY 3.0f

void gf_init(GameFlow *g, int enabled) {
    memset(g, 0, sizeof *g);
    g->enabled = enabled ? 1 : 0;
}

void gf_start(GameFlow *g, const GfLaunch *l) {
    if (!g->enabled || !l) return;
    g->launch = *l;
    g->last = *l;
    g->state = GF_LOADING;
    g->timer = 0.0f;
    g->frames = 0;
}

static void copy_str(char *dst, size_t n, const char *src) {
    size_t k = src ? strlen(src) : 0;
    if (k >= n) k = n - 1;
    if (k) memcpy(dst, src, k);
    dst[k] = 0;
}

static GfCommand cmd_of(GfCmdKind k) {
    GfCommand c;
    memset(&c, 0, sizeof c);
    c.kind = k;
    return c;
}

static GfCommand cmd_error(GameFlow *g, const char *msg) {
    GfCommand c = cmd_of(GF_CMD_ERROR);
    copy_str(c.message, sizeof c.message, msg && *msg ? msg : "launch failed");
    g->state = GF_QR_TRACK;
    g->timer = 0.0f;
    g->frames = 0;
    return c;
}

static int rejected(const GfHost *h) {
    return h->race_state == 1 && !h->race_active && h->race_error && h->race_error[0];
}

static int name_matches(const char *want, const char *have) {
    return !want[0] || (have && strcmp(want, have) == 0);
}

GfCommand gf_step(GameFlow *g, const GfHost *h, float dt) {
    if (!g->enabled) return cmd_of(GF_CMD_NONE);
    for (;;) {
        switch (g->state) {
        case GF_LOADING: {
            g->state = GF_TRACK;
            g->frames = 0;
            return cmd_of(GF_CMD_SHOW_LOADING);
        }
        case GF_TRACK:
        case GF_CAR: {
            int is_track = g->state == GF_TRACK;
            const char *want = is_track ? g->launch.track : g->launch.car;
            const char *have = is_track ? h->track : h->car;
            if (name_matches(want, have)) {
                g->state = is_track ? GF_CAR : (g->launch.free_run ? GF_FREE_STOP : GF_ARM);
                g->frames = 0;
                continue;
            }
            if (g->frames == 0) {
                GfCommand c = cmd_of(is_track ? GF_CMD_SWITCH_TRACK : GF_CMD_SWITCH_CAR);
                copy_str(c.name, sizeof c.name, want);
                g->frames = 1;
                return c;
            }
            if (++g->frames > GF_SWITCH_WAIT_FRAMES)
                return cmd_error(g, is_track ? "track switch failed" : "car switch failed");
            return cmd_of(GF_CMD_NONE);
        }
        case GF_FREE_STOP:
            g->state = GF_CLOSE;
            if (h->race_active) return cmd_of(GF_CMD_STOP_RACE);
            continue;
        case GF_ARM: {
            GfCommand c = cmd_of(GF_CMD_ARM_EVENT);
            c.event_id = g->launch.event_id;
            c.laps = g->launch.laps;
            c.difficulty = g->launch.difficulty;
            g->state = GF_ARM_CHECK;
            return c;
        }
        case GF_ARM_CHECK:
            if (rejected(h)) return cmd_error(g, h->race_error);
            g->state = GF_CLOSE;
            continue;
        case GF_CLOSE:
            g->state = g->launch.free_run ? GF_RUNNING : GF_WAIT_RACE;
            g->timer = 0.0f;
            return cmd_of(GF_CMD_CLOSE_MENU);
        case GF_WAIT_RACE:
            if (rejected(h)) return cmd_error(g, h->race_error);
            if (h->race_active) {
                g->state = GF_RUNNING;
                g->timer = 0.0f;
            }
            return cmd_of(GF_CMD_NONE);
        case GF_RUNNING:
            if (!g->launch.free_run && h->race_state == 2) {
                g->timer += dt;
                if (g->timer >= GF_RESULTS_DELAY) {
                    g->state = GF_RESULTS;
                    return cmd_of(GF_CMD_OPEN_RESULTS);
                }
            } else {
                g->timer = 0.0f;
            }
            return cmd_of(GF_CMD_NONE);
        case GF_QUIT_MAIN:
            g->state = GF_QUIT_STOP;
            return cmd_of(GF_CMD_OPEN_MAIN);
        case GF_QUIT_STOP:
            g->state = GF_IDLE;
            return cmd_of(GF_CMD_STOP_RACE);
        default:
            return cmd_of(GF_CMD_NONE);
        }
    }
}

void gf_pause_context(const GameFlow *g, int race_active, PauseContext *c) {
    (void)g;
    pe_context_default(c);
    c->mode = 0;
    c->race = race_active ? 1 : 0;
}

int gf_pause_action(GameFlow *g, FeAction a) {
    if (!g->enabled) return 0;
    switch (a) {
    case FE_ACTION_QUIT:
        g->state = GF_QUIT_MAIN;
        return 1;
    case FE_ACTION_RESTART:
        if (g->last.free_run) return 0;
        g->launch = g->last;
        g->state = GF_ARM;
        g->timer = 0.0f;
        g->frames = 0;
        return 1;
    case FE_ACTION_RESUME:
        return 1;
    default:
        return 0;
    }
}
