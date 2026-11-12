/* GU setup. The init sequence is the standard pspsdk sample sequence (BSD). */
#include <pspuser.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include "gfx.h"
#include "platform.h"

static unsigned int __attribute__((aligned(16))) g_list[256 * 1024];

/* Relative VRAM offsets; GU_PSM_5650 is 2 bytes per pixel. */
static void *fbp0 = (void *)0;
static void *fbp1 = (void *)(BUF_W * SCR_H * 2);
static void *zbp = (void *)(2 * BUF_W * SCR_H * 2);

/* Starts as fbp0; a C initializer cannot name another static variable. */
static void *g_draw = (void *)0;

void gfx_init(void)
{
	sceGuInit();
	sceGuStart(GU_DIRECT, g_list);
	sceGuDrawBuffer(GU_PSM_5650, fbp0, BUF_W);
	sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
	sceGuDepthBuffer(zbp, BUF_W);
	sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
	sceGuViewport(2048, 2048, SCR_W, SCR_H);
	sceGuDepthRange(65535, 0);
	sceGuScissor(0, 0, SCR_W, SCR_H);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuShadeModel(GU_SMOOTH);
	sceGuDisable(GU_CULL_FACE);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
	plat_log("gfx: %dx%d stride %d, vram=%p", SCR_W, SCR_H, BUF_W, sceGeEdramGetAddr());
}

void gfx_begin(void)
{
	sceGuStart(GU_DIRECT, g_list);
	/* GE clear colour is 0xAABBGGRR. The intended background is the dark warm
	 * tone RGB(0x30,0x20,0x10); the byte-swapped 0xff302010 read as dark blue on
	 * real hardware (user report, 2026-11-12) while PPSSPP hid it. */
	sceGuClearColor(0xff102030);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void gfx_end(void)
{
	sceGuFinish();
	sceGuSync(0, 0);
}

void gfx_swap(void)
{
	sceDisplayWaitVblankStart();
	g_draw = sceGuSwapBuffers();
}

uint32_t gfx_draw_offset(void)
{
	return (uint32_t)g_draw;
}
