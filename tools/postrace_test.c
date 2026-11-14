#include "postrace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    PostRace *p=pr_create("/nonexistent-openug2-synthetic-data");assert(p);
    PostRaceInfo info={0};SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;
    pr_open(p,&info);assert(pr_is_open(p));assert(pr_status(p)[0]);
    e.key.keysym.sym=SDLK_r;pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_RESTART);assert(!pr_is_open(p));
    assert(pr_update(p,1)==0);pr_open(p,&info);e.key.keysym.sym=SDLK_ESCAPE;
    pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_QUIT_MAIN);
    pr_destroy(p);assert(!pr_is_open(NULL));assert(pr_update(NULL,0)==0);
    puts("postrace_test: 9 checks PASS");return 0;
}
