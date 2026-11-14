/* Runtime-only retail identity check. No retail data is embedded. */
#include "fe_binding.h"
#include "ugui/ugui_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int seen[sizeof fe_binding_sets/sizeof fe_binding_sets[0]], failed, checked;
static int visit(void *user,const UgChunkRef *c) {
    (void)user;
    if(c->id!=UG_CHUNK_PKG_HASHED && c->id!=UG_CHUNK_PKG_RAW)return 0;
    UgPackageBlob blob; char err[256];
    if(!ug_package_from_chunk(c,&blob,err,sizeof err)){fprintf(stderr,"%s\n",err);failed++;return 1;}
    for(unsigned k=0;k<sizeof seen/sizeof seen[0];k++) {
        const FeBindingSet *set=&fe_binding_sets[k];
        if(strcmp(blob.name,set->package))continue;
        UgFngPackage p;
        if(!ug_fng_parse(blob.data,blob.len,&p,err,sizeof err)){failed++;continue;}
        seen[k]++;
        for(int j=0;j<set->count;j++) {
            if(fe_identity_find(&p,&set->identity[j])<0){fprintf(stderr,"FAIL %s GUID %08x\n",set->package,set->identity[j].guid);failed++;}
            else checked++;
        }
        printf("CHECK %s objects=%d identities=%d\n",set->package,p.nobj,set->count);
        ug_fng_free(&p);
    }
    free(blob.name);free(blob.data);
    return 1;
}
int main(void) {
    const char *root=getenv("OPENUG2_DATA");
    if(!root){puts("SKIP: OPENUG2_DATA unset");return 77;}
    const char *files[]={"FRONTEND/FrontB.lzc","GLOBAL/InGameRace.bun","GLOBAL/InGameDrag.bun","GLOBAL/InGameDrift.bun"};
    for(unsigned k=0;k<sizeof files/sizeof files[0];k++) {
        char path[1024],err[256];UgBuf b;
        snprintf(path,sizeof path,"%s/%s",root,files[k]);
        if(!ug_bundle_read(path,&b,err,sizeof err)){fprintf(stderr,"FAIL %s\n",err);failed++;continue;}
        if(!ug_chunks_walk(b.data,b.len,visit,NULL,err,sizeof err)){fprintf(stderr,"FAIL %s\n",err);failed++;}
        ug_buf_free(&b);
    }
    for(unsigned k=0;k<sizeof seen/sizeof seen[0];k++)if(!seen[k]){fprintf(stderr,"FAIL missing %s\n",fe_binding_sets[k].package);failed++;}
    printf("identities=%d failures=%d\n",checked,failed);return failed?1:0;
}
