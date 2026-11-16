/* gps_marker.h -- the 3D GPS guidance arrow (M9 U4, AMEND-M9 "GPS marker").
 *
 * Retail guides the player with a solid arrow floating above the road in
 * front of the car [observed]. The solid ships as the object MARKER_GPS_AID in
 * GLOBAL/InGameCommon.bun: two material ranges over one 158-vertex pool, a
 * dark-blue core and a light-blue shell, both wearing texture key 0x1e232d55
 * (a 64x64 horizontal light band) whose record is authored additive
 * (usage 2, blend 2) [data]. The vertex stream carries the blue as prelight
 * [data]. The solid has opaque vertex alpha [data]; the owner still shows an
 * opaque dark-blue core [observed]. Use depth-writing opaque solid passes
 * with MODULATE2X prelight [inferred]; adding both faces washes the core out.
 *
 * The solid is authored pointing DOWN (tip at -Z of its own frame, the flat
 * faces along +-Y) [data]. The fitted pose raises it far above/ahead of the
 * car; local-X pitch adjusts tilt. The straight still constrains screen bounds
 * [observed], but cannot identify unique metric placement or bob [inferred].
 * See GPS-ARROW.md for the calibration assumptions and remaining captures.
 *
 * Header-only, static functions. The GPU half needs render.h (SDL/RHI); a GL-
 * free tool defines GPS_MARKER_NO_GPU before including this and gets the
 * geometry loader and the pose math only. */
#ifndef OPENUG2_GPS_MARKER_H
#define OPENUG2_GPS_MARKER_H

#include "nfsu2.h"
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_MARKER_BUNDLE        "GLOBAL/InGameCommon.bun"
#define GPS_MARKER_SOLID         "MARKER_GPS_AID"     /* [data] */
#define GPS_MARKER_AHEAD_M       30.0f  /* [inferred] metric gauge for the observed straight-view fit */
#define GPS_MARKER_HEIGHT_M      12.285345f /* [observed] straight-view fit; world height remains inferred */
#define GPS_MARKER_PITCH_DEG     -2.75f  /* [observed] fitted tilt for that camera, not turn/animation evidence */
#define GPS_MARKER_BOB_M         0.15f  /* [inferred] vertical bob amplitude */
#define GPS_MARKER_BOB_PERIOD_MS 1600u  /* [inferred] bob period */
#define GPS_MARKER_SCALE         3.540327f /* [observed] fitted screen size; metric scale remains inferred */
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

/* [inferred] World-space fit parameters. Positive pitch raises the authored tip;
   negative pitch lowers it. Pitch is about the marker's local right axis. */
typedef struct GpsMarkerPose { float ahead_m,height_m,pitch_deg,scale; } GpsMarkerPose;
static GpsMarkerPose gps_marker_defaults(void) {
    return (GpsMarkerPose){GPS_MARKER_AHEAD_M,GPS_MARKER_HEIGHT_M,GPS_MARKER_PITCH_DEG,GPS_MARKER_SCALE};
}

/* Atomic strict parser: exactly four finite comma-separated values. Bounds are
   calibration safeguards [inferred], not retail limits. */
static int gps_marker_parse(const char *text,GpsMarkerPose *out) {
    if (!text || !out) return 0;
    float v[4]; const char *p=text;
    for (int i=0;i<4;i++) {
        char *end; errno=0; v[i]=strtof(p,&end);
        if (end==p || errno==ERANGE || !isfinite(v[i])) return 0;
        p=end; while (isspace((unsigned char)*p)) p++;
        if (i<3) { if (*p!=',') return 0; p++; }
        else if (*p) return 0;
    }
    if (v[0]<0 || v[0]>1000 || v[1]<-1000 || v[1]>1000 ||
        v[2]<-180 || v[2]>180 || v[3]<=0 || v[3]>100) return 0;
    *out=(GpsMarkerPose){v[0],v[1],v[2],v[3]};return 1;
}
static int gps_marker_visible(int guidance,int racing,int paused,int frontend) {
    return guidance && !racing && !paused && !frontend;
}

/* Bearing: atan2(dy,dx), radians in world XY. Bob retains its uncalibrated
   amplitude/period until moving retail references establish them [inferred]. */
static void gps_marker_pose_configured(const GpsMarkerPose *cfg,const float car[3],
        float bearing,unsigned t_ms,float pos[3],float *yaw) {
    GpsMarkerPose d=gps_marker_defaults(); if (!cfg) cfg=&d;
    float phase=(float)(t_ms%GPS_MARKER_BOB_PERIOD_MS)/(float)GPS_MARKER_BOB_PERIOD_MS;
    pos[0]=car[0]+cfg->ahead_m*cosf(bearing);
    pos[1]=car[1]+cfg->ahead_m*sinf(bearing);
    pos[2]=car[2]+cfg->height_m+GPS_MARKER_BOB_M*sinf(6.28318531f*phase);
    *yaw=bearing;
}
static void gps_marker_pose(const float car[3],float bearing,unsigned t_ms,float pos[3],float *yaw) {
    gps_marker_pose_configured(NULL,car,bearing,t_ms,pos,yaw);
}

/* Column-major proper rotation, followed by uniform positive scale. At pitch0:
   model X=right, Y=up, Z=back, and the authored -Z tip leads [inferred]. */
static void gps_marker_model_configured(const GpsMarkerPose *cfg,const float pos[3],float yaw,float m[16]) {
    GpsMarkerPose d=gps_marker_defaults(); if (!cfg) cfg=&d;
    float c=cosf(yaw),s=sinf(yaw),k=cfg->scale;
    float p=cfg->pitch_deg*0.017453292519943295f,cp=cosf(p),sp=sinf(p);
    float out[16]={s*k,-c*k,0,0,
        -c*sp*k,-s*sp*k,cp*k,0,
        -c*cp*k,-s*cp*k,-sp*k,0,
        pos[0],pos[1],pos[2],1};
    memcpy(m,out,sizeof out);
}
static void gps_marker_model(const float pos[3],float yaw,float m[16]) {
    gps_marker_model_configured(NULL,pos,yaw,m);
}

/* [inferred] Placement follows the car so a turning route cannot move the
   whole guide outside the view. Orientation independently follows the route. */
static void gps_marker_transform(const GpsMarkerPose *cfg,const float car[3],
        float heading,float bearing,unsigned t_ms,float model[16]) {
    float pos[3],anchor_yaw;
    gps_marker_pose_configured(cfg,car,heading,t_ms,pos,&anchor_yaw);
    gps_marker_model_configured(cfg,pos,bearing,model);
}

/* Calibration diagnostic: projected geometry bounds in drawable pixels; no
   clipping or occlusion claim. Returns0 if any vertex is behind the camera. */
static int gps_marker_project_bounds(const GpsMarkerGeom *g,const float model[16],
        const float vp[16],int w,int h,float box[4]) {
    if (!g || !model || !vp || !box || w<=0 || h<=0) return 0;
    float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};int count=0;
    for (int i=0;i<g->scene.count;i++) {
        const N2Mesh *mesh=g->scene.meshes+i;
        for (int j=0;j<mesh->nidx;j++) {
            const float *p=mesh->verts+(size_t)mesh->idx[j]*5;
            float world[4],clip[4];
            for (int a=0;a<4;a++) world[a]=model[a]*p[0]+model[4+a]*p[1]+model[8+a]*p[2]+model[12+a];
            for (int a=0;a<4;a++) clip[a]=vp[a]*world[0]+vp[4+a]*world[1]+vp[8+a]*world[2]+vp[12+a]*world[3];
            if (!isfinite(clip[3]) || clip[3]<=1e-6f) return 0;
            float q[2]={(clip[0]/clip[3]+1)*.5f*w,(1-clip[1]/clip[3])*.5f*h};
            for(int a=0;a<2;a++){if(!isfinite(q[a]))return 0;if(q[a]<lo[a])lo[a]=q[a];if(q[a]>hi[a])hi[a]=q[a];}
            count++;
        }
    }
    if(!count)return 0;
    box[0]=lo[0];box[1]=lo[1];box[2]=hi[0];box[3]=hi[1];return 1;
}

#ifndef GPS_MARKER_NO_GPU
#include "render.h"

typedef struct GpsMarker {
    GpsMarkerGeom geom;
    GpuMesh      *gpu;
    RhiTexture    tex;        /* authored band, or the flat fallback */
    int           textured;   /* 1 = authored texture decoded */
    uint32_t      shader;     /* shader the pipeline below belongs to */
    RhiPipeline   solid;      /* [inferred] opaque/depth-writing, matched to the captured solid */
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
   switches as found (render_mark_end); the bound pipeline is the opaque solid pass. */
static void gps_marker_draw(GpsMarker *m, const RProg *rp, const float mvp[16],
                            const float car_pos[3], float heading, float bearing, unsigned t_ms,const GpsMarkerPose *cfg) {
    if (!m || !rp || !mvp || !car_pos) return;
    if (m->shader != rp->shader.id || !m->solid.id) {
        UG2CarPipelines cp;
        render_car_pipelines(rp, &cp);
        m->solid = cp.opaque[RHI_CULL_NONE]; m->shader = rp->shader.id;
    }
    float model[16], mv[16];
    gps_marker_transform(cfg,car_pos,heading,bearing,t_ms,model);
    mat_mul(mvp, model, mv);
    RMarkSaved saved;
    render_mark_begin(rp, &saved);          /* unlit, clamp(2*texel*vertex), fog */
    rhi_cmd_bind_pipeline(m->solid);        /* observed opaque core; no additive face accumulation */
    render_model(rp, model);                /* fog distance from world position */
    ug2_copy(rp->p->uMVP, mv, 16);
    rhi_cmd_bind_texture(0, m->tex);
    for (int i = 0; i < m->geom.scene.count; i++) render_draw_gpumesh(&m->gpu[i]);
    render_model(rp, NULL);
    render_mark_end(rp, &saved);
}
#endif /* GPS_MARKER_NO_GPU */

#endif /* OPENUG2_GPS_MARKER_H */
