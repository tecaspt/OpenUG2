/* Screenshot: read the framebuffer back through the uncached CPU view. */
#include <pspuser.h>
#include <pspge.h>
#include <stdio.h>
#include <string.h>
#include "shot.h"
#include "gfx.h"
#include "platform.h"

static unsigned char g_row[SCR_W * 3];

void shot_write(const char *name)
{
	uint16_t *px;
	const uint16_t *src;
	char path[300];
	char hdr[32];
	int fd, x, y, n;

	/* 0x40000000 gives the uncached view: we see what the GE actually wrote. */
	px = (uint16_t *)(((uint32_t)sceGeEdramGetAddr() | 0x40000000u) + gfx_draw_offset());

	snprintf(path, sizeof(path), "%s/%s.ppm", plat_dir(), name);
	fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (fd < 0) {
		plat_log("SHOT OPEN FAIL %s (%d)", path, fd);
		return;
	}

	n = snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", SCR_W, SCR_H);
	if (sceIoWrite(fd, hdr, n) != n)
		plat_log("SHOT HDR SHORT %s", path);

	for (y = 0; y < SCR_H; y++) {
		/* The draw buffer has a 512-pixel stride, not 480. */
		src = px + (size_t)y * BUF_W;
		for (x = 0; x < SCR_W; x++) {
			uint16_t p = src[x];
			g_row[x * 3 + 0] = (unsigned char)((p & 31) << 3);
			g_row[x * 3 + 1] = (unsigned char)(((p >> 5) & 63) << 2);
			g_row[x * 3 + 2] = (unsigned char)((p >> 11) & 31) << 3;
		}
		if (sceIoWrite(fd, g_row, SCR_W * 3) != SCR_W * 3) {
			plat_log("SHOT ROW SHORT at y=%d", y);
			break;
		}
	}

	sceIoClose(fd);
	plat_log("SHOT wrote %s (draw offset %#x)", path, gfx_draw_offset());
}
