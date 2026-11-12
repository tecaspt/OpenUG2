/* region_conv.h - M4 region -> world-cell converter.
 *
 * region_survey loads a region and runs the geometry half of the conversion
 * (grid build, ROAD/TERRAIN triangle splitting to the 8 m edge limit, centroid
 * cell assignment) without writing any file, so the cell counts and the budget
 * table can be checked before the texture encoder and U2PK writers are built
 * out. region_convert (added in a later increment) does the full job and emits
 * index.u2pk / cells.u2pk / tex.u2pk.
 */
#ifndef PSP_CONVERTER_REGION_CONV_H
#define PSP_CONVERTER_REGION_CONV_H

#include <stdint.h>

#include "u2pk.h"

#define REGION_CELL_M   U2PK_CELL_M /* cell side length, metres (M4 spec) */
#define REGION_EDGE_MAX  8.0f    /* longest ROAD/TERRAIN triangle edge, metres */

/* Max camera poses region_poses may emit; matches the runtime's MAX_POSES. */
#define MAX_POSE_DUMPS 8

typedef struct {
	char     trackname[32];
	int      nmesh_visible;
	long long tris_in;        /* triangles drawn after mesh filtering */
	long long tris_out;       /* triangles after 8 m splitting */
	int      grid_w, grid_h;  /* cell grid dimensions */
	int      ncell;           /* grid_w * grid_h */
	int      ncell_used;      /* cells that received at least one triangle */
	int      worst_cell_tris;
	int      worst_cell_cx, worst_cell_cy;
	int      worst_win_tris;  /* worst 3x3 window triangle count */
	int      worst_win_batches;
	int      ntex;            /* distinct texture keys among drawn meshes */
	int      within_budget;   /* every cell <=256KB and every 3x3 window <=15000
	                             tris and <=250 batches (approximate here: the
	                             survey counts triangles and (cell,tex) batches) */
	/* diagnostics for the 8 m split blow-up */
	long long road_in, terr_in, other_in;
	long long road_out, terr_out, other_out;
	long long big_src_tris;     /* source tris whose longest edge > 32 m */
	float     max_src_edge;     /* largest source triangle edge seen (m) */
	float     src_hist[6];      /* source edges: <=2,<=4,<=8,<=16,<=32,>32 m */
	int       src_hist_n[6];
	/* written-file sizes (bytes) from region_convert */
	long long cells_bytes, index_bytes, tex_bytes;
	int       ncell_written;
	int       nbatch_total;
	int       ntex_real;      /* keys whose real pixels decoded (vs white placeholder) */
} RegionReport;

int region_survey(const char *dataroot, const char *trackname, RegionReport *rep);

/* Full M4 conversion. Writes <outdir>/index.u2pk, <outdir>/cells.u2pk and
 * <outdir>/tex.u2pk for <trackname>, filling *rep with the budget table and the
 * file sizes. <why> receives a one-line reason on failure. Returns 1 on
 * success. */
int region_convert(const char *dataroot, const char *trackname,
                   const char *outdir, RegionReport *rep);

/* Emulator-test helper: reads <dataroot>/TRACKS/ROUTES<reg>/PathsFreeRoam.bin
 * (the racing-line centreline), picks <npose> nodes spread along it, looks each
 * node's ground height up in the loaded ROAD/TERRAIN scene, and writes a
 * runtime data/world.cfg with "region=<reg>" plus one "pose=x y z yaw" per node
 * (z = ground + 2 m, yaw = toward the next node). Returns the pose count written
 * (0 if no usable path), or -1 on failure. */
int region_poses(const char *dataroot, const char *trackname,
                 const char *cfgpath, int npose);

#endif
