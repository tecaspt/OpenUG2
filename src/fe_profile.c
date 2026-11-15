/* [inferred] FE lifecycle around the existing, byte-preserving profile container. */
#include "fe_profile.h"
#include "controls_store.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int result(FeProfile *p,int rc) {
    p->last_error=rc;
    if(rc==ST_OK)p->status[0]=0;
    else snprintf(p->status,sizeof p->status,"Profile operation failed (%d)",rc);
    return rc;
}
static int activate_name(FeProfile *p,const char *name) {
    if(!p->store)return ST_OK;
    char current[ST_NAME_MAX+1];
    if(st_reg_get_string(p->store,"ActiveProfile",current,sizeof current) && !strcmp(current,name))return ST_OK;
    return st_reg_set_string(p->store,"ActiveProfile",name);
}
static int active(FeProfile *p){return activate_name(p,p->value.name);}
int fep_refresh(FeProfile *p) {
    p->count=0;DIR *dir=opendir(p->root);if(!dir)return result(p,ST_OK);
    struct dirent *e;
    while((e=readdir(dir)) && p->count<FEP_MAX_PROFILES) {
        if(!st_profile_name_valid(e->d_name) || st_profile_status(p->root,e->d_name)!=ST_OK)continue;
        snprintf(p->names[p->count++],ST_NAME_MAX+1,"%.*s",ST_NAME_MAX,e->d_name);
    }
    closedir(dir);
    for(int i=0;i<p->count;i++)for(int j=i+1;j<p->count;j++)if(strcmp(p->names[j],p->names[i])<0){char tmp[ST_NAME_MAX+1];memcpy(tmp,p->names[i],sizeof tmp);memcpy(p->names[i],p->names[j],sizeof tmp);memcpy(p->names[j],tmp,sizeof tmp);}
    p->current=0;for(int i=0;i<p->count;i++)if(!strcmp(p->names[i],p->value.name))p->current=i;
    return result(p,ST_OK);
}
FeProfile *fep_create(const char *root,StRegStore *store) {
    FeProfile *p=calloc(1,sizeof *p);if(!p)return NULL;p->store=store;
    int rc=root?(snprintf(p->root,sizeof p->root,"%s",root)>=(int)sizeof p->root?ST_E_ARG:ST_OK):st_default_profile_root(p->root,sizeof p->root);
    if(rc!=ST_OK){result(p,rc);return p;}
    char name[ST_NAME_MAX+1];
    if(!store || !st_reg_get_string(store,"ActiveProfile",name,sizeof name))snprintf(name,sizeof name,"%s",CS_PROFILE_NAME);
    rc=st_profile_load(p->root,name,&p->value);
    if(rc==ST_E_NOTFOUND){st_profile_defaults(&p->value,0);snprintf(p->value.name,sizeof p->value.name,"%s",name);p->valid=st_profile_name_valid(name);}
    else if(rc==ST_OK){p->valid=1;p->persisted=1;}
    fep_refresh(p);if(rc!=ST_OK && rc!=ST_E_NOTFOUND)result(p,rc);
    return p;
}
void fep_destroy(FeProfile *p){free(p);}
int fep_load(FeProfile *p,const char *name) {
    StProfile *next=malloc(sizeof *next);if(!next)return result(p,ST_E_NOMEM);
    int rc=st_profile_load(p->root,name,next);
    int loaded=rc==ST_OK;
    if(loaded)rc=activate_name(p,next->name);
    if(rc==ST_OK){p->value=*next;p->valid=p->persisted=1;}
    else if(loaded && p->valid)active(p);
    free(next);fep_refresh(p);return result(p,rc);
}
int fep_new(FeProfile *p,const char *name) {
    if(!p->root[0] || !st_profile_name_valid(name) || st_profile_status(p->root,name)!=ST_E_NOTFOUND)return result(p,ST_E_ARG);
    StProfile *next=malloc(sizeof *next);if(!next)return result(p,ST_E_NOMEM);
    st_profile_defaults(next,0);snprintf(next->name,sizeof next->name,"%s",name);
    int rc=st_profile_save(p->root,next);
    if(rc==ST_OK){
        rc=activate_name(p,next->name);
        if(rc==ST_OK){p->value=*next;p->valid=p->persisted=1;}
        else {st_profile_delete(p->root,next->name);if(p->valid)active(p);}
    }
    free(next);fep_refresh(p);return result(p,rc);
}
int fep_save(FeProfile *p) {
    if(!p || !p->valid)return ST_E_CORRUPT;
    int rc=st_profile_save(p->root,&p->value);
    if(rc==ST_OK){p->persisted=1;rc=active(p);}return result(p,rc);
}
int fep_rename(FeProfile *p,const char *name) {
    if(!p->valid || !st_profile_name_valid(name))return result(p,ST_E_ARG);
    if(!strcmp(name,p->value.name))return fep_save(p);
    if(st_profile_status(p->root,name)!=ST_E_NOTFOUND)return result(p,ST_E_ARG);
    char old[33];snprintf(old,sizeof old,"%s",p->value.name);
    snprintf(p->value.name,sizeof p->value.name,"%s",name);
    int rc=st_profile_save(p->root,&p->value);
    if(rc!=ST_OK){snprintf(p->value.name,sizeof p->value.name,"%s",old);return result(p,rc);}
    rc=active(p);
    if(rc==ST_OK && p->persisted)rc=st_profile_delete(p->root,old);
    if(rc!=ST_OK){st_profile_delete(p->root,name);snprintf(p->value.name,sizeof p->value.name,"%s",old);active(p);return result(p,rc);}
    p->persisted=1;fep_refresh(p);return result(p,ST_OK);
}
int fep_delete(FeProfile *p,const char *name) {
    if(!strcmp(p->value.name,name))return result(p,ST_E_ARG); /* never silently discard the active profile */
    int rc=st_profile_delete(p->root,name);fep_refresh(p);return result(p,rc);
}
