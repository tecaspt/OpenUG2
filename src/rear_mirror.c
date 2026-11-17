#include "rear_mirror.h"

static int prepare(RearMirror *m)
{
    static const char *const names[]={"aPosition","aUV"};
    static const float vertices[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1,
                                   -1,1,0,1, 1,-1,1,0, 1,1,1,1};
    static const char vs_gl[]="#version 120\nattribute vec2 aPosition,aUV; varying vec2 vUV; void main(){vUV=aUV;gl_Position=vec4(aPosition,0.,1.);}";
    static const char vs_es[]="attribute vec2 aPosition,aUV; varying mediump vec2 vUV; void main(){vUV=aUV;gl_Position=vec4(aPosition,0.,1.);}";
#define MIRROR_FRAGMENT "uniform sampler2D uTex0; varying mediump vec2 vUV; void main(){vec2 p=vUV; vec2 edge=min(p,1.-p)*vec2(265.,45.); float d=min(edge.x,edge.y); if(d<1.)gl_FragColor=vec4(.025,.025,.03,1.); else if(d<3.)gl_FragColor=vec4(vec3(p.y>.5?.38:.12),1.); else gl_FragColor=texture2D(uTex0,vec2(1.-p.x,p.y));}"
    static const char fs_es[]="precision mediump float; " MIRROR_FRAGMENT;
    static const char fs_gl[]="#version 120\n#define mediump\n" MIRROR_FRAGMENT;
#undef MIRROR_FRAGMENT
    RhiShaderDesc sd={0}; RhiPipelineDesc pd;
    const RhiCaps *caps=rhi_caps();
    if(!caps || !caps->scene_target)return 0;
    if(!m->target.id && rhi_scene_target_create(REAR_MIRROR_WIDTH,REAR_MIRROR_HEIGHT,&m->target)!=RHI_OK)return 0;
    sd.vertex_source=caps->backend==RHI_BACKEND_GLES2?vs_es:vs_gl;
    sd.fragment_source=caps->backend==RHI_BACKEND_GLES2?fs_es:fs_gl;
    sd.attribute_names=names;sd.attribute_count=2;sd.label="rear mirror";
    if(!m->shader.id)m->shader=rhi_shader_create(&sd);
    if(!m->vertices.id)m->vertices=rhi_buffer_create(&(RhiBufferDesc){RHI_BUFFER_VERTEX,RHI_UPDATE_STATIC,sizeof vertices,vertices});
    if(!m->shader.id || !m->vertices.id)return 0;
    rhi_pipeline_desc_init(&pd);pd.shader=m->shader;
    pd.vertex.attrib_count=2;pd.vertex.stride[0]=4*sizeof(float);
    pd.vertex.attribs[0].location=0;pd.vertex.attribs[0].format=RHI_VERTEX_FLOAT2;
    pd.vertex.attribs[1].location=1;pd.vertex.attribs[1].format=RHI_VERTEX_FLOAT2;
    pd.vertex.attribs[1].offset=2*sizeof(float);
    pd.depth_test=pd.depth_write=0;pd.cull=RHI_CULL_NONE;
    m->pipeline=rhi_pipeline_create(&pd);
    return m->pipeline.id!=0;
}
int rear_mirror_begin(RearMirror *m)
{
    if(!m || m->failed)return 0;
    rhi_cmd_save_state(&m->saved);
    if(!prepare(m) || rhi_cmd_select_scene_target(m->target)!=RHI_OK){
        m->failed=1;rhi_cmd_restore_state(&m->saved);return 0;
    }
    m->active=1;
    rhi_cmd_set_viewport(0,0,REAR_MIRROR_WIDTH,REAR_MIRROR_HEIGHT);
    return 1;
}
void rear_mirror_finish(RearMirror *m)
{
    if(!m || !m->active)return;
    rhi_cmd_unselect_scene_target();
    rhi_cmd_restore_state(&m->saved);m->active=0;
}
void rear_mirror_draw(RearMirror *m,int w,int h)
{
    RhiStateSnapshot saved;float rect[4];
    if(!m || m->failed || !m->target.id)return;
    rear_mirror_rect(w,h,rect);
    rhi_cmd_save_state(&saved);
    rhi_cmd_set_viewport((int)rect[0],(int)(h-rect[1]-rect[3]),(int)rect[2],(int)rect[3]);
    rhi_cmd_bind_pipeline(m->pipeline);
    rhi_cmd_bind_vertex_buffer(0,m->vertices,0);
    rhi_cmd_bind_texture(0,rhi_scene_target_color(m->target));
    rhi_cmd_draw(6,0);
    rhi_cmd_restore_state(&saved);
}
void rear_mirror_destroy(RearMirror *m)
{
    if(!m)return;
    rear_mirror_finish(m);
    if(m->target.id)rhi_scene_target_destroy(m->target);
    if(m->shader.id)rhi_shader_destroy(m->shader);
    if(m->vertices.id)rhi_buffer_destroy(m->vertices);
    memset(m,0,sizeof *m);
}
