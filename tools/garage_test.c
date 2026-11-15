#include "garage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 GarageCatalog cat={0};cat.menu[GARAGE_KIT].count=2;cat.menu[GARAGE_KIT].options[1].value=2;
 GarageConfig c={0};Garage g;GarageRequest r;garage_init(&g,&cat,&c);
 assert(!garage_begin(&g,-1));assert(garage_begin(&g,GARAGE_KIT));assert(!garage_select(&g,2));
 assert(garage_select(&g,1));garage_update(&g,.29f);assert(!garage_take(&g,&r));garage_update(&g,.02f);assert(garage_take(&g,&r));
 assert(r.config.car.body_kit==2&&!g.applied.car.body_kit);garage_ack(&g,r.serial+1,1,NULL);assert(g.inflight);
 garage_finish(&g,0);garage_ack(&g,r.serial,1,NULL);assert(garage_take(&g,&r)&&r.action==GAR_RESTORE&&!r.config.car.body_kit);
 garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&!g.applied.car.body_kit);
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,0,"failure");assert(g.editing&&!g.applied.car.body_kit);
 assert(garage_select(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&g.applied.car.body_kit==2);
 /* A final request cannot be retargeted before its acknowledgement. */
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,0));garage_finish(&g,1);assert(garage_take(&g,&r));
 assert(!garage_select(&g,1));garage_finish(&g,0);assert(!g.action);garage_ack(&g,r.serial,1,NULL);assert(!g.applied.car.body_kit);
 /* Preview coalescing preserves the latest choice while an old request completes. */
 assert(garage_begin(&g,GARAGE_KIT));assert(garage_select(&g,1));garage_update(&g,1);assert(garage_take(&g,&r));
 assert(garage_select(&g,0));garage_ack(&g,r.serial,1,NULL);garage_update(&g,1);assert(garage_take(&g,&r)&&!r.config.car.body_kit);
 garage_ack(&g,r.serial,1,NULL);garage_finish(&g,0);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);
 GarageRims rims={0};rims.count=2;strcpy(rims.options[0].brand,"ALPHA");rims.options[0].style=1;
 strcpy(rims.options[1].brand,"BETA");rims.options[1].style=2;
 assert(garage_begin_rims(&g,&rims));assert(!garage_select_rim(&g,2));assert(garage_select_rim(&g,0));
 garage_update(&g,.29f);assert(!garage_take(&g,&r));garage_update(&g,.02f);assert(garage_take(&g,&r));
 assert(!strcmp(r.config.rim_brand,"ALPHA")&&r.config.rim_style==1);garage_ack(&g,r.serial,1,NULL);
 assert(garage_select_rim(&g,1));garage_finish(&g,0);assert(garage_take(&g,&r)&&r.action==GAR_RESTORE&&!r.config.rim_brand[0]);
 garage_ack(&g,r.serial,1,NULL);assert(!g.editing&&!g.applied.rim_brand[0]);
 assert(garage_begin_rims(&g,&rims));assert(garage_select_rim(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));
 garage_ack(&g,r.serial,0,"rim failure");assert(g.editing&&!g.applied.rim_brand[0]);
 assert(garage_select_rim(&g,1));garage_finish(&g,1);assert(garage_take(&g,&r));garage_ack(&g,r.serial,1,NULL);
 assert(!g.editing&&!strcmp(g.applied.rim_brand,"BETA")&&g.applied.rim_style==2);
 puts("garage_test: PASS (selection/debounce/ack/stale serial/restore/keep/failure)");return 0;
}
