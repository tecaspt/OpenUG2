/* Stage5 contact composition: dynamic BR body or explicit prescribed-pose
 * fixture. Both paths apply each wheel contribution once. */
#include "drive_stage1.h"
#include <math.h>
#include <string.h>
int stage5_car_read(const unsigned char*data,long length,const char*name,CtCar*out){return stage5_car_read_upgraded(data,length,name,NULL,out);}
/* Weight (mass, centre.x, yaw scale; inertia is rebuilt from the mass) and suspension (axle rates, ride height,
 * wheel-z raise) per docs/parity/PERF-PAINT.md. Zero fractions leave every byte of the stock record read. */
static float cl01(float u){return u>0?(u>1?1:u):0;}
static float lp(float v,float t,float x){return (t-v)*x+v;}
int stage5_car_read_upgraded(const unsigned char*data,long length,const char*name,const DrvUpgrade*up,CtCar*out){
 if(!data||!name||!out)return 0;
 long row=n2_global_car_record(data,length,name);if(row<0||row>length-0x890)return 0;
 CtCar c={0};
#define READ(f,o) memcpy(&(f),data+row+(o),sizeof(f))
 READ(c.centre,0x110);READ(c.mass,0x220);READ(c.dimensions,0x224);READ(c.yaw_scale,0x270);READ(c.aero_preload,0x37c);READ(c.bonus,0x390);
 for(int i=0;i<4;i++){READ(c.mount[i],0x120+0x30*i);READ(c.radius[i],0x130+0x30*i);}
 for(int i=0;i<2;i++){int o=0x280+0x20*i;READ(c.axle[i].progression,o);READ(c.axle[i].stiffness,o+4);READ(c.axle[i].d_positive,o+8);READ(c.axle[i].d_nonpositive,o+12);READ(c.axle[i].antiroll,o+16);READ(c.axle[i].upper,o+20);READ(c.axle[i].lower,o+24);READ(c.axle[i].reference,o+28);}
#undef READ
 if(up&&up->weight>0){
  float r[3];memcpy(r,data+row+0x450,sizeof r);float u=cl01(up->weight);
  if(!(r[0]<1.0f))u=0;  /* no weight kit on this record (traffic): no effect */
  c.mass=drv_upgrade_mass(c.mass,r[0],u);
  if(isfinite(r[1]))c.centre[0]+=r[1]*u;
  if(isfinite(r[2])&&r[2]>0)c.yaw_scale=lp(c.yaw_scale,r[2],u);
 }
 if(up&&(up->suspension>0||up->sway>0)){
  float c0=cl01(up->suspension),c1=cl01(up->sway),z[3],raise[2];
  memcpy(z,data+row+0x710,sizeof z);memcpy(raise,data+row+0x714,sizeof raise);
  for(int i=0;i<2;i++){
   CtAxle t;int o=0x6d0+0x20*i;
   memcpy(&t.progression,data+row+o,4);memcpy(&t.stiffness,data+row+o+4,4);memcpy(&t.d_positive,data+row+o+8,4);
   memcpy(&t.d_nonpositive,data+row+o+12,4);memcpy(&t.antiroll,data+row+o+16,4);memcpy(&t.upper,data+row+o+20,4);
   memcpy(&t.lower,data+row+o+24,4);memcpy(&t.reference,data+row+o+28,4);
   CtAxle*a=&c.axle[i];
   if(c0>0){a->progression=lp(a->progression,t.progression,c0);a->stiffness=lp(a->stiffness,t.stiffness,c0);
    a->d_positive=lp(a->d_positive,t.d_positive,c0);a->d_nonpositive=lp(a->d_nonpositive,t.d_nonpositive,c0);
    a->upper=lp(a->upper,t.upper,c0);a->lower=lp(a->lower,t.lower,c0);a->reference=lp(a->reference,t.reference,c0);}
   if(c1>0)a->antiroll=lp(a->antiroll,t.antiroll,c1);
  }
  if(c0>0){
   if(isfinite(z[0]))c.centre[2]-=z[0]*c0;
   for(int i=0;i<4;i++)if(isfinite(raise[i/2]))c.mount[i][2]+=raise[i/2]*c0;
  }
 }
 /* Independent loader validation is stricter than the finite component domain. */
 const float *values=(const float*)&c;for(unsigned i=0;i<sizeof c/sizeof(float);i++)if(!isfinite(values[i]))return 0;
 if(c.mass<=0||c.yaw_scale<=0||c.mount[0][0]<=c.mount[3][0])return 0;
 for(int k=0;k<3;k++)if(c.dimensions[k]<=0)return 0;
 for(int k=0;k<4;k++)if(c.radius[k]<=0)return 0;
 for(int k=0;k<2;k++)if(c.axle[k].upper<=c.axle[k].lower)return 0;
 *out=c;return 1;
}
int stage5_drive_contacts(Stage1Drive*s,const CtCar*c,CtQuery query,void*user){if(!s||!s->ready||!s->lateral_ready||!s->drivetrain.brakes.enabled||!c||!query)return -1;ct_init(&s->contacts,c);s->contact_query=query;s->contact_user=user;s->contact_ready=1;s->prepared=0;s->drivetrain.brakes.signed_distribution=1;return 0;}
int stage5_body_car_read(const unsigned char *data,long length,const char *name,BodyCar *out){return stage5_body_car_read_upgraded(data,length,name,NULL,out);}
int stage5_body_car_read_upgraded(const unsigned char *data,long length,const char *name,const DrvUpgrade *up,BodyCar *out){
 CtCar car;BodyCar c={0};float adjusted[3];if(!out||!stage5_car_read_upgraded(data,length,name,up,&car))return 0;
 long row=n2_global_car_record(data,length,name);ct_inertia(&car,c.inertia,adjusted);c.yaw_scale=car.yaw_scale;
 memcpy(c.limit,data+row+0x398,sizeof c.limit);for(int k=0;k<2;k++)if(!isfinite(c.limit[k])||c.limit[k]<0)return 0;*out=c;return 1;
}
int stage5_drive_body(Stage1Drive*s,const BodyCar*c,unsigned selector,int session_status,int suppress){
 if(!s||!s->contact_ready||!c||!isfinite(c->yaw_scale)||c->yaw_scale<=0)return -1;
 for(int k=0;k<3;k++)if(!isfinite(c->inertia[k])||c->inertia[k]<=0)return -1;
 for(int k=0;k<2;k++)if(!isfinite(c->limit[k])||c->limit[k]<0)return -1;
 s->body_car=*c;s->dynamic_body=1;s->dynamic_initialized=0;s->prepared=0;s->inertia_selector=selector;s->body_race=session_status;s->session=(DriveSession){DRIVE_SESSION_SUPPLIED,session_status};s->impulses_suppressed=suppress;s->body_gravity=9.8f;
 s->motion=(BodyMotion){.axes={{1,0,0},{0,1,0},{0,0,1}},.adjusted=1};return 0;
}
static void dynamic_pose(Stage1Drive*s){CtPose*p=&s->contact_pose;p->axes=s->motion.axes;memcpy(p->position,s->motion.position,sizeof p->position);memcpy(p->velocity,s->body.velocity,sizeof p->velocity);memcpy(p->omega,s->motion.omega,sizeof p->omega);s->yaw_rate=s->motion.omega[2];s->yaw_momentum=s->motion.angular_momentum[2];}
static float host_bottom(const Stage1Drive*s){float bottom=0;for(int i=0;i<4;i++)bottom+=(s->contacts.car.mount[i][2]-s->contacts.car.radius[i])*.25f;return bottom;}
static void dynamic_origin(const Stage1Drive*s,float origin[3]){const float*c=s->contacts.car.centre;const ResBasis*b=&s->motion.axes;for(int k=0;k<3;k++)origin[k]=(float)(s->motion.position[k]-(double)b->forward[k]*c[0]-(double)b->lateral[k]*c[1]-(double)b->up[k]*c[2]);}
static void dynamic_host_origin(const Stage1Drive*s,float origin[3]){dynamic_origin(s,origin);float bottom=host_bottom(s);for(int k=0;k<3;k++)origin[k]+=s->motion.axes.up[k]*bottom;}
void stage5_body_host_correction(Stage1Drive*s,const float origin[3],const float velocity[2]){
 if(!s||!s->dynamic_initialized)return;
 float old[3];dynamic_host_origin(s,old);for(int k=0;k<3;k++)s->motion.position[k]+=origin[k]-old[k];for(int k=0;k<2;k++){s->body.velocity[k]=velocity[k]*PHYS_TICKRATE;s->body.momentum[k]=s->body.mass*s->body.velocity[k];}dynamic_pose(s);
}
static void pose(Stage1Drive*s,const float pos[3],float heading,const PhysRideState*ride,float elapsed){
 if(s->dynamic_body){dynamic_pose(s);return;}
 CtPose*p=&s->contact_pose;float pitch=ride?ride->pitch+elapsed*ride->pitch_rate:0,roll=ride?ride->roll+elapsed*ride->roll_rate:0,ch=cosf(heading),sh=sinf(heading),cp=cosf(pitch),sp=sinf(pitch),cr=cosf(roll),sr=sinf(roll);
 p->axes=(ResBasis){{ch*cp,sh*cp,sp},{-sh*cr-ch*sp*sr,ch*cr-sh*sp*sr,cp*sr},{sh*sr-ch*sp*cr,-ch*sr-sh*sp*cr,cp*cr}};
 float bottom=0;for(int i=0;i<4;i++)bottom+=(s->contacts.car.mount[i][2]-s->contacts.car.radius[i])*.25f;
 for(int k=0;k<3;k++){p->position[k]=pos[k];p->velocity[k]=s->body.velocity[k];p->omega[k]=(ride?ride->roll_rate:0)*p->axes.forward[k];}
 p->position[2]+=s->contacts.car.centre[2]-bottom+(ride?elapsed*ride->vz:0);
 p->velocity[2]=ride?ride->vz:0;
 p->omega[0]+=(ride?ride->pitch_rate:0)*sh;p->omega[1]-=(ride?ride->pitch_rate:0)*ch;p->omega[2]+=s->yaw_rate;
}
static void geometry(Stage1Drive*s,float h){
 unsigned upper=ct_post(&s->contacts,&s->contact_pose,s->wheel_angle,h,s->contact_query,s->contact_user);
 /* The prescribed-pose fixture suppresses bottoming; the dynamic path uses
  * BR-6 caps and its explicit suppression input. Both retain the zero sweep. */
 for(int i=0;i<4;i++) {
  CtWheel*w=s->contacts.wheel+i;CtPose*p=&s->contact_pose;
  float velocity[3]={p->velocity[0]+p->omega[1]*w->arm[2]-p->omega[2]*w->arm[1],p->velocity[1]+p->omega[2]*w->arm[0]-p->omega[0]*w->arm[2],p->velocity[2]+p->omega[0]*w->arm[1]-p->omega[1]*w->arm[0]};
  s->contacts.bottom_request[i]=(upper&(1u<<i))?ct_bottom(-(float)res_dot(w->normal,velocity),s->contacts.car.mass):0;
  if(s->dynamic_body&&!s->impulses_suppressed&&(upper&(1u<<i))){float impulse[3];for(int k=0;k<3;k++)impulse[k]=s->contacts.bottom_request[i]*w->normal[k];body_impulse(&s->motion,&s->body,impulse,w->arm,1);}
 }
 if(upper) {
  /* Repeat every established post callback, without derivatives/Euler. Even
   * clutch mode transitions can be observable in a zero-duration sweep. */
  if(s->dynamic_body){body_post(&s->motion,&s->body,&s->body_car,s->body_gravity);dynamic_pose(s);}else res_post(&s->body,1);
  drv_post(&s->drivetrain);
  ct_post(&s->contacts,&s->contact_pose,s->wheel_angle,0,s->contact_query,s->contact_user);
 }
}
/* Host chassis contact. The wheels are otherwise the body's only ground
 * support, and they give none once the car is tilted past its travel, so a
 * rolled car fell through the road. Hull: wheel footprint from hub height (zero
 * travel) to the roof. A tyre carrying load keeps its hub radius-upper above
 * the road, so the hull floor cannot touch flat ground while the wheels drive.
 * Inelastic normal impulse plus positional push-out; no friction.
 * ponytail: 8 hull corners, no edge/face test; a crest between corners is missed. */
static void chassis_ground(Stage1Drive*s){
 const CtCar*car=&s->contacts.car;BodyMotion*m=&s->motion;ResBody*b=&s->body;
 float roof=host_bottom(s)+car->dimensions[2],push=0,normal[3]={0,0,1};
 float inertia[3]={s->body_car.inertia[0],s->body_car.inertia[1],s->body_car.inertia[2]*(m->adjusted?s->body_car.yaw_scale:1)};
 for(int i=0;i<8;i++){
  const float*mount=car->mount[i&3];float local[3]={mount[0]-car->centre[0],mount[1]-car->centre[1],(i<4?mount[2]:roof)-car->centre[2]},arm[3],q[3];
  for(int k=0;k<3;k++){arm[k]=m->axes.forward[k]*local[0]+m->axes.lateral[k]*local[1]+m->axes.up[k]*local[2];q[k]=m->position[k]+arm[k];}
  q[2]+=car->radius[i&3]; /* the query subtracts the wheel radius from the probe */
  CtGround g=s->contact_query(s->contact_user,q,i&3);
  float depth=(g.height+g.height_offset-(q[2]-car->radius[i&3]))*g.normal[2];
  if(!g.found||depth<=0)continue;
  if(depth>push){push=depth;memcpy(normal,g.normal,sizeof normal);}
  float v[3]={b->velocity[0]+m->omega[1]*arm[2]-m->omega[2]*arm[1],b->velocity[1]+m->omega[2]*arm[0]-m->omega[0]*arm[2],b->velocity[2]+m->omega[0]*arm[1]-m->omega[1]*arm[0]};
  float closing=-(float)res_dot(v,g.normal);if(closing<=0)continue;
  /* Effective mass at the corner: 1/m + sum over body axes of (r x n)^2/I. */
  float r[3]={(float)res_dot(arm,m->axes.forward),(float)res_dot(arm,m->axes.lateral),(float)res_dot(arm,m->axes.up)};
  float n[3]={(float)res_dot(g.normal,m->axes.forward),(float)res_dot(g.normal,m->axes.lateral),(float)res_dot(g.normal,m->axes.up)};
  float rn[3]={r[1]*n[2]-r[2]*n[1],r[2]*n[0]-r[0]*n[2],r[0]*n[1]-r[1]*n[0]},inverse=1/b->mass;
  for(int k=0;k<3;k++)inverse+=rn[k]*rn[k]/inertia[k];
  float impulse[3];for(int k=0;k<3;k++)impulse[k]=g.normal[k]*closing/inverse;
  body_impulse(m,b,impulse,arm,1);res_post(b,0);body_publish_angular(m,&s->body_car);
 }
 for(int k=0;k<3;k++)m->position[k]+=normal[k]*push;
 dynamic_pose(s);
}
static void sample(Stage1Drive*s,DrvEnv*env){
 CtPose*p=&s->contact_pose;CtState*c=&s->contacts;env->body_speed=(float)sqrt(res_dot(p->velocity,p->velocity));env->signed_forward=(float)res_dot(p->velocity,p->axes.forward);env->planar_tyres=1;env->supplied_frame_grip=1;env->frame_grip[0]=c->grip_front;env->frame_grip[1]=c->grip_rear;
 for(int i=0;i<4;i++){CtWheel*w=c->wheel+i;DrvWheelEnv*e=env->wheel+i;ResBasis axes;ct_wheel_axes(&p->axes,s->wheel_angle[i],&axes);float velocity[3]={p->velocity[0]+p->omega[1]*w->arm[2]-p->omega[2]*w->arm[1],p->velocity[1]+p->omega[2]*w->arm[0]-p->omega[0]*w->arm[2],p->velocity[2]+p->omega[0]*w->arm[1]-p->omega[1]*w->arm[0]};e->vx=(float)res_dot(velocity,axes.forward);e->vy=(float)res_dot(velocity,axes.lateral);e->slip_angle=lat_slip(e->vx,e->vy);e->contact=w->contact;e->load=w->load;const CtSurface*sf=ct_surface(w->surface);e->mu_peak_surface=sf->peak;e->mu_slide_surface=sf->slide;e->roll_drag=sf->spin;e->locked=s->drivetrain.brakes.locked[i];e->drag_flag=(s->damage_mask>>i)&1u;e->lateral_grip_force=lat_grip(e->load,s->lateral_car.angle[i/2],s->lateral_car.reference[i/2],s->lateral_car.factor[i/2],e->slip_angle,e->vx,1);}
}
static void prepare(Stage1Drive*s,DrvEnv*env){
 ResBody*b=&s->body;CtPose*p=&s->contact_pose;Drivetrain*d=&s->drivetrain;
 res_prepare_aero(b,&s->resistance_car,&s->resistance_input,&p->axes,&s->aero);
 /* BC-1/2: snapshot the candidate-basis linear accumulator before the passes.
  * Normal wheel forces and prepared angular moments enter only later. */
 DrvBodyPlanar coupling={.mass=b->mass,.inv_iz=s->dynamic_body?body_inverse_yaw(&s->motion,&s->body_car):s->inverse_yaw_inertia,.ex=(float)res_dot(b->force,p->axes.forward),.ey=(float)res_dot(b->force,p->axes.lateral)};
 for(int i=0;i<4;i++){coupling.pos[i][0]=s->contacts.mount[i][0];coupling.pos[i][1]=s->contacts.mount[i][1];float a=s->wheel_angle[i];coupling.head[i][0]=cosf(a);coupling.head[i][1]=sinf(a);coupling.lat[i][0]=-sinf(a);coupling.lat[i][1]=cosf(a);}
 drv_couple(d,env,drv_body_accel_planar,&coupling);
 memset(s->contact_force,0,sizeof s->contact_force);memset(s->contact_moment,0,sizeof s->contact_moment);
 for(int i=0;i<4;i++){ResBasis axes;ct_wheel_axes(&p->axes,s->wheel_angle[i],&axes);ct_apply(p,s->contacts.wheel+i,&axes,1,d->force_long[i],d->force_lat[i],s->contact_force[i],s->contact_moment[i]);for(int k=0;k<3;k++){b->force[k]+=s->contact_force[i][k];b->moment[k]+=s->contact_moment[i][k];}for(int k=0;k<2;k++)s->prepared_wheel[i][k]=s->contact_force[i][k];s->prepared_moment[i]=s->contact_moment[i][2];}
 if(s->dynamic_body)body_opposing(b,&s->motion,s->state_code,s->inertia_selector);
 s->prepared=1;
}
float stage5_drive_step(Stage1Drive*s,float pos[3],float vel[2],float*heading,float*speed,float pedal,float steer,int hb,const PhysRideState*ride){
 /* OM-2a: no preparation, derivative, publication or integration while suspended. */
 if(s->session.phase==DRIVE_SESSION_SUSPENDED)return 0;
 const float dt=1/PHYS_TICKRATE;Drivetrain*d=&s->drivetrain;CtState*c=&s->contacts;ResBody*b=&s->body;ResInput*r=&s->resistance_input;DrvEnv env;drv_env_default(&env);s->wheel_travel=0;memset(s->wheel_angular_travel,0,sizeof s->wheel_angular_travel);
 if(s->dynamic_body){
  if(!s->dynamic_initialized){
   /* Independent start policy: host ground-origin plus zero-travel wheel
    * clearance, level heading, supplied horizontal velocity, zero rotation. */
   s->motion.axes=res_basis_level(*heading);float bottom=0;for(int i=0;i<4;i++)bottom+=(c->car.mount[i][2]-c->car.radius[i])*.25f;
   for(int k=0;k<3;k++)s->motion.position[k]=pos[k]+s->motion.axes.forward[k]*c->car.centre[0]+s->motion.axes.lateral[k]*c->car.centre[1]+s->motion.axes.up[k]*(c->car.centre[2]-bottom);
   float v[3]={vel[0]*PHYS_TICKRATE,vel[1]*PHYS_TICKRATE,0},w[3]={0};body_initialize(&s->motion,b,&s->body_car,v,w);s->dynamic_initialized=1;
  }
  dynamic_pose(s);
 }else{for(int k=0;k<2;k++){b->velocity[k]=vel[k]*PHYS_TICKRATE;b->momentum[k]=b->mass*b->velocity[k];}b->velocity[2]=ride?ride->vz:0;b->momentum[2]=b->mass*b->velocity[2];pose(s,pos,*heading,ride,0);}
 /* Creation prepares with zero pedals, before first frame coefficient refresh. */
 if(!s->prepared){ct_frame(c,&s->resistance_car,r->enable,1,NULL);if(s->dynamic_body){body_post(&s->motion,b,&s->body_car,s->body_gravity);dynamic_pose(s);}else res_post(b,1);geometry(s,0);sample(s,&env);prepare(s,&env);ct_export(c,&s->contact_pose);}
 ct_frame(c,&s->resistance_car,r->enable,1,NULL);
 for(int i=0;i<4;i++)s->body_surface[i]=ct_surface(c->exported_surface[i])->body_drag;
 res_frame_inputs(r,s->state_code,s->alternate_movement,s->disable_stabilize,c->exported_mask,s->body_surface);
 d->tune.torque_scale=r->scale;d->tune.torque_bonus=r->bonus;
 float forward=c->exported_forward;if(pedal<0&&forward<=.3f)s->reverse=1;if(pedal>0&&forward>=-.3f)s->reverse=0;
 /* Independent keyboard policy (U3-1/U5-4): oppose actual motion with the
  * service brake, and hold the accelerator until both actual and pending
  * gears agree with the requested direction. The pending target matters when
  * an interrupted handoff changes gear during this tick. Keep this as a raw
  * frame input: shaping, selector/clutch progress and all frame consumers
  * retain their usual cadence, with no shaft/wheel/lock/force resets. */
 int braking=(pedal<0&&forward>.3f)||(pedal>0&&forward<-.3f);
 int opposite=s->reverse?(d->gear>DRV_GEAR_N||d->target_gear>DRV_GEAR_N):
     (d->gear==DRV_GEAR_R||d->target_gear==DRV_GEAR_R);
 DrvInput input={.throttle=braking||opposite?0:fabsf(pedal),.brake=braking?fabsf(pedal):0,.handbrake=hb?1:0,.gear_cmd=s->reverse?DRV_GEAR_R:DRV_CMD_AUTO};
 /* Distribution reads the exported frame snapshot. Shaping/exclusivity occurs
  * once inside drv_frame_begin, before hb/grip, and never in each substep. */
 if(s->dynamic_body){
  float throttle=input.throttle<=.15f?0:input.throttle;if(throttle>0&&throttle<.5f&&d->gear>DRV_GEAR_N&&d->omega_e*DRV_KR<3000)throttle=.5f;
  body_select(&s->motion,&s->body_car,body_selection(s->state_code,s->inertia_selector));
  /* Resolve only after creation/restoration and preceding preparation. */
  drive_session_status(s->session,s->dynamic_initialized&&s->prepared,&s->body_race);
  s->body_mode=body_mode(s->state_code,s->body_race,throttle,r->contacts,c->exported_heading[2]);r->stabilize=s->body_mode.stabilize;
  float slip=lat_body_slip(c->exported_forward,c->exported_heading,c->exported_velocity)*.01745329238474369f;
  s->body_mode.torque_limit=body_torque_limit(d->tune.is_player,s->body_car.limit,c->exported_forward,slip,s->motion.angular_momentum[2]);
  if(s->state_code==0||s->state_code==2)s->body_gravity=9.8f;
 }
 d->env.body_speed=c->exported_speed;d->env.signed_forward=c->exported_forward;drv_frame_begin(d,&input,dt,-1);r->throttle=d->throttle;
 c->blend=ct_blend(d->handbrake,c->blend,dt);c->blend_refreshes++;
 float sigma=lat_body_slip(c->exported_forward,c->exported_heading,c->exported_velocity)*.01745329238474369f;
 float eta=ct_eta(d->handbrake,d->tune.is_player,c->exported_forward,sigma,ct_mph(c->exported_forward));float etas[4]={0,0,eta,eta};brk_handbrake(&d->brakes.car,etas,d->brakes.handbrake);
 CtGrip grip=ct_grip(d->tune.is_player,eta,sigma,c->exported_speed,fabsf(c->exported_heading[2]),c->blend,c->car.bonus[0],c->car.bonus[1],s->contact_event,d->tune.grip_q,d->tune.grip_loc_x,s->contact_v0,s->contact_s0);c->grip_front=grip.front;c->grip_rear=grip.rear;
 stage1_steering_frame(s,steer,&s->contact_pose.axes);
 float h;int steps=drv_substep_count(dt,&h);for(int step=0;step<steps;step++){
  if(s->dynamic_body){
   BodyDerivative derivative;body_collect(&s->motion,b,&s->body_car,&s->body_mode,&s->resistance_car,r,&derivative);
   if(s->body_observe)s->body_observe(s->body_observe_user,s,h,0);
   for(int i=0;i<4;i++){float angle=h*d->wheel[i].omega;s->wheel_angular_travel[i]+=angle;s->wheel_travel+=angle*.25f;}
   body_integrate(&s->motion,b,&derivative,h);drv_integrate(d,h);body_post(&s->motion,b,&s->body_car,s->body_gravity);dynamic_pose(s);
   chassis_ground(s);geometry(s,h);sample(s,&env);prepare(s,&env);
   if(s->body_observe)s->body_observe(s->body_observe_user,s,h,1);
   body_repair(&s->motion.axes);dynamic_pose(s);continue;
  }
  ResDerivative derivative;res_derivative(b,&s->resistance_car,r,&s->contact_pose.axes,fabsf(s->yaw_rate),&derivative);
  float oldrate=s->yaw_rate;s->yaw_momentum=(float)(s->yaw_momentum+(double)h*(b->moment[2]-s->yaw_damping*s->yaw_momentum));s->yaw_rate=s->yaw_momentum*s->inverse_yaw_inertia;*heading=(float)(*heading+(double)h*oldrate);
  /* Position derivatives use pre-Euler velocity. Vertical pose is prescribed,
   * with full vertical force retained for diagnostics/external 3D consumers. */
  for(int k=0;k<2;k++)pos[k]=(float)(pos[k]+(double)h*b->velocity[k]);
  for(int i=0;i<4;i++){float angle=h*d->wheel[i].omega;s->wheel_angular_travel[i]+=angle;s->wheel_travel+=angle*.25f;}
  res_integrate(b,&derivative,h);drv_integrate(d,h);res_post(b,1);
  b->velocity[2]=ride?ride->vz:0;b->momentum[2]=b->mass*b->velocity[2];pose(s,pos,*heading,ride,(step+1)*h);geometry(s,h);sample(s,&env);prepare(s,&env);
 }
 if(s->dynamic_body){dynamic_host_origin(s,pos);*heading=atan2f(s->motion.axes.forward[1],s->motion.axes.forward[0]);}
 ct_export(c,&s->contact_pose);s->exported_forward=c->exported_forward;for(int k=0;k<2;k++)vel[k]=b->velocity[k]/PHYS_TICKRATE;*speed=c->exported_forward/PHYS_TICKRATE;return fabsf(vel[0]*sinf(*heading)-vel[1]*cosf(*heading));
}
