/* Synthetic vectors only; retail checks are a separate runtime helper. */
#include "fe_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(!fet_name_is_real(NULL));assert(!fet_name_is_real(""));
    assert(!fet_name_is_real(" 4202 "));assert(!fet_name_is_real("123"));
    assert(fet_name_is_real("Synthetic Avenue"));assert(fet_name_is_real("Route 12"));
    unsigned char a[48]={0},b[48]={0};
    assert(fet_same_layout(a,sizeof a,b,sizeof b));b[47]=1;
    assert(!fet_same_layout(a,sizeof a,b,sizeof b));b[47]=0;
    assert(!fet_same_layout(a,24,b,48));assert(!fet_same_layout(a,23,b,23));
    assert(!fet_same_layout(a,0,b,0));assert(!fet_same_layout(NULL,24,b,24));
    uint8_t rgba[]={1,2,3,0,4,5,6,0};
    assert(fet_map_opaque(rgba,2));assert(rgba[0]==1 && rgba[7]==255);
    rgba[7]=0;assert(!fet_map_opaque(rgba,2) && rgba[7]==0);
    assert(!fet_map_opaque(NULL,0));
    puts("fe_polish_test: 16 checks PASS");return 0;
}
