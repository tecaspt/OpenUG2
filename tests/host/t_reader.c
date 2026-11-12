/* Build a valid synthetic car file, then (a) prove u2pk_open rejects bad magic,
 * bad version, bad total_size and every truncation, and (b) never crashes or
 * reads out of bounds on malformed input. Run under ASan to catch the reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u2pk.h"

#define FILE_LEN 1616u

/* One batch spanning 3 verts / 3 indices; texture 0; opaque. */
static void fill_batch(U2pkBatch *b)
{
	memset(b, 0, sizeof *b);
	b->first_vertex = 0;
	b->vertex_count = 3;
	b->first_index = 0;
	b->index_count = 3;
	b->texture = 0;
	b->mode = U2PK_MODE_OPAQUE;
	for (int k = 0; k < 3; k++) { b->pos_scale[k] = 1.0f; b->pos_offset[k] = 0.0f; }
	for (int k = 0; k < 2; k++) { b->uv_scale[k] = 1.0f; b->uv_offset[k] = 0.0f; }
}

static void build(uint8_t *f)
{
	memset(f, 0, FILE_LEN);
	U2pkHeader h;
	memset(&h, 0, sizeof h);
	memcpy(h.magic, U2PK_MAGIC, 4);
	h.version = U2PK_VERSION;
	h.kind = U2PK_KIND_CAR;
	h.section_count = 6;
	h.total_size = FILE_LEN;
	memcpy(f, &h, sizeof h);

	U2pkSection tab[6] = {
		{ U2PK_S_BATCHES,  128u, 192u, 3u },
		{ U2PK_S_VERTS,    320u,  48u, 3u },
		{ U2PK_S_INDICES,  368u,  16u, 3u },
		{ U2PK_S_TEXTURES, 384u,  32u, 1u },
		{ U2PK_S_CLUTS,    480u, 1024u, 1u },
		{ U2PK_S_CARINFO,  1504u, 112u, 1u },
	};
	memcpy(f + sizeof(U2pkHeader), tab, sizeof tab);

	for (int i = 0; i < 3; i++) {
		U2pkBatch b;
		fill_batch(&b);
		memcpy(f + 128u + (unsigned)i * 64u, &b, sizeof b);
	}

	U2pkVertex v[3];
	for (int i = 0; i < 3; i++) {
		memset(&v[i], 0, sizeof v[i]);
		v[i].u = (uint16_t)(i * 4000);
		v[i].v = (uint16_t)(i * 5000);
		v[i].color = 0xffffu;
		v[i].x = (int16_t)(i * 100 - 100);
		v[i].y = (int16_t)(i * 100 - 100);
		v[i].z = (int16_t)(i * 100 - 100);
	}
	memcpy(f + 320u, v, sizeof v);

	uint16_t idx[3] = { 0, 1, 2 };
	memcpy(f + 368u, idx, sizeof idx);

	U2pkTexHeader t;
	memset(&t, 0, sizeof t);
	t.w = 16; t.h = 8;
	t.psm = U2PK_PSM_T8;
	t.swizzled = 1;
	t.data_offset = 416u;                 /* inside the TEXTURES section slack */
	t.data_size = 64u;                    /* 416 + 64 = 480, still < CLUTS */
	t.clut = 0;
	memcpy(f + 384u, &t, sizeof t);

	U2pkCarInfo c;
	memset(&c, 0, sizeof c);
	c.lod_count = 2;
	c.batch_first[0] = 0; c.batch_count[0] = 1;   /* batch 0 = player */
	c.batch_first[1] = 1; c.batch_count[1] = 1;   /* batch 1 = AI */
	c.wheel_batch_first = 2; c.wheel_batch_count = 1;
	c.wheel_pos[0][0] = -0.4f; c.wheel_pos[0][2] = 0.3f;   /* finite */
	c.wheel_radius = 0.3f;
	c.body_box[0] = 0.0f; c.body_box[1] = 1.0f;
	c.body_box[2] = 0.0f; c.body_box[3] = 1.0f;
	c.body_box[4] = 0.0f; c.body_box[5] = 1.0f;
	memcpy(f + 1504u, &c, sizeof c);
}

/* Flip a byte and require rejection; the reader must not read past `len`. */
static int reject_variant(uint8_t *f, int which)
{
	uint8_t snap[FILE_LEN];
	memcpy(snap, f, FILE_LEN);
	U2pkView v;
	int ok = 0;

	switch (which) {
	case 0: f[0] ^= 0x20; ok = u2pk_open(f, FILE_LEN, &v); break;      /* bad magic */
	case 1: { uint16_t bad = 999; memcpy(f + 4, &bad, 2);              /* bad version */
	          ok = u2pk_open(f, FILE_LEN, &v); } break;
	case 2: { uint32_t bad = FILE_LEN + 32; memcpy(f + 8, &bad, 4);    /* bad size */
	          ok = u2pk_open(f, FILE_LEN, &v); } break;
	case 3: { U2pkBatch b; fill_batch(&b); b.texture = 5;              /* tex oob */
	          memcpy(f + 128u, &b, sizeof b); ok = u2pk_open(f, FILE_LEN, &v); } break;
	case 4: { U2pkBatch b; fill_batch(&b); b.pos_scale[0] = 0.0f;      /* scale not >0 */
	          memcpy(f + 128u, &b, sizeof b); ok = u2pk_open(f, FILE_LEN, &v); } break;
	case 5: { U2pkCarInfo c; memcpy(&c, f + 1504u, sizeof c);          /* lod_count != 2 */
	          c.lod_count = 3; memcpy(f + 1504u, &c, sizeof c);
	          ok = u2pk_open(f, FILE_LEN, &v); } break;
	case 6: { U2pkCarInfo c; memcpy(&c, f + 1504u, sizeof c);          /* ranges overlap */
	          c.batch_count[0] = 3; memcpy(f + 1504u, &c, sizeof c);
	          ok = u2pk_open(f, FILE_LEN, &v); } break;
	default: break;
	}
	memcpy(f, snap, FILE_LEN);
	return ok == 0;
}

int main(void)
{
	uint8_t *f = malloc(FILE_LEN);
	U2pkView v;
	if (!f) { printf("FAIL alloc\n"); return 1; }

	build(f);
	if (!u2pk_open(f, FILE_LEN, &v)) {
		printf("FAIL valid synthetic car rejected: %s\n", v.err ? v.err : "?");
		return 1;
	}
	if (v.nbatch != 3 || v.nvert != 3 || v.nindex != 3 || v.ntex != 1 || v.nclut != 1) {
		printf("FAIL view counts wrong (b=%u v=%u i=%u t=%u c=%u)\n",
		       v.nbatch, v.nvert, v.nindex, v.ntex, v.nclut);
		return 1;
	}

	static const char *why[] = { "bad magic", "bad version", "bad total_size",
	                             "batch texture out of range", "pos_scale <= 0",
	                             "lod_count != 2", "car batch ranges overlap" };
	for (int k = 0; k < 7; k++)
		if (!reject_variant(f, k)) {
			printf("FAIL accepted a %s file\n", why[k]);
			return 1;
		}

	/* Truncation ladder: every length below FILE_LEN must be rejected without
	 * reading past the truncated buffer (ASan guards that). */
	for (uint32_t L = 0; L < FILE_LEN; L++)
		if (u2pk_open(f, L, &v)) {
			printf("FAIL accepted a file truncated to %u bytes\n", L);
			return 1;
		}

	free(f);
	printf("t_reader PASS (valid accepted; 7 corruption classes + all truncations rejected)\n");
	return 0;
}
