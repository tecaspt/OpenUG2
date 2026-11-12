/* DXT1 block byte order: PC vs the PSP GE.
 *
 * PC  block: [u16 c0][u16 c1][u32 indices]
 * PSP block: [u32 indices][u16 c0][u16 c1]
 * Blocks themselves stay in the same row-major order in both.
 */
#include <stdint.h>
#include <string.h>
#include "dxt.h"

#define DXT1_BLOCK 8

static uint32_t ld32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void st32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void dxt1_pc_to_psp(uint8_t *out, const uint8_t *in, unsigned w, unsigned h)
{
	unsigned blocks = (w / 4) * (h / 4);
	unsigned b;
	for (b = 0; b < blocks; b++) {
		const uint8_t *s = in + b * DXT1_BLOCK;
		uint8_t *d = out + b * DXT1_BLOCK;
		st32(d, ld32(s + 4));
		d[4] = s[0]; d[5] = s[1];
		d[6] = s[2]; d[7] = s[3];
	}
}

void dxt1_psp_to_pc(uint8_t *out, const uint8_t *in, unsigned w, unsigned h)
{
	unsigned blocks = (w / 4) * (h / 4);
	unsigned b;
	for (b = 0; b < blocks; b++) {
		const uint8_t *s = in + b * DXT1_BLOCK;
		uint8_t *d = out + b * DXT1_BLOCK;
		d[0] = s[4]; d[1] = s[5];
		d[2] = s[6]; d[3] = s[7];
		st32(d + 4, ld32(s));
	}
}

/* 5650 -> 8888, used by the DXT1 encoder in the converter. */
void dxt_color5650_to_rgba(uint32_t *dst, uint16_t c)
{
	uint32_t r = (c & 0x1f) << 3, g = ((c >> 5) & 0x3f) << 2, b = ((c >> 11) & 0x1f) << 3;
	dst[0] = r | (r >> 5);
	dst[1] = g | (g >> 6);
	dst[2] = b | (b >> 5);
	dst[3] = 255;
}
