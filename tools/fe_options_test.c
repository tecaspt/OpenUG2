#include "options_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void tick(OptionsMenu *m){om_update(m,2);}
int main(void) {
    static StProfile profile;st_profile_defaults(&profile,0);
    OptionsMenu menu={0};OmConfig cfg;om_config_default(&cfg,OM_CTX_FRONTEND);cfg.profile=&profile;
    StGraphics gfx;StGfxCaps caps;StRegStore store;st_gfx_defaults(&gfx);st_gfx_caps_full(&caps,1);assert(st_reg_open_memory(&store)==ST_OK);
    om_open_hub(&menu,&cfg,&gfx,&caps,&store);assert(menu.cfg.ctx==OM_CTX_FRONTEND);assert(!strcmp(om_package_name(menu.cfg.ctx,menu.screen),"UI_OptionsMain.fng"));
    om_input(&menu,OM_IN_ACCEPT);tick(&menu);assert(menu.screen==OM_SCREEN_AUDIO && menu.nrows==8);
    float before=profile.audio.master;om_input(&menu,OM_IN_LEFT);assert(profile.audio.master<before);assert(om_take_profile_request(&menu));assert(!om_take_profile_request(&menu));
    om_input(&menu,OM_IN_BACK);tick(&menu);assert(menu.screen==OM_SCREEN_HUB);
    for(int i=0;i<3;i++)om_input(&menu,OM_IN_RIGHT);
    om_input(&menu,OM_IN_ACCEPT);tick(&menu);assert(menu.screen==OM_SCREEN_PLAYER);
    int gauges=profile.player[0].gauges;om_input(&menu,OM_IN_RIGHT);assert(profile.player[0].gauges!=gauges);
    om_input(&menu,OM_IN_BACK);tick(&menu);om_input(&menu,OM_IN_LEFT);om_input(&menu,OM_IN_ACCEPT);tick(&menu);assert(menu.screen==OM_SCREEN_GAMEPLAY && menu.nrows==5);
    om_input(&menu,OM_IN_BACK);tick(&menu);om_input(&menu,OM_IN_BACK);tick(&menu);assert(!om_is_open(&menu));
    st_reg_close(&store);puts("fe_options_test: frontend context/audio/player/gameplay/profile edits/back PASS");return 0;
}
