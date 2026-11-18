#include <math.h>
#include <string.h>
#include "drive_stage1.h"

static int valid_car(const DrvCar *c) {
    if(!c || !isfinite(c->mass) || c->mass<=0 ||
       !isfinite(c->engine_inertia) || c->engine_inertia<=0 ||
       !isfinite(c->idle_rpm) || c->idle_rpm<=0 ||
       !isfinite(c->shift_rpm) || c->shift_rpm<=c->idle_rpm ||
       !isfinite(c->max_rpm) || c->max_rpm<c->shift_rpm ||
       !isfinite(c->rear_share) || c->rear_share<0 || c->rear_share>1 ||
       !isfinite(c->clutch_k) || c->clutch_k<0 ||
       !isfinite(c->clutch_rate) || c->clutch_rate<=0 ||
       !isfinite(c->launch_fraction) ||
       !isfinite(c->final_front) || c->final_front<=0 ||
       !isfinite(c->final_rear) || c->final_rear<=0 ||
       c->gear_count<1 || c->gear_count>6 ||
       !isfinite(c->ratio[0]) || c->ratio[0]>=0 || c->ratio[1]!=0)return 0;
    for(int k=0;k<4;k++)if(!isfinite(c->wheel_radius[k]) || c->wheel_radius[k]<=0)return 0;
    for(int k=2;k<c->gear_count+2;k++)if(!isfinite(c->ratio[k]) || c->ratio[k]<=0)return 0;
    for(int k=0;k<2;k++)if(!isfinite(c->theta_max[k]) || c->theta_max[k]<0 ||
       !isfinite(c->mu_peak[k]) || c->mu_peak[k]<0 || !isfinite(c->mu_slide[k]) || c->mu_slide[k]<0)return 0;
    for(int k=0;k<9;k++)if(!isfinite(c->torque_primary[k]) || c->torque_primary[k]<0 ||
       !isfinite(c->torque_secondary[k]) || c->torque_secondary[k]<0)return 0;
    for(int k=0;k<3;k++)if(!isfinite(c->engine_brake[k]) || c->engine_brake[k]<0)return 0;
    return 1;
}

int stage1_car_read(const unsigned char *data,long length,const char *name,DrvCar *out) {
    return stage1_car_read_upgraded(data,length,name,NULL,out);
}

/* Career performance parts (docs/parity/PERF-PAINT.md). All blocks are in the same 0x890 record [data]:
 * gearbox L1..L3 replace the stock 0x2C0..0x2FF block with 0x460/0x4A0/0x4E0 (same layout); the engine master
 * add-curve 0x530 (9 samples, primary-torque units; L1..L3 blocks are 0.34/0.68/1.0 of it) is added to the primary
 * torque scaled by the engine fraction, and the level block header (+4 shift, +8 max, floats) raises the rpm;
 * tyre grip lerps from the stock axle blocks 0x1E0/0x200 to the per-car maximum 0x650/0x670. */
int stage1_car_read_upgraded(const unsigned char *data,long length,const char *name,const DrvUpgrade *up,DrvCar *out) {
    if(!data || !name || !out)return 0;
    long row=n2_global_car_record(data,length,name);
    if(row<0 || row>length-0x890)return 0;
    DrvCar c={0};
#define READ(field,offset) memcpy(&(field),data+row+(offset),sizeof(field))
    READ(c.mass,0x220);
    for(int i=0;i<4;i++){
        int w=0x120+0x30*i;
        if(n2_u32(data+row+w+0x18)!=(unsigned)i)return 0;
        READ(c.wheel_radius[i],w+0x10);
    }
    float tire=up?up->tires:0.0f;
    if(!(tire>=0.0f))tire=0.0f;
    if(tire>1.0f)tire=1.0f;
    for(int a=0;a<2;a++){
        int w=0x1e0+0x20*a,m=0x650+0x20*a;
        float pk,sl;
        READ(c.theta_max[a],w);READ(c.mu_peak[a],w+0xc);READ(c.mu_slide[a],w+0x10);
        READ(pk,m+0xc);READ(sl,m+0x10);
        if(tire>0.0f && isfinite(pk) && isfinite(sl) && pk>=c.mu_peak[a] && sl>=c.mu_slide[a] && pk<5.0f && sl<5.0f){
            c.mu_peak[a]+=tire*(pk-c.mu_peak[a]);c.mu_slide[a]+=tire*(sl-c.mu_slide[a]);
        }
    }
    int gl=up?up->gear_level:0, gb=(gl>=1&&gl<=3)?0x460+0x40*(gl-1):0x2c0;
    READ(c.launch_fraction,gb);READ(c.final_front,gb+0x8);READ(c.final_rear,gb+0xc);
    READ(c.rear_share,gb+0x10);READ(c.clutch_k,gb+0x14);
    c.gear_count=(int)n2_u32(data+row+gb+0x18);READ(c.clutch_rate,gb+0x1c);
    READ(c.ratio,gb+0x20);READ(c.idle_rpm,0x300);READ(c.shift_rpm,0x304);
    READ(c.max_rpm,0x308);READ(c.engine_inertia,0x30c);
    READ(c.torque_primary,0x310);READ(c.torque_secondary,0x334);READ(c.engine_brake,0x358);
    if(up && up->engine>0.0f){
        float e=up->engine>1.0f?1.0f:up->engine;
        for(int i=0;i<DRV_TORQUE_SAMPLES;i++){
            float add;READ(add,0x530+4*i);
            if(isfinite(add) && add>=0.0f && add<5.0f)c.torque_primary[i]+=e*add;
        }
    }
    if(up && up->engine_level>=1 && up->engine_level<=3){
        float rs,rm;int hb=0x560+0x40*(up->engine_level-1);
        READ(rs,hb+4);READ(rm,hb+8);
        if(isfinite(rs) && isfinite(rm) && rs>=0.0f && rm>=0.0f && rs<=3000.0f && rm<=3000.0f){c.shift_rpm+=rs;c.max_rpm+=rm;}
    }
    /* Suspension (category 5) carries no field here on purpose: see the DrvUpgrade note in the header. */
#undef READ
    if(!valid_car(&c) || drv_car_finalize(&c))return 0;
    *out=c;return 1;
}

int stage2_car_read(const unsigned char *data,long length,const char *name,ResCar *out) {
    if(!data || !name || !out)return 0;
    long row=n2_global_car_record(data,length,name);
    if(row<0 || row>length-0x890)return 0;
    ResCar c;
    memcpy(&c.drag,data+row+0x370,4);memcpy(&c.front,data+row+0x374,4);
    memcpy(&c.rear,data+row+0x378,4);memcpy(&c.limit_mph,data+row+0x3a0,4);
    if(!isfinite(c.drag)||c.drag<0||!isfinite(c.front)||c.front<0||
       !isfinite(c.rear)||c.rear<0||!isfinite(c.limit_mph)||c.limit_mph<0)return 0;
    *out=c;return 1;
}

int stage3_car_read(const unsigned char *data,long length,const char *name,BrkCar *out) {
    if(!data || !name || !out)return 0;
    long row=n2_global_car_record(data,length,name);
    if(row<0 || row>length-0x890)return 0;
    BrkCar car;
    memcpy(&car.service,data+row+0x384,4);memcpy(&car.handbrake,data+row+0x388,4);
    memcpy(&car.rear_share,data+row+0x38c,4);
    if(!isfinite(car.service)||car.service<0||!isfinite(car.handbrake)||car.handbrake<0||
       !isfinite(car.rear_share)||car.rear_share<0||car.rear_share>1)return 0;
    *out=car;return 1;
}
static int valid_lateral(const LatCar *c) {
    if(!c||!isfinite(c->gain)||c->gain<=0)return 0;
    for(int i=0;i<4;i++)for(int k=0;k<3;k++)if(!isfinite(c->mount[i][k]))return 0;
    if(c->mount[0][0]<=c->mount[3][0]||c->mount[0][1]<=c->mount[1][1])return 0;
    for(int k=0;k<2;k++)if(!isfinite(c->angle[k])||c->angle[k]<=0||
        !isfinite(c->reference[k])||c->reference[k]<=0||!isfinite(c->factor[k])||c->factor[k]<0)return 0;
    for(int k=0;k<3;k++)if(!isfinite(c->dimensions[k])||c->dimensions[k]<=0)return 0;
    return 1;
}
int stage4_car_read(const unsigned char *data,long length,const char *name,LatCar *out) {
    if(!data||!name||!out)return 0;
    long row=n2_global_car_record(data,length,name);if(row<0||row>length-0x890)return 0;
    LatCar c={0};
    for(int i=0;i<4;i++)memcpy(c.mount[i],data+row+0x120+0x30*i,12);
    for(int a=0;a<2;a++){
        memcpy(&c.angle[a],data+row+0x1e0+0x20*a,4);
        memcpy(&c.reference[a],data+row+0x1e4+0x20*a,4);
        memcpy(&c.factor[a],data+row+0x1e8+0x20*a,4);
    }
    memcpy(c.dimensions,data+row+0x224,12);memcpy(&c.gain,data+row+0x380,4);
    if(!valid_lateral(&c))return 0;
    *out=c;return 1;
}
int stage4_drive_lateral(Stage1Drive *s,const LatCar *car,float inverse,float damping) {
    if(!s||!s->ready||!valid_lateral(car)||!isfinite(inverse)||inverse<=0||!isfinite(damping)||damping<0)return -1;
    s->lateral_car=*car;s->lateral_ready=1;s->inverse_yaw_inertia=inverse;s->yaw_damping=damping;
    s->steering=(LatCommand){.previous_limit=45};s->yaw_rate=s->yaw_momentum=0;
    memset(s->wheel_angle,0,sizeof s->wheel_angle);memset(s->input_history,0,sizeof s->input_history);
    s->previous_mapped=0;s->input_cursor=s->rate_cursor=0;
    for(int k=0;k<9;k++)s->rate_history[k]=1;
    return 0;
}
int stage3_drive_brakes(Stage1Drive *s,const BrkCar *car,unsigned locks,int relief_disabled) {
    if(!s || !s->ready || drv_brakes_supply(&s->drivetrain,car,locks,relief_disabled,1))return -1;
    s->initial_lock_mask=locks;
    return 0;
}
int stage1_drive_init(Stage1Drive *s,const DrvCar *car,const ResCar *resistance,float reduction) {
    if(!s || !valid_car(car) || !resistance || !isfinite(reduction)||reduction<0||reduction>1)return -1;
    const float fields[]={resistance->drag,resistance->front,resistance->rear,resistance->limit_mph};
    for(unsigned k=0;k<sizeof fields/sizeof *fields;k++)if(!isfinite(fields[k])||fields[k]<0)return -1;
    Stage1Drive fresh={0};DrvTune tune;drv_tune_default(&tune);
    tune.is_player=1;
    if(drv_init(&fresh.drivetrain,car,&tune))return -1;
    fresh.resistance_car=*resistance;
    fresh.resistance_input=(ResInput){.scale=1,.reduction=reduction,.enable=1,.surface=1,.stabilize=1};
    for(int k=0;k<4;k++)fresh.body_surface[k]=fresh.rolling_surface[k]=1;
    fresh.body.mass=car->mass;res_post(&fresh.body,0);
    fresh.ready=1;*s=fresh;return 0;
}
void stage1_drive_reset(Stage1Drive *s,const float velocity[2],float heading) {
    if(!s || !s->ready)return;
    /* Keep supplied tuning/environment while resetting dynamic state. */
    Stage1Drive previous=*s;
    stage1_drive_init(s,&previous.drivetrain.car,&previous.resistance_car,previous.resistance_input.reduction);
    if(previous.drivetrain.brakes.enabled)
        stage3_drive_brakes(s,&previous.drivetrain.brakes.car,previous.initial_lock_mask,previous.drivetrain.brakes.relief_disabled);
    if(previous.lateral_ready)stage4_drive_lateral(s,&previous.lateral_car,previous.inverse_yaw_inertia,previous.yaw_damping);
    if(previous.contact_ready) {
        stage5_drive_contacts(s,&previous.contacts.car,previous.contact_query,previous.contact_user);
        s->contact_event=previous.contact_event;s->contact_v0=previous.contact_v0;s->contact_s0=previous.contact_s0;
    }
    if(previous.dynamic_body) {
        stage5_drive_body(s,&previous.body_car,previous.inertia_selector,previous.body_race,previous.impulses_suppressed);
        s->session=previous.session;s->body_gravity=previous.body_gravity;s->body_observe=previous.body_observe;s->body_observe_user=previous.body_observe_user;
    }
    s->resistance_input=previous.resistance_input;
    memcpy(s->body_surface,previous.body_surface,sizeof s->body_surface);
    memcpy(s->rolling_surface,previous.rolling_surface,sizeof s->rolling_surface);
    s->damage_mask=previous.damage_mask;s->state_code=previous.state_code;
    s->alternate_movement=previous.alternate_movement;s->disable_stabilize=previous.disable_stabilize;
    float v=(velocity[0]*cosf(heading)+velocity[1]*sinf(heading))*PHYS_TICKRATE;
    for(int k=0;k<4;k++) {
        s->drivetrain.wheel[k].omega=v/s->drivetrain.car.wheel_radius[k];
        /* Saved-state entry explicitly supplies direction from host velocity. */
        brk_direction_post(&s->drivetrain.wheel[k].omega,&s->drivetrain.wheel[k].dir);
    }
    for(int k=0;k<2;k++)s->body.momentum[k]=s->body.mass*velocity[k]*PHYS_TICKRATE;
    res_post(&s->body,0);
    s->exported_forward=v;
    if(s->contact_ready){s->contacts.exported_forward=v;s->contacts.exported_speed=fabsf(v);}
}
float stage1_drive_rpm(const Stage1Drive *s){return s->drivetrain.omega_e*DRV_KR;}
int stage1_drive_gear(const Stage1Drive *s){return s->drivetrain.gear-1;}

/* Planar adapter: the independent suspension supplies the body-up constraint.
 * Keep vertical aero load/moments as diagnostics; no invented load feedback. */
static void prepare(Stage1Drive *s,DrvEnv *env,const ResBasis *basis,int host_hold) {
    Drivetrain *d=&s->drivetrain;
    res_prepare_aero(&s->body,&s->resistance_car,&s->resistance_input,basis,&s->aero);
    s->body.force[2]=0;
    if(s->lateral_ready) {
        DrvBodyPlanar coupling={.mass=d->car.mass,.inv_iz=s->inverse_yaw_inertia,
            .ex=(float)res_dot(s->body.force,basis->forward),
            .ey=(float)res_dot(s->body.force,basis->lateral)};
        for(int i=0;i<4;i++) {
            memcpy(coupling.pos[i],s->lateral_car.mount[i],sizeof coupling.pos[i]);
            float a=s->wheel_angle[i];coupling.head[i][0]=cosf(a);coupling.head[i][1]=sinf(a);
            coupling.lat[i][0]=-sinf(a);coupling.lat[i][1]=cosf(a);
            /* drv_prepare_tyre applies wT once, after this unweighted curve. */
            env->wheel[i].lateral_grip_force=lat_grip(env->wheel[i].load,s->lateral_car.angle[i/2],
                s->lateral_car.reference[i/2],s->lateral_car.factor[i/2],
                env->wheel[i].slip_angle,env->wheel[i].vx,1);
        }
        drv_couple(d,env,drv_body_accel_planar,&coupling);
    } else {
        DrvBodyStraight coupling={.mass=d->car.mass,.ex=(float)res_dot(s->body.force,basis->forward)};
        drv_couple(d,env,drv_body_accel_straight,&coupling);
    }
    memset(s->prepared_wheel,0,sizeof s->prepared_wheel);memset(s->prepared_moment,0,sizeof s->prepared_moment);
    for(int i=0;i<4;i++)if(env->wheel[i].contact&&!host_hold) {
        float f[2]={d->force_long[i],0},m=0;
        if(s->lateral_ready) {
            f[0]=0;lat_accumulate(f,&m,s->lateral_car.mount[i],s->wheel_angle[i],d->force_long[i],d->force_lat[i]);
        }
        for(int k=0;k<2;k++) {
            s->prepared_wheel[i][k]=f[0]*basis->forward[k]+f[1]*basis->lateral[k];
            s->body.force[k]+=s->prepared_wheel[i][k];
        }
        s->prepared_moment[i]=m;s->body.moment[2]+=m;
    }
    s->prepared=1;
}
static void sample_contacts(Stage1Drive *s,DrvEnv *env,const ResBasis *basis) {
    float forward=(float)res_dot(s->body.velocity,basis->forward);
    env->body_speed=hypotf(s->body.velocity[0],s->body.velocity[1]);
    env->signed_forward=forward;env->planar_tyres=s->lateral_ready;
    float v[2]={forward,(float)res_dot(s->body.velocity,basis->lateral)};
    for(int i=0;i<4;i++) {
        if(s->lateral_ready){
            float u[2];lat_contact(v,s->yaw_rate,s->lateral_car.mount[i],s->wheel_angle[i],u,&env->wheel[i].vx,&env->wheel[i].vy);
            env->wheel[i].slip_angle=lat_slip(env->wheel[i].vx,env->wheel[i].vy);
        }else env->wheel[i].vx=forward;
    }
}
void stage1_steering_frame(Stage1Drive *s,float raw,const ResBasis *basis) {
    /* Host U4-1/2 choice: fixed 60 Hz box averages, zero input/one rate seeds.
     * These are explicit producers, not asserted original timed histories. */
    float history=0,rate=0,mapped=lat_curve("I",raw);
    for(int i=0;i<33;i++)history+=s->input_history[i]/33;
    float sample=lat_curve("E",fabsf((mapped-s->previous_mapped)*PHYS_TICKRATE));
    s->rate_history[s->rate_cursor]=sample;s->rate_cursor=(s->rate_cursor+1)%9;
    for(int i=0;i<9;i++)rate+=s->rate_history[i]/9;
    LatFilter in={.raw=raw,.vctl=s->exported_forward,.history=history,.rate_history=rate,
        .old_degrees=lat_decode(s->steering.command),.previous_limit=s->steering.previous_limit,
        .sigma=s->contact_ready?lat_body_slip(s->contacts.exported_forward,s->contacts.exported_heading,s->contacts.exported_velocity):lat_body_slip((float)res_dot(s->body.velocity,basis->forward),basis->forward,s->body.velocity),
        .latch=s->steering.latch,.speed=hypotf(s->body.velocity[0],s->body.velocity[1]),
        .throttle=s->drivetrain.throttle,.brake=s->drivetrain.brake,.handbrake=s->contact_ready?s->drivetrain.handbrake:0,
        .gain=s->lateral_car.gain,.dt=1/PHYS_TICKRATE};
    s->steering=lat_filter(&in);s->previous_mapped=mapped;
    s->input_history[s->input_cursor]=mapped;s->input_cursor=(s->input_cursor+1)%33;
    lat_geometry(s->steering.command,s->lateral_car.mount[0][0]-s->lateral_car.mount[3][0],
        s->lateral_car.mount[0][1]-s->lateral_car.mount[1][1],s->wheel_angle);
}
float stage1_drive_step(Stage1Drive *s,float pos[3],float vel[2],float *heading,
    float *speed,float pedal,float steer,int handbrake,const PhysSurface *sf,
    const PhysVehicle *vh,const PhysRideState *ride) {
    if(!s || !s->ready)return 0;
    if(s->contact_ready)return stage5_drive_step(s,pos,vel,heading,speed,pedal,steer,handbrake,ride);
    if(!sf)sf=&PHYS_SURF_ROAD;
    Drivetrain *d=&s->drivetrain;ResBody *b=&s->body;
    s->wheel_travel=0;memset(s->wheel_angular_travel,0,sizeof s->wheel_angular_travel);
    ResBasis basis=res_basis_level(*heading);
    for(int k=0;k<2;k++) { b->velocity[k]=vel[k]*PHYS_TICKRATE;b->momentum[k]=b->mass*b->velocity[k]; }
    float forward=(float)res_dot(b->velocity,basis.forward);
    /* Host keyboard policy, retained from the Stage-1 playable adapter. */
    if(pedal<0 && forward<=.3f)s->reverse=1;
    if(pedal>0 && forward>=-.3f)s->reverse=0;
    int braking=(pedal<0 && !s->reverse)||(pedal>0 && s->reverse);
    DrvInput input={.throttle=braking||handbrake?0:fabsf(pedal),
        .brake=braking?fabsf(pedal):0,.handbrake=handbrake?1:0,
        .gear_cmd=s->reverse?DRV_GEAR_R:DRV_CMD_AUTO};
    unsigned mask=ride?ride->contact_mask:0;
    ResInput *r=&s->resistance_input;
    res_frame_inputs(r,s->state_code,s->alternate_movement,s->disable_stabilize,mask,s->body_surface);
    /* One shared scale/bonus; this ordinary adapter has no exceptional engine multiplier. */
    d->tune.torque_scale=r->scale;d->tune.torque_bonus=r->bonus;
    DrvEnv env;drv_env_default(&env);env.rear_grip_multiplier=1;
    for(int k=0;k<4;k++){
        env.wheel[k].contact=(mask>>k)&1u;
        env.wheel[k].load=env.wheel[k].contact?d->car.mass*9.8f/4:0;
        env.wheel[k].mu_peak_surface=env.wheel[k].mu_slide_surface=sf->accel;
        env.wheel[k].locked=d->brakes.enabled?d->brakes.locked[k]:0;
        env.wheel[k].roll_drag=s->rolling_surface[k];
        env.wheel[k].drag_flag=(s->damage_mask>>k)&1u;
        env.wheel[k].vx=forward;
    }
    sample_contacts(s,&env,&basis);
    /* Finish creation with zero pedals before this frame changes coefficients.
     * Existing prepared forces survive subsequent frame input changes (S3-7). */
    if(!s->prepared)prepare(s,&env,&basis,handbrake);
    d->env=env;
    if(d->brakes.enabled)for(int i=0;i<4;i++)d->env.wheel[i].locked=d->brakes.locked[i];
    drv_frame_begin(d,&input,1/PHYS_TICKRATE,-1);r->throttle=d->throttle;
    if(s->lateral_ready)stage1_steering_frame(s,steer,&basis);
    float h;int n=drv_substep_count(1/PHYS_TICKRATE,&h);
    for(int k=0;k<n;k++){
        /* Remove previous tyre force when support is lost or Space hold engages.
         * Its prior prepared aero remains; there is no extra resistance pass. */
        for(int i=0;i<4;i++)if(!env.wheel[i].contact||handbrake) {
            for(int axis=0;axis<2;axis++) {
                b->force[axis]-=s->prepared_wheel[i][axis];
                s->prepared_wheel[i][axis]=0;
            }
            b->moment[2]-=s->prepared_moment[i];s->prepared_moment[i]=0;
        }
        ResDerivative derivative;res_derivative(b,&s->resistance_car,r,&basis,fabsf(s->yaw_rate),&derivative);
        /* Body derivative is collected before wheel/shaft integration. The
         * independent blocks share no state mutated by either Euler update. */
        for(int i=0;i<4;i++){float angle=h*d->wheel[i].omega;s->wheel_angular_travel[i]+=angle;s->wheel_travel+=angle*.25f;}
        if(s->lateral_ready) {
            float rate=s->yaw_rate;
            s->yaw_momentum=(float)(s->yaw_momentum+(double)h*(b->moment[2]-s->yaw_damping*s->yaw_momentum));
            s->yaw_rate=s->yaw_momentum*s->inverse_yaw_inertia;
            if(handbrake){rate=0;s->yaw_rate=s->yaw_momentum=0;}
            *heading=(float)(*heading+(double)h*rate);
        }
        res_integrate(b,&derivative,h);
        res_post(b,0);
        drv_integrate(d,h);
        basis=res_basis_level(*heading);
        forward=(float)res_dot(b->velocity,basis.forward);
        for(int i=0;i<4;i++){
            env.wheel[i].vx=forward;
            /* Space remains an explicit host hold, outside S3-8's deferred
             * nonzero handbrake producer. It does not assign service locks. */
            if(handbrake)d->wheel[i].omega=0;
        }
        sample_contacts(s,&env,&basis);
        prepare(s,&env,&basis,handbrake);
    }
    vel[0]=b->velocity[0]/PHYS_TICKRATE;vel[1]=b->velocity[1]/PHYS_TICKRATE;
    s->exported_forward=forward;
    if(s->lateral_ready) {
        if(mask)return phys_stage4_chassis_step(pos,vel,heading,speed,handbrake,sf,vh);
        pos[0]+=vel[0];pos[1]+=vel[1];*speed=forward/PHYS_TICKRATE;
        return fabsf(vel[0]*basis.forward[1]-vel[1]*basis.forward[0]);
    }
    if(mask)return phys_stage1_chassis_step(pos,vel,heading,speed,steer,handbrake,sf,vh);
    pos[0]+=vel[0];pos[1]+=vel[1];*speed=forward/PHYS_TICKRATE;
    return fabsf(vel[0]*basis.forward[1]-vel[1]*basis.forward[0]);
}
