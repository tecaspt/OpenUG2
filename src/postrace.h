#ifndef OPENUG2_POSTRACE_H
#define OPENUG2_POSTRACE_H
#include <SDL.h>
#include <stdint.h>
#include "race_rules.h"
typedef struct PostRace PostRace;
typedef struct PostRaceStanding {
    char name[32],car[32];uint32_t points;int place;
    double seconds;int status; /* field results: status PR_STAND_FINISHED / _RUNNING / _OUT */
} PostRaceStanding;
typedef struct PostRaceInfo {
    int event_id, kind, finish_place, failed;
    double seconds;
    char car[32];
    uint32_t drift_points;int have_drift_points;
    int have_url_standings,standing_count,url_round,url_rounds;
    int have_standings; /* the whole field, ordered by place (Circuit/Sprint/Drag/Lap KO) */
    PostRaceStanding standing[RR_MAX_CARS];
    int career, stage_before, stage_after;
    float player_x,player_y,heading;
    double top_speed_kmh,average_speed_kmh,best_lap;
    int have_top_speed,have_average_speed,have_best_lap;
    unsigned drift_banked,drift_cancelled;int have_drift_chains;
    PostRaceStanding round_standing[RR_MAX_CARS];
    int have_round_standings;
    RrReward reward; /* optional supplied award; available=0 leaves existing title */
} PostRaceInfo;
enum { PR_STAND_FINISHED=0, PR_STAND_RUNNING, PR_STAND_OUT };
enum { PR_ACT_NONE=0, PR_ACT_RESTART, PR_ACT_CONTINUE, PR_ACT_QUIT_MAIN };
enum { PR_SCREEN_RESULTS, PR_SCREEN_TOURN_RESULTS, PR_SCREEN_TOURN_STANDINGS,
       PR_SCREEN_REWARD, PR_SCREEN_STATS, PR_SCREEN_WORLD_MAP, PR_SCREEN_MENU };
/* Pure flow and copied presentation state, also available without a renderer. */
int pr_flow(const PostRaceInfo *,int screens[7]);
int pr_screen(const PostRace *);
const PostRaceInfo *pr_info(const PostRace *);
const char *pr_bound_text(const PostRace *,uint32_t name_hash);
PostRace *pr_create(const char *dataroot);
void pr_open(PostRace *,const PostRaceInfo *);
void pr_event(PostRace *,const SDL_Event *,SDL_Window *);
int pr_update(PostRace *,float seconds);
void pr_draw(PostRace *,int width,int height);
int pr_is_open(const PostRace *);
const char *pr_status(const PostRace *);
void pr_destroy(PostRace *);
#endif
