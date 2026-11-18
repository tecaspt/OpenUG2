/* pause_entries.h -- entry catalogue of the pause menu per context (SPEC_GUI_ART 2.2-2.4, tvg07).
 *
 * Pure data and rules, no GL/SDL. The host describes the situation with a PauseContext; pe_build()
 * returns the entries in list order (entry k is bound to the package object option_k). Icon texture
 * keys and label keys are data lookup keys of the retail packages/language file. */
#ifndef OPENUG2_PAUSE_ENTRIES_H
#define OPENUG2_PAUSE_ENTRIES_H

#include <stdint.h>

#include "gui_dialog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PE_MAX 10                 /* option_1 ... option_10 */
#define PE_MODE_CAREER 1u         /* game-mode values of SPEC_GUI_ART 2.2 */
#define PE_MODE_TWO_PLAYER 4u
#define PE_MODE_ONLINE 8u
#define PE_MODE_LAN 0x200u
#define PE_RACE_NONE 0            /* race-activity value: free roam / explore */
#define PE_RACE_SPECIAL 7         /* special race state (meaning unknown, 2.2) */

typedef struct PauseContext {
    uint32_t mode;                /* game mode */
    int race;                     /* race activity: 0 none, 7 special, other = race in progress */
    int from_map;                 /* race launched from the career world map */
    int session;                  /* online session state; > 2 = connected */
    int options_ok;               /* the Options availability condition (2.3) */
    int garage_flag;              /* never set in the retail PC game (2.3) */
    int substate4;                /* racing sub-state 4 (selects the "lost progress" texts, 2.4) */
    int replay_available;         /* replay data exists: the Replay entry is focusable */
    int online_flag;              /* the extra online flag of the quit confirmation texts (2.4) */
} PauseContext;

/* Retail career free-roam pause: mode 1, no race, Options available, no replay data. */
void pe_context_default(PauseContext *c);

typedef enum {
    PE_EXPLORE_FREE, PE_FREE_RUN, PE_GARAGE, PE_OPTIONS, PE_REPLAY,
    PE_QUIT_MAIN, PE_QUIT_ONLINE, PE_QUIT_LAN,
    PE_WORLD_MAP, PE_RESUME_RACE, PE_RESTART, PE_EXPLORE_RACE
} PeId;

typedef enum {
    PE_ACT_RESUME_EXPLORE,        /* resume gameplay after the exit (free-roam resume request) */
    PE_ACT_RESUME_RACE,           /* resume gameplay at once; the pause package is removed */
    PE_ACT_OPEN_OPTIONS,          /* after exit: switch to UI_PauseOptionsMain.fng */
    PE_ACT_OPEN_REPLAY,           /* after exit: switch to ui_ReplayControl.fng */
    PE_ACT_CONFIRM_QUIT, PE_ACT_CONFIRM_RESTART, PE_ACT_CONFIRM_EXPLORE,
    PE_ACT_CONFIRM_WORLD_MAP, PE_ACT_CONFIRM_GARAGE
} PeAction;

typedef struct PauseEntry {
    PeId id;
    PeAction action;
    int immediate;                /* 1: runs at once on activation; 0: after the 320 ms exit */
    uint32_t icon_key;            /* texture key of the entry icon */
    uint32_t label_key;           /* language label key of the caption */
    const char *icon_name;
    const char *label;            /* English caption (language-file text for that key) */
    int needs_replay;             /* disabled unless replay data is available */
    int disabled;                 /* evaluated: needs_replay && !replay_available */
    GdKind dialog;                /* confirmation dialog kind (TASK-AL); 0 = the default
                                   * two-button dialog which the action would open */
} PauseEntry;

/* Builds the list; returns the number of entries (0..PE_MAX). */
int pe_build(const PauseContext *c, PauseEntry out[PE_MAX]);

/* Vector vocabulary (tvg07): ids "explore_free", ... and actions "resume_explore", ... */
const char *pe_id_name(PeId id);
const char *pe_action_name(PeAction a);

#ifdef __cplusplus
}
#endif
#endif
