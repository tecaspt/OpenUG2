#ifndef OPENUG2_FE_PROFILE_H
#define OPENUG2_FE_PROFILE_H
#include "settings.h"
#define FEP_MAX_PROFILES 32
typedef struct FeProfile {
    StProfile value;
    StRegStore *store;
    char root[1024],names[FEP_MAX_PROFILES][ST_NAME_MAX+1];
    int count,current,valid,persisted,last_error;
    char status[160];
} FeProfile;
FeProfile *fep_create(const char *root,StRegStore *store);
void fep_destroy(FeProfile *);
int fep_refresh(FeProfile *);
int fep_new(FeProfile *,const char *);
int fep_rename(FeProfile *,const char *);
int fep_load(FeProfile *,const char *);
int fep_delete(FeProfile *,const char *);
int fep_save(FeProfile *);
/* Lazily create the active profile; reload one created since this object opened. */
int fep_persist(FeProfile *);
#endif
