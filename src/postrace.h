#ifndef OPENUG2_POSTRACE_H
#define OPENUG2_POSTRACE_H
#include <SDL.h>
typedef struct PostRace PostRace;
typedef struct PostRaceInfo {
    int event_id, kind, finish_place, failed;
    double seconds;
    char car[32];
} PostRaceInfo;
enum { PR_ACT_NONE=0, PR_ACT_RESTART, PR_ACT_CONTINUE, PR_ACT_QUIT_MAIN };
PostRace *pr_create(const char *dataroot);
void pr_open(PostRace *,const PostRaceInfo *);
void pr_event(PostRace *,const SDL_Event *,SDL_Window *);
int pr_update(PostRace *,float seconds);
void pr_draw(PostRace *,int width,int height);
int pr_is_open(const PostRace *);
const char *pr_status(const PostRace *);
void pr_destroy(PostRace *);
#endif
