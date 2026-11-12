#ifndef OPENUG2_GFX_H
#define OPENUG2_GFX_H

#include <stdint.h>

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272

void gfx_init(void);
void gfx_begin(void);        /* sceGuStart + clear */
void gfx_end(void);          /* sceGuFinish + sceGuSync */
void gfx_swap(void);         /* vblank + sceGuSwapBuffers, updates draw buffer */

/* Relative VRAM offset of the buffer currently being drawn into. */
uint32_t gfx_draw_offset(void);

#endif
