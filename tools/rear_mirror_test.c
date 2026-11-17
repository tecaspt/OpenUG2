#include "rear_mirror.h"
#include <assert.h>
#include <stdio.h>
static void policy_checks(void)
{
    int n=17;float r[4],eye[3],dir[3];
    assert(rear_mirror_parse("off",&n) && n==0);
    assert(rear_mirror_parse("on",&n) && n==1);
    assert(!rear_mirror_parse("ON",&n) && n==1);
    assert(!rear_mirror_parse(NULL,&n));
    assert(!rear_mirror_parse("on",NULL));
    for(int mask=0;mask<32;mask++)
        assert(rear_mirror_visible(mask&1,mask&2,mask&4,mask&8,mask&16)==(mask==1));
    rear_mirror_rect(1170,643,r);
    assert(fabsf(r[0]-452.5f)<.01f && r[1]==75 && r[2]==265 && r[3]==45);
    rear_mirror_rect(640,480,r);
    assert(fabsf(r[0]*2+r[2]-640)<.01f);
    assert(fabsf(r[2]/r[3]-265.f/45)<.001f);
    for(int k=0;k<4;k++){
        float h=k*1.5707963268f;
        rear_mirror_camera((float[]){10,20,30},h,2,eye,dir);
        assert(eye[0]==10 && eye[1]==20 && fabsf(eye[2]-32.3f)<.001f);
        assert(fabsf(dir[0]*cosf(h)+dir[1]*sinf(h)+1)<.001f);
    }
    puts("rear_mirror_test: 48 checks passed");
}

#ifdef REAR_MIRROR_GL_TEST
#include "rhi_gl.h"
#include "render_target.h"
static void gpu_checks(void)
{
    RearMirror mirror={0};RenderTarget scene={0};
    unsigned char pixels[1170*643*4];
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    assert(rhi_prepare_window(RHI_BACKEND_DEFAULT)==RHI_OK);
    SDL_Window *win=SDL_CreateWindow("mirror test",0,0,1170,643,
        rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN);
    assert(win);
    RhiDeviceDesc device={RHI_BACKEND_DEFAULT,0,win};
    assert(rhi_device_create(&device)==RHI_OK);
    for(int i=0;i<3;i++){
        assert(rhi_frame_begin(NULL)==RHI_OK);
        rhi_cmd_set_viewport(7,11,100,200);
        assert(rear_mirror_begin(&mirror));
        assert(rhi_scene_target_is_selected(mirror.target));
        assert(rhi_cmd_clear_main((float[]){1,0,0,1},1)==RHI_OK);
        glEnable(GL_SCISSOR_TEST);glScissor(128,0,128,40);
        glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        rear_mirror_finish(&mirror);
        int vp[4];rhi_viewport(vp);
        assert(vp[0]==7 && vp[1]==11 && vp[2]==100 && vp[3]==200);
        assert(!rhi_scene_target_is_selected(mirror.target));
        float scale=i==0?1.f:i==1?.5f:.75f;
        assert(render_target_begin(&scene,1170,643,scale)==(i!=0));
        assert(rhi_cmd_clear_main((float[]){0,1,0,1},1)==RHI_OK);
        render_target_present(&scene);
        rear_mirror_draw(&mirror,1170,643);
        assert(rhi_read_pixels(0,0,1170,643,RHI_READ_RGBA8,pixels,0)==RHI_OK);
        unsigned char *left=pixels+(95*1170+480)*4;
        unsigned char *right=pixels+(95*1170+680)*4;
        assert(left[2]>240 && left[0]<10);
        assert(right[0]>240 && right[2]<10);
        assert(pixels[(140*1170+480)*4+1]>240);
        assert(glGetError()==GL_NO_ERROR);
        assert(rhi_frame_end()==RHI_OK);
    }
    rear_mirror_destroy(&mirror);render_target_destroy(&scene);
    assert(glGetError()==GL_NO_ERROR);
    rhi_device_destroy();SDL_DestroyWindow(win);SDL_Quit();
    puts("rear mirror GPU: reflection, placement, state, scale 1/.5/.75 passed");
}
#endif
int main(void)
{
    policy_checks();
#ifdef REAR_MIRROR_GL_TEST
    gpu_checks();
#endif
    return 0;
}
