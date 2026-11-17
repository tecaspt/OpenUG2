/* gps_marker.h -- the 3D GPS guidance arrow (M9 U4, AMEND-M9 "GPS marker").
 *
 * Retail guides the player with a solid arrow floating above the road in
 * front of the car [observed]. The solid ships as the object MARKER_GPS_AID in
 * GLOBAL/InGameCommon.bun: two material ranges over one 158-vertex pool, a
 * dark-blue core and a light-blue shell. The core uses its opaque arrow atlas;
 * the rim uses an additive horizontal light band [data]. The generic world
 * loader's unresolved-slot fallback assigned that band to both ranges; this
 * loader recovers each range's recorded slot. Prelight supplies the face blue
 * [data]. Channel gains match the stills [observed]; pass/lighting interpretation
 * and intermediate turning poses remain [inferred]. Source bytes stay intact.
 *
 * The solid is authored pointing DOWN (tip at -Z of its own frame, the flat
 * faces along +-Y) [data]. The fitted pose raises it far above/ahead of the
 * car; local-X pitch adjusts tilt and flattens for side bearings. Two stills
 * constrain straight/turn screen bounds [observed], but cannot identify unique
 * metric placement, interpolation or bob [inferred].
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
#define GPS_MARKER_HEIGHT_M      12.919865f /* [observed] straight-view fit; world height remains inferred */
#define GPS_MARKER_PITCH_DEG      0.45f  /* [observed] fitted tilt for that camera, not turn/animation evidence */
#define GPS_MARKER_BOB_M         0.15f  /* [inferred] vertical bob amplitude */
#define GPS_MARKER_BOB_PERIOD_MS 1600u  /* [inferred] bob period */
#define GPS_MARKER_SCALE         3.905761f /* [observed] fitted screen size; metric scale remains inferred */
/* [observed] Side-view framing fit; metric offsets/interpolation are inferred. */
#define GPS_MARKER_TURN_SIDE_M   4.1f
#define GPS_MARKER_TURN_UP_M     1.1f
#define GPS_MARKER_TURN_SCALE    1.10f

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

/* The generic world loader substitutes its last resolved texture when no
   pack-key set is supplied. Recover this solid's exact range/slot bindings. */
static int gps_marker_bind_ranges(GpsMarkerGeom *g,const uint32_t *slots,int ns,
        const N2Sub *ranges,int nr) {
    if(!g || !slots || !ranges || !g->scene.meshes || nr!=g->scene.count || nr<=0 || ns<=0)return 0;
    uint32_t chain=0;
    for(int i=0;i<nr;i++) {
        if(ranges[i].mat>=(uint32_t)ns || !slots[ranges[i].mat] ||
           ranges[i].count!=(uint32_t)g->scene.meshes[i].nidx || ranges[i].start!=chain)return 0;
        if(ranges[i].count>UINT32_MAX-chain)return 0;
        chain+=ranges[i].count;
    }
    for(int i=0;i<nr;i++)g->scene.meshes[i].texkey=slots[ranges[i].mat];
    return 1;
}
typedef struct { GpsMarkerGeom *g; int matches,valid; } GpsMarkerBindings;
static int gps_marker_bind_chunk(const unsigned char *data,uint32_t tag,long beg,long end,void *context) {
    GpsMarkerBindings *b=context;
    if(tag!=0x80134010u)return (tag>>28)==8;
    char name[128];n2_mesh_name(data,beg,end,name,sizeof name);
    if(strcmp(name,GPS_MARKER_SOLID))return 0;
    b->matches++;uint32_t slots[64];N2Sub ranges[64];
    int ns=n2_mesh_texslots(data,beg,end,slots,64),nr=n2_mesh_submeshes(data,beg,end,ranges,64);
    b->valid=gps_marker_bind_ranges(b->g,slots,ns,ranges,nr);return 0;
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
    GpsMarkerBindings bindings={g,0,0};
    asset_chunks_walk(data,0,len,gps_marker_bind_chunk,&bindings);
    if(bindings.matches!=1 || !bindings.valid){gps_marker_geom_free(g);return 0;}
    g->texkey=g->scene.meshes[0].texkey;
    float c[3];
    for (int a = 0; a < 3; a++) c[a] = 0.5f * (g->bb_min[a] + g->bb_max[a]);
    for (int i = 0; i < g->scene.count; i++) {
        N2Mesh *m = &g->scene.meshes[i];
        for (int k = 0; k < m->nverts; k++)
            for (int a = 0; a < 3; a++) m->verts[k * 5 + a] -= c[a];
    }
    return g->tris;
}

/* [inferred] Two-range material identity: the darker prelight range is the
   blue core, the brighter one its rim. Reject absent/ambiguous source colours. */
static int gps_marker_core_range(const GpsMarkerGeom *g) {
    if(!g || g->scene.count!=2 || !g->scene.meshes)return -1;
    double light[2]={0};
    for(int i=0;i<2;i++) {
        const N2Mesh *m=g->scene.meshes+i;
        if(!m->vcol || !m->idx || m->nidx<=0 || m->nverts<=0)return -1;
        for(int j=0;j<m->nidx;j++) {
            int n=m->idx[j];if(n>=m->nverts)return -1;
            for(int a=0;a<3;a++)light[i]+=m->vcol[n*4+a];
        }
        light[i]/=m->nidx;
    }
    if(fabs(light[0]-light[1])<1e-6)return -1;
    return light[0]<light[1]?0:1;
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

/* [observed] A side-facing arrow remains near the straight screen height and
   shifts toward the indicated side. Two stills constrain endpoints only; the
   smooth symmetric sine interpolation and physical offsets are [inferred]. */
static void gps_marker_transform(const GpsMarkerPose *cfg,const float car[3],
        float heading,float bearing,unsigned t_ms,float model[16]) {
    GpsMarkerPose turn=cfg?*cfg:gps_marker_defaults();
    float side=sinf(bearing-heading),weight=side*side,pos[3],anchor_yaw;
    turn.scale*=1.0f+(GPS_MARKER_TURN_SCALE-1.0f)*weight;
    turn.pitch_deg*=1.0f-weight; /* flatter when viewed side-on; straight unchanged */
    gps_marker_pose_configured(&turn,car,heading,t_ms,pos,&anchor_yaw);
    pos[0]-=GPS_MARKER_TURN_SIDE_M*side*sinf(heading);
    pos[1]+=GPS_MARKER_TURN_SIDE_M*side*cosf(heading);
    pos[2]+=GPS_MARKER_TURN_UP_M*weight;
    gps_marker_model_configured(&turn,pos,bearing,model);
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

/* [data] Source atlas, band and prelight are unchanged. Channel gains fit the
   sampled still colours [observed]; MODULATE2X/pass interpretation is inferred. */
static const unsigned char gps_marker_vs[] =
    "attribute vec3 aPos;attribute vec2 aUV;attribute vec4 aColor;\n"
    "uniform mat4 uMVP;varying vec2 vUV;varying vec4 vColor;\n"
    "void main(){vUV=aUV;vColor=aColor;gl_Position=uMVP*vec4(aPos,1.0);}\n";
static const unsigned char gps_marker_fs[] =
    "uniform sampler2D uTex;uniform vec3 uColor;varying vec2 vUV;varying vec4 vColor;\n"
    "void main(){vec4 t=texture2D(uTex,vUV);\n"
    "gl_FragColor=vec4(clamp(2.0*t.rgb*vColor.rgb*uColor,0.0,1.0),t.a*vColor.a);}\n";

typedef struct GpsMarker {
    GpsMarkerGeom geom;
    GpuMesh *gpu;
    RhiTexture tex[2];
    int core;
    RProg material;
    RhiPipeline halo,solid;
} GpsMarker;

static void gps_marker_free(GpsMarker *m) {
    if(!m)return;
    free_scene_gpu(m->gpu,m->geom.scene.count);
    for(int i=0;i<2;i++)if(m->tex[i].id)rhi_texture_destroy(m->tex[i]);
    if(m->material.shader.id)rhi_shader_destroy(m->material.shader);
    gps_marker_geom_free(&m->geom);free(m);
}

static GpsMarker *gps_marker_load(const char *dataroot) {
    GpsMarker *m=calloc(1,sizeof *m);if(!m)return NULL;
    unsigned char *data=NULL;long len=0;
    if(!gps_marker_geom_load(dataroot,&m->geom,&data,&len)){free(m);return NULL;}
    m->core=gps_marker_core_range(&m->geom);
    if(m->core<0){fprintf(stderr,"gps_marker: unsupported material ranges\n");free(data);gps_marker_free(m);return NULL;}
    N2Tpk tpk=n2_tpk_open(data,len);
    for(int i=0;i<2;i++) {
        N2Tex tex={0};
        if(n2_tpk_decode(data,len,tpk,m->geom.scene.meshes[i].texkey,&tex))m->tex[i]=upload_tex(&tex);
        free(tex.rgb);free(tex.alpha);free(tex.dxt);
    }
    free(tpk.blk);free(data);
    if(!m->tex[0].id || !m->tex[1].id){fprintf(stderr,"gps_marker: authored textures unavailable\n");gps_marker_free(m);return NULL;}
    m->gpu=upload_scene(&m->geom.scene);
    static const char *const attributes[]={"aPos","aUV","aNor","aColor"};
    static const RhiParamLayout *const blocks[]={&ug2_params_layout};
    m->material.shader=render_shader_create("GPS marker",GLSL_VS_HEADER,gps_marker_vs,sizeof gps_marker_vs-1,
        GLSL_FS_HEADER,gps_marker_fs,sizeof gps_marker_fs-1,attributes,4,blocks,1);
    m->material.p=rhi_shader_params(m->material.shader,0);
    if(!m->gpu || !m->material.shader.id || !m->material.p){gps_marker_free(m);return NULL;}
    UG2CarPipelines cp;render_car_pipelines(&m->material,&cp);
    m->solid=cp.opaque[RHI_CULL_NONE];m->halo=cp.halo;
    if(!m->solid.id || !m->halo.id){gps_marker_free(m);return NULL;}
    return m;
}

/* Private shader/params and complete RHI snapshot keep the world/HUD state
   untouched. Source geometry, prelight and texture bytes are never rewritten. */
static void gps_marker_draw(GpsMarker *m,const RProg *rp,const float mvp[16],
        const float car_pos[3],float heading,float bearing,unsigned t_ms,const GpsMarkerPose *cfg) {
    if(!m || !rp || !mvp || !car_pos)return;
    float model[16];gps_marker_transform(cfg,car_pos,heading,bearing,t_ms,model);
    RhiStateSnapshot saved;rhi_cmd_save_state(&saved);
    UG2Params *p=m->material.p;
    mat_mul(mvp,model,p->uMVP);ug2_copy(p->uModel,model,16);p->uTex=0;
    /* Opaque atlas first; the authored band then lights its front faces as
       well as the outline. Reversing this order erased the cyan inner band. */
    ug2_vec3(p->uColor,1.8f,.67f,1.18f);
    rhi_cmd_bind_pipeline(m->solid);rhi_cmd_bind_texture(0,m->tex[m->core]);
    render_draw_gpumesh(m->gpu+m->core);
    ug2_vec3(p->uColor,1.8f,1.45f,1.0f);
    rhi_cmd_bind_pipeline(m->halo);rhi_cmd_bind_texture(0,m->tex[1-m->core]);
    render_draw_gpumesh(m->gpu+1-m->core);
    rhi_cmd_restore_state(&saved);
}

#endif /* GPS_MARKER_NO_GPU */

#endif /* OPENUG2_GPS_MARKER_H */
