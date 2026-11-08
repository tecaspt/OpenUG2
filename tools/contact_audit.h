/* Read-only world-contact attribution for race_events_check (issue #8 WP2).
 *
 * Records every wall/rail contact the production AI solver returns through
 * g_ai_wall_audit_hook, attributes it to its scene mesh and triangle, and for
 * each repeated source gathers geometry/material evidence:
 *   - packed BUN polygon: source offset, section, activation group, flags,
 *     surface attribute, corners; closure (event-owned group) vs static;
 *   - placed render mesh: type/asset name, model keys, 0x34103 row
 *     (translation, bounds, rotation/scale), scenery-group membership;
 *   - visible backing: fraction of the source face within 0.5 m of a drawn
 *     (non-collision) triangle, and which placement owns that triangle;
 *   - exact duplicates and road support on both sides of the face.
 * The classification is derived from that evidence only; names are reported
 * but never consulted. It observes contacts; it never alters the solver. */
#ifndef OPENUG2_CONTACT_AUDIT_H
#define OPENUG2_CONTACT_AUDIT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ai.h"
#include "world.h"
#include "world_collision.h"
#include "world_group_reader.h"

enum { CA_REPEAT_HITS=10, CA_EPISODE_GAP=30, CA_SAMPLES=40 };
#define CA_BACKING_DIST .5f

typedef struct {
    int mesh,tri,kind;          /* kind 0 wall list, 1 rail/mesh push */
    long hits,episodes,first,last;
    unsigned cars,phases;
    float pos[3],nx,ny,maxpen,span;
} CaSource;
typedef struct { uint64_t id; int index; } CaPoly;
typedef struct {
    FILE *log;
    int event,freeroam;
    char region[16];
    const N2Path *course;      /* the driven race course, for crossing tests */
    long frame;
    const N2Scene *scene;
    const UG2TrackCollision *tc;
    const AiCar *cars;
    CaPoly *poly; size_t npoly;
    CaSource *src; int nsrc,cap;
    long contacts,rails,unattributed;
    const unsigned char *stream; long stream_len;
    unsigned char *companion; long companion_len;
} ContactAudit;
static ContactAudit g_ca;

static int ca_poly_cmp(const void *a,const void *b) {
    uint64_t x=((const CaPoly*)a)->id,y=((const CaPoly*)b)->id;return (x>y)-(x<y);
}
static int ca_is_polygon(const N2Mesh *m) {
    return m->cat==N2_COLLISION && !strcmp(m->sname,"BUN_TRACK_POLYGON");
}
static int ca_polygon(const N2Mesh *m) {
    if(!ca_is_polygon(m) || !g_ca.poly)return -1;
    CaPoly key={m->placement_id,0};
    const CaPoly *p=bsearch(&key,g_ca.poly,g_ca.npoly,sizeof key,ca_poly_cmp);
    return p?p->index:-1;
}
static const char *ca_group(int poly) {
    if(poly<0 || !g_ca.tc)return "-";
    unsigned g=g_ca.tc->polygons[poly].metadata[0];
    return g<g_ca.tc->activation.count?(const char*)g_ca.tc->activation.records[g]+9:"?";
}
/* static = present in free roam; closure = an event-owned group. */
static const char *ca_class(int mesh) {
    const N2Mesh *m=g_ca.scene->meshes+mesh;
    if(m->prop_id)return "prop";
    int poly=ca_polygon(m);
    if(poly>=0)return wg_event_id(ca_group(poly))>0?"closure":"static";
    return "mesh-static";
}

static void ca_begin(const char *region,int event,int freeroam,const N2Scene *scene,
                     const UG2TrackCollision *tc,const AiCar *cars,
                     const unsigned char *stream,long stream_len,const char *root) {
    free(g_ca.poly);free(g_ca.src);free(g_ca.companion);
    FILE *log=g_ca.log;memset(&g_ca,0,sizeof g_ca);g_ca.log=log;
    g_ca.event=event;g_ca.freeroam=freeroam;
    snprintf(g_ca.region,sizeof g_ca.region,"%s",!strncmp(region,"STREAM",6)?region+6:region);g_ca.scene=scene;g_ca.tc=tc;g_ca.cars=cars;
    g_ca.stream=stream;g_ca.stream_len=stream_len;
    if(tc && tc->count) {
        uint32_t bundle=wce_bundle_hash(region);
        g_ca.poly=malloc(tc->count*sizeof *g_ca.poly);
        if(g_ca.poly) {
            for(size_t k=0;k<tc->count;k++)
                g_ca.poly[k]=(CaPoly){wce_polygon_id(bundle,tc->polygons[k].source_offset),(int)k};
            g_ca.npoly=tc->count;qsort(g_ca.poly,g_ca.npoly,sizeof *g_ca.poly,ca_poly_cmp);
        }
    }
    char path[1024];
    const char *stem=!strncmp(region,"STREAM",6)?region+6:region;
    snprintf(path,sizeof path,"%s/%s.BUN",root,stem);
    g_ca.companion=n2_read_file(path,&g_ca.companion_len);
}

static void ca_record(const AiCar *car,int mesh,int tri,int kind,int phase,
                      float nx,float ny,float pen,float span) {
    if(!g_ca.scene || mesh<0 || mesh>=g_ca.scene->count){g_ca.unattributed++;return;}
    int car_index=g_ca.cars?(int)(car-g_ca.cars):-1;
    int k=0;
    for(;k<g_ca.nsrc;k++)if(g_ca.src[k].mesh==mesh && g_ca.src[k].tri==tri && g_ca.src[k].kind==kind)break;
    if(k==g_ca.nsrc) {
        if(g_ca.nsrc==g_ca.cap) {
            int cap=g_ca.cap?g_ca.cap*2:64;
            CaSource *grown=realloc(g_ca.src,(size_t)cap*sizeof *grown);
            if(!grown)return;
            g_ca.src=grown;g_ca.cap=cap;
        }
        CaSource *s=g_ca.src+g_ca.nsrc++;memset(s,0,sizeof *s);
        s->mesh=mesh;s->tri=tri;s->kind=kind;s->first=g_ca.frame;s->last=-1000000;
        memcpy(s->pos,car->pos,sizeof s->pos);s->nx=nx;s->ny=ny;s->span=span;
    }
    CaSource *s=g_ca.src+k;
    if(s->last!=g_ca.frame)s->hits++;
    if(g_ca.frame-s->last>CA_EPISODE_GAP)s->episodes++;
    s->last=g_ca.frame;
    if(car_index>=0 && car_index<32)s->cars|=1u<<car_index;
    s->phases|=1u<<phase;
    if(pen>s->maxpen)s->maxpen=pen;
    if(g_ca.log)
        fprintf(g_ca.log,"CONTACT\t%d\t%s\t%ld\t%d\t%d\t%s\t%d\t%d\t%s\t%.3f\t%.3f\t%.3f\t%+.3f\t%+.3f\t%.4f\t%.3f\n",
            g_ca.event,g_ca.freeroam?"free":"race",g_ca.frame,car_index,phase,kind?"rail":"wall",
            mesh,tri,ca_class(mesh),car->pos[0],car->pos[1],car->pos[2],nx,ny,pen,span);
}

static void ca_hook(const AiCar *car,const PhysWallContact *walls,int nwalls,
                    int rail_mesh,int rail_tri,int nrails,int phase) {
    for(int k=0;k<nwalls && k<8;k++) {
        g_ca.contacts++;
        ca_record(car,walls[k].mesh,walls[k].tri,0,phase,walls[k].nx,walls[k].ny,walls[k].pen,walls[k].span);
    }
    if(nwalls>8)g_ca.unattributed+=nwalls-8;
    if(nrails) {
        g_ca.rails++;
        if(rail_mesh>=0)ca_record(car,rail_mesh,rail_tri,1,phase,0,0,0,0);
        else g_ca.unattributed++;
    }
}

/* ---- evidence ---- */
static void ca_face(const N2Mesh *m,int tri,const float *v[3]) {
    const float *verts=m->wall_verts?m->wall_verts:m->verts;
    const uint16_t *idx=m->wall_verts?m->wall_idx:m->idx;
    for(int k=0;k<3;k++)v[k]=verts+idx[tri*3+k]*5;
}
static float ca_point_tri_d2(const float p[3],const float *a,const float *b,const float *c) {
    /* Ericson, closest point on triangle */
    float ab[3],ac[3],ap[3],q[3];
    for(int i=0;i<3;i++){ab[i]=b[i]-a[i];ac[i]=c[i]-a[i];ap[i]=p[i]-a[i];}
    float d1=ab[0]*ap[0]+ab[1]*ap[1]+ab[2]*ap[2],d2=ac[0]*ap[0]+ac[1]*ap[1]+ac[2]*ap[2];
    if(d1<=0 && d2<=0){memcpy(q,a,sizeof q);goto done;}
    float bp[3];for(int i=0;i<3;i++)bp[i]=p[i]-b[i];
    float d3=ab[0]*bp[0]+ab[1]*bp[1]+ab[2]*bp[2],d4=ac[0]*bp[0]+ac[1]*bp[1]+ac[2]*bp[2];
    if(d3>=0 && d4<=d3){memcpy(q,b,sizeof q);goto done;}
    float vc=d1*d4-d3*d2;
    if(vc<=0 && d1>=0 && d3<=0){float t=d1/(d1-d3);for(int i=0;i<3;i++)q[i]=a[i]+t*ab[i];goto done;}
    float cp[3];for(int i=0;i<3;i++)cp[i]=p[i]-c[i];
    float d5=ab[0]*cp[0]+ab[1]*cp[1]+ab[2]*cp[2],d6=ac[0]*cp[0]+ac[1]*cp[1]+ac[2]*cp[2];
    if(d6>=0 && d5<=d6){memcpy(q,c,sizeof q);goto done;}
    float vb=d5*d2-d1*d6;
    if(vb<=0 && d2>=0 && d6<=0){float t=d2/(d2-d6);for(int i=0;i<3;i++)q[i]=a[i]+t*ac[i];goto done;}
    float va=d3*d6-d5*d4;
    if(va<=0 && d4-d3>=0 && d5-d6>=0) {
        float t=(d4-d3)/((d4-d3)+(d5-d6));for(int i=0;i<3;i++)q[i]=b[i]+t*(c[i]-b[i]);goto done;
    }
    {float den=1/(va+vb+vc),v=vb*den,w=vc*den;for(int i=0;i<3;i++)q[i]=a[i]+ab[i]*v+ac[i]*w;}
done:
    {float dx=p[0]-q[0],dy=p[1]-q[1],dz=p[2]-q[2];return dx*dx+dy*dy+dz*dz;}
}
/* Drawn geometry: every scene mesh except invisible collision-only members. */
static int ca_drawn(const N2Mesh *m) { return m->cat!=N2_COLLISION && m->nidx>=3; }

typedef struct {
    float coverage;      /* share of car-height face samples within 0.5 m of drawn geometry */
    int samples;         /* face samples inside the car-height band */
    float course_d;      /* XY distance from the face footprint to the course line */
    int backing;         /* mesh owning most covered samples, -1 none */
    float backing_d;     /* that mesh's mean sample distance */
    int road_left,road_right;   /* ROAD/TERRAIN support at base height either side */
    int duplicates;      /* other sources with the same face within 5 cm */
} CaEvidence;

static float ca_seg_d2(float px,float py,float ax,float ay,float bx,float by) {
    float dx=bx-ax,dy=by-ay,l=dx*dx+dy*dy,t=l>1e-12f?((px-ax)*dx+(py-ay)*dy)/l:0;
    t=t<0?0:t>1?1:t;float ex=px-ax-t*dx,ey=py-ay-t*dy;return ex*ex+ey*ey;
}
static int ca_seg_cross(const float *a,const float *b,const float *c,const float *d) {
    float d1=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
    float d2=(b[0]-a[0])*(d[1]-a[1])-(b[1]-a[1])*(d[0]-a[0]);
    float d3=(d[0]-c[0])*(a[1]-c[1])-(d[1]-c[1])*(a[0]-c[0]);
    float d4=(d[0]-c[0])*(b[1]-c[1])-(d[1]-c[1])*(b[0]-c[0]);
    return ((d1>0)!=(d2>0)) && ((d3>0)!=(d4>0));
}
/* XY distance between the face's widest horizontal extent and the course
 * polyline; zero when the footprint crosses the driven line. */
static float ca_course_distance(const float *v[3]) {
    const N2Path *c=g_ca.course;
    if(!c || c->n<2)return -1;
    int i0=0,i1=1;float best=-1;
    for(int i=0;i<3;i++)for(int j=i+1;j<3;j++){
        float d=hypotf(v[i][0]-v[j][0],v[i][1]-v[j][1]);if(d>best){best=d;i0=i;i1=j;}}
    const float *a=v[i0],*b=v[i1];float d2=1e30f;
    for(int k=0;k+1<c->n;k++) {
        const float *p=c->xy+2*k,*q=c->xy+2*k+2;
        if(ca_seg_cross(a,b,p,q))return 0;
        d2=fminf(d2,ca_seg_d2(p[0],p[1],a[0],a[1],b[0],b[1]));
        d2=fminf(d2,ca_seg_d2(a[0],a[1],p[0],p[1],q[0],q[1]));
    }
    return sqrtf(d2);
}

/* zc: car reference Z at the first contact. Coverage and road support are
 * measured in the car's own height band, not over a 254 m boundary plane. */
static void ca_evidence(int mesh,int tri,const float at[3],CaEvidence *e) {
    const float zc=at[2];
    const N2Scene *s=g_ca.scene;const N2Mesh *m=s->meshes+mesh;
    memset(e,0,sizeof *e);e->backing=-1;
    const float *v[3];ca_face(m,tri,v);
    float lo[3],hi[3];
    for(int i=0;i<3;i++){lo[i]=fminf(v[0][i],fminf(v[1][i],v[2][i]));hi[i]=fmaxf(v[0][i],fmaxf(v[1][i],v[2][i]));}
    e->course_d=ca_course_distance(v);
    /* barycentric samples over the face, kept inside the car-height band;
       a boundary plane spanning hundreds of metres is refined until the
       band holds samples. */
    static float pts[CA_SAMPLES*CA_SAMPLES*64][3];int np=0;
    for(int grid=CA_SAMPLES;grid<=CA_SAMPLES*8 && np<12;grid*=2) {
        np=0;
        for(int a=0;a<grid;a++)for(int b=0;a+b<grid;b++) {
            float u=(a+1.0f/3)/grid,w=(b+1.0f/3)/grid,t=1-u-w;
            if(t<0)continue;
            float q[3];for(int i=0;i<3;i++)q[i]=v[0][i]*t+v[1][i]*u+v[2][i]*w;
            if(q[2]<zc-.3f || q[2]>zc+2.0f)continue;
            memcpy(pts[np++],q,sizeof q);
        }
    }
    e->samples=np;
    lo[2]=fmaxf(lo[2],zc-.3f);hi[2]=fminf(hi[2],zc+2.0f);
    int *owner=calloc((size_t)np,sizeof *owner);float *best=malloc((size_t)np*sizeof *best);
    int *count=calloc((size_t)s->count,sizeof *count);
    if(!owner || !best || !count){free(owner);free(best);free(count);return;}
    for(int k=0;k<np;k++){best[k]=CA_BACKING_DIST*CA_BACKING_DIST;owner[k]=-1;}
    const float r=CA_BACKING_DIST;
    for(int n=0;n<s->count;n++) {
        const N2Mesh *o=s->meshes+n;
        if(n==mesh || !ca_drawn(o))continue;
        float ol[3]={1e30f,1e30f,1e30f},oh[3]={-1e30f,-1e30f,-1e30f};
        for(int q=0;q<o->nverts;q++)for(int i=0;i<3;i++){
            float x=o->verts[q*5+i];ol[i]=fminf(ol[i],x);oh[i]=fmaxf(oh[i],x);}
        int far=0;for(int i=0;i<3;i++)far|=ol[i]>hi[i]+r || oh[i]<lo[i]-r;
        if(far)continue;
        for(int t=0;t+2<o->nidx;t+=3) {
            const float *a=o->verts+o->idx[t]*5,*b=o->verts+o->idx[t+1]*5,*c=o->verts+o->idx[t+2]*5;
            for(int k=0;k<np;k++) {
                float d2=ca_point_tri_d2(pts[k],a,b,c);
                if(d2<best[k]){best[k]=d2;owner[k]=n;}
            }
        }
    }
    int covered=0;
    for(int k=0;k<np;k++)if(owner[k]>=0){covered++;count[owner[k]]++;}
    e->coverage=np?(float)covered/np:0;
    int top=-1;for(int n=0;n<s->count;n++)if(count[n] && (top<0 || count[n]>count[top]))top=n;
    e->backing=top;
    if(top>=0){float sum=0;int c=0;for(int k=0;k<np;k++)if(owner[k]==top){sum+=sqrtf(best[k]);c++;}e->backing_d=c?sum/c:0;}
    free(owner);free(best);free(count);
    /* road support 1.5 m either side of the face, at the contact height */
    float nx=(v[1][1]-v[0][1])*(v[2][2]-v[0][2])-(v[1][2]-v[0][2])*(v[2][1]-v[0][1]);
    float ny=(v[1][2]-v[0][2])*(v[2][0]-v[0][0])-(v[1][0]-v[0][0])*(v[2][2]-v[0][2]);
    float nl=hypotf(nx,ny);
    if(nl>1e-6f) {
        nx/=nl;ny/=nl;
        /* the footprint point nearest the contact, offset along the normal */
        int i0=0,i1=1;float span=-1;
        for(int i=0;i<3;i++)for(int j=i+1;j<3;j++){
            float d=hypotf(v[i][0]-v[j][0],v[i][1]-v[j][1]);if(d>span){span=d;i0=i;i1=j;}}
        float ex=v[i1][0]-v[i0][0],ey=v[i1][1]-v[i0][1],el=ex*ex+ey*ey;
        float t=el>1e-12f?((at[0]-v[i0][0])*ex+(at[1]-v[i0][1])*ey)/el:0;t=t<0?0:t>1?1:t;
        float cx=v[i0][0]+t*ex,cy=v[i0][1]+t*ey;
        for(int side=0;side<2;side++) {
            float sg=side?-1.f:1.f,z=zc+.3f;
            WGroundHit h;
            int cat=world_ground_hit(s,cx+sg*1.5f*nx,cy+sg*1.5f*ny,z,&h);
            int ok=(cat==WSURF_ROAD || cat==WSURF_TERRAIN) && fabsf(h.z-zc)<1.0f;
            if(side)e->road_right=ok?(cat==WSURF_ROAD?2:1):0;else e->road_left=ok?(cat==WSURF_ROAD?2:1):0;
        }
    }
    /* exact duplicates among non-drawn collision members and same-model placements */
    for(int n=0;n<s->count;n++) {
        const N2Mesh *o=s->meshes+n;
        if(n==mesh)continue;
        if(ca_is_polygon(m) && ca_is_polygon(o)) {
            int same=0;
            for(int q=0;q<o->nverts && q<4;q++)for(int p=0;p<m->nverts && p<4;p++) {
                float dx=o->verts[q*5]-m->verts[p*5],dy=o->verts[q*5+1]-m->verts[p*5+1],dz=o->verts[q*5+2]-m->verts[p*5+2];
                if(dx*dx+dy*dy+dz*dz<.0025f){same++;break;}
            }
            if(same>=4)e->duplicates++;
        } else if(!ca_is_polygon(m) && o->world_model_key && o->world_model_key==m->world_model_key &&
                  o->placement_id!=m->placement_id && o->nverts==m->nverts && m->nverts>0) {
            float dx=o->verts[0]-m->verts[0],dy=o->verts[1]-m->verts[1],dz=o->verts[2]-m->verts[2];
            if(dx*dx+dy*dy+dz*dz<.0025f)e->duplicates++;
        }
    }
}

/* ---- 0x34103 placement row and group membership ---- */
typedef struct { uint32_t want; int found; WInstPlacement p; char type[33]; } CaRow;
static int ca_row_visit(const WInstPlacement *p,const char *type,void *user) {
    CaRow *row=user;
    if(p->placement_id!=row->want)return 1;
    row->p=*p;snprintf(row->type,sizeof row->type,"%s",type);row->found=1;return 0;
}
typedef struct { unsigned section,row; char names[160]; } CaGroups;
static int ca_group_visit(const WGMember *m,void *user) {
    CaGroups *g=user;
    if(m->section!=g->section || m->instance!=g->row)return 1;
    size_t n=strlen(g->names);
    snprintf(g->names+n,sizeof g->names-n,"%s%s",n?",":"",m->group_name);
    return 1;
}
static void ca_placement(FILE *out,const char *tag,const N2Mesh *m) {
    if(!m->placement_id || ca_is_polygon(m)){fprintf(out,"    %s placement none\n",tag);return;}
    uint32_t low=(uint32_t)m->placement_id;
    CaRow row;memset(&row,0,sizeof row);row.want=low;
    if(g_ca.stream)winst_test_collect_placements(g_ca.stream,g_ca.stream_len,0,0,1e9f,ca_row_visit,&row,NULL);
    CaGroups groups={low>>16,low&0xffffu,""};
    WGTable table;
    if(g_ca.companion && wg_open_file(g_ca.companion,(size_t)g_ca.companion_len,&table))
        wg_visit(&table,ca_group_visit,&groups);
    if(!row.found){fprintf(out,"    %s placement section=%u row=%u not-found groups=[%s]\n",tag,low>>16,low&0xffffu,groups.names);return;}
    const float *x=row.p.matrix;
    float sx=sqrtf(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]),sy=sqrtf(x[4]*x[4]+x[5]*x[5]+x[6]*x[6]),sz=sqrtf(x[8]*x[8]+x[9]*x[9]+x[10]*x[10]);
    /* mesh bounds vs the row's authored bounds: an indicator of transform decode error */
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(int q=0;q<m->nverts;q++)for(int i=0;i<3;i++){lo[i]=fminf(lo[i],m->verts[q*5+i]);hi[i]=fmaxf(hi[i],m->verts[q*5+i]);}
    float outside=0;
    for(int i=0;i<3;i++){outside=fmaxf(outside,row.p.bounds_min[i]-lo[i]);outside=fmaxf(outside,hi[i]-row.p.bounds_max[i]);}
    fprintf(out,"    %s placement section=%u row=%u type=%s keys=%08x/%08x/%08x flags=%04x "
        "t=(%.2f %.2f %.2f) yaw=%.1f scale=(%.3f %.3f %.3f) bounds=(%.1f %.1f %.1f)..(%.1f %.1f %.1f) "
        "mesh-outside-bounds=%.2f groups=[%s]\n",
        tag,low>>16,low&0xffffu,row.type,row.p.model_keys[0],row.p.model_keys[1],row.p.model_keys[2],
        row.p.flags,x[12],x[13],x[14],atan2f(x[1],x[0])*57.29578f,sx,sy,sz,
        row.p.bounds_min[0],row.p.bounds_min[1],row.p.bounds_min[2],
        row.p.bounds_max[0],row.p.bounds_max[1],row.p.bounds_max[2],outside,groups.names);
}
static void ca_mesh(FILE *out,const char *tag,int mesh) {
    const N2Mesh *m=g_ca.scene->meshes+mesh;
    fprintf(out,"    %s mesh=%d %s asset=%s cat=%d scen=%s drawn=%d model=%08x base=%08x lod-fallback=%d "
        "texkey=%08x mat_exact=%d wall_policy=%d wall_h=%.2f ground_disabled=%d prop=%d\n",
        tag,mesh,m->sname,m->aname[0]?m->aname:"-",m->cat,n2_scen_name(m->scen),ca_drawn(m),
        m->world_model_key,m->world_base_model_key,
        m->world_base_model_key && m->world_model_key!=m->world_base_model_key,
        m->texkey,m->mat_exact,m->wall_policy,m->wall_height,m->ground_disabled,m->prop_id!=0);
}
/* Geometry/material evidence only. "crosses-course" means the face footprint
 * intersects the driven event line itself; "across-road" means drivable ROAD
 * at the contact height on both sides of the face. */
static const char *ca_classify(const CaSource *s,const CaEvidence *e) {
    const N2Mesh *m=g_ca.scene->meshes+s->mesh;
    int across=e->road_left==2 && e->road_right==2,crosses=e->course_d==0;
    if(e->duplicates)return "duplicate-instance";
    if(m->world_base_model_key && m->world_model_key!=m->world_base_model_key)return "wrong-prototype-lod-suspect";
    if(e->coverage<.2f)return crosses?"collision-only-crosses-course":across?"collision-only-across-road":"collision-only";
    if(crosses || across)return crosses?"authored-overlap-crosses-course":"authored-overlap-across-road";
    return "backed-by-visible-face";
}

static void ca_end(FILE *out) {
    long repeated=0,by_class[4][2]={{0}};
    const char *names[4]={"static","closure","prop","mesh-static"};
    for(int k=0;k<g_ca.nsrc;k++) {
        const CaSource *s=g_ca.src+k;const char *c=ca_class(s->mesh);
        int ci=0;while(ci<3 && strcmp(c,names[ci]))ci++;
        by_class[ci][0]++;by_class[ci][1]+=s->hits;
        if(s->hits<CA_REPEAT_HITS)continue;
        repeated++;
        CaEvidence e;ca_evidence(s->mesh,s->tri,s->pos,&e);
        const N2Mesh *m=g_ca.scene->meshes+s->mesh;
        const float *v[3];ca_face(m,s->tri,v);
        float zlo=fminf(v[0][2],fminf(v[1][2],v[2][2])),zhi=fmaxf(v[0][2],fmaxf(v[1][2],v[2][2]));
        int poly=ca_polygon(m);
        fprintf(out,"CONTACT-SOURCE %d %s %s %s %s mesh=%d tri=%d hits=%ld episodes=%ld cars=%x phases=%x "
            "frames=%ld..%ld at=(%.2f %.2f %.2f) n=(%+.3f %+.3f) maxpen=%.3f face-z=%.2f..%.2f "
            "coverage=%.2f/%d road=%d/%d course=%.2f dup=%d class=%s\n",
            g_ca.event,g_ca.region,g_ca.freeroam?"free":"race",c,s->kind?"rail":"wall",s->mesh,s->tri,s->hits,
            s->episodes,s->cars,s->phases,s->first,s->last,s->pos[0],s->pos[1],s->pos[2],s->nx,s->ny,
            s->maxpen,zlo,zhi,e.coverage,e.samples,e.road_left,e.road_right,e.course_d,e.duplicates,
            ca_classify(s,&e));
        if(poly>=0) {
            const UG2TrackPolygon *p=g_ca.tc->polygons+poly;
            fprintf(out,"    source polygon=%d offset=0x%llx section=%u group=%u:%s owner=%d meta=%02x%02x%02x%02x attr=%u "
                "corners=(%.2f %.2f %.2f)(%.2f %.2f %.2f)(%.2f %.2f %.2f)(%.2f %.2f %.2f)\n",
                poly,(unsigned long long)p->source_offset,p->section,p->metadata[0],ca_group(poly),
                wg_event_id(ca_group(poly)),p->metadata[0],p->metadata[1],p->metadata[2],p->metadata[3],
                p->attribute,p->corners[0][0],p->corners[0][1],p->corners[0][2],p->corners[1][0],p->corners[1][1],
                p->corners[1][2],p->corners[2][0],p->corners[2][1],p->corners[2][2],p->corners[3][0],
                p->corners[3][1],p->corners[3][2]);
        } else {
            ca_mesh(out,"source",s->mesh);ca_placement(out,"source",m);
        }
        if(e.backing>=0) {
            fprintf(out,"    backing distance=%.2f\n",e.backing_d);
            ca_mesh(out,"backing",e.backing);ca_placement(out,"backing",g_ca.scene->meshes+e.backing);
        }
    }
    fprintf(out,"CONTACT-SUMMARY %d %s %s resident-generation=1(single load) contacts=%ld rail-pushes=%ld "
        "unattributed=%ld sources=%d repeated=%ld static=%ld/%ld closure=%ld/%ld prop=%ld/%ld mesh-static=%ld/%ld\n",
        g_ca.event,g_ca.region,g_ca.freeroam?"free":"race",g_ca.contacts,g_ca.rails,g_ca.unattributed,g_ca.nsrc,repeated,
        by_class[0][0],by_class[0][1],by_class[1][0],by_class[1][1],by_class[2][0],by_class[2][1],
        by_class[3][0],by_class[3][1]);
    if(g_ca.log)fflush(g_ca.log);
    free(g_ca.poly);g_ca.poly=NULL;free(g_ca.src);g_ca.src=NULL;g_ca.nsrc=g_ca.cap=0;
    free(g_ca.companion);g_ca.companion=NULL;g_ca.scene=NULL;g_ca.tc=NULL;
}
#endif
