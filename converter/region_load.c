/* region_load.c - see region_load.h. Drives the upstream world loader and
 * computes the cell-grid extent. GL/SDL-free: the only GL symbols the loader
 * references live in world_bind_textures / the texture cache, which we never
 * call, so they are stubbed at the bottom of this file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "region_load.h"

static char *xslurp(const char *path, long *len)
{
	*len = 0;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
	long n = ftell(f);
	if (n < 0) { fclose(f); return 0; }
	rewind(f);
	unsigned char *b = (unsigned char *)malloc((size_t)n + 1);
	if (!b) { fclose(f); return 0; }
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return 0; }
	fclose(f);
	b[n] = 0;
	*len = n;
	return (char *)b;
}

int region_open(Region *out, const char *dataroot, const char *trackname,
                const char **why)
{
	static char msg[512];
	memset(out, 0, sizeof *out);

	char troot[1024];
	snprintf(troot, sizeof troot, "%s/TRACKS", dataroot);

	/* Authored focus, exactly as main.c derives it: the companion region file
	 * is the stem (drop a leading "STREAM"), the stream file the full name. */
	const char *stem = !strncmp(trackname, "STREAM", 6) ? trackname + 6 : trackname;
	char cpath[1024], spath[1024];
	snprintf(cpath, sizeof cpath, "%s/%s.BUN", troot, stem);
	snprintf(spath, sizeof spath, "%s/%s.BUN", troot, trackname);
	long clen = 0, slen = 0;
	char *cb = xslurp(cpath, &clen);
	char *sb = xslurp(spath, &slen);
	float focus[2] = { 0.0f, 0.0f };
	if (cb && sb)
		winst_default_focus((const unsigned char *)cb, clen,
		                    (const unsigned char *)sb, slen, focus);
	free(cb); free(sb);
	out->focus[0] = focus[0];
	out->focus[1] = focus[1];

	World *w = (World *)calloc(1, sizeof *w);
	if (!w) { *why = "out of memory"; return 0; }
	out->w = w;

	/* view_radius 1400 m is the shipped resident policy (main.c
	 * resident_policy.resident_radius); it selects the home region plus the
	 * neighbouring set dressing around the authored focus. */
	WLoadOptions opt = { 1, focus[0], focus[1], 1400.0f, 0 };
	int nm = world_load_ex(w, troot, trackname, &opt);
	if (nm <= 0) {
		snprintf(msg, sizeof msg, "world_load_ex returned %d for %s", nm,
		         trackname);
		*why = msg;
		free(w);
		out->w = 0;
		return 0;
	}

	out->scene   = &w->neighborhood.scene;
	out->nmesh   = out->scene->count;

	/* Cell grid extent = the ROAD/TERRAIN bounding box. Props and set dressing
	 * are authored across the whole city, so the all-mesh bbox is meaningless;
	 * the ground is what defines a drivable slice. */
	float *bb = out->ground_bb;
	bb[0] = bb[2] = 1e30f;
	bb[1] = bb[3] = -1e30f;
	out->ground_tris = 0;
	int ground_meshes = 0;
	for (int i = 0; i < out->scene->count; i++) {
		const N2Mesh *m = &out->scene->meshes[i];
		if (!region_mesh_ground(m)) continue;
		ground_meshes++;
		out->ground_tris += m->nidx / 3;
		for (int v = 0; v < m->nverts; v++) {
			float x = m->verts[v * 5 + 0], y = m->verts[v * 5 + 1];
			if (x < bb[0]) bb[0] = x;
			if (x > bb[1]) bb[1] = x;
			if (y < bb[2]) bb[2] = y;
			if (y > bb[3]) bb[3] = y;
		}
	}
	if (!ground_meshes) {
		*why = "no ROAD/TERRAIN meshes loaded";
		return 0;
	}
	*why = 0;
	return 1;
}

void region_close(Region *r)
{
	if (!r) return;
	if (r->w) {
		world_neighborhood_free(&r->w->neighborhood);
		world_city_free(&r->w->city);
		free(r->w);
		r->w = 0;
	}
	r->scene = 0;
}

/* Stubs for the two GL symbols world.c names inside world_bind_textures and
 * the texture cache, which the converter never invokes. */
void rhi_texture_destroy(RhiTexture tex) { (void)tex; }
RhiTexture upload_tex_keyed(uint32_t key, const char *pack, const N2Tex *t)
{
	RhiTexture r;
	memset(&r, 0, sizeof r);
	(void)key; (void)pack; (void)t;
	return r;
}
