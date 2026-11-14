#include "game_catalog.h"
#include "world.h"
#include "resource.h"
#include "race.h"
#include "ugui/ugui_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int gcat_cmp(const void *a,const void *b) {
    const GcEvent *x=a,*y=b;
    if(x->kind!=y->kind)return x->kind<y->kind?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}

int gcat_build(GameCatalog *c,const char *dataroot) {
    if(!c || !dataroot)return 0;
    memset(c,0,sizeof *c);
    char troot[1024];snprintf(troot,sizeof troot,"%s/TRACKS",dataroot);
    char tl[16][64];
    int nt,sel=0;
    nt=res_list_tracks(troot,tl,16,"",&sel);
    World *w=calloc(1,sizeof *w);
    if(!w)return 0;
    for(int k=0;k<nt && w->neighborhood.nreg<16;k++) {
        if(!strcmp(tl[k],"ALL") || !strcmp(tl[k],"STREAML4RH") || !strcmp(tl[k],"STREAML4RR"))continue;
        if(strncmp(tl[k],"STREAM",6))continue;
        snprintf(w->neighborhood.rgn[w->neighborhood.nreg++].name,64,"%.63s",tl[k]);
    }
    world_load_events(w,troot);
    for(int k=0;k<w->city.nev && c->nev<GC_MAX_EVENTS;k++) {
        const WEvent *e=w->city.ev+k;
        if(e->info.kind==N2_RACE_UNKNOWN)continue;
        GcEvent *g=c->ev+c->nev++;
        char key[32];
        g->id=e->id;snprintf(g->region,sizeof g->region,"%s",e->reg);
        g->kind=e->info.kind;g->circuit=e->circuit;g->downhill=e->info.downhill;
        g->length_m=e->info.length>0?e->info.length:e->len100m*100;
        snprintf(key,sizeof key,"TRACK_NAME_%d",e->id);
        g->name_key=ug_hash_upper(key);
    }
    qsort(c->ev,c->nev,sizeof c->ev[0],gcat_cmp);
    for(int k=0;k<c->nev;k++) {
        int r;
        for(r=0;r<c->nregion;r++)if(!strcmp(c->region[r].stem,c->ev[k].region))break;
        if(r==c->nregion) {
            if(r>=8)continue;
            snprintf(c->region[r].stem,sizeof c->region[r].stem,"%s",c->ev[k].region);
            snprintf(c->region[r].track,sizeof c->region[r].track,"STREAM%s",c->ev[k].region);
            c->nregion++;
        }
        c->region[r].nevents++;
    }
    /* order regions by stem for stable output */
    for(int i=0;i<c->nregion;i++)for(int j=i+1;j<c->nregion;j++)
        if(strcmp(c->region[j].stem,c->region[i].stem)<0){GcRegion t=c->region[i];c->region[i]=c->region[j];c->region[j]=t;}
    free(w);
    char (*cl)[64]=malloc(64*sizeof *cl);
    if(!cl)return c->nev>0;
    int n=res_list_cars(dataroot,cl,64,"",&sel);
    for(int i=0;i<n && c->ncar<64;i++) {
        char p[1100];snprintf(p,sizeof p,"%s/CARS/%s/TEXTURES.BIN",dataroot,cl[i]);
        FILE *f=fopen(p,"rb");
        int empty=0;
        if(f){empty=fseek(f,0,SEEK_END)==0 && ftell(f)==0;fclose(f);}
        GcCar *g=c->car+c->ncar++;
        snprintf(g->name,sizeof g->name,"%s",cl[i]);g->traffic=empty;
    }
    free(cl);
    return c->nev>0;
}
