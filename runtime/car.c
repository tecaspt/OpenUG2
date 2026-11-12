/* See car.h. The U2PK file stores s16 positions (shared/u2pk.h); on load we
 * re-cut them into GE float vertices (GU_VERTEX_32BITF) and keep the batch's
 * pos_offset/pos_scale applied by the model matrix exactly as before, so the
 * world placement is unchanged but the position pipeline no longer depends on
 * how a given GE reads a 16-bit fixed vertex.
 *
 * Why: the fixed GU_VERTEX_16BIT path renders correctly in PPSSPP's software GE
 * but showed badly distorted geometry on a real PSP-1000 (user hardware report,
 * 2026-11-12) - PPSSPP and the ALLEGREX GE disagree on that rarely-used format's
 * interpretation. Float vertices are unambiguous on both, so the same value
 * (s16/32768, matching the emulator's fixed-vertex result and the U2PK doc
 * comment "metres = s16/32768 * pos_scale + pos_offset") is written as a float
 * and the model matrix does the rest. GU_TEXTURE_16BIT still drives UVs via
 * sceGuTexScale/sceGuTexOffset. */
#include <pspuser.h>
#include <pspgu.h>
#include <pspgum.h>
#include <string.h>
#include "car.h"
#include "platform.h"

#define VERTFMT (GU_TEXTURE_16BIT  | GU_COLOR_8888 | GU_VERTEX_32BITF | \
                 GU_INDEX_16BIT    | GU_TRANSFORM_3D)

/* Interleaved GE vertex: uv (u16,u16), colour (u32 8888), pos (float x,y,z) =
 * 20 B. The GE reads attributes in the fixed order texture, colour, position;
 * a 32-bit position must land on a 4-byte boundary, so the bytes before it
 * (uv 4 + colour 4) are forced to a multiple of 4 by using GU_COLOR_8888 rather
 * than GU_COLOR_5650 (2 bytes would leave the float misaligned at offset 6 -
 * a stride-18 layout reads as garbage even in PPSSPP, confirmed 2026-11-12). */
#define CAR_VSTRIDE 20

/* The file stores colour as GU_COLOR_5650, and the GE reads that format with R
 * in the LOW bits (see the U2pkVertex note; the raw u16 rendered red in the
 * old 5650 path only because of this). Expand to the GE's 8888 order, which is
 * likewise R-in-low (0xAABBGGRR, same as the CLUT and clear colour), so
 * MODULATE matches the 5650 path. Decoding R from the high bits swaps R/B. */
static uint32_t rgb565_to_8888(uint16_t c)
{
	unsigned r = (c & 0x1f) * 255 / 31;
	unsigned g = ((c >> 5) & 0x3f) * 255 / 63;
	unsigned b = ((c >> 11) & 0x1f) * 255 / 31;
	return 0xff000000u | (b << 16) | (g << 8) | r;
}

static const ScePspFVector3 g_mirror = { 1.0f, -1.0f, 1.0f }; /* FR/RR: left->right */

static int load_file(Pool *pool, const char *relpath, CarG *car)
{
	char path[300];
	int fd, sz, r;

	plat_path(path, (int)sizeof(path), relpath);
	fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (fd < 0) {
		plat_log("car: cannot open %s", path);
		return 0;
	}
	sz = sceIoLseek(fd, 0, PSP_SEEK_END);
	sceIoLseek(fd, 0, PSP_SEEK_SET);
	if (sz <= 0) {
		sceIoClose(fd);
		plat_log("car: %s is empty", path);
		return 0;
	}
	car->buf = (uint8_t *)pool_alloc(pool, (size_t)sz, 16);
	if (!car->buf) {
		sceIoClose(fd);
		plat_log("car: Cars pool cannot take %d bytes", sz);
		return 0;
	}
	r = sceIoRead(fd, car->buf, (unsigned int)sz);
	sceIoClose(fd);
	if (r != sz) {
		plat_log("car: short read %d of %d", r, sz);
		car->buf = 0;
		return 0;
	}
	car->len = (uint32_t)sz;
	return 1;
}

/* Re-cut every s16 vertex into a 20-byte GE float vertex: uv (u16,u16), colour
 * (u32 8888), position (3 x float). The uv bytes copy straight across; the
 * colour is expanded 5650 -> 8888; the position floats land at offset 8 (a
 * multiple of 4 for every vertex because the stride is 20), so a MIPS float
 * store is naturally aligned. */
static int build_float_verts(Pool *pool, CarG *car)
{
	uint32_t n = car->v.nvert;
	const U2pkVertex *s = car->v.vert;
	uint8_t *d;
	uint32_t i;

	car->vertf = (uint8_t *)pool_alloc(pool, (size_t)n * CAR_VSTRIDE, 16);
	if (!car->vertf) {
		plat_log("car: Cars pool cannot take %u float verts", (unsigned)n);
		return 0;
	}
	d = car->vertf;
	for (i = 0; i < n; i++) {
		uint8_t *o = d + (size_t)i * CAR_VSTRIDE;
		uint32_t col = rgb565_to_8888(s[i].color);
		memcpy(o, &s[i].u, 4);           /* u, v */
		memcpy(o + 4, &col, 4);          /* colour 8888 */
		memcpy(o + 8,  &(float){ (float)s[i].x / 32768.0f }, 4);
		memcpy(o + 12, &(float){ (float)s[i].y / 32768.0f }, 4);
		memcpy(o + 16, &(float){ (float)s[i].z / 32768.0f }, 4);
	}
	return 1;
}

int car_load(Pool *pool, const char *relpath, int lod, CarG *out)
{
	memset(out, 0, sizeof *out);
	if (!load_file(pool, relpath, out))
		return 0;
	/* The GE reads buf, the cluts and the texture data directly out of RAM, so
	 * the dirty lines the read left in the dcache must reach memory first. */
	sceKernelDcacheWritebackAll();
	if (!u2pk_open(out->buf, out->len, &out->v)) {
		plat_log("car: u2pk_open: %s", out->v.err ? out->v.err : "?");
		out->buf = 0;
		return 0;
	}
	if (out->v.hdr.kind != U2PK_KIND_CAR || !out->v.car) {
		plat_log("car: %s is not a car file (kind=%u)", relpath,
		         (unsigned)out->v.hdr.kind);
		out->buf = 0;
		return 0;
	}
	if (!build_float_verts(pool, out)) {
		out->buf = 0;
		out->vertf = 0;
		return 0;
	}
	/* The GE reads the float vertices straight out of RAM: push the dirty
	 * cache lines the CPU writes above down to memory first. */
	sceKernelDcacheWritebackAll();
	out->lod = lod ? 1 : 0;
	out->world[0] = out->world[1] = out->world[2] = 0.0f;
	out->yaw = 0.0f;
	plat_log("car: %s len=%u lod=%d batch=%u vert=%u idx=%u tex=%u clut=%u r=%.3f",
	         relpath, (unsigned)out->len, out->lod, (unsigned)out->v.nbatch,
	         (unsigned)out->v.nvert, (unsigned)out->v.nindex,
	         (unsigned)out->v.ntex, (unsigned)out->v.nclut,
	         out->v.car->wheel_radius);
	return 1;
}

static void setup_texture(const CarG *car, const U2pkBatch *b)
{
	const U2pkTexHeader *t;
	const uint8_t *data;

	if (b->texture < 0) {
		sceGuDisable(GU_TEXTURE_2D);
		return;
	}
	t = &car->v.tex[b->texture];
	data = car->v.buf + t->data_offset;
	sceGuEnable(GU_TEXTURE_2D);
	if (t->psm == U2PK_PSM_T8 && t->clut >= 0) {
		sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
		sceGuClutLoad(256 / 8, car->v.clut + (size_t)t->clut * 256);
	}
	sceGuTexMode(t->psm, 0, 0, t->swizzled);
	sceGuTexImage(0, t->w, t->h, t->w, data);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuTexScale(b->uv_scale[0], b->uv_scale[1]);
	sceGuTexOffset(b->uv_offset[0], b->uv_offset[1]);
}

static void apply_mode(uint32_t mode)
{
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
	if (mode == U2PK_MODE_CUTOUT) {
		sceGuEnable(GU_ALPHA_TEST);
		sceGuAlphaFunc(GU_GREATER, 127, 0xff);
	} else if (mode == U2PK_MODE_BLEND) {
		sceGuEnable(GU_BLEND);
		sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
		sceGuDepthMask(GU_TRUE);
	}
}

/* Draw one batch after the caller has pushed the car/wheel transform. */
static void draw_batch(const CarG *car, const U2pkBatch *b)
{
	sceGumTranslate((const ScePspFVector3 *)b->pos_offset);
	sceGumScale((const ScePspFVector3 *)b->pos_scale);
	setup_texture(car, b);
	apply_mode(b->mode);
	sceGumDrawArray(GU_TRIANGLES, VERTFMT, b->index_count,
	                (unsigned int *)(car->v.index + b->first_index),
	                car->vertf + (size_t)b->first_vertex * CAR_VSTRIDE);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuDepthMask(GU_FALSE);
}

/* Draws batches [first, first+count), opaque then cutout then blend, under the
 * car transform; when wheel_i >= 0 the wheel hub transform is added too. */
static void draw_run(const CarG *car, uint32_t first, uint32_t count, int wheel_i)
{
	const ScePspFVector3 *world = (const ScePspFVector3 *)car->world;
	int pass, k;

	for (pass = U2PK_MODE_OPAQUE; pass <= U2PK_MODE_BLEND; pass++) {
		for (k = 0; k < (int)count; k++) {
			const U2pkBatch *b = &car->v.batch[first + k];
			if ((int)b->mode != pass)
				continue;
			sceGumMatrixMode(GU_MODEL);
			sceGumLoadIdentity();
			sceGumTranslate(world);
			if (car->yaw != 0.0f)
				sceGumRotateZ(car->yaw);
			if (wheel_i >= 0) {
				const ScePspFVector3 *hub =
				    (const ScePspFVector3 *)car->v.car->wheel_pos[wheel_i];
				sceGumTranslate(hub);
				if (wheel_i == 1 || wheel_i == 3)   /* FR, RR mirror across X */
					sceGumScale(&g_mirror);
			}
			draw_batch(car, b);
		}
	}
}

void car_draw(const CarG *car)
{
	const U2pkCarInfo *ci = car->v.car;
	int lod = car->lod;
	int w;

	draw_run(car, ci->batch_first[lod], ci->batch_count[lod], -1);
	for (w = 0; w < 4; w++)
		draw_run(car, ci->wheel_batch_first, ci->wheel_batch_count, w);
}
