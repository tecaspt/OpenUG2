/* Retail sponsor art, touch routing and credited post-race bonus. */
#include "fe_menu.h"
#include "postrace.h"
#include "race.h"
#include "rhi_gl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
static int checks;
#define CK(x) do {checks++; assert(x);} while(0)
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

static void draw(FeMenu *m,const char *root,const char *name) {
    for(int f=0;f<60;f++) {
        fem_update(m,1.f/30);rhi_frame_begin(NULL);fem_draw(m,960,720);
        if(f==59) {char path[768];snprintf(path,sizeof path,"%s/%s.png",root,name);save(path,960,720);}
        rhi_frame_end();CK(fem_visual_available(m));
    }
}
static FemRequest tap(FeMenu *m,SDL_Window *win,int x,int y) {
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.which=SDL_TOUCH_MOUSEID;e.button.x=x;e.button.y=y;
    fem_event(m,&e,win);e.type=SDL_MOUSEBUTTONUP;fem_event(m,&e,win);return fem_update(m,.04f);
}
int main(int argc,char **argv) {
    CK(argc==3);CK(SDL_Init(SDL_INIT_VIDEO)==0);
    rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *win=SDL_CreateWindow("Sponsor check",0,0,960,720,rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN);
    CK(win);RhiDeviceDesc dev={RHI_BACKEND_DEFAULT,0,win};CK(rhi_device_create(&dev)==RHI_OK);
    static CareerData cd;CareerState state;CK(cd_load(&cd,argv[1]));
    cr_new(&state,&cd,"GOLF");state.stage=2;state.bank=1250;
    FeMenu *menu=fem_create(argv[1],NULL,0);CK(menu);
    fem_open_sponsor(menu,&cd,&state);draw(menu,argv[2],"render-popup");
    CK(tap(menu,win,773,451).kind==FEM_REQ_NONE);draw(menu,argv[2],"render-choice");
    CK(tap(menu,win,780,360).kind==FEM_REQ_NONE); /* locked Eurolite */
    CK(tap(menu,win,480,360).kind==FEM_REQ_NONE);
    CK(tap(menu,win,180,360).kind==FEM_REQ_NONE); /* Streetglow */
    FemRequest req=tap(menu,win,480,360);
    CK(req.kind==FEM_REQ_SPONSOR_SIGN && req.sponsor_index==0);
    CK(cr_sponsor_choose(&state,&cd,req.sponsor_index));
    fem_sponsor_signed(menu,state.bank);draw(menu,argv[2],"render-signed");
    CK(state.bank==2525);CK(tap(menu,win,480,360).kind==FEM_REQ_CAREER_ROAM);
    fem_destroy(menu);
    PostRace *post=pr_create(argv[1]);CK(post);
    PostRaceInfo info={.kind=N2_RACE_CIRCUIT,.career=1,.finish_place=1,.seconds=60,
        .reward={.available=1,.cash=1900,.reputation=100}};strcpy(info.car,"GOLF");
    pr_open(post,&info);pr_sponsor_reward(post,900);
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RETURN;
    pr_event(post,&e,win);pr_update(post,0);CK(pr_screen(post)==PR_SCREEN_REWARD);
    for(int f=0;f<60;f++) {
        pr_update(post,1.f/30);rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);pr_draw(post,960,720);
        if(f==59){char path[768];snprintf(path,sizeof path,"%s/render-url-bonus.png",argv[2]);save(path,960,720);}
        rhi_frame_end();CK(!pr_status(post)[0]);
    }
    CK(!strcmp(pr_bound_text(post,0x18d97a24u),"1900"));
    CK(!strcmp(pr_bound_text(post,0xeb406fecu),"Sponsor +$900"));
    pr_destroy(post);rhi_device_destroy();SDL_DestroyWindow(win);SDL_Quit();
    printf("sponsor_render_check: %d checks PASS (240 rendered frames)\n",checks);return 0;
}
