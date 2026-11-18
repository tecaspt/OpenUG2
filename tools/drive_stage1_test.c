/* Synthetic gameplay adapter regressions; no retail assets. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "drive_stage1.h"
#ifdef NDEBUG
#error assertions required
#endif
static DrvCar car(float mass) {
    DrvCar c={0};c.mass=mass;c.gear_count=4;c.rear_share=.9f;
    c.final_front=c.final_rear=4;c.clutch_k=.05f;c.clutch_rate=2.5f;
    c.ratio[0]=-3;c.ratio[2]=3;c.ratio[3]=2;c.ratio[4]=1.4f;c.ratio[5]=1;
    c.idle_rpm=1000;c.shift_rpm=6000;c.max_rpm=7000;c.engine_inertia=.0003f;
    for(int k=0;k<4;k++)c.wheel_radius[k]=.32f;
    for(int k=0;k<2;k++){c.theta_max[k]=12;c.mu_peak[k]=1.3f;c.mu_slide[k]=1.1f;}
    for(int k=0;k<9;k++)c.torque_primary[k]=.3f;
    for(int k=0;k<3;k++)c.engine_brake[k]=.03f;
    assert(drv_car_finalize(&c)==0);return c;
}
static const BrkCar brakes={.service=.45f,.handbrake=.75f,.rear_share=.3f};
static int configured_init(Stage1Drive *s,const DrvCar *car,const ResCar *air,float reduction) {
    int result=stage1_drive_init(s,car,air,reduction);
    return result?result:stage3_drive_brakes(s,&brakes,0,0);
}
static const ResCar resistance={.drag=.0005f,.front=.12f,.rear=.18f,.limit_mph=0};
typedef struct { Stage1Drive drive;float p[3],v[2],h,s;int tick; } Trace;
static void init(Trace *t,float mass){memset(t,0,sizeof *t);DrvCar c=car(mass);assert(configured_init(&t->drive,&c,&resistance,0)==0);}
static void step(Trace *t,float pedal,int hb,unsigned contact){
    PhysRideState ride={.contact_mask=contact};
    stage1_drive_step(&t->drive,t->p,t->v,&t->h,&t->s,pedal,0,hb,NULL,NULL,&ride);
    assert(isfinite(t->s)&&isfinite(t->drive.drivetrain.omega_e));
    assert(t->drive.drivetrain.omega_e>=t->drive.drivetrain.car.idle_rpm*DRV_KW);
    ++t->tick;
}
/* Serialize invented tuning into the permitted independent parser's framing.
 * This checks offset mapping and validation without opening a game file. */
static void parser_test(void){
    unsigned char bytes[16+0x890]={0};
    uint32_t tag=0x34600,size=8+0x890;
    memcpy(bytes,&tag,4);memcpy(bytes+4,&size,4);memset(bytes+8,0x11,8);
    unsigned char *row=bytes+16;strcpy((char *)row,"SYNTH");
    strcpy((char *)row+0x40,"CARS\\SYNTH\\GEOMETRY.BIN");
    DrvCar c=car(1.2f),out;
#define PUT(field,offset) memcpy(row+(offset),&(field),sizeof(field))
    PUT(c.mass,0x220);
    for(int k=0;k<4;k++){
        PUT(c.wheel_radius[k],0x130+0x30*k);
        uint32_t index=k;PUT(index,0x138+0x30*k);
    }
    for(int a=0;a<2;a++){
        PUT(c.theta_max[a],0x1e0+0x20*a);
        PUT(c.mu_peak[a],0x1ec+0x20*a);PUT(c.mu_slide[a],0x1f0+0x20*a);
    }
    PUT(c.launch_fraction,0x2c0);PUT(c.final_front,0x2c8);PUT(c.final_rear,0x2cc);
    PUT(c.rear_share,0x2d0);PUT(c.clutch_k,0x2d4);
    uint32_t count=c.gear_count;PUT(count,0x2d8);PUT(c.clutch_rate,0x2dc);
    PUT(c.ratio,0x2e0);PUT(c.idle_rpm,0x300);PUT(c.shift_rpm,0x304);
    PUT(c.max_rpm,0x308);PUT(c.engine_inertia,0x30c);
    PUT(c.torque_primary,0x310);PUT(c.torque_secondary,0x334);PUT(c.engine_brake,0x358);
    assert(stage1_car_read(bytes,sizeof bytes,"SYNTH",&out));
    assert(!memcmp(&out,&c,sizeof c));
    {   /* career upgrades (stage1_car_read_upgraded) */
        DrvCar st=out,u;DrvUpgrade z={0};
        assert(stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",NULL,&u)&&!memcmp(&u,&st,sizeof st));
        assert(stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",&z,&u)&&!memcmp(&u,&st,sizeof st));
        /* gearbox L3 block = stock block with a different final drive */
        unsigned char blk[0x40];memcpy(blk,row+0x2c0,0x40);float fd=c.final_front*1.1f;memcpy(blk+0x8,&fd,4);
        memcpy(row+0x4e0,blk,0x40);
        DrvUpgrade g={0};g.gear_level=3;
        assert(stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",&g,&u));
        assert(fabsf(u.final_front-fd)<1e-6f && u.gear_count==c.gear_count);
        g.gear_level=1;   /* L1 block 0x460 is all zero: invalid car, read refused */
        assert(!stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",&g,&u));
        /* engine: master add-curve 0x530 = 0.05 per sample, L2 header raise 500/700 rpm */
        float add=.05f,r1=500,r2=700;for(int k=0;k<9;k++)memcpy(row+0x530+4*k,&add,4);
        memcpy(row+0x5a0+4,&r1,4);memcpy(row+0x5a0+8,&r2,4);
        DrvUpgrade e={0};e.engine=.5f;e.engine_level=2;
        assert(stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",&e,&u));
        for(int k=0;k<9;k++)assert(fabsf(u.torque_primary[k]-(c.torque_primary[k]+.025f))<1e-5f);
        assert(fabsf(u.shift_rpm-(c.shift_rpm+500))<1e-3f && fabsf(u.max_rpm-(c.max_rpm+700))<1e-3f);
        /* tyres: max block 0x650/0x670 = stock + 0.4; half fraction -> + 0.2 */
        for(int a=0;a<2;a++){float pk=c.mu_peak[a]+.4f,sl=c.mu_slide[a]+.4f;memcpy(row+0x650+0x20*a+0xc,&pk,4);memcpy(row+0x650+0x20*a+0x10,&sl,4);}
        DrvUpgrade t={0};t.tires=.5f;
        assert(stage1_car_read_upgraded(bytes,sizeof bytes,"SYNTH",&t,&u));
        for(int a=0;a<2;a++)assert(fabsf(u.mu_peak[a]-(c.mu_peak[a]+.2f))<1e-5f && fabsf(u.mu_slide[a]-(c.mu_slide[a]+.2f))<1e-5f);
        memset(row+0x460,0,0x40*3);memset(row+0x530,0,4*9);memset(row+0x5a0,0,12);memset(row+0x650,0,0x40);
        assert(stage1_car_read(bytes,sizeof bytes,"SYNTH",&u)&&!memcmp(&u,&st,sizeof st));
    }
    DrvCar saved=out;
    float huge[9];for(int k=0;k<9;k++)huge[k]=FLT_MAX;
    PUT(huge,0x310);PUT(huge,0x334);
    assert(!stage1_car_read(bytes,sizeof bytes,"SYNTH",&out));
    assert(!memcmp(&saved,&out,sizeof out));
    PUT(c.torque_primary,0x310);PUT(c.torque_secondary,0x334);
    ResCar air= { .drag=.0007f,.front=.13f,.rear=.27f,.limit_mph=137 },air_out;
    PUT(air.drag,0x370);PUT(air.front,0x374);PUT(air.rear,0x378);PUT(air.limit_mph,0x3a0);
    assert(stage2_car_read(bytes,sizeof bytes,"SYNTH",&air_out));assert(!memcmp(&air,&air_out,sizeof air));
    float huge_drag=FLT_MAX;PUT(huge_drag,0x370);
    assert(stage2_car_read(bytes,sizeof bytes,"SYNTH",&air_out));
    Trace t={0};assert(configured_init(&t.drive,&c,&air_out,0)==0);
    t.v[0]=10/PHYS_TICKRATE;step(&t,0,0,15);
    assert(isfinite(t.p[0])&&isfinite(t.drive.body.force[0]));
    PUT(air.drag,0x370);
    assert(!stage2_car_read(bytes,sizeof bytes-1,"SYNTH",&air_out));
    assert(!stage2_car_read(bytes,sizeof bytes,"MISSING",&air_out));
    float invalid=-1;PUT(invalid,0x374);assert(!stage2_car_read(bytes,sizeof bytes,"SYNTH",&air_out));
    invalid=NAN;PUT(invalid,0x374);assert(!stage2_car_read(bytes,sizeof bytes,"SYNTH",&air_out));
    BrkCar brake_out;float adjacent=99;
    PUT(adjacent,0x380);PUT(brakes.service,0x384);PUT(brakes.handbrake,0x388);PUT(brakes.rear_share,0x38c);
    assert(stage3_car_read(bytes,sizeof bytes,"SYNTH",&brake_out));assert(!memcmp(&brakes,&brake_out,sizeof brakes));
    assert(!stage3_car_read(bytes,sizeof bytes-1,"SYNTH",&brake_out));
    assert(!stage3_car_read(bytes,sizeof bytes,"MISSING",&brake_out));
    invalid=-1;PUT(invalid,0x384);assert(!stage3_car_read(bytes,sizeof bytes,"SYNTH",&brake_out));
    invalid=NAN;PUT(invalid,0x384);assert(!stage3_car_read(bytes,sizeof bytes,"SYNTH",&brake_out));
    PUT(brakes.service,0x384);invalid=1.1f;PUT(invalid,0x38c);
    assert(!stage3_car_read(bytes,sizeof bytes,"SYNTH",&brake_out));
    assert(!stage1_car_read(bytes,sizeof bytes-1,"SYNTH",&out));
    assert(!stage1_car_read(bytes,sizeof bytes,"MISSING",&out));
    float bad=NAN;PUT(bad,0x30c);
    assert(!stage1_car_read(bytes,sizeof bytes,"SYNTH",&out));
    PUT(c.engine_inertia,0x30c);row[0x138]=3;
    assert(!stage1_car_read(bytes,sizeof bytes,"SYNTH",&out));
#undef PUT
}
static void numeric_boundaries(void){
    Trace t;init(&t,1.2f);Stage1Drive saved=t.drive;DrvCar c=car(1.2f);
    for(int k=0;k<9;k++)c.torque_primary[k]=c.torque_secondary[k]=FLT_MAX;
    assert(stage1_drive_init(&t.drive,&c,&resistance,0)<0);
    assert(!memcmp(&saved,&t.drive,sizeof saved));
    Drivetrain old=saved.drivetrain;
    assert(drv_init(&saved.drivetrain,&c,NULL)<0);
    assert(!memcmp(&old,&saved.drivetrain,sizeof old));
    c=car(1.2f);c.launch_fraction=FLT_MAX;
    assert(stage1_drive_init(&t.drive,&c,&resistance,0)<0);
    assert(!memcmp(&t.drive,&saved,sizeof saved));
    ResCar air=resistance;air.drag=FLT_MAX;
    assert(configured_init(&t.drive,&c,&air,0)<0);
    c=car(1.2f);assert(configured_init(&t.drive,&c,&air,0)==0);
    t.v[0]=10/PHYS_TICKRATE;step(&t,0,0,15);
    for(int k=0;k<3;k++)assert(isfinite(t.p[k])&&isfinite(t.drive.body.force[k]));
    assert(isfinite(t.s));
}
static void tests(void){
    Trace a;init(&a,1.2f);
    for(int k=0;k<600;k++)step(&a,1,0,15);
    printf("launch 10 s: %.2f km/h gear %d rpm %.0f\n",PHYS_KMH(a.s),stage1_drive_gear(&a.drive),stage1_drive_rpm(&a.drive));
    assert(a.s>20/3.6f/PHYS_TICKRATE&&a.p[0]>20);
    assert(stage1_drive_gear(&a.drive)>=2);
    float launch=a.s;
    for(int k=0;k<120;k++)step(&a,0,0,15);
    assert(a.s<launch&&a.s>0);
    float coast=a.s;
    for(int k=0;k<60;k++)step(&a,-1,0,15);
    assert(a.s<coast);
    for(int k=0;k<600;k++)step(&a,-1,0,15);
    assert(a.s<-.02f&&stage1_drive_gear(&a.drive)==-1);
    for(int k=0;k<300;k++)step(&a,1,0,15);
    assert(a.s>0&&stage1_drive_gear(&a.drive)>0);
    for(int k=0;k<600;k++)step(&a,1,1,15);
    assert(a.s==0&&a.v[0]==0&&a.v[1]==0);
    stage1_drive_reset(&a.drive,a.v,a.h);
    assert(stage1_drive_gear(&a.drive)==0&&a.drive.drivetrain.feedback==0);
    assert(a.drive.drivetrain.engage==0&&a.drive.drivetrain.wheel[0].omega==0);
    Trace air;init(&air,1.2f);
    for(int k=0;k<120;k++)step(&air,1,0,0);
    assert(air.s==0&&air.p[0]==0);
    /* A paused/zero-tick frame never calls the adapter: all state remains exact. */
    Stage1Drive snapshot=a.drive;PhysClock clock={0};assert(phys_clock_steps(&clock,0)==0);
    assert(!memcmp(&snapshot,&a.drive,sizeof snapshot));
    /* A fresh car replaces every engine/wheel/clutch state. */
    DrvCar c=car(1.8f);assert(configured_init(&a.drive,&c,&resistance,0)==0);
    assert(a.drive.drivetrain.car.mass==1.8f&&stage1_drive_gear(&a.drive)==0);
    /* No engine torque means no hidden arcade propulsion. */
    for(int k=0;k<9;k++)c.torque_primary[k]=0;
    c.clutch_k=0; /* disconnect the idle-clamped rotating engine as well */
    assert(configured_init(&a.drive,&c,&resistance,0)==0);memset(a.v,0,sizeof a.v);a.s=0;
    for(int k=0;k<300;k++)step(&a,1,0,15);
    assert(a.s==0);
    /* Every axle class uses actual shaft torque, and steering stays live. */
    const float shares[]={0,.5f,1};
    for(unsigned i=0;i<sizeof shares/sizeof *shares;i++){
        Trace t;init(&t,1.2f);c=car(1.2f);c.rear_share=shares[i];
        assert(configured_init(&t.drive,&c,&resistance,0)==0);
        for(int k=0;k<600;k++)step(&t,1,0,15);
        assert(t.s>.05f&&stage1_drive_gear(&t.drive)>=2);
        float oldh=t.h;PhysRideState ride={.contact_mask=15};
        stage1_drive_step(&t.drive,t.p,t.v,&t.h,&t.s,1,.5f,0,NULL,NULL,&ride);
        assert(t.h>oldh);
        /* Collision impulses enter through changed host velocity; no teleport
           or stale velocity copy restores the previous body motion. */
        t.v[0]=t.v[1]=0;t.s=0;
        step(&t,0,1,15);assert(t.s==0);
        t.v[0]=.2f;t.v[1]=.1f;
        float airh=t.h;
        for(int k=0;k<30;k++)step(&t,1,0,0);
        assert(t.h==airh&&t.v[1]>0&&t.v[1]<.1f);
    }
    Trace reference;init(&reference,1.2f);
    for(int k=0;k<600;k++)step(&reference,k<420?1:0,0,15);
    int rates[]={30,60,90,144,240};
    for(unsigned r=0;r<sizeof rates/sizeof *rates;r++){
        Trace t;init(&t,1.2f);clock=(PhysClock){0};
        for(int frame=0;frame<10*rates[r];frame++){
            int n=phys_clock_steps(&clock,1.0/rates[r]);
            for(int k=0;k<n;k++)step(&t,t.tick<420?1:0,0,15);
        }
        assert(t.tick==reference.tick&&t.s==reference.s&&t.p[0]==reference.p[0]);
        assert(!memcmp(&t.drive,&reference.drive,sizeof t.drive));
    }
    /* Refuse missing/invalid data instead of substituting invented car tuning. */
    DrvCar out;assert(!stage1_car_read(NULL,0,"SYNTH",&out));
    c=car(1.2f);c.ratio[2]=NAN;assert(configured_init(&a.drive,&c,&resistance,0)<0);
}
/* Controlled planar fixtures isolate the Stage-2 adapter boundary from tyres. */
static void resistance_integration(void) {
    Trace a;init(&a,1.25f);DrvCar c=car(1.25f);
    c.clutch_k=0;for(int k=0;k<9;k++)c.torque_primary[k]=0;
    ResCar air={.drag=.0005f,.front=.12f,.rear=.18f};
    assert(configured_init(&a.drive,&c,&air,0)==0);
    a.drive.resistance_input.stabilize=0;a.drive.disable_stabilize=1;
    a.v[0]=30/PHYS_TICKRATE;
    /* With no contact, drag still opposes motion. Independently calculate the
     * exact four prepared-force Euler steps, including persistent b32 stores. */
    float expected=30,force=(float)(-(double)air.drag*2*30*30),h;
    int n=drv_substep_count(1/PHYS_TICKRATE,&h);float momentum=c.mass*expected;
    for(int k=0;k<n;k++){
        momentum=(float)(momentum+(double)h*force);expected=(float)((double)momentum/c.mass);
        force=(float)(-(double)air.drag*2*expected*expected);
    }
    step(&a,0,0,0);assert(fabsf(a.s*PHYS_TICKRATE-expected)<.00001f);
    assert(a.drive.aero.load>0&&a.drive.body.velocity[2]==0);
    /* sf.drag and all host caps must have no influence on this mode. */
    Trace b=a;PhysSurface strange=PHYS_SURF_ROAD;strange.drag=.1f;strange.topfrac=.01f;
    PhysRideState ride={.contact_mask=15};
    a.drive.prepared=b.drive.prepared=0;a.drive.body.force[0]=b.drive.body.force[0]=0;
    float oldtop=g_phys_tune.top_kmh;g_phys_tune.top_kmh=1;
    stage1_drive_step(&a.drive,a.p,a.v,&a.h,&a.s,0,0,0,NULL,NULL,&ride);
    stage1_drive_step(&b.drive,b.p,b.v,&b.h,&b.s,0,0,0,&strange,NULL,&ride);
    g_phys_tune.top_kmh=oldtop;
    assert(a.s==b.s&&a.s*PHYS_TICKRATE>20);
    /* Positive-force threshold replaces thrust without clamping speed. */
    init(&a,1.25f);a.v[0]=80/PHYS_TICKRATE;a.drive.resistance_car=(ResCar){.limit_mph=50};
    a.drive.disable_stabilize=1;a.drive.prepared=1;a.drive.body.force[0]=10;
    step(&a,0,0,0);assert(a.s*PHYS_TICKRATE<80&&a.s*PHYS_TICKRATE>79);
    assert(a.drive.body.saved_force[0]==0); /* final substep has no new thrust */
    /* Absolute cap is symmetric and independent of host road speed settings. */
    for(int sign=-1;sign<=1;sign+=2) {
        init(&a,1.25f);a.drive.resistance_car=(ResCar){0};a.drive.disable_stabilize=1;
        a.v[0]=sign*250/PHYS_TICKRATE;step(&a,0,0,0);
        assert(fabsf(fabsf(a.s)*PHYS_TICKRATE-200)<.0001f);
    }
    /* Damage and the two surface channels are supplied separately every frame. */
    init(&a,1.25f);a.drive.damage_mask=5;
    a.drive.rolling_surface[0]=3;a.drive.body_surface[0]=9;a.drive.body_surface[2]=3;
    step(&a,0,0,5);
    assert(a.drive.resistance_input.surface==6&&a.drive.resistance_input.contacts==2);
    assert(a.drive.drivetrain.env.wheel[0].roll_drag==3);
    assert(a.drive.drivetrain.env.wheel[0].drag_flag==1&&a.drive.drivetrain.env.wheel[2].drag_flag==1);
    assert(a.drive.drivetrain.env.wheel[1].drag_flag==0);
    a.drive.body.z=.5f;a.drive.resistance_input.reduction=.4f;
    stage1_drive_reset(&a.drive,a.v,a.h);
    assert(a.drive.body.z==0&&a.drive.resistance_input.reduction==.4f&&a.drive.damage_mask==5);
    air.drag=NAN;assert(configured_init(&a.drive,&c,&air,0)<0);
    assert(configured_init(&a.drive,&c,&resistance,NAN)<0);
}
static void braking_integration(void) {
    Trace t;init(&t,1.2f);DrvCar c=car(1.2f);
    c.clutch_k=0;for(int i=0;i<9;i++)c.torque_primary[i]=0;
    for(int i=0;i<3;i++)c.engine_brake[i]=0;
    ResCar zero={0};assert(configured_init(&t.drive,&c,&zero,0)==0);
    t.drive.disable_stabilize=1;t.v[0]=20/PHYS_TICKRATE;
    for(int i=0;i<4;i++) {
        t.drive.drivetrain.wheel[i].omega=20/c.wheel_radius[i];
        t.drive.drivetrain.wheel[i].dir=DRV_DIR_FORWARD;
    }
    /* No initial hidden body deceleration: braking changes only the coupled
     * wheel forces. The old host brake sliders have no service authority. */
    Trace a=t,b=t;float old=g_phys_tune.brake;PhysVehicle vh={0};vh.brake=100;
    PhysRideState ride={.contact_mask=15};g_phys_tune.brake=100;
    stage1_drive_step(&a.drive,a.p,a.v,&a.h,&a.s,-1,0,0,NULL,&vh,&ride);
    g_phys_tune.brake=.001f;
    stage1_drive_step(&b.drive,b.p,b.v,&b.h,&b.s,-1,0,0,NULL,NULL,&ride);
    g_phys_tune.brake=old;assert(a.s==b.s&&a.s<20/PHYS_TICKRATE);
    /* Zero requested service strength removes service authority entirely when
     * all other opposing sources are zero. Rolling itself stays in the wheels. */
    BrkCar none=brakes;none.service=0;
    a=t;b=t;assert(stage3_drive_brakes(&b.drive,&none,0,0)==0);
    step(&a,-1,0,15);step(&b,-1,0,15);assert(a.s<b.s);
    /* Airborne brake torque still changes wheel spin; no contact body force. */
    a=t;b=t;step(&a,-1,0,0);step(&b,0,0,0);
    assert(a.s==b.s&&a.s==20/PHYS_TICKRATE);
    assert(a.drive.drivetrain.wheel[0].omega<b.drive.drivetrain.wheel[0].omega);
    /* Locked tyres still contribute their final force exactly once. */
    a=t;BrkCar strong=brakes;strong.service=10;
    assert(stage3_drive_brakes(&a.drive,&strong,0,1)==0);
    a.v[0]=5/PHYS_TICKRATE;
    for(int i=0;i<4;i++)a.drive.drivetrain.wheel[i].omega=1;
    step(&a,-1,0,15);
    assert(a.drive.drivetrain.brakes.locked[0]);
    assert(a.drive.prepared_wheel[0][0]<0);
    float force=0;for(int i=0;i<4;i++)force+=a.drive.prepared_wheel[i][0];
    assert(fabsf(a.drive.body.force[0]-force)<.000001f);
    /* Reset uses explicit host initialization, keeps relief selection and car
     * tuning, clears prepared state, and supplies the saved-state direction. */
    assert(stage3_drive_brakes(&t.drive,&brakes,5,1)==0);
    t.v[0]=-.1f;stage1_drive_reset(&t.drive,t.v,0);
    assert(t.drive.initial_lock_mask==5&&t.drive.drivetrain.brakes.relief_disabled);
    assert(t.drive.drivetrain.brakes.locked[0]&&!t.drive.drivetrain.brakes.locked[1]);
    assert(t.drive.drivetrain.wheel[0].dir==DRV_DIR_BACKWARD&&!t.drive.prepared);
    assert(stage3_drive_brakes(&t.drive,&brakes,16,0)<0);
}
int main(void){parser_test();numeric_boundaries();tests();resistance_integration();braking_integration();phys_selftest();puts("Stage 1 gameplay adapter tests passed");return 0;}
