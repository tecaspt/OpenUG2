/* Native GL smoke/visual probe; consumes assets only through the implementation. */
#include "hud.h"
#include "hud_race.h"
#include "race.h"
#include "rhi_gl.h"   /* this probe reads pixels with raw GL (render.h is GL-free) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int capture(const char*path,int w,int h){unsigned char*raw=malloc((size_t)w*h*3),*top=malloc((size_t)w*h*3);if(!raw||!top){free(raw);free(top);return 0;}glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,raw);for(int y=0;y<h;y++)memcpy(top+(size_t)y*w*3,raw+(size_t)(h-y-1)*w*3,(size_t)w*3);write_png(path,w,h,top);free(raw);free(top);return 1;}
/* M9 --icons: a synthetic minimap icon set (probe inputs only, no retail
 * bytes) on the free-roam map, north up, at 960x720. Writes
 * hud-minimap-icons.png, the same frame without the fail-closed icon
 * (hud-minimap-icons-closed.png, must be identical), hud-m9-nodes.png
 * (wrong way + outrun) and hud-minimap-icons.json (expected pixels and tints)
 * for tools/hud_minimap_pixcheck.py. */
typedef struct { const char *name; int kind, variant; double east, north, heading; int expect; } ProbeIcon;
static unsigned char *grab(int w,int h){unsigned char*raw=malloc((size_t)w*h*3);if(raw){glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,raw);}return raw;}
static int icons_probe(Hud*h,const char*dir,const HudState*base,double window){
 const int W=960,H=720;
 /* offsets in map windows (east, north); the last one is far outside and must clamp */
 static const ProbeIcon set[]={
  {"event_circuit",HMI_EVENT,0,.20,.20,0,1},{"event_short_track",HMI_EVENT,1,-.20,.20,0,1},
  {"event_sprint_failclosed",HMI_EVENT,2,0,-.22,0,0},{"shop_car_lot",HMI_SHOP,0,-.24,-.08,0,1},
  {"showcase",HMI_SHOWCASE,0,.24,-.12,0,1},{"start_line",HMI_START,0,.06,-.06,0,1},
  {"event_circuit_clamped",HMI_EVENT,0,3,-2,0,1}};
 const int n=(int)(sizeof set/sizeof set[0]);
 HudMapIcon icons[16];HudState s=*base;s.racing=0;s.map_heading_explicit=1;s.map_heading_clockwise=0;
 for(int i=0;i<n;i++)icons[i]=(HudMapIcon){(float)(s.car_x+set[i].east*window),(float)(s.car_y+set[i].north*window),(float)set[i].heading,(unsigned char)set[i].kind,(unsigned char)set[i].variant};
 float rival[2]={(float)(s.car_x-.05*window),(float)(s.car_y+.30*window)},rival_heading[1]={(float)M_PI};/* heading west: art points left */
 s.opponent_xy=rival;s.opponent_heading=rival_heading;s.opponent_n=1;
 s.nav_active=1;s.nav_target_x=(float)(s.car_x-.10*window);s.nav_target_y=(float)(s.car_y-.32*window);
 unsigned char*img[3]={0,0,0};HaMapFrame f;
 for(int pass=0;pass<3;pass++){
  /* pass 1 drops the fail-closed icon: the frame must not change */
  HudMapIcon kept[16];int m=0;for(int i=0;i<n;i++)if(pass==0||set[i].expect)kept[m++]=icons[i];
  s.map_icons=kept;s.map_icon_n=m;
  if(pass==2){s.map_icons=NULL;s.map_icon_n=0;s.nav_active=0;s.opponent_n=0;} /* background only: lets the check ignore map pixels */
  glViewport(0,0,W,H);glClearColor(.04f,.05f,.07f,1);glClear(GL_COLOR_BUFFER_BIT);hud_draw(h,NULL,NULL,&s,W,H);
  if(!hud_map_frame(h,&f)){fprintf(stderr,"icons: map not drawn\n");return 0;}
  img[pass]=grab(W,H);char path[4096];snprintf(path,sizeof path,"%s/%s",dir,pass==2?"hud-minimap-icons-bg.png":pass?"hud-minimap-icons-closed.png":"hud-minimap-icons.png");capture(path,W,H);
 }
 int same=img[0]&&img[1]&&!memcmp(img[0],img[1],(size_t)W*H*3);free(img[0]);free(img[1]);free(img[2]);
 HudSettings cfg;hud_default_settings(&cfg);double ring=cfg.icon_ring*fmin(W/640.0,H/480.0),scale=fmin(W/640.0,H/480.0);
 char path[4096];snprintf(path,sizeof path,"%s/hud-minimap-icons.json",dir);FILE*j=fopen(path,"w");if(!j)return 0;
 fprintf(j,"{\"image\":\"hud-minimap-icons.png\",\"closed_image\":\"hud-minimap-icons-closed.png\",\"background_image\":\"hud-minimap-icons-bg.png\",\"width\":%d,\"height\":%d,\"window\":%.3f,\"centre\":[%.3f,%.3f],\"ring\":%.3f,\"failclosed_identical\":%d,\"icons\":[",
  W,H,window,W*.5+scale*f.center[0],H*.5+scale*f.center[1],ring,same);
 int first=1;
 for(int i=0;i<n+2;i++){double wx,wy;int kind,variant;const char*name;double hd=0;
  if(i<n){if(!set[i].expect)continue;wx=icons[i].x;wy=icons[i].y;kind=set[i].kind;variant=set[i].variant;name=set[i].name;}
  else if(i==n){wx=rival[0];wy=rival[1];kind=HMI_RIVAL;variant=1;name="rival_west";hd=rival_heading[0];}
  else{wx=s.nav_target_x;wy=s.nav_target_y;kind=HMI_GPS;variant=0;name="gps_selector";}
  unsigned char rgb[3];double at[2]={0,0};if(!hud_minimap_tint(kind,variant,rgb))continue;int r=hud_minimap_place(&f,wx,wy,ring,at);
  fprintf(j,"%s{\"name\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"rgb\":[%d,%d,%d],\"clamped\":%d,\"rotated\":%d}",first?"":",",name,at[0],at[1],rgb[0],rgb[1],rgb[2],r==2,kind==HMI_RIVAL&&hd!=0);first=0;}
 fprintf(j,"]}\n");fclose(j);
 /* data-backed nodes: wrong way + outrun bar (probe inputs only) */
 HudState d=*base;d.wrong_way=1;d.have_outrun=1;d.outrun_lead_m=180;d.outrun_goal_m=300;
 glViewport(0,0,W,H);glClear(GL_COLOR_BUFFER_BIT);hud_draw(h,NULL,NULL,&d,W,H);snprintf(path,sizeof path,"%s/hud-m9-nodes.png",dir);capture(path,W,H);
 HudAudit a;hud_audit(h,&a);printf("icons: failclosed_identical=%d wrongway drawn=%u getaway drawn=%u\n",same,(a.resource_drawn>>20)&1,(a.resource_drawn>>22)&1);
 return same;
}
int main(int argc,char**argv){
 double probe_window=0;int probe_icons=0;
 {int k=1;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--window")&&i+1<argc){probe_window=atof(argv[++i]);continue;}if(!strcmp(argv[i],"--icons")){probe_icons=1;continue;}argv[k++]=argv[i];}
  argc=k;argv[argc]=NULL;}
 if(probe_window<0||!isfinite(probe_window)){fprintf(stderr,"--window: positive metres\n");return 2;}
 if(argc>=4&&!strcmp(argv[1],"--catalog")){
  FILE*f=fopen(argv[2],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0)return 2;unsigned char*d=malloc((size_t)n);if(!d||fread(d,1,(size_t)n,f)!=(size_t)n)return 2;fclose(f);
  for(int i=3;i<argc;i++)printf("%d%s",hud_catalog_has(d,(size_t)n,(uint32_t)strtoul(argv[i],NULL,10)),i+1==argc?"\n":" ");free(d);return 0;
 }
if(argc<3){fprintf(stderr,"usage: hud_asset_probe [--window M] [--icons] DATA_ROOT OUTPUT_DIR [east north]\n");return 2;}if(SDL_Init(SDL_INIT_VIDEO)){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 2;}SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_Window*window=SDL_CreateWindow("Native HUD probe",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!window){fprintf(stderr,"window: %s\n",SDL_GetError());return 2;}SDL_GLContext context=SDL_GL_CreateContext(window);if(!context){fprintf(stderr,"GL: %s\n",SDL_GetError());return 2;}printf("GL renderer: %s\n",glGetString(GL_RENDERER));Hud*h=hud_init(argv[1]);if(!h)return 2;HudAudit audit;hud_audit(h,&audit);printf("nodes=%u images=%u texts=%u groups=%u resources=%u fonts=%u atlases=%u gauge=%d map=%d metadata=%d\n",audit.nodes,audit.images,audit.texts,audit.groups,audit.resources,audit.fonts,audit.original_atlases,audit.gauge_targets,audit.map_targets,audit.track_metadata);
 HudState s={0};s.speed_kmh=137;s.rpm=4050;s.rpm_maximum=8100;s.gear=4;s.have_rpm=1;s.map_heading_explicit=1;s.map_window_world=500;s.car_x=argc>3?(float)atof(argv[3]):0;s.car_y=argc>4?(float)atof(argv[4]):0;
 if(probe_window>0){HudSettings cfg;hud_default_settings(&cfg);cfg.map_window_m=probe_window;hud_configure(h,&cfg);s.map_window_world=0;}
 if(probe_icons&&!icons_probe(h,argv[2],&s,probe_window>0?probe_window:500)){fprintf(stderr,"icons probe failed\n");return 1;}
 unsigned resolved=0,drawn=0;for(int skin=0;skin<=10;skin++){s.skin_index=skin;s.gear=skin==0?0:skin==1?-1:4;s.imperial_units=skin%2;s.map_heading_clockwise=skin*.25;int w=skin==10?1280:640,ht=skin==10?720:480;glViewport(0,0,w,ht);glClearColor(.04f,.05f,.07f,1);glClear(GL_COLOR_BUFFER_BIT);hud_draw(h,NULL,NULL,&s,w,ht);char path[4096];snprintf(path,sizeof path,"%s/hud-skin-%02d.png",argv[2],skin);capture(path,w,ht);hud_audit(h,&audit);resolved|=audit.resource_resolved;drawn|=audit.resource_drawn;printf("skin %02d: resource resolved mask=0x%x drawn mask=0x%x\n",skin,audit.resource_resolved,audit.resource_drawn);GLenum err=glGetError();if(err){fprintf(stderr,"GL error skin %d: 0x%x\n",skin,err);return 1;}}
 /* Capture-amendment states: career idle (gear 1, widget state A, bank 0) and
    an opt-in moving state (gear 2, pending message, bank 25, inferred
    redline/NOS level/shift indicator enabled) and the opt-in N2O glow at
    unit 410 with a gear change in progress (gear colour). Values are probe inputs only. */
 {HudSettings cfg;hud_default_settings(&cfg);HudState c=s;c.skin_index=0;c.imperial_units=1;c.career_context=1;c.have_money=1;c.money=0;c.gear=1;c.speed_kmh=0;c.rpm=900;c.rpm_maximum=9500;
  const char*names[]={"hud-career-idle.png","hud-amendments-optin.png","hud-nos-glow.png"};
  for(int k=0;k<3;k++){if(k==2){cfg.nos_glow=1;c.gear_change_in_progress=1;c.clock_ms=1000+410*cfg.glow_ms_per_unit;}if(k==1){cfg.inferred_redline=cfg.inferred_nos_level=cfg.shift_indicator=1;c.money=25;c.gear=2;c.speed_kmh=96.56f;c.rpm=7600;c.message_waiting=1;c.have_nos=1;c.have_nitro_tank=1;c.nitro_tank=.05f;c.have_redline_rpm=1;c.rpm_redline=7600;c.have_shift_rpm=1;c.shift_rpm=7500;}
   hud_configure(h,&cfg);if(k==2)hud_nos_glow_event(h,1000);glViewport(0,0,1280,720);glClear(GL_COLOR_BUFFER_BIT);hud_draw(h,NULL,NULL,&c,1280,720);char path[4096];snprintf(path,sizeof path,"%s/%s",argv[2],names[k]);capture(path,1280,720);hud_audit(h,&audit);resolved|=audit.resource_resolved;drawn|=audit.resource_drawn;printf("%s: resource resolved mask=0x%x drawn mask=0x%x\n",names[k],audit.resource_resolved,audit.resource_drawn);GLenum err=glGetError();if(err){fprintf(stderr,"GL error amendment state %d: 0x%x\n",k,err);return 1;}}
  hud_default_settings(&cfg);hud_configure(h,&cfg);}
 /* Same handle switches between all event packages and returns to free roam. */
 {const int kinds[]={N2_RACE_CIRCUIT,N2_RACE_SPRINT,N2_RACE_STREETX,N2_RACE_DRAG,N2_RACE_DRIFT};
  const unsigned counts[]={258,258,120,95,137};
  GLuint state_buffer,state_tex[2];glGenBuffers(1,&state_buffer);glGenTextures(2,state_tex);
  for(int k=0;k<5;k++)for(int aspect=0;aspect<2;aspect++)for(int phase=0;phase<3;phase++){
   HudState c=s;c.racing=1;c.race_kind=kinds[k];c.circuit=k!=1&&k!=3;
   c.lap=2;c.laps=3;c.position=2;c.cars=4;c.have_race_time=1;c.elapsed_seconds=83.45;
   c.race_phase=phase==0?HUD_RACE_COUNTDOWN:phase==1?HUD_RACE_RUNNING:HUD_RACE_FINISHED;
   c.countdown_seconds=2.5;c.drift_score=12345;c.drift_chain=678;c.drift_angle=27;
   c.engine_heat=.6;c.have_rpm=1;c.rpm=5000;c.rpm_maximum=10000;
   c.have_shift_rpm=1;c.shift_rpm=4800;c.clock_ms=1000+phase*100;
   float route[]={c.car_x-100,c.car_y-100,c.car_x,c.car_y,c.car_x+100,c.car_y+100};
   float rival[]={c.car_x+40,c.car_y+20};c.route_xy=route;c.route_n=3;c.route_next=2;c.opponent_xy=rival;c.opponent_n=1;
   int width=aspect?1280:960;glViewport(0,0,width,720);glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
   glBindBuffer(GL_ARRAY_BUFFER,state_buffer);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,state_tex[0]);
   glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,state_tex[1]);glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);glDisable(GL_BLEND);
   hud_draw(h,NULL,NULL,&c,width,720);
   GLint value;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&value);assert((GLuint)value==state_buffer);
   glGetIntegerv(GL_ACTIVE_TEXTURE,&value);assert(value==GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);assert((GLuint)value==state_tex[1]);
   glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);assert((GLuint)value==state_tex[0]);
   glGetIntegerv(GL_BLEND_SRC_RGB,&value);assert(value==GL_ONE);glGetIntegerv(GL_BLEND_DST_RGB,&value);assert(value==GL_ZERO);
   assert(!glIsEnabled(GL_BLEND));
   assert(glIsEnabled(GL_CULL_FACE)&&glIsEnabled(GL_DEPTH_TEST));GLboolean dm;glGetBooleanv(GL_DEPTH_WRITEMASK,&dm);assert(dm);
   hud_audit(h,&audit);assert(audit.nodes==counts[k]&&audit.gauge_targets);
   float wx,wy;int picked=hud_pick(h,-.7f,-.5f,&wx,&wy);
   if(kinds[k]==N2_RACE_DRAG)assert(!picked);
   char path[4096];snprintf(path,sizeof path,"%s/race-%d-%s-%d.png",argv[2],kinds[k],aspect?"169":"43",phase);capture(path,width,720);
   GLenum err=glGetError();if(err){fprintf(stderr,"race GL error %d: %x\n",kinds[k],err);return 1;}
   printf("race kind=%d aspect=%d phase=%d nodes=%u gauge=%d map=%d\n",kinds[k],aspect,phase,audit.nodes,audit.gauge_targets,audit.map_targets);
  }
  HudState hidden=s;hidden.racing=1;hidden.race_kind=N2_RACE_DRAG;hidden.have_nos=hidden.have_nitro_tank=1;
  hidden.nitro_tank=.5f;hidden.hide_gauge=1;hud_draw(h,NULL,NULL,&hidden,1280,720);hud_audit(h,&audit);
  assert(!(audit.resource_drawn&((1u<<0)|(1u<<15)|(1u<<16)|(1u<<17))));
  glBindBuffer(GL_ARRAY_BUFFER,0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
  glDeleteBuffers(1,&state_buffer);glDeleteTextures(2,state_tex);
 }
 s.hide_map=1;s.speed_kmh=NAN;s.rpm=INFINITY;s.gear=-2;hud_draw(h,NULL,NULL,&s,1280,720);s.hide_map=0;s.car_x=NAN;hud_draw(h,NULL,NULL,&s,1280,720);hud_draw(h,NULL,NULL,&s,0,480);hud_audit(h,&audit);const char*features[]={"needle","authored-redline","scale","fill","numeric-font-atlas","roads","map-mask","north","player","border-children","bank-backing","widget-backing-dpad","widget-centre-figure","widget-mail","widget-right-highlight","nos-backing","nos-alpha-arc","shift-icon","n2o-glow","minimap-icons","wrongway","box-laps","getaway","tach-nos"};for(unsigned j=0;j<24;j++)printf("resource %s: resolved=%u drawn=%u\n",features[j],(resolved>>j)&1,(drawn>>j)&1);printf("diagnostics: %s\n",hud_missing(h));hud_free(&h);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return audit.nodes&&audit.original_atlases&&audit.gauge_targets?0:1;}
