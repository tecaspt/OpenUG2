#include "postrace.h"
#include "race.h"
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
    int checked=9;
    for(int kind=N2_RACE_CIRCUIT;kind<=N2_RACE_URL;kind++)for(int career=0;career<2;career++) {
        info=(PostRaceInfo){.kind=kind,.career=career,.have_url_standings=kind==N2_RACE_URL,
            .reward={.available=1,.cash=1234,.reputation=67},.seconds=72.12,.drift_points=4567};
        int flow[7],n=pr_flow(&info,flow);
        assert(n==3+2*career+(kind==N2_RACE_URL));checked++;
        pr_open(p,&info);info.reward.cash=9999;
        assert(pr_info(p)->reward.cash==1234 && pr_info(p)->seconds==72.12 && pr_info(p)->drift_points==4567);checked++;
        for(int i=0;i<n;i++) {
            assert(pr_screen(p)==flow[i]);checked++;
            if(i==n-1)break;
            if(i%3==0){e=(SDL_Event){0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RETURN;pr_event(p,&e,NULL);}
            else if(i%3==1){e=(SDL_Event){0};e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=SDL_CONTROLLER_BUTTON_A;pr_event(p,&e,NULL);}
            else {e=(SDL_Event){0};e.type=SDL_FINGERDOWN;e.tfinger.x=.8f;e.tfinger.y=.9f;pr_event(p,&e,NULL);e.type=SDL_FINGERUP;pr_event(p,&e,NULL);}
            assert(pr_update(p,0)==PR_ACT_NONE);checked++;
            if(i==0) {e=(SDL_Event){0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;pr_event(p,&e,NULL);
                assert(pr_update(p,0)==0 && pr_screen(p)==flow[0]);checked++;
                e.key.keysym.sym=SDLK_RETURN;pr_event(p,&e,NULL);assert(pr_update(p,0)==0);checked++;}
        }
        e=(SDL_Event){0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RIGHT;pr_event(p,&e,NULL);
        e.key.keysym.sym=SDLK_RETURN;pr_event(p,&e,NULL);assert(pr_update(p,0)==PR_ACT_CONTINUE);checked++;
    }
    pr_destroy(p);assert(!pr_is_open(NULL));assert(pr_update(NULL,0)==0);
    printf("postrace_test: %d checks PASS\n",checked);return 0;
}
