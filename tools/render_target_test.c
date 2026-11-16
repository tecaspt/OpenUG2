/* Portable contract test: fake RHI records resource and state transitions. */
#include "../src/render_target.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static RhiCaps caps;
static int calls, creates, destroys, selected, fail_create, fail_select;
static int width, height, step, draws;
static unsigned next_id = 1;
#define CALL() (++calls)
const RhiCaps *rhi_caps(void) { CALL(); return &caps; }
RhiResult rhi_scene_target_create(int32_t w, int32_t h, RhiSceneTarget *out) {
    CALL(); ++creates; width=w; height=h; out->id=0;
    if (fail_create) return RHI_ERR_OUT_OF_MEMORY;
    out->id=next_id++; return RHI_OK;
}
void rhi_scene_target_destroy(RhiSceneTarget t) { CALL(); if (t.id) ++destroys; }
RhiTexture rhi_scene_target_color(RhiSceneTarget t) { RhiTexture x = {t.id}; CALL(); return x; }
int rhi_scene_target_is_selected(RhiSceneTarget t) { CALL(); return t.id && selected; }
RhiResult rhi_cmd_select_scene_target(RhiSceneTarget t) { CALL(); assert(t.id && !selected); if (fail_select) return RHI_ERR_INVALID; selected=1; return RHI_OK; }
RhiResult rhi_cmd_unselect_scene_target(void) { CALL(); assert(selected); selected=0; step=1; return RHI_OK; }
RhiShader rhi_shader_create(const RhiShaderDesc *d) {
    RhiShader s = {next_id++}; CALL(); assert(d->param_block_count==0);
    assert(strstr(d->fragment_source,"uTex0") && strstr(d->fragment_source,"texture2D"));
    if (caps.backend==RHI_BACKEND_GLES2) assert(strstr(d->fragment_source,"precision mediump float"));
    else assert(strstr(d->fragment_source,"#version 120"));
    return s;
}
void rhi_shader_destroy(RhiShader s) { CALL(); assert(s.id); }
RhiBuffer rhi_buffer_create(const RhiBufferDesc *d) {
    const float *p = d->data; RhiBuffer b = {next_id++}; CALL();
    assert(d->size==12*sizeof(float));
    assert(p[0]==-1 && p[1]==-1 && p[2]==0 && p[3]==0);
    assert(p[4]==3 && p[5]==-1 && p[6]==2 && p[7]==0);
    assert(p[8]==-1 && p[9]==3 && p[10]==0 && p[11]==2);
    return b;
}
void rhi_buffer_destroy(RhiBuffer b) { CALL(); assert(b.id); }
void rhi_pipeline_desc_init(RhiPipelineDesc *d) { CALL(); memset(d,0,sizeof(*d)); }
RhiPipeline rhi_pipeline_create(const RhiPipelineDesc *d) {
    RhiPipeline p = {next_id++}; CALL(); assert(!d->depth_test && !d->depth_write && !d->blend && !d->stencil_test && d->color_write==15); return p;
}
void rhi_cmd_save_state(RhiStateSnapshot *s) { CALL(); assert(!selected && step==1); step=2; memset(s,0,sizeof(*s)); }
void rhi_cmd_restore_state(const RhiStateSnapshot *s) { CALL(); (void)s; assert(step==3); step=4; }
RhiResult rhi_cmd_set_viewport(int32_t x,int32_t y,int32_t w,int32_t h) { CALL(); assert(step==2 && x==0 && y==0 && w>0 && h>0); return RHI_OK; }
void rhi_cmd_bind_pipeline(RhiPipeline p) { CALL(); assert(p.id && step==2); }
void rhi_cmd_bind_vertex_buffer(uint32_t slot,RhiBuffer b,uint32_t offset) { CALL(); assert(slot==0 && b.id && offset==0 && step==2); }
void rhi_cmd_bind_texture(uint32_t slot,RhiTexture t) { CALL(); assert(slot==0 && t.id && step==2); }
void rhi_cmd_draw(uint32_t n,uint32_t first) { CALL(); assert(n==3 && !first && step==2); step=3; ++draws; }

static void test_parser(void) {
    const char *bad[]={"", "foo", "0.499", "1.01", "nan", "inf", "-inf", "0.7x", "0.7 ", " 0.7", "1.00000001", "1e999", NULL};
    float out=42; int i;
    assert(!render_target_parse_scale(NULL,&out) && out==42);
    assert(!render_target_parse_scale("0.5",NULL));
    for (i=0;bad[i];++i) { assert(!render_target_parse_scale(bad[i],&out)); assert(out==42); }
    assert(render_target_parse_scale("0.5",&out) && out==0.5f);
    assert(render_target_parse_scale("1",&out) && out==1.f);
}
static void test_backend(RhiBackend backend) {
    RenderTarget rt = {0}; unsigned old; int before;
    memset(&caps,0,sizeof(caps)); caps.backend=backend; caps.scene_target=1;
    before=calls; assert(!render_target_begin(&rt,100,51,1)); render_target_present(&rt); assert(calls==before);
    assert(!render_target_begin(&rt,100,51,NAN)); assert(calls==before);
    caps.scene_target=0; assert(!render_target_begin(&rt,100,51,.5f)); assert(rt.effective_scale==1);
    caps.scene_target=1; assert(render_target_begin(&rt,100,51,.5f));
    assert(width==50 && height==25 && rt.scene_w==50 && rt.scene_h==25 && selected);
    render_target_present(&rt); assert(!selected && step==4 && draws>0);
    old=rt.target.id; before=creates;
    assert(render_target_begin(&rt,100,51,.5f)); assert(creates==before && rt.target.id==old);
    render_target_present(&rt);
    fail_create=1; assert(!render_target_begin(&rt,200,100,.5f)); assert(rt.target.id==old && rt.effective_scale==1);
    before=creates; assert(!render_target_begin(&rt,200,100,.5f)); assert(creates==before);
    fail_create=0; assert(render_target_begin(&rt,202,102,.5f)); assert(rt.target.id!=old && width==101 && height==51);
    render_target_present(&rt);
    fail_select=1; old=rt.target.id; assert(!render_target_begin(&rt,300,100,.5f)); assert(rt.target.id==old && !selected); fail_select=0;
    assert(render_target_begin(&rt,1,1,.5f)); assert(width==1 && height==1);
    before=calls; assert(!render_target_begin(&rt,1,1,.5f) && rt.active && selected); assert(calls==before+1);
    selected=0; /* frame end recovered a missed presentation */
    assert(render_target_begin(&rt,1,1,.5f) && selected); render_target_present(&rt);
    assert(render_target_begin(&rt,1,1,.5f)); selected=0;
    before=calls; assert(!render_target_begin(&rt,1,1,1.f) && !rt.active); render_target_present(&rt); assert(calls==before);
    assert(render_target_begin(&rt,1,1,.5f)); selected=0;
    before=draws; render_target_present(&rt); assert(!rt.active && draws==before);
    assert(render_target_begin(&rt,1,1,.5f));
    render_target_destroy(&rt); assert(!rt.target.id && !rt.active && !selected);
    before=calls; render_target_destroy(&rt); assert(calls==before);
}
int main(void) { test_parser(); test_backend(RHI_BACKEND_GL); test_backend(RHI_BACKEND_GLES2); puts("render_target_test: OK"); return 0; }

