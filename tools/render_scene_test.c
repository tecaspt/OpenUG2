#include "rhi_gl.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    assert(rhi_prepare_window(RHI_BACKEND_DEFAULT)==RHI_OK);
    SDL_Window *win=SDL_CreateWindow("scene",0,0,64,64,rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN); assert(win);
    RhiDeviceDesc device={RHI_BACKEND_DEFAULT,0,win}; assert(rhi_device_create(&device)==RHI_OK);
    assert(rhi_caps()->scene_target);
    RhiSceneTarget scene={123}; assert(rhi_scene_target_create(0,4,&scene)==RHI_ERR_INVALID && !scene.id);
    GLint fb0,rb0,active0,tex0; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb0); glGetIntegerv(GL_RENDERBUFFER_BINDING,&rb0);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_ACTIVE_TEXTURE,&active0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex0);
    assert(rhi_scene_target_create(rhi_caps()->max_texture_size+1,12,&scene)==RHI_ERR_UNSUPPORTED && !scene.id);
    GLint check; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&check); assert(check==fb0);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&check); assert(check==rb0);
    assert(rhi_scene_target_create(16,12,&scene)==RHI_OK);
    GLint n; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&n); assert(n==fb0); glGetIntegerv(GL_RENDERBUFFER_BINDING,&n); assert(n==rb0);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&n); assert(n==active0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&n); assert(n==tex0);
    RhiTexture color=rhi_scene_target_color(scene); assert(color.id && rhi_texture_is_live(color));
    const RhiSamplerDesc *sd=rhi_sampler_desc(rhi_texture_sampler(color));
    assert(sd && sd->min_filter==RHI_FILTER_LINEAR && sd->mag_filter==RHI_FILTER_LINEAR && sd->mip_filter==RHI_MIP_NONE && sd->wrap_u==RHI_WRAP_CLAMP && sd->wrap_v==RHI_WRAP_CLAMP);
    assert(rhi_cmd_select_scene_target(scene)==RHI_ERR_INVALID); assert(rhi_frame_begin(NULL)==RHI_OK);
    assert(!rhi_scene_target_is_selected(scene));
    glEnable(GL_SCISSOR_TEST); assert(rhi_cmd_select_scene_target(scene)==RHI_OK); assert(rhi_scene_target_is_selected(scene)); assert(!glIsEnabled(GL_SCISSOR_TEST));
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT,vp); assert(vp[2]==16 && vp[3]==12);
    glGetIntegerv(GL_STENCIL_BITS,&n); assert(n>=8);
    assert(rhi_cmd_select_scene_target(scene)==RHI_ERR_INVALID);
    glClearStencil(7); glStencilMask(255); glClear(GL_STENCIL_BUFFER_BIT);
    float red[4]={1,0,0,1}; assert(rhi_cmd_clear_main(red,1)==RHI_OK);
#ifndef N2_GLES
    { unsigned char stencil=255; glReadPixels(0,0,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil); assert(stencil==0); }
#endif
    unsigned char pixel[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel); assert(pixel[0]==255 && pixel[1]==0 && pixel[3]==255);
    assert(rhi_read_pixels(0,0,1,1,RHI_READ_RGBA8,pixel,0)==RHI_ERR_INVALID);
    RhiTextureDesc td={RHI_TEXTURE_2D,RHI_FORMAT_RGBA8,4,4,RHI_TEXTURE_SAMPLED|RHI_TEXTURE_COPY_DST};
    RhiTexture copy=rhi_texture_create(&td); assert(copy.id);
    assert(rhi_cmd_copy_main_to_texture(copy,13,0,4,4)==RHI_ERR_INVALID);
    assert(rhi_cmd_copy_main_to_texture(copy,0,0,4,4)==RHI_OK);
    RhiTextureDesc dd={RHI_TEXTURE_2D,RHI_FORMAT_DEPTH,4,4,RHI_TEXTURE_SAMPLED|RHI_TEXTURE_COPY_DST};
    RhiTexture depth_copy=rhi_texture_create(&dd); assert(depth_copy.id);
#ifdef N2_GLES
    assert(rhi_cmd_copy_main_to_texture(depth_copy,0,0,4,4)==RHI_ERR_UNSUPPORTED);
#else
    assert(rhi_cmd_copy_main_to_texture(depth_copy,0,0,4,4)==RHI_OK);
    glBindTexture(GL_TEXTURE_2D,rhi_texture_native_id(depth_copy));
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_DEPTH_SIZE,&n); assert(n>=24);
#endif
    RhiTextureDesc pd={RHI_TEXTURE_2D,RHI_FORMAT_RGBA8,4,4,RHI_TEXTURE_COLOR_TARGET};
    RhiTexture pass_color=rhi_texture_create(&pd); assert(pass_color.id && rhi_texture_upload(pass_color,0,0,NULL,0)==RHI_OK);
    RhiStateSnapshot saved; rhi_cmd_save_state(&saved);
    RhiPassDesc pass={{pass_color,0},{{0},0},RHI_LOAD_CLEAR,RHI_LOAD_KEEP,{0,1,0,1},1};
    assert(rhi_cmd_begin_pass(&pass)==RHI_OK); assert(rhi_cmd_unselect_scene_target()==RHI_ERR_INVALID);
    assert(rhi_cmd_begin_pass(&pass)==RHI_ERR_INVALID); rhi_cmd_end_pass(); rhi_cmd_restore_state(&saved);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel); assert(pixel[0]==255 && pixel[1]==0);
    assert(rhi_cmd_unselect_scene_target()==RHI_OK); glGetIntegerv(GL_VIEWPORT,vp); assert(vp[2]==64 && vp[3]==64);
    assert(rhi_cmd_unselect_scene_target()==RHI_OK);
    /* Attach the copy as an ordinary pass and verify copy-main read the scene. */
    RhiTextureDesc cd={RHI_TEXTURE_2D,RHI_FORMAT_RGBA8,4,4,RHI_TEXTURE_COLOR_TARGET|RHI_TEXTURE_COPY_DST|RHI_TEXTURE_SAMPLED};
    RhiTexture copied=rhi_texture_create(&cd); assert(copied.id);
    assert(rhi_cmd_select_scene_target(scene)==RHI_OK); assert(rhi_cmd_copy_main_to_texture(copied,0,0,4,4)==RHI_OK);
    RhiStateSnapshot saved2; rhi_cmd_save_state(&saved2); pass.color.texture=copied; pass.color_load=RHI_LOAD_KEEP;
    assert(rhi_cmd_begin_pass(&pass)==RHI_OK); glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel); assert(pixel[0]==255 && pixel[1]==0 && pixel[3]==255);
    rhi_cmd_end_pass(); rhi_cmd_restore_state(&saved2); assert(rhi_frame_end()==RHI_OK); assert(!rhi_scene_target_is_selected(scene));
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(rhi_read_pixels(0,0,1,1,RHI_READ_RGBA8,pixel,0)==RHI_OK);
    rhi_scene_target_destroy(scene); assert(!rhi_scene_target_is_selected(scene)); assert(!rhi_scene_target_color(scene).id && !rhi_texture_is_live(color));
    assert(rhi_cmd_select_scene_target(scene)==RHI_ERR_INVALID);
    RhiSceneTarget resized={0}; assert(rhi_scene_target_create(8,8,&resized)==RHI_OK && resized.id!=scene.id);
    assert(rhi_cmd_select_scene_target(resized)==RHI_OK); assert(rhi_scene_target_is_selected(resized)); assert(!rhi_scene_target_is_selected(scene)); rhi_scene_target_destroy(resized);
    assert(rhi_read_pixels(0,0,1,1,RHI_READ_RGBA8,pixel,0)==RHI_OK);
    rhi_texture_destroy(depth_copy); rhi_texture_destroy(copy); rhi_texture_destroy(pass_color); rhi_texture_destroy(copied);
    assert(rhi_frame_end()==RHI_OK); assert(rhi_scene_target_create(4,4,&resized)==RHI_OK);
    rhi_device_destroy(); SDL_DestroyWindow(win); SDL_Quit(); puts("scene target PASS"); return 0;
}
