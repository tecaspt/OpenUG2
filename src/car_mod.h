#ifndef OPENUG2_CAR_MOD_H
#define OPENUG2_CAR_MOD_H
#include "nfsu2.h"
#include <limits.h>
#include <dirent.h>

static const char *const n2_mod_libraries[] = {
    "", "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF"
};

static void n2_mod_option(N2PartMenu *menu,int value,const char *label) {
    for(int i=0;i<menu->count;i++)if(menu->options[i].value==value)return;
    if(menu->count>=256)return;
    N2PartOption *o=&menu->options[menu->count++];o->value=value;
    snprintf(o->label,sizeof o->label,"%s",label);
}

/* Enumerate object names, not arbitrary byte runs or guessed numeric ranges. */
typedef struct { int library; N2PartMenu *menus; } N2ModWalk;
static int n2_mod_catalog_chunk(const unsigned char *d,uint32_t tag,long p,long end,void *context) {
    N2ModWalk *walk=(N2ModWalk *)context;
    int library=walk->library;N2PartMenu *menus=walk->menus;
    long size=end-p;
    if(tag==0x80134010u) {
        char name[64],label[48];n2_car_mesh_name(d,p,p+size,name);
        int num=0;long at=0,n=0;int kind=n2_name_variant((unsigned char *)name,strlen(name),&num,&at,&n);
        int slot=n2_car_part(name);
        if(library)slot=library<=3?N2_PART_SPOILER:library==4?N2_PART_EXHAUST:N2_PART_SCOOP;
        if(slot>=0 && kind && !strstr(name,"_CF") && !strstr(name,"WIDE") && !strstr(name,"KITW")) {
            int value=(kind-1)*100+num+1;
            N2CarConfig cfg={.body_kit=num,.hood_style=num};
            if(!library || slot==N2_PART_EXHAUST)cfg.parts[slot]=value;
            N2Scene probe={0};n2_walk_car(d,p-8,p+size,&probe,NULL,0,&cfg,0);
            int renderable=probe.count>0;n2_free_scene(&probe);
            if(!renderable)return 0; /* named empty placeholders */
            if(library) {
                int variant=strstr(name,"_DUAL")?1:strstr(name,"_OFFSET")?2:0;
                value=library*1000+variant*100+num;
                const char *type=library==1?"Coupe":library==2?"Hatch":library==3?"SUV":
                                 library==4?"Tip":variant==1?"Dual":variant==2?"Offset":"Single";
                snprintf(label,sizeof label,"Style %02d (%s)",num,type);
            } else if(kind==1 && !num)snprintf(label,sizeof label,"Stock");
            else snprintf(label,sizeof label,"%s %02d",kind==1?"Kit":"Style",num);
            n2_mod_option(menus+slot,value,label);
        }
        return 0;
    }
    return (tag>>28)==8;
}
static void n2_mod_catalog_walk(const unsigned char *d,long beg,long end,
                                int library,N2PartMenu menus[N2_PART_COUNT]) {
    N2ModWalk walk={library,menus};
    asset_chunks_walk(d,beg,end,n2_mod_catalog_chunk,&walk);
}

static void n2_mod_catalog(const char *root,const unsigned char *data,long len,
                            const N2Scene *car,N2PartMenu menus[N2_PART_COUNT]) {
    memset(menus,0,sizeof(N2PartMenu)*N2_PART_COUNT);
    for(int i=0;i<N2_PART_COUNT;i++)n2_mod_option(menus+i,0,i<N2_PART_SPOILER?"Follow kit preset":"Stock / none");
    n2_mod_catalog_walk(data,0,len,0,menus);
    for(int lib=1;lib<=5;lib++) {
        float m[16];
        int supported=lib<=3?n2_car_socket(data,len,car,0xc93b73fdu,m)==1:
                      lib==5?n2_car_socket(data,len,car,0x90c81258u,m)==1:
                      n2_car_socket(data,len,car,0xbcf8a18bu,m)==1 || n2_car_socket(data,len,car,0xbd7cf15eu,m)==1;
        if(!supported)continue;
        char path[1024];long bytes=0;
        snprintf(path,sizeof path,"%s/CARS/%s/GEOMETRY.BIN",root,n2_mod_libraries[lib]);
        unsigned char *d=n2_read_file(path,&bytes);
        if(d)n2_mod_catalog_walk(d,0,bytes,lib,menus);
        free(d);
    }
    for(int part=0;part<N2_PART_COUNT;part++) {
        N2PartMenu *m=menus+part;
        for(int i=1;i<m->count;i++) {
            N2PartOption option=m->options[i];int j=i;
            while(j && m->options[j-1].value>option.value){m->options[j]=m->options[j-1];j--;}
            m->options[j]=option;
        }
    }
}

/* Append complete material slices at the car's named socket. Spoilers/scoops
 * use source XYZ; exhaust tips use the same axis conversion as stock exhausts.
 * Painted library objects have empty texture packs and use material colours.
 * Carbon variants are excluded until their shared texture binding is supported. */
static int n2_mod_attach(const char *root,const unsigned char *data,long len,
                         int slot,int value,N2Scene *car) {
    int lib=value/1000,variant=value%1000/100,style=value%100;
    if(!root || lib<1 || lib>5 || !style || variant>2 ||
       (lib<=3 && slot!=N2_PART_SPOILER) || (lib==4 && slot!=N2_PART_EXHAUST) ||
       (lib==5 && slot!=N2_PART_SCOOP) || (lib!=5 && variant))return 0;
    float sockets[2][16];int ns=0;
    const uint32_t keys[]={slot==N2_PART_SPOILER?0xc93b73fdu:
                           slot==N2_PART_SCOOP?0x90c81258u:0xbcf8a18bu,0xbd7cf15eu};
    for(int i=0;i<(slot==N2_PART_EXHAUST?2:1);i++) {
        int found=n2_car_socket(data,len,car,keys[i],sockets[ns]);
        if(found<0)return 0;
        ns+=found;
    }
    if(!ns)return 0;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/%s/GEOMETRY.BIN",root,n2_mod_libraries[lib]);
    unsigned char *d=n2_read_file(path,&bytes);if(!d)return 0;
    N2CarConfig cfg={.hood_style=style};cfg.parts[N2_PART_EXHAUST]=101+style;
    N2Scene part={0};n2_walk_car(d,0,bytes,&part,NULL,0,&cfg,0);
    int kept=0;
    for(int i=0;i<part.count;i++) {
        N2Mesh *m=part.meshes+i;char name[64];long off=m->car_source;
        n2_car_mesh_name(d,off,off+n2_u32(d+off-4),name);
        int v=strstr(name,"_DUAL")?1:strstr(name,"_OFFSET")?2:0;
        if(strstr(name,"_CF") || v!=variant) {free(m->verts);free(m->idx);continue;}
        part.meshes[kept++]=*m;
    }
    part.count=kept;free(d);n2_car_dedupe_lod(&part);
    if(!part.count){n2_free_scene(&part);return 0;}
    int old=car->count,total=old+part.count*ns;
    N2Mesh *grown=(N2Mesh *)realloc(car->meshes,(size_t)total*sizeof *grown);
    if(!grown){n2_free_scene(&part);return 0;}car->meshes=grown;car->cap=total;
    for(int k=0;k<ns;k++)for(int i=0;i<part.count;i++) {
        N2Mesh *src=part.meshes+i,*dst=car->meshes+car->count++;
        *dst=*src;dst->car_source=0;dst->famkey=0;dst->car_part=(unsigned char)(slot+1);
        dst->verts=(float *)malloc((size_t)src->nverts*(src->authored_normals?8:5)*sizeof(float));
        dst->idx=(uint16_t *)malloc((size_t)src->nidx*sizeof(uint16_t));dst->vcol=NULL;
        if(!dst->verts || !dst->idx){n2_free_scene(&part);return 0;}
        memcpy(dst->verts,src->verts,(size_t)src->nverts*(src->authored_normals?8:5)*sizeof(float));
        memcpy(dst->idx,src->idx,(size_t)src->nidx*sizeof(uint16_t));
        n2_car_transform(dst,sockets[k],slot==N2_PART_EXHAUST);
    }
    n2_free_scene(&part);return 1;
}

/* Traffic bodies have no rear socket. Fit the shared plate to their indexed
 * rear skin at a host-selected height, rather than to the bumper bounding box. */
static int n2_mod_traffic_plate_hit(const N2Scene *car,float y,float z,
                                     float *rear,float *slope) {
    float best=INFINITY,dz=0;
    for(int i=0;i<car->count;i++) {
        const N2Mesh *m=car->meshes+i;
        if(m->car_material!=0x19e0ecbeu || m->car_mount!=N2_MOUNT_BODY)continue; /* TRAFFIC */
        for(int j=0;j+2<m->nidx;j+=3) {
            if(m->idx[j]>=m->nverts || m->idx[j+1]>=m->nverts || m->idx[j+2]>=m->nverts)return -1;
            const float *a=m->verts+m->idx[j]*5,*b=m->verts+m->idx[j+1]*5,*c=m->verts+m->idx[j+2]*5;
            float nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
            if(!isfinite(nx))return -1;
            if(nx>=-1e-7f)continue;
            float u=((y-a[1])*(c[2]-a[2])-(z-a[2])*(c[1]-a[1]))/nx;
            float v=((b[1]-a[1])*(z-a[2])-(b[2]-a[2])*(y-a[1]))/nx;
            if(u<-.0001f || v<-.0001f || u+v>1.0001f)continue;
            float x=a[0]+u*(b[0]-a[0])+v*(c[0]-a[0]);
            float nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            if(!isfinite(x) || !isfinite(nz))return -1;
            if(x<-.1f && x<best){best=x;dz=-nz/nx;}
        }
    }
    if(!isfinite(best))return 0;
    *rear=best;*slope=dz;return 1;
}

static int n2_mod_plate_frame(const unsigned char *data,long len,const N2Scene *car,float socket[16]) {
    int found=n2_car_socket_frame(data,len,car,0xc805f7e0u,socket,1); /* REAR_LICENSE_PLATE */
    if(found)return found;
    float cy=0,cz=.58f,scale=1;
    /* Host placement hints: these indexed faces bound the visible plate recess.
     * Derive the mount from their vertices; traffic files provide no marker. */
    static const struct {const char *body;int face[4];} recess[]={
        {"MINIVAN_BODY_A",{386,387,-1,-1}},
        {"SUV_BODY_A",{54,55,207,208}},
        {"DR_SEDAN02_BODY_A",{348,349,-1,-1}},
        {"TAXI02_BODY_A",{351,352,-1,-1}},
        {"PANELVAN_BODY_A",{115,116,-1,-1}}
    };
    for(int i=0;i<car->count;i++) {
        const N2Mesh *m=car->meshes+i;
        if(m->car_material!=0x19e0ecbeu || m->car_mount!=N2_MOUNT_BODY)continue;
        char name[64];n2_car_source_name(data,len,m,name);
        for(unsigned r=0;r<sizeof recess/sizeof *recess;r++)if(!strcmp(name,recess[r].body)) {
            float ymin=INFINITY,ymax=-INFINITY,zmin=INFINITY,zmax=-INFINITY;
            for(int f=0;f<4 && recess[r].face[f]>=0;f++) {
                int first=recess[r].face[f]*3;
                if(first+2>=m->nidx)return -1;
                for(int k=0;k<3;k++) {
                    int v=m->idx[first+k];if(v>=m->nverts)return -1;
                    float y=m->verts[v*5+1],z=m->verts[v*5+2];
                    if(!isfinite(y) || !isfinite(z))return -1;
                    ymin=fminf(ymin,y);ymax=fmaxf(ymax,y);zmin=fminf(zmin,z);zmax=fmaxf(zmax,z);
                }
            }
            cy=(ymin+ymax)*.5f;cz=(zmin+zmax)*.5f;
            scale=fminf(1,fminf((ymax-ymin)*.9f/.305f,(zmax-zmin)*.9f/.143f));
            if(scale<.5f)return -1;
        }
    }
    float rear,slope;found=n2_mod_traffic_plate_hit(car,cy,cz,&rear,&slope);
    if(found!=1)return found;
    if(fabsf(slope)>1)return -1;
    float upz=1/sqrtf(1+slope*slope),upx=slope*upz,offset=0;
    /* Sample beyond STYLE01's 0.305 x 0.143 m footprint to clear the skin. */
    for(int y=-1;y<=1;y++)for(int z=-1;z<=1;z++) {
        float x,unused;
        found=n2_mod_traffic_plate_hit(car,cy+y*.155f*scale,cz+z*.073f*scale*upz,&x,&unused);
        if(found!=1)return found;
        offset=fminf(offset,x-(rear+z*.073f*scale*upx));
    }
    const float frame[]={upx*scale,0,upz*scale,0, 0,scale,0,0, -upz*scale,0,upx*scale,0,
                         rear+offset+upx*.071f*scale-.003f,cy,cz+upz*.071f*scale,1};
    memcpy(socket,frame,sizeof frame);return 1;
}

static int n2_mod_plate_data(const unsigned char *data,long len,N2Scene *car,
                             const unsigned char *plate,long bytes,
                             const uint32_t *keys,int nkeys) {
    for(int i=0;i<car->count;i++)if(car->meshes[i].car_material==N2_MAT_LICENSEPLATE)return 0;
    float socket[16];int found=n2_mod_plate_frame(data,len,car,socket);
    if(found!=1)return found;
    if(!plate || bytes<=0)return -1;
    N2CarConfig cfg={.hood_style=1};N2Scene part={0};
    n2_load_car(plate,bytes,&part,keys,nkeys,&cfg);
    if(!part.count)goto failed;
    float scale[3];
    for(int a=0;a<3;a++) {
        scale[a]=hypotf(hypotf(socket[a*4],socket[a*4+1]),socket[a*4+2]);
        for(int k=0;k<3;k++)socket[a*4+k]/=scale[a];
    }
    for(int i=0;i<part.count;i++) {
        N2Mesh *m=part.meshes+i;
        if(m->car_material!=N2_MAT_LICENSEPLATE || m->texkey!=N2_TEX_LICENSEPLATE)goto failed;
        /* Preserve authored width/height scales; normals use inverse scale. */
        for(int v=0;v<m->nverts;v++)for(int a=0;a<3;a++) {
            int axis=a==0?2:a==2?0:1;
            m->verts[v*5+a]*=scale[axis];
            if(m->authored_normals)m->verts[m->nverts*5+v*3+a]/=scale[axis];
        }
        /* The plate's -X normal/+Z up use the same socket axes as exhausts. */
        n2_car_transform(m,socket,1);
        m->car_source=0;m->famkey=0;m->namekey=0;m->tierid=0;
        m->vkind=0;m->vnum=0;m->car_part=0;m->car_mount=N2_MOUNT_BODY;
    }
    if(part.count>INT_MAX-car->count)goto failed;
    int total=car->count+part.count;
    N2Mesh *grown=(N2Mesh *)realloc(car->meshes,(size_t)total*sizeof *grown);
    if(!grown)goto failed;
    car->meshes=grown;car->cap=total;
    memcpy(car->meshes+car->count,part.meshes,(size_t)part.count*sizeof *part.meshes);
    car->count=total;free(part.meshes);return 1;
failed:
    n2_free_scene(&part);return -1;
}

static int n2_mod_add_plate(const char *root,const unsigned char *data,long len,
                            N2Scene *car) {
    float socket[16];int found=n2_mod_plate_frame(data,len,car,socket);
    if(found!=1)return found;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/PLATES/GEOMETRY.BIN",root);
    unsigned char *plate=n2_read_file(path,&bytes);if(!plate)return -1;
    /* This library references GLOBALB, independently of the car's own TPK. */
    const uint32_t key=N2_TEX_LICENSEPLATE;
    int result=n2_mod_plate_data(data,len,car,plate,bytes,&key,1);
    free(plate);return result;
}

static int n2_mod_assemble(const char *root,const unsigned char *data,long len,
                           const N2CarConfig *cfg,N2Scene *car) {
    for(int p=0;p<N2_PART_COUNT;p++) {
        int value=cfg->parts[p];if(!value)continue;
        if(value>=1000) {if(!n2_mod_attach(root,data,len,p,value,car))return 0;}
        else {
            if(value<1 || value>200)return 0;
            int found=0;
            for(int i=0;i<car->count;i++) {
                N2Mesh *m=car->meshes+i;
                if(m->car_part==p+1 && m->vkind==(value-1)/100+1 && m->vnum==(value-1)%100)found=1;
            }
            if(!found)return 0;
        }
    }
    return 1;
}

/* [inferred] Garage choices are exposed only after full CPU assembly succeeds. */
#include "garage.h"
static int n2_mod_garage_scene(const char *root,const unsigned char *data,long len,
                              const N2CarConfig *cfg,N2Scene *scene) {
    if(n2_load_car(data,len,scene,NULL,0,cfg)<=0 || !n2_mod_assemble(root,data,len,cfg,scene))return 0;
    int triangles=0;
    for(int i=0;i<scene->count;i++) {
        N2Mesh *m=scene->meshes+i;triangles+=m->nidx/3;
        if(m->nverts<1||!m->verts||!m->idx)return 0;
        for(int v=0;v<m->nverts;v++)for(int a=0;a<3;a++)if(!isfinite(m->verts[v*5+a]))return 0;
        for(int j=0;j<m->nidx;j++)if(m->idx[j]>=m->nverts)return 0;
    }
    if(!triangles)return 0;
    if(cfg->body_kit) {
        int found=0;for(int i=0;i<scene->count;i++)if(scene->meshes[i].vkind==1&&scene->meshes[i].vnum==cfg->body_kit)found=1;
        if(!found)return 0;
    }
    for(int slot=0;slot<N2_PART_COUNT;slot++)if(cfg->parts[slot]) {
        int found=0,value=cfg->parts[slot];
        for(int i=0;i<scene->count;i++) {
            N2Mesh *m=scene->meshes+i;
            if(m->car_part!=slot+1)continue;
            if(value>=1000 || (m->vkind==(value>=101?2:1) && m->vnum==(value>=101?value-101:value-1)))found=1;
        }
        if(!found)return 0;
    }
    return 1;
}
static int n2_mod_garage_catalog(const char *root,const char *name,GarageCatalog *out) {
    if(!root||!name||!out||strlen(name)>=sizeof out->car)return 0;
    for(const char *p=name;*p;p++)if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))return 0;
    char path[1024];snprintf(path,sizeof path,"%s/CARS/%s/GEOMETRY.BIN",root,name);
    long len=0;unsigned char *data=n2_read_file(path,&len);if(!data)return 0;
    memset(out,0,sizeof *out);snprintf(out->car,sizeof out->car,"%s",name);
    N2CarConfig cfg={0};N2Scene stock={0};int ok=n2_mod_garage_scene(root,data,len,&cfg,&stock);
    if(ok) {
        n2_mod_catalog(root,data,len,&stock,out->menu);
        int ids[100],n=n2_car_variant_numbers(data,len,1,ids,100);
        n2_mod_option(&out->menu[GARAGE_KIT],0,"Stock");
        for(int i=0;i<n;i++){char label[48];snprintf(label,sizeof label,"Kit %02d",ids[i]);n2_mod_option(&out->menu[GARAGE_KIT],ids[i],label);}
        for(int slot=0;slot<GARAGE_SLOTS;slot++) {
            N2PartMenu *m=&out->menu[slot];int kept=0;
            for(int i=0;i<m->count;i++) {
                cfg=(N2CarConfig){0};int value=m->options[i].value;
                if(slot==GARAGE_KIT)cfg.body_kit=value;else cfg.parts[slot]=value;
                N2Scene probe={0};int builds=n2_mod_garage_scene(root,data,len,&cfg,&probe);n2_free_scene(&probe);
                if(builds)m->options[kept++]=m->options[i];
            }
            m->count=kept;
        }
    }
    n2_free_scene(&stock);free(data);return ok;
}

/* [data] Enumerate installed wheel archives and their STYLE variants; no numeric guesses. */
static int n2_mod_rim_scene(const unsigned char *data,long len,const unsigned char *textures,long tlen,
                            const uint32_t *keys,int nkeys,int style,N2Scene *scene) {
    N2CarConfig cfg={.hood_style=style,.wheel_style_id=style};
    if(n2_load_car(data,len,scene,keys,nkeys,&cfg)<=0 || n2_rim_select_tier(scene)<=0 ||
       scene->meshes[0].vkind!=2 || scene->meshes[0].vnum!=style)return 0;
    int triangles=0;
    for(int i=0;i<scene->count;i++) {
        const N2Mesh *m=scene->meshes+i;
        if(!m->verts||!m->idx||m->nverts<1)return 0;
        triangles+=m->nidx/3;
        for(int v=0;v<m->nverts;v++)for(int a=0;a<3;a++)if(!isfinite(m->verts[v*5+a]))return 0;
        for(int j=0;j<m->nidx;j++)if(m->idx[j]>=m->nverts)return 0;
    }
    N2Tex tex={0};int ok=triangles>0 && scene->meshes[0].texkey && n2_load_car_tex_by_key(textures,tlen,scene->meshes[0].texkey,&tex) && tex.rgb && tex.w>0 && tex.h>0;
    free(tex.rgb);free(tex.alpha);free(tex.dxt);return ok;
}
static int n2_mod_rim_catalog(const char *root,GarageRims *out) {
    if(!root||!out)return 0;
    memset(out,0,sizeof *out);char folder[1024],file[1100];
    snprintf(folder,sizeof folder,"%s/CARS/WHEELS",root);
    snprintf(file,sizeof file,"%s/TEXTURES.BIN",folder);long tlen=0;
    unsigned char *textures=n2_read_file(file,&tlen);if(!textures)return 0;
    uint32_t keys[512];int nkeys=n2_car_tex_keys(textures,tlen,keys,512);
    DIR *dir=opendir(folder);if(!dir){free(textures);return 0;}struct dirent *e;int overflow=0;
    while((e=readdir(dir))) {
        size_t n=strlen(e->d_name);
        if(n<14||strncmp(e->d_name,"GEOMETRY_",9)||strcmp(e->d_name+n-4,".BIN")||n-13>=24)continue;
        char brand[24]={0};memcpy(brand,e->d_name+9,n-13);
        int valid=1;for(const char *p=brand;*p;p++)if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))valid=0;
        if(!valid)continue;
        snprintf(file,sizeof file,"%.1023s/%.36s",folder,e->d_name);long len=0;unsigned char *data=n2_read_file(file,&len);if(!data)continue;
        int ids[100],count=n2_car_variant_numbers(data,len,2,ids,100);
        for(int k=0;k<count;k++) {
            N2Scene scene={0};int ok=n2_mod_rim_scene(data,len,textures,tlen,keys,nkeys,ids[k],&scene);n2_free_scene(&scene);
            if(!ok)continue;
            if(out->count>=GARAGE_MAX_RIMS){overflow=1;break;}
            GarageRimOption *option=out->options+out->count++;
            snprintf(option->brand,sizeof option->brand,"%s",brand);option->style=ids[k];
        }
        free(data);if(overflow)break;
    }
    closedir(dir);free(textures);
    if(overflow){out->count=0;return 0;}
    for(int i=1;i<out->count;i++) {
        GarageRimOption value=out->options[i];int j=i;
        while(j && (strcmp(out->options[j-1].brand,value.brand)>0 ||
              (!strcmp(out->options[j-1].brand,value.brand)&&out->options[j-1].style>value.style))) {
            out->options[j]=out->options[j-1];j--;
        }
        out->options[j]=value;
    }
    return out->count>0;
}
#endif
