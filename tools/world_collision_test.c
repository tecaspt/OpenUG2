/* Asset-free compiled collision corrections: unchanged draw/ground payload,
   all material slices, exact overrides and forward/reverse/airborne contacts. */
#include <assert.h>
#include "world.h"
#include "world_collision_rules.h"
#include "ug2_track_collision.h"

static N2Mesh mesh(uint32_t key,uint64_t id,float x,int ground) {
    float verts[]={x,-4,0,0,0,x,4,0,0,0,x,4,1,0,0,x,-4,1,0,0,
                   x-5,-5,0,0,0,x+5,-5,0,0,0,x+5,5,0,0,0,x-5,5,0,0,0};
    uint16_t idx[]={0,1,2,0,2,3,4,5,6,4,6,7};
    N2Mesh m={0};m.verts=malloc(sizeof verts);m.idx=malloc(sizeof idx);assert(m.verts&&m.idx);
    memcpy(m.verts,verts,sizeof verts);memcpy(m.idx,idx,sizeof idx);
    m.nverts=8;m.nidx=ground?12:6;m.cat=ground?N2_TERRAIN:N2_OTHER;
    m.scen=ground?N2_SC_TERRAIN:N2_SC_WALL;m.world_model_key=key;m.placement_id=id;
    strcpy(m.sname,"TRUNCATED_SHARED_NAME");return m;
}
static int contact(const N2Mesh *m,float x,float z) {
    N2Scene s={(N2Mesh*)m,1,1};float pos[]={x,0,z},vel[]={.1f,0};
    float bb[]={-.8f,-.4f,0,.8f,.4f,1.4f};
    return collide_body_mesh_wall(pos,vel,0,bb,z+.05f,z+1.4f,&s,0,.30f,INFINITY,NULL);
}
static void roadside(void) {
    const char *names[]={"TRN_TEST_ROAD","TRN_TEST_TERRAIN","XB_TEST_BUILDING",
        "XO_TEST_TREE","XO_TEST_CONTAINER","PAN_OCEAN","TRN_TEST_WATERFALL",
        "XW_TEST_GUARDRAIL","XO_TRACKBARRIER_PLAYER_00"};
    N2Mesh meshes[9];N2Scene s={meshes,9,9};
    uint64_t ids[9];float verts[9][40];uint16_t indices[9][12];
    static const float boundary[][3]={{140,-4,0},{140,4,0}};
    const WCollisionRule rule={1,108,8,2,boundary},guard={0,107,8,0,NULL};
    const WCollisionEdits edits={"TEST",1,&guard};
    for(int n=0;n<9;n++) {
        meshes[n]=mesh(100+n,100+n,20*n,1);
        strcpy(meshes[n].sname,names[n]);meshes[n].scen=n2_scen_class(names[n]);
        if(n==0)meshes[n].cat=N2_ROAD;
        if(n==2 || n==3 || n==4 || n==7)meshes[n].cat=N2_OTHER;
        if(n==7)meshes[n].cat=N2_ROAD; /* barrier material is road, geometry is scenery */
        if(n==8)meshes[n].cat=N2_OTHER;
        ids[n]=n2_world_source_id(meshes+n);
        memcpy(verts[n],meshes[n].verts,sizeof verts[n]);
        memcpy(indices[n],meshes[n].idx,sizeof indices[n]);
        assert(contact(meshes+n,20*n+.1f,0));
    }
    meshes[7].wall_policy=2;meshes[7].wall_height=8;
    assert(wce_wall(meshes+7,&rule,0) && contact(meshes+7,140.1f,6));
    assert(wce_roadside(&edits,&s,0) && wce_roadside(&edits,&s,0) && s.count==9);
    for(int n=0;n<9;n++) {
        N2Mesh *m=meshes+n;float x=20*n;
        int wall=n<3 || n==7;
        assert(m->wall_policy==(n<3?0:n==7?2:1));
        assert(m->wall_height==(n==7?8:0) && n2_world_body_disabled(m)==!wall);
        assert((m->wall_nidx>0)==(n==7));
        assert(ids[n]==n2_world_source_id(m) && !memcmp(verts[n],m->verts,sizeof verts[n]));
        assert(!memcmp(indices[n],m->idx,sizeof indices[n]));
        assert(contact(m,x+.1f,0)==wall && contact(m,x-.1f,0)==wall);
        assert(contact(m,x+.1f,6)==(n==7) && !contact(m,x+.1f,9));
        float face[9];assert(phys_wall_debug_face(m,0,.3f,face)==wall);
        N2Scene one={m,1,1};float anchor[]={x-2,0,.5f},eye[]={x+2,0,.5f};
        int camera_wall=n!=3 && n!=4; /* visible road/terrain/buildings/barriers, not decorative props */
        int camera_hit=world_camera_clip(&one,NULL,anchor,eye,.1f)<1;
        if(camera_hit!=camera_wall)fprintf(stderr,"camera fixture %d %s category %d scenery %d hit %d expected %d\n",n,names[n],m->cat,m->scen,camera_hit,camera_wall);
        assert(camera_hit==camera_wall);
        float ground=99;int expected=n<2?(n==0?WSURF_ROAD:WSURF_TERRAIN):WSURF_NONE;
        assert(world_ground_at(&one,x-1,0,1,&ground)==expected);
        WGroundHit hit;int verdict;
        assert(world_wheel_support(&one,x-1,0,0,.25f,.18f,&hit,NULL,&verdict)==expected);
        assert(verdict==(n<2?WWS_CONTACT:WWS_NOCOVER));
        float from[]={x-1,0,1},to[]={x-1,0,-1};
        assert((world_ground_sweep(&one,from,to,NULL)<1)==(n<2));
        assert((world_camera_clip(&one,NULL,from,to,.1f)<1)==camera_wall);
        if(n<2)assert(fabsf(ground)<1e-6f);
        else assert(n2_ground_z(&one,x-1,0,99)==99);
    }
    float obst[9][4],z[9][2];int src[9];int count=phys_collect_walls(&s,obst,src,z,9);assert(count==2);
    const float bb[]={-.8f,-.4f,0,.8f,.4f,1.4f};
    for(int side=-1;side<=1;side+=2) {
        float old[]={140+4*side,0,6},pos[]={140-4*side,0,6},vel[]={-side*2,0};
        assert(world_body_walls_move(&s,old,pos,vel,0,bb,6,7.4f,-.5f,
            obst,z,count,src,NULL,0,NULL,NULL)>0);
        assert(side*(pos[0]-140)>.79f);
        float anchor[]={140+2*side,0,6},eye[]={140-2*side,0,6};
        N2Scene one={meshes+7,1,1};assert(world_camera_clip(&one,NULL,anchor,eye,.1f)<1);
    }
    for(int n=0;n<9;n++){free(meshes[n].verts);free(meshes[n].idx);
        free(meshes[n].wall_verts);free(meshes[n].wall_idx);}
}
static void boundary_outline(void) {
    float verts[40]={-6,-6,0,0,0, -6,6,0,0,0, -6,6,.8f,0,0, -6,-6,.8f,0,0,
                     -6,-6,0,0,0, 6,-6,0,0,0, 6,-6,.8f,0,0, -6,-6,.8f,0,0};
    uint16_t left[]={0,1,2,0,2,3},bottom[]={4,5,6,4,6,7};
    N2Mesh m[2]={{.verts=verts,.nverts=8,.idx=left,.nidx=6,.scen=N2_SC_WALL,
                  .cat=N2_OTHER,.placement_id=900,.world_model_key=902},
                 {.verts=verts,.nverts=8,.idx=bottom,.nidx=6,.scen=N2_SC_WALL,
                  .cat=N2_OTHER,.placement_id=900,.world_model_key=902}};
    strcpy(m[0].sname,"XW_CURVED_WALL");strcpy(m[1].sname,m[0].sname);
    const WCollisionRule guard={0,902,8,0,NULL};const WCollisionEdits edits={"TEST",1,&guard};
    N2Scene scene={m,2,2};assert(wce_roadside(&edits,&scene,0));
    assert(m[0].wall_verts && !m[1].wall_verts);
    assert(!cw_probe_contact(&scene,0,0,0,1,.1f,1.5f)); /* do not close the opening */
    assert(cw_probe_contact(&scene,0,-5.9f,0,1,6,7.4f));
    assert(cw_probe_contact(&scene,0,0,-5.9f,1,6,7.4f)); /* second material owns this face */
    for(int n=0;n<2;n++){free(m[n].wall_verts);free(m[n].wall_idx);}
}
static void planter_materials(void) {
    float verts[60]={-1,-1,0,0,0, -1,1,0,0,0, -1,1,.5f,0,0, -1,-1,.5f,0,0,
                     1,-1,0,0,0, 1,1,0,0,0, 1,1,.5f,0,0, 1,-1,.5f,0,0,
                     -10,-10,6,0,0, 10,-10,6,0,0, 10,-10,9,0,0, -10,-10,9,0,0};
    uint16_t leaves[]={8,9,10,8,10,11},base[]={0,1,2,0,2,3,4,5,6,4,6,7};
    N2Mesh m[2]={{.verts=verts,.nverts=12,.idx=leaves,.nidx=6,.scen=N2_SC_TREE,
                  .cat=N2_OTHER,.placement_id=901},
                 {.verts=verts,.nverts=12,.idx=base,.nidx=12,.scen=N2_SC_TREE,
                  .cat=N2_OTHER,.placement_id=901}};
    strcpy(m[0].sname,"XT_PLANTERPALMTREE");strcpy(m[1].sname,m[0].sname);
    N2Scene scene={m,2,2};assert(wce_roadside(NULL,&scene,0));
    assert(m[0].wall_nidx==24 && !m[1].wall_verts);
    assert(cw_probe_contact(&scene,0,-.9f,0,.5f,.1f,1.5f));
    assert(cw_probe_contact(&scene,0,.9f,0,.5f,.1f,1.5f));
    assert(!cw_probe_contact(&scene,0,0,-9.9f,.5f,6,7.4f));
    for(int n=0;n<2;n++){free(m[n].wall_verts);free(m[n].wall_idx);}
}
static void sloped_boundary(void) {
    float verts[20]={0,-6,0,0,0, 0,6,3,0,0, 0,6,3.8f,0,0, 0,-6,.8f,0,0};
    uint16_t idx[]={0,1,2,0,2,3};
    N2Mesh m={.verts=verts,.nverts=4,.idx=idx,.nidx=6,.cat=N2_OTHER,.scen=N2_SC_WALL};
    strcpy(m.sname,"XW_GREENBARRIER_TEST");N2Scene scene={&m,1,1};
    assert(wce_roadside(NULL,&scene,0) && m.wall_nidx==12);
    assert(cw_probe_contact(&scene,0,.1f,5,.5f,3.2f,4.6f));
    float anchor[]={-2,5,3.2f},eye[]={2,5,3.2f};
    assert(world_camera_clip(&scene,NULL,anchor,eye,.1f)<1);
    free(m.wall_verts);free(m.wall_idx);
}
static void compiled_rules(void) {
    const WCollisionEdits *e=&city_collision_edits;
    assert(wce_map(e,"STREAML4RA")&&wce_map(e,"STREAML4RA.BUN")&&!wce_map(e,"STREAML4RB"));
    for (int n=0;n<e->count;n++) {
        const WCollisionRule *r=e->rules+n;
        assert(r->kind>=0&&r->kind<=2&&isfinite(r->height)&&r->height>=0&&r->height<=1000);
        assert(r->kind==2 ? !r->key : r->key!=0);
        if (r->kind==0) assert(r->key<=UINT32_MAX&&!r->count&&!r->points);
        else if (r->kind==1 && r->height==0) assert(!r->count&&!r->points);
        else assert(r->height>0&&r->count>=2&&r->count<=16383&&r->points);
        for (int k=0;k<r->count;k++) for (int a=0;a<3;a++)
            assert(isfinite(r->points[k][a])&&fabsf(r->points[k][a])<100000);
        for (int k=0;k<n;k++) assert(r->kind==2||r->kind!=e->rules[k].kind||r->key!=e->rules[k].key);
    }
}
static void buildings(void) {
    const char *names[]={"XB_MODERNHOTELBASE_1A_00","XB_MODERNHOTELBASE_1A_00",
        "XO_IndustrialOfficesA_1a_CK","XO_INDUSTRIALSHEDB_1A_CK_00",
        "XO_GasstationTN_1a_00","XS_HOTELB_1A_00","XW_GUARDRAIL_A_1A_FWY_00"};
    N2Mesh meshes[8];N2Scene s={meshes,8,8};
    for(int n=0;n<7;n++) {
        meshes[n]=mesh(11,n<2?202:300+n,10*n,0);
        meshes[n].scen=n2_scen_class(names[n]);
        strcpy(meshes[n].sname,names[n]);
        assert(contact(meshes+n,10*n+.1f,0));
    }
    meshes[7]=mesh(20,400,80,1);
    static const float boundary[][3]={{0,-4,0},{0,4,0}};
    const WCollisionRule rules[]={{0,11,8,0,NULL},{1,999,8,2,boundary}};
    const WCollisionEdits edits={"TEST",2,rules};
    assert(wce_apply(&edits,&s,1));
    for(int n=0;n<5;n++) {
        assert(meshes[n].scen==N2_SC_BUILDING && meshes[n].wall_policy==0);
        assert(!meshes[n].wall_verts && contact(meshes+n,10*n+.1f,0));
        float face[9];assert(phys_wall_debug_face(meshes+n,0,.3f,face));
        assert(!contact(meshes+n,10*n+.1f,6)); /* retain source height */
        N2Scene one={meshes+n,1,1};float anchor[]={10*n-2,0,.5f},eye[]={10*n+2,0,.5f};
        assert(world_camera_clip(&one,NULL,anchor,eye,.1f)<1);
    }
    assert(meshes[5].scen==N2_SC_STRUCT && meshes[6].scen==N2_SC_WALL);
    assert(contact(meshes+5,50.1f,6) && contact(meshes+6,60.1f,6));
    /* The same building policy applies without a matching map-specific export. */
    meshes[0].wall_policy=2;meshes[0].wall_height=8;
    assert(wce_wall(meshes,rules+1,0) && wce_apply(NULL,&s,0));
    assert(meshes[0].wall_policy==0 && !meshes[0].wall_verts && contact(meshes,.1f,0));
    float obst[8][4],z[8][2];int src[8];assert(phys_collect_walls(&s,obst,src,z,8)==7);
    assert(!cw_probe_contact(&s,0,.1f,4.8f,.4f,0,1.4f)); /* actual opening, inside full vertex bounds */
    const float bb[]={-.8f,-.4f,0,.8f,.4f,1.4f};
    for(int side=-1;side<=1;side+=2) {
        float old[]={2*side,0,0},pos[]={-2*side,0,0},vel[]={-2*side,0};
        assert(world_body_walls_move(&s,old,pos,vel,0,bb,.05f,1.4f,0,obst,z,7,src,NULL,0,NULL,NULL)>0);
        assert(pos[0]*side>0);
    }
    float ground=99;N2Scene terrain={meshes+7,1,1};
    assert(world_ground_at(&terrain,79,0,1,&ground)!=WSURF_NONE && fabsf(ground)<1e-6f);
    for(int n=0;n<8;n++) {free(meshes[n].verts);free(meshes[n].idx);
        free(meshes[n].wall_verts);free(meshes[n].wall_idx);}
}
static void repeated_models(void) {
    static const float local[][3]={{16,-4,0},{16,4,0}};
    const WCollisionRule rules[]={{0,10,0,0,NULL},{0,11,8,0,NULL},{0,99,8,0,NULL},
        {1,202,8,2,local},{2,0,8,2,local}};
    const WCollisionEdits edits={"HOME",5,rules};
    N2Scene s={0};s.count=s.cap=6;s.meshes=calloc(6,sizeof *s.meshes);assert(s.meshes);
    s.meshes[0]=mesh(10,101,0,0);
    s.meshes[1]=mesh(91,102,10,1);s.meshes[1].world_base_model_key=10;
    s.meshes[2]=mesh(11,103,30,0);
    s.meshes[3]=mesh(93,202,50,0);s.meshes[3].world_base_model_key=11;
    s.meshes[4]=mesh(30,104,70,0); /* same name, unrelated model */
    s.meshes[5]=mesh(99,105,90,0);s.meshes[5].world_base_model_key=10;
    float verts[6][40];uint16_t indices[6][12];
    for(int n=0;n<6;n++) {memcpy(verts[n],s.meshes[n].verts,sizeof verts[n]);
        memcpy(indices[n],s.meshes[n].idx,sizeof indices[n]);}
    assert(!wce_map(&edits,"OTHER"));assert(wce_apply(&edits,&s,0) && s.count==6);
    for(int n=0;n<6;n++) {
        assert(!memcmp(verts[n],s.meshes[n].verts,sizeof verts[n]));
        assert(!memcmp(indices[n],s.meshes[n].idx,sizeof indices[n]));
    }
    assert(!contact(s.meshes,.1f,0) && !contact(s.meshes+1,10.1f,0));
    assert(contact(s.meshes+2,30.1f,6) && contact(s.meshes+3,50.1f,6));
    assert(!contact(s.meshes+3,16.1f,6) && !contact(s.meshes+3,50.1f,9));
    assert(s.meshes[4].wall_policy==0 && contact(s.meshes+4,70.1f,0));
    assert(contact(s.meshes+5,90.1f,6)); /* selected-model edit wins over its family */
    float ground=99;N2Scene terrain={s.meshes+1,1,1};
    assert(world_ground_at(&terrain,9,0,1,&ground)!=WSURF_NONE && fabsf(ground)<1e-6f);
    for(int n=0;n<s.count;n++) {free(s.meshes[n].verts);free(s.meshes[n].idx);
        free(s.meshes[n].wall_verts);free(s.meshes[n].wall_idx);}
    free(s.meshes);
}
static void source_building_walls(void) {
    static const float front[][3]={{16,-4,0},{16,4,0}};
    const WCollisionRule rules[]={{0,11,8,0,NULL},{1,202,12,2,front}};
    const WCollisionEdits edits={"TEST",2,rules};
    N2Mesh m[3]={mesh(11,202,0,0),mesh(11,202,0,0),mesh(11,203,30,0)};
    N2Scene s={m,3,3};
    for(int n=0;n<3;n++)m[n].scen=N2_SC_BUILDING;
    float original[40];memcpy(original,m[0].verts,sizeof original);
    assert(wce_roadside(&edits,&s,1));
    assert(contact(m,16.1f,9) && contact(m,15.9f,9));
    assert(!contact(m,.1f,0) && !contact(m,16.1f,13));
    assert(!m[1].wall_verts && !contact(m+1,16.1f,9));
    assert(contact(m+2,30.1f,0));
    assert(!memcmp(original,m[0].verts,sizeof original));
    float obst[3][4],z[3][2];int src[3];
    assert(phys_collect_walls(&s,obst,src,z,3)==2);
    float anchor[]={14,0,9},eye[]={18,0,9};
    assert(world_camera_clip(&s,NULL,anchor,eye,.1f)<1);
    float ground=99;assert(world_ground_at(&s,16,0,1,&ground)==WSURF_NONE);
    assert(wce_roadside(&edits,&s,1) && phys_collect_walls(&s,obst,src,z,3)==2);
    const WCollisionRule removed={1,202,0,0,NULL};
    const WCollisionEdits removal={"TEST",1,&removed};
    assert(wce_roadside(&removal,&s,1));
    assert(!contact(m,.1f,0) && !contact(m+1,.1f,0) && contact(m+2,30.1f,0));
    assert(wce_roadside(&edits,&s,0));
    for(int n=0;n<3;n++) {
        assert(!m[n].wall_verts && !contact(m+n,16.1f,9));
        assert(contact(m+n,(n==2?30:0)+.1f,0));
        free(m[n].verts);free(m[n].idx);
    }
}
static void pilot_boundaries(void) {
    const uint64_t ids[]={UINT64_C(0x4152344c0712004e),UINT64_C(0x4152344c02c50067),
        UINT64_C(0x4152344c02c500d9),UINT64_C(0x4152344c02c500da)};
    N2Mesh *m=calloc(5,sizeof *m);assert(m);N2Scene s={m,5,5};
    for(int n=0;n<5;n++) {
        m[n]=mesh(11,n<4?ids[n]:999,100*n,0);
        m[n].scen=n==0 || n==4?N2_SC_BUILDING:n==1?N2_SC_TREE:N2_SC_WALL;
    }
    strcpy(m[1].sname,"XT_PILOT_HEDGE");
    assert(wce_roadside(&city_collision_edits,&s,1));
    m=s.meshes;
    assert(cw_probe_contact(&s,0,-135.8f,94.6f,.35f,9,10.4f)); /* close decorative gap */
    assert(cw_probe_contact(&s,0,-130,95.6f,.5f,9,10.4f)); /* shared street frontage */
    assert(cw_probe_contact(&s,0,-142,93.75f,.5f,9,10.4f));
    assert(!m[1].wall_verts && n2_world_body_disabled(m+1)); /* no individual flower box */
    assert(!cw_probe_contact(&s,0,-155,95,.85f,2,3.4f)); /* outside passage stays open */
    assert(cw_probe_contact(&s,2,-184.35f,102,.5f,33,34.4f));
    assert(cw_probe_contact(&s,3,-183.15f,102,.5f,33,34.4f));
    assert(!cw_probe_contact(&s,0,-130,95.6f,1,24,25.4f));
    assert(!cw_probe_contact(&s,2,-184.35f,102,.5f,2,3.4f));
    assert(contact(m+4,400.1f,0));
    assert(wce_roadside(&city_collision_edits,&s,0));
    assert(!cw_probe_contact(&s,0,-130,95.6f,1,9,10.4f));
    assert(!cw_probe_contact(&s,1,-142,94,.5f,9,10.4f));
    for(int n=0;n<s.count;n++) {free(m[n].verts);free(m[n].idx);
        free(m[n].wall_verts);free(m[n].wall_idx);}
    free(m);
}
static void pilot_roadside_boundary(void) {
    const uint64_t ids[]={UINT64_C(0x4152344c02c50053),UINT64_C(0x4152344c02c6002d),
        UINT64_C(0x1f2dfe7f7045e19f),UINT64_C(0x33668dcc71aa1b84)};
    N2Mesh *m=calloc(4,sizeof *m);assert(m);N2Scene s={m,4,4};
    for(int n=0;n<4;n++)m[n]=mesh(n==1?0xfa2a3df8:11,ids[n],40*n,n>=2);
    m[1].scen=N2_SC_PROP;strcpy(m[1].sname,"XO_PATHGUARDC_1A_00");
    assert(wce_roadside(&city_collision_edits,&s,1));m=s.meshes;
    assert(cw_probe_contact(&s,0,-130,79.3f,.5f,9,10.4f));
    assert(cw_probe_contact(&s,0,-95,88.6f,.5f,9,10.4f)); /* former cyan/pink seam */
    assert(!cw_probe_contact(&s,0,-130,90,.85f,2,3.4f)); /* main road */
    assert(!cw_probe_contact(&s,0,-130,79.3f,.5f,24,25.4f));
    assert(!m[1].wall_verts && n2_world_body_disabled(m+1));
    for(int n=2;n<4;n++) {
        N2Scene one={m+n,1,1};float ground=99,anchor[]={40*n-2,0,.5f},eye[]={40*n+2,0,.5f};
        assert(!m[n].ground_disabled && !contact(m+n,40*n+.1f,0));
        assert(world_ground_at(&one,40*n-1,0,1,&ground)==WSURF_TERRAIN && fabsf(ground)<1e-6f);
        assert(world_camera_clip(&one,NULL,anchor,eye,.1f)<1); /* visible terrain still blocks the camera */
    }
    assert(wce_roadside(&city_collision_edits,&s,0));
    assert(contact(m,.1f,0) && contact(m+1,35.1f,0));
    for(int n=2;n<4;n++)assert(contact(m+n,40*n+.1f,0));
    for(int n=0;n<s.count;n++){free(m[n].verts);free(m[n].idx);free(m[n].wall_verts);free(m[n].wall_idx);}
    free(m);
}
static void bun_walls(void) {
    N2Mesh *m=calloc(3,sizeof *m);assert(m);N2Scene scene={m,3,3};
    m[0]=mesh(1,1,0,0);m[0].scen=N2_SC_BUILDING;
    m[1]=mesh(2,2,20,1);m[2]=mesh(3,3,40,0);m[2].prop_id=3;
    UG2TrackPolygon polys[2]={{0}};UG2TrackPolygon *poly=polys;
    poly->source_offset=104;poly->section=121;
    const float corners[4][3]={{10,-4,0},{10,4,0},{10,4,12},{10,-4,12}};
    memcpy(poly->corners,corners,sizeof corners);
    const float horizontal[4][3]={{-4,-4,20},{4,-4,20},{4,4,20},{-4,4,20}};
    memcpy(polys[1].corners,horizontal,sizeof horizontal);
    UG2TrackCollision data={.polygons=polys,.count=2,.groups=1,.total=2};
    data.activation.count=1;strcpy((char*)data.activation.records[0]+9,"NONE");
    assert(wce_bun(&scene,&data,0x12345678,0));
    assert(scene.count==4 && !contact(scene.meshes,.1f,0));
    assert(contact(scene.meshes+3,10.1f,9) && !contact(scene.meshes+3,10.1f,13));
    assert(contact(scene.meshes+2,40.1f,0)); /* independent breakable object */
    assert(scene.meshes[3].cat==N2_COLLISION && scene.meshes[3].ground_disabled);
    float ground;N2Scene floor={scene.meshes+1,1,1};
    assert(world_ground_at(&floor,19,0,1,&ground)==WSURF_TERRAIN);
    float a[]={8,0,9},b[]={12,0,9};assert(world_camera_clip(&scene,NULL,a,b,.1f)<1);
    uint64_t id=n2_world_source_id(scene.meshes+3);memset(poly->corners,0,sizeof poly->corners);
    assert(id && contact(scene.meshes+3,10.1f,9)); /* scene owns its geometry */
    for(int k=0;k<scene.count;k++){free(scene.meshes[k].verts);free(scene.meshes[k].idx);free(scene.meshes[k].wall_verts);free(scene.meshes[k].wall_idx);}
    free(scene.meshes);
}
static int bun_probe(const N2Scene *scene,float x) {
    for(int k=0;k<scene->count;k++)if(cw_probe_contact(scene,k,x,0,.5f,1,2.5f))return 1;
    return 0;
}
static void bun_event_walls(void) {
    static const char *names[]={"NONE","PLAYER_BARRIERS_4004","BARRIERS_4103","COMMON_UNKNOWN","BARRIERS_CAREER0","BARRIERS_DEMO"};
    UG2TrackPolygon polygons[6]={{0}};UG2TrackCollision data={0};data.polygons=polygons;data.count=6;data.activation.count=6;
    for(int k=0;k<6;k++) {
        float x=10+10*k;
        float points[4][3]={{x,-4,0},{x,4,0},{x,4,8},{x,-4,8}};
        memcpy(polygons[k].corners,points,sizeof points);polygons[k].metadata[0]=(unsigned char)k;polygons[k].source_offset=32*k;
        strcpy((char*)data.activation.records[k]+9,names[k]);
    }
    const int events[]={-1,4004,4103,4999,0};
    for(int q=0;q<5;q++) {
        int event=events[q];N2Scene scene={0};assert(wce_bun(&scene,&data,7,event));
        float obst[6][4],z[6][2];int src[6];int count=phys_collect_walls(&scene,obst,src,z,6);
        assert(count==(event==-1||event==4999?2:event==4004||event==4103?3:6));
        assert(bun_probe(&scene,10.1f));
        assert(bun_probe(&scene,40.1f)); /* unknown/common retained */
        assert(!!bun_probe(&scene,20.1f)==(event==4004||event==0));
        assert(!!bun_probe(&scene,30.1f)==(event==4103||event==0));
        assert(!!bun_probe(&scene,50.1f)==(event==0)); /* career: closed only in */
        assert(!!bun_probe(&scene,60.1f)==(event==0)); /* explicit unfiltered scene */
        for(int k=0;k<scene.count;k++){free(scene.meshes[k].verts);free(scene.meshes[k].idx);}free(scene.meshes);
    }
    N2Mesh *m=calloc(1,sizeof *m);assert(m);m[0]=mesh(1,1,0,0);N2Scene scene={m,1,1};
    polygons[5].metadata[0]=6;
    assert(!wce_bun(&scene,&data,7,-1) && scene.count==1 && contact(scene.meshes,.1f,0));
    free(scene.meshes[0].verts);free(scene.meshes[0].idx);free(scene.meshes);
}

/* ---- Issue #8 WP1: deterministic driving fixtures on the production path ----
 * Packed BUN polygons go through wce_bun, phys_collect_walls, the wall index,
 * an active ground grid and world_body_walls_move (wall list + rail pass), as
 * in race_events_check. The body is a MIATA-sized capsule moving at up to
 * 30 m/s (0.5 m/tick). Geometry only; no names select behaviour. */
typedef struct {
    N2Scene scene; WGroundGrid grid; float (*bounds)[4];
    float (*obst)[4],(*obz)[2]; int *src,n;
} Rig;
static const float RIG_BB[6]={-1.97f,-.94f,0,1.97f,.94f,1.37f};
static N2Mesh rig_quad(const float q[4][3],int cat) {
    N2Mesh m={0};m.verts=calloc(20,sizeof *m.verts);m.idx=malloc(6*sizeof *m.idx);
    assert(m.verts && m.idx);
    for(int k=0;k<4;k++)memcpy(m.verts+k*5,q[k],3*sizeof(float));
    const uint16_t idx[]={0,1,2,0,2,3};memcpy(m.idx,idx,sizeof idx);
    m.nverts=4;m.nidx=6;m.cat=cat;m.scen=cat==N2_ROAD?N2_SC_TERRAIN:N2_SC_OTHER;return m;
}
/* ground: ROAD quads; walls: packed polygons (four corners each). */
static void rig_build(Rig *r,const float (*ground)[4][3],int nground,
                      const float (*walls)[4][3],int nwalls) {
    memset(r,0,sizeof *r);
    r->scene.meshes=calloc((size_t)nground,sizeof *r->scene.meshes);assert(r->scene.meshes);
    for(int k=0;k<nground;k++)r->scene.meshes[k]=rig_quad(ground[k],N2_ROAD);
    r->scene.count=r->scene.cap=nground;
    UG2TrackPolygon *poly=calloc((size_t)(nwalls?nwalls:1),sizeof *poly);assert(poly);
    for(int k=0;k<nwalls;k++){memcpy(poly[k].corners,walls[k],sizeof poly[k].corners);poly[k].source_offset=64u*(unsigned)k;}
    UG2TrackCollision data={.polygons=poly,.count=(size_t)nwalls,.groups=1,.total=(size_t)nwalls};
    data.activation.count=1;strcpy((char*)data.activation.records[0]+9,"NONE");
    assert(wce_bun(&r->scene,&data,0x5eedu,-1));free(poly);
    int n=r->scene.count;
    r->bounds=malloc((size_t)n*sizeof *r->bounds);r->obst=malloc((size_t)n*sizeof *r->obst);
    r->obz=malloc((size_t)n*sizeof *r->obz);r->src=malloc((size_t)n*sizeof *r->src);
    assert(r->bounds && r->obst && r->obz && r->src);
    for(int k=0;k<n;k++) {
        const N2Mesh *m=r->scene.meshes+k;float *b=r->bounds[k];
        b[0]=b[1]=1e30f;b[2]=b[3]=-1e30f;
        for(int v=0;v<m->nverts;v++){b[0]=fminf(b[0],m->verts[v*5]);b[1]=fminf(b[1],m->verts[v*5+1]);
            b[2]=fmaxf(b[2],m->verts[v*5]);b[3]=fmaxf(b[3],m->verts[v*5+1]);}
    }
    assert(world_ground_grid_build(&r->grid,&r->scene,(const float (*)[4])r->bounds));
    world_ground_grid_activate(&r->grid);
    r->n=phys_collect_walls(&r->scene,r->obst,r->src,r->obz,n);
    phys_wall_index_build((const float (*)[4])r->obst,(const float (*)[2])r->obz,r->n);
}
static void rig_free(Rig *r) {
    phys_wall_index_build(NULL,NULL,0);world_ground_grid_activate(NULL);world_ground_grid_free(&r->grid);
    for(int k=0;k<r->scene.count;k++){free(r->scene.meshes[k].verts);free(r->scene.meshes[k].idx);
        free(r->scene.meshes[k].wall_verts);free(r->scene.meshes[k].wall_idx);}
    free(r->scene.meshes);free(r->bounds);free(r->obst);free(r->obz);free(r->src);
}
/* Constant heading, per-tick velocity; the car rides the selected ground
 * layer like the race harness. Returns the number of ticks with any contact. */
static int rig_drive(Rig *r,float pos[3],float vel[2],float heading,int ticks) {
    int touched=0;
    for(int t=0;t<ticks;t++) {
        float old[3]={pos[0],pos[1],pos[2]};
        pos[0]+=vel[0];pos[1]+=vel[1];
        float z;if(world_ground_at(&r->scene,pos[0],pos[1],pos[2]+.5f,&z)!=WSURF_NONE)pos[2]=z;
        int rails=0;
        int walls=world_body_walls_move(&r->scene,old,pos,vel,heading,RIG_BB,pos[2]+.05f,pos[2]+RIG_BB[5],0,
            (const float (*)[4])r->obst,(const float (*)[2])r->obz,r->n,r->src,NULL,0,NULL,&rails);
        touched+=walls>0 || rails>0;
        if(getenv("RIG_TRACE"))printf("  t%d pos (%.3f %.3f %.3f) vel (%.4f %.4f) walls %d rails %d\n",t,pos[0],pos[1],pos[2],vel[0],vel[1],walls,rails);
        assert(isfinite(pos[0]) && isfinite(pos[1]) && isfinite(vel[0]) && isfinite(vel[1]));
    }
    return touched;
}
static void rig_wall(float out[4][3],float ax,float ay,float az,float bx,float by,float bz,float h) {
    const float q[4][3]={{ax,ay,az},{bx,by,bz},{bx,by,bz+h},{ax,ay,az+h}};memcpy(out,q,sizeof q);
}
static const float RIG_FLOOR[1][4][3]={{{-400,-400,0},{400,-400,0},{400,400,0},{-400,400,0}}};

/* Rotated concave (L-shaped) building: every face blocks head-on at 30 m/s,
 * the reflex corner cannot be entered, and the open notch is not closed by a
 * hull/bounding-box barrier. Alternate faces use reversed winding. */
static void fixture_rotated_concave_building(void) {
    const float L[6][2]={{0,0},{20,0},{20,8},{8,8},{8,20},{0,20}};
    const float th=.6f,c=cosf(th),sn=sinf(th),ox=100,oy=50;
#define W(lx,ly,axis) ((axis)?oy+sn*(lx)+c*(ly):ox+c*(lx)-sn*(ly))
    float walls[6][4][3];
    for(int k=0;k<6;k++) {
        const float *a=L[k],*b=L[(k+1)%6];
        if(k&1)rig_wall(walls[k],W(b[0],b[1],0),W(b[0],b[1],1),0,W(a[0],a[1],0),W(a[0],a[1],1),0,8);
        else rig_wall(walls[k],W(a[0],a[1],0),W(a[0],a[1],1),0,W(b[0],b[1],0),W(b[0],b[1],1),0,8);
    }
    Rig r;rig_build(&r,RIG_FLOOR,1,(const float (*)[4][3])walls,6);
    assert(r.n==6);
    for(int k=0;k<6;k++) {     /* head-on into every exterior face */
        const float *a=L[k],*b=L[(k+1)%6];
        float ex=b[0]-a[0],ey=b[1]-a[1],len=hypotf(ex,ey),nx=ey/len,ny=-ex/len; /* outward (CCW polygon) */
        float mx=(a[0]+b[0])/2+6*nx,my=(a[1]+b[1])/2+6*ny;
        float dx=-(c*nx-sn*ny),dy=-(sn*nx+c*ny);
        float pos[3]={W(mx,my,0),W(mx,my,1),0},vel[2]={.5f*dx,.5f*dy};
        int hits=rig_drive(&r,pos,vel,atan2f(dy,dx),40);
        float lx=c*(pos[0]-ox)+sn*(pos[1]-oy),ly=-sn*(pos[0]-ox)+c*(pos[1]-oy);
        float d=(lx-a[0])*nx+(ly-a[1])*ny;
        printf("concave building face %d: hits %d stop distance %.4f (front bumper 1.97)\n",k,hits,d);
        assert(hits>0 && d>1.969f && d<2.2f && fabsf(vel[0]*dx+vel[1]*dy)<1e-5f);
    }
    {   /* diagonal into the reflex corner (8,8) from inside the notch */
        float dlx=-.70710678f,dly=-.70710678f,dx=c*dlx-sn*dly,dy=sn*dlx+c*dly;
        float pos[3]={W(16,16,0),W(16,16,1),0},vel[2]={.5f*dx,.5f*dy};
        float h=atan2f(dy,dx);
        assert(rig_drive(&r,pos,vel,h,60)>0);
        float lx=c*(pos[0]-ox)+sn*(pos[1]-oy),ly=-sn*(pos[0]-ox)+c*(pos[1]-oy);
        float fx=lx+1.03f*dlx,fy=ly+1.03f*dly;  /* capsule front axis end */
        printf("concave building reflex corner: front axis end (%.4f %.4f), radius 0.94\n",fx,fy);
        assert(fx>=8.93f && fy>=8.93f);
    }
    {   /* across the notch's would-be hull diagonal: no contact until the face */
        float dx=-c,dy=-sn,pos[3]={W(34,14,0),W(34,14,1),0},vel[2]={.5f*dx,.5f*dy};
        int hits=rig_drive(&r,pos,vel,atan2f(dy,dx),47);
        float lx=c*(pos[0]-ox)+sn*(pos[1]-oy);
        printf("concave building notch: hits %d at x %.4f (face 8 + 1.97)\n",hits,lx);
        assert(hits==0 && lx<10.6f);  /* crossed the hull diagonal (x+y=28 at y=14: x=14) freely */
        hits=rig_drive(&r,pos,vel,atan2f(dy,dx),10);
        lx=c*(pos[0]-ox)+sn*(pos[1]-oy);
        printf("concave building notch face: hits %d stop x %.4f\n",hits,lx);
        assert(hits>0 && lx>9.969f && lx<10.2f);
    }
#undef W
    rig_free(&r);
}

/* Curbs and measured guardrail heights, perpendicular at 30 m/s from both
 * sides: a 0.15 m curb is no wall, 0.55/0.9/1.8 m barriers never tunnel. */
static void fixture_curbs_and_guardrails(void) {
    const float th=.35f,c=cosf(th),sn=sinf(th);
    const float heights[]={.15f,.55f,.9f,1.8f};
    for(int k=0;k<4;k++)for(int side=-1;side<=1;side+=2) {
        float w[1][4][3];rig_wall(w[0],-10*sn,10*c,0,10*sn,-10*c,0,heights[k]); /* line through 0 along (sn,-c) */
        Rig r;rig_build(&r,RIG_FLOOR,1,(const float (*)[4][3])w,1);
        float pos[3]={side*8*c,side*8*sn,0},vel[2]={-side*.5f*c,-side*.5f*sn};
        int hits=rig_drive(&r,pos,vel,th,40);
        float d=side*(pos[0]*c+pos[1]*sn);
        printf("barrier %.2f m side %+d: hits %d distance %.4f\n",heights[k],side,hits,d);
        if(heights[k]<WALL_MIN_FACE_SPAN) assert(r.n==0 && hits==0 && d<-10);
        else assert(r.n==1 && hits>0 && d>1.969f && d<2.2f);
        rig_free(&r);
    }
}

/* A guardrail built from collinear 4 m segments: a shallow 30 m/s graze must
 * slide along the whole run. Exact joints, a 1 cm gap and a 5 mm step toward
 * the road test seam snagging; the car may never cross to the far side. */
static float guardrail_slide(float gap,float step,float angle) {
    /* The body starts beside the first segment; it never meets a free end. */
    float walls[10][4][3];
    for(int k=0;k<10;k++) {
        float x0=k*4.0f,x1=x0+4-gap,y=(k&1)?step:0;
        rig_wall(walls[k],x0,y,0,x1,y,0,1.0f);
    }
    float c=cosf(angle),sn=sinf(angle);
    for(int k=0;k<10;k++)for(int v=0;v<4;v++) {
        float x=walls[k][v][0],y=walls[k][v][1];walls[k][v][0]=c*x-sn*y;walls[k][v][1]=sn*x+c*y;
    }
    Rig r;rig_build(&r,RIG_FLOOR,1,(const float (*)[4][3])walls,10);
    float lx=2.5f,ly=-1.0f,vx=.5f*cosf(.17f),vy=.5f*sinf(.17f);
    float pos[3]={c*lx-sn*ly,sn*lx+c*ly,0},vel[2]={c*vx-sn*vy,sn*vx+c*vy};
    int ticks=60;rig_drive(&r,pos,vel,angle,ticks);
    float fx=c*pos[0]+sn*pos[1],fy=-sn*pos[0]+c*pos[1];
    float tv=c*vel[0]+sn*vel[1];
    printf("guardrail gap %.3f step %.3f angle %.2f: along %.3f lateral %.4f along-speed %.4f m/tick\n",
           gap,step,angle,fx,fy,tv);
    assert(fy<=-.939f+(step>0?step:0) && fy>-1.5f);
    rig_free(&r);
    return fx;
}
static void fixture_guardrail_seams(void) {
    for(int a=0;a<2;a++) {
        float angle=a?-.83f:0;
        float clean=guardrail_slide(0,0,angle);
        assert(clean>25);
        assert(guardrail_slide(.01f,0,angle)>clean-.5f);
        /* Alternate segments 5 mm nearer the road: the rounded body rides
           over the joint ends instead of snagging on each end face. */
        assert(guardrail_slide(0,-.005f,angle)>clean-.5f);
    }
}

/* Graded road: a 1 m rail following an 8% grade still slides the body
 * uphill, and a 0.2 m curb following the grade (2 m+ total Z span) is
 * crossed without a wall response. */
static void fixture_slope(void) {
    const float g=.08f;
    const float ground[1][4][3]={{{-30,-60,-60*g},{30,-60,-60*g},{30,60,60*g},{-30,60,60*g}}};
    float walls[6][4][3];
    for(int k=0;k<5;k++)rig_wall(walls[k],5,-50+20*k,(-50+20*k)*g,5,-30+20*k,(-30+20*k)*g,1.0f);
    rig_wall(walls[5],-3,-50,-50*g,-3,50,50*g,.2f);
    Rig r;rig_build(&r,ground,1,(const float (*)[4][3])walls,6);
    assert(r.n==5);
    float pos[3]={3.8f,-40,-40*g},vel[2]={.03f,.4f};
    rig_drive(&r,pos,vel,1.5707963f,150);
    printf("slope rail: x %.4f y %.3f z %.3f along-speed %.4f\n",pos[0],pos[1],pos[2],vel[1]);
    assert(pos[0]<=5-.939f && pos[0]>3.5f && pos[1]>15 && fabsf(pos[2]-pos[1]*g)<.01f);
    float cross[3]={2,10,10*g},cv[2]={-.3f,0};
    int hits=rig_drive(&r,cross,cv,3.14159265f,40);
    printf("slope curb: hits %d x %.3f\n",hits,cross[0]);
    assert(hits==0 && cross[0]<-9);
    rig_free(&r);
}

/* Stacked layers: a deck at z=8 with 1 m parapets over a ground-level 3 m
 * divider. Parapets stop deck traffic, never ground traffic beneath them; the
 * divider stops ground traffic, never deck traffic above it. */
static void fixture_vertical_overlap(void) {
    const float ground[2][4][3]={
        {{-60,-60,0},{60,-60,0},{60,60,0},{-60,60,0}},
        {{-20,-10,8},{20,-10,8},{20,10,8},{-20,10,8}}};
    float walls[3][4][3];
    rig_wall(walls[0],-20,10,8,20,10,8,1);
    rig_wall(walls[1],20,-10,8,-20,-10,8,1);
    rig_wall(walls[2],0,-40,0,0,40,0,3);
    Rig r;rig_build(&r,ground,2,(const float (*)[4][3])walls,3);
    assert(r.n==3);
    float under[3]={-8,-30,0},uv[2]={0,.5f};      /* ground road beneath both parapets */
    int hits=rig_drive(&r,under,uv,1.5707963f,120);
    printf("under deck: hits %d y %.2f z %.2f\n",hits,under[1],under[2]);
    assert(hits==0 && under[1]>29 && fabsf(under[2])<1e-4f);
    float over[3]={-12,0,8},ov[2]={.5f,0};        /* deck across the divider below */
    hits=rig_drive(&r,over,ov,0,40);
    printf("over divider: hits %d x %.2f z %.2f\n",hits,over[0],over[2]);
    assert(hits==0 && over[0]>7.9f && fabsf(over[2]-8)<1e-4f);
    float deck[3]={10,0,8},dv[2]={0,.5f};          /* deck traffic into a parapet */
    hits=rig_drive(&r,deck,dv,1.5707963f,40);
    printf("deck parapet: hits %d y %.4f\n",hits,deck[1]);
    assert(hits>0 && deck[1]<=10-1.969f && deck[1]>7.5f);
    float low[3]={-8,0,0},lv[2]={.5f,0};           /* ground traffic into the divider */
    hits=rig_drive(&r,low,lv,0,40);
    assert(hits>0 && low[0]<=-1.969f && low[0]>-2.3f);
    rig_free(&r);
}

/* A duplicated packed polygon (same face, second source row) must respond
 * exactly like the single authored face: no double push, no extra stop. */
static void fixture_duplicate_polygon(void) {
    float one[1][4][3],two[2][4][3];
    rig_wall(one[0],10,-20,0,10,20,0,2);memcpy(two[0],one[0],sizeof one[0]);memcpy(two[1],one[0],sizeof one[0]);
    float result[2][4];
    for(int k=0;k<2;k++) {
        Rig r;rig_build(&r,RIG_FLOOR,1,k?(const float (*)[4][3])two:(const float (*)[4][3])one,k+1);
        float pos[3]={0,0,0},vel[2]={.5f*cosf(.4f),.5f*sinf(.4f)};
        rig_drive(&r,pos,vel,.4f,60);
        result[k][0]=pos[0];result[k][1]=pos[1];result[k][2]=vel[0];result[k][3]=vel[1];
        rig_free(&r);
    }
    printf("duplicate polygon: single (%.5f %.5f) duplicated (%.5f %.5f)\n",
           result[0][0],result[0][1],result[1][0],result[1][1]);
    for(int i=0;i<4;i++)assert(fabsf(result[0][i]-result[1][i])<1e-5f);
}
int main(void) {
    bun_walls();bun_event_walls();
    fixture_rotated_concave_building();fixture_curbs_and_guardrails();fixture_guardrail_seams();
    fixture_slope();fixture_vertical_overlap();fixture_duplicate_polygon();
    roadside();boundary_outline();planter_materials();sloped_boundary();compiled_rules();buildings();repeated_models();source_building_walls();pilot_boundaries();pilot_roadside_boundary();
    static const float replacement[][3]={{16,-4,0},{16,4,0}},standalone[][3]={{40,-4,0},{40,4,0}};
    static const WCollisionRule rules[]={
        {0,10,0,0,NULL},{0,11,8,0,NULL},{1,202,8,2,replacement},{2,0,8,2,standalone}
    };
    const WCollisionEdits e={"TEST",4,rules};
    N2Scene s={0};s.count=s.cap=6;s.meshes=calloc(6,sizeof *s.meshes);assert(s.meshes);
    s.meshes[0]=mesh(10,101,0,1);s.meshes[1]=mesh(10,102,4,1);
    s.meshes[2]=mesh(20,103,6,0); /* same name, different model: keep */
    s.meshes[3]=mesh(11,202,12,0);s.meshes[4]=mesh(11,202,12,0);
    s.meshes[5]=mesh(11,203,30,0);
    uint64_t ids[6];float saved[6][40];uint16_t indices[6][12];
    for (int n=0;n<6;n++) {ids[n]=n2_world_source_id(s.meshes+n);memcpy(saved[n],s.meshes[n].verts,sizeof saved[n]);
        memcpy(indices[n],s.meshes[n].idx,sizeof indices[n]);}
    assert(contact(s.meshes,.1f,0));assert(wce_apply(&e,&s,1)&&s.count==7);
    for (int n=0;n<6;n++) {
        assert(ids[n]==n2_world_source_id(s.meshes+n));
        assert(!memcmp(saved[n],s.meshes[n].verts,sizeof saved[n]));
        assert(!memcmp(indices[n],s.meshes[n].idx,sizeof indices[n]));
    }
    assert(!contact(s.meshes,.1f,0)&&!contact(s.meshes+1,4.1f,0));
    assert(contact(s.meshes+2,6.1f,0));assert(!contact(s.meshes+3,12.1f,0));
    assert(contact(s.meshes+3,16.1f,6)&&!contact(s.meshes+4,16.1f,6));
    assert(contact(s.meshes+5,30.1f,6)&&!contact(s.meshes+5,30.1f,9));
    assert(contact(s.meshes+6,40.1f,6)&&s.meshes[6].cat==N2_COLLISION);
    N2Scene invisible={s.meshes+6,1,1};float bounds[][4]={{40,-4,40,4}};RhiTexture textures[]={{0}};
    WorldBatchUpload *upload=upload_world_batches_begin(&invisible,bounds,textures,(RhiTexture){0},NULL,NULL);
    N2Batch *batches=NULL;int nbatch=0;assert(upload);
    assert(upload_world_batches_step(&upload,1,&batches,&nbatch,NULL)==1&&!upload&&nbatch==0);
    free(batches);
    float faces[9];assert(!phys_wall_debug_face(s.meshes,0,.3f,faces));
    float ground=99;N2Scene terrain={s.meshes,2,2};
    assert(world_ground_at(&terrain,-1,0,1,&ground)!=WSURF_NONE&&fabsf(ground)<1e-6f);
    float from[]={-1,0,1},to[]={-1,0,-1};assert(world_ground_sweep(&terrain,from,to,NULL)<1);
    float anchor[]={-2,0,.5f},eye[]={2,0,.5f};N2Scene one={s.meshes,1,1};
    assert(world_camera_clip(&one,NULL,anchor,eye,.1f)<1); /* visible source facade, body walls replaced by BUN */
    one.meshes=s.meshes+3;anchor[0]=14;eye[0]=18;anchor[2]=eye[2]=6;
    assert(world_camera_clip(&one,NULL,anchor,eye,.1f)<1);
    float obst[8][4],z[8][2];int src[8],count=phys_collect_walls(&s,obst,src,z,8);
    assert(count==4);
    const float bb[]={-.8f,-.4f,0,.8f,.4f,1.4f};
    for (int side=-1;side<=1;side+=2) {
        float old[]={40+4*side,0,6},pos[]={40-4*side,0,6},vel[]={-side*2,0};
        assert(world_body_walls_move(&s,old,pos,vel,0,bb,6,7.4f,-.5f,
            obst,z,count,src,NULL,0,NULL,NULL)>0);
        assert(side*(pos[0]-40)>.79f);
    }
    for (int n=0;n<s.count;n++) {free(s.meshes[n].verts);free(s.meshes[n].idx);
        free(s.meshes[n].wall_verts);free(s.meshes[n].wall_idx);}
    free(s.meshes);puts("world_collision_test: PASS");return 0;
}
