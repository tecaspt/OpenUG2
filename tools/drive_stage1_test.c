/* Synthetic gameplay adapter regressions; no retail assets. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
        DrvUpgrade e={0};e.engine=.5f;e.ecu_level=2;
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
/* Synthetic regressions cannot prove the decoded upgrade offsets, only that a pattern is read the way
 * it is written. This section opens the retail car table and checks the applied categories against
 * numbers read out of the record itself, on four player cars and one traffic car. The data root is
 * argv[1] or OPENUG2_DATA; without either the section is skipped, never silently passed. No path is
 * baked in. */
static const unsigned char *g_data;
static long g_len;
static float rec_f(const unsigned char *b,long row,int off){float v;memcpy(&v,b+row+off,4);return v;}
static int   rec_n(const unsigned char *b,long row,int off){unsigned v;memcpy(&v,b+row+off,4);return (int)v;}
static int same_car(const DrvCar *a,const DrvCar *b){return !memcmp(a,b,sizeof *a);}
/* One car, stock baseline plus every implemented effect at fraction 1.0 / level 3. */
static void retail_car(long row,const char *name){
    DrvCar stock,up;DrvUpgrade full={0},part={0};
    unsigned char *rec=malloc(0x890);assert(rec);
    assert(stage1_car_read(g_data,g_len,name,&stock));
    assert(stage1_car_read_upgraded(g_data,g_len,name,NULL,&up)&&same_car(&up,&stock));
    DrvUpgrade z={0};
    assert(stage1_car_read_upgraded(g_data,g_len,name,&z,&up)&&same_car(&up,&stock));
    memcpy(rec,g_data+row,0x890);
    /* tyres: the max block is the lerp ceiling, axle by axle, where the guard allows it */
    for(int a=0;a<2;a++){
        float pk=rec_f(rec,0x650+0x20*a+0xc,0),sl=rec_f(rec,0x650+0x20*a+0x10,0);
        part=(DrvUpgrade){0};part.tires=1.0f;
        assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up));
        if(pk>=stock.mu_peak[a]&&sl>=stock.mu_slide[a]){
            assert(fabsf(up.mu_peak[a]-pk)<1e-6f&&fabsf(up.mu_slide[a]-sl)<1e-6f);
            assert(up.mu_peak[a]>=stock.mu_peak[a]&&up.mu_slide[a]>=stock.mu_slide[a]);
            part.tires=.5f;
            assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up));
            assert(fabsf(up.mu_peak[a]-(stock.mu_peak[a]+.5f*(pk-stock.mu_peak[a])))<1e-5f);
        } else {   /* max below stock (every traffic car): refused, stock stands */
            assert(fabsf(up.mu_peak[a]-stock.mu_peak[a])<1e-6f&&fabsf(up.mu_slide[a]-stock.mu_slide[a])<1e-6f);
        }
    }
    /* gearbox: the level block replaces the stock block wholesale */
    for(int lv=1;lv<=3;lv++){
        int gb=0x460+0x40*(lv-1);
        part=(DrvUpgrade){0};part.gear_level=lv;
        assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up));
        assert(up.gear_count==rec_n(rec,gb+0x18,0));
        assert(fabsf(up.final_front-rec_f(rec,gb+0x8,0))<1e-6f&&fabsf(up.final_rear-rec_f(rec,gb+0xc,0))<1e-6f);
        assert(fabsf(up.rear_share-rec_f(rec,gb+0x10,0))<1e-6f&&fabsf(up.clutch_k-rec_f(rec,gb+0x14,0))<1e-6f);
        assert(fabsf(up.clutch_rate-rec_f(rec,gb+0x1c,0))<1e-6f&&fabsf(up.launch_fraction-rec_f(rec,gb,0))<1e-6f);
        for(int k=0;k<6;k++)assert(fabsf(up.ratio[k]-rec_f(rec,gb+0x20,4*k))<1e-6f);
    }
    /* engine: fraction 1.0 = the whole master add-curve; the level header alone raises rpm only */
    part=(DrvUpgrade){0};part.engine=1.0f;
    assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up));
    for(int k=0;k<9;k++)assert(fabsf(up.torque_primary[k]-(stock.torque_primary[k]+rec_f(rec,0x530,4*k)))<1e-6f);
    assert(fabsf(up.shift_rpm-stock.shift_rpm)<1e-4f&&fabsf(up.max_rpm-stock.max_rpm)<1e-4f);
    part=(DrvUpgrade){0};part.ecu_level=3;
    assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up));
    assert(fabsf(up.shift_rpm-(stock.shift_rpm+rec_f(rec,0x5e4,0)))<1e-3f);
    assert(fabsf(up.max_rpm-(stock.max_rpm+rec_f(rec,0x5e8,0)))<1e-3f);
    assert(fabsf(up.torque_primary[0]-stock.torque_primary[0])<1e-6f); /* level alone adds no torque */
    /* the suspension fraction must not move any Stage 1 field (docs/parity/PERF-PAINT.md) */
    part=(DrvUpgrade){0};part.suspension=1.0f;
    assert(stage1_car_read_upgraded(g_data,g_len,name,&part,&up)&&same_car(&up,&stock));
    /* fully upgraded against stock, the table for docs/parity/PERF-PAINT.md */
    full.engine=1.0f;full.ecu_level=3;full.gear_level=3;full.tires=1.0f;full.suspension=1.0f;
    assert(stage1_car_read_upgraded(g_data,g_len,name,&full,&up));
    float tp0=0,tp1=0,add0=0;
    for(int k=0;k<9;k++){tp0+=stock.torque_primary[k];tp1+=up.torque_primary[k];add0+=rec_f(rec,0x530,4*k);}
    printf("  %-6s gears %d->%d  shift %.0f->%.0f max %.0f->%.0f  torque %.3f->%.3f (+%.1f%%)  "
           "mu %.2f/%.2f->%.2f/%.2f  mass %.3f->%.3f\n",name,stock.gear_count,up.gear_count,
           stock.shift_rpm,up.shift_rpm,stock.max_rpm,up.max_rpm,tp0,tp1,100*add0/tp0,
           stock.mu_peak[0],stock.mu_slide[0],up.mu_peak[0],up.mu_slide[0],stock.mass,up.mass);
    assert(up.mass==stock.mass);   /* weight upgrade stays unapplied */
    assert(up.gear_count>=stock.gear_count);
    free(rec);
}

/* ---- weight, turbo, ECU, brakes, suspension and nitrous on the real records ----------------- */
static int near_f(float a,float b){return fabsf(a-b)<=1e-5f*(1.0f+fabsf(b));}
typedef struct { DrvCar d;ResCar r;BrkCar b;LatCar l;CtCar c;BodyCar y; } Set;
static int read_set(const char *name,const DrvUpgrade *up,Set *o){
    return stage1_car_read_upgraded(g_data,g_len,name,up,&o->d)&&stage2_car_read_upgraded(g_data,g_len,name,up,&o->r)&&
        stage3_car_read_upgraded(g_data,g_len,name,up,&o->b)&&stage4_car_read_upgraded(g_data,g_len,name,up,&o->l)&&
        stage5_car_read_upgraded(g_data,g_len,name,up,&o->c)&&stage5_body_car_read_upgraded(g_data,g_len,name,up,&o->y);
}
static int read_stock(const char *name,Set *o){
    return stage1_car_read(g_data,g_len,name,&o->d)&&stage2_car_read(g_data,g_len,name,&o->r)&&
        stage3_car_read(g_data,g_len,name,&o->b)&&stage4_car_read(g_data,g_len,name,&o->l)&&
        stage5_car_read(g_data,g_len,name,&o->c)&&stage5_body_car_read(g_data,g_len,name,&o->y);
}
/* Straight-line run on the drive adapter: 0-100 km/h seconds and top speed over 90 s. nos > 0 holds the nitrous button. */
static void straight_run(const Set *s,int nos,float *t100,float *top){
    Trace t;memset(&t,0,sizeof t);
    assert(configured_init(&t.drive,&s->d,&s->r,0)==0);
    t.drive.resistance_input.scale=1;
    assert(stage3_drive_brakes(&t.drive,&s->b,0,0)==0);
    *t100=-1;*top=0;
    for(int k=0;k<5400;k++){
        if(nos)stage1_nos_update(&t.drive,1,1/PHYS_TICKRATE);
        step(&t,1,0,15);
        float kmh=PHYS_KMH(t.s);
        if(*t100<0&&kmh>=100)*t100=(k+1)/PHYS_TICKRATE;
        if(kmh>*top)*top=kmh;
    }
}
static void retail_effects(long row,const char *name,const float rpm_add[3],int player){
    const unsigned char *rec=g_data+row;
    Set st,u,z;
    assert(read_stock(name,&st));
    /* NULL and all-zero upgrades: every reader memcmp-identical to the stock reader */
    DrvUpgrade zero={0};
    assert(stage1_car_read_upgraded(g_data,g_len,name,NULL,&u.d)&&!memcmp(&u.d,&st.d,sizeof st.d));
    assert(read_set(name,NULL,&u)&&!memcmp(&u,&st,sizeof u));
    memset(&u,0,sizeof u);memset(&z,0,sizeof z);
    assert(read_set(name,&zero,&z)&&!memcmp(&z.d,&st.d,sizeof st.d)&&!memcmp(&z.r,&st.r,sizeof st.r)&&!memcmp(&z.b,&st.b,sizeof st.b)&&
           !memcmp(&z.l,&st.l,sizeof st.l)&&!memcmp(&z.c,&st.c,sizeof st.c)&&!memcmp(&z.y,&st.y,sizeof st.y));
    assert(st.d.nos_capacity==0&&st.d.nos_torque==0);
    /* weight: mass x 0x450, SUPRA centre.x +0x454, yaw scale -> 0x458, inertia rebuilt from the new mass */
    DrvUpgrade up={0};up.weight=1;
    assert(read_set(name,&up,&u));
    float r450=rec_f(rec,0,0x450),r454=rec_f(rec,0,0x454),r458=rec_f(rec,0,0x458);
    assert(near_f(u.d.mass,st.d.mass*r450)&&near_f(u.c.mass,u.d.mass));
    if(player)assert(near_f(u.c.centre[0],st.c.centre[0]+r454));
    if(player)assert(near_f(u.c.yaw_scale,r458)&&near_f(u.y.yaw_scale,r458));
    else assert(u.c.yaw_scale==st.c.yaw_scale&&u.c.centre[0]==st.c.centre[0]&&u.d.mass==st.d.mass);   /* TAXI: no kit */
    for(int k=0;k<3;k++)assert(near_f(u.y.inertia[k],st.y.inertia[k]*r450));
    assert(memcmp(&u.b,&st.b,sizeof st.b)==0&&memcmp(&u.l,&st.l,sizeof st.l)==0);
    up.weight=.5f;assert(read_set(name,&up,&u));
    assert(near_f(u.d.mass,st.d.mass*(.5f*(r450-1)+1)));
    /* brakes: lerp to 0x6C0 / 0x6C4 / 0x6C8 */
    up=(DrvUpgrade){0};up.brakes=1;assert(read_set(name,&up,&u));
    assert(near_f(u.b.service,rec_f(rec,0,0x6c0))&&near_f(u.b.handbrake,rec_f(rec,0,0x6c4))&&near_f(u.b.rear_share,rec_f(rec,0,0x6c8)));
    up.brakes=.5f;assert(read_set(name,&up,&u));
    assert(near_f(u.b.service,.5f*(st.b.service+rec_f(rec,0,0x6c0)))&&near_f(u.b.handbrake,.5f*(st.b.handbrake+rec_f(rec,0,0x6c4))));
    assert(near_f(u.b.rear_share,.5f*(st.b.rear_share+rec_f(rec,0,0x6c8)))&&memcmp(&u.d,&st.d,sizeof st.d)==0);
    /* turbo: secondary torque lerps to 0x620 */
    up=(DrvUpgrade){0};up.turbo=1;assert(read_set(name,&up,&u));
    for(int k=0;k<9;k++){
        float top=rec_f(rec,0,0x620+4*k);
        if(top>=st.d.torque_secondary[k])assert(near_f(u.d.torque_secondary[k],top));
        else assert(u.d.torque_secondary[k]==st.d.torque_secondary[k]);
        assert(u.d.torque_primary[k]==st.d.torque_primary[k]);
    }
    up.turbo=.5f;assert(read_set(name,&up,&u));
    for(int k=0;k<9;k++){float top=rec_f(rec,0,0x620+4*k);if(top>=st.d.torque_secondary[k])assert(near_f(u.d.torque_secondary[k],.5f*(st.d.torque_secondary[k]+top)));}
    /* ECU: rpm block by level (+idle, shift, max), 0x5F0 torque, 0x614 engine brake, governor off */
    up=(DrvUpgrade){0};up.ecu=1;up.ecu_level=3;up.governor_off=1;assert(read_set(name,&up,&u));
    for(int k=0;k<9;k++)assert(near_f(u.d.torque_primary[k],st.d.torque_primary[k]+rec_f(rec,0,0x5f0+4*k)));
    for(int k=0;k<3;k++)assert(near_f(u.d.engine_brake[k],st.d.engine_brake[k]+rec_f(rec,0,0x614+4*k)));
    assert(near_f(u.d.shift_rpm,st.d.shift_rpm+rpm_add[0])&&near_f(u.d.max_rpm,st.d.max_rpm+rpm_add[0]));
    assert(near_f(u.d.idle_rpm,st.d.idle_rpm+rec_f(rec,0,0x5e0)));
    assert(u.r.limit_mph==0&&u.r.drag==st.r.drag&&u.r.front==st.r.front&&u.r.rear==st.r.rear);
    if(player)assert(st.r.limit_mph>0);
    for(int lv=1;lv<=2;lv++){
        up=(DrvUpgrade){0};up.ecu_level=lv;assert(stage1_car_read_upgraded(g_data,g_len,name,&up,&u.d));
        assert(near_f(u.d.shift_rpm,st.d.shift_rpm+rec_f(rec,0,0x560+0x40*(lv-1)+4)));
    }
    /* engine alone: no rpm change, engine brake += 0x554 */
    up=(DrvUpgrade){0};up.engine=1;assert(read_set(name,&up,&u));
    assert(u.d.shift_rpm==st.d.shift_rpm&&u.d.max_rpm==st.d.max_rpm&&u.d.idle_rpm==st.d.idle_rpm);
    for(int k=0;k<3;k++)assert(near_f(u.d.engine_brake[k],st.d.engine_brake[k]+rec_f(rec,0,0x554+4*k)));
    /* suspension: springs (c0), anti-roll (c1), steering gain (c2) each alone */
    up=(DrvUpgrade){0};up.suspension=1;assert(read_set(name,&up,&u));
    for(int a=0;a<2;a++){
        int o=0x6d0+0x20*a;
        assert(near_f(u.c.axle[a].stiffness,rec_f(rec,0,o+4))&&near_f(u.c.axle[a].progression,rec_f(rec,0,o)));
        assert(near_f(u.c.axle[a].upper,rec_f(rec,0,o+20))&&near_f(u.c.axle[a].lower,rec_f(rec,0,o+24)));
        assert(u.c.axle[a].antiroll==st.c.axle[a].antiroll);
    }
    assert(near_f(u.c.centre[2],st.c.centre[2]-rec_f(rec,0,0x710)));
    for(int i=0;i<4;i++){assert(near_f(u.c.mount[i][2],st.c.mount[i][2]+rec_f(rec,0,0x714+4*(i/2)))&&near_f(u.l.mount[i][2],u.c.mount[i][2]));}
    assert(u.l.gain==st.l.gain&&memcmp(&u.d,&st.d,sizeof st.d)==0);
    up=(DrvUpgrade){0};up.sway=1;assert(read_set(name,&up,&u));
    for(int a=0;a<2;a++){
        assert(near_f(u.c.axle[a].antiroll,rec_f(rec,0,0x6d0+0x20*a+16)));
        assert(u.c.axle[a].stiffness==st.c.axle[a].stiffness&&u.c.axle[a].upper==st.c.axle[a].upper);
    }
    assert(memcmp(u.c.mount,st.c.mount,sizeof st.c.mount)==0&&u.c.centre[2]==st.c.centre[2]&&u.l.gain==st.l.gain);
    up=(DrvUpgrade){0};up.steer=1;assert(read_set(name,&up,&u));
    assert(near_f(u.l.gain,rec_f(rec,0,0x6cc))&&memcmp(&u.c,&st.c,sizeof st.c)==0);
    /* nitrous: bottle lerps 0x3B0 -> 0x690 (6 s), torque 0x69C set, not scaled */
    up=(DrvUpgrade){0};up.nitrous=.333f;assert(read_set(name,&up,&u));
    assert(fabsf(u.d.nos_capacity-1.998f)<1e-3f&&near_f(u.d.nos_torque,.5f));
    up.nitrous=1.0f;assert(read_set(name,&up,&u));
    assert(near_f(u.d.nos_capacity,6.0f)&&near_f(u.d.nos_torque,.5f));
    assert(memcmp(u.d.torque_primary,st.d.torque_primary,sizeof st.d.torque_primary)==0);
    /* retail level rounding */
    assert(drv_upgrade_level(.2f)==1&&drv_upgrade_level(.5f)==2&&drv_upgrade_level(.84f)==3&&drv_upgrade_level(0)==0&&drv_upgrade_level(2)==3);
}
static void retail_table(const char *name){
    Set st,fu;assert(read_stock(name,&st));
    DrvUpgrade full={.engine=1,.ecu_level=3,.gear_level=3,.tires=1,.suspension=1,.weight=1,.turbo=1,.brakes=1,.ecu=1,.nitrous=1,.sway=1,.steer=1,.governor_off=1};
    assert(read_set(name,&full,&fu));
    float t0,v0,t1,v1,t2,v2;
    straight_run(&st,0,&t0,&v0);straight_run(&fu,0,&t1,&v1);straight_run(&fu,1,&t2,&v2);
    printf("  TABLE %-6s mass %.3f->%.3f  0-100 %.2f s->%.2f s (NOS %.2f s)  top %.0f->%.0f km/h  brake %.3f->%.3f  steer gain %.3f->%.3f  nos %.1f s\n",
        name,st.d.mass,fu.d.mass,t0,t1,t2,v0,v1,st.b.service,fu.b.service,st.l.gain,fu.l.gain,fu.d.nos_capacity);
    assert(t0>0&&t1>0&&t1<=t0+.01f&&v1>=v0);
}
static void retail_test(void){
    const char *root= getenv("OPENUG2_DATA");
    if(!root||!root[0])root= getenv("OPENUG2_TEST_DATA");
    if(!root||!root[0]){puts("stage1 retail: no data root, skipped");return;}
    char path[1024];
    int n=snprintf(path,sizeof path,"%s/GLOBAL/GLOBALB.BUN",root);
    if(n<=0||n>=(int)sizeof path){puts("stage1 retail: path too long, skipped");return;}
    FILE *f=fopen(path,"rb");
    if(!f){printf("stage1 retail: %s absent, skipped\n",path);return;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc((size_t)size);assert(b);
    if(fread(b,1,(size_t)size,f)!=(size_t)size){free(b);fclose(f);puts("stage1 retail: short read, skipped");return;}
    fclose(f);g_data=b;g_len=size;
    static const char *cars[]={"GOLF","240SX","SUPRA","350Z","TAXI"};
    puts("stage1 retail: fully upgraded vs stock (real records)");
    for(unsigned i=0;i<sizeof cars/sizeof *cars;i++){
        long row=n2_global_car_record(b,size,cars[i]);
        assert(row>=0);
        retail_car(row,cars[i]);
    }
    {   /* ECU rpm block L3 (idle, shift, max): +1500 / +2000 / +1000 / +1000 on the four player cars */
        static const float rpm_add[5][3]={{1500,0,0},{2000,0,0},{1000,0,0},{1000,0,0},{0,0,0}};
        for(int i=0;i<5;i++){
            long row=n2_global_car_record(b,size,cars[i]);
            retail_effects(row,cars[i],rpm_add[i],i<4);
        }
        puts("stage1 retail: weight/turbo/ECU/brakes/suspension/nitrous effects passed");
        for(int i=0;i<4;i++)retail_table(cars[i]);
    }
    /* Traffic cars carry one gearbox template at every level (five ratios, final drive 3.00, against
     * their stock block) and a rear tyre whose max block sits *below* stock (slide 1.60 -> 1.50):
     * the replacement must still happen and the guard must refuse the below-stock value. Their engine
     * add-curve and rpm headers are all zero, so those two stay stock by themselves. Offsets are
     * printed rather than hard-coded here; the per-car numbers belong to the player cars above. */
    DrvCar s,u;DrvUpgrade full={.engine=1.0f,.ecu_level=3,.gear_level=3,.tires=1.0f,.suspension=1.0f};
    assert(stage1_car_read(b,size,"TAXI",&s));
    assert(stage1_car_read_upgraded(b,size,"TAXI",&full,&u));
    printf("  %-6s final %.4f->%.4f  ratio3 %.4f->%.4f  mu %.2f/%.2f->%.2f/%.2f\n","TAXI",
           s.final_front,u.final_front,s.ratio[3],u.ratio[3],
           s.mu_slide[1],u.mu_slide[1],s.mu_peak[0],u.mu_peak[0]);
    assert(fabsf(u.final_front-s.final_front)>1e-6f||fabsf(u.ratio[3]-s.ratio[3])>1e-6f);
    assert(fabsf(u.mu_slide[1]-s.mu_slide[1])<1e-6f);   /* not lerped down to the 1.50 below stock */
    assert(fabsf(u.mu_peak[0]-s.mu_peak[0])<1e-6f&&fabsf(u.mu_slide[0]-s.mu_slide[0])<1e-6f);
    assert(u.shift_rpm==s.shift_rpm&&u.max_rpm==s.max_rpm&&u.mass==s.mass);
    assert(s.mu_peak[0]>=1.9f&&s.mu_slide[0]>=1.5f);   /* template grip, per record */
    free(b);g_data=0;g_len=0;
}
int main(int argc,char **argv){
    parser_test();numeric_boundaries();tests();resistance_integration();braking_integration();phys_selftest();
    if(argc>1&&argv[1]&&argv[1][0])setenv("OPENUG2_TEST_DATA",argv[1],1);
    retail_test();
    puts("Stage 1 gameplay adapter tests passed");return 0;
}
