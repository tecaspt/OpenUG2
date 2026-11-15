/* Runtime-only retail results rendering; assets/output supplied at runtime. */
#include "postrace.h"
#include "race.h"
#include "rhi_gl.h"
#include <png.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void capture(const char *file,int w,int h) {
    unsigned char *pixels=malloc((size_t)w*h*3);assert(pixels);
    assert(rhi_read_pixels(0,0,w,h,RHI_READ_RGB8,pixels,0)==RHI_OK);
    int lit=0;for(int k=0;k<w*h;k++)if(pixels[k*3]>20 || pixels[k*3+1]>20 || pixels[k*3+2]>20)lit++;
    assert(lit>w*h/100);
    FILE *f=fopen(file,"wb");assert(f);
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);assert(png);
    png_infop info=png_create_info_struct(png);assert(info);
    if(setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png,&info);fclose(f);free(pixels);
        fputs("PNG write failed\n",stderr);exit(1);
    }
    png_init_io(png,f);png_set_IHDR(png,info,w,h,8,PNG_COLOR_TYPE_RGB,0,0,0);png_write_info(png,info);
    for(int y=0;y<h;y++)png_write_row(png,pixels+(size_t)y*w*3);
    png_write_end(png,info);png_destroy_write_struct(&png,&info);fclose(f);free(pixels);
}
int main(int argc,char **argv) {
    const char *root=getenv("OPENUG2_DATA");if(!root){puts("postrace_rules_check: SKIP OPENUG2_DATA unset");return 77;}
    if(argc!=2){fprintf(stderr,"usage: postrace_rules_check OUTPUT_DIR\n");return 2;}
    const int w=960,h=720;assert(!SDL_Init(SDL_INIT_VIDEO));rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *window=SDL_CreateWindow("results rules",0,0,w,h,rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN);assert(window);
    RhiDeviceDesc desc={RHI_BACKEND_DEFAULT,0,window};assert(rhi_device_create(&desc)==RHI_OK);
    PostRace *post=pr_create(root);assert(post);
    PostRaceInfo info={.kind=N2_RACE_DRIFT,.event_id=4444,.finish_place=2,.seconds=90,.car="SYNTH",.drift_points=12345,.have_drift_points=1};
    pr_open(post,&info);if(pr_status(post)[0]){fprintf(stderr,"%s\n",pr_status(post));return 1;}
    for(int tick=0;tick<45;tick++) {
        pr_update(post,1/30.f);rhi_frame_begin(NULL);pr_draw(post,w,h);
        if(tick==44){char path[1024];snprintf(path,sizeof path,"%s/drift-points.png",argv[1]);capture(path,w,h);}
        rhi_frame_end();
    }
    info=(PostRaceInfo){.kind=N2_RACE_URL,.finish_place=2,.seconds=90,
        .have_url_standings=1,.standing_count=3,.url_round=2,.url_rounds=3,
        .standing={{"Player","SYNTH",14,2},{"Leader","SYNTH2",16,1},{"Rival","SYNTH3",10,3}}};
    pr_open(post,&info);assert(!pr_status(post)[0]);
    for(int tick=0;tick<45;tick++) {
        pr_update(post,1/30.f);rhi_frame_begin(NULL);pr_draw(post,w,h);
        if(tick==44){char path[1024];snprintf(path,sizeof path,"%s/url-standings.png",argv[1]);capture(path,w,h);}
        rhi_frame_end();
    }
    assert(!pr_status(post)[0]);
    pr_destroy(post);rhi_device_destroy();SDL_DestroyWindow(window);SDL_Quit();
    puts("postrace_rules_check: PASS (retail results/caption binding and offscreen draw)");return 0;
}
