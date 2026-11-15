#include "fe_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_write(void *u,const char *n,const char *v){(void)u;(void)n;(void)v;return ST_E_IO;}
static int no_string(void *u,const char *n,char *b,size_t c){(void)u;(void)n;(void)b;(void)c;return 0;}
static const StRegOps fail_ops={.get_string=no_string,.set_string=fail_write};
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    StRegStore store={0};assert(st_reg_open_memory(&store)==ST_OK);
    FeProfile *p=fep_create(argv[1],&store);assert(p && p->valid && !p->persisted);
    assert(fep_new(p,"Alpha")==ST_OK);p->value.raw[40000]=0x5a;p->value.audio.master=.35f;
    assert(fep_save(p)==ST_OK);assert(fep_new(p,"Alpha")==ST_E_ARG);
    assert(fep_rename(p,"Beta")==ST_OK);assert(st_profile_status(argv[1],"Alpha")==ST_E_NOTFOUND);
    assert(fep_new(p,"Gamma")==ST_OK);assert(fep_load(p,"Beta")==ST_OK);
    assert(p->value.raw[40000]==0x5a && p->value.audio.master==.35f);
    assert(fep_rename(p,"Gamma")==ST_E_ARG);assert(!strcmp(p->value.name,"Beta"));
    assert(fep_delete(p,"Beta")==ST_E_ARG);assert(fep_delete(p,"Gamma")==ST_OK);
    fep_destroy(p);p=fep_create(argv[1],&store);assert(p && p->valid && p->persisted && !strcmp(p->value.name,"Beta"));
    assert(fep_load(p,"Missing")==ST_E_NOTFOUND && !strcmp(p->value.name,"Beta"));
    assert(fep_new(p,"Other")==ST_OK);assert(fep_load(p,"Beta")==ST_OK);
    StRegStore failure={&fail_ops,NULL};p->store=&failure;
    assert(fep_load(p,"Other")==ST_E_IO && !strcmp(p->value.name,"Beta"));
    assert(fep_rename(p,"Delta")==ST_E_IO);assert(!strcmp(p->value.name,"Beta"));
    assert(st_profile_status(argv[1],"Beta")==ST_OK && st_profile_status(argv[1],"Delta")==ST_E_NOTFOUND);
    assert(fep_new(p,"Delta")==ST_E_IO);assert(!strcmp(p->value.name,"Beta"));
    assert(st_profile_status(argv[1],"Delta")==ST_E_NOTFOUND);p->store=&store;
    assert(fep_delete(p,"Other")==ST_OK);
    assert(fep_rename(p,"../bad")==ST_E_ARG);fep_destroy(p);st_reg_close(&store);
    puts("fe_profile_test: create/save/rename/collision/load/delete/reopen/opaque bytes PASS");return 0;
}
