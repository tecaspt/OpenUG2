/* Texture swizzling for the PSP GE (T8 "swizzled" is our standard layout).
 *
 * The GE stores a texture as a linear array of 16x8 blocks: within a row of
 * blocks the stride is 128 bytes, and one block is 16 bytes wide by 8 tall.
 * Requirements: width_bytes % 16 == 0 and height % 8 == 0.
 */
#include <stdint.h>
#include "swizzle.h"

void swizzle(uint8_t *out, const uint8_t *in, unsigned width_bytes, unsigned height)
{
	unsigned rowblocks = width_bytes / 16;
	unsigned j, i;
	for (j = 0; j < height; j++) {
		for (i = 0; i < width_bytes; i++) {
			unsigned bx = i / 16, by = j / 8, x = i % 16, y = j % 8;
			out[(bx + by * rowblocks) * 128 + y * 16 + x] = in[j * width_bytes + i];
		}
	}
}

void unswizzle(uint8_t *out, const uint8_t *in, unsigned width_bytes, unsigned height)
{
	unsigned rowblocks = width_bytes / 16;
	unsigned j, i;
	for (j = 0; j < height; j++) {
		for (i = 0; i < width_bytes; i++) {
			unsigned bx = i / 16, by = j / 8, x = i % 16, y = j % 8;
			out[j * width_bytes + i] = in[(bx + by * rowblocks) * 128 + y * 16 + x];
		}
	}
}
