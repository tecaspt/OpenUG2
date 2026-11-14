/* Optional check against locally installed assets; no game files are fixtures. */
#include "game_catalog.h"
#include "race.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    const char *root=argc>1?argv[1]:getenv("OPENUG2_DATA");
    if(!root){fprintf(stderr,"Usage: game_catalog_check DATAROOT\n");return 2;}
    GameCatalog *c=malloc(sizeof *c);
    if(!c || !gcat_build(c,root)){fprintf(stderr,"catalogue build failed\n");return 1;}
    int player=0,kinds[8]={0};
    for(int i=0;i<c->ncar;i++)player+=!c->car[i].traffic;
    for(int i=0;i<c->nev;i++)kinds[c->ev[i].kind&7]++;
    printf("CATALOG events=%d regions=%d cars=%d player=%d\n",c->nev,c->nregion,c->ncar,player);
    printf("KINDS");
    for(int k=N2_RACE_CIRCUIT;k<=N2_RACE_URL;k++)printf(" %s %d",k==N2_RACE_STREETX?"Street":n2_race_name(k),kinds[k]);
    printf("\nREGIONS");
    for(int r=0;r<c->nregion;r++)printf(" %s %d",c->region[r].stem+2,c->region[r].nevents);
    printf("\n");
    free(c);return 0;
}
