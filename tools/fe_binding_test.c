/* Synthetic identity vectors: no retail payloads. */
#include "fe_binding.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    UgFngObject objects[2]={{0}};
    UgFngPackage p={0}; p.obj=objects; p.nobj=2;
    FeIdentity id={19,1,9,2,3};
    objects[0].name_hash=19;objects[0].guid=1;objects[0].parent_guid=9;
    objects[0].type=2;objects[0].res_index=3;
    objects[1]=objects[0];objects[1].guid=2;
    assert(fe_identity_find(&p,&id)==0); /* duplicate names are legal */
    objects[1].guid=1;assert(fe_identity_find(&p,&id)==-1);
    p.nobj=1;
    objects[0].parent_guid=8;assert(fe_identity_find(&p,&id)==-1);
    objects[0].parent_guid=9;objects[0].res_index=4;assert(fe_identity_find(&p,&id)==-1);
    objects[0].res_index=3;objects[0].type=1;assert(fe_identity_find(&p,&id)==-1);
    objects[0].type=2;objects[0].name_hash=20;assert(fe_identity_find(&p,&id)==-1);
    objects[0].name_hash=19;objects[0].guid=2;assert(fe_identity_find(&p,&id)==-1);
    p.nobj=0;assert(fe_identity_find(&p,&id)==-1);
    puts("fe_binding_test: PASS (8 synthetic identity checks)");return 0;
}
