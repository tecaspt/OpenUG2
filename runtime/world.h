/* world.h - M4 runtime: draw a static world slice (a 3x3 window of cells) from
 * index.u2pk / cells.u2pk / tex.u2pk produced by psp_convert region convert.
 *
 * The cell blobs have no textures of their own: their batches index the shared
 * tex.u2pk loaded once into the Textures pool. Vertices are re-cut from the
 * file's s16 format into GE float vertices exactly as car.c does (a real
 * PSP-1000 reads 16-bit fixed positions differently from PPSSPP).
 */
#ifndef OPENUG2_RUNTIME_WORLD_H
#define OPENUG2_RUNTIME_WORLD_H

#include <stdint.h>
#include <stddef.h>
#include "pool.h"
#include "u2pk.h"

#define WORLD_WIN 3            /* load a 3x3 cell window */

typedef struct {
	uint8_t  *blob;            /* the cell's U2PK bytes (POOL_WORLD) */
	uint32_t  len;
	uint8_t  *vertf;           /* float-cut vertices (POOL_WORLD) */
	U2pkView  v;
	int       cx, cy;
} WorldCell;

typedef struct {
	Pool     *pw, *pt;         /* POOL_WORLD, POOL_TEXTURES (borrowed) */

	/* region index — resident at the base of POOL_WORLD */
	uint8_t  *idxbuf;          /* index.u2pk bytes */
	uint32_t  idxlen;
	U2pkView  iv;
	const uint8_t *celltab;    /* ncell x 24 */
	int       gw, gh;          /* grid dims */
	float     x0, y0;          /* grid origin (world metres) */
	int       ncell;
	size_t    world_floor;     /* POOL_WORLD->used just past the index: the
	                            * level world_load_window resets back to, so the
	                            * index survives a window reload but the transient
	                            * blobs do not. */

	/* shared textures */
	uint8_t  *texbuf;          /* tex.u2pk bytes (POOL_TEXTURES) */
	uint32_t  texlen;
	U2pkView  tv;

	char      cells_path[300]; /* absolute path to cells.u2pk */

	/* loaded window */
	WorldCell win[WORLD_WIN * WORLD_WIN];
	int       nwin;
	int       win_cx, win_cy;  /* centre cell of the loaded window */
	int       have_win;
} World;

/* Load dir/index.u2pk and dir/tex.u2pk, remembering dir/cells.u2pk. The index
 * is parked at the base of `pworld` (POOL_WORLD) and the texture file fills
 * `ptex` (POOL_TEXTURES). Returns 1 on success. */
int  world_open(World *w, Pool *pworld, Pool *ptex, const char *region_dir);
/* Synchronously (re)load the 3x3 cell window centred on the cell containing
 * world (x,y): reset POOL_WORLD to the index floor, seek+read each cell blob
 * out of cells.u2pk, and re-cut its s16 vertices into GE float verts. Logs the
 * load cost as LOAD us=<n>. Returns 1 if the window loaded any cells. */
int  world_load_window(World *w, float x, float y);
/* Cell index (cy*gw+cx) containing a world position, or -1. */
int  world_cell_at(const World *w, float x, float y);
/* Highest COLLIDE surface at or below z_from under world (x,y), over the
 * currently loaded 3x3 window (re-read fresh each call, never a cached blob
 * pointer). Fills *z_out and a unit up-facing n_out[3]; returns 1 on a hit.
 * The caller reloads the window when the centre cell changes; the drive spine
 * does so before each tick. Mirrors host_world_ground on the host. */
int  world_ground_at(const World *w, float x, float y, float z_from,
                     float *z_out, float n_out[3]);
/* Draw the loaded window under the current VIEW/PROJECTION (camera already
 * set), enabling backface culling. Returns the number of batches submitted. */
int  world_draw(const World *w);

#endif
