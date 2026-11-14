#ifndef OPENUG2_GAME_CATALOG_H
#define OPENUG2_GAME_CATALOG_H
#include <stdint.h>
/* Retail event/region/car catalogue built from the installed data (local only). */
#define GC_MAX_EVENTS 128
typedef struct GcEvent { int id; char region[8]; int kind,circuit,downhill,length_m; uint32_t name_key; } GcEvent;
typedef struct GcCar { char name[32]; int traffic; } GcCar;
typedef struct GcRegion { char track[16],stem[8]; int nevents; } GcRegion;
typedef struct GameCatalog { GcEvent ev[GC_MAX_EVENTS]; int nev; GcCar car[64]; int ncar; GcRegion region[8]; int nregion; } GameCatalog;
/* Returns 1 on success (at least one event), 0 on failure. Events sorted by (kind,id). */
int gcat_build(GameCatalog *c,const char *dataroot);
#endif
