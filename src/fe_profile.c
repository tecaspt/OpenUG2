/* [inferred] FE lifecycle around the existing, byte-preserving profile container. */
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif
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
/* [inferred] Profile rename carries career and Garage bytes opaquely before removing the
 * old directory. This also preserves unknown/newer saves. Only ENOENT means absent. */
static int carry_aux(const char *root,const char *from,const char *to,const char *file) {
    char src[1200],dst[1200];
    if(snprintf(src,sizeof src,"%s/%s/%s",root,from,file)>=(int)sizeof src ||
       snprintf(dst,sizeof dst,"%s/%s/%s",root,to,file)>=(int)sizeof dst)return 0;
    struct stat info;
#ifdef _WIN32
    DWORD attributes=GetFileAttributesA(src);
    if(attributes==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return 0;
    if(stat(src,&info)!=0)return 0;
#else
    if(lstat(src,&info)!=0)return errno==ENOENT;
    if(!S_ISREG(info.st_mode))return 0;
#endif
    FILE *in=fopen(src,"rb");if(!in)return 0;
    FILE *out=fopen(dst,"wb");if(!out){fclose(in);return 0;}
    char buf[4096];size_t n;int ok=1;
    while((n=fread(buf,1,sizeof buf,in))>0)if(fwrite(buf,1,n,out)!=n){ok=0;break;}
    if(ferror(in))ok=0;
    if(fclose(in)!=0)ok=0;
    if(fclose(out)!=0)ok=0;
    if(!ok)remove(dst);
    return ok;
}
/* Preserve opaque regular auxiliary files; refuse directories/special files before mutation. */
static int carry_profile_aux(const char *root,const char *from,const char *to,int copy) {
 char dirpath[1200];if(snprintf(dirpath,sizeof dirpath,"%s/%s",root,from)>=(int)sizeof dirpath)return 0;
 DIR *dir=opendir(dirpath);if(!dir)return 0;
 struct dirent *e;int ok=1;
 while((e=readdir(dir))) {
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")||!strcmp(e->d_name,from))continue;
  char path[1600];struct stat info;
  if(!strcmp(e->d_name,to)||snprintf(path,sizeof path,"%s/%s",dirpath,e->d_name)>=(int)sizeof path){ok=0;break;}
#ifdef _WIN32
  DWORD attrs=GetFileAttributesA(path);
  if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||stat(path,&info)){ok=0;break;}
#else
  if(lstat(path,&info)||!S_ISREG(info.st_mode)){ok=0;break;}
#endif
  if(copy&&!carry_aux(root,from,to,e->d_name)){ok=0;break;}
 }
 if(closedir(dir))ok=0;
 return ok;
}
int fep_rename(FeProfile *p,const char *name) {
    if(!p->valid || !st_profile_name_valid(name))return result(p,ST_E_ARG);
    if(!strcmp(name,p->value.name))return fep_save(p);
    if(st_profile_status(p->root,name)!=ST_E_NOTFOUND)return result(p,ST_E_ARG);
    char old[33];snprintf(old,sizeof old,"%s",p->value.name);
    if(p->persisted&&!carry_profile_aux(p->root,old,name,0))return result(p,ST_E_IO);
    snprintf(p->value.name,sizeof p->value.name,"%s",name);
    int rc=st_profile_save(p->root,&p->value);
    if(rc!=ST_OK){snprintf(p->value.name,sizeof p->value.name,"%s",old);return result(p,rc);}
    rc=active(p);
    if(rc==ST_OK && p->persisted && !carry_profile_aux(p->root,old,name,1))rc=ST_E_IO;
    if(rc==ST_OK && p->persisted) {
        rc=st_profile_delete(p->root,old);
        if(rc!=ST_OK) {
            /* Cleanup may already have removed old children. Preserve the complete new copy. */
            p->persisted=1;fep_refresh(p);result(p,rc);
            snprintf(p->status,sizeof p->status,"Renamed; old profile cleanup failed (%d)",rc);
            return rc;
        }
    }
    if(rc!=ST_OK){st_profile_delete(p->root,name);snprintf(p->value.name,sizeof p->value.name,"%s",old);active(p);return result(p,rc);}
    p->persisted=1;fep_refresh(p);return result(p,ST_OK);
}
int fep_delete(FeProfile *p,const char *name) {
    if(!strcmp(p->value.name,name))return result(p,ST_E_ARG); /* never silently discard the active profile */
    int rc=st_profile_delete(p->root,name);fep_refresh(p);return result(p,rc);
}
