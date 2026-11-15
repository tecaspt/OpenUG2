/* [inferred] Transactional, host-acknowledged customization model. No renderer. */
#ifndef OPENUG2_GARAGE_H
#define OPENUG2_GARAGE_H
#include "car_config.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GARAGE_PREVIEW_SECONDS .3f
#define GARAGE_KIT N2_PART_COUNT
#define GARAGE_SLOTS (N2_PART_COUNT+1)
typedef struct GarageCatalog { char car[32];N2PartMenu menu[GARAGE_SLOTS]; } GarageCatalog;
typedef struct GarageConfig { N2CarConfig car;char rim_brand[24];int rim_style; } GarageConfig;
enum { GAR_PREVIEW=1,GAR_KEEP,GAR_RESTORE };
typedef struct GarageRequest { uint32_t serial;int action;GarageConfig config; } GarageRequest;
typedef struct Garage {
 const GarageCatalog *catalog;
 GarageConfig applied,pending,baseline,displayed;
 GarageRequest flight;uint32_t serial;
 int slot,editing,action,inflight,event;float quiet;
 char status[160];
} Garage;
int garage_config_valid(const GarageConfig *);
void garage_init(Garage *,const GarageCatalog *,const GarageConfig *);
int garage_begin(Garage *,int slot);
int garage_select(Garage *,int option);
void garage_finish(Garage *,int keep);
void garage_update(Garage *,float dt);
int garage_take(Garage *,GarageRequest *);
void garage_ack(Garage *,uint32_t serial,int success,const char *error);
#ifdef __cplusplus
}
#endif
#endif
