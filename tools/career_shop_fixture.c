/* Data-only fixture: a stage-1 career parked inside a named open shop.
 * The caller supplies an isolated, existing profile root. */
#include "career.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    static CareerData cd; static CareerState st; static StProfile profile;
    if(argc!=5){fprintf(stderr,"usage: career_shop_fixture DATA PROFILE_ROOT PROFILE SHOP\n");return 2;}
    if(!cd_load(&cd,argv[1])){fprintf(stderr,"%s\n",cd.error);return 1;}
    int sh=cd_find_shop(&cd,cd_hash(argv[4]));
    cr_new(&st,&cd,"GOLF");
    if(sh<0||!cr_shop_open(&st,&cd,sh))return 1;
    int z=cd_find_zone(&cd,cd.shop[sh].zone_hash),m=cd_find_marker(&cd,cd.shop[sh].hash);
    if(z<0||m<0)return 1;
    for(int i=0;i<4;i++){st.pose[0]+=cd.zone[z].poly[i][0]*.25f;st.pose[1]+=cd.zone[z].poly[i][1]*.25f;}
    st.pose[2]=cd.marker[m].z;st.pose[3]=cd.marker[m].heading;
    st_profile_defaults(&profile,0);
    snprintf(profile.name,sizeof profile.name,"%s",argv[3]);
    if(st_profile_save(argv[2],&profile)!=ST_OK||cr_save_file(argv[2],argv[3],&st)!=ST_OK)return 1;
    printf("fixture: %s stage %d pose %.3f %.3f %.3f %.3f\n",argv[4],st.stage,st.pose[0],st.pose[1],st.pose[2],st.pose[3]);
    return 0;
}
