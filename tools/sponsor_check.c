/* Retail contracts, persistence and transactional host checks; optional isolated run fixture. */
#include "career_host.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static int checks, fails;
#define CK(x) do {checks++; if(!(x)){fails++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);}}while(0)
static CareerHost host;
static void stage_two(CareerState *s) {
    cr_new(s,&host.cd,"GOLF");
    for (int guard=0;s->stage==1 && guard<CD_MAX_RACES;guard++) {
        int list[CD_MAX_RACES],n=cr_list(s,&host.cd,list,CD_MAX_RACES);
        if(!n)break;
        CrResult r={host.cd.race[list[0]].name_hash,1,4,0,0,20};
        cr_apply(s,&host.cd,&r,NULL,NULL,NULL);
    }
}
int main(int argc,char **argv) {
    if(argc!=2 && argc!=4 && argc!=5)return 2;
    CK(cd_load(&host.cd,argv[1]));host.loaded=1;host.active=1;host.race=-1;
    CK(host.cd.nsponsor==20);
    for(int st=2;st<=5;st++) {
        cr_new(&host.st,&host.cd,"GOLF");host.st.stage=st;host.st.rep=1000;host.st.entered=1;
        int offers=0;
        for(int i=0;i<host.cd.nsponsor;i++)if(cr_sponsor_eligible(&host.st,&host.cd,i)) {
            CareerState state=host.st;offers++;
            CK(host.cd.sponsor[i].hash==cd_hash(host.cd.sponsor[i].name));
            CK(cr_sponsor_choose(&state,&host.cd,i));
            int sponsor_events=0;
            for(int r=0;r<host.cd.nrace;r++)if(host.cd.race[r].category==CD_CAT_SPONSOR && cr_available(&state,&host.cd,r))sponsor_events++;
            CK(sponsor_events==3);
        }
        CK(offers==5);
    }
    stage_two(&host.st);CK(host.st.stage==2 && cr_sponsor_pending(&host.st,&host.cd));
    CareerState before=host.st;
    snprintf(host.root,sizeof host.root,"/missing/sponsor-save");strcpy(host.profile,"Driver");
    CK(!ch_sponsor_choose(&host,0));CK(!memcmp(&before,&host.st,sizeof before));
    if(argc==5) {
        CK(cr_load_file(argv[2],argv[3],&host.st)==ST_OK);
        CK(host.st.stage==2 && cr_sponsor_current(&host.st,&host.cd)==0 && host.st.bank==2525);
        CK(!cr_sponsor_choose(&host.st,&host.cd,0) && host.st.bank==2525);
        printf("sponsor cold save: stage %d sponsor %08x bank %lld\n",host.st.stage,host.st.sponsor[2],(long long)host.st.bank);
    }
    if(argc==4) {
        static StProfile profile;st_profile_defaults(&profile,0);
        snprintf(profile.name,sizeof profile.name,"%s",argv[3]);
        CK(st_profile_save(argv[2],&profile)==ST_OK);
        CK(cr_save_file(argv[2],argv[3],&host.st)==ST_OK);
        printf("sponsor fixture: reached stage %d bank %lld wins %u\n",host.st.stage,(long long)host.st.bank,host.st.won);
    }
    printf("sponsor_check: %d checks, %d failures\n",checks,fails);return fails!=0;
}
