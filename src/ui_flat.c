/* ui_flat.c -- see ui_flat.h. Draws only through rhi.h. */
#include "rhi.h"
#include "generated_shaders.h"   /* src/shaders/: make-embedded byte arrays */

#include "ui_flat.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define UF_SEGS 32

/* Same parameter block as frontend/frontend_draw.c. */
typedef struct UfParams {
    float uMVP[16];
    float uColor[4];
} UfParams;

static const RhiParamField uf_param_fields[] = {
    {"uMVP",   RHI_PARAM_MAT4, offsetof(UfParams, uMVP),   1},
    {"uColor", RHI_PARAM_VEC4, offsetof(UfParams, uColor), 1},
};
static const RhiParamLayout uf_param_layout = {
    uf_param_fields, sizeof uf_param_fields / sizeof uf_param_fields[0], sizeof(UfParams), "FeParams"
};

struct UiFlat {
    RhiShader shader;
    UfParams *params;
    RhiPipeline pipeline;
    RhiBuffer quad;      /* unit square (0,0)-(1,1), 6 vertices */
    RhiBuffer tri;       /* (0,0) (1,0) (0,1) */
    RhiBuffer disc;      /* unit disc at the origin, UF_SEGS triangles */
    RhiStateSnapshot saved;
    int w, h, in_frame;
};

static char *uf_text(const unsigned char *body, unsigned len) {
    char *t = (char *)malloc(len + 1);
    if (t) { memcpy(t, body, len); t[len] = 0; }
    return t;
}

static RhiBuffer uf_vbo(const float *v, size_t bytes) {
    return rhi_buffer_create(&(RhiBufferDesc){RHI_BUFFER_VERTEX, RHI_UPDATE_STATIC, bytes, v});
}

UiFlat *uf_create(void) {
    UiFlat *u = (UiFlat *)calloc(1, sizeof *u);
    if (!u) return 0;

    static const char *const attributes[] = { "aPos" };
    static const RhiParamLayout *const blocks[] = { &uf_param_layout };
    char *vs = uf_text(src_shaders_frontend_vert, src_shaders_frontend_vert_len);
    char *fs = uf_text(src_shaders_frontend_frag, src_shaders_frontend_frag_len);
    if (vs && fs)
        u->shader = rhi_shader_create(&(RhiShaderDesc){ vs, fs, attributes, 1, "frontend", blocks, 1 });
    free(vs); free(fs);
    u->params = (UfParams *)rhi_shader_params(u->shader, 0);

    RhiPipelineDesc pd;
    rhi_pipeline_desc_init(&pd);
    pd.shader = u->shader;
    pd.vertex.attribs[0] = (RhiVertexAttrib){ 0, 0, 0, RHI_VERTEX_FLOAT2 };
    pd.vertex.attrib_count = 1;
    pd.depth_write = 0;
    pd.blend = 1;
    pd.src_rgb = pd.src_alpha = RHI_BLEND_SRC_ALPHA;
    pd.dst_rgb = pd.dst_alpha = RHI_BLEND_ONE_MINUS_SRC_ALPHA;
    u->pipeline = rhi_pipeline_create(&pd);

    const float quad[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    const float tri[] = { 0,0, 1,0, 0,1 };
    float disc[UF_SEGS * 6];
    for (int i = 0; i < UF_SEGS; i++) {
        float a0 = 6.28318530718f * (float)i / UF_SEGS, a1 = 6.28318530718f * (float)(i + 1) / UF_SEGS;
        float *p = disc + i * 6;
        p[0] = 0; p[1] = 0;
        p[2] = cosf(a0); p[3] = sinf(a0);
        p[4] = cosf(a1); p[5] = sinf(a1);
    }
    u->quad = uf_vbo(quad, sizeof quad);
    u->tri = uf_vbo(tri, sizeof tri);
    u->disc = uf_vbo(disc, sizeof disc);
    if (!u->params || !u->pipeline.id || !u->quad.id || !u->tri.id || !u->disc.id) {
        uf_destroy(u);
        return 0;
    }
    return u;
}

void uf_destroy(UiFlat *u) {
    if (!u) return;
    rhi_shader_destroy(u->shader);
    rhi_buffer_destroy(u->quad);
    rhi_buffer_destroy(u->tri);
    rhi_buffer_destroy(u->disc);
    free(u);
}

void uf_begin(UiFlat *u, int w, int h) {
    if (!u || w <= 0 || h <= 0) return;
    rhi_cmd_save_state(&u->saved);
    u->w = w; u->h = h; u->in_frame = 1;
    rhi_cmd_set_viewport(0, 0, w, h);
    rhi_cmd_bind_pipeline(u->pipeline);
}

void uf_end(UiFlat *u) {
    if (!u || !u->in_frame) return;
    rhi_cmd_restore_state(&u->saved);
    u->in_frame = 0;
}

void uf_color(UiFlat *u, float r, float g, float b, float a) {
    if (!u) return;
    u->params->uColor[0] = r; u->params->uColor[1] = g;
    u->params->uColor[2] = b; u->params->uColor[3] = a;
}

/* Affine map of the unit shape: x' = a*x + c*y + tx, y' = b*x + d*y + ty (pixels, y down) to NDC. */
static void uf_draw(UiFlat *u, RhiBuffer vb, unsigned count,
                    float a, float b, float c, float d, float tx, float ty) {
    float sx = 2.0f / (float)u->w, sy = -2.0f / (float)u->h;
    float M[16] = { a * sx, b * sy, 0, 0,  c * sx, d * sy, 0, 0,  0, 0, 1, 0,  tx * sx - 1.0f, ty * sy + 1.0f, 0, 1 };
    memcpy(u->params->uMVP, M, sizeof M);
    rhi_cmd_bind_vertex_buffer(0, vb, 0);
    rhi_cmd_draw(count, 0);
}

void uf_rect(UiFlat *u, float x, float y, float w, float h) {
    if (!u || !u->in_frame) return;
    uf_draw(u, u->quad, 6, w, 0, 0, h, x, y);
}

void uf_circle(UiFlat *u, float cx, float cy, float r) {
    if (!u || !u->in_frame) return;
    uf_draw(u, u->disc, UF_SEGS * 3, r, 0, 0, r, cx, cy);
}

void uf_triangle(UiFlat *u, float x0, float y0, float x1, float y1, float x2, float y2) {
    if (!u || !u->in_frame) return;
    uf_draw(u, u->tri, 3, x1 - x0, y1 - y0, x2 - x0, y2 - y0, x0, y0);
}
