/* Optional installed-data evidence. Reports metadata only; no embedded retail bytes. */
#include "nfsu2.h"
#include "attrib.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned hash(const char *s){unsigned h=~0u;while(*s)h=h*33+(unsigned char)*s++;return h;}
static void candidate(const N2Attrib *a,void *u) {
    int *n=u;
    if(strstr(a->name,"REWARD")||strstr(a->name,"CASH")||strstr(a->name,"MONEY")||strstr(a->name,"REPUTATION")) {
        printf("ATTR-CANDIDATE name=%s class=%08x size=%u (fields not decoded)\n",a->name,a->classhash,a->size);(*n)++;
    }
}
int main(void) {
    const char *root=getenv("OPENUG2_DATA");if(!root){puts("SKIP: OPENUG2_DATA unset");return 77;}
    char path[1200];snprintf(path,sizeof path,"%s/GLOBAL/GLOBALB.BUN",root);
    long bytes=0;unsigned char *data=n2_read_file(path,&bytes);if(!data)return 1;
    int names=0,records=n2_attrib_walk(data,bytes,candidate,&names);free(data);
    printf("ATTR records=%d reward-name-candidates=%d (no numerical reward decoder)\n",records,names);
    snprintf(path,sizeof path,"%s/TRACKS",root);DIR *dir=opendir(path);if(!dir)return 1;
    struct dirent *entry;int f=0,b=0,packages=0;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"ROUTES",6))continue;
        snprintf(path,sizeof path,"%s/TRACKS/%s/TrackPosMarkersAll.bin",root,entry->d_name);
        data=n2_read_file(path,&bytes);if(!data)continue;
        N2Leaf leaves[4];int n=0;n2_find_leaves(data,0,bytes,0x34146,leaves,&n,4);
        for(int l=0;l<n;l++)for(long at=leaves[l].off+8;at+48<=leaves[l].off+leaves[l].size;at+=48) {
            unsigned name=n2_u32(data+at+8);
            if(name==hash("ForwardStartingPosition"))f++;
            if(name==hash("BackwardStartingPosition"))b++;
        }
        free(data);packages++;
    }
    closedir(dir);printf("GRID packages=%d forward=%d backward=%d hashes=%08x/%08x\n",packages,f,b,hash("ForwardStartingPosition"),hash("BackwardStartingPosition"));
    if(records<1||f<1||b<1)return 1;
    puts("race_rules_data_check: PASS (installed metadata only, numerical fidelity NOT established)");return 0;
}
