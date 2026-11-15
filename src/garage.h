/* [inferred] Transactional, host-acknowledged customization model. No renderer. */
#ifndef OPENUG2_GARAGE_H
#define OPENUG2_GARAGE_H
#include "car_config.h"
#include <stdint.h>
#include <stddef.h>
#include "settings.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GARAGE_FILE_NAME "openug2_garage.bin"
#define GARAGE_SAVE_LIMIT 1048576u
#define GARAGE_PREVIEW_SECONDS .3f
#define GARAGE_KIT N2_PART_COUNT
#define GARAGE_RIM (N2_PART_COUNT+1)
#define GARAGE_MAX_RIMS 512
typedef struct GarageRimOption { char brand[24];int style; } GarageRimOption;
typedef struct GarageRims { int count;GarageRimOption options[GARAGE_MAX_RIMS]; } GarageRims;
#define GARAGE_SLOTS (N2_PART_COUNT+1)
typedef struct GarageCatalog { char car[32];N2PartMenu menu[GARAGE_SLOTS]; } GarageCatalog;
typedef struct GarageConfig { N2CarConfig car;char rim_brand[24];int rim_style; } GarageConfig;
/* ST_* results. Reads validate the complete document; failure leaves outputs unchanged.
 * Put preserves unknown bytes and allocates *out (caller frees); NULL/0 creates a document. */
int garage_store_get(const uint8_t *bytes,size_t len,const char *car,GarageConfig *out);
int garage_store_put(const uint8_t *bytes,size_t len,const char *car,const GarageConfig *cfg,uint8_t **out,size_t *outlen);
/* File writes require an existing profile and atomically replace only the Garage file.
 * Only actual absence returns ST_E_NOTFOUND; corrupt/newer/IO never silently create a save. */
int garage_load(const char *root,const char *profile,const char *car,GarageConfig *out);
int garage_save(const char *root,const char *profile,const char *car,const GarageConfig *cfg);
enum { GAR_PREVIEW=1,GAR_KEEP,GAR_RESTORE };
typedef struct GarageRequest { uint32_t serial;int action;GarageConfig config; } GarageRequest;
typedef struct Garage {
 const GarageCatalog *catalog;const GarageRims *rims;
 GarageConfig applied,pending,baseline,displayed;
 GarageRequest flight;uint32_t serial;
 int slot,editing,action,inflight,event;float quiet;
 char status[160];
} Garage;
int garage_config_valid(const GarageConfig *);
void garage_init(Garage *,const GarageCatalog *,const GarageConfig *);
int garage_begin(Garage *,int slot);
int garage_select(Garage *,int option);
int garage_begin_rims(Garage *,const GarageRims *);
int garage_select_rim(Garage *,int option);
void garage_finish(Garage *,int keep);
void garage_update(Garage *,float dt);
int garage_take(Garage *,GarageRequest *);
void garage_ack(Garage *,uint32_t serial,int success,const char *error);
#ifdef __cplusplus
}
#endif
#endif
