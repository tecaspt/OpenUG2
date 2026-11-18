#include "postrace.h"
#include "race.h"
#include "rhi_gl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
static void save(const char *path, int w, int h) {
    unsigned char *px = malloc((size_t)w * h * 3);
    if (rhi_read_pixels(0, 0, w, h, RHI_READ_RGB8, px, 0) != RHI_OK) { printf("read failed\n"); free(px); return; }
    FILE *f = fopen(path, "wb");
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    png_infop i = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, i, w, h, 8, PNG_COLOR_TYPE_RGB, 0, 0, 0);
    png_write_info(p, i);
    for (int y = 0; y < h; y++) png_write_row(p, px + (size_t)y * w * 3);
    png_write_end(p, i);
    png_destroy_write_struct(&p, &i);
    fclose(f); free(px);
    printf("wrote %s\n", path);
}

static void key(PostRace *p,SDL_Keycode key) {
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;pr_event(p,&e,NULL);
    pr_update(p,0);
}
int main(int argc,char **argv) {
    assert(argc==3);assert(SDL_Init(SDL_INIT_VIDEO)==0);
    rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *win=SDL_CreateWindow("Post-race check",0,0,960,720,rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN);
    assert(win);RhiDeviceDesc dev={RHI_BACKEND_DEFAULT,0,win};assert(rhi_device_create(&dev)==RHI_OK);
    PostRace *p=pr_create(argv[1]);assert(p);
    int checks=0;
    for(int scenario=0;scenario<3;scenario++) {
        PostRaceInfo info={.kind=scenario==2?N2_RACE_URL:N2_RACE_DRIFT,.career=scenario==1,
            .event_id=4174,.finish_place=1,.seconds=62.34,.have_drift_points=scenario!=2,.drift_points=12345,
            .have_drift_chains=1,.drift_banked=12,.drift_cancelled=2,.reward={.available=1,.cash=1000,.reputation=100},
            .have_url_standings=scenario==2,.have_round_standings=scenario==2,.standing_count=2,.url_round=1,.url_rounds=3};
        strcpy(info.car,"MIATA");
        info.standing[0]=(PostRaceStanding){.name="Player",.car="MIATA",.place=2,.points=8};
        info.standing[1]=(PostRaceStanding){.name="Racer 1",.car="GOLF",.place=1,.points=10};
        info.round_standing[0]=(PostRaceStanding){.name="Player",.car="MIATA",.place=1,.seconds=62.34,.points=10};
        info.round_standing[1]=(PostRaceStanding){.name="Racer 1",.car="GOLF",.place=2,.seconds=64.56,.points=8};
        int screens[7],count=pr_flow(&info,screens);pr_open(p,&info);
        for(int at=0;at<count;at++) {
            assert(pr_screen(p)==screens[at]);checks++;
            for(int f=0;f<60;f++) {
                assert(pr_update(p,1.0f/30)==0);
                rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);
                pr_draw(p,960,720);
                if(f==59){char out[768];snprintf(out,sizeof out,"%s/%s-%d.png",argv[2],scenario==0?"quick":scenario==1?"career":"tournament",screens[at]);save(out,960,720);}
                rhi_frame_end();assert(!pr_status(p)[0]);
            }
            if(screens[at]==PR_SCREEN_REWARD) {
                assert(!strcmp(pr_bound_text(p,0x18d97a23u),"100"));
                assert(!strcmp(pr_bound_text(p,0x18d97a24u),"1000"));checks+=2;
            }
            if(screens[at]==PR_SCREEN_STATS) {
                assert(!strcmp(pr_bound_text(p,0x9b483b01u),"1:02.34"));
                assert(!strcmp(pr_bound_text(p,0x9b483b02u),""));checks+=2;
            }
            if(screens[at]==PR_SCREEN_TOURN_RESULTS) {
                assert(!strcmp(pr_bound_text(p,0xa5cc56b5u),"Player"));
                assert(!strcmp(pr_bound_text(p,0xa3ea9c37u),"1:02.34"));checks+=2;
            }
            if(screens[at]==PR_SCREEN_TOURN_STANDINGS) {
                assert(!strcmp(pr_bound_text(p,0xa5cc56b5u),"Racer 1"));
                assert(!strcmp(pr_bound_text(p,0xf2028e7eu),"10"));checks+=2;
            }
            if(screens[at]==PR_SCREEN_WORLD_MAP) {
                key(p,SDLK_ESCAPE);assert(pr_screen(p)==PR_SCREEN_STATS);checks++;
                key(p,SDLK_RETURN);assert(pr_screen(p)==PR_SCREEN_WORLD_MAP && !pr_status(p)[0]);checks++;
            }
            if(at<count-1)key(p,SDLK_RETURN);
        }
        key(p,SDLK_RIGHT);key(p,SDLK_RETURN);assert(!pr_is_open(p));checks++;
    }
    pr_destroy(p);rhi_device_destroy();SDL_DestroyWindow(win);SDL_Quit();
    printf("postrace_check: %d checks PASS\n",checks);return 0;
}
