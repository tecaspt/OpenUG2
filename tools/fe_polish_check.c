/* Runtime retail check: reads OPENUG2_DATA, embeds no retail payloads or captions. */
#include "fe_menu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int guid(const GvScreen *s,uint32_t id) {
    for(int i=0;i<s->scene.n;i++)if(s->scene.nodes[i].obj->guid==id)return i;
    return -1;
}
int main(void) {
    const char *root=getenv("OPENUG2_DATA");
    if(!root){puts("SKIP: OPENUG2_DATA unset");return 77;}
    UgAssets assets;char error[256];
    if(!ug_assets_open(&assets,root,NULL,error,sizeof error)){fprintf(stderr,"FAIL: %s\n",error);return 1;}
    GameCatalog cat={0};cat.nev=2;cat.ncar=2;
    for(int i=0;i<2;i++) {
        cat.ev[i].id=4201+i;cat.ev[i].kind=3;strcpy(cat.ev[i].region,"L4RB");
        char key[40];snprintf(key,sizeof key,"TRACK_NAME_%d",cat.ev[i].id);
        cat.ev[i].name_key=ug_hash_upper(key);
    }
    strcpy(cat.car[0].name,"MIATA");strcpy(cat.car[1].name,"RX7");
    FeChoice choice={0};GvScreen screen={0};choice.cat=&cat;choice.kind=3;choice.selected=1;
    assert(fet_open(&choice,&screen,&assets,0));
    assert(choice.track_name_keys[1]==cat.ev[0].name_key);
    assert(choice.track_map_ids[1]==cat.ev[0].id);
    assert(choice.map_textures[1]>0);
    const UgTexture *texture=&assets.tex.t[choice.map_textures[1]-1];
    assert(texture->rgba && texture->rgba[3]>0);
    gv_screen_update(&screen,1000);fet_sync(&choice,&screen,0,0);
    int caption=guid(&screen,0xdf142),map=guid(&screen,0xe2305),thumbnails=0;
    assert(caption>=0 && map>=0);
    assert(!strcmp(screen.scene.nodes[caption].text_override,ug_strtab_get(&assets.strings,cat.ev[0].name_key)));
    assert(screen.scene.nodes[map].tex_override==choice.map_textures[1]);
    for(int i=0;i<screen.scene.n;i++)if(screen.scene.nodes[i].obj->name_hash==0xf5ff3dbc && !screen.scene.nodes[i].app_hidden)thumbnails++;
    assert(thumbnails==1);gv_screen_free(&screen);
    assert(fec_open(&choice,&screen,&assets));gv_screen_update(&screen,1000);fec_sync(&choice,&screen,.3f);
    int logo=guid(&screen,0xdcac7);assert(logo>=0 && screen.scene.nodes[logo].tex_override>0 && !screen.scene.nodes[logo].app_hidden);
    assert(!strcmp(choice.preview,cat.car[0].name));
    gv_screen_free(&screen);ug_assets_close(&assets);fet_free(&choice);
    puts("fe_polish_check: named layout alias, map, thumbnail and car badge PASS");return 0;
}
