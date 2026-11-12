/* region_load.h - host-side wrapper around the upstream OpenUG2 world loader,
 * for the M4 region->cell converter. It loads a STREAM region (e.g.
 * STREAML4RH) with the same call the upstream --instance-audit path uses
 * (world_load_ex), GL- and SDL-free, and hands back the assembled world-space
 * scene plus the ROAD/TERRAIN bounding box that defines the cell grid.
 *
 * The upstream world.c is MIT/public-domain single-tree code (see the submodule
 * and PSP_PORT_NOTE.md); this file only drives its public API and adds the
 * authored-focus derivation main.c does, so the converter can pick the same
 * region extent the game would. The two GL entry points the loader's
 * never-called texture-upload path references are stubbed here.
 */
#ifndef PSP_CONVERTER_REGION_LOAD_H
#define PSP_CONVERTER_REGION_LOAD_H

#include "world.h"   /* World, N2Scene, N2Mesh, N2_ROAD/N2_TERRAIN, N2_SC_* */

typedef struct {
	World    *w;          /* owned loader state (heap; freed by region_close) */
	N2Scene  *scene;      /* == &w->neighborhood.scene: world-space meshes */
	float     focus[2];   /* authored focus used for the load */
	float     ground_bb[4];   /* x0,x1,y0,y1 over ROAD/TERRAIN verts: grid extent */
	long long ground_tris;    /* ROAD/TERRAIN triangle count */
	int       nmesh;
} Region;

/* Loads <dataroot>/TRACKS/<trackname>.BUN (e.g. "STREAML4RH"), selecting the
 * region around the authored focus exactly as the game's open-world path does.
 * <why> receives a one-line reason on failure. On success the caller must call
 * region_close. Returns 1 on success. */
int  region_open(Region *out, const char *dataroot, const char *trackname,
                 const char **why);
void region_close(Region *r);

/* 1 when the mesh should be drawn in a world cell per M4 step 2: ROAD or
 * TERRAIN ground, or scenery categories BUILDING / WALL / STRUCT. Sky, glow,
 * effect-only and unclassified meshes are excluded. */
static int region_mesh_visible(const N2Mesh *m) {
	if (!m || !m->idx || m->nidx < 3) return 0;
	if (m->cat == N2_SKY || m->cat == N2_GLOW || m->cat == N2_COLLISION)
		return 0;
	if (n2_world_effect_only((N2Mesh *)m)) return 0;
	if (m->cat == N2_ROAD || m->cat == N2_TERRAIN) return 1;
	return m->scen == N2_SC_BUILDING || m->scen == N2_SC_WALL ||
	       m->scen == N2_SC_STRUCT;
}

/* True for ROAD/TERRAIN ground meshes (the COLLIDE section uses these). */
static int region_mesh_ground(const N2Mesh *m) {
	return m && (m->cat == N2_ROAD || m->cat == N2_TERRAIN);
}

#endif
