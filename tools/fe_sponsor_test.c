/* Sponsor navigation and real menu input routing, without retail art. */
#include "fe_menu.h"
#include "fe_screen.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CK(x) do {checks++; if(!(x)){fails++;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static FemRequest input(FeMenu *m, int type, int code) {
    SDL_Event e; memset(&e,0,sizeof e);e.type=type;
    if(type==SDL_KEYDOWN)e.key.keysym.sym=code;else e.cbutton.button=code;
    fem_event(m,&e,NULL);return fem_update(m,.04f);
}
int main(void) {
    static CareerData cd;CareerState s;FeSponsor p;
    cd.nstage=1;cd.stage[0].index=2;cd.stage[0].nsponsors=2;
    cd.nsponsor=2;
    for(int i=0;i<2;i++) {
        cd.stage[0].sponsor[i]=cd.sponsor[i].hash=100+i;
        strcpy(cd.sponsor[i].name,"TEST");cd.sponsor[i].signing=1000;
    }
    cd.sponsor[1].reputation=75;
    cr_new(&s,&cd,"CAR");s.stage=2;
    fsp_open(&p,&cd,&s);
    CK(p.n==2 && p.page==0 && p.sel==0);
    CK(!strcmp(fsp_package(&p),"UI_SponsorPopup.fng"));
    CK(fsp_input(&p,FES_IN_ACCEPT)==FSP_PAGE && p.page==1);
    fsp_input(&p,FES_IN_RIGHT);CK(p.sel==1);
    CK(fsp_input(&p,FES_IN_ACCEPT)==FSP_NONE);
    fsp_input(&p,5);CK(p.sel==0 && fsp_input(&p,FES_IN_ACCEPT)==FSP_SIGN);
    CK(fsp_input(&p,FES_IN_BACK)==FSP_PAGE && p.page==0);
    CK(fsp_input(&p,FES_IN_BACK)==FSP_LEAVE);
    FeMenu *menu=fem_create("/missing",NULL,0);CK(menu!=NULL);
    fem_open_sponsor(menu,&cd,&s);
    CK(fem_screen(menu)==FEM_SCR_SPONSOR);
    CK(input(menu,SDL_KEYDOWN,SDLK_RETURN).kind==FEM_REQ_NONE);
    input(menu,SDL_CONTROLLERBUTTONDOWN,SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    CK(input(menu,SDL_CONTROLLERBUTTONDOWN,SDL_CONTROLLER_BUTTON_A).kind==FEM_REQ_NONE);
    input(menu,SDL_KEYDOWN,SDLK_LEFT);
    FemRequest r=input(menu,SDL_CONTROLLERBUTTONDOWN,SDL_CONTROLLER_BUTTON_A);
    CK(r.kind==FEM_REQ_SPONSOR_SIGN && r.sponsor_index==0);
    fem_sponsor_signed(menu,1000);
    CK(input(menu,SDL_KEYDOWN,SDLK_RETURN).kind==FEM_REQ_CAREER_ROAM);
    fem_destroy(menu);
    printf("fe_sponsor_test: %d checks, %d failures\n",checks,fails);return fails!=0;
}
