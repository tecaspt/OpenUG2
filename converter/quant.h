/* RGBA8 image helpers for psp_convert: box-filter downscale and median-cut
 * quantisation. Standalone - the host test t_quant links this file alone.
 */
#ifndef OPENUG2_CONVERTER_QUANT_H
#define OPENUG2_CONVERTER_QUANT_H

#include <stdint.h>

/* Pixels are 0xAARRGGBB. */

/* Halves the image with a box filter, repeatedly, until neither side exceeds
 * maxdim. w/h are updated in place; *px is reallocated. A side that is already
 * within maxdim is still halved only if the other one is not, so an 8x512 image
 * becomes 8x8 rather than losing its width. Returns 1, or 0 on allocation
 * failure (the input is then left untouched). */
int img_halve_to(uint32_t **px, int *w, int *h, int maxdim);

/* Median-cut quantise of npix pixels into at most maxc palette entries.
 *
 * If the image has no more distinct colours than maxc, the palette *is* those
 * colours and every pixel round-trips exactly - real car skins have far more
 * than 256, but the 200-colour test image must not shift by a single channel
 * step, and median cut would move every box mean off the true colours.
 *
 * Otherwise: one box holds every pixel; repeatedly split the box with the
 * largest channel range along its widest channel at its median, until there are
 * maxc boxes or no box can be split; each entry is its box's mean. Every pixel
 * is then mapped to the nearest entry (squared distance over r,g,b,a).
 *
 * pal gets 4 bytes per entry (r,g,b,a) and idx one byte per pixel. Returns the
 * entry count, or 0 on failure. */
int quant_mediacut(const uint32_t *px, int npix, int maxc, uint32_t *pal,
                   uint8_t *idx);

#endif /* OPENUG2_CONVERTER_QUANT_H */
