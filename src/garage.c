#include "garage.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int garage_config_valid(const GarageConfig *c) {
 if(!c||!memchr(c->rim_brand,0,sizeof c->rim_brand)||c->rim_style<0||c->rim_style>999||
 c->car.body_kit<0||c->car.body_kit>99||c->car.hood_style<0||c->car.hood_style>999||
 c->car.spoiler_style<0||c->car.spoiler_style>999||c->car.wheel_style_id<0||c->car.wheel_style_id>999)return 0;
 for(const char *p=c->rim_brand;*p;p++)if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))return 0;
 if(!c->rim_brand[0]&&c->rim_style)return 0;
 for(int i=0;i<N2_PART_COUNT;i++)if(c->car.parts[i]<0||c->car.parts[i]>5999)return 0;
 return 1;
}
void garage_init(Garage *g,const GarageCatalog *cat,const GarageConfig *c) {
 memset(g,0,sizeof *g);g->catalog=cat;g->slot=-1;
 if(c&&garage_config_valid(c))g->applied=g->pending=g->displayed=*c;
}
int garage_begin(Garage *g,int slot) {
 if(!g||!g->catalog||g->editing||g->inflight||slot<0||slot>=GARAGE_SLOTS||g->catalog->menu[slot].count<1||g->catalog->menu[slot].count>256)return 0;
 g->slot=slot;g->baseline=g->pending=g->applied;g->editing=1;g->event=0;g->status[0]=0;return 1;
}
int garage_select(Garage *g,int index) {
 if(!g||!g->editing||g->action>GAR_PREVIEW||(g->inflight&&g->flight.action>GAR_PREVIEW))return 0;
 if(g->slot<0||g->slot>=GARAGE_SLOTS)return 0;
 const N2PartMenu *m=&g->catalog->menu[g->slot];if(index<0||index>=m->count)return 0;
 if(g->slot==GARAGE_KIT) {
  g->pending.car.body_kit=m->options[index].value;
  for(int p=0;p<N2_PART_SPOILER;p++)if(p!=N2_PART_ENGINE&&p!=N2_PART_AUDIO)g->pending.car.parts[p]=0;
 }else g->pending.car.parts[g->slot]=m->options[index].value;
 g->quiet=0;g->action=GAR_PREVIEW;g->status[0]=0;return 1;
}
void garage_finish(Garage *g,int keep) {
 if(!g||!g->editing||g->action>GAR_PREVIEW||(g->inflight&&g->flight.action>GAR_PREVIEW))return;
 if(!keep)g->pending=g->baseline;
 g->action=keep?GAR_KEEP:GAR_RESTORE;g->quiet=GARAGE_PREVIEW_SECONDS;
}
void garage_update(Garage *g,float dt){if(g&&isfinite(dt)&&dt>0)g->quiet=fminf(GARAGE_PREVIEW_SECONDS,g->quiet+dt);}
int garage_take(Garage *g,GarageRequest *r) {
 if(!g||!r||!g->action||g->inflight||g->quiet+1e-6f<GARAGE_PREVIEW_SECONDS)return 0;
 g->serial++;if(!g->serial)g->serial++;
 g->flight=(GarageRequest){g->serial,g->action,g->pending};g->action=0;g->inflight=1;*r=g->flight;return 1;
}
void garage_ack(Garage *g,uint32_t serial,int success,const char *error) {
 if(!g||!g->inflight||serial!=g->flight.serial)return;
 g->inflight=0;
 if(!success) {
  snprintf(g->status,sizeof g->status,"%s",error?error:"Part could not be applied");
  if(!g->action)g->pending=g->displayed;
  return;
 }
 g->displayed=g->flight.config;
 if(g->flight.action!=GAR_PREVIEW) {
  g->applied=g->pending=g->flight.config;g->editing=0;g->event=g->flight.action;
 }
}

int garage_begin_rims(Garage *g,const GarageRims *rims) {
 if(!g||!rims||rims->count<1||rims->count>GARAGE_MAX_RIMS||g->editing||g->inflight)return 0;
 g->rims=rims;g->slot=GARAGE_RIM;g->baseline=g->pending=g->applied;
 g->editing=1;g->event=0;g->status[0]=0;return 1;
}
int garage_select_rim(Garage *g,int index) {
 if(!g||!g->editing||g->slot!=GARAGE_RIM||!g->rims||index<0||index>=g->rims->count||
    g->action>GAR_PREVIEW||(g->inflight&&g->flight.action>GAR_PREVIEW))return 0;
 GarageConfig next=g->pending;
 memcpy(next.rim_brand,g->rims->options[index].brand,sizeof next.rim_brand);
 next.rim_style=g->rims->options[index].style;
 if(!garage_config_valid(&next))return 0;
 g->pending=next;g->quiet=0;g->action=GAR_PREVIEW;g->status[0]=0;return 1;
}

/* The on-disk format uses explicit little-endian fields; unknown bytes stay opaque. */
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
static uint32_t gs_rd(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void gs_wr(uint8_t *p,uint32_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int gs_name(const char *p,size_t cap,int empty) {
 const char *end=memchr(p,0,cap);if(!end||(!empty&&end==p))return 0;
 for(;p<end;p++)if(!((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))return 0;
 return 1;
}
static int gs_car(const char *p) {return p&&strlen(p)<32&&gs_name(p,strlen(p)+1,0);}
static int gs_decode(const uint8_t *p,GarageConfig *c) {
 GarageConfig v={0};v.car.body_kit=(int32_t)gs_rd(p+32);v.car.hood_style=(int32_t)gs_rd(p+36);
 v.car.spoiler_style=(int32_t)gs_rd(p+40);v.car.wheel_style_id=(int32_t)gs_rd(p+44);
 for(int i=0;i<N2_PART_COUNT;i++)v.car.parts[i]=(int32_t)gs_rd(p+48+4*i);
 memcpy(v.rim_brand,p+92,24);v.rim_style=(int32_t)gs_rd(p+116);
 if(!gs_name((const char *)p,32,0)||!garage_config_valid(&v))return 0;
 *c=v;return 1;
}
static void gs_encode(uint8_t *p,const GarageConfig *c) {
 gs_wr(p+32,(uint32_t)c->car.body_kit);gs_wr(p+36,(uint32_t)c->car.hood_style);
 gs_wr(p+40,(uint32_t)c->car.spoiler_style);gs_wr(p+44,(uint32_t)c->car.wheel_style_id);
 for(int i=0;i<N2_PART_COUNT;i++)gs_wr(p+48+4*i,(uint32_t)c->car.parts[i]);
 memcpy(p+92,c->rim_brand,strlen(c->rim_brand)+1);gs_wr(p+116,(uint32_t)c->rim_style);
}
/* Validates every known record and rejects duplicate keys, even after a match. */
static int gs_parse(const uint8_t *bytes,size_t len,const char *car,StContainerView *v,size_t *found) {
 if(!bytes||len>GARAGE_SAVE_LIMIT)return ST_E_CORRUPT;
 int rc=st_container_parse(bytes,len,v);if(rc!=ST_OK)return rc;
 if((uint64_t)ST_CONTAINER_HEADER+v->size_a+v->size_b!=len||v->size_a<8||gs_rd(v->a)!=0x5241474fu)return ST_E_CORRUPT;
 uint32_t version=gs_rd(v->a+4);if(!version)return ST_E_CORRUPT;if(version>1)return ST_E_INCOMPATIBLE;
 *found=SIZE_MAX;
 for(size_t at=0;at<v->size_b;) {
  if(v->size_b-at<8)return ST_E_CORRUPT;
  const uint8_t *h=v->b+at;uint32_t n=gs_rd(h+4);if(n>v->size_b-at-8)return ST_E_CORRUPT;
  if(h[0]==1&&h[1]==0) {
   GarageConfig cfg;if(n<120||!gs_decode(h+8,&cfg))return ST_E_CORRUPT;
   for(size_t prev=0;prev<at;) {
    const uint8_t *q=v->b+prev;uint32_t qn=gs_rd(q+4);
    if(q[0]==1&&q[1]==0&&!strcmp((const char *)q+8,(const char *)h+8))return ST_E_CORRUPT;
    prev+=8+qn;
   }
   if(!strcmp((const char *)h+8,car))*found=at;
  }
  at+=8+n;
 }
 return ST_OK;
}
int garage_store_get(const uint8_t *bytes,size_t len,const char *car,GarageConfig *out) {
 if(!gs_car(car)||!out||!bytes)return ST_E_ARG;
 StContainerView v;size_t at;int rc=gs_parse(bytes,len,car,&v,&at);if(rc!=ST_OK)return rc;
 if(at==SIZE_MAX)return ST_E_NOTFOUND;
 GarageConfig c;if(!gs_decode(v.b+at+8,&c))return ST_E_CORRUPT;*out=c;return ST_OK;
}
int garage_store_put(const uint8_t *bytes,size_t len,const char *car,const GarageConfig *cfg,uint8_t **out,size_t *outlen) {
 if(!gs_car(car)||!garage_config_valid(cfg)||!out||!outlen||(!bytes&&len)||(bytes&&!len))return ST_E_ARG;
 uint8_t a[8]={0};gs_wr(a,0x5241474fu);gs_wr(a+4,1);StContainerView v={a,NULL,8,0};size_t at=SIZE_MAX;
 if(bytes){int rc=gs_parse(bytes,len,car,&v,&at);if(rc!=ST_OK)return rc;}
 size_t extra=at==SIZE_MAX?128:0,total=ST_CONTAINER_HEADER+(size_t)v.size_a+v.size_b+extra;
 if(total>GARAGE_SAVE_LIMIT)return ST_E_CORRUPT;
 uint8_t *b=calloc(1,(size_t)v.size_b+extra);if(!b)return ST_E_NOMEM;
 if(v.size_b)memcpy(b,v.b,v.size_b);
 if(at==SIZE_MAX){at=v.size_b;b[at]=1;gs_wr(b+at+4,120);memcpy(b+at+8,car,strlen(car)+1);}
 gs_encode(b+at+8,cfg);uint8_t *result=malloc(total);
 if(!result){free(b);return ST_E_NOMEM;}
 size_t n=st_container_build(result,total,v.a,v.size_a,b,(uint32_t)(v.size_b+extra));free(b);
 if(n!=total){free(result);return ST_E_IO;}*out=result;*outlen=total;return ST_OK;
}
#ifndef _WIN32
static int gs_dir(const char *root,const char *profile,int *out) {
 int r=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(r<0)return errno==ENOENT?ST_E_NOTFOUND:ST_E_IO;
 int d=openat(r,profile,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);int err=errno;close(r);
 if(d<0)return err==ENOENT?ST_E_NOTFOUND:ST_E_IO;
 *out=d;return ST_OK;
}
static int gs_profile(int d,const char *profile) {
 int f=openat(d,profile,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(f<0)return 0;
 struct stat st;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode);close(f);return ok;
}
static int gs_target(int d,int *fd,int single) {
 int f=openat(d,GARAGE_FILE_NAME,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(f<0)return errno==ENOENT?ST_E_NOTFOUND:ST_E_IO;
 struct stat st;if(fstat(f,&st)||!S_ISREG(st.st_mode)||(single&&st.st_nlink!=1)){close(f);return ST_E_IO;}
 *fd=f;return ST_OK;
}
static int gs_read(int d,uint8_t **out,size_t *len) {
 int f,rc=gs_target(d,&f,0);if(rc!=ST_OK)return rc;
 struct stat st;if(fstat(f,&st)||st.st_size<0||(uint64_t)st.st_size>GARAGE_SAVE_LIMIT){close(f);return ST_E_IO;}
 size_t n=(size_t)st.st_size;uint8_t *b=malloc(n?n:1);if(!b){close(f);return ST_E_NOMEM;}
 size_t at=0;while(at<n){ssize_t got=read(f,b+at,n-at);if(got<0&&errno==EINTR)continue;if(got<=0){free(b);close(f);return ST_E_IO;}at+=(size_t)got;}
 uint8_t extra;ssize_t got;do{got=read(f,&extra,1);}while(got<0&&errno==EINTR);
 int closed=close(f);if(got!=0||closed){free(b);return ST_E_IO;}*out=b;*len=n;return ST_OK;
}
int garage_load(const char *root,const char *profile,const char *car,GarageConfig *out) {
 if(!root||!st_profile_name_valid(profile)||!gs_car(car)||!out)return ST_E_ARG;
 int d,dirrc=gs_dir(root,profile,&d);if(dirrc!=ST_OK)return dirrc;
 uint8_t *b=NULL;size_t n=0;int rc=gs_read(d,&b,&n);close(d);
 if(rc==ST_OK)rc=garage_store_get(b,n,car,out);
 free(b);return rc;
}
int garage_save(const char *root,const char *profile,const char *car,const GarageConfig *cfg) {
 if(!root||!st_profile_name_valid(profile)||!gs_car(car)||!garage_config_valid(cfg))return ST_E_ARG;
 int d,dirrc=gs_dir(root,profile,&d);if(dirrc!=ST_OK)return ST_E_IO;
 if(!gs_profile(d,profile)){close(d);return ST_E_IO;}
 uint8_t *old=NULL,*data=NULL;size_t oldn=0,n=0;int rc=gs_read(d,&old,&oldn);
 if(rc==ST_E_NOTFOUND)rc=ST_OK;
 if(rc==ST_OK)rc=garage_store_put(old,oldn,car,cfg,&data,&n);
 free(old);
 if(rc!=ST_OK){close(d);return rc;}
 char temp[96];int f=-1;
 for(int i=0;i<128;i++){snprintf(temp,sizeof temp,".openug2_garage.%ld.%d.tmp",(long)getpid(),i);f=openat(d,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(f>=0||errno!=EEXIST)break;}
 if(f<0){free(data);close(d);return ST_E_IO;}
 size_t at=0;while(at<n){ssize_t wrote=write(f,data+at,n-at);if(wrote<0&&errno==EINTR)continue;if(wrote<=0){rc=ST_E_IO;break;}at+=(size_t)wrote;}
 if(rc==ST_OK&&fsync(f))rc=ST_E_IO;
 if(close(f))rc=ST_E_IO;
 free(data);
 if(rc==ST_OK){int target;int check=gs_target(d,&target,1);if(check==ST_OK)close(target);else if(check!=ST_E_NOTFOUND)rc=ST_E_IO;}
 if(rc==ST_OK&&renameat(d,temp,d,GARAGE_FILE_NAME))rc=ST_E_IO;
 if(rc!=ST_OK)unlinkat(d,temp,0);
 close(d);return rc;
}
#else
/* Windows opens reparse points themselves and refuses them before access. */
static int gs_win_dir(const char *root,const char *profile,char *dir,size_t cap) {
 DWORD a=GetFileAttributesA(root);if(a==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND?ST_E_NOTFOUND:ST_E_IO;
 if(!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT))return ST_E_IO;
 if(snprintf(dir,cap,"%s\\%s",root,profile)>=(int)cap)return ST_E_IO;
 a=GetFileAttributesA(dir);if(a==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND?ST_E_NOTFOUND:ST_E_IO;
 return (a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT)?ST_OK:ST_E_IO;
}
static int gs_win_read(const char *path,uint8_t **out,size_t *len) {
 HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
 if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?ST_E_NOTFOUND:ST_E_IO;
 BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER sz;
 if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||info.nNumberOfLinks!=1||GetFileType(h)!=FILE_TYPE_DISK||!GetFileSizeEx(h,&sz)||sz.QuadPart<0||sz.QuadPart>GARAGE_SAVE_LIMIT){CloseHandle(h);return ST_E_IO;}
 size_t n=(size_t)sz.QuadPart;uint8_t *b=malloc(n?n:1);if(!b){CloseHandle(h);return ST_E_NOMEM;}
 DWORD got=0,extra=0;uint8_t x;int ok=ReadFile(h,b,(DWORD)n,&got,NULL)&&got==n&&ReadFile(h,&x,1,&extra,NULL)&&!extra;
 if(!CloseHandle(h))ok=0;
 if(!ok){free(b);return ST_E_IO;}*out=b;*len=n;return ST_OK;
}
int garage_load(const char *root,const char *profile,const char *car,GarageConfig *out) {
 if(!root||!st_profile_name_valid(profile)||!gs_car(car)||!out)return ST_E_ARG;
 char dir[1024],path[1100];int dr=gs_win_dir(root,profile,dir,sizeof dir);if(dr!=ST_OK)return dr;
 if(snprintf(path,sizeof path,"%s\\%s",dir,GARAGE_FILE_NAME)>=(int)sizeof path)return ST_E_IO;
 uint8_t *b=NULL;size_t n=0;int rc=gs_win_read(path,&b,&n);if(rc==ST_OK)rc=garage_store_get(b,n,car,out);free(b);return rc;
}
int garage_save(const char *root,const char *profile,const char *car,const GarageConfig *cfg) {
 if(!root||!st_profile_name_valid(profile)||!gs_car(car)||!garage_config_valid(cfg))return ST_E_ARG;
 char dir[1024],path[1100],tmp[1100];if(gs_win_dir(root,profile,dir,sizeof dir)!=ST_OK||snprintf(path,sizeof path,"%s\\%s",dir,GARAGE_FILE_NAME)>=(int)sizeof path)return ST_E_IO;
 char profilepath[1100];if(snprintf(profilepath,sizeof profilepath,"%s\\%s",dir,profile)>=(int)sizeof profilepath)return ST_E_IO;
 uint8_t *profiledata=NULL;size_t profilen=0;int pr=gs_win_read(profilepath,&profiledata,&profilen);free(profiledata);if(pr!=ST_OK)return ST_E_IO;
 uint8_t *old=NULL,*data=NULL;size_t oldn=0,n=0;int rc=gs_win_read(path,&old,&oldn);if(rc==ST_E_NOTFOUND)rc=ST_OK;
 if(rc==ST_OK)rc=garage_store_put(old,oldn,car,cfg,&data,&n);
 free(old);if(rc!=ST_OK)return rc;
 HANDLE h=INVALID_HANDLE_VALUE;for(int i=0;i<128;i++){snprintf(tmp,sizeof tmp,"%s\\.openug2_garage.%lu.%d.tmp",dir,(unsigned long)GetCurrentProcessId(),i);h=CreateFileA(tmp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(h!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_FILE_EXISTS)break;}
 if(h==INVALID_HANDLE_VALUE){free(data);return ST_E_IO;}
 DWORD wrote=0;int ok=WriteFile(h,data,(DWORD)n,&wrote,NULL)&&wrote==n&&FlushFileBuffers(h);if(!CloseHandle(h))ok=0;free(data);
 uint8_t *check=NULL;size_t checkn=0;if(ok){int cr=gs_win_read(path,&check,&checkn);free(check);if(cr!=ST_OK&&cr!=ST_E_NOTFOUND)ok=0;}
 if(ok)ok=MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 if(!ok)DeleteFileA(tmp);
 return ok?ST_OK:ST_E_IO;
}
#endif
