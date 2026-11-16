#include "render_target.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int render_target_parse_scale(const char *text, float *out)
{
    char *end;
    double value;
    if (!text || !out || !*text || isspace((unsigned char)*text)) return 0;
    errno = 0;
    value = strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(value) ||
        value < 0.5f || value > 1.0f) return 0;
    *out = (float)value;
    return 1;
}

static int prepare_compositor(RenderTarget *rt, RhiBackend backend)
{
    static const char *const names[] = {"aPosition", "aUV"};
    static const float triangle[] = {
        -1.f, -1.f, 0.f, 0.f,  3.f, -1.f, 2.f, 0.f,
        -1.f,  3.f, 0.f, 2.f
    };
    static const char vertex_gl[] =
        "#version 120\nattribute vec2 aPosition; attribute vec2 aUV;"
        "varying vec2 vUV; void main(){vUV=aUV; gl_Position=vec4(aPosition,0.0,1.0);}";
    static const char fragment_gl[] =
        "#version 120\nuniform sampler2D uTex0; varying vec2 vUV;"
        "void main(){gl_FragColor=texture2D(uTex0,vUV);}";
    static const char vertex_es[] =
        "attribute vec2 aPosition; attribute vec2 aUV; varying mediump vec2 vUV;"
        "void main(){vUV=aUV; gl_Position=vec4(aPosition,0.0,1.0);}";
    static const char fragment_es[] =
        "precision mediump float; uniform sampler2D uTex0; varying mediump vec2 vUV;"
        "void main(){gl_FragColor=texture2D(uTex0,vUV);}";
    RhiShaderDesc shader;
    RhiBufferDesc buffer;
    RhiPipelineDesc pipeline;
    if (rt->shader.id && rt->vertices.id && rt->pipeline.id) return 1;
    memset(&shader, 0, sizeof(shader));
    shader.vertex_source = backend == RHI_BACKEND_GLES2 ? vertex_es : vertex_gl;
    shader.fragment_source = backend == RHI_BACKEND_GLES2 ? fragment_es : fragment_gl;
    shader.attribute_names = names;
    shader.attribute_count = 2;
    shader.label = "scene composite";
    if (!rt->shader.id) rt->shader = rhi_shader_create(&shader);
    if (!rt->shader.id) return 0;
    memset(&buffer, 0, sizeof(buffer));
    buffer.usage = RHI_BUFFER_VERTEX;
    buffer.update = RHI_UPDATE_STATIC;
    buffer.size = sizeof(triangle);
    buffer.data = triangle;
    if (!rt->vertices.id) rt->vertices = rhi_buffer_create(&buffer);
    if (!rt->vertices.id) return 0;
    rhi_pipeline_desc_init(&pipeline);
    pipeline.shader = rt->shader;
    pipeline.vertex.attrib_count = 2;
    pipeline.vertex.attribs[0].location = 0;
    pipeline.vertex.attribs[0].format = RHI_VERTEX_FLOAT2;
    pipeline.vertex.attribs[1].location = 1;
    pipeline.vertex.attribs[1].offset = 2 * sizeof(float);
    pipeline.vertex.attribs[1].format = RHI_VERTEX_FLOAT2;
    pipeline.vertex.stride[0] = 4 * sizeof(float);
    pipeline.depth_test = pipeline.depth_write = pipeline.blend = pipeline.stencil_test = 0;
    pipeline.cull = RHI_CULL_NONE;
    pipeline.color_write = 15;
    rt->pipeline = rhi_pipeline_create(&pipeline);
    return rt->pipeline.id != 0;
}

int render_target_begin(RenderTarget *rt, int w, int h, float scale)
{
    const RhiCaps *caps;
    RhiSceneTarget replacement = {0};
    int sw, sh;
    if (!rt) return 0;
    if (!isfinite(scale)) scale = 1.f;
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 1.f) scale = 1.f;
    if (scale == 1.f) { rt->active = 0; rt->effective_scale = 1.f; return 0; }
    if (rt->active) {
        if (rhi_scene_target_is_selected(rt->target)) return 0;
        rt->active = 0;
    }
    rt->effective_scale = 1.f;
    if (w <= 0 || h <= 0) return 0;
    caps = rhi_caps();
    if (!caps || !caps->scene_target) return 0;
    sw = (int)floor((double)w * scale);
    sh = (int)floor((double)h * scale);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (rt->failed_w == sw && rt->failed_h == sh) return 0;
    if (!rt->target.id || rt->scene_w != sw || rt->scene_h != sh) {
        if (rhi_scene_target_create(sw, sh, &replacement) != RHI_OK) goto failed;
    }
    if (!prepare_compositor(rt, caps->backend)) goto failed;
    if (rhi_cmd_select_scene_target(replacement.id ? replacement : rt->target) != RHI_OK) goto failed;
    if (replacement.id) {
        rhi_scene_target_destroy(rt->target);
        rt->target = replacement;
    }
    rt->drawable_w = w; rt->drawable_h = h;
    rt->scene_w = sw; rt->scene_h = sh;
    rt->failed_w = rt->failed_h = 0;
    rt->effective_scale = scale;
    rt->active = 1;
    return 1;
failed:
    if (replacement.id) rhi_scene_target_destroy(replacement);
    rt->failed_w = sw; rt->failed_h = sh;
    return 0;
}

void render_target_present(RenderTarget *rt)
{
    RhiStateSnapshot state;
    if (!rt || !rt->active) return;
    if (!rhi_scene_target_is_selected(rt->target)) { rt->active = 0; return; }
    if (rhi_cmd_unselect_scene_target() == RHI_OK) {
        rhi_cmd_save_state(&state);
        rhi_cmd_set_viewport(0, 0, rt->drawable_w, rt->drawable_h);
        rhi_cmd_bind_pipeline(rt->pipeline);
        rhi_cmd_bind_vertex_buffer(0, rt->vertices, 0);
        rhi_cmd_bind_texture(0, rhi_scene_target_color(rt->target));
        rhi_cmd_draw(3, 0);
        rhi_cmd_restore_state(&state);
    }
    rt->active = 0;
}

void render_target_destroy(RenderTarget *rt)
{
    if (!rt) return;
    if (rt->active && rhi_scene_target_is_selected(rt->target)) rhi_cmd_unselect_scene_target();
    if (rt->target.id) rhi_scene_target_destroy(rt->target);
    if (rt->vertices.id) rhi_buffer_destroy(rt->vertices);
    if (rt->shader.id) rhi_shader_destroy(rt->shader);
    memset(rt, 0, sizeof(*rt));
}
