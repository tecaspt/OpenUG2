/* game_flow.h -- frontend-to-race launch state machine (PLAN-M1 U3). Pure logic: no SDL, no GL. The host
 * (main.c) fills a GfHost snapshot each frame, calls gf_step() and executes the single returned command. */
#ifndef OPENUG2_GAME_FLOW_H
#define OPENUG2_GAME_FLOW_H

#include "frontend/frontend.h"
#include "pause_entries.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfLaunch { int free_run, event_id; char track[16], car[32]; int laps, difficulty; } GfLaunch;

typedef struct GfHost {
    const char *track, *car;   /* currently loaded track / car (may be NULL) */
    int race_state, race_active, race_event_id, race_failed, finish_place, cars;
    double race_seconds;
    const char *race_error;    /* non-empty when the last arm was rejected */
} GfHost;

typedef enum {
    GF_CMD_NONE = 0, GF_CMD_SHOW_LOADING, GF_CMD_SWITCH_TRACK, GF_CMD_SWITCH_CAR, GF_CMD_ARM_EVENT,
    GF_CMD_STOP_RACE, GF_CMD_CLOSE_MENU, GF_CMD_OPEN_MAIN, GF_CMD_OPEN_RESULTS, GF_CMD_ERROR
} GfCmdKind;

typedef struct GfCommand { GfCmdKind kind; char name[32]; int event_id, laps, difficulty; char message[96]; } GfCommand;

/* state values (GameFlow.state) */
enum {
    GF_IDLE = 0, GF_LOADING, GF_TRACK, GF_CAR, GF_FREE_STOP, GF_ARM, GF_ARM_CHECK, GF_CLOSE, GF_WAIT_RACE,
    GF_RUNNING, GF_RESULTS, GF_QUIT_MAIN, GF_QUIT_STOP, GF_QR_TRACK
};

typedef struct GameFlow { int enabled, state; GfLaunch launch, last; float timer; int frames; } GameFlow;

void gf_init(GameFlow *g, int enabled);
void gf_start(GameFlow *g, const GfLaunch *l);
/* One command per call (GF_CMD_NONE when nothing to do). */
GfCommand gf_step(GameFlow *g, const GfHost *h, float dt);
/* Non-career pause: mode 0; free roam shows "Return to Free Run". */
void gf_pause_context(const GameFlow *g, int race_active, PauseContext *c);
/* 1 when handled (QUIT, RESTART, RESUME); 0 otherwise and always while disabled. QUIT queues OPEN_MAIN then
 * STOP_RACE, RESTART queues ARM_EVENT of the last launch; both are delivered by following gf_step() calls. */
int gf_pause_action(GameFlow *g, FeAction a);

#ifdef __cplusplus
}
#endif
#endif
