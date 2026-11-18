/* Synthetic vinyl-layer navigation, purchase/remove requests and preview lifetime checks.
 * The rules under test are documented in docs/parity/VINYLS.md. */
#include "fe_vinyl.h"
#include "fe_menu.h"
#include "fe_screen.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CK(v) do { checks++; if(!(v)){fails++;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);} } while(0)
static FemRequest key(FeMenu *m,SDL_Keycode k) {
 SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;e.key.keysym.sym=k;fem_event(m,&e,NULL);return fem_update(m,1.0f/30);
}
/* A car's catalogue: three flames, one unique, one artFactory style (one colour), plus a
 * slot whose name admits no retail category (which the host must keep out of the shop). */
static void goto_cat(FeVinyl *v,int cat) {
 for(int s=0;s<v->ncat;s++) {
  if(v->cats[v->cat_sel]==cat)return;
  fvt_input(v,FVT_IN_NEXT_TAB);
 }
}
static void fill(FeVinylShop *s) {
 static const struct { const char *tok; int cat, colors; } d[] = {
  {"FLAME_001",CR_VINYL_CAT_FLAMES,3},{"FLAME_002",CR_VINYL_CAT_FLAMES,3},
  {"FLAME_003",CR_VINYL_CAT_FLAMES,3},{"UNIQUE_1",CR_VINYL_CAT_UNIQUE,3},
  {"SKYLINE_ART_F_1",CR_VINYL_CAT_ARTFACTORY,1},{"SHUFFLE_1",-1,3} };
 memset(s,0,sizeof *s);s->bank=1000;
 for(int i=0;i<6;i++) {
  FeVinylDesign *x=&s->design[s->ndesign++];
  snprintf(x->token,sizeof x->token,"%s",d[i].tok);
  x->category=d[i].cat;x->colors=d[i].colors;x->price=i==3?5000:250;
  x->open=d[i].cat!=CR_VINYL_CAT_UNIQUE && i!=1;
 }
 for(int i=0;i<4;i++) {
  FeVinylColor *c=&s->color[s->ncolor++];
  c->index=i;c->paint.key=0x100u+(unsigned)i;c->paint.group=CD_PAINT_VINYL;
  c->open=i!=2;c->price=50;
 }
}
int main(void) {
 static FeVinylShop shop;static FeVinyl v;
 fill(&shop);
 fvt_open(&v,&shop);
 CK(v.page==FVT_PAGE_LAYER && v.shop.layer==0);
 CK(!fvt_selected(&v));                                   /* the layer row lists no design */
 CK(v.ncat==3 && v.cats[0]==CR_VINYL_CAT_FLAMES && v.cats[1]==CR_VINYL_CAT_UNIQUE
    && v.cats[2]==CR_VINYL_CAT_ARTFACTORY);               /* the uncategorised slot is out */
 CK(!strcmp(fvt_package(&v),"UI_ChooseVinylLayer.fng"));

 /* An empty layer walks straight into the design list of the first category. */
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE && v.page==FVT_PAGE_DESIGN);
 CK(v.nrows==3 && v.sel==0);
 char tok[24];uint32_t col[CR_VINYL_COLORS];
 fvt_preview(&v,tok,sizeof tok,col);
 CK(!strcmp(tok,"FLAME_001"));
 CK(col[0]==0x100u && col[1]==0x101u && col[2]==0x103u);  /* the locked colour is skipped */

 /* A locked design and an unaffordable one are refused before the colour page. */
 CK(fvt_input(&v,FES_IN_RIGHT)==FVT_EV_NONE && v.sel==1);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_NONE && v.msg==FVT_MSG_LOCKED && v.page==FVT_PAGE_DESIGN);
 v.msg=FVT_MSG_NONE;
 CK(fvt_input(&v,FES_IN_RIGHT)==FVT_EV_NONE && v.sel==2);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE && v.page==FVT_PAGE_COLOR);

 /* The colour page arms the invoice line first and buys it on the second Accept. */
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE && v.confirm && v.msg==FVT_MSG_CONFIRM);
 CK(!strcmp(v.req.token,"FLAME_003") && v.req.layer==0 && !v.req.remove);
 CK(v.req.colour[0]==0x100u && v.req.colour[2]==0x103u);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_BUY);

 /* Back unwinds the invoice, then the colour page, then the design list to the layers. */
 fvt_open(&v,&shop);fvt_input(&v,FES_IN_ACCEPT);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE && v.page==FVT_PAGE_COLOR);
 CK(fvt_input(&v,FES_IN_BACK)==FVT_EV_PAGE && v.page==FVT_PAGE_DESIGN);
 CK(fvt_input(&v,FES_IN_BACK)==FVT_EV_PAGE && v.page==FVT_PAGE_LAYER);
 fvt_preview(&v,tok,sizeof tok,col);
 CK(!tok[0] && !col[0]);                                  /* Back leaves nothing previewed */
 CK(fvt_input(&v,FES_IN_BACK)==FVT_EV_LEAVE);

 /* A three-colour design offers every unlocked colour, and a house style only A. */
 fvt_open(&v,&shop);fvt_input(&v,FES_IN_ACCEPT);           /* layers -> flames */
 v.sel=2;fvt_input(&v,FES_IN_ACCEPT);                      /* FLAME_003 -> colours */
 CK(v.colour_sel[0]==0 && v.colour_sel[1]==1 && v.colour_sel[2]==3);
 fvt_open(&v,&shop);                                       /* a fresh visit starts on the first tab */
 CK(v.page==FVT_PAGE_LAYER && v.cat_sel==0 && v.nrows==3);
 fvt_input(&v,FES_IN_ACCEPT);                              /* layers -> design list */
 goto_cat(&v,CR_VINYL_CAT_ARTFACTORY);                     /* the tabs work at that level too */
 CK(v.nrows==1 && v.sel==0);
 fvt_input(&v,FES_IN_ACCEPT);                              /* the style offers one colour */
 CK(v.page==FVT_PAGE_COLOR && v.colour_sel[0]>=0 && v.colour_sel[1]<0 && v.colour_sel[2]<0);
 fvt_input(&v,FES_IN_ACCEPT);                              /* arm the invoice */
 CK(!strcmp(v.req.token,"SKYLINE_ART_F_1") && v.req.colour[0] && !v.req.colour[1] && !v.req.colour[2]);

 /* Layer selection cycles the layer row, and the layer shown is the layer the car would wear.
  * The edited layer belongs to the snapshot, so the host (not the visit) decides it. */
 shop.layer=0;fvt_open(&v,&shop);
 CK(fvt_preview_layer(&v)==0);
 fvt_input(&v,FES_IN_LEFT);CK(fvt_preview_layer(&v)==3);   /* the stack is named from underneath */
 fvt_input(&v,FES_IN_RIGHT);CK(fvt_preview_layer(&v)==0);  /* and a visit starts where the host set it */
 shop.layer=0;

 /* A layer that already wears a design asks before taking it off, and removing is free. */
 snprintf(shop.current,sizeof shop.current,"FLAME_002");
 shop.current_colour[0]=0x101u;
 fvt_open(&v,&shop);
 CK(v.page==FVT_PAGE_LAYER && !strcmp(v.shop.current,"FLAME_002"));
 fvt_preview(&v,tok,sizeof tok,col);
 CK(!strcmp(tok,"FLAME_002") && col[0]==0x101u);           /* the owned colour stays selected */
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE && v.confirm && v.msg==FVT_MSG_CONFIRM);
 CK(fvt_input(&v,FES_IN_BACK)==FVT_EV_PAGE && v.page==FVT_PAGE_DESIGN && !v.confirm);
 CK(v.nrows==3);                                           /* "change it": the list is open */
 CK(fvt_input(&v,FES_IN_BACK)==FVT_EV_PAGE && v.page==FVT_PAGE_LAYER);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_PAGE);             /* ask again */
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_REMOVE && v.req.remove==1 && !v.req.token[0]);

 /* An owned design reads as installed and is not charged a second time. */
 shop.design[0].owned=1;shop.current[0]=0;shop.current_colour[0]=0;
 fvt_open(&v,&shop);fvt_input(&v,FES_IN_ACCEPT);            /* layers -> flames design list */
 CK(fvt_selected(&v) && fvt_selected(&v)->owned);           /* the first row is FLAME_001 */
 fvt_input(&v,FES_IN_ACCEPT);                              /* owned, so the colour page opens */
 CK(v.page==FVT_PAGE_COLOR && v.msg==FVT_MSG_NONE);
 fvt_input(&v,FES_IN_ACCEPT);fvt_input(&v,FES_IN_ACCEPT);  /* a recolour is a normal request */
 CK(!strcmp(v.req.token,"FLAME_001"));

 /* The buy keeps the retail colour page (retail shows the vinyl it just applied) and the
  * design stays previewed there; fvt_settle is what clears the pending choice, and
  * fvt_refresh then keeps the page, the row and the layer while showing the host's message. */
 CK(v.page==FVT_PAGE_COLOR);
 fvt_preview(&v,tok,sizeof tok,col);
 CK(!strcmp(tok,"FLAME_001"));                             /* still on show while on that page */
 fvt_settle(&v);
 fvt_preview(&v,tok,sizeof tok,col);
 CK(!tok[0]);                                              /* settled: nothing half-applied */
 CK(!strcmp(v.req.token,"FLAME_001"));                     /* the host can still read the buy */
 fvt_open(&v,&shop);fvt_input(&v,FES_IN_ACCEPT);fvt_input(&v,FES_IN_ACCEPT);
 fvt_refresh(&v,&shop,FVT_MSG_NO_FUNDS);
 CK(v.page==FVT_PAGE_COLOR && v.msg==FVT_MSG_NO_FUNDS && v.shop.bank==1000);
 fvt_refresh(&v,&shop,FVT_MSG_NONE);
 CK(v.msg==FVT_MSG_NONE);

 /* Nothing to browse at all: the layer row reports the category as locked. */
 fill(&shop);shop.ndesign=0;
 fvt_open(&v,&shop);
 CK(!v.ncat && fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_NONE && v.msg==FVT_MSG_LOCKED);
 fvt_open(&v,NULL);
 CK(fvt_input(&v,FES_IN_ACCEPT)==FVT_EV_NONE && fvt_input(&v,FES_IN_LEFT)==FVT_EV_NONE);
 fvt_preview(&v,tok,sizeof tok,col);CK(!tok[0]);

 /* The menu host: opening shows the layer screen, a buy request carries the whole choice,
  * and the preview is only live while that screen is open. */
 fill(&shop);
 FeMenu *m=fem_create("/nonexistent",NULL,1);CK(m!=NULL);
 fem_open_vinyl(m,&shop);
 CK(fem_screen(m)==FEM_SCR_CAREER_VINYL);
 CK(!fem_vinyl_preview(m));                                /* the layer row holds nothing yet */
 FemRequest r=key(m,SDLK_RETURN);                          /* layers -> design list */
 CK(r.kind==FEM_REQ_NONE);
 CK(fem_vinyl_preview(m));                                 /* browsing previews on the car */
 r=key(m,SDLK_RETURN);                                     /* design -> colour page */
 CK(r.kind==FEM_REQ_NONE);
 key(m,SDLK_RETURN);                                       /* arm the invoice */
 uint32_t pc[CR_VINYL_COLORS];fem_vinyl_preview_colours(m,pc);  /* the choice is on show */
 CK(pc[0] && pc[1] && pc[2]);
 r=key(m,SDLK_RETURN);
 CK(r.kind==FEM_REQ_VINYL_BUY && !r.vinyl.remove && r.vinyl.layer==0);
 CK(!strcmp(r.vinyl.token,"FLAME_001"));
 CK(fem_vinyl_preview_layer(m)==0);
 fem_vinyl_refresh(m,&shop,FVT_MSG_NONE);                  /* the host accepted the purchase */
 CK(!fem_vinyl_preview(m));                                /* settling left nothing on the car */
 r=key(m,SDLK_ESCAPE);CK(r.kind==FEM_REQ_CAREER_ROAM);     /* the layer row is the way out */
 CK(!fem_vinyl_preview(m) && fem_vinyl_preview_layer(m)==0);   /* empty layer previews nothing,
                                                             * but the layer row still names
                                                             * the layer under editing */
 fem_destroy(m);
 fprintf(stderr,"fe_vinyl_test: checks=%d fails=%d\n",checks,fails);
 return fails!=0;
}
