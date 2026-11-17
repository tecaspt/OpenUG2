#ifndef REAR_MIRROR_H
#define REAR_MIRROR_H
#include "rhi.h"
#include <math.h>
#include <string.h>

#define REAR_MIRROR_WIDTH 256
#define REAR_MIRROR_HEIGHT 40
#define REAR_MIRROR_DISTANCE 220.f

typedef struct RearMirror {
    RhiSceneTarget target;
    RhiShader shader;
    RhiPipeline pipeline;
    RhiBuffer vertices;
    RhiStateSnapshot saved;
    int failed, active;
} RearMirror;

static inline int rear_mirror_parse(const char *s, int *value)
{
    if (!s || !value) return 0;
    if (!strcmp(s,"on")) { *value=1; return 1; }
    if (!strcmp(s,"off")) { *value=0; return 1; }
    return 0;
}
static inline int rear_mirror_visible(int enabled, int overlay, int presentation,
                                      int detached, int diagnostic)
{
    return enabled && !overlay && !presentation && !detached && !diagnostic;
}
/* Pixel rectangle, top-left origin. Height-based scaling preserves the
 * observed panel aspect on wide displays and the scene render-scale path. */
static inline void rear_mirror_rect(int w, int h, float rect[4])
{
    float scale=h/643.f;
    rect[2]=265.f*scale; rect[3]=45.f*scale;
    rect[0]=(w-rect[2])*.5f; rect[1]=75.f*scale;
}
static inline void rear_mirror_camera(const float pos[3], float heading,
                                      float roof, float eye[3], float direction[3])
{
    eye[0]=pos[0]; eye[1]=pos[1]; eye[2]=pos[2]+roof+.3f;
    direction[0]=-cosf(heading); direction[1]=-sinf(heading); direction[2]=-.035f;
}
/* Call before selecting the scaled main scene; finish restores the main
 * target and all saved command state. Disabled frames never call begin. */
int rear_mirror_begin(RearMirror *);
void rear_mirror_finish(RearMirror *);
void rear_mirror_draw(RearMirror *, int width, int height);
void rear_mirror_destroy(RearMirror *);
#endif
