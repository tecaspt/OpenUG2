/* Invented vectors only: no installed assets. */
#include "physics.h"
#include <assert.h>
#include <stdio.h>
static N2PhysicsAttr motor(void) {
    N2PhysicsAttr a={.mass_tonnes=1.2f,.idle_rpm=800,.redline_rpm=6500,.limiter_rpm=7200};
    for(int k=0;k<9;k++)a.torque[k]=.2f;
    for(int k=0;k<4;k++)a.gearbox[k]=(N2GearboxAttr){.final_drive=4,.gear_count=5,.forward={3.2f,2.1f,1.5f,1.1f,.85f}};
    return a;
}
static float pull(const PhysDragConfig *config,int modern) {
    float p[3]={0},v[2]={0},h=0,speed=0;PhysManual manual={.gear=1};
    PhysVehicle profile=phys_vehicle_from_source(config->source,0,0,1.4f,2.5f,1.5f,.2f);
    for(int k=0;k<7200;k++) {
        int shift=k>=140 && (k-140)%60==0;
        if(modern)phys_drag_step(&manual,config,p,v,&h,&speed,1,0,0,shift,NULL,&profile,NULL);
        else {
            float gain=phys_manual_step(&manual,config->source,0,0,config->wheel_radius,speed,1,shift,1/60.f);
            PhysVehicle driven=profile;driven.accel*=gain;
            phys_car_step(p,v,&h,&speed,gain>0?1:0,0,0,NULL,&driven);
        }
        assert(isfinite(speed) && speed>=0);
    }
    return PHYS_KMH(speed);
}
int main(void) {
    N2PhysicsAttr a=motor();PhysDragConfig c={&a,.3f,.0005f,0,0};assert(phys_drag_config_valid(&c));
    float before=pull(&c,0),after=pull(&c,1);
    fprintf(stderr,"synthetic speed legacy=%.3f dimensional=%.3f km/h\n",before,after);
    assert(after>150 && after>before*1.5f);
    float p[3]={0},v[2]={0},h=0,speed=0;PhysManual m={.gear=5};
    v[0]=speed=100/(PHYS_TICKRATE*3.6f);
    for(int k=0;k<60;k++)phys_drag_step(&m,&c,p,v,&h,&speed,1,0,0,0,NULL,NULL,NULL);
    assert(PHYS_KMH(speed)>100);
    float old=speed;phys_drag_step(&m,&c,p,v,&h,&speed,0,0,0,0,NULL,NULL,NULL);assert(speed<old);
    old=speed;phys_drag_step(&m,&c,p,v,&h,&speed,-1,0,0,0,NULL,NULL,NULL);assert(speed<old);
    PhysRideState airborne={0};old=speed;
    phys_drag_step(&m,&c,p,v,&h,&speed,1,0,0,0,NULL,NULL,&airborne);assert(speed==old);
    PhysDragConfig invalid=c;invalid.air_drag=NAN;old=p[0];
    assert(!phys_drag_config_valid(&invalid));phys_drag_step(&m,&invalid,p,v,&h,&speed,1,0,0,0,NULL,NULL,NULL);assert(p[0]==old);
    unsigned char bytes[16+0x890]={0};uint32_t tag=0x34600,size=8+0x890;
    memcpy(bytes,&tag,4);memcpy(bytes+4,&size,4);memset(bytes+8,0x11,8);
    strcpy((char *)bytes+16,"SYNTH");strcpy((char *)bytes+16+0x40,"CARS\\SYNTH\\GEOMETRY.BIN");
    float front=.3f,rear=.32f,drag=.0005f;a.rear_drive=1;
    memcpy(bytes+16+0x130,&front,4);memcpy(bytes+16+0x190,&rear,4);memcpy(bytes+16+0x370,&drag,4);
    PhysDragConfig parsed={0};assert(phys_drag_config_read(&parsed,&a,bytes,sizeof bytes,"SYNTH",0,0));assert(parsed.wheel_radius==rear && parsed.air_drag==drag);
    PhysDragConfig saved=parsed;assert(!phys_drag_config_read(&parsed,&a,bytes,10,"SYNTH",0,0));assert(!memcmp(&saved,&parsed,sizeof saved));
    printf("drag_physics_test: PASS (legacy %.2f km/h, dimensional %.2f km/h; high gear/coast/brake/airborne/parser/invalid)\n",before,after);
    return 0;
}
