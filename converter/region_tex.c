/* region_tex.c - see region_tex.h. Reuses the car converter's T8 path (median-
 * cut + swizzle, CLUT byte order GU_PSM_8888 = 0xAABBGGRR) but resolves each
 * key through the packs the loaded region already holds in memory, in the order
 * the game's world renderer uses: the region STREAM TPKs, then LOC4DYNTEX (car
 * offset-slot path), then the master and common packs. GLOBALB is deliberately
 * NOT consulted here - it carries car art, and no L4RH ground key lives in it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "region_tex.h"
#include "region_load.h"   /* world.h -> nfsu2.h: N2Tex, N2Tpk, n2_tpk_*, n2_load_car_tex_by_key */
#include "quant.h"
#include "swizzle.h"
#include "u2pk.h"

/* Resolve one texture key through the packs a loaded region keeps alive. Mirrors
 * main.c's per-mesh track-texture lookup. Safe to call with a reused N2Tex:
 * n2_tpk_decode zeroes its output, so a miss never leaves a stale pointer. */
static int decode_world_key(WorldNeighborhood *nb, uint32_t key, N2Tex *t)
{
	if (!nb || !key) return 0;
	for (int r = 0; r < nb->nreg; r++) {
		WRegion *g = &nb->rgn[r];
		if (g->data && n2_tpk_decode(g->data, g->len, g->tpk, key, t))
			return 1;
	}
	if (nb->loc4 && n2_load_car_tex_by_key(nb->loc4, nb->loc4len, key, t))
		return 1;
	if (nb->master &&
	    n2_tpk_decode(nb->master, nb->masterlen, nb->mastertpk, key, t))
		return 1;
	if (nb->common &&
	    n2_tpk_decode(nb->common, nb->commonlen, nb->commontpk, key, t))
		return 1;
	return 0;
}

/* Encode one key into `t`. On decode failure t becomes a 16x8 white T8 (single
 * opaque CLUT entry): white texels MODULATE to the baked vertex colour, so an
 * undecodable key renders exactly as the white-placeholder slice did. */
static void encode_one(WorldNeighborhood *nb, RTex *t)
{
	N2Tex src;
	int maxdim = t->ground ? 128 : 64;
	memset(&src, 0, sizeof src);
	memset(&t->clut[0], 0, sizeof t->clut);
	t->data = 0;
	t->size = 0;
	t->nclut = 0;
	t->encoded = 0;

	int dec = decode_world_key(nb, t->key, &src);
	if (dec && src.w > 0 && src.h > 0 && src.rgb) {
		int w = src.w, h = src.h;
		size_t np = (size_t)w * h;
		uint32_t *px = (uint32_t *)malloc(np * sizeof *px);
		if (px) {
			int has_alpha = src.alpha != 0, binary = 1, hole = 0;
			for (size_t i = 0; i < np; i++) {
				unsigned a = has_alpha ? src.alpha[i] : 255;
				if (a < 128) hole = 1;
				if (a != 0 && a != 255) binary = 0;
				px[i] = ((uint32_t)a << 24) | ((uint32_t)src.rgb[i * 3] << 16) |
				        ((uint32_t)src.rgb[i * 3 + 1] << 8) | src.rgb[i * 3 + 2];
			}
			int mode = (has_alpha && hole) ? (binary ? U2PK_MODE_CUTOUT
			                                         : U2PK_MODE_BLEND)
			                               : U2PK_MODE_OPAQUE;
			if (img_halve_to(&px, &w, &h, maxdim)) {
				int pw = (w + 15) & ~15, ph = (h + 7) & ~7;
				if (pw < 16) pw = 16;
				if (ph < 8) ph = 8;
				uint32_t *pad = (uint32_t *)malloc((size_t)pw * ph * sizeof *pad);
				if (pad) {
					for (int y = 0; y < ph; y++) {
						int sy = y < h ? y : h - 1;
						for (int x = 0; x < pw; x++) {
							int sx = x < w ? x : w - 1;
							pad[(size_t)y * pw + x] = px[(size_t)sy * w + sx];
						}
					}
					uint32_t pal[256];
					uint8_t *idx = (uint8_t *)malloc((size_t)pw * ph);
					int nq = idx ? quant_mediacut(pad, pw * ph, 256, pal, idx) : 0;
					if (nq > 0) {
						t->w = pw; t->h = ph; t->mode = mode; t->nclut = nq;
						t->size = (size_t)pw * ph;
						t->data = (uint8_t *)malloc(t->size);
						if (t->data) {
							for (int i = 0; i < nq; i++) {
								int a = (int)((pal[i] >> 24) & 0xff);
								if (mode == U2PK_MODE_OPAQUE) a = 255;
								else if (mode == U2PK_MODE_CUTOUT) a = a >= 128 ? 255 : 0;
								uint32_t rgb = pal[i] & 0xffffffu;   /* 0xRRGGBB */
								uint32_t rr = (rgb >> 16) & 0xff, gg = (rgb >> 8) & 0xff,
								         bb = rgb & 0xff;
								t->clut[i] = ((uint32_t)a << 24) | (bb << 16) |
								             (gg << 8) | rr;          /* AABBGGRR */
							}
							swizzle(t->data, idx, (unsigned)pw, (unsigned)ph);
							t->encoded = 1;
						}
					}
					free(idx);
					free(pad);
				}
			}
			free(px);
		}
	}
	free(src.rgb);
	free(src.alpha);
	free(src.dxt);

	if (!t->encoded) {
		fprintf(stderr, "region_tex: key %08x (ground=%d) not encoded: "
		        "decode=%d w=%d h=%d rgb=%d\n", (unsigned)t->key, t->ground,
		        dec, src.w, src.h, src.rgb != 0);
		/* 16x8 white T8, one opaque white CLUT entry, zero indices. */
		t->w = 16; t->h = 8; t->mode = U2PK_MODE_OPAQUE; t->nclut = 1;
		t->clut[0] = 0xffffffffu;
		t->size = (size_t)t->w * t->h;
		t->data = (uint8_t *)calloc(t->size, 1);
		if (!t->data) return;
		t->encoded = 0;
	}
}

static uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

int region_tex_write(WorldNeighborhood *nb, RTex *tex, int n,
                     const char *outpath, long long *bytes_out,
                     char *err, int errn)
{
	int i, ncluts = 0, nsect = 1;   /* TEXTURES always; CLUTS if any */
	uint32_t *clutdata = 0;
	FILE *f;
	long long total;

	if (err) err[0] = 0;
	for (i = 0; i < n; i++) tex[i].clut_slot = -1;

	for (i = 0; i < n; i++) {
		encode_one(nb, &tex[i]);
		if (!tex[i].data) {
			if (err) snprintf(err, errn, "texture %u out of memory", (unsigned)i);
			for (int k = 0; k <= i; k++) free(tex[k].data);
			return 0;
		}
	}

	/* Dedup CLUTs by content; every T8 references one slot. */
	for (i = 0; i < n; i++) {
		int dup = -1;
		for (int c = 0; c < ncluts; c++)
			if (!memcmp((uint8_t *)clutdata + (size_t)c * 1024, tex[i].clut,
			            (size_t)tex[i].nclut * 4)) { dup = c; break; }
		if (dup >= 0) { tex[i].clut_slot = dup; continue; }
		uint32_t *c = (uint32_t *)realloc(clutdata,
		                                  (size_t)(ncluts + 1) * 256 * 4);
		if (!c) {
			if (err) snprintf(err, errn, "clut out of memory");
			for (int k = 0; k < n; k++) free(tex[k].data);
			free(clutdata);
			return 0;
		}
		clutdata = c;
		memset(clutdata + (size_t)ncluts * 256, 0, 1024);
		memcpy(clutdata + (size_t)ncluts * 256, tex[i].clut,
		       (size_t)tex[i].nclut * 4);
		tex[i].clut_slot = ncluts++;
	}
	if (ncluts) nsect = 2;

	uint32_t tex_at = 32 + 16 * (uint32_t)nsect;
	uint32_t clut_at = ncluts ? align16(tex_at + (uint32_t)n * 32) : 0;
	uint32_t data_at = align16(clut_at + (uint32_t)ncluts * 1024);

	/* Assign each texture's absolute data offset (T8 sizes are multiples of 128,
	 * so each stays 16-byte aligned without extra padding). */
	uint32_t cur = data_at;
	int nc = n > 0 ? n : 1;
	U2pkTexHeader *th = (U2pkTexHeader *)calloc((size_t)nc, sizeof *th);
	if (!th) {
		if (err) snprintf(err, errn, "tex header OOM");
		for (int k = 0; k < n; k++) free(tex[k].data);
		free(clutdata);
		return 0;
	}
	for (i = 0; i < n; i++) {
		th[i].w = (uint16_t)tex[i].w;
		th[i].h = (uint16_t)tex[i].h;
		th[i].psm = U2PK_PSM_T8;
		th[i].swizzled = 1;
		th[i].data_offset = cur;
		th[i].data_size = (uint32_t)tex[i].size;
		th[i].clut = (int32_t)tex[i].clut_slot;
		cur += (uint32_t)tex[i].size;
	}
	total = (long long)cur;

	U2pkHeader hd;
	memset(&hd, 0, sizeof hd);
	memcpy(hd.magic, U2PK_MAGIC, 4);
	hd.version = U2PK_VERSION;
	hd.kind = U2PK_KIND_REGION;
	hd.section_count = (uint32_t)nsect;
	hd.total_size = (uint32_t)total;

	U2pkSection sect[2];
	sect[0].type = U2PK_S_TEXTURES;
	sect[0].offset = tex_at;
	sect[0].size = (uint32_t)n * 32;
	sect[0].count = (uint32_t)n;
	if (ncluts) {
		sect[1].type = U2PK_S_CLUTS;
		sect[1].offset = clut_at;
		sect[1].size = (uint32_t)ncluts * 1024;
		sect[1].count = (uint32_t)ncluts;
	}

	f = fopen(outpath, "wb");
	if (!f) {
		if (err) snprintf(err, errn, "cannot write %s", outpath);
		free(th);
		for (int k = 0; k < n; k++) free(tex[k].data);
		free(clutdata);
		return 0;
	}
	fwrite(&hd, sizeof hd, 1, f);
	fwrite(sect, 16, (size_t)nsect, f);
	fseek(f, tex_at, SEEK_SET);
	fwrite(th, 32, (size_t)nc, f);
	if (ncluts) { fseek(f, (long)clut_at, SEEK_SET); fwrite(clutdata, 1024, (size_t)ncluts, f); }
	for (i = 0; i < n; i++) { fseek(f, (long)th[i].data_offset, SEEK_SET);
		fwrite(tex[i].data, 1, tex[i].size, f); }
	fclose(f);

	free(th);
	free(clutdata);
	for (int k = 0; k < n; k++) { free(tex[k].data); tex[k].data = 0; }
	if (bytes_out) *bytes_out = total;
	return 1;
}
