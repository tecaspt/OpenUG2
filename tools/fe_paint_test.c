/* Synthetic paint navigation, transaction request and preview lifetime checks. */
#include "fe_paint.h"
#include "fe_menu.h"
#include "fe_screen.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CK(v) do { checks++; if(!(v)){fails++;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);} } while(0)
static FemRequest key(FeMenu *m,SDL_Keycode k) {
 SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;e.key.keysym.sym=k;fem_event(m,&e,NULL);return fem_update(m,1.0f/30);
}
int main(void) {
 static FePaintShop shop;static FePaint p;
 shop.n=63;shop.bank=1000;
 for(int i=0;i<shop.n;i++) {
  FePaintColor *c=&shop.color[i];c->index=i;c->paint.key=i+1;c->paint.group=i<60?0:i==60?1:i==61?3:4;
  c->open=i!=2;c->price=i==3?2000:100;c->installed=i==0;
 }
 fpt_open(&p,&shop);CK(p.ncats==4 && !p.page);CK(!fpt_selected(&p));
 CK(fpt_input(&p,FES_IN_LEFT)==FPT_EV_NONE && p.cat_sel==0);
 CK(fpt_input(&p,FES_IN_ACCEPT)==FPT_EV_PAGE && p.nrows==60);
 CK(fpt_selected(&p)->index==0);
 fpt_input(&p,FES_IN_ACCEPT);CK(p.msg==FPT_MSG_OWNED);
 fpt_input(&p,FES_IN_RIGHT);CK(p.msg==FPT_MSG_NONE);
 fpt_input(&p,FES_IN_ACCEPT);CK(p.confirm && p.msg==FPT_MSG_CONFIRM);
 CK(fpt_input(&p,FES_IN_BACK)==FPT_EV_NONE && !p.confirm && p.page);
 fpt_input(&p,FES_IN_ACCEPT);CK(fpt_input(&p,FES_IN_ACCEPT)==FPT_EV_BUY && p.buy_index==1);
 shop.color[1].installed=1;shop.color[0].installed=0;shop.bank-=100;
 fpt_refresh(&p,&shop,FPT_MSG_NONE);CK(p.sel==1 && p.shop.bank==900 && fpt_selected(&p)->installed);
 fpt_input(&p,FES_IN_RIGHT);fpt_input(&p,FES_IN_ACCEPT);CK(p.msg==FPT_MSG_LOCKED && !p.confirm);
 fpt_input(&p,FES_IN_RIGHT);fpt_input(&p,FES_IN_ACCEPT);CK(p.msg==FPT_MSG_NO_FUNDS && !p.confirm);
 fpt_input(&p,6);CK(p.sel==23);fpt_input(&p,5);CK(p.sel==3);
 CK(fpt_input(&p,FES_IN_BACK)==FPT_EV_PAGE && !fpt_selected(&p));
 fpt_input(&p,FES_IN_RIGHT);fpt_input(&p,FES_IN_ACCEPT);CK(p.nrows==1 && fpt_selected(&p)->index==60);
 fpt_input(&p,FES_IN_BACK);CK(fpt_input(&p,FES_IN_BACK)==FPT_EV_LEAVE);
 fpt_open(&p,NULL);CK(fpt_input(&p,FES_IN_ACCEPT)==FPT_EV_NONE);
 FeMenu *m=fem_create("/nonexistent",NULL,1);CK(m!=NULL);
 fem_open_paint(m,&shop);CK(fem_screen(m)==FEM_SCR_CAREER_PAINT && !fem_paint_preview(m,0));
 key(m,SDLK_RETURN);CK(fem_paint_preview(m,0)!=NULL && !fem_paint_preview(m,1));
 key(m,SDLK_RIGHT);key(m,SDLK_RIGHT);key(m,SDLK_RIGHT);key(m,SDLK_RIGHT);
 CK(fem_paint_preview(m,0)->key==5);key(m,SDLK_RETURN);
 FemRequest r=key(m,SDLK_RETURN);CK(r.kind==FEM_REQ_PAINT_BUY && r.paint_index==4);
 CK(key(m,SDLK_ESCAPE).kind==FEM_REQ_NONE);
 fem_paint_refresh(m,&shop,FPT_MSG_SAVE);key(m,SDLK_ESCAPE);CK(!fem_paint_preview(m,0));
 CK(key(m,SDLK_ESCAPE).kind==FEM_REQ_CAREER_ROAM);
 fem_close(m);CK(!fem_paint_preview(m,0));fem_destroy(m);
 printf("fe_paint_test: %d checks, %d failures\n",checks,fails);return fails?1:0;
}
