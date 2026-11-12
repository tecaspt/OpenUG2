/* region_tex.h - encode the distinct source textures of a world region into one
 * shared tex.u2pk (M4 step 4). Each key is decoded from the loaded world's own
 * texture packs - the region STREAM TPKs first, then LOC4DYNTEX, then the
 * master and common packs - exactly the lookup the game's world renderer uses
 * (main.c per-mesh track-texture resolution), NOT GLOBALB, which holds car art.
 * Each key is capped to 128 px (road/terrain) or 64 px (else), quantised to a
 * 256-colour T8 + CLUT pair and swizzled - the same T8 path the car converter
 * uses, which the M4 spec explicitly allows ("PSP DXT1 ... or T8"). A key that
 * no pack decodes gets a 16x8 white placeholder, so its geometry still renders
 * as its (already baked) per-vertex colour. Slots keep caller order.
 */
#ifndef PSP_CONVERTER_REGION_TEX_H
#define PSP_CONVERTER_REGION_TEX_H

#include <stddef.h>
#include <stdint.h>

#include "world.h"   /* WorldNeighborhood */

typedef struct {
	uint32_t key;
	int      ground;         /* used by a ROAD/TERRAIN mesh -> 128 cap, else 64 */

	int      w, h;           /* padded GE size actually written */
	int      mode;           /* U2PK_MODE_* implied by the source alpha */
	int      nclut;
	int      clut_slot;      /* assigned by the writer, -1 = none */
	int      encoded;        /* 1 = decoded real art, 0 = white placeholder */
	uint8_t *data;           /* swizzled T8, w*h bytes (freed by the writer) */
	size_t   size;
	uint32_t clut[256];      /* GU_PSM_8888 order (0xAABBGGRR) */
} RTex;

/* Encodes tex[0..n) (order preserved; tex[i] corresponds to source key
 * tex[i].key) into a tex.u2pk at outpath, resolving each key through the packs
 * the loaded region keeps alive in *nb. Returns 1 on success, 0 with *err set.
 * Frees every tex[i].data it allocated before returning. */
int region_tex_write(WorldNeighborhood *nb, RTex *tex, int n,
                     const char *outpath, long long *bytes_out,
                     char *err, int errn);

#endif
