#ifndef OPENUG2_FE_GARAGE_H
#define OPENUG2_FE_GARAGE_H
#include "garage.h"
#include "gui_view.h"
typedef struct FeGarage FeGarage;
enum { FEG_HUB,FEG_SLOTS,FEG_PARTS,FEG_BRANDS,FEG_RIMS };
FeGarage *feg_create(const char *root);
void feg_destroy(FeGarage *);
/* Optional prevalidated host cache; also permits asset-free navigation tests. */
int feg_use_catalog(FeGarage *,const GarageCatalog *,const GarageConfig *);
int feg_use_rims(FeGarage *,const GarageRims *);
int feg_current(FeGarage *,const char *car,const GarageConfig *);
int feg_open(FeGarage *,GvScreen *,UgAssets *);
/* 0 unchanged, 1 rebuild package, 2 return to Main. Inputs use FES_IN_* values. */
int feg_input(FeGarage *,int input);
int feg_advance(FeGarage *,float seconds);
int feg_sync(FeGarage *,GvScreen *,float ms);
int feg_take(FeGarage *,GarageRequest *);
void feg_ack(FeGarage *,uint32_t serial,int success,const char *error);
const char *feg_status(const FeGarage *);
int feg_page(const FeGarage *);
#endif
