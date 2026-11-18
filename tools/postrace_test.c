#include "postrace.h"
#include "race.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void navigate(PostRace *p,int method,int back) {
    SDL_Event e={0};
    if(method==0){e.type=SDL_KEYDOWN;e.key.keysym.sym=back?SDLK_ESCAPE:SDLK_RETURN;}
    else if(method==1){e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=back?SDL_CONTROLLER_BUTTON_B:SDL_CONTROLLER_BUTTON_A;}
    else {e.type=SDL_FINGERDOWN;e.tfinger.x=back?.15f:.8f;e.tfinger.y=back?.94f:.85f;
        pr_event(p,&e,NULL);e.type=SDL_FINGERUP;}
    pr_event(p,&e,NULL);
}
int main(void) {
    PostRace *p=pr_create("/nonexistent-openug2-synthetic-data");assert(p);
    PostRaceInfo info={0};SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;
    pr_open(p,&info);assert(pr_is_open(p));assert(pr_status(p)[0]);
    e.key.keysym.sym=SDLK_r;pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_RESTART);assert(!pr_is_open(p));
    assert(pr_update(p,1)==0);pr_open(p,&info);e.key.keysym.sym=SDLK_ESCAPE;
    pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_QUIT_MAIN);
    int checked=9;
    /* Independent expected sequences: changes to the flow builder must preserve
     * the screen contract, rather than merely agreeing with its own output. */
    static const int expected[2][2][6]={
        {{PR_SCREEN_RESULTS,PR_SCREEN_STATS,PR_SCREEN_MENU},
         {PR_SCREEN_TOURN_RESULTS,PR_SCREEN_TOURN_STANDINGS,PR_SCREEN_STATS,PR_SCREEN_MENU}},
        {{PR_SCREEN_RESULTS,PR_SCREEN_REWARD,PR_SCREEN_STATS,PR_SCREEN_WORLD_MAP,PR_SCREEN_MENU},
         {PR_SCREEN_TOURN_RESULTS,PR_SCREEN_TOURN_STANDINGS,PR_SCREEN_REWARD,PR_SCREEN_STATS,PR_SCREEN_WORLD_MAP,PR_SCREEN_MENU}}
    };
    for(int method=0;method<3;method++)for(int kind=N2_RACE_CIRCUIT;kind<=N2_RACE_URL;kind++)for(int career=0;career<2;career++) {
        info=(PostRaceInfo){.kind=kind,.career=career,.have_url_standings=kind==N2_RACE_URL,
            .reward={.available=1,.cash=1234,.reputation=67},.seconds=72.12,.drift_points=4567};
        int flow[7],n=pr_flow(&info,flow);
        assert(n==3+2*career+(kind==N2_RACE_URL));checked++;
        assert(!memcmp(flow,expected[career][kind==N2_RACE_URL],(size_t)n*sizeof flow[0]));checked++;
        pr_open(p,&info);info.reward.cash=9999;
        assert(pr_info(p)->reward.cash==1234 && pr_info(p)->seconds==72.12 && pr_info(p)->drift_points==4567);checked++;
        for(int i=0;i<n;i++) {
            assert(pr_screen(p)==flow[i]);checked++;
            if(i==n-1)break;
            navigate(p,method,0);assert(pr_update(p,0)==PR_ACT_NONE);checked++;
            if(i+1<n-1) {
                navigate(p,method,1);assert(pr_update(p,0)==0 && pr_screen(p)==flow[i]);checked++;
                navigate(p,method,0);assert(pr_update(p,0)==0 && pr_screen(p)==flow[i+1]);checked++;
            }
        }
        e=(SDL_Event){0};
        if(method==0){e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RIGHT;pr_event(p,&e,NULL);navigate(p,method,0);}
        else if(method==1){e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;pr_event(p,&e,NULL);navigate(p,method,0);}
        else {e.type=SDL_FINGERDOWN;e.tfinger.x=.9f;e.tfinger.y=.5f;pr_event(p,&e,NULL);e.type=SDL_FINGERUP;pr_event(p,&e,NULL);
            e.type=SDL_FINGERDOWN;e.tfinger.x=.5f;pr_event(p,&e,NULL);e.type=SDL_FINGERUP;pr_event(p,&e,NULL);}
        assert(pr_update(p,0)==PR_ACT_CONTINUE);checked++;
        pr_open(p,&info);navigate(p,method,1);assert(pr_update(p,0)==PR_ACT_QUIT_MAIN);checked++;
    }
    info=(PostRaceInfo){.kind=N2_RACE_DRAG};pr_open(p,&info);
    e=(SDL_Event){0};e.type=SDL_FINGERDOWN;e.tfinger.x=.15f;e.tfinger.y=.85f;pr_event(p,&e,NULL);
    e.type=SDL_FINGERUP;pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_RESTART);checked++;
    pr_destroy(p);assert(!pr_is_open(NULL));assert(pr_update(NULL,0)==0);
    printf("postrace_test: %d checks PASS\n",checked);return 0;
}
