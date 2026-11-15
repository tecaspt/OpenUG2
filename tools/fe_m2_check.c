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
    int back_pixels=0;
    for(int y=660;y<708;y++)for(int x=36;x<255;x++) {
        size_t at=((size_t)y*w+x)*3;
        if(px[at]>80 || px[at+1]>80 || px[at+2]>80)back_pixels++;
    }
    fprintf(stderr,"visible Back %s: %d pixels\n",path,back_pixels);
    assert(back_pixels>100);
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
int main(int argc, char **argv) {
    if(argc!=3){fprintf(stderr,"usage: fe_m2_check DATA OUTPUT_DIR\n");return 2;}
    dir = argv[2];
    SDL_Init(SDL_INIT_VIDEO);
    rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *win = SDL_CreateWindow("fe smoke", 0, 0, W, H, rhi_window_flags(RHI_BACKEND_DEFAULT) | SDL_WINDOW_HIDDEN);
    RhiDeviceDesc dev = { RHI_BACKEND_DEFAULT, 0, win };
    if (rhi_device_create(&dev) != RHI_OK) { printf("rhi fail\n"); return 1; }
    static GameCatalog cat;
    char storage[1024],config[1100],profiles[1100];
    snprintf(storage,sizeof storage,"%s/state.XXXXXX",dir);assert(mkdtemp(storage));
    snprintf(config,sizeof config,"%s/settings.ini",storage);snprintf(profiles,sizeof profiles,"%s/profiles",storage);
    /* Resolve touch key centres from the installed layout, not guessed screen coordinates. */
    UgAssets assets;char error[256];GvScreen kb={0};
    assert(ug_assets_open(&assets,argv[1],NULL,error,sizeof error));
    assert(gv_screen_load(&kb,&assets,"UI_VirtualKeyboard.fng"));gv_screen_update(&kb,2000);
    float ax=0,ay=0,dx=0,dy=0;
    for(int i=0;i<kb.scene.n;i++){UgNode *n=&kb.scene.nodes[i];if(n->obj->guid==0x10c1a){ax=(n->we+320)*W/640;ay=(n->wf+240)*H/480;}if(n->obj->guid==0x10c82){dx=(n->we+320)*W/640;dy=(n->wf+240)*H/480;}}
    assert(ax && dx);gv_screen_free(&kb);ug_assets_close(&assets);
    FeMenu *m=fem_create(argv[1],&cat,1);assert(m);assert(fem_set_storage(m,config,profiles)==ST_OK);
    fem_open(m,FEM_SCR_MAIN);frames(m,30,"m2-main.png");
    StRegStore external;assert(st_reg_open_ini(&external,config)==ST_OK);
    assert(st_reg_set_string(&external,"PauseOwnedKey","retained")==ST_OK);st_reg_close(&external);
    key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,40,"options-hub.png");
    assert(fem_screen(m)==FEM_SCR_OPTIONS && fem_options_page(m)==OM_SCREEN_HUB);
    key(m,SDLK_RETURN);frames(m,40,"audio.png");assert(fem_options_page(m)==OM_SCREEN_AUDIO);
    float volume=fem_current_profile(m)->audio.master;key(m,SDLK_LEFT);frames(m,1,NULL);assert(fem_current_profile(m)->audio.master<volume);assert(fem_take_apply(m)&FEM_APPLY_PROFILE);
    key(m,SDLK_ESCAPE);frames(m,40,NULL);key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,40,"display.png");assert(fem_options_page(m)==OM_SCREEN_DISPLAY);
    key(m,SDLK_RIGHT);frames(m,1,NULL);assert(fem_take_apply(m)&FEM_APPLY_GRAPHICS);
    key(m,SDLK_DOWN);key(m,SDLK_DOWN);key(m,SDLK_RETURN);frames(m,40,"customize.png");assert(fem_options_page(m)==OM_SCREEN_CUSTOMIZE);
    key(m,SDLK_ESCAPE);frames(m,40,NULL);key(m,SDLK_DOWN);key(m,SDLK_DOWN);key(m,SDLK_DOWN);key(m,SDLK_RETURN);frames(m,10,"defaults-dialog.png");
    key(m,SDLK_ESCAPE);frames(m,20,NULL);
    key(m,SDLK_ESCAPE);frames(m,40,NULL);key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,40,"gameplay.png");assert(fem_options_page(m)==OM_SCREEN_GAMEPLAY);
    key(m,SDLK_ESCAPE);frames(m,40,NULL);key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,40,"player.png");assert(fem_options_page(m)==OM_SCREEN_PLAYER);
    key(m,SDLK_RIGHT);frames(m,1,NULL);assert(fem_take_apply(m)&FEM_APPLY_PROFILE);
    /* Touch Back through the leaf and hub to Main. */
    click(m,win,145,680);frames(m,40,NULL);click(m,win,145,680);frames(m,40,NULL);assert(fem_screen(m)==FEM_SCR_MAIN);
    key(m,SDLK_LEFT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,40,"profile-manager.png");assert(fem_screen(m)==FEM_SCR_PROFILE);
    key(m,SDLK_RETURN);frames(m,40,"profile-keyboard.png");
    click(m,win,(int)ax,(int)ay);frames(m,1,NULL);click(m,win,(int)dx,(int)dy);frames(m,40,"profile-created.png");
    assert(!strcmp(fem_current_profile(m)->name,"A"));
    key(m,SDLK_RIGHT);frames(m,20,NULL);key(m,SDLK_RETURN);frames(m,30,NULL);key(m,SDLK_BACKSPACE);
    SDL_Event input={0};input.type=SDL_TEXTINPUT;snprintf(input.text.text,sizeof input.text.text,"Beta");fem_event(m,&input,win);key(m,SDLK_RETURN);frames(m,40,"profile-renamed.png");assert(!strcmp(fem_current_profile(m)->name,"Beta"));
    key(m,SDLK_RIGHT);frames(m,15,NULL);key(m,SDLK_RIGHT);frames(m,15,NULL);key(m,SDLK_RETURN);frames(m,30,NULL);
    snprintf(input.text.text,sizeof input.text.text,"OPENUG2");fem_event(m,&input,win);key(m,SDLK_RETURN);frames(m,40,NULL);assert(!strcmp(fem_current_profile(m)->name,"OPENUG2"));
    assert(fem_current_profile(m)->audio.master<volume);
    key(m,SDLK_RIGHT);frames(m,15,NULL);key(m,SDLK_RIGHT);frames(m,15,NULL);key(m,SDLK_RIGHT);frames(m,15,NULL);key(m,SDLK_RETURN);frames(m,30,NULL);
    snprintf(input.text.text,sizeof input.text.text,"Beta");fem_event(m,&input,win);key(m,SDLK_RETURN);frames(m,10,"profile-delete-confirm.png");key(m,SDLK_RETURN);frames(m,40,NULL);assert(st_profile_status(profiles,"Beta")==ST_E_NOTFOUND);
    assert(st_reg_open_ini(&external,config)==ST_OK);char retained[32];
    assert(st_reg_get_string(&external,"PauseOwnedKey",retained,sizeof retained) && !strcmp(retained,"retained"));st_reg_close(&external);
    StProfile *saved=malloc(sizeof *saved);assert(saved);assert(st_profile_load(profiles,"OPENUG2",saved)==ST_OK);assert(saved->audio.master<volume);free(saved);
    printf("fe_m2_check: Main/options/profile/touch keyboard/persistence PASS\n");
    /* The Profile screen and keyboard both retain a tap-reachable Back. */
    key(m,SDLK_RETURN);frames(m,30,NULL);click(m,win,145,680);frames(m,30,NULL);
    click(m,win,145,680);frames(m,40,NULL);assert(fem_screen(m)==FEM_SCR_MAIN);
    fem_destroy(m);
    /* Missing retail packages must never permit an invisible profile mutation. */
    m=fem_create("/missing-openug2-retail",&cat,0);assert(m);assert(fem_set_storage(m,config,profiles)==ST_OK);
    fem_open(m,FEM_SCR_OPTIONS);assert(!fem_visual_available(m) && fem_status(m)[0]);
    volume=fem_current_profile(m)->audio.master;key(m,SDLK_RETURN);key(m,SDLK_LEFT);fem_update(m,1);
    assert(fem_current_profile(m)->audio.master==volume);key(m,SDLK_ESCAPE);assert(fem_screen(m)==FEM_SCR_MAIN);
    fem_open(m,FEM_SCR_PROFILE);key(m,SDLK_RETURN);fem_update(m,1);assert(!strcmp(fem_current_profile(m)->name,"OPENUG2"));
    key(m,SDLK_ESCAPE);assert(fem_screen(m)==FEM_SCR_MAIN);fem_destroy(m);
    puts("fe_m2_check: keyboard/manager touch Back and missing-art fail-closed PASS");
    rhi_device_destroy();
    return 0;
}
