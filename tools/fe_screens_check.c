#include "rhi_gl.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fe_menu.h"
#include "postrace.h"
#include <assert.h>
#include "game_catalog.h"
#include "career_data.h"
#include "pause_menu.h"
#include <math.h>

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
static void report(FeMenu *m, const char *tag) {
    printf("[%s] open=%d screen=%d visual=%d status='%s'\n", tag, fem_is_open(m), fem_screen(m), fem_visual_available(m), fem_status(m));
}
static void worldmap_checks(FeMenu *m, SDL_Window *win, const char *data) {
    CareerData *cd=calloc(1,sizeof *cd); assert(cd && cd_load(cd,data));
    FeMapIcon icons[4]={{0}};
    const char *shops[]={"UC_CAR_LOT","CRIB_1"};
    for(int i=0;i<2;i++) {
        int k=cd_find_shop(cd,cd_hash(shops[i])); assert(k>=0);
        int z=cd_find_zone(cd,cd->shop[k].zone_hash); assert(z>=0);
        icons[i].kind=FWM_ICON_SHOP;icons[i].id=cd->shop[k].hash;
        icons[i].label_key=i==0?ug_hash_upper("CAREER_CAR_LOT_TITLE"):0x7ce21662u;
        int marker=cd_find_marker(cd,cd->shop[k].hash);assert(marker>=0);
        icons[i].x=cd->marker[marker].x;icons[i].y=cd->marker[marker].y;
        printf("map control shop %s marker: %.6f %.6f\n",shops[i],icons[i].x,icons[i].y);
    }
    const char *const labels[]={"","QR_CIRCUIT","QR_SPRINT","QR_DRAG","QR_DRIFT","QR_STREET_CROSS","QR_URL"};
    int used=2;
    for(int i=0;i<cd->nrace && used<4;i++) {
        int z=cd_find_zone(cd,cd->race[i].locator_hash);if(z<0)continue;
        FeMapIcon *o=&icons[used++];o->kind=FWM_ICON_EVENT;o->id=cd->race[i].name_hash;
        o->label_key=cd->race[i].kind>0 && cd->race[i].kind<7?ug_hash_upper(labels[cd->race[i].kind]):0;
        /* This diagnostic deliberately chooses known zones, independent of current unlock stage. */
        for(int j=0;j<4;j++){o->x+=cd->zone[z].poly[j][0]*.25f;o->y+=cd->zone[z].poly[j][1]*.25f;}
        printf("map control event %s %s: %.6f %.6f\n",cd->race[i].name,cd->race[i].locator,o->x,o->y);
    }
    assert(used==4);
    for(int i=0;i<4;i++) {
        char png[48];snprintf(png,sizeof png,"worldmap-control%d.png",i);
        fem_open_world_map(m,icons,4,icons[i].x,icons[i].y,(float)i*1.57079632679f);
        frames(m,60,png);report(m,"worldmap");assert(fem_visual_available(m));
        const FeWorldMap *state=fem_world_map_state(m);assert(state && state->selected==i);
        float x,y;double wx,wy;
        assert(fwm_project(&state->projection,icons[i].x,icons[i].y,&x,&y));
        assert(fwm_unproject(&state->projection,x,y,&wx,&wy));
        assert(fabs(wx-icons[i].x)<.001 && fabs(wy-icons[i].y)<.001);
        printf("map projection control%d world %.6f %.6f -> virtual %.6f %.6f\n",i,icons[i].x,icons[i].y,x,y);
        key(m,SDLK_RETURN);FemRequest r=frames(m,2,NULL);
        assert(r.kind==FEM_REQ_SET_DESTINATION && r.destination.kind==icons[i].kind && r.destination.id==icons[i].id);
        assert(r.destination.x==icons[i].x && r.destination.y==icons[i].y);
        assert(frames(m,1,NULL).kind==FEM_REQ_NONE);
    }
    fem_open_world_map(m,icons,4,icons[0].x,icons[0].y,1);frames(m,30,NULL);
    const FeWorldMap *state=fem_world_map_state(m);float x,y;
    assert(fwm_project(&state->projection,icons[3].x,icons[3].y,&x,&y));
    click(m,win,(int)lroundf((x+320)*W/640),(int)lroundf((y+240)*H/480));
    assert(frames(m,1,NULL).kind==FEM_REQ_NONE);
    click(m,win,375,668); /* authored Select Event caption */
    FemRequest touch=frames(m,2,"worldmap-destination.png");
    assert(touch.kind==FEM_REQ_SET_DESTINATION && touch.destination.id==icons[3].id);
    fem_open_world_map(m,icons,4,icons[0].x,icons[0].y,0);frames(m,5,NULL);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=375;e.button.y=668;e.button.which=SDL_TOUCH_MOUSEID;fem_event(m,&e,win);
    SDL_Event lost={0};lost.type=SDL_WINDOWEVENT;lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;fem_event(m,&lost,win);
    e.type=SDL_MOUSEBUTTONUP;fem_event(m,&e,win);
    assert(frames(m,1,NULL).kind==FEM_REQ_NONE);

    fem_open_world_map(m,NULL,0,icons[0].x,icons[0].y,0);frames(m,5,NULL);key(m,SDLK_RETURN);
    assert(frames(m,1,NULL).kind==FEM_REQ_NONE);key(m,SDLK_ESCAPE);
    assert(frames(m,1,NULL).kind==FEM_REQ_CAREER_ROAM);
    fem_open_world_map(m,icons,4,icons[0].x,icons[0].y,0);frames(m,5,NULL);
    click(m,win,145,680);assert(frames(m,1,NULL).kind==FEM_REQ_CAREER_ROAM);
    free(cd);
}
static void pause_key(PauseMenu *p, SDL_Keycode k) {
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;pause_menu_event(p,&e,NULL);
}
static int pause_find(PauseMenu *p, PeId id) {
    const PauseScreen *ps=pause_menu_screen(p);
    for(int i=0;i<ps->n;i++)if(ps->entries[i].id==id)return i;
    return -1;
}
static void pause_worldmap_checks(const char *data) {
    PauseMenu *p=pause_menu_create(data);assert(p);PauseContext c;pe_context_default(&c);
    pause_menu_set_context(p,&c);pause_menu_open(p);assert(pause_find(p,PE_WORLD_MAP)<0);
    pause_menu_set_world_map_enabled(p,1);pause_menu_open(p);pause_menu_update(p,.05f);
    int at=pause_find(p,PE_WORLD_MAP);assert(at>=0);
    for(int i=0;i<PE_MAX && ps_selected(pause_menu_screen(p))!=at;i++) {
        pause_key(p,SDLK_RIGHT);pause_menu_update(p,.5f);
    }
    assert(ps_selected(pause_menu_screen(p))==at);
    pause_key(p,SDLK_RETURN);pause_menu_update(p,.05f);assert(ps_dialog_open(pause_menu_screen(p)));
    pause_key(p,SDLK_ESCAPE);pause_menu_update(p,.5f);assert(pause_menu_is_open(p));
    assert(!ps_dialog_open(pause_menu_screen(p)));
    pause_key(p,SDLK_RETURN);pause_menu_update(p,.05f);
    pause_key(p,SDLK_LEFT);pause_menu_update(p,.05f);pause_key(p,SDLK_RETURN);
    assert(pause_menu_update(p,.5f)==FE_ACTION_WORLD_MAP);
    pause_menu_open(p);pause_menu_update(p,.05f);pause_key(p,SDLK_ESCAPE);
    assert(pause_menu_update(p,.5f)==FE_ACTION_RESUME);
    c.mode=0;pause_menu_set_context(p,&c);pause_menu_open(p);assert(pause_find(p,PE_WORLD_MAP)<0);
    c.mode=PE_MODE_CAREER;c.garage_flag=1;pause_menu_set_context(p,&c);
    pause_menu_set_world_map_enabled(p,0);pause_menu_open(p);pause_menu_update(p,.05f);
    at=pause_find(p,PE_GARAGE);assert(at>=0);
    for(int i=0;i<PE_MAX && ps_selected(pause_menu_screen(p))!=at;i++) {
        pause_key(p,SDLK_RIGHT);pause_menu_update(p,.5f);
    }
    assert(ps_selected(pause_menu_screen(p))==at);
    pause_key(p,SDLK_RETURN);pause_menu_update(p,.05f);
    pause_key(p,SDLK_LEFT);pause_menu_update(p,.05f);pause_key(p,SDLK_RETURN);
    assert(pause_menu_update(p,.5f)==FE_ACTION_RESUME);

    pause_menu_destroy(p);
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
    key(m,SDLK_RIGHT);frames(m,1,NULL);key(m,SDLK_DOWN);frames(m,1,NULL);key(m,SDLK_RIGHT);frames(m,5,NULL);   /* Laps+1, Direction=Reverse */
    key(m,SDLK_RETURN);frames(m,60,"mode_options.png");key(m,SDLK_RIGHT);frames(m,5,NULL);   /* Difficulty=Medium */
    key(m,SDLK_RETURN);frames(m,60,"car.png");report(m,"car");
    key(m,SDLK_RIGHT);frames(m,10,NULL);printf("preview=%s\n",fem_preview_car(m));assert(!strcmp(fem_preview_car(m),"RX7"));
    key(m,SDLK_RETURN);FemRequest r=frames(m,2,NULL);
    printf("request=%d event=%d car=%s track=%s laps=%d difficulty=%d\n",r.kind,r.event_id,r.car,r.track,r.laps,r.difficulty);
    assert(r.kind==FEM_REQ_QUICK_RACE && r.event_id==4000 && r.laps==3 && r.difficulty==1 && !strcmp(r.car,"RX7"));
    {RrOptions d=rr_options_default();assert(r.rules.reverse==1 && r.rules.opponents==d.opponents && r.rules.traffic==d.traffic && r.rules.catch_up==d.catch_up && r.rules.lap_knockout==0);}
    PostRace *post=pr_create(argv[1]);PostRaceInfo info={.event_id=4001,.finish_place=1,.seconds=62.34};strcpy(info.car,"MIATA");
    pr_open(post,&info);
    for(int n=0;n<60;n++){pr_update(post,1.0f/30);rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);pr_draw(post,W,H);if(n==59){char out[600];snprintf(out,sizeof out,"%s/results.png",dir);save(out,W,H);}rhi_frame_end();}
    printf("results open=%d status=%s\n",pr_is_open(post),pr_status(post));
    SDL_Event next={0};next.type=SDL_KEYDOWN;next.key.keysym.sym=SDLK_RETURN;pr_event(post,&next,NULL);
    for(int n=0;n<60;n++){pr_update(post,1.0f/30);rhi_frame_begin(NULL);static const float black[4]={0,0,0,1};rhi_cmd_clear_main(black,1);pr_draw(post,W,H);if(n==59){char out[600];snprintf(out,sizeof out,"%s/postmenu.png",dir);save(out,W,H);}rhi_frame_end();}
    printf("postmenu open=%d status=%s\n",pr_is_open(post),pr_status(post));assert(pr_is_open(post) && !pr_status(post)[0]);
    pr_event(post,&next,NULL);assert(pr_update(post,0)==PR_ACT_RESTART);pr_destroy(post);
    fem_open(m,FEM_SCR_QR_TRACK);click(m,win,145,680);frames(m,30,NULL);assert(fem_screen(m)==FEM_SCR_QR_MODE);
    {   /* M6 career Car Lot (UI_CareerCarLot.fng): browse, must-buy Back block, two-step buy (touch), owned car */
        FeLotCar lot[2]={{"MIATA",0,0,1},{"RX7",0,0,1}};
        fem_open_lot(m,lot,2,1,0);frames(m,60,"lot.png");report(m,"lot");assert(fem_visual_available(m));
        key(m,SDLK_RIGHT);frames(m,15,NULL);assert(!strcmp(fem_preview_car(m),"RX7"));
        click(m,win,145,680);frames(m,30,"lot_must_buy.png");assert(fem_screen(m)==FEM_SCR_CAREER_LOT);
        click(m,win,480,360);frames(m,30,"lot_confirm.png");
        click(m,win,480,360);FemRequest b=frames(m,2,NULL);
        printf("lot request=%d car=%s\n",b.kind,b.car);assert(b.kind==FEM_REQ_LOT_BUY && !strcmp(b.car,"RX7"));
        lot[0].owned=1;fem_open_lot(m,lot,2,0,12345);key(m,SDLK_LEFT);frames(m,1,NULL);key(m,SDLK_RETURN);frames(m,30,"lot_same_car.png");
        key(m,SDLK_ESCAPE);assert(frames(m,2,NULL).kind==FEM_REQ_CAREER_ROAM);
    }
    worldmap_checks(m,win,argv[1]);
    /* Direct World Map opening must load its own map/player art without a preceding QR visit. */
    FeMenu *standalone=fem_create(argv[1],&cat,1);assert(standalone);
    worldmap_checks(standalone,win,argv[1]);fem_destroy(standalone);
    pause_worldmap_checks(argv[1]);
    printf("fe_screens_check: PASS\n");
    fem_destroy(m);
    rhi_device_destroy();
    return 0;
}
