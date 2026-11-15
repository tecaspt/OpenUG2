/* Runtime-only installed data probe. Full configured ordinary dynamics on a
 * synthetic level ground query; no world meshes/collisions. Full-throttle 300 s. */
#include "drive_stage1.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static CtGround level_query(void *u,const float point[3],int wheel) {
 (void)u;(void)point;(void)wheel;return (CtGround){.found=1,.height=0,.surface=1,.normal={0,0,1}};
}
static void run(const N2PhysicsAttr *a,const DrvCar *c,const ResCar *r,int mode,
 const unsigned char *global,long size,const char *name){
 PhysDragConfig config;assert(phys_drag_config_read(&config,a,global,size,name,0,0));
 float p[3]={0},v[2]={0},h=0,speed=0,max[7]={0},rpm[7]={0};int gear=1;
 PhysVehicle vehicle=phys_vehicle_from_source(a,0,0,1.4f,2.5f,1.5f,.2f);PhysManual manual={.gear=1};
 PhysRideState level={.contact_mask=15};Stage1Drive stage={0};if(mode==2) {
  BrkCar brakes;LatCar lat;CtCar contact;BodyCar body;
  assert(stage3_car_read(global,size,name,&brakes));assert(stage4_car_read(global,size,name,&lat));
  assert(stage5_car_read(global,size,name,&contact));assert(stage5_body_car_read(global,size,name,&body));
  assert(!stage1_drive_init(&stage,c,r,0));assert(!stage3_drive_brakes(&stage,&brakes,0,0));
  assert(!stage4_drive_lateral(&stage,&lat,1/lat_inertia(c->mass,lat.dimensions),0));
  assert(!stage5_drive_contacts(&stage,&contact,level_query,NULL));assert(!stage5_drive_body(&stage,&body,1,0,0));
  stage.session=drive_session_local(1,0);p[2]=.5f;
 }
 for(int tick=0;tick<18000;tick++){
  if(mode!=2){int shift=(mode==0 || mode==3)?(tick>=140 && (tick-140)%60==0):(manual.rpm>=a->redline_rpm && manual.gear<a->gearbox[0].gear_count);
   if(mode>=3)phys_drag_step(&manual,&config,p,v,&h,&speed,1,0,0,shift,NULL,&vehicle,NULL);
   else {float gain=phys_manual_step(&manual,a,0,0,c->wheel_radius[0],speed,1,shift,1/60.f);PhysVehicle driven=vehicle;driven.accel*=gain;
   phys_car_step(p,v,&h,&speed,gain>0?1:0,0,0,NULL,&driven);}gear=manual.gear;
  }else{stage1_drive_step(&stage,p,v,&h,&speed,1,0,0,NULL,&vehicle,&level);gear=stage1_drive_gear(&stage);}
  assert(isfinite(speed) && isfinite(h) && isfinite(v[0]) && isfinite(v[1]));
  if(gear>=1 && gear<=6 && PHYS_KMH(speed)>max[gear]){max[gear]=PHYS_KMH(speed);rpm[gear]=mode!=2?manual.rpm:stage1_drive_rpm(&stage);}
 }
 float peak=0;for(int i=1;i<=6;i++)peak=fmaxf(peak,max[i]);
 assert(peak>20);if(mode==2 || mode>=3)assert(peak>100);
 const char *modes[]={"legacy-drag-early","legacy-drag-redline","freeroam-level-full-host","new-drag-early","new-drag-redline"};
 for(int i=1;i<=a->gearbox[0].gear_count;i++)printf("PROBE %s gear=%d max_kmh=%.3f rpm=%.1f\n",modes[mode],i,max[i],rpm[i]);
}
int main(void){const char *root=getenv("OPENUG2_DATA");if(!root)return 77;char path[1024];snprintf(path,sizeof path,"%s/GLOBAL/GLOBALB.BUN",root);long n=0;unsigned char *b=n2_read_file(path,&n);if(!b)return 1;
 const char *names[]={"COROLLA","GOLF","MIATA"};for(int k=0;k<3;k++){N2PhysicsAttr a;DrvCar c;ResCar r;assert(n2_global_physics_attr(b,n,names[k],&a));assert(stage1_car_read(b,n,names[k],&c));assert(stage2_car_read(b,n,names[k],&r));
 printf("SOURCE_DRIVE %s rear_share=%.6f front_radius=%.6f rear_radius=%.6f final_front=%.6f final_rear=%.6f\n",names[k],a.rear_drive,c.wheel_radius[0],c.wheel_radius[2],c.final_front,c.final_rear);
 printf("CAR %s mass=%f radius=%f drag=%f idle=%.0f redline=%.0f limiter=%.0f final=%f ratios=",names[k],a.mass_tonnes,c.wheel_radius[0],r.drag,a.idle_rpm,a.redline_rpm,a.limiter_rpm,a.gearbox[0].final_drive);for(int j=0;j<a.gearbox[0].gear_count;j++)printf(" %f",a.gearbox[0].forward[j]);puts("");for(int mode=0;mode<5;mode++)run(&a,&c,&r,mode,b,n,names[k]);
 }free(b);puts("drag_speed_check: PASS (level-road source probes; not a world race replay)");return 0;}
