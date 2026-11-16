#ifndef OPENUG2_FE_WORLDMAP_H
#define OPENUG2_FE_WORLDMAP_H
#include <stdint.h>
#include "ugui/ugui_canvas.h"
#define FWM_MAX_ICONS 288
#define FWM_ICON_EVENT 1
#define FWM_ICON_SHOP 2
#define FWM_IN_NEXT 7
#define FWM_IN_PREVIOUS 8
#define FWM_EV_NONE 0
#define FWM_EV_DESTINATION 1
#define FWM_EV_LEAVE 2
typedef struct FeMapIcon { float x,y; int kind,variant; uint32_t id,label_key; } FeMapIcon;
typedef struct FeMapDestination { float x,y; int kind; uint32_t id; } FeMapDestination;
typedef struct FeMapProjection { double origin_x,origin_y,extent; float cx,cy,a,b,c,d; } FeMapProjection;
struct FeWorldMapView; struct GvScreen; struct UgAssets;
typedef struct FeWorldMap { FeMapIcon icon[FWM_MAX_ICONS]; int n,selected; float player_x,player_y,heading; FeMapProjection projection; struct FeWorldMapView *view; char status[220]; } FeWorldMap;
void fwm_open(FeWorldMap *,const FeMapIcon *,int,float,float,float);
int fwm_project(const FeMapProjection *,double,double,float *,float *);
int fwm_unproject(const FeMapProjection *,float,float,double *,double *);
/* Arrival check for the explicitly selected four-corner zone, independent of HUD/speed. */
int fwm_zone_contains(const float polygon[4][2],float x,float y);
int fwm_hit(const FeWorldMap *,float,float,float);
int fwm_select(FeWorldMap *,int);
/* Repeated taps cycle distinct identities sharing the closest map position. */
int fwm_touch_select(FeWorldMap *,float,float,float);
int fwm_input(FeWorldMap *,int,FeMapDestination *);
int fwm_bind(FeWorldMap *,struct GvScreen *,struct UgAssets *);
int fwm_sync(FeWorldMap *,struct GvScreen *);
int fwm_draw(FeWorldMap *,struct GvScreen *,UgCanvas *);
void fwm_destroy(FeWorldMap *);
#endif
