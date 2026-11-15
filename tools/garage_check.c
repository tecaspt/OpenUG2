/* Runtime-only asset proof, never embeds retail bytes. */
#include "garage.h"
#include "game_catalog.h"
#include "car_mod.h"
#include <assert.h>
int main(int argc,char **argv){
 const char *root=argc>1?argv[1]:getenv("OPENUG2_DATA");if(!root){puts("garage_check: SKIP data unset");return 77;}
 GameCatalog *cat=calloc(1,sizeof *cat);GarageCatalog *parts=calloc(1,sizeof *parts);assert(cat&&parts);
 if(!gcat_build(cat,root))return 1;
 GarageRims *rims=calloc(1,sizeof *rims);assert(rims);
 if(!n2_mod_rim_catalog(root,rims)){fprintf(stderr,"FAIL rim catalogue\n");return 1;}
 char wheelpath[1100];snprintf(wheelpath,sizeof wheelpath,"%s/CARS/WHEELS/TEXTURES.BIN",root);
 long tlen=0;unsigned char *textures=n2_read_file(wheelpath,&tlen);if(!textures){fprintf(stderr,"FAIL wheel textures\n");return 1;}
 uint32_t keys[512];int nkeys=n2_car_tex_keys(textures,tlen,keys,512);
 for(int i=0;i<rims->count;i++) {
  snprintf(wheelpath,sizeof wheelpath,"%s/CARS/WHEELS/GEOMETRY_%s.BIN",root,rims->options[i].brand);
  long len=0;unsigned char *data=n2_read_file(wheelpath,&len);N2Scene scene={0};
  int ok=data&&n2_mod_rim_scene(data,len,textures,tlen,keys,nkeys,rims->options[i].style,&scene);
  n2_free_scene(&scene);free(data);
  if(!ok){fprintf(stderr,"FAIL rim %s %d\n",rims->options[i].brand,rims->options[i].style);return 1;}
  printf("RIM %s %d rebuilt CPU geometry/textures\n",rims->options[i].brand,rims->options[i].style);
 }
 free(textures);
 printf("RIM-CATALOG count=%d\n",rims->count);free(rims);
 int cars=0,options=0;
 for(int c=0;c<cat->ncar;c++)if(!cat->car[c].traffic){
  if(!n2_mod_garage_catalog(root,cat->car[c].name,parts)){fprintf(stderr,"FAIL catalog %s\n",cat->car[c].name);return 1;}
  char file[1024];snprintf(file,sizeof file,"%s/CARS/%s/GEOMETRY.BIN",root,cat->car[c].name);long len=0;unsigned char *data=n2_read_file(file,&len);assert(data);
  int total=0;for(int s=0;s<GARAGE_SLOTS;s++)for(int i=0;i<parts->menu[s].count;i++){
   N2CarConfig cfg={0};if(s==GARAGE_KIT)cfg.body_kit=parts->menu[s].options[i].value;else cfg.parts[s]=parts->menu[s].options[i].value;
   N2Scene scene={0};if(!n2_mod_garage_scene(root,data,len,&cfg,&scene)){fprintf(stderr,"FAIL %s slot%d value%d\n",cat->car[c].name,s,parts->menu[s].options[i].value);return 1;}
   n2_free_scene(&scene);total++;
  }free(data);printf("GARAGE %s options=%d valid CPU geometry/selection\n",cat->car[c].name,total);fflush(stdout);cars++;options+=total;
 }
 printf("garage_check: PASS cars=%d options=%d (CPU assembly; GPU proof is separate)\n",cars,options);free(parts);free(cat);return !cars;
}
