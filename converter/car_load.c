/* See car_load.h. Copied from OpenUG2 src/main.c, GL removed. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "car_load.h"

/* main.c:128-130 - the body-box seeds, used only when GLOBALB has no record. */
#define SEED_FRONTF 0.639f
#define SEED_REARF  0.597f
#define SEED_TRACKF 0.760f

/* main.c:373 load_global_car_data */
static unsigned char *load_global_car_data(const char *root, long *len)
{
	char path[1024];
	*len = 0;
	snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", root);
	unsigned char *data = n2_read_file(path, len);
	if (data)
		return data;
	snprintf(path, sizeof path, "%s/GLOBAL/GlobalB.lzc", root);
	long bytes = 0;
	unsigned char *packed = n2_read_file(path, &bytes);
	if (packed && bytes >= 16 && bytes <= INT_MAX && !memcmp(packed, "JDLZ", 4)) {
		uint32_t size = n2_u32(packed + 8);
		if (size && size < (1u << 28)) {
			data = (unsigned char *)malloc(size);
			if (data) {
				*len = n2_jdlz(packed, (int)bytes, data, (int)size);
				if (*len <= 0) {
					free(data);
					data = NULL;
					*len = 0;
				}
			}
		}
	}
	free(packed);
	return data;
}

/* main.c:395 load_car_texture_data. An empty local pack means the car's authored
 * skin keys live in the shared CARS pack. */
static unsigned char *load_car_texture_data(const char *path, const char *root,
                                            long *len, int *prelight)
{
	unsigned char *data = n2_read_file(path, len);
	*prelight = data && *len == 0;
	if (*prelight) {
		char shared[1024];
		snprintf(shared, sizeof shared, "%s/CARS/TEXTURES.BIN", root);
		long size = 0;
		unsigned char *pack = n2_read_file(shared, &size);
		if (pack && size > 0) {
			free(data);
			data = pack;
			*len = size;
		} else {
			free(pack);
		}
	}
	return data;
}

void car_close(Car *c)
{
	if (!c)
		return;
	n2_free_scene(&c->scene);
	free(c->geom);
	free(c->tpk);
	free(c->gt.blk);
	free(c->glob);
	memset(c, 0, sizeof *c);
}

int car_open(Car *c, const char *root, const char *name, const char **why)
{
	char path[1024];
	memset(c, 0, sizeof *c);
	snprintf(c->name, sizeof c->name, "%s", name);

	snprintf(path, sizeof path, "%s/CARS/%s/GEOMETRY.BIN", root, name);
	c->geom = n2_read_file(path, &c->geomlen);
	if (!c->geom || c->geomlen <= 0) {
		*why = "GEOMETRY.BIN missing or empty";
		car_close(c);
		return 0;
	}

	snprintf(path, sizeof path, "%s/CARS/%s/TEXTURES.BIN", root, name);
	c->tpk = load_car_texture_data(path, root, &c->tpklen, &c->prelight);
	c->nkeys = c->tpk ? n2_car_tex_keys(c->tpk, c->tpklen, c->keys, 2048) : 0;

	c->glob = load_global_car_data(root, &c->globlen);
	c->gt = c->glob ? n2_tpk_open(c->glob, c->globlen) : (N2Tpk){0};
	if (c->glob)
		c->nkeys += n2_tpk_keys(c->glob, c->gt, c->keys + c->nkeys,
		                       2048 - c->nkeys);

	/* Stock configuration: body kit 0, no aftermarket. */
	N2CarConfig cfg;
	memset(&cfg, 0, sizeof cfg);
	if (n2_load_car_colored(c->geom, c->geomlen, &c->scene, c->keys, c->nkeys,
	                        &cfg, c->prelight) <= 0) {
		*why = "no geometry in GEOMETRY.BIN";
		car_close(c);
		return 0;
	}

	c->stock_wheel = n2_car_prepare_wheels(&c->scene);
	if (c->stock_wheel >= 0)
		c->stock_tier = c->scene.meshes[c->stock_wheel].tierid;

	n2_car_profile(&c->scene, name, SEED_FRONTF, SEED_REARF, SEED_TRACKF,
	               n2_car_brake_radius(c->geom, 0, c->geomlen), &c->prof);

	/* Stance, exactly main.c:131 wheel_config_for: the exact factory axle/track
	 * from the GLOBALB car table when it has a record, else this car's own body
	 * box scaled by the fleet seeds. Hub Z is always the tyre mesh's own
	 * measured hub height, because that is the height the wheel geometry is
	 * modelled at; the wheel slices are drawn around the model origin. */
	N2WheelAttr wa;
	float fa, ra, ft, rt;
	if (n2_global_wheel_attr(c->glob, c->globlen, name, &wa)) {
		fa = wa.front_axle;
		ra = wa.rear_axle;
		ft = wa.front_track;
		rt = wa.rear_track;
		c->wheel_from_global = 1;
	} else {
		fa = c->prof.body[1] * SEED_FRONTF;
		ra = c->prof.body[0] * SEED_REARF;
		ft = rt = 2.0f * c->prof.body[3] * SEED_TRACKF;
	}
	const float axle[4] = { fa, fa, ra, ra };
	const float half[4] = { ft * .5f, -ft * .5f, rt * .5f, -rt * .5f };
	for (int k = 0; k < 4; k++) {
		c->hub[k][0] = axle[k];
		c->hub[k][1] = half[k];
		c->hub[k][2] = c->prof.hub_z;
	}
	c->wheel_radius = c->prof.wheel_r;

	if (c->prof.wheel_r <= 0.0f) {
		*why = "car has no measurable wheel radius";
		car_close(c);
		return 0;
	}
	return 1;
}

int car_tex(Car *c, uint32_t key, N2Tex *t)
{
	memset(t, 0, sizeof *t);
	if (!key)
		return 0;
	if (c->tpk && n2_load_car_tex_by_key(c->tpk, c->tpklen, key, t))
		return 1;
	if (c->glob && n2_tpk_decode(c->glob, c->globlen, c->gt, key, t))
		return 1;
	return 0;
}
