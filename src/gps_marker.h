/* gps_marker.h -- the 3D GPS guidance arrow (M9 U4, AMEND-M9 "GPS marker").
 *
 * Retail guides the player with a solid arrow floating above the road in
 * front of the car [observed]. The solid ships as the object MARKER_GPS_AID in
 * GLOBAL/InGameCommon.bun: two material ranges over one 158-vertex pool, a
 * dark-blue core and a light-blue shell, both wearing texture key 0x1e232d55
 * (a 64x64 horizontal light band) whose record is authored additive
 * (usage 2, blend 2) [data]. The vertex stream carries the blue as prelight
 * [data]; the texture only shades it, so the draw is MODULATE2X of texel and
 * vertex colour, added to the frame (the tire-mark shading law, additive).
 *
 * The solid is authored pointing DOWN (tip at -Z of its own frame, the flat
 * faces along +-Y) [data]. Laid flat so the tip leads along the guidance
 * bearing [inferred]: model +X -> right of travel, +Y -> world up, -Z ->
 * forward. Placement ahead of / above the car and the bob are [inferred]
 * named constants below, to be calibrated against owner captures.
 *
 * Header-only, static functions. The GPU half needs render.h (SDL/RHI); a GL-
 * free tool defines GPS_MARKER_NO_GPU before including this and gets the
 * geometry loader and the pose math only. */
#ifndef OPENUG2_GPS_MARKER_H
#define OPENUG2_GPS_MARKER_H

#include "nfsu2.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_MARKER_BUNDLE        "GLOBAL/InGameCommon.bun"
#define GPS_MARKER_SOLID         "MARKER_GPS_AID"     /* [data] */
#define GPS_MARKER_AHEAD_M       6.0f   /* [inferred] centre ahead of the car along the bearing */
#define GPS_MARKER_HEIGHT_M      2.5f   /* [inferred] centre above the road (car contact point) */
#define GPS_MARKER_BOB_M         0.15f  /* [inferred] vertical bob amplitude */
#define GPS_MARKER_BOB_PERIOD_MS 1600u  /* [inferred] bob period */
#define GPS_MARKER_SCALE         1.0f   /* authored size is used as-is (3.2 m x 1.9 m) [data] */
/* Untextured fallback: a 1x1 texel of 128 makes MODULATE2X pass the authored
   vertex blue through unchanged. */
#define GPS_MARKER_FLAT_TEXEL    128

/* CPU geometry: only the MARKER_GPS_AID ranges, recentred on their joint
   bounding box so the pose places the arrow's middle. */
typedef struct {
    N2Scene  scene;
    float    bb_min[3], bb_max[3];   /* source bounds, before recentring */
    int      tris;
    uint32_t texkey;                 /* first range's diffuse key, 0 if none */
} GpsMarkerGeom;

static void gps_marker_geom_free(GpsMarkerGeom *g) {
    if (!g) return;
    n2_free_scene(&g->scene);
    memset(g, 0, sizeof *g);
}

/* Extract the solid from the bundle bytes. Returns the triangle count, 0 when
   the solid is absent (scene left empty). */
static int gps_marker_geom_from_bundle(const unsigned char *data, long len, GpsMarkerGeom *g) {
    memset(g, 0, sizeof *g);
    N2Scene all;
    n2_load_scene(data, len, &all, NULL, 0);
    for (int a = 0; a < 3; a++) { g->bb_min[a] = 1e30f; g->bb_max[a] = -1e30f; }
    for (int i = 0; i < all.count; i++) {
        N2Mesh *m = &all.meshes[i];
        if (strcmp(m->sname, GPS_MARKER_SOLID) || m->nidx < 3 || m->nverts < 3) continue;
        if (g->scene.count == g->scene.cap) {
            int cap = g->scene.cap ? g->scene.cap * 2 : 4;
            N2Mesh *grown = (N2Mesh *)realloc(g->scene.meshes, (size_t)cap * sizeof *grown);
            if (!grown) break;
            g->scene.meshes = grown; g->scene.cap = cap;
        }
        g->scene.meshes[g->scene.count++] = *m;   /* takes ownership of the buffers */
        memset(m, 0, sizeof *m);
    }
    n2_free_scene(&all);
    for (int i = 0; i < g->scene.count; i++) {
        const N2Mesh *m = &g->scene.meshes[i];
        if (!g->texkey) g->texkey = m->texkey;
        g->tris += m->nidx / 3;
        for (int k = 0; k < m->nidx; k++) {
            const float *p = m->verts + (size_t)m->idx[k] * 5;
            for (int a = 0; a < 3; a++) {
                if (p[a] < g->bb_min[a]) g->bb_min[a] = p[a];
                if (p[a] > g->bb_max[a]) g->bb_max[a] = p[a];
            }
        }
    }
    if (!g->tris) { gps_marker_geom_free(g); return 0; }
    float c[3];
    for (int a = 0; a < 3; a++) c[a] = 0.5f * (g->bb_min[a] + g->bb_max[a]);
    for (int i = 0; i < g->scene.count; i++) {
        N2Mesh *m = &g->scene.meshes[i];
        for (int k = 0; k < m->nverts; k++)
            for (int a = 0; a < 3; a++) m->verts[k * 5 + a] -= c[a];
    }
    return g->tris;
}

/* Reads <dataroot>/GLOBAL/InGameCommon.bun. Returns the triangle count; 0 and
   a reason on stderr when the bundle or the solid is missing. *bundle (may be
   NULL) receives the bytes for texture decoding; the caller frees them. */
static int gps_marker_geom_load(const char *dataroot, GpsMarkerGeom *g,
                                unsigned char **bundle, long *bundle_len) {
    memset(g, 0, sizeof *g);
    if (bundle) *bundle = NULL;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dataroot ? dataroot : ".", GPS_MARKER_BUNDLE);
    long len = 0;
    unsigned char *data = n2_read_file(path, &len);
    if (!data) { fprintf(stderr, "gps_marker: cannot read %s\n", path); return 0; }
    int tris = gps_marker_geom_from_bundle(data, len, g);
    if (!tris) fprintf(stderr, "gps_marker: %s has no %s solid\n", path, GPS_MARKER_SOLID);
    if (bundle && tris) { *bundle = data; if (bundle_len) *bundle_len = len; }
    else free(data);
    return tris;
}

/* Pose: centre GPS_MARKER_AHEAD_M along the bearing from the car, raised
   GPS_MARKER_HEIGHT_M plus a sine bob. bearing uses HudState.nav_bearing's
   convention: radians, atan2(dy, dx) in world XY (0 = +X, counter-clockwise).
   yaw = bearing. */
static void gps_marker_pose(const float car[3], float bearing, unsigned t_ms,
                            float pos[3], float *yaw) {
    float phase = (float)(t_ms % GPS_MARKER_BOB_PERIOD_MS) / (float)GPS_MARKER_BOB_PERIOD_MS;
    pos[0] = car[0] + GPS_MARKER_AHEAD_M * cosf(bearing);
    pos[1] = car[1] + GPS_MARKER_AHEAD_M * sinf(bearing);
    pos[2] = car[2] + GPS_MARKER_HEIGHT_M + GPS_MARKER_BOB_M * sinf(6.28318531f * phase);
    *yaw = bearing;
}

/* Column-major model matrix (render.h convention): model +X -> right of the
   heading, +Y -> world up, +Z -> backwards, so the authored -Z tip leads.
   A proper rotation (det = +1) times GPS_MARKER_SCALE. */
static void gps_marker_model(const float pos[3], float yaw, float m[16]) {
    float c = cosf(yaw), s = sinf(yaw), k = GPS_MARKER_SCALE;
    float out[16] = {
         s * k, -c * k, 0,     0,    /* model X: right   = (sin, -cos, 0) */
         0,      0,     k,     0,    /* model Y: up      = (0, 0, 1)      */
        -c * k, -s * k, 0,     0,    /* model Z: back    = -(cos, sin, 0) */
         pos[0], pos[1], pos[2], 1
    };
    memcpy(m, out, sizeof out);
}

#ifndef GPS_MARKER_NO_GPU
#include "render.h"

typedef struct GpsMarker {
    GpsMarkerGeom geom;
    GpuMesh      *gpu;
    RhiTexture    tex;        /* authored band, or the flat fallback */
    int           textured;   /* 1 = authored texture decoded */
    uint32_t      shader;     /* shader the pipeline below belongs to */
    RhiPipeline   add;        /* src-alpha + one, depth tested, no write */
} GpsMarker;

static void gps_marker_free(GpsMarker *m) {
    if (!m) return;
    free_scene_gpu(m->gpu, m->geom.scene.count);
    if (m->tex.id) rhi_texture_destroy(m->tex);
    gps_marker_geom_free(&m->geom);
    free(m);
}

/* Needs a current RHI device (call after the renderer is up). */
static GpsMarker *gps_marker_load(const char *dataroot) {
    GpsMarker *m = (GpsMarker *)calloc(1, sizeof *m);
    if (!m) return NULL;
    unsigned char *data = NULL; long len = 0;
    if (!gps_marker_geom_load(dataroot, &m->geom, &data, &len)) { free(m); return NULL; }
    m->gpu = upload_scene(&m->geom.scene);
    if (!m->gpu) {
        fprintf(stderr, "gps_marker: GPU upload failed\n");
        free(data); gps_marker_geom_free(&m->geom); free(m); return NULL;
    }
    if (m->geom.texkey) {
        N2Tpk tpk = n2_tpk_open(data, len);
        N2Tex t; memset(&t, 0, sizeof t);
        if (n2_tpk_decode(data, len, tpk, m->geom.texkey, &t)) {
            m->tex = upload_tex(&t);
            m->textured = m->tex.id != 0;
        }
        free(t.rgb); free(t.alpha); free(t.dxt); free(tpk.blk);
    }
    free(data);
    if (!m->textured) {
        N2Tex flat; memset(&flat, 0, sizeof flat);
        unsigned char rgb[3] = {GPS_MARKER_FLAT_TEXEL, GPS_MARKER_FLAT_TEXEL, GPS_MARKER_FLAT_TEXEL};
        flat.w = flat.h = 1; flat.rgb = rgb;
        m->tex = upload_tex(&flat);
        fprintf(stderr, "gps_marker: texture %08x not decoded; drawing the vertex colour flat\n",
                m->geom.texkey);
    }
    return m;
}

/* Draw with the world camera bound: rp is main's program, mvp the world
   view-projection (main.c `MVP`). Leaves uModel at identity and the shading
   switches as found (render_mark_end); the bound pipeline is the additive one. */
static void gps_marker_draw(GpsMarker *m, const RProg *rp, const float mvp[16],
                            const float car_pos[3], float bearing, unsigned t_ms) {
    if (!m || !rp || !mvp || !car_pos) return;
    if (m->shader != rp->shader.id || !m->add.id) {
        UG2CarPipelines cp;
        render_car_pipelines(rp, &cp);
        m->add = cp.halo; m->shader = rp->shader.id;
    }
    float pos[3], yaw, model[16], mv[16];
    gps_marker_pose(car_pos, bearing, t_ms, pos, &yaw);
    gps_marker_model(pos, yaw, model);
    mat_mul(mvp, model, mv);
    RMarkSaved saved;
    render_mark_begin(rp, &saved);          /* unlit, clamp(2*texel*vertex), fog */
    rhi_cmd_bind_pipeline(m->add);          /* authored additive (blend 2) */
    render_model(rp, model);                /* fog distance from world position */
    ug2_copy(rp->p->uMVP, mv, 16);
    rhi_cmd_bind_texture(0, m->tex);
    for (int i = 0; i < m->geom.scene.count; i++) render_draw_gpumesh(&m->gpu[i]);
    render_model(rp, NULL);
    render_mark_end(rp, &saved);
}
#endif /* GPS_MARKER_NO_GPU */

#endif /* OPENUG2_GPS_MARKER_H */
