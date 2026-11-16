#include "../src/render_target.h"
#include "../src/rhi_gl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char reference[256*128*4], pixels[256*128*4];
static RhiShader shader;
static RhiPipeline pipeline;
static RhiBuffer vertices;
static void setup(void) {
    static const float v[]={-1,-1, 3,-1, -1,3};
    static const char *names[]={"aPosition"};
    const int es=rhi_caps()->backend==RHI_BACKEND_GLES2;
    RhiShaderDesc sd={0}; RhiBufferDesc bd={0}; RhiPipelineDesc pd;
    sd.vertex_source=es ? "attribute vec2 aPosition; varying mediump vec2 uv; void main(){uv=aPosition*.5+.5; gl_Position=vec4(aPosition,0.,1.);}" : "#version 120\nattribute vec2 aPosition; varying vec2 uv; void main(){uv=aPosition*.5+.5; gl_Position=vec4(aPosition,0.,1.);}";
    sd.fragment_source=es ? "precision mediump float; varying mediump vec2 uv; void main(){gl_FragColor=vec4(uv,.25,1.);}" : "#version 120\nvarying vec2 uv; void main(){gl_FragColor=vec4(uv,.25,1.);}";
    sd.attribute_names=names; sd.attribute_count=1; shader=rhi_shader_create(&sd); assert(shader.id);
    bd.usage=RHI_BUFFER_VERTEX; bd.update=RHI_UPDATE_STATIC; bd.size=sizeof(v); bd.data=v; vertices=rhi_buffer_create(&bd); assert(vertices.id);
    rhi_pipeline_desc_init(&pd); pd.shader=shader; pd.vertex.attrib_count=1; pd.vertex.attribs[0].format=RHI_VERTEX_FLOAT2; pd.vertex.stride[0]=2*sizeof(float); pd.depth_test=0; pd.depth_write=0; pd.color_write=15;
    pipeline=rhi_pipeline_create(&pd); assert(pipeline.id);
}
static void gradient(void) { float clear[]={0,0,0,1}; assert(rhi_cmd_clear_main(clear,1)==RHI_OK); rhi_cmd_bind_pipeline(pipeline); rhi_cmd_bind_vertex_buffer(0,vertices,0); rhi_cmd_draw(3,0); }
static void check(float scale, RenderTarget *rt) {
    int x,y,c,vp[4];
    assert(rhi_frame_begin(NULL)==RHI_OK);
    assert(render_target_begin(rt,256,128,scale)); gradient(); render_target_present(rt);
    rhi_viewport(vp); assert(vp[0]==0 && vp[1]==0 && vp[2]==256 && vp[3]==128);
    assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,pixels,0)==RHI_OK);
    for(y=2;y<126;y++) for(x=2;x<254;x++) for(c=0;c<4;c++) assert(abs((int)pixels[(y*256+x)*4+c]-(int)reference[(y*256+x)*4+c])<=2);
    glEnable(GL_SCISSOR_TEST); glScissor(31,40,1,7); glClearColor(1,1,1,1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
    assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,pixels,0)==RHI_OK);
    for(y=0;y<128;y++) for(x=30;x<=32;x++) { unsigned char *p=pixels+(y*256+x)*4; if(x==31 && y>=81 && y<88) assert(p[0]==255 && p[1]==255 && p[2]==255); else assert(p[2]<100); }
    assert(glGetError()==GL_NO_ERROR); assert(rhi_frame_end()==RHI_OK);
}
int main(void) {
    RenderTarget rt={0}; int i;
    assert(SDL_Init(SDL_INIT_VIDEO)==0); assert(rhi_prepare_window(RHI_BACKEND_DEFAULT)==RHI_OK);
    SDL_Window *win=SDL_CreateWindow("composite",0,0,256,128,rhi_window_flags(RHI_BACKEND_DEFAULT)|SDL_WINDOW_HIDDEN); assert(win);
    RhiDeviceDesc device={RHI_BACKEND_DEFAULT,0,win}; assert(rhi_device_create(&device)==RHI_OK); assert(rhi_caps()->scene_target); setup();
    assert(rhi_frame_begin(NULL)==RHI_OK); gradient(); assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,reference,0)==RHI_OK);
    assert(!render_target_begin(&rt,256,128,1.f)); render_target_present(&rt); assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,pixels,0)==RHI_OK);
    assert(!memcmp(pixels,reference,sizeof(pixels))); assert(rhi_frame_end()==RHI_OK);
    check(.5f,&rt); check(.75f,&rt);
    /* Frame end recovers a forgotten selection; next frame must composite new pixels. */
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(render_target_begin(&rt,256,128,.5f));
    { float red[]={1,0,0,1}; assert(rhi_cmd_clear_main(red,1)==RHI_OK); }
    assert(rhi_frame_end()==RHI_OK);
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(render_target_begin(&rt,256,128,.5f));
    gradient(); render_target_present(&rt); assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,pixels,0)==RHI_OK);
    for (i=0;i<4;i++) assert(abs((int)pixels[(64*256+128)*4+i]-(int)reference[(64*256+128)*4+i])<=2);
    assert(rhi_frame_end()==RHI_OK);
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(render_target_begin(&rt,256,128,.5f));
    assert(rhi_frame_end()==RHI_OK);
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(!render_target_begin(&rt,256,128,1.f));
    gradient(); render_target_present(&rt); assert(rhi_read_pixels(0,0,256,128,RHI_READ_RGBA8,pixels,0)==RHI_OK);
    assert(!memcmp(pixels,reference,sizeof(pixels))); assert(rhi_frame_end()==RHI_OK);
    SDL_SetWindowSize(win,320,180); SDL_PumpEvents();
    assert(rhi_frame_begin(NULL)==RHI_OK); assert(render_target_begin(&rt,320,180,.75f)); assert(rt.scene_w==240 && rt.scene_h==135); gradient(); render_target_present(&rt);
    assert(glGetError()==GL_NO_ERROR); assert(rhi_frame_end()==RHI_OK);
    render_target_destroy(&rt); rhi_buffer_destroy(vertices); rhi_shader_destroy(shader); i=glGetError(); assert(i==GL_NO_ERROR);
    rhi_device_destroy(); SDL_DestroyWindow(win); SDL_Quit(); puts("render_target GPU: OK"); return 0;
}
