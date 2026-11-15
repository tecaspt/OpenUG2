/* Runtime-only asset proof, never embeds retail bytes. */
#include "garage.h"
#include "game_catalog.h"
#include "car_mod.h"
#include <assert.h>
int main(int argc,char **argv){
 const char *root=argc>1?argv[1]:getenv("OPENUG2_DATA");if(!root){puts("garage_check: SKIP data unset");return 77;}
 GameCatalog *cat=calloc(1,sizeof *cat);GarageCatalog *parts=calloc(1,sizeof *parts);assert(cat&&parts);
 if(!gcat_build(cat,root))return 1;
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
