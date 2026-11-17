/* Runtime-data straight-view calibration probe. No retail payload is embedded.
 * Usage: gps_marker_pose_check DATAROOT [ahead,height,pitch,scale]
 * The flat, steady Focus/FAR camera at173km/h is a calibration assumption. */
#define GPS_MARKER_NO_GPU
#include "gps_marker.h"
#include "camera.h"
static void multiply(const float a[16],const float b[16],float out[16]) {
 for(int c=0;c<4;c++)for(int r=0;r<4;r++){out[c*4+r]=0;for(int k=0;k<4;k++)out[c*4+r]+=a[k*4+r]*b[c*4+k];}
}
static void view_matrix(const CamPose *p,float m[16]) {
 float f[3],r[3],u[3],n=0;
 for(int a=0;a<3;a++){f[a]=p->look[a]-p->eye[a];n+=f[a]*f[a];}n=sqrtf(n);for(int a=0;a<3;a++)f[a]/=n;
 r[0]=f[1]*p->up[2]-f[2]*p->up[1];r[1]=f[2]*p->up[0]-f[0]*p->up[2];r[2]=f[0]*p->up[1]-f[1]*p->up[0];
 n=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);for(int a=0;a<3;a++)r[a]/=n;
 u[0]=r[1]*f[2]-r[2]*f[1];u[1]=r[2]*f[0]-r[0]*f[2];u[2]=r[0]*f[1]-r[1]*f[0];
 memset(m,0,16*sizeof *m);for(int a=0;a<3;a++){m[a*4]=r[a];m[a*4+1]=u[a];m[a*4+2]=-f[a];m[12]-=r[a]*p->eye[a];m[13]-=u[a]*p->eye[a];m[14]+=f[a]*p->eye[a];}m[15]=1;
}
static int project(const GpsMarkerGeom *g,const GpsMarkerPose *cfg,const float vp[16],float box[4]) {
 float car[3]={0,0,0},p[3],yaw,m[16];gps_marker_pose_configured(cfg,car,0,0,p,&yaw);gps_marker_model_configured(cfg,p,yaw,m);
 return gps_marker_project_bounds(g,m,vp,1280,720,box);
}
int main(int argc,char **argv) {
 if(argc<2){puts("SKIP: data root required");return 77;}
 char path[1024];snprintf(path,sizeof path,"%s/GLOBAL/GLOBALB.BUN",argv[1]);long len=0;
 unsigned char *data=n2_read_file(path,&len);CamCarData car;
 if(!data || !cam_car_data_load(data,len,"FOCUS",&car)){free(data);puts("FAIL Focus camera data unavailable");return 1;}free(data);
 CamState state;cam_init(&state);cam_set_car(&state,&car);cam_set_requested(&state,CAM_FAR);
 CamInput in={0};float up[3]={0,0,1};cam_car_axes(0,up,&in.car);in.car.pos[2]=.35f;in.vel[0]=in.speed=173/3.6f;in.wheels_down=4;
 cam_tick(&state,&in,1.f/60);CamPose camera;cam_pose(&state,&in.car,&camera);
 const CamEntry *entry=cam_car_entry(&car,0,CAM_FAR);
 printf("FOCUS FAR: fwd=%.6f up=%.6f lat=%.6f pitch_bau=%d fov_bau=%d ride_assumed=0.35 speed=173km/h\n",entry->fwd,entry->up,entry->lat,entry->pitch_bau,camera.fov_bau);
 printf("eye=%.6f %.6f %.6f look=%.6f %.6f %.6f\n",camera.eye[0],camera.eye[1],camera.eye[2],camera.look[0],camera.look[1],camera.look[2]);
 float v[16],p[16],vp[16];view_matrix(&camera,v);cam_projection(CAM_PROJ_DEFAULT,camera.fov_bau,1280,720,1,.1f,1500,p);multiply(p,v,vp);
 printf("projection xs=%.6f ys=%.6f FOV=%.6f,%.6f degrees policy=wide\n",p[0],p[5],2*atanf(1/p[0])*57.2957795131f,2*atanf(1/p[5])*57.2957795131f);
 GpsMarkerGeom g;if(!gps_marker_geom_load(argv[1],&g,NULL,NULL)){puts("FAIL marker unavailable");return 1;}
 if(gps_marker_core_range(&g)<0){gps_marker_geom_free(&g);puts("FAIL unsupported marker materials");return 1;}
 for(int i=0;i<g.scene.count;i++) {
  const N2Mesh *mesh=g.scene.meshes+i;double c[4]={0};int lo[4]={255,255,255,255},hi[4]={0};
  for(int j=0;j<mesh->nidx;j++)for(int a=0;a<4;a++) {int value=mesh->vcol[mesh->idx[j]*4+a];c[a]+=value;if(value<lo[a])lo[a]=value;if(value>hi[a])hi[a]=value;}
  printf("range%d tris=%d RGBAmean=%.2f,%.2f,%.2f,%.2f alpha=%d..%d\n",i,mesh->nidx/3,c[0]/mesh->nidx,c[1]/mesh->nidx,c[2]/mesh->nidx,c[3]/mesh->nidx,lo[3],hi[3]);
 }
 GpsMarkerPose best=gps_marker_defaults();float box[4],err=1e30f;
 if(argc>2){if(!gps_marker_parse(argv[2],&best)){gps_marker_geom_free(&g);return 2;}}
 else {
  /* [inferred] Fix ahead30m: monocular size does not identify metric distance.
     Fit height/scale/tilt to the owner-observed straight-image bounds. */
  for(int j=0;j<=340;j++) {
   GpsMarkerPose q={30,12,-85+j*.25f,2};
   for(int loop=0;loop<6;loop++) {
    float lo=-20,hi=50;
    for(int n=0;n<25;n++){q.height_m=(lo+hi)/2;if(!project(&g,&q,vp,box))break;if((box[1]+box[3])*.5f>200)lo=q.height_m;else hi=q.height_m;}
    if(!project(&g,&q,vp,box))break;
    q.scale*=115/(box[2]-box[0]);
   }
   if(project(&g,&q,vp,box)){float e=fabsf(box[3]-box[1]-80);if(e<err){err=e;best=q;}}
  }
 }
 if(!project(&g,&best,vp,box)){gps_marker_geom_free(&g);return 1;}
 printf("POSE %.6f,%.6f,%.6f,%.6f\nBOUNDS %.3f %.3f %.3f %.3f centre %.3f %.3f size %.3f %.3f\n",best.ahead_m,best.height_m,best.pitch_deg,best.scale,box[0],box[1],box[2],box[3],(box[0]+box[2])*.5f,(box[1]+box[3])*.5f,box[2]-box[0],box[3]-box[1]);
 gps_marker_geom_free(&g);puts("PASS");return 0;
}
