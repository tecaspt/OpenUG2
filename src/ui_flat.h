/* ui_flat.h -- flat-colour 2D primitives in pixel coordinates (origin top-left, y down), drawn through rhi.h
 * with the "frontend" shader and the FeParams layout of frontend/frontend_draw.c. Used by the touch overlay. */
#ifndef OPENUG2_UI_FLAT_H
#define OPENUG2_UI_FLAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiFlat UiFlat;

/* NULL when the shader or pipeline cannot be created. Needs the rhi device. */
UiFlat *uf_create(void);
void uf_destroy(UiFlat *u);
/* Saves the rhi state, binds the pipeline and sets a full-target viewport of w x h pixels. */
void uf_begin(UiFlat *u, int w, int h);
/* Restores the state saved by uf_begin. */
void uf_end(UiFlat *u);
void uf_color(UiFlat *u, float r, float g, float b, float a);
void uf_rect(UiFlat *u, float x, float y, float w, float h);
void uf_circle(UiFlat *u, float cx, float cy, float r);
/* Outline circle: band from radius r - width to r. */
void uf_ring(UiFlat *u, float cx, float cy, float r, float width);
void uf_triangle(UiFlat *u, float x0, float y0, float x1, float y1, float x2, float y2);

#ifdef __cplusplus
}
#endif
#endif
