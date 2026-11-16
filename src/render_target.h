#ifndef RENDER_TARGET_H
#define RENDER_TARGET_H

#include "rhi.h"

/* Zero-initialize. Begin once per frame before scene drawing; present before
 * full-resolution UI. Scale 1 bypasses GPU work; failure leaves direct rendering
 * at effective_scale 1. Destroy while the device still exists. */
typedef struct RenderTarget {
    RhiSceneTarget target;
    RhiShader shader;
    RhiPipeline pipeline;
    RhiBuffer vertices;
    int drawable_w, drawable_h, scene_w, scene_h, active;
    int failed_w, failed_h;
    float effective_scale;
} RenderTarget;

int render_target_parse_scale(const char *text, float *out);
int render_target_begin(RenderTarget *, int drawable_w, int drawable_h, float scale);
void render_target_present(RenderTarget *);
void render_target_destroy(RenderTarget *);
#endif
