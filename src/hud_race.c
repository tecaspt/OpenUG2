#include "hud_race.h"
#include "race.h"
#include "asset_chunks.h"
#include "ugui/ugui_codec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char *hud_race_package(int kind) {
    switch(kind) {
    case N2_RACE_STREETX:return "HUD_Short_Track.fng";
    case N2_RACE_DRAG:return "HUD_Drag.fng";
    case N2_RACE_DRIFT:return "HUD_Drift.fng";
    default:return "HUD_SingleRace.fng";
    }
}

typedef struct { const char *name; HaPackage package; int found,bad; } PackageRead;
static int package_chunk(const unsigned char *data,uint32_t tag,long beg,long end,void *context) {
    PackageRead *r=context;
    if(tag!=0x30203 && tag!=0x30210)return !!(tag&0x80000000u);
    if(r->bad)return 0;
    const unsigned char *body=data+beg;size_t len=(size_t)(end-beg);
    if(tag==0x30210){if(len<4){r->bad=1;return 0;}body+=4;len-=4;}
    unsigned char *decoded=NULL;size_t decoded_len=0;
    /* HUD packages are small; reject decompression bombs before allocation. */
    if(ug_codec_detect(body,len)!=UG_CODEC_NONE && asset_chunk_u32(body+8)>4*1024*1024){r->bad=1;return 0;}
    /* HUFF also carries an independent 24-bit stream size. */
    if(ug_codec_detect(body,len)==UG_CODEC_HUFF){
        if(len<21){r->bad=1;return 0;}
        size_t at=18+((body[16]&1)?3:0);
        if(len<at+3 || ((unsigned)body[at]<<16|(unsigned)body[at+1]<<8|body[at+2])>4*1024*1024){r->bad=1;return 0;}
    }
    char error[128];int result=ug_decompress(body,len,&decoded,&decoded_len,error,sizeof error);
    if(!result){r->bad=1;return 0;}
    if(result==1){body=decoded;len=decoded_len;}
    if(len>4*1024*1024 || len<8){free(decoded);r->bad=1;return 0;}
    unsigned char *wrapped=malloc(len+8);
    if(!wrapped){free(decoded);r->bad=1;return 0;}
    uint32_t header[]={0x30203,(uint32_t)len};
    for(int j=0;j<2;j++)for(int k=0;k<4;k++)wrapped[j*4+k]=(unsigned char)(header[j]>>(8*k));
    memcpy(wrapped+8,body,len);
    HaPackage candidate={0};
    if(ha_package_select(wrapped,len+8,r->name,&candidate)) {
        if(r->found){ha_free(&candidate);r->bad=1;}
        else {r->package=candidate;r->found=1;}
    }
    free(wrapped);free(decoded);return 0;
}
int hud_race_package_read(const unsigned char *data,size_t len,const char *name,HaPackage *out) {
    if(!out)return 0;memset(out,0,sizeof *out);
    if(!data||!name||!len||len>LONG_MAX)return 0;
    PackageRead read={.name=name};
    if(!asset_chunks_walk(data,0,(long)len,package_chunk,&read)||read.bad||!read.found){ha_free(&read.package);return 0;}
    *out=read.package;return 1;
}

void hud_race_clock_step(HudRaceClock *c,int event,int phase,double dt) {
    if(!c)return;
    if(event<0||phase<0||phase>2){memset(c,0,sizeof *c);return;}
    if(!c->active || c->event!=event || (phase==0&&c->phase!=0) || (phase==1&&c->phase==2))
        *c=(HudRaceClock){.active=1,.event=event};
    if(phase==1&&isfinite(dt)&&dt>0&&dt<=1)c->elapsed+=dt;
    c->phase=phase;
}
int hud_race_time(double seconds,char out[64]) {
    out[0]=0;if(!isfinite(seconds)||seconds<0||seconds>=360000)return 0;
    unsigned cs=(unsigned)floor(seconds*100+1e-6);
    if(cs>=360000)snprintf(out,64,"%02u:%02u:%02u.%02u",cs/360000,cs/6000%60,cs/100%60,cs%100);
    else snprintf(out,64,"%02u:%02u.%02u",cs/6000,cs/100%60,cs%100);
    return 1;
}
int hud_race_text(const HudState *s,int field,char out[64]) {
    out[0]=0;if(!s||!s->racing)return 0;
    int position=s->position>0&&s->cars>=s->position;
    switch(field) {
    case HR_POSITION:if(position)snprintf(out,64,"%d",s->position);break;
    case HR_ORDINAL:if(position){int x=s->position%100;snprintf(out,64,"%s",x>=11&&x<=13?"th":x%10==1?"st":x%10==2?"nd":x%10==3?"rd":"th");}break;
    case HR_CARS:if(position)snprintf(out,64,"/%d",s->cars);break;
    case HR_LAP_LABEL:if(s->race_kind!=N2_RACE_DRAG)snprintf(out,64,"%s",s->circuit?"Laps":"Complete");break;
    case HR_LAP:
        if(s->race_kind==N2_RACE_DRAG)break;
        if(s->circuit&&s->lap>0&&s->laps>0)snprintf(out,64,"%d",s->lap>s->laps?s->laps:s->lap);
        else if(!s->circuit&&isfinite(s->sprint_progress))snprintf(out,64,"%d%%",(int)(fminf(1,fmaxf(0,s->sprint_progress))*100));
        break;
    case HR_LAPS:if(s->circuit&&s->laps>0&&s->race_kind!=N2_RACE_DRAG)snprintf(out,64,"/%d",s->laps);break;
    case HR_TIME_LABEL:if(s->have_race_time&&s->race_kind!=N2_RACE_DRIFT)snprintf(out,64,"Time:");break;
    case HR_TIME:if(s->have_race_time&&s->race_kind!=N2_RACE_DRIFT)hud_race_time(s->elapsed_seconds,out);break;
    case HR_BANNER:
        if(s->notice && s->notice[0])snprintf(out,64,"%s",s->notice);
        else if(s->race_phase==HUD_RACE_COUNTDOWN&&isfinite(s->countdown_seconds)&&s->countdown_seconds>0&&s->countdown_seconds<=3)
            snprintf(out,64,"%d",(int)ceil(s->countdown_seconds));
        else if(s->race_phase==HUD_RACE_RUNNING&&s->have_race_time&&s->elapsed_seconds>=0&&s->elapsed_seconds<.4)snprintf(out,64,"GO!");
        else if(s->race_phase==HUD_RACE_FINISHED)snprintf(out,64,"%s",position&&s->position==1?"WINNER":"FINISHED");
        else if(s->race_phase==HUD_RACE_FAILED)snprintf(out,64,"%s",s->engine_failed?"ENGINE BLOWN":"RACE OVER");
        break;
    case HR_DRIFT_CHAIN:if(s->race_kind==N2_RACE_DRIFT&&isfinite(s->drift_chain)&&s->drift_chain>0)snprintf(out,64,"%.0f",fminf(1e9f,s->drift_chain));break;
    case HR_DRIFT_TOTAL:if(s->race_kind==N2_RACE_DRIFT&&isfinite(s->drift_score)&&s->drift_score>=0)snprintf(out,64,"%.0f",fminf(1e9f,s->drift_score));break;
    case HR_DRIFT_TOTAL_LABEL:if(s->race_kind==N2_RACE_DRIFT)snprintf(out,64,"Total:");break;
    case HR_DRIFT_ANGLE:if(s->race_kind==N2_RACE_DRIFT&&isfinite(s->drift_angle))snprintf(out,64,"%.0f",fminf(180,fabsf(s->drift_angle)));break;
    }
    return out[0]!=0;
}
uint32_t hud_race_field_key(int kind,int field) {
    /* Opaque object-name identifiers read from the supplied HUD packages. */
    static const uint32_t keys[HR_FIELD_COUNT]={0x9c183bf8,0x44f24239,0x3bbd6268,
        0x8b7e6ea1,0x57f0e3a5,0xf2685238,0x05092833,0x30d9802e,0xa7806ddb,
        0x6a4beef0,0x39dfbebd,0x576e3eed,0xb5c6103b};
    if(field<0||field>=HR_FIELD_COUNT)return 0;
    if(kind==N2_RACE_DRAG && field==HR_TIME)return 0x657cb989;
    return keys[field];
}

int hud_race_map_point(const HaMapFrame*f,double x,double y,double out[2]) {
    if(!f||!f->enabled||!isfinite(x)||!isfinite(y)||!isfinite(f->heading)||
       !isfinite(f->window)||f->window<=0||f->size[0]<=0||f->size[1]<=0)return 0;
    double c=cos(f->heading),s=sin(f->heading),dx=x-f->world[0],dy=-(y-f->world[1]);
    double lx=(c*dx+s*dy)/f->window*f->size[0],ly=(-s*dx+c*dy)/f->window*f->size[1];
    out[0]=f->center[0]+f->basis[0]*lx+f->basis[2]*ly;
    out[1]=f->center[1]+f->basis[1]*lx+f->basis[3]*ly;
    return isfinite(out[0])&&isfinite(out[1]);
}

float hud_race_drag_angle(const HaNode *needle,double rpm) {
    if(!needle)return 0;
    /* shortcut: symmetric sweep fitted to the supplied 10,000 RPM artwork;
       replace when the retail animation curve is independently verified. */
    double end=2*atan2(needle->quaternion[2],needle->quaternion[3]);
    double reading=isfinite(rpm)?fmin(10000,fmax(0,rpm)):0;
    return (float)(end*(reading/10000*2-2));
}

int hud_race_box_laps(const HudState *s) {
    return s&&s->racing&&s->circuit&&s->laps>0&&s->race_kind!=N2_RACE_DRAG;
}
int hud_race_outrun(const HudState *s,double *fraction,char text[64]) {
    if(text)text[0]=0;
    if(fraction)*fraction=0;
    if(!s||!s->have_outrun)return 0;
    if(!isfinite(s->outrun_lead_m)||!isfinite(s->outrun_goal_m)||!(s->outrun_goal_m>0))return -1;
    double f=(double)s->outrun_lead_m/s->outrun_goal_m;
    if(fraction)*fraction=f<0?0:f>1?1:f;
    if(text){long lead=lround(fmin(1e6,fmax(-1e6,(double)s->outrun_lead_m)));
        snprintf(text,64,"%c %ld",lead<0?'-':'+',lead<0?-lead:lead);}
    return 1;
}
double hud_race_icon_angle(double map_heading,double world_heading) {
    if(!isfinite(map_heading)||!isfinite(world_heading))return 0;
    double a=fmod(M_PI*.5-world_heading-map_heading,2*M_PI);
    if(a<=-M_PI)a+=2*M_PI;
    if(a>M_PI)a-=2*M_PI;
    return a;
}
