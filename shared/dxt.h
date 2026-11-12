#ifndef OPENUG2_SHARED_DXT_H
#define OPENUG2_SHARED_DXT_H

#include <stdint.h>

/* Sizes assume w and h are multiples of 4; buffers are w*h/2 bytes (DXT1). */
void dxt1_pc_to_psp(uint8_t *out, const uint8_t *in, unsigned w, unsigned h);
void dxt1_psp_to_pc(uint8_t *out, const uint8_t *in, unsigned w, unsigned h);
void dxt_color5650_to_rgba(uint32_t *dst, uint16_t c);

#endif
