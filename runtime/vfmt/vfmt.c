/* Vertex-format probe, built as its own EBOOT so it cannot overwrite the game.
 *
 * The U2PK vertex encoding (docs/U2PK.md) only works if the GE reads a 12-byte
 * [u16 u, u16 v, u16 color5650, s16 x, y, z] vertex the way the document says.
 * M0 proved the T8 + CLUT and DXT1 byte orders, but with 32-bit float vertices
 * and no index buffer, so three things were still unmeasured: the divisor behind
 * GU_VERTEX_16BIT / GU_TEXTURE_16BIT, the GU_COLOR_5650 bit order, and whether
 * GU_INDEX_16BIT walks a 12-byte stride from a 4-byte-aligned base.
 *
 * Four 1 m x 1 m quads, 2 m apart, centred 4 m from the camera. With a 60 deg
 * vertical FOV the screen covers 2*4*tan(30) = 4.619 m in 272 px, so 58.9 px
 * per metre either way: each quad is ~59 px on a side, centred at screen x =
 * 240 + cx*58.9 for cx = -3, -1, 1, 3, and screen y 136. tests/psp/vfmt.sh
 * samples those pixels and prints the verdict:
 *
 *   Q1 solid 0xF800  -> red iff R is in the low 5 bits of 5650. Its measured
 *                       pixel width is the position divisor: 59 px for s16/32768,
 *                       ~120000 px (off screen) for s16/16.
 *   Q2 solid 0x07E0  -> green iff G is the middle 6 bits.
 *   Q3 T8 texture    -> which texture corner lands on which screen corner, i.e.
 *                       the UV divisor and direction, and whether texture row 0
 *                       (magenta marker) or row 15 (cyan) is the top edge - that
 *                       decides if the converter has to flip rows.
 *   Q4 = Q3's bytes, but the vertex array is based 12 bytes into its buffer, so
 *      the GE reads a 12-byte stride from a 4-byte-aligned (not 16) address.
 */
#include <pspuser.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "pool.h"
#include "gfx.h"
#include "shot.h"
#include "swizzle.h"
#include "u2pk.h"

#define TEXW 16
#define FRAMES 5
#define SHOT_AT 2

static unsigned char __attribute__((aligned(16))) tex_lin[TEXW * TEXW];
static unsigned char __attribute__((aligned(16))) tex_swz[TEXW * TEXW];
static unsigned int __attribute__((aligned(16))) clut[8];

/* Four quads, each index run padded to 8 u16 so every run starts 16-byte
 * aligned - the same rule the converter uses for real batches. */
static uint16_t __attribute__((aligned(16))) idx[4 * 8];
static unsigned char __attribute__((aligned(16))) vbuf[3 * 4 * 12];
static unsigned char __attribute__((aligned(16))) vodd[12 + 4 * 12];

#define CL_WHITE 1
#define CL_BLUE 2
#define CL_RED 3
#define CL_GREEN 4
#define CL_ROW0 5    /* texture row 0;  0xffff00ff = magenta in 0xAABBGGRR */
#define CL_ROW15 6   /* texture row 15; 0xff00ffff = yellow  in 0xAABBGGRR */

/* The batch scale, deliberately not 1.0 so the model matrix carries a real
 * pos_scale: the quad is 0.25 m of local half-extent, which QSCALE turns into
 * the 1 m side the pixel measurements below assume. */
#define QSCALE 2.0f

/* metres = s16/32768 * scale, so the stored integer is metres/scale*32768. */
static int16_t qpos(float metres, float scale)
{
	float v = metres / scale * 32768.0f;
	if (v > 32767.0f) v = 32767.0f;
	if (v < -32768.0f) v = -32768.0f;
	return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static void put(U2pkVertex *v, float x, float z, uint16_t u, uint16_t w, uint16_t col)
{
	v->u = u;
	v->v = w;
	v->color = col;
	v->x = qpos(x, QSCALE);
	v->y = 0;
	v->z = qpos(z, QSCALE);
}

/* Unit quad centred on the origin in the XZ plane (Z is up). UV deliberately
 * runs to 65535 with TexScale 1.0: with uv = u16/32768 that asks for TWO
 * repeats, and measuring exactly two repeats across the quad is the direct
 * proof of the divisor, so the doubling is the point, not a mistake.
 * Coordinates are batch-local (already divided by QSCALE) because the s16
 * position field only reaches +-1.0 m of scaled distance; QSCALE turns the
 * +-0.5 m below back into 1 m. */
static void quad(U2pkVertex *v, uint16_t col)
{
	put(&v[0], -0.5f, -0.5f, 0, 0, col);
	put(&v[1], +0.5f, -0.5f, 65535, 0, col);
	put(&v[2], +0.5f, +0.5f, 65535, 65535, col);
	put(&v[3], -0.5f, +0.5f, 0, 65535, col);
}

/* The model matrix a real batch gets: pos_offset as a translate, pos_scale as a
 * scale, applied in that order so the s16 values stay inside their range. */
static void place(float cx)
{
	ScePspFVector3 off = { cx, 0.f, 0.f };
	ScePspFVector3 scl = { QSCALE, QSCALE, QSCALE };

	sceGumMatrixMode(GU_MODEL);
	sceGumLoadIdentity();
	sceGumTranslate(&off);
	sceGumScale(&scl);
}

static void build_tex(void)
{
	int x, y;

	for (y = 0; y < TEXW; y++) {
		for (x = 0; x < TEXW; x++) {
			unsigned char c = (x < TEXW / 2) ? CL_WHITE : CL_BLUE;
			if (y >= TEXW / 2)
				c = (x < TEXW / 2) ? CL_RED : CL_GREEN;
			if (y == 0) c = CL_ROW0;
			if (y == TEXW - 1) c = CL_ROW15;
			tex_lin[y * TEXW + x] = c;
		}
	}
	memset(clut, 0, sizeof(clut));
	clut[CL_WHITE] = 0xffffffffu;
	clut[CL_BLUE] = 0xffff0000u;    /* the GE reads CLUT words as 0xAABBGGRR */
	clut[CL_RED] = 0xff0000ffu;
	clut[CL_GREEN] = 0xff00ff00u;
	clut[CL_ROW0] = 0xffff00ffu;    /* magenta */
	clut[CL_ROW15] = 0xff00ffffu;   /* yellow */
	swizzle(tex_swz, tex_lin, TEXW, TEXW);
}

static void camera(void)
{
	ScePspFVector3 eye = { 0.f, -4.f, 0.f };
	ScePspFVector3 center = { 0.f, 0.f, 0.f };
	ScePspFVector3 up = { 0.f, 0.f, 1.f };

	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadIdentity();
	sceGumPerspective(60.f, 480.f / 272.f, 0.5f, 100.f);
	sceGumMatrixMode(GU_VIEW);
	sceGumLoadIdentity();
	sceGumLookAt(&eye, &center, &up);
	sceGumMatrixMode(GU_MODEL);
	sceGumLoadIdentity();
}

static void draw_solid(const U2pkVertex *verts, const uint16_t *indices)
{
	sceGuDisable(GU_TEXTURE_2D);
	sceGumDrawArray(GU_TRIANGLES,
	               GU_TEXTURE_16BIT | GU_COLOR_5650 | GU_VERTEX_16BIT |
	               GU_INDEX_16BIT | GU_TRANSFORM_3D,
	               6, indices, verts);
	sceGuEnable(GU_TEXTURE_2D);
}

static void draw_textured(const U2pkVertex *verts, const uint16_t *indices)
{
	sceGuEnable(GU_TEXTURE_2D);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
	sceGuClutLoad(1, clut);   /* one chunk of 8 entries */
	sceGuTexMode(GU_PSM_T8, 0, 0, 1);
	sceGuTexImage(0, TEXW, TEXW, TEXW, tex_swz);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuTexScale(1.0f, 1.0f);
	sceGuTexOffset(0.0f, 0.0f);
	sceGumDrawArray(GU_TRIANGLES,
	               GU_TEXTURE_16BIT | GU_COLOR_5650 | GU_VERTEX_16BIT |
	               GU_INDEX_16BIT | GU_TRANSFORM_3D,
	               6, indices, verts);
}

int main(int argc, char **argv)
{
	const U2pkVertex *q[4];
	int i, frame;

	plat_init(argc, argv);
	plat_setup_callbacks();
	plat_log("VFMTPROBE start sizeof(U2pkVertex)=%u", (unsigned)sizeof(U2pkVertex));

	for (i = 0; i < 4; i++) {
		uint16_t *t = idx + i * 8;
		t[0] = 0; t[1] = 1; t[2] = 2;
		t[3] = 0; t[4] = 2; t[5] = 3;
		t[6] = 0; t[7] = 0;   /* padding, never referenced */
	}
	q[0] = (const U2pkVertex *)vbuf;
	q[1] = (const U2pkVertex *)(vbuf + 4 * 12);
	q[2] = (const U2pkVertex *)(vbuf + 8 * 12);
	q[3] = (const U2pkVertex *)(vodd + 12);   /* 4-byte aligned, deliberately */
	quad((U2pkVertex *)q[0], 0xf800u);
	quad((U2pkVertex *)q[1], 0x07e0u);
	quad((U2pkVertex *)q[2], 0xffffu);
	memcpy((void *)q[3], q[2], 4 * sizeof(U2pkVertex));
	plat_log("VFMTPROBE bases %p %p %p %p (last is 12-byte offset)",
	         (const void *)q[0], (const void *)q[1], (const void *)q[2],
	         (const void *)q[3]);
	build_tex();
	sceKernelDcacheWritebackAll();

	gfx_init();
	for (frame = 1; frame <= FRAMES; frame++) {
		gfx_begin();
		camera();
		place(-3.0f);
		draw_solid(q[0], idx + 0);
		place(-1.0f);
		draw_solid(q[1], idx + 8);
		place(1.0f);
		draw_textured(q[2], idx + 16);
		place(3.0f);
		draw_textured(q[3], idx + 24);
		gfx_end();
		if (frame == SHOT_AT) {
			shot_write("vfmt");
			plat_log("VFMTPROBE shot at frame %d; Q1 red Q2 green Q3/Q4 textured",
			         frame);
			plat_log("VFMTPROBE expect quad centres at x=63,181,299,417 y=136");
		}
		gfx_swap();
	}
	plat_log("VFMTPROBE done");
	return 0;
}
