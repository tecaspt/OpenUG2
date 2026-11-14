#include "rhi_gl.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fe_menu.h"
#include "postrace.h"
#include <assert.h>
#include "game_catalog.h"

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
static void key(FeMenu *m, SDL_Keycode k) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    fem_event(m, &e, NULL);
}
static void click(FeMenu *m, SDL_Window *win, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y; e.button.which = SDL_TOUCH_MOUSEID;
    fem_event(m, &e, win);
    e.type = SDL_MOUSEBUTTONUP;
    fem_event(m, &e, win);
}
static const char *dir;
static int W = 960, H = 720;
static FemRequest frames(FeMenu *m, int n, const char *png) {
    FemRequest r = {0};
    for (int k = 0; k < n; k++) {
        FemRequest q = fem_update(m, 1.0f / 30);
        if (q.kind) r = q;
        rhi_frame_begin(NULL);
        static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);
        fem_draw(m, W, H);
        if (png && k == n - 1) { char p[600]; snprintf(p, sizeof p, "%s/%s", dir, png); save(p, W, H); }
        rhi_frame_end();
    }
    return r;
}
static void report(FeMenu *m, const char *tag) {
    printf("[%s] open=%d screen=%d visual=%d status='%s'\n", tag, fem_is_open(m), fem_screen(m), fem_visual_available(m), fem_status(m));
}
int main(int argc, char **argv) {
    if(argc!=3){fprintf(stderr,"usage: fe_screens_check DATA OUTPUT_DIR\n");return 2;}
    dir = argv[2];
    SDL_Init(SDL_INIT_VIDEO);
    rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *win = SDL_CreateWindow("fe smoke", 0, 0, W, H, rhi_window_flags(RHI_BACKEND_DEFAULT) | SDL_WINDOW_HIDDEN);
    RhiDeviceDesc dev = { RHI_BACKEND_DEFAULT, 0, win };
    if (rhi_device_create(&dev) != RHI_OK) { printf("rhi fail\n"); return 1; }
    static GameCatalog cat;
    int have = 1;   /* synthetic catalogue (the real builder needs the whole world loader) */
    for (int k = 1; k <= 6; k++) { cat.ev[cat.nev].id = 4000 + k; cat.ev[cat.nev].kind = k; strcpy(cat.ev[cat.nev].region, "RB"); cat.nev++; }
    strcpy(cat.region[0].stem, "RB"); strcpy(cat.region[0].track, "STREAML4RB"); cat.nregion = 1;
    printf("catalogue: %s nev=%d\n", have ? "built" : "none", have ? cat.nev : 0);
    FeMenu *m = fem_create(argv[1], have ? &cat : NULL, 1);
    fem_set_current(m, "MIATA", "STREAML4RA");
    cat.ev[0].id=4000;cat.ev[0].name_key=ug_hash_upper("TRACK_NAME_4000");cat.ev[0].length_m=1300;
    strcpy(cat.car[0].name,"MIATA");strcpy(cat.car[1].name,"RX7");cat.ncar=2;
    fem_open(m,FEM_SCR_QR_TRACK);frames(m,60,"track.png");report(m,"track");assert(fem_visual_available(m));
    key(m,SDLK_RETURN);frames(m,60,"options.png");report(m,"options");
    key(m,SDLK_RIGHT);frames(m,1,NULL);key(m,SDLK_DOWN);frames(m,1,NULL);key(m,SDLK_RIGHT);frames(m,5,NULL);
    key(m,SDLK_RETURN);frames(m,60,"mode_options.png");key(m,SDLK_RETURN);frames(m,60,"car.png");report(m,"car");
    key(m,SDLK_RIGHT);frames(m,10,NULL);printf("preview=%s\n",fem_preview_car(m));assert(!strcmp(fem_preview_car(m),"RX7"));
    key(m,SDLK_RETURN);FemRequest r=frames(m,2,NULL);
    printf("request=%d event=%d car=%s track=%s laps=%d difficulty=%d\n",r.kind,r.event_id,r.car,r.track,r.laps,r.difficulty);
    assert(r.kind==FEM_REQ_QUICK_RACE && r.event_id==4000 && r.laps==3 && r.difficulty==1 && !strcmp(r.car,"RX7"));
    PostRace *post=pr_create(argv[1]);PostRaceInfo info={.event_id=4001,.finish_place=1,.seconds=62.34};strcpy(info.car,"MIATA");
    pr_open(post,&info);
    for(int n=0;n<60;n++){pr_update(post,1.0f/30);rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);pr_draw(post,W,H);if(n==59){char out[600];snprintf(out,sizeof out,"%s/results.png",dir);save(out,W,H);}rhi_frame_end();}
    printf("results open=%d status=%s\n",pr_is_open(post),pr_status(post));
    SDL_Event next={0};next.type=SDL_KEYDOWN;next.key.keysym.sym=SDLK_RETURN;pr_event(post,&next,NULL);
    for(int n=0;n<60;n++){pr_update(post,1.0f/30);rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);pr_draw(post,W,H);if(n==59){char out[600];snprintf(out,sizeof out,"%s/postmenu.png",dir);save(out,W,H);}rhi_frame_end();}
    printf("postmenu open=%d status=%s\n",pr_is_open(post),pr_status(post));assert(pr_is_open(post) && !pr_status(post)[0]);
    pr_event(post,&next,NULL);assert(pr_update(post,0)==PR_ACT_RESTART);pr_destroy(post);
    fem_open(m,FEM_SCR_QR_TRACK);click(m,win,145,680);frames(m,30,NULL);assert(fem_screen(m)==FEM_SCR_QR_MODE);
    printf("fe_screens_check: PASS\n");
    fem_destroy(m);
    rhi_device_destroy();
    return 0;
}
