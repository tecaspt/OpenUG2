#ifndef OPENUG2_SHARED_SWIZZLE_H
#define OPENUG2_SHARED_SWIZZLE_H

#include <stdint.h>

/* Both require width_bytes % 16 == 0 and height % 8 == 0. */
void swizzle(uint8_t *out, const uint8_t *in, unsigned width_bytes, unsigned height);
void unswizzle(uint8_t *out, const uint8_t *in, unsigned width_bytes, unsigned height);

#endif
