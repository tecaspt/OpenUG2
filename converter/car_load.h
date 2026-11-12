/* Car front-end for psp_convert.
 *
 * This is the load sequence of OpenUG2's src/main.c prepare_car_switch()
 * (main.c:455-525) with the GL upload removed and the physics stance kept, so a
 * converted car is the same geometry the desktop build draws. Everything here
 * is copied from upstream on purpose: the milestone rule is that the PSP must
 * not invent its own car loading.
 *
 * Nothing in this file knows about the PSP or U2PK.
 */
#ifndef OPENUG2_CONVERTER_CAR_LOAD_H
#define OPENUG2_CONVERTER_CAR_LOAD_H

#include "nfsu2.h"

typedef struct {
	char name[64];
	unsigned char *geom; long geomlen;   /* CARS/<name>/GEOMETRY.BIN */
	unsigned char *tpk;  long tpklen;    /* CARS/<name>/TEXTURES.BIN */
	unsigned char *glob; long globlen;   /* GLOBAL/GLOBALB.BUN */
	N2Tpk gt;                            /* view into glob */
	int prelight;                        /* 1 = the car pack was empty */
	uint32_t keys[2048]; int nkeys;      /* every texture key this car may use */
	N2Scene scene;                       /* LODs already deduped by the loader */
	N2CarProfile prof;
	float hub[4][3];                     /* FL, FR, RL, RR hub centres */
	float wheel_radius;
	int stock_wheel;                     /* scene index of a stock wheel slice */
	uint32_t stock_tier;                 /* tierid of that wheel, 0 = none */
	int wheel_from_global;               /* 1 = axle/track came from GLOBALB */
} Car;

/* Loads one car from a retail data root (the directory holding CARS/ and
 * GLOBAL/). Returns 1 on success; on failure *why names the reason and the
 * Car holds nothing that needs freeing. */
int car_open(Car *c, const char *root, const char *name, const char **why);
void car_close(Car *c);

/* Decodes one texture key, car pack first and GLOBALB second, exactly like the
 * desktop build's texture map. Returns 1 on success; the caller owns
 * t->rgb / t->alpha / t->dxt. */
int car_tex(Car *c, uint32_t key, N2Tex *t);

#endif /* OPENUG2_CONVERTER_CAR_LOAD_H */
