/* pause_entries.c -- see pause_entries.h. Rules and data: SPEC_GUI_ART 2.2 (catalogue and order),
 * 2.3 (conditions), 2.4 (actions and timing). */
#include "pause_entries.h"

#include <string.h>

typedef struct Proto {
    PeId id;
    PeAction action;
    int immediate;
    uint32_t icon_key, label_key;
    const char *icon_name, *label;
    int needs_replay;
} Proto;

#define ICON_EXPLORE 0x62AA0DF6u
static const Proto P_EXPLORE_FREE = { PE_EXPLORE_FREE, PE_ACT_RESUME_EXPLORE, 0, ICON_EXPLORE, 0xC3E5BCDCu, "INGAME_ICON_EXPLORE", "Return to Explore Mode", 0 };
static const Proto P_FREE_RUN     = { PE_FREE_RUN, PE_ACT_RESUME_EXPLORE, 0, ICON_EXPLORE, 0xC44A6515u, "INGAME_ICON_EXPLORE", "Return to Free Run", 0 };
static const Proto P_GARAGE       = { PE_GARAGE, PE_ACT_CONFIRM_GARAGE, 1, 0xB8038AC0u, 0x453890E1u, "SHOP_ICON_CRIB", "Jump to Garage?", 0 };
static const Proto P_OPTIONS      = { PE_OPTIONS, PE_ACT_OPEN_OPTIONS, 0, 0x520DE4E3u, 0xB34993C9u, "INGAME_ICON_OPTIONS", "Options", 0 };
static const Proto P_REPLAY       = { PE_REPLAY, PE_ACT_OPEN_REPLAY, 0, 0x84D1D864u, 0x7840428Au, "INGAME_ICON_REPLAY", "Replay", 1 };
static const Proto P_QUIT_MAIN    = { PE_QUIT_MAIN, PE_ACT_CONFIRM_QUIT, 1, 0x4C9E34E6u, 0xC7E1420Cu, "INGAME_ICON_QUIT_TO_FE", "Quit to Main Menu", 0 };
static const Proto P_QUIT_ONLINE  = { PE_QUIT_ONLINE, PE_ACT_CONFIRM_QUIT, 1, 0x4C9E34E6u, 0xFF956AF2u, "INGAME_ICON_QUIT_TO_FE", "Quit to Online Main Menu", 0 };
static const Proto P_QUIT_LAN     = { PE_QUIT_LAN, PE_ACT_CONFIRM_QUIT, 1, 0x4C9E34E6u, 0xF39723A1u, "INGAME_ICON_QUIT_TO_FE", "Quit to LAN Main Menu", 0 };
static const Proto P_WORLD_MAP    = { PE_WORLD_MAP, PE_ACT_CONFIRM_WORLD_MAP, 1, 0x3109913Cu, 0x5E670A9Eu, "INGAME_ICON_WORLD_MAP", "Return to World Map", 0 };
static const Proto P_RESUME_RACE  = { PE_RESUME_RACE, PE_ACT_RESUME_RACE, 1, 0x12BB5EA2u, 0xF6601088u, "INGAME_ICON_RESUME_RACE", "Resume Race", 0 };
static const Proto P_RESTART      = { PE_RESTART, PE_ACT_CONFIRM_RESTART, 1, 0xB295A6B6u, 0x80833FC2u, "INGAME_ICON_RESTART_RACE", "Restart", 0 };
static const Proto P_EXPLORE_RACE = { PE_EXPLORE_RACE, PE_ACT_CONFIRM_EXPLORE, 1, ICON_EXPLORE, 0xDB4E3C0Du, "INGAME_ICON_EXPLORE", "Return to Explore Mode", 0 };

void pe_context_default(PauseContext *c) {
    memset(c, 0, sizeof *c);
    c->mode = PE_MODE_CAREER;
    c->options_ok = 1;
}

static int add(PauseEntry *out, int n, const Proto *p, const PauseContext *c) {
    if (n >= PE_MAX) return n;
    PauseEntry *e = &out[n];
    e->id = p->id;
    e->action = p->action;
    e->immediate = p->immediate;
    e->icon_key = p->icon_key;
    e->label_key = p->label_key;
    e->icon_name = p->icon_name;
    e->label = p->label;
    e->needs_replay = p->needs_replay;
    e->disabled = p->needs_replay && !c->replay_available;     /* 2.3: replay entry needs replay data */
    e->dialog = 0;                                             /* retail: default two-button dialog */
    return n + 1;
}

static const Proto *quit_proto(const PauseContext *c) {        /* A.5 / B.7 */
    if (c->mode == PE_MODE_ONLINE) return &P_QUIT_ONLINE;
    if (c->mode == PE_MODE_LAN) return &P_QUIT_LAN;
    return &P_QUIT_MAIN;
}
static int mode_is_online(uint32_t m) { return m == PE_MODE_ONLINE || m == PE_MODE_LAN; }

int pe_build(const PauseContext *c, PauseEntry out[PE_MAX]) {
    int n = 0;
    if (!c || !out) return 0;
    int career = c->mode == PE_MODE_CAREER;
    int replay_mode = c->mode != PE_MODE_TWO_PLAYER && !mode_is_online(c->mode);
    if (c->race == PE_RACE_NONE) {                              /* A: explore / free roam */
        n = add(out, n, career ? &P_EXPLORE_FREE : &P_FREE_RUN, c);
        if (career && c->garage_flag) n = add(out, n, &P_GARAGE, c);
        if (c->options_ok) n = add(out, n, &P_OPTIONS, c);
        if (replay_mode) n = add(out, n, &P_REPLAY, c);
        n = add(out, n, quit_proto(c), c);
        return n;
    }
    if (c->session > 2)                                         /* B: connected session, quit only */
        return add(out, 0, quit_proto(c), c);
    if (career && c->from_map) n = add(out, n, &P_WORLD_MAP, c);
    n = add(out, n, &P_RESUME_RACE, c);
    if (c->race != PE_RACE_SPECIAL && !mode_is_online(c->mode)) n = add(out, n, &P_RESTART, c);
    if (career && c->race != PE_RACE_SPECIAL) n = add(out, n, &P_EXPLORE_RACE, c);
    if (c->options_ok) n = add(out, n, &P_OPTIONS, c);
    if (replay_mode) n = add(out, n, &P_REPLAY, c);
    n = add(out, n, quit_proto(c), c);
    return n;
}

const char *pe_id_name(PeId id) {
    switch (id) {
    case PE_EXPLORE_FREE: return "explore_free";
    case PE_FREE_RUN: return "free_run";
    case PE_GARAGE: return "garage";
    case PE_OPTIONS: return "options";
    case PE_REPLAY: return "replay";
    case PE_QUIT_MAIN: return "quit_main";
    case PE_QUIT_ONLINE: return "quit_online";
    case PE_QUIT_LAN: return "quit_lan";
    case PE_WORLD_MAP: return "world_map";
    case PE_RESUME_RACE: return "resume_race";
    case PE_RESTART: return "restart";
    case PE_EXPLORE_RACE: return "explore_race";
    }
    return "?";
}
const char *pe_action_name(PeAction a) {
    switch (a) {
    case PE_ACT_RESUME_EXPLORE: return "resume_explore";
    case PE_ACT_RESUME_RACE: return "resume_race";
    case PE_ACT_OPEN_OPTIONS: return "open:UI_PauseOptionsMain.fng";
    case PE_ACT_OPEN_REPLAY: return "open:ui_ReplayControl.fng";
    case PE_ACT_CONFIRM_QUIT: return "confirm:quit";
    case PE_ACT_CONFIRM_RESTART: return "confirm:restart";
    case PE_ACT_CONFIRM_EXPLORE: return "confirm:explore";
    case PE_ACT_CONFIRM_WORLD_MAP: return "confirm:world_map";
    case PE_ACT_CONFIRM_GARAGE: return "confirm:garage";
    }
    return "?";
}
